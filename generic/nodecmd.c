/*----------------------------------------------------------------------------
|   Copyright (C) 1999  Jochen C. Loewer (loewerj@hotmail.com)
+-----------------------------------------------------------------------------
|
|   $Header$
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
|   Revision 1.2  2002/06/02 06:36:24  zoran
|   Added thread safety with capability of sharing DOM trees between
|   threads and ability to read/write-lock DOM documents
|
|   Revision 1.1.1.1  2002/02/22 01:05:35  rolf
|   tDOM0.7test with Jochens first set of patches
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

#define PARSER_NODE 9999 /* Hack so that we can invoke XML parser */

/*----------------------------------------------------------------------------
|   Types
|
|   This structure represents one stack slot. The stack itself
|   is implemented as double-linked-list of following structures.
|
\---------------------------------------------------------------------------*/
typedef struct StackSlot {
    void             *element;   /* The stacked element */
    struct StackSlot *nextPtr;   /* Next link */
    struct StackSlot *prevPtr;   /* Previous link */
} StackSlot;

/*----------------------------------------------------------------------------
|   Beginning of the stack and current element pointer are local
|   to current thread and also local to this file.
|   For non-threaded environments, it's a regular static.
|
\---------------------------------------------------------------------------*/

typedef struct CurrentStack {
    StackSlot *elementStack;
    StackSlot *currentSlot;
} CurrentStack;

#ifndef TCL_THREADS
  static CurrentStack dataKey;
# define TSDPTR(a) a
#else
  static Tcl_ThreadDataKey dataKey;
# define TSDPTR(a) (CurrentStack*)Tcl_GetThreadData((a),sizeof(CurrentStack))
#endif

/*----------------------------------------------------------------------------
|   Forward declarations
|
\---------------------------------------------------------------------------*/

static void * StackPush _ANSI_ARGS_((void *element));
static void * StackPop  _ANSI_ARGS_((void));
static void * StackTop  _ANSI_ARGS_((void));
static void   StackFinalize _ANSI_ARGS_((ClientData clientData));

static int NodeObjCmd _ANSI_ARGS_((ClientData,Tcl_Interp*,int,Tcl_Obj *CONST o[]));
static void  domAppendChild1(domNode*, domNode *);

