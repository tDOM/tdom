/*----------------------------------------------------------------------------
|   Copyright (C) 1999  Jochen C. Loewer (loewerj@hotmail.com)
+-----------------------------------------------------------------------------
|
|   $Header$
|
|
|
|   The contents of this file are subject to the Mozilla Public License
|   Version 1.1 (the "License"); you may not use this file except in
|   compliance with the License. You may obtain a copy of the License at
|   http://www.mozilla.org/MPL/
|
|   Software distributed under the License is distributed on an "AS IS"
|   basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See the
|   License for the specific language governing rights and limitations
|   under the License.
|
|   The Original Code is tDOM.
|
|   The Initial Developer of the Original Code is Jochen Loewer
|   Portions created by Jochen Loewer are Copyright (C) 1998, 1999
|   Jochen Loewer. All Rights Reserved.
|
|   Contributor(s):
|       July00  Zoran Vasiljevic  Added this file.
|
|   $Log$
|   Revision 1.1  2002/02/22 01:05:35  rolf
|   Initial revision
|
|
|
|   written by Zoran Vasiljevic
|   July 12, 2000
|
\---------------------------------------------------------------------------*/


/*----------------------------------------------------------------------------
|   Includes
|
\---------------------------------------------------------------------------*/
#include <dom.h>
#include <tcl.h>
#include <nodecmd.h>


/*----------------------------------------------------------------------------
|   Types
|
|   This structure represents one stack slot. The stack itself
|   is implemented as double-linked-list of following structures.
|
\---------------------------------------------------------------------------*/
typedef struct StackSlot {
    void             *element;   /* the stacked element */
    struct StackSlot *nextPtr;   /* next link */
    struct StackSlot *prevPtr;   /* previous link */
} StackSlot;


/*----------------------------------------------------------------------------
|   Beginning of the stack and current element pointer are local to 
|   current thread and also local to this file.
|
\---------------------------------------------------------------------------*/ 
typedef struct LocalThreadSpecificData {
    StackSlot *elementStack;
    StackSlot *currentSlot;
} LocalThreadSpecificData;


/*----------------------------------------------------------------------------
|   Macros
|
\---------------------------------------------------------------------------*/
#ifndef TCL_THREADS

#   define LOCAL_TSD_KEY(a) a
    static LocalThreadSpecificData dataKey;
#   define EXITHANDLER(finalize, arg) Tcl_CreateExitHandler(finalize, arg)

#else

#   define LOCAL_TSD_KEY(a) (LocalThreadSpecificData*)\
            Tcl_GetThreadData((a), sizeof(LocalThreadSpecificData))
#   define EXITHANDLER(finalize, arg) Tcl_CreateThreadExitHandler(finalize, arg)
    static Tcl_ThreadDataKey dataKey;

#endif



/*----------------------------------------------------------------------------
|   Forward declarations
|
\---------------------------------------------------------------------------*/
static void   StackFinalize _ANSI_ARGS_((ClientData clientData));



/*----------------------------------------------------------------------------
|   StackPush
|
\---------------------------------------------------------------------------*/
static void *
StackPush (element)
    void *element;
{
    StackSlot *newElement;
    LocalThreadSpecificData *tsdPtr = LOCAL_TSD_KEY(&dataKey);

    /*-------------------------------------------------------------------
    |   Reuse already allocated stack slots, if any
    |
    \------------------------------------------------------------------*/
    if (tsdPtr->currentSlot && tsdPtr->currentSlot->nextPtr) {
        tsdPtr->currentSlot = tsdPtr->currentSlot->nextPtr;
        tsdPtr->currentSlot->element = element;
        return element;
    }

    /*-------------------------------------------------------------------
    |   Allocate new stack slot
    |
    \------------------------------------------------------------------*/
    newElement = (StackSlot *)Tcl_Alloc(sizeof(StackSlot));
    memset(newElement, 0, sizeof(StackSlot));

    if (tsdPtr->elementStack == NULL) {
        tsdPtr->elementStack = newElement;
        EXITHANDLER(StackFinalize, tsdPtr->elementStack);
    } else {
        tsdPtr->currentSlot->nextPtr = newElement;
        newElement->prevPtr = tsdPtr->currentSlot;
    }

    tsdPtr->currentSlot = newElement;
    tsdPtr->currentSlot->element = element;

    return element;
}

/*----------------------------------------------------------------------------
|   StackPop  -  pops the element from stack
|
\---------------------------------------------------------------------------*/
static void *
StackPop ()
{
    void *element;
    LocalThreadSpecificData *tsdPtr = LOCAL_TSD_KEY(&dataKey);

    element = tsdPtr->currentSlot->element;
    if (tsdPtr->currentSlot->prevPtr) {
        tsdPtr->currentSlot = tsdPtr->currentSlot->prevPtr;
    }

    return element;
}

/*----------------------------------------------------------------------------
|   StackTop  -  returns top-level element from stack
|
\---------------------------------------------------------------------------*/
static void *
StackTop ()
{
    LocalThreadSpecificData *tsdPtr = LOCAL_TSD_KEY(&dataKey);

    if (tsdPtr->currentSlot == NULL) {
        return NULL;
    }

    return tsdPtr->currentSlot->element;
}


/*----------------------------------------------------------------------------
|   StackFinalize - reclaims stack memory (slots only, not elements)
|
\---------------------------------------------------------------------------*/
static void
StackFinalize (clientData)
    ClientData clientData;
{
    StackSlot *tmp, *stack = (StackSlot *)clientData;

    while (stack) {
        tmp = stack->nextPtr;
        Tcl_Free((char *)stack);
        stack = tmp;
    }
}


/*----------------------------------------------------------------------------
|   NodeObjCmd
|
\---------------------------------------------------------------------------*/
static int
NodeObjCmd (arg, interp, objc, objv)
    ClientData      arg;                /* Type of node to create. */
    Tcl_Interp    * interp;             /* Current interpreter. */
    int             objc;               /* Number of arguments. */
    Tcl_Obj *CONST  objv[];             /* Argument objects. */     
{
    domDocument *doc;
    int          textLen, tgtLen, dataLen, i, argEnd, ret = TCL_OK;
    char        *tag, *p, *textValue, *attrName, *attrValue, *tgt, *data;
    domNode     *parent, *newNode;
    Tcl_Obj     *cmdObj;

    /*--------------------------------------------------------------------
    |   Need parent node to get the owner document and to append new 
    |   child tag to it. The current parent node is stored on the stack.
    |   Take care to calculate real tag name (w/o leading namespace)
    |
    \-------------------------------------------------------------------*/
    parent = (domNode *)StackTop();    
    if (parent == NULL) {
        Tcl_AppendResult(interp, "called outside domNode context", NULL);
        return TCL_ERROR;
    }
    doc = parent->ownerDocument;

    tag = p = Tcl_GetStringFromObj(objv[0], NULL);
    while ((p = strstr(p, "::"))) {
       p += 2;
       tag = p;
    }

    /*------------------------------------------------------------------------
    |   Create new node according to type. Special case is the ELEMENT_NODE
    |   since here we may enter into recursion. The ELEMENT_NODE is the only
    |   node type which may have script body as last argument.
    |
    \-----------------------------------------------------------------------*/
    switch ((int)arg) {
        case CDATA_SECTION_NODE: 
        case COMMENT_NODE:
        case TEXT_NODE:
            if (objc != 2) {
                Tcl_WrongNumArgs(interp, 1, objv, "text");
                return TCL_ERROR;
            }
            textValue = Tcl_GetStringFromObj(objv[1], &textLen);
            newNode = (domNode*)domNewTextNode(doc, textValue, textLen, (int)arg);
            domAppendChild(parent, newNode);
            break;

        case PROCESSING_INSTRUCTION_NODE:
            if (objc != 3) {
                Tcl_WrongNumArgs(interp, 1, objv, "target data");
                return TCL_ERROR;
            } 
            tgt  = Tcl_GetStringFromObj(objv[1], &tgtLen);
            data = Tcl_GetStringFromObj(objv[2], &dataLen);
            newNode = (domNode *) domNewProcessingInstructionNode(doc, tgt, tgtLen, 
                                                                       data, dataLen);
            domAppendChild(parent, newNode);
            break;

        case ELEMENT_NODE:
            newNode = (domNode *)domNewElementNode(doc, tag, ELEMENT_NODE);
            domAppendChild(parent, newNode);
        
            if ((objc % 2) == 0) {
                cmdObj = objv[objc-1]; /* the command body argument */
                argEnd = objc - 1;
            } else {
                cmdObj = NULL;
                argEnd = objc;
            }
            for (i = 1; i < argEnd; i += 2) {
                attrName  = Tcl_GetStringFromObj(objv[i], NULL);
                attrValue = Tcl_GetStringFromObj(objv[i+1], NULL);
                domSetAttribute (newNode, attrName, attrValue);
            }
            if (cmdObj) {
                ret = nodecmd_appendFromTclScript(interp, newNode, cmdObj);
            }
            break;
    }
    return ret;
}