/*----------------------------------------------------------------------------
|   StackPush
|
\---------------------------------------------------------------------------*/
static void *
StackPush (element)
    void *element;
{
    StackSlot *newElement;
    CurrentStack *tsdPtr = TSDPTR(&dataKey);

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
        TDomThreaded(Tcl_CreateThreadExitHandler(StackFinalize, 
                                                 tsdPtr->elementStack);)
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
    CurrentStack *tsdPtr = TSDPTR(&dataKey);

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
    CurrentStack *tsdPtr = TSDPTR(&dataKey);

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
        Tcl_Free((char*)stack);
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
    int len, dlen, i, ret;
    char *tag, *p, *tval, *aval;
    domNode *parent, *newNode;
    domDocument *doc;
    Tcl_Obj *cmdObj, **opts;

    /*------------------------------------------------------------------------
    |   Need parent node to get the owner document and to append new 
    |   child tag to it. The current parent node is stored on the stack.
    |
    \-----------------------------------------------------------------------*/

    parent = (domNode *)StackTop();    
    if (parent == NULL) {
        Tcl_AppendResult(interp, "called outside domNode context", NULL);
        return TCL_ERROR;
    }
    doc = parent->ownerDocument;

    /*------------------------------------------------------------------------
    |   Create new node according to type. Special case is the ELEMENT_NODE
    |   since here we may enter into recursion. The ELEMENT_NODE is the only
    |   node type which may have script body as last argument.
    |
    \-----------------------------------------------------------------------*/

    ret = TCL_OK;

    switch ((int)arg) {
    case CDATA_SECTION_NODE: /* FALL-THRU */
    case COMMENT_NODE:       /* FALL-THRU */
    case TEXT_NODE:
        if (objc != 2) {
            Tcl_WrongNumArgs(interp, 1, objv, "text");
            return TCL_ERROR;
        }
        tval = Tcl_GetStringFromObj(objv[1], &len);
        newNode = (domNode*)domNewTextNode(doc, tval, len, (int)arg);
        domAppendChild1(parent, newNode);
        break;

    case PROCESSING_INSTRUCTION_NODE:
        if (objc != 3) {
            Tcl_WrongNumArgs(interp, 1, objv, "target data");
            return TCL_ERROR;
        } 
        tval = Tcl_GetStringFromObj(objv[1], &len);
        aval = Tcl_GetStringFromObj(objv[2], &dlen);
        newNode = (domNode *)
            domNewProcessingInstructionNode(doc, tval, len, aval, dlen);
        domAppendChild1(parent, newNode);
        break;

    case PARSER_NODE: /* non-standard node-type : a hack! */
        if (objc != 2) {
            Tcl_WrongNumArgs(interp, 1, objv, "markup");
            return TCL_ERROR;
        }
        ret = tcldom_appendXML(interp, parent, objv[1]);
        break;

    case ELEMENT_NODE:
        tag = Tcl_GetStringFromObj(objv[0], &len);
        p = tag + len;
        /* Isolate just the tag name, i.e. skip it's parent namespace */
        while (--p > tag) {
            if ((*p == ':') && (*(p-1) == ':')) {
                p++; /* just after the last "::" */
                tag = p;
                break;
            }
        }

        newNode = (domNode *)domNewElementNode(doc, tag, ELEMENT_NODE);
        domAppendChild1(parent, newNode);
        
        /*
         * Allow for following syntax:
         *   cmd ?-option value ...? ?script?
         *   cmd key_value_list script
         */

        if ((objc % 2) == 0) {
            cmdObj = objv[objc-1];
            len  = objc - 2; /* skip both command and script */
            opts = (Tcl_Obj**)objv + 1;
        } else if((objc == 3)
                  && Tcl_ListObjGetElements(interp,objv[1],&len,&opts)==TCL_OK
                  && (len == 0 || len > 1)) {
            if ((len % 2)) {
                Tcl_AppendResult(interp, "list must have "
                                 "an even number of elements", NULL);
                return TCL_ERROR;
            }
            cmdObj = objv[2];
        } else {
            cmdObj = NULL;
            len  = objc - 1; /* skip command */
            opts = (Tcl_Obj**)objv + 1;
        }
        for (i = 0; i < len; i += 2) {
            tval = Tcl_GetStringFromObj(opts[i], NULL);
            if (*tval != '-') {
                Tcl_AppendResult(interp, "bad option: ", tval, NULL);
                return TCL_ERROR;
            }
            tval++;
            aval = Tcl_GetStringFromObj(opts[i+1], NULL);
            domSetAttribute(newNode, tval, aval);
        }
        if (cmdObj) {
            ret = nodecmd_appendFromScript(interp, newNode, cmdObj);
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
    int index, ret, type;
    char *nsName;
    Tcl_DString cmdName;

    enum {
        ELM_NODE, TXT_NODE, CDS_NODE, CMT_NODE, PIC_NODE, PRS_NODE
    };

    char *subcmd[] = {
        "elementNode", "textNode", "cdataNode", "commentNode", "piNode",
        "parserNode", NULL
    };

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
    ret = Tcl_Eval (interp, "namespace current");
    if (ret != TCL_OK) {
        return ret;
    }
    nsName = Tcl_GetStringResult(interp);
    Tcl_DStringAppend(&cmdName, nsName, -1);
    if (strcmp(nsName, "::")) {
        Tcl_DStringAppend(&cmdName, "::", 2);
    }
    Tcl_DStringAppend(&cmdName, Tcl_GetStringFromObj(objv[2], NULL), -1);

    switch (index) {
    case PRS_NODE: type = PARSER_NODE;                 break;
    case ELM_NODE: type = ELEMENT_NODE;                break;
    case TXT_NODE: type = TEXT_NODE;                   break;
    case CDS_NODE: type = CDATA_SECTION_NODE;          break;
    case CMT_NODE: type = COMMENT_NODE;                break;
    case PIC_NODE: type = PROCESSING_INSTRUCTION_NODE; break;
    }
    Tcl_CreateObjCommand(interp, Tcl_DStringValue(&cmdName), NodeObjCmd,
                         (ClientData)type, NULL);
    Tcl_DStringResult(interp, &cmdName);
    Tcl_DStringFree(&cmdName);

    return TCL_OK;
}


/*----------------------------------------------------------------------------
|   nodecmd_appendFromScript
|
\---------------------------------------------------------------------------*/
int
nodecmd_appendFromScript (interp, node, cmdObj)
    Tcl_Interp *interp;                 /* Current interpreter. */
    domNode    *node;                   /* Parent dom node */
    Tcl_Obj    *cmdObj;                 /* Argument objects. */
{
    int ret;

    StackPush((void *)node);
    Tcl_AllowExceptions(interp);
    ret = Tcl_EvalObj(interp, cmdObj);
    StackPop();

    return (ret == TCL_BREAK) ? TCL_OK : ret;
}

/*----------------------------------------------------------------------------
|   nodecmd_curentNode
|
\---------------------------------------------------------------------------*/

void *
nodecmd_currentNode(void)
{
    return StackTop();
}

/*---------------------------------------------------------------------------
|   domAppendChild1
|
|   A (slightly faster) shortcut for standard domAppendChild function
\--------------------------------------------------------------------------*/

static void
domAppendChild1 (domNode *node, domNode *childToAppend)
{
    if (node->lastChild) {
        node->lastChild->nextSibling = childToAppend;
        childToAppend->previousSibling = node->lastChild;
    } else {
        node->firstChild = childToAppend;
        childToAppend->previousSibling = NULL;
    }

    node->lastChild = childToAppend;
    childToAppend->nextSibling = NULL;
    childToAppend->parentNode = node;
    node->ownerDocument->fragments = NULL;
}