/*----------------------------------------------------------------------------
|   nodecmd_createNodeCmd  -  implements the "createNodeCmd" method of
|                             "dom" Tcl command
| 
|   This command is used to generate other Tcl commands which in turn
|   generate tDOM nodes. These new commands can only be called within
|   the context of the domNode command, however.
|   
|   Syntax: dom createNodeCmd <elementType> commandName
|
|           where <elementType> can be one of:
|              elementNode, commentNode, textNode, cdataNode or piNode
|
|   Example: 
|
|      % dom createNodeCmd elementNode html::body 
|      % dom createNodeCmd elementNode html::title 
|      % dom createNodeCmd textNode    html::t
|
|   And usage:
|
|      % set d [dom createDocument html]
|      % set n [$d documentElement]
|      % $n appendFromScript {
|           html::body {
|           html::title {html::t "This is an example"}
|      }
|      % puts [$n asHTML]
|
\---------------------------------------------------------------------------*/
int
nodecmd_createNodeCmd (dummy, interp, objc, objv)
    ClientData      dummy;              /* Not used. */
    Tcl_Interp    * interp;             /* Current interpreter. */
    int             objc;               /* Number of arguments. */
    Tcl_Obj *CONST  objv[];             /* Argument objects. */
{
#if ((TCL_MAJOR_VERSION >= 8) && (TCL_MINOR_VERSION > 0))    
    /* Tcl_Namespace *currentNs; */
#endif    
    int            index, ret, type = ELEMENT_NODE;
    Tcl_DString    cmdName;
    enum { ELM_NODE, TXT_NODE, CDS_NODE, CMT_NODE, PIC_NODE  };
    char *subcmd[] = {
        "elementNode","textNode","cdataNode","commentNode","piNode", NULL};


    if (objc != 3) {
        Tcl_WrongNumArgs(interp, 1, objv, "option ?arg ...?");
        return TCL_ERROR;
    }
    ret = Tcl_GetIndexFromObj(interp, objv[1], subcmd, "option", 0, &index);
    if (ret != TCL_OK) {
        return ret;
    }

    /*--------------------------------------------------------------------
    |   Construct fully qualified command name using current namespace
    |
    \-------------------------------------------------------------------*/
    Tcl_DStringInit (&cmdName);
#if ((TCL_MAJOR_VERSION >= 8) && (TCL_MINOR_VERSION > 0))    
    /* for stubs not exported:
       currentNs = (Tcl_Namespace*)Tcl_GetCurrentNamespace(interp);
       Tcl_DStringAppend(&cmdName, currentNs->fullName, -1);
    */
    ret = Tcl_Eval (interp, "namespace current");
    if (ret != TCL_OK) {
        return ret;
    }
    Tcl_DStringAppend(&cmdName, Tcl_GetStringResult (interp), -1);
    Tcl_DStringAppend(&cmdName, "::", 2);
#endif    
    Tcl_DStringAppend(&cmdName, Tcl_GetStringFromObj(objv[2], NULL), -1);

    switch (index) {
        case ELM_NODE: type = ELEMENT_NODE;                break;
        case TXT_NODE: type = TEXT_NODE;                   break;
        case CDS_NODE: type = CDATA_SECTION_NODE;          break;
        case CMT_NODE: type = COMMENT_NODE;                break;
        case PIC_NODE: type = PROCESSING_INSTRUCTION_NODE; break;
    }
    Tcl_CreateObjCommand(interp, Tcl_DStringValue(&cmdName), NodeObjCmd,
                         (ClientData)type, NULL);
    Tcl_DStringFree(&cmdName);

    return TCL_OK;
}


/*----------------------------------------------------------------------------
|   nodecmd_appendFromTclScript
|
\---------------------------------------------------------------------------*/
int
nodecmd_appendFromTclScript (interp, node, cmdObj)
    Tcl_Interp *interp;                 /* Current interpreter. */
    domNode    *node;                   /* Parent dom node */
    Tcl_Obj    *cmdObj;                 /* Argument objects. */     
{
    int ret;

    StackPush((void *)node);
#if TCL_MAJOR_VERSION > 8 && TCL_MINOR_VERSION > 0
    ret = Tcl_EvalObjEx(interp, cmdObj, 0);
#else
    ret = Tcl_EvalObj(interp, cmdObj);
#endif
    StackPop();
    
    return ret;
}

