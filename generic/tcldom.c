/*----------------------------------------------------------------------------
|   Copyright (c) 1999 Jochen Loewer (loewerj@hotmail.com)
+-----------------------------------------------------------------------------
|
|   $Header$
|
|
|   A DOM implementation for Tcl using James Clark's expat XML parser
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
|       Sept99  Carsten Zerbst    Added comment and processing instructions
|                                 nodes.
|       June00  Zoran Vasiljevic  Made thread-safe.
|       July00  Zoran Vasiljevic  Added "domNode appendFromScript"
|
|
|   $Log$
|   Revision 1.2  2002/02/23 01:13:33  rolf
|   Some code tweaking for a mostly warning free MS build
|
|   Revision 1.1.1.1  2002/02/22 01:05:35  rolf
|   tDOM0.7test with Jochens first set of patches
|
|
|
|   written by Jochen Loewer
|   April, 1999
|
\---------------------------------------------------------------------------*/



/*----------------------------------------------------------------------------
|   Includes
|
\---------------------------------------------------------------------------*/
#include <tcl.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <dom.h>
#include <domxpath.h>
#include <domxslt.h>
#include <xmlsimple.h>
#include <domhtml.h>
#include <nodecmd.h>
#include <tcldom.h>


/*----------------------------------------------------------------------------
|   Debug Macros
|
\---------------------------------------------------------------------------*/
#ifdef DEBUG
# define DBG(x) x
#else
# define DBG(x) 
#endif


/*----------------------------------------------------------------------------
|   Macros
|
\---------------------------------------------------------------------------*/
#define XP_CHILD         0
#define XP_DESCENDANT    1
#define XP_ANCESTOR      2
#define XP_FSIBLING      3
#define XP_PSIBLING      4

#define MAX_REWRITE_ARGS 50

#define SetResult(str) \
                     (Tcl_SetStringObj (Tcl_GetObjResult (interp), (str), -1))
#define AppendResult(str) \
                     (Tcl_AppendToObj (Tcl_GetObjResult (interp), (str), -1))
#define SetIntResult(i) \
                     (Tcl_SetIntObj (Tcl_GetObjResult (interp), (i) ))
#define SetDoubleResult(d) \
                     (Tcl_SetDoubleObj (Tcl_GetObjResult (interp), (d) ))
#define CheckArgs(min,max,n,msg) \
                     if ((objc < min) || (objc >max)) { \
                         Tcl_WrongNumArgs(interp, n, objv, msg); \
                         return TCL_ERROR; \
                     }
#if TclOnly8Bits
#define writeChars(var,chan,buf,len)  (chan) ? \
                     ((void)Tcl_Write ((chan), (buf), (len) )) : \
                     (Tcl_AppendToObj ((var), (buf), (len) ));
#define TCLGETBYTES(obj,pLen) Tcl_GetStringFromObj(obj,pLen)
#else
#define writeChars(var,chan,buf,len)  (chan) ? \
                     ((void)Tcl_WriteChars ((chan), (buf), (len) )) : \
                     (Tcl_AppendToObj ((var), (buf), (len) ));
#define TCLGETBYTES(obj,pLen) Tcl_GetByteArrayFromObj(obj,pLen)
#endif          
/*----------------------------------------------------------------------------
|   Module Globals
|
\---------------------------------------------------------------------------*/
#ifndef TCL_THREADS

    static TEncoding *Encoding_to_8bit      = NULL;
    static int        storeLineColumn       = 0;
    static int        dontCreateObjCommands = 0;

#   define TSD(x)         x
#   define GetTcldomTSD()
    
#else

    typedef struct LocalThreadSpecificData {

        TEncoding *Encoding_to_8bit;
        int        storeLineColumn;
        int        dontCreateObjCommands;
     
    } LocalThreadSpecificData;

    static Tcl_ThreadDataKey dataKey;
    
#   define TSD(x)          tsdPtr->x
#   define GetTcldomTSD()  LocalThreadSpecificData *tsdPtr = \
                                (LocalThreadSpecificData*)   \
                                Tcl_GetThreadData(           \
                                    &dataKey,                \
                                    sizeof(LocalThreadSpecificData));
#endif


static char dom_usage[] = 
                "Usage dom <subCommand> <args>, where subCommand can be:    \n"
                "          parse ?-keepEmpties? ?-channel <channel> ?-baseurl <baseurl>?  \n"
                "                ?-feedbackAfter <#Bytes>? ?-externalentitycommand <cmd>? \n"
                "                ?-simple? ?-html? ?-ns? ?<xml>? ?<objVar>? \n"
                "          createDocument docElemName ?objVar?              \n"
                "          createNodeCmd  (element|comment|text|cdata|pi)Node commandName \n"
                "          setResultEncoding ?encodingName?                 \n"
                "          setStoreLineColumn ?boolean?                     \n"
                ;

static char domObj_usage[] =
                "Usage docObj <method> <args>, where method can be:\n"
                "          documentElement ?objVar?                \n"
                "          getElementsByTagName name               \n"
                "          createElement tagName ?objVar?          \n"
                "          createElementNS uri tagName ?objVar?    \n"
                "          createCDATASection data ?objVar?        \n"
                "          createTextNode text ?objVar?            \n"
                "          createComment text ?objVar?             \n"
                "          createProcessingInstruction target data ?objVar? \n"
                "          delete                                  \n"
                "          getDefaultOutputMethod                  \n" 
                ;

static char node_usage[] =
                "Usage nodeObj <method> <args>, where method can be:\n"
                "    nodeType             \n"
                "    nodeName             \n"
                "    nodeValue ?newValue? \n"
                "    hasChildNodes        \n"
                "    childNodes           \n"
                "    childNodesLive               \n"
                "    parentNode                   \n"
                "    firstChild ?nodeObjVar?      \n"
                "    lastChild ?nodeObjVar?       \n"
                "    nextSibling ?nodeObjVar?     \n"
                "    previousSibling ?nodeObjVar? \n"
                "    hasAttribute attrName        \n"
                "    getAttribute attrName ?defaultValue? \n"
                "    setAttribute attrName value ?attrName value ...? \n"
                "    removeAttribute attrName     \n"
                "    hasAttributeNS uri localName  \n"
                "    getAttributeNS uri localName ?defaultValue? \n"
                "    setAttributeNS uri attrName value ?attrName value ...? \n"
                "    removeAttributeNS uri attrName  \n"
                "    attributes ?attrNamePattern?    \n"
                "    appendChild new      \n"
                "    insertBefore new ref \n"
                "    replaceChild new old \n"
                "    removeChild child    \n"
                "    cloneNode ?-deep?    \n"
                "    ownerDocument        \n"
                "    getElementsByTagName name \n"
                "    getElementsByTagNameNS uri name \n"
                "    getElementbyId id    \n"
                "    find attrName attrValue ?nodeObjVar?   \n"
                "    child      number|all ?type? ?attrName attrValue? \n"
                "    descendant number|all ?type? ?attrName attrValue? \n"
                "    ancestor   number|all ?type? ?attrName attrValue? \n"
                "    fsibling   number|all ?type? ?attrName attrValue? \n"
                "    psibling   number|all ?type? ?attrName attrValue? \n"
                "    root ?nodeObjVar?           \n"
                "    target                      \n"
                "    data                        \n"
                "    text                        \n"
                "    prefix                      \n"
                "    namespaceURI                \n"
                "    getBaseURI                  \n"
                "    localName                   \n"
                "    delete                      \n"
                "    getLine                     \n"
                "    getColumn                   \n"
                "    @<attrName> ?defaultValue?  \n"
                "    asList                      \n"
                "    asXML ?-indent <none,0..8>? ?-channel <channelId>? \n"
                "    asHTML ?-channel <channelId>?\n"
                "    appendFromList nestedList   \n"
                "    appendFromScript script     \n"
                "    appendXML xmlString         \n"
                "    selectNodes xpathQuery ?typeVar? \n"
                "    toXPath                     \n"
                "    xslt <xsltDocNode>          \n"
                ;



/*----------------------------------------------------------------------------
|   Types
|
\---------------------------------------------------------------------------*/
typedef struct TcldomDocDeleteInfo {
    domDocument * document;
    Tcl_Interp  * interp;
    char        * traceVarName;
    
} TcldomDocDeleteInfo;



/*----------------------------------------------------------------------------
|   Prototypes for procedures defined later in this file:
|
\---------------------------------------------------------------------------*/
static int tcldom_DocObjCmd (ClientData  clientData,
                             Tcl_Interp *interp,
                             int         objc,
                             Tcl_Obj    *objv[]);
                             
static void tcldom_docCmdDeleteProc (ClientData  clientData);
                              
static char * tcldom_docTrace (ClientData  clientData,
                               Tcl_Interp *interp,
                               char       *name1,
                               char       *name2,
                               int        flags  );
                               

/*----------------------------------------------------------------------------
|   UtfCount     
|
\---------------------------------------------------------------------------*/
#if !TclOnly8Bits
static int
UtfCount(
    int ch
)
{
    if ((ch > 0) && (ch < 0x80)) { return 1; }
    if (ch <= 0x7FF)             { return 2; }
    if (ch <= 0xFFFF)            { return 3; } 
    return 0;
}
#endif


/*----------------------------------------------------------------------------
|   tcldom_docDeleteNode
|
\---------------------------------------------------------------------------*/
static void
tcldom_docDeleteNode (
    domNode  * node,
    void     * clientData
)
{
    Tcl_Interp *interp = clientData;
    char     objCmdName[40];
    
    /* try to delete the node object commands, ignore errors */
    if (node->nodeFlags & VISIBLE_IN_TCL) {
        sprintf (objCmdName, "domNode%d", node->nodeNumber);
        Tcl_DeleteCommand (interp, objCmdName);                                                
    }
}



/*----------------------------------------------------------------------------
|   tcldom_docCmdDeleteProc
|
\---------------------------------------------------------------------------*/
static 
void tcldom_docCmdDeleteProc  (
    ClientData  clientData
)
{
    TcldomDocDeleteInfo  * dinfo = (TcldomDocDeleteInfo*) clientData;

    DBG(fprintf (stderr, "tcldom_docCmdDeleteProc doc%x !\n", dinfo->document);)
    if (dinfo->traceVarName) {
        DBG(fprintf (stderr, "tcldom_docCmdDeleteProc calling Tcl_UntraceVar ...\n");)
        Tcl_UntraceVar (dinfo->interp, dinfo->traceVarName, 
                                TCL_TRACE_WRITES |  TCL_TRACE_UNSETS,   
                                tcldom_docTrace, clientData);    
        free(dinfo->traceVarName);
        dinfo->traceVarName = NULL;
    }
    /* delete DOM tree */                     
    domFreeDocument (dinfo->document, tcldom_docDeleteNode, dinfo->interp );
    Tcl_Free ((void*)dinfo);
}


/*----------------------------------------------------------------------------
|   tcldom_docTrace   
|
\---------------------------------------------------------------------------*/
static 
char * tcldom_docTrace (
    ClientData  clientData,
    Tcl_Interp *interp,
    char       *name1,
    char       *name2,
    int        flags
)
{
    TcldomDocDeleteInfo * dinfo;
    char                  objCmdName[40];
    
    
    dinfo = (TcldomDocDeleteInfo*) clientData; 

    DBG(fprintf (stderr, "tcldom_trace %x doc%x !\n", flags, clientData );)
    if (flags & TCL_TRACE_WRITES) {
        return "var is read-only";
    }
    if (flags & TCL_TRACE_UNSETS) {
        DBG(fprintf (stderr, "tcldom_trace delete domDoc%d (addr %x)!\n",
                     dinfo->document->documentNumber, dinfo->document);)

        /* delete document by deleting Tcl object command */
       
       
        sprintf (objCmdName, "domDoc%d", dinfo->document->documentNumber );
       /*
        Tcl_UntraceVar (interp, name1, TCL_TRACE_WRITES | 
                                       TCL_TRACE_UNSETS,   
                                       tcldom_docTrace, clientData);
       */
        DBG(fprintf (stderr, "tcldom_trace calling Tcl_DeleteCommand\n");)
        Tcl_DeleteCommand ( interp, objCmdName );
    }
    return NULL;
}


/*----------------------------------------------------------------------------
|   tcldom_nodeTrace   
|
\---------------------------------------------------------------------------*/
static
char * tcldom_nodeTrace (
    ClientData  clientData,
    Tcl_Interp *interp,
    char       *name1,
    char       *name2,
    int        flags
)
{
    char     objCmdName[40];
    domNode *node;
    
    DBG(fprintf (stderr, "tcldom_nodeTrace %x %d !\n", flags, (int)clientData );)
    
    node = (domNode*) clientData;
    
    if (flags & TCL_TRACE_UNSETS) {
        DBG(fprintf (stderr, "tcldom_nodeTrace delete domNode%d !\n", 
                             node->nodeNumber );)
        sprintf (objCmdName, "domNode%d", node->nodeNumber );
        Tcl_DeleteCommand ( interp, objCmdName );
        Tcl_UntraceVar (interp, name1, TCL_TRACE_WRITES | 
                                       TCL_TRACE_UNSETS,   
                                       tcldom_nodeTrace, clientData);
        node->nodeFlags &= ~VISIBLE_IN_TCL;
        return NULL;
    }
    if (flags & TCL_TRACE_WRITES) {
        return "node object is read-only";
    }
    return NULL;
}


/*----------------------------------------------------------------------------
|   tcldom_createNodeObj
|
\---------------------------------------------------------------------------*/
static
void tcldom_createNodeObj (
    Tcl_Interp * interp,
    domNode    * node,
    char       * objCmdName
) 
{
    GetTcldomTSD()

    if (TSD(dontCreateObjCommands)) {
        sprintf (objCmdName, "domNode0x%x", (unsigned int)node);
    } else {
        sprintf (objCmdName, "domNode%d", node->nodeNumber);
        DBG(fprintf(stderr,"creating %s\n",objCmdName);)
        Tcl_CreateObjCommand ( interp, objCmdName, 
                               (Tcl_ObjCmdProc *)  tcldom_NodeObjCmd,
                               (ClientData)        node, 
                               (Tcl_CmdDeleteProc*)NULL );
        node->nodeFlags |= VISIBLE_IN_TCL;
    }
} 


/*----------------------------------------------------------------------------
|   tcldom_createDocumentObj
|
\---------------------------------------------------------------------------*/
static
void tcldom_createDocumentObj (
    Tcl_Interp  * interp,
    domDocument * doc,
    char        * objCmdName
) 
{
    sprintf (objCmdName, "domDoc%d", doc->documentNumber);
} 


/*----------------------------------------------------------------------------
|   tcldom_returnNodeObj   
|
\---------------------------------------------------------------------------*/
static
int tcldom_returnNodeObj (
    Tcl_Interp *interp,
    domNode    *node,
    int         setVariable,
    Tcl_Obj    *var_name
) 
{
    GetTcldomTSD()
    char  objCmdName[40], *objVar;

    if (node == NULL) {
        if (setVariable) {
            objVar = Tcl_GetStringFromObj (var_name, NULL);
            Tcl_UnsetVar (interp, objVar, 0);                                    
            Tcl_SetVar   (interp, objVar, "", 0);
        } 
        SetResult ( "");
        return TCL_OK;
    }
    tcldom_createNodeObj (interp, node, objCmdName);
    if (TSD(dontCreateObjCommands)) {
        if (setVariable) {
            objVar = Tcl_GetStringFromObj (var_name, NULL);
            Tcl_SetVar   (interp, objVar, objCmdName, 0);
        }
    } else {
        if (setVariable) {
            objVar = Tcl_GetStringFromObj (var_name, NULL);
            Tcl_UnsetVar (interp, objVar, 0);                                    
            Tcl_SetVar   (interp, objVar, objCmdName, 0);
            Tcl_TraceVar (interp, objVar, TCL_TRACE_WRITES | 
                                          TCL_TRACE_UNSETS,   
                                          tcldom_nodeTrace, (ClientData) node);
        }
    }
    SetResult ( objCmdName);
    return TCL_OK;
}


/*----------------------------------------------------------------------------
|   tcldom_returnDocumentObj   
|
\---------------------------------------------------------------------------*/
int tcldom_returnDocumentObj (
    Tcl_Interp  *interp,
    domDocument *document,
    int          setVariable,
    Tcl_Obj     *var_name
) 
{
    char objCmdName[40], *objVar;
    TcldomDocDeleteInfo  *dinfo;
    Tcl_CmdInfo          cmd_info;

    
    if (document == NULL) {
        if (setVariable) {
            objVar = Tcl_GetStringFromObj (var_name, NULL);
            Tcl_UnsetVar (interp, objVar, 0);                                    
            Tcl_SetVar   (interp, objVar, "", 0);
        } 
        SetResult ( "");
        return TCL_OK;
    }

    tcldom_createDocumentObj (interp, document, objCmdName);

    if (!Tcl_GetCommandInfo(interp, objCmdName, &cmd_info)) {

        dinfo = (TcldomDocDeleteInfo*) Tcl_Alloc (sizeof(TcldomDocDeleteInfo));
        dinfo->interp       = interp;
        dinfo->document     = document;
        dinfo->traceVarName = NULL;

        Tcl_CreateObjCommand ( interp, objCmdName, 
                                      (Tcl_ObjCmdProc *)  tcldom_DocObjCmd,
                                      (ClientData)        dinfo, 
                                      (Tcl_CmdDeleteProc*)tcldom_docCmdDeleteProc );
    } else {
        /* reuse old information */
        dinfo = (TcldomDocDeleteInfo*)cmd_info.objClientData;
    }
    if (setVariable) {
        objVar = Tcl_GetStringFromObj (var_name, NULL);
        dinfo->traceVarName = strdup(objVar);
        Tcl_UnsetVar (interp, objVar, 0);                                    
        Tcl_SetVar   (interp, objVar, objCmdName, 0);
        Tcl_TraceVar (interp, objVar, TCL_TRACE_WRITES | 
                                      TCL_TRACE_UNSETS,   
                                      tcldom_docTrace, (ClientData) dinfo);
    }
    SetResult ( objCmdName);
    return TCL_OK;
}



/*----------------------------------------------------------------------------
|   tcldom_getElementsByTagName
|
\---------------------------------------------------------------------------*/
static int 
tcldom_getElementsByTagName (
    Tcl_Interp *interp,
    char       *namePattern,
    domNode    *node,
    int         nsIndex
)
{
    int      result;
    domNode *child, *temp;


    if (    ((nsIndex == -1) || (nsIndex == node->namespace)) 
         && Tcl_StringMatch( (char*)node->nodeName, namePattern)
    ) {                
        Tcl_Obj *namePtr;
        Tcl_Obj *resultPtr = Tcl_GetObjResult(interp);
        char    objCmdName[40];
        
        tcldom_createNodeObj (interp, node, objCmdName);
        namePtr = Tcl_NewStringObj (objCmdName, -1);
        result = Tcl_ListObjAppendElement(interp, resultPtr,
                                                  namePtr);
        if (result != TCL_OK) {
            Tcl_DecrRefCount (namePtr);
            return result;
        }
        return TCL_OK;
    }

    /* recurs to the child nodes */
    if (node->nodeType == ELEMENT_NODE) {
        child = node->firstChild;
        while (child) {
            temp = child->nextSibling;
            result = tcldom_getElementsByTagName (interp, namePattern, child, nsIndex);
            if (result != TCL_OK) {
                return result;
            }
            child = temp;
        }
    }
    
    return TCL_OK;
}


/*----------------------------------------------------------------------------
|   tcldom_find
|
\---------------------------------------------------------------------------*/
static
domNode * tcldom_find (
    domNode    *node,
    char       *attrName,
    char       *attrVal,
    int         length
)
{
    domNode     *child, *result;
    domAttrNode *attrs;
    
    if (node->nodeType != ELEMENT_NODE) return NULL;

    attrs = node->firstAttr;
    while (attrs) {
        if ((strcmp(attrs->nodeName, attrName)==0) &&
            (length == attrs->valueLength)         && 
            (strncmp(attrs->nodeValue, attrVal, length)==0)) {

            return node;
        } 
        attrs = attrs->nextSibling;
    }
    child = node->firstChild;
    while (child != NULL) {

        result = tcldom_find (child, attrName, attrVal, length);
        if (result != NULL) {
            return result;
        }
        child = child->nextSibling;
    }
    return NULL;
}


/*----------------------------------------------------------------------------
|   tcldom_xpointerAddCallback
|
\---------------------------------------------------------------------------*/
static
int tcldom_xpointerAddCallback (
    domNode    * node,
    void       * clientData
)
{
    Tcl_Interp * interp = (Tcl_Interp*)clientData;
    Tcl_Obj    * resultPtr = Tcl_GetObjResult(interp); 
    Tcl_Obj    * namePtr;
    char         objCmdName[40]; 
    int          result;
    
    
    tcldom_createNodeObj (interp, node, objCmdName);
    namePtr = Tcl_NewStringObj (objCmdName, -1);
    result  = Tcl_ListObjAppendElement(interp, resultPtr,
                                               namePtr);    
    if (result != TCL_OK) {
        Tcl_DecrRefCount (namePtr);
    } 
    return result;
}


/*----------------------------------------------------------------------------
|   tcldom_xpointerSearch
|
\---------------------------------------------------------------------------*/
static
int tcldom_xpointerSearch (
    Tcl_Interp * interp,    
    int          mode,
    domNode    * node,
    int          objc,
    Tcl_Obj    * CONST  objv[]
)
{
    char *str;
    int  i = 0;
    int  result = 0;
    int  all = 0;
    int  instance = 0;
    int  type = ELEMENT_NODE;
    char *element   = NULL;
    char *attrName  = NULL;
    char *attrValue = NULL;
    int   attrLen;

    
    str = Tcl_GetStringFromObj (objv[2], NULL);
    if (strcmp(str, "all")==0) {
        all = 1;
    } else {
        if (Tcl_GetIntFromObj(interp, objv[2], &instance) != TCL_OK) {
            SetResult ( "instance must be integer or 'all'");
            return TCL_ERROR;
        }
    }
    if (objc > 3) {
        str = Tcl_GetStringFromObj (objv[3], NULL);
        if (*str == '#') {
            if (strcmp(str,"#text")==0) {
                type = TEXT_NODE;
            } else if (strcmp(str,"#cdata")==0) {
                type = CDATA_SECTION_NODE;
            } else if (strcmp(str,"#all")==0) {
                type = ALL_NODES;
            } else if (strcmp(str,"#element")==0) {
                type = ELEMENT_NODE;
            } else {
                SetResult ( "wrong node type");
                return TCL_ERROR;
            }
        } else {
            element = str;
        }   
    }
    if (objc >= 5) {
        if ((type != ELEMENT_NODE) && (type != ALL_NODES)) {
            SetResult ( "Attribute search only for element nodes");
            return TCL_ERROR;
        }
        attrName  = Tcl_GetStringFromObj (objv[4], NULL);
        if (objc == 6) {
            attrValue = Tcl_GetStringFromObj (objv[5], &attrLen);
        } else {
            attrValue = "*";
            attrLen = 1;
        }
    }
    Tcl_ResetResult (interp);
    switch (mode) {
        case XP_CHILD:
            result = domXPointerChild (node, all, instance, type, element, 
                                                  attrName, attrValue, attrLen,
                                                  tcldom_xpointerAddCallback,
                                                  interp);
            break;

        case XP_DESCENDANT:
            result = domXPointerDescendant (node, all, instance, &i, type, element, 
                                                  attrName, attrValue,  attrLen,
                                                  tcldom_xpointerAddCallback,
                                                  interp);
            break;

        case XP_ANCESTOR:
            result = domXPointerAncestor (node, all, instance, &i, type, element, 
                                                attrName, attrValue,  attrLen,
                                                tcldom_xpointerAddCallback,
                                                interp);
            break;

        case XP_FSIBLING:
            result = domXPointerXSibling (node, 1, all, instance, type, element, 
                                                        attrName, attrValue, attrLen,
                                                        tcldom_xpointerAddCallback,
                                                        interp);
            break;

        case XP_PSIBLING:
            result = domXPointerXSibling (node, 0, all, instance, type, element, 
                                                        attrName, attrValue,  attrLen,
                                                        tcldom_xpointerAddCallback,
                                                        interp);
            break;
    }
    if (result != 0) {
        return TCL_ERROR;
    }
    return TCL_OK;
}


/*----------------------------------------------------------------------------
|   tcldom_getNodeFromName   
|
\---------------------------------------------------------------------------*/
static domNode * tcldom_getNodeFromName (
    Tcl_Interp  *interp,
    char        *nodeName,
    char       **errMsg
)
{
    Tcl_CmdInfo  cmdInfo;
    domNode     *node;
    int          result;
    
    if (strncmp(nodeName, "domNode", 7)!=0) {
        DBG(fprintf(stderr, "-%s- %d \n",nodeName, strncmp(nodeName, "domNode", 7) );)
        *errMsg = "parameter not a domNode!";
        return NULL;
    }
    if (    (nodeName[7]!='0') 
         || (nodeName[8]!='x')
         || (sscanf(&nodeName[9], "%x", (unsigned int*)&node) !=1 )
       )          
    {  
        result = Tcl_GetCommandInfo (interp, nodeName, &cmdInfo);
        if (!result) {
           *errMsg = "parameter not a domNode!";
           return NULL;
        }
        if (   (!cmdInfo.isNativeObjectProc)
            || (cmdInfo.objProc != (Tcl_ObjCmdProc*)tcldom_NodeObjCmd)) {
                
            *errMsg = "not a dom object!";
            return NULL;
        }
        node = (domNode*)cmdInfo.objClientData;
    }
    return node;
}

/*----------------------------------------------------------------------------
|   tcldom_getDocumentFromName   
|
\---------------------------------------------------------------------------*/
static domDocument * tcldom_getDocumentFromName (
    Tcl_Interp  *interp,
    char        *docName,
    char       **errMsg
)
{
    Tcl_CmdInfo  cmdInfo;
    domDocument *doc;
    int          result;    
    TcldomDocDeleteInfo * dinfo;
    
    if (strncmp(docName, "domDoc", 6)!=0) {
        DBG(fprintf(stderr, "-%s- %d \n",docName, strncmp(docName, "domDoc", 6) );)
        *errMsg = "parameter not a domDoc!";
        return NULL;
    }
    if (    (docName[6]!='0') 
         || (docName[7]!='x')
         || (sscanf(&docName[8], "%x", (unsigned int*)&doc) !=1 )
       )          
    {  
        result = Tcl_GetCommandInfo (interp, docName, &cmdInfo);
        if (!result) {
           *errMsg = "parameter not a domDoc!";
           return NULL;
        }
        if (   (!cmdInfo.isNativeObjectProc)
            || (cmdInfo.objProc != (Tcl_ObjCmdProc*)tcldom_DocObjCmd)) {
                
            *errMsg = "not a document object!";
            return NULL;
        }
        dinfo = (TcldomDocDeleteInfo*)cmdInfo.objClientData;
        return dinfo->document;
    }
    return doc;
}


/*----------------------------------------------------------------------------
|   tcldom_appendXML
|
\---------------------------------------------------------------------------*/
static
int tcldom_appendXML (
    Tcl_Interp *interp,    
    domNode    *node,
    Tcl_Obj    *obj
)
{   
    GetTcldomTSD()
    char        *xml_string;
    int          xml_string_len;
    domDocument *doc;
    XML_Parser   parser;
    
    
    xml_string = TCLGETBYTES( obj, &xml_string_len);

#ifdef TDOM_NO_EXPAT
    Tcl_AppendResult(interp, "tDOM was compiled without Expat!", NULL);
    return TCL_ERROR;
#else    
    parser = XML_ParserCreate(NULL);     

    doc = domReadDocument (parser, 
                           xml_string, 
                           xml_string_len,
                           1, 
                           TSD(Encoding_to_8bit), 
                           TSD(storeLineColumn),
                           0,
                           NULL,
                           NULL,
                           node->ownerDocument->extResolver,
                           interp);
    if (doc == NULL) {
        char s[50];
        long byteIndex, i;
        
        Tcl_ResetResult(interp);
        sprintf(s, "%d", XML_GetCurrentLineNumber(parser));
        Tcl_AppendResult(interp, "error \"", XML_ErrorString(XML_GetErrorCode(parser)),
                                 "\" at line ", s, " character ", NULL);
        sprintf(s, "%d", XML_GetCurrentColumnNumber(parser));
        Tcl_AppendResult(interp, s, NULL);
        byteIndex = XML_GetCurrentByteIndex(parser);
        if (byteIndex != -1) {
             Tcl_AppendResult(interp, "\n\"", NULL);
             s[1] = '\0';
             for (i=-20; i < 40; i++) {
                 if ((byteIndex+i)>=0) {
                     if (xml_string[byteIndex+i]) {
                         s[0] = xml_string[byteIndex+i];
                         Tcl_AppendResult(interp, s, NULL);
                         if (i==0) {
                             Tcl_AppendResult(interp, " <--Error-- ", NULL);
                         }
                     } else {
                         break;
                     }
                 }
             }
             Tcl_AppendResult(interp, "\"",NULL);            
        }
        XML_ParserFree(parser); 
        return TCL_ERROR;
    }
    XML_ParserFree(parser); 
    
    domAppendChild (node, doc->documentElement);
    Tcl_Free ((void*)doc);
    
    return tcldom_returnNodeObj (interp, node, 0, NULL);     
#endif    
}


/*----------------------------------------------------------------------------
|   tcldom_xpathResultSet
|
\---------------------------------------------------------------------------*/
static
int tcldom_xpathResultSet (
    Tcl_Interp      *interp,
    xpathResultSet  *rs,
    Tcl_Obj         *type,
    Tcl_Obj         *value
)
{
    int          rc, i;
    Tcl_Obj     *namePtr, *objv[2];
    char         objCmdName[40];
    domAttrNode *attr;
     
    switch (rs->type) {
        case EmptyResult:
             Tcl_SetStringObj (type, "empty", -1);
             Tcl_SetStringObj (value, "", -1);
             break;
       
        case BoolResult:
             Tcl_SetStringObj (type, "bool", -1);
             Tcl_SetIntObj (value, rs->intvalue);
             break;
             
        case IntResult:
             Tcl_SetStringObj (type, "number", -1);
             Tcl_SetIntObj (value, rs->intvalue);
             break;
             
        case RealResult:
             Tcl_SetStringObj (type, "number", -1);
             Tcl_SetDoubleObj (value, rs->realvalue);
             break;
             
        case StringResult:
             Tcl_SetStringObj (type, "string", -1);
             Tcl_SetStringObj (value, rs->string, rs->string_len);
             break;
             
        case MixedSetResult:
        case NodeSetResult:
             Tcl_SetStringObj (type, "nodes", 5);
             for (i=0; i<rs->nr_nodes; i++) {
                 if (rs->nodes[i]->nodeType == DOCUMENT_NODE) {
                     tcldom_createDocumentObj (interp, 
                                               (domDocument*)(rs->nodes[i]),
                                               objCmdName);
                     namePtr = Tcl_NewStringObj (objCmdName, -1);
                 } else 
                 if (rs->nodes[i]->nodeType == ATTRIBUTE_NODE) {
                     attr = (domAttrNode*)rs->nodes[i];
                     objv[0] = Tcl_NewStringObj (attr->nodeName, -1);
                     objv[1] = Tcl_NewStringObj (attr->nodeValue, 
                                                 attr->valueLength);
                     namePtr = Tcl_NewListObj (2, objv);
                 } else {
                     tcldom_createNodeObj (interp, rs->nodes[i], 
                                           objCmdName);
                 namePtr = Tcl_NewStringObj (objCmdName, -1);
                 }
                 rc = Tcl_ListObjAppendElement(interp, value, namePtr);
                 if (rc != TCL_OK) {
                     Tcl_DecrRefCount (namePtr);
                     return rc;
                 }
             }
             break;
             
        case AttrNodeSetResult:
             Tcl_SetStringObj (type, "attrnodes",-1);
             Tcl_SetListObj (value, 0, NULL);
             for (i=0; i<rs->nr_nodes; i++) {        
                 attr = (domAttrNode*)rs->nodes[i];
                 namePtr = Tcl_NewStringObj (attr->nodeName, -1);
                 rc = Tcl_ListObjAppendElement(interp, value, namePtr);
                 if (rc != TCL_OK) {
                     Tcl_DecrRefCount (namePtr);
                     return rc;
                 }
                 namePtr = Tcl_NewStringObj (attr->nodeValue, attr->valueLength);
                 rc = Tcl_ListObjAppendElement(interp, value, namePtr);
                 if (rc != TCL_OK) {
                     Tcl_DecrRefCount (namePtr);
                     return rc;
                 }
             }
             break;

        case AttrValueSetResult:
             Tcl_SetStringObj (type, "attrvalues",-1);
             Tcl_SetListObj (value, 0, NULL);
             for (i=0; i<rs->nr_nodes; i++) {        
                 attr = (domAttrNode*)rs->nodes[i];
                 namePtr = Tcl_NewStringObj (attr->nodeValue, attr->valueLength);
                 rc = Tcl_ListObjAppendElement(interp, value, namePtr);
                 if (rc != TCL_OK) {
                     Tcl_DecrRefCount (namePtr);
                     return rc;
                 }
             }
             break;
     } 
     return TCL_OK;
}


/*----------------------------------------------------------------------------
|   tcldom_xpathFuncCallBack
|
\---------------------------------------------------------------------------*/
static
int tcldom_xpathFuncCallBack (
    void            *clientData,
    char            *functionName,
    domNode         *ctxNode,
    int              position,
    xpathResultSet  *nodeList,
    int              argc,
    xpathResultSets *args,
    xpathResultSet  *result, 
    char           **errMsg
)
{   
    Tcl_Interp  *interp = (Tcl_Interp*) clientData;
    char         tclxpathFuncName[200], objCmdName[40];
    char         *errStr, *typeStr, *nodeName;
    Tcl_Obj     *resultPtr, *objv[MAX_REWRITE_ARGS], *type, *value, *nodeObj;
    Tcl_CmdInfo  cmdInfo;
    int          objc, rc, i, errStrLen, listLen, intValue;
    double       doubleValue;
    domNode     *node;
    
    DBG(fprintf(stderr, 
                "tcldom_xpathFuncCallBack functionName=%s position=%d argc=%d\n", 
                functionName, position, argc);)
                     
    sprintf (tclxpathFuncName, "::dom::xpathFunc::%s", functionName);
    DBG(fprintf(stderr, "testing %s\n", tclxpathFuncName);)
    rc = Tcl_GetCommandInfo (interp, tclxpathFuncName, &cmdInfo);
    if (!rc) {
        *errMsg = (char*)strdup("Tcl unknown function!");
        return XPATH_EVAL_ERR;     
    }  
    if (!cmdInfo.isNativeObjectProc) {
        *errMsg = (char*)strdup("can't access Tcl level method!");
        return XPATH_EVAL_ERR;     
    }
    if ( (5+(2*argc)) >= MAX_REWRITE_ARGS) {
        *errMsg = (char*)strdup("too many args to call Tcl level method!");
        return XPATH_EVAL_ERR;     
    }
    objc = 0;
    objv[objc++] = Tcl_NewStringObj(tclxpathFuncName, -1);
    tcldom_createNodeObj (interp, ctxNode, objCmdName);
    objv[objc++] = Tcl_NewStringObj (objCmdName, -1);

    objv[objc++] = Tcl_NewIntObj (position);
 
    type  = Tcl_NewObj();
    value = Tcl_NewObj();
    tcldom_xpathResultSet (interp, nodeList, type, value);
    objv[objc++] = type;
    objv[objc++] = value;
    
    for (i=0; i<argc; i++) { 
        type  = Tcl_NewObj();
        value = Tcl_NewObj();
        tcldom_xpathResultSet (interp, args[i], type, value);
        objv[objc++] = type;
        objv[objc++] = value;
    }
    rc = (cmdInfo.objProc (cmdInfo.objClientData, interp, objc, objv));    
    if (rc == TCL_OK) {
        xpathRSInit (result);
        resultPtr = Tcl_GetObjResult(interp);
        rc = Tcl_ListObjLength (interp, resultPtr, &listLen);
        if (rc == TCL_OK) {
            if (listLen == 1) {
                rsSetString (result, Tcl_GetStringFromObj(resultPtr, NULL) );
                return XPATH_OK;
            }
            if (listLen != 2) {
                *errMsg = (char*)strdup("wrong return tuple! Must be {type value} !");
                return XPATH_EVAL_ERR;
            }
            rc = Tcl_ListObjIndex (interp, resultPtr, 0, &type);
            rc = Tcl_ListObjIndex (interp, resultPtr, 1, &value);
            typeStr = Tcl_GetStringFromObj( type, NULL);
            if (strcmp(typeStr, "bool")==0) {
                rc = Tcl_GetBooleanFromObj (interp, value, &intValue);
                rsSetBool(result, intValue );
            } else
            if (strcmp(typeStr, "number")==0) {
                rc = Tcl_GetIntFromObj (interp, value, &intValue);
                if (rc == TCL_OK) {
                    rsSetInt(result, intValue );
                } else {
                    rc = Tcl_GetDoubleFromObj (interp, value, &doubleValue);
                    rsSetReal(result, doubleValue );
                }
            } else
            if (strcmp(typeStr, "string")==0) {
                rsSetString(result, Tcl_GetStringFromObj(value,NULL) );
            } else
            if (strcmp(typeStr, "nodes")==0) {
                rc = Tcl_ListObjLength (interp, value, &listLen);
                if (rc != TCL_OK) {
                    *errMsg = strdup("value not a node list!");
                    return XPATH_EVAL_ERR;
                }
                for (i=0; i < listLen; i++) {
                    rc = Tcl_ListObjIndex (interp, value, i, &nodeObj);
                    nodeName = Tcl_GetStringFromObj (nodeObj, NULL);
                    node = tcldom_getNodeFromName (interp, nodeName, &errStr);
                    if (node == NULL) {
                        *errMsg = strdup(errStr);
                        return XPATH_EVAL_ERR;
                    }
                    rsAddNode (result, node);
                }
            } else
            if (strcmp(typeStr, "attrnodes")==0) { 
                *errMsg = strdup("attrnodes not implemented yet!");
                return XPATH_EVAL_ERR;
            } else
            if (strcmp(typeStr, "attrvalues")==0) {
                rsSetString(result, Tcl_GetStringFromObj(value,NULL) );
            }    
        } else {
            fprintf(stderr, "ListObjLength != TCL_OK --> returning XPATH_EVAL_ERR \n");
            return XPATH_EVAL_ERR;
        }
        return XPATH_OK;
    }
    errStr = Tcl_GetStringFromObj( Tcl_GetObjResult(interp), &errStrLen);
    *errMsg = (char*)malloc(120+strlen(functionName) + errStrLen);
    strcpy(*errMsg, "Tcl error while executing XPATH extension function '");
    strcat(*errMsg, functionName );
    strcat(*errMsg, "':\n" );
    strcat(*errMsg, errStr);
    Tcl_DecrRefCount(Tcl_GetObjResult(interp));
    
    DBG( fprintf(stderr, "returning XPATH_EVAL_ERR \n"); )
    return XPATH_EVAL_ERR;
}


/*----------------------------------------------------------------------------
|   tcldom_selectNodes
|
\---------------------------------------------------------------------------*/
static
int tcldom_selectNodes (
    Tcl_Interp *interp,    
    domNode    *node,
    Tcl_Obj    *obj,
    Tcl_Obj    *typeObj
)
{   
    char          *xpathQuery, *typeVar;
    char          *errMsg = NULL;
    int            xpathQueryLen;
    int            rc;
    xpathResultSet rs;    
    Tcl_Obj       *type;
    xpathCBs       cbs;
    
    xpathQuery = Tcl_GetStringFromObj( obj, &xpathQueryLen);

    xpathRSInit( &rs );
    
    cbs.funcCB         = tcldom_xpathFuncCallBack;
    cbs.funcClientData = interp;
    cbs.varCB          = NULL;
    cbs.varClientData  = NULL;

    rc = xpathEval (node, node, xpathQuery, &cbs, &errMsg, &rs);
    /*rc = xpathEval (node, xpathQuery, &cbs, &errMsg, &rs);*/
 
    if (rc != XPATH_OK) {
        xpathRSFree( &rs );
        SetResult ( errMsg);
        DBG( fprintf(stderr, "errMsg = %s \n", errMsg); ) 
        if (errMsg) free(errMsg);
        return TCL_ERROR;
    }
    if (errMsg) free(errMsg);
    typeVar = NULL;
    if (typeObj != NULL) {
        typeVar = Tcl_GetStringFromObj (typeObj, NULL);
    }
    type = Tcl_NewObj();
    Tcl_IncrRefCount(type);
    DBG( 
      fprintf(stderr, "before tcldom_xpathResultSet \n"); 
    )
    tcldom_xpathResultSet (interp, &rs, type, Tcl_GetObjResult (interp));
    DBG(
      fprintf(stderr, "after tcldom_xpathResultSet \n"); 
    )
    if (typeVar) {
        Tcl_SetVar(interp,typeVar, Tcl_GetStringFromObj(type, NULL), 0);
    }
    Tcl_DecrRefCount(type);

    xpathRSFree( &rs );
    return TCL_OK;
}



/*----------------------------------------------------------------------------
|   tcldom_appendFromTclList
|
\---------------------------------------------------------------------------*/
static
int tcldom_appendFromTclList (
    Tcl_Interp *interp,    
    domNode    *node,
    Tcl_Obj    *obj
)
{   
    int     i, rc, length, valueLength, attrLength, attrValueLength, childListLength;
    Tcl_Obj *lnode, *tagNameObj, *valueObj, 
            *attrListObj, *attrObj, *childListObj, *childObj;
    char    *tag_name, *value, *attrName, *attrValue;
    domNode *newnode;
    
    
    /*-------------------------------------------------------------------------
    |   check format of Tcl list node
    \------------------------------------------------------------------------*/
    lnode = obj;
    if ((rc = Tcl_ListObjLength(interp, lnode, &length)) != TCL_OK) {
        return rc;
    }
    if ((length != 3) && (length != 2)) {
        SetResult ( "invalid node list format!");
        return TCL_ERROR;
    }

    /*-------------------------------------------------------------------------
    |   create node
    \------------------------------------------------------------------------*/
    if ((rc = Tcl_ListObjIndex (interp, lnode, 0, &tagNameObj)) != TCL_OK) {
        return rc;        
    }    
    tag_name = Tcl_GetStringFromObj (tagNameObj, NULL);

    if ((strcmp(tag_name,"#cdata")==0) || (strcmp(tag_name,"#text")==0)) {
        if (length != 2) {
            SetResult ( "invalid text node list format!");
            return TCL_ERROR;
        }
        /*----------------------------------------------------------------------
        |   create text node
        \---------------------------------------------------------------------*/
        if ((rc = Tcl_ListObjIndex (interp, lnode, 1, &valueObj)) != TCL_OK) {
            return rc;
        }
        value = Tcl_GetStringFromObj (valueObj, &valueLength);
        newnode = (domNode*)domNewTextNode(node->ownerDocument, value, valueLength, TEXT_NODE);
        domAppendChild (node, newnode);
        return TCL_OK;
    }

    /*-------------------------------------------------------------------------
    |   create element node
    \------------------------------------------------------------------------*/
    newnode = domNewElementNode(node->ownerDocument, tag_name, ELEMENT_NODE);
    domAppendChild (node, newnode);
    
    /*-------------------------------------------------------------------------
    |   create atributes
    \------------------------------------------------------------------------*/
    if (length > 1) {
        if ((rc = Tcl_ListObjIndex (interp, lnode, 1, &attrListObj)) != TCL_OK) {
            return rc;        
        }    
        if ((rc = Tcl_ListObjLength(interp, attrListObj, &attrLength)) != TCL_OK) {
            return rc;
        }
        for (i=0; i<attrLength; i++) {

            if ((rc = Tcl_ListObjIndex (interp, attrListObj, i, &attrObj)) != TCL_OK) {
                return rc;
            }
            attrName = Tcl_GetStringFromObj (attrObj, NULL);
            i++;        
        
            if ((rc = Tcl_ListObjIndex (interp, attrListObj, i, &attrObj)) != TCL_OK) {
                return rc;
            }
            attrValue = Tcl_GetStringFromObj (attrObj, &attrValueLength);
        
            domSetAttribute (newnode, attrName, attrValue);
        }
    }

    /*-------------------------------------------------------------------------
    |   add child nodes
    \------------------------------------------------------------------------*/
    if (length == 3) {
        if ((rc = Tcl_ListObjIndex (interp, lnode, 2, &childListObj)) != TCL_OK) {
            return rc;        
        }    
        if ((rc = Tcl_ListObjLength(interp, childListObj, &childListLength)) != TCL_OK) {
           return rc;
        }
        for (i=0; i<childListLength; i++) {
            if ((rc = Tcl_ListObjIndex (interp, childListObj, i, &childObj)) != TCL_OK) {
                return rc;        
            }    
            if ((rc = tcldom_appendFromTclList (interp, newnode, childObj)) != TCL_OK) {
                return rc;
            }
        }
    }
    return tcldom_returnNodeObj (interp, node, 0, NULL);
}


/*----------------------------------------------------------------------------
|   tcldom_treeAsTclList
|
\---------------------------------------------------------------------------*/
static
Tcl_Obj * tcldom_treeAsTclList (
    Tcl_Interp *interp,    
    domNode    *node
)
{
    Tcl_Obj *name, *value;
    Tcl_Obj *attrsList, *attrName, *attrValue;
    Tcl_Obj *childList;
    Tcl_Obj *objv[4];
    int     result;
    domNode     *child;
    domAttrNode *attrs;
    
    

    if ((node->nodeType == TEXT_NODE) || (node->nodeType == CDATA_SECTION_NODE)) {

        value  = Tcl_NewStringObj ( ((domTextNode*)node)->nodeValue, 
                                    ((domTextNode*)node)->valueLength); 
        objv[0] = Tcl_NewStringObj ("#text", -1);
        objv[1] = value;
        return Tcl_NewListObj (2, objv);
    }
    
    name = Tcl_NewStringObj (node->nodeName, -1);

    attrsList = Tcl_NewListObj (0, NULL);
    attrs = node->firstAttr;
    while (attrs) {
        attrName = Tcl_NewStringObj (attrs->nodeName, -1);
        attrValue = Tcl_NewStringObj (attrs->nodeValue, attrs->valueLength);
        Tcl_ListObjAppendElement (interp, attrsList, attrName);
        Tcl_ListObjAppendElement (interp, attrsList, attrValue);
        attrs = attrs->nextSibling;
    }

    childList = Tcl_NewListObj (0, NULL);
    if (node->nodeType == ELEMENT_NODE) {
        child = node->firstChild;
        while (child != NULL) {

            result = Tcl_ListObjAppendElement(interp, childList,
                                              tcldom_treeAsTclList(interp,child));
            if (result != TCL_OK) {
                return NULL;
            }
            child = child->nextSibling;
        }
    }
    
    objv[0] = name;
    objv[1] = attrsList;
    objv[2] = childList;
    
    return Tcl_NewListObj (3, objv);
}


/*----------------------------------------------------------------------------
|   tcldom_AppendEscaped
|
\---------------------------------------------------------------------------*/
static
void tcldom_AppendEscaped (
    Tcl_Obj    *xmlString,
    Tcl_Channel chan,
    char       *value,
    int         value_length
)
{
#define APESC_BUF_SIZE 512
#define AP(c)  *b++ = c;
    char  buf[APESC_BUF_SIZE + 80], *b, *bLimit,  *pc, *pEnd;
#if !TclOnly8Bits
    int i, clen = 0;
#endif

    b = buf;
    bLimit = b + APESC_BUF_SIZE;    
    pc = pEnd = value;
    if (value_length != -1) {
        pEnd = pc + value_length;
    }
    while (   (value_length == -1 && *pc)
           || (value_length != -1 && pc != pEnd)
    ) {
        if (*pc == '"') { AP('&') AP('q') AP('u') AP('o') AP('t') AP(';')
        } else 
        if (*pc == '&') { AP('&') AP('a') AP('m') AP('p') AP(';')
        } else 
        if (*pc == '<') { AP('&') AP('l') AP('t') AP(';')
        } else 
        if (*pc == '>') { AP('&') AP('g') AP('t') AP(';')
#if TclOnly8Bits                              
        } else {
            AP(*pc)
        }
#else
        } else {
        if ((unsigned char)*pc > 127) {
                clen = UtfCount (*pc);
                if (!clen) {
                    fprintf (stderr, "can only handle UTF-8 chars up to 3 bytes long.");
                    exit(1);
                }
            for (i = 0; i < clen; i++) {
                AP(*pc);
                pc++;
            }
            pc--;
            } else
                AP(*pc);
        }
#endif
        if (b >= bLimit) {
            writeChars(xmlString, chan, buf, b - buf);
            b = buf;
        }
        pc++;
    }
    if (b > buf) {
        writeChars(xmlString, chan, buf, b - buf);
    }
}


/*----------------------------------------------------------------------------
|   tcldom_tolower
|
\---------------------------------------------------------------------------*/
void tcldom_tolower (
    char *str,
    char *str_out,
    int  len
)
{
    char *p;
    int  i;
    
    len--; i = 0; p = str_out;
    while (*str && (i < len)) {
        *p++ = tolower(*str++);
        i++;
    }
    *p++ = '\0';
}


/*----------------------------------------------------------------------------
|   tcldom_treeAsHTML
|
\---------------------------------------------------------------------------*/
static
void tcldom_treeAsHTML (
    Tcl_Obj     *htmlString,
    domNode     *node,
    Tcl_Channel  chan 
)
{
    int          empty;
    domNode     *child;
    domAttrNode *attrs;
    char         tag[80], attrName[80];
        
    if (node->nodeType == CDATA_SECTION_NODE ||
        node->nodeType == PROCESSING_INSTRUCTION_NODE) return;

    if (node->nodeType == TEXT_NODE) {
        if (node->nodeFlags & DISABLE_OUTPUT_ESCAPING) {
            writeChars (htmlString, chan, ((domTextNode*)node)->nodeValue, 
                        ((domTextNode*)node)->valueLength);
        } else {
            tcldom_AppendEscaped (htmlString, chan,
                                  ((domTextNode*)node)->nodeValue, 
                                  ((domTextNode*)node)->valueLength);
        }
        return;
    }

    if (node->nodeType == COMMENT_NODE) {
        writeChars (htmlString, chan, "<!--", 4);
        writeChars (htmlString, chan, ((domTextNode*)node)->nodeValue,
   		                   ((domTextNode*)node)->valueLength);
        writeChars (htmlString, chan,  "-->", 3);
        return;

    }

    tcldom_tolower(node->nodeName, tag, 80);
    writeChars (htmlString, chan, "<", 1);
    writeChars (htmlString, chan, tag, -1);

    attrs = node->firstAttr;
    while (attrs) {
        tcldom_tolower(attrs->nodeName, attrName, 80);
        writeChars (htmlString, chan, " ", 1);
        writeChars (htmlString, chan, attrName, -1);
        writeChars (htmlString, chan, "=\"", 2);
        tcldom_AppendEscaped (htmlString, chan, attrs->nodeValue, -1);
        writeChars (htmlString, chan, "\"", 1);
        attrs = attrs->nextSibling;
    }
    writeChars (htmlString, chan, ">", 1);
    
    
    /*-----------------------------------------------------------
    |   check for empty HTML tags 
    \----------------------------------------------------------*/
    empty = 0;
    switch (tag[0]) {
        case 'a':  if (!strcmp(tag,"area"))     empty = 1; break;
        case 'b':  if (!strcmp(tag,"br")     || 
                       !strcmp(tag,"base")   || 
                       !strcmp(tag,"basefont")) empty = 1; break;
        case 'c':  if (!strcmp(tag,"col"))      empty = 1; break;
        case 'f':  if (!strcmp(tag,"frame"))    empty = 1; break;
        case 'h':  if (!strcmp(tag,"hr"))       empty = 1; break;
        case 'i':  if (!strcmp(tag,"img")   || 
                       !strcmp(tag,"input") || 
                       !strcmp(tag,"isindex"))  empty = 1; break;
        case 'l':  if (!strcmp(tag,"link"))     empty = 1; break;
        case 'm':  if (!strcmp(tag,"meta"))     empty = 1; break;
        case 'p':  if (!strcmp(tag,"param"))    empty = 1; break;
/*          case 'p':  if (!strcmp(tag,"p")   || */
/*                         !strcmp(tag,"param"))    empty = 1; break; */
    }   
    if (empty) { 
/*          if (node->nextSibling && node->nextSibling->nodeType != TEXT_NODE) { */
/*              writeChars (htmlString, chan, "\n", 1); */
/*          } */
        /* strange ! should not happen ! */
        child = node->firstChild;
        while (child != NULL) {
            tcldom_treeAsHTML (htmlString, child, chan);
            child = child->nextSibling;
        }
        return;
    }

    if (node->nodeType == ELEMENT_NODE) {
        child = node->firstChild;
        if ((child != NULL) && (child != node->lastChild)
            && (child->nodeType != TEXT_NODE)) {
            writeChars (htmlString, chan, "\n", 1);
        }
        while (child != NULL) {
            tcldom_treeAsHTML (htmlString, child, chan);
            child = child->nextSibling;
        }
        if ((node->firstChild != NULL) && (node->firstChild != node->lastChild)
            && (node->lastChild->nodeType != TEXT_NODE)) {
            writeChars (htmlString, chan, "\n", 1);
        }
    }
    writeChars (htmlString, chan, "</", 2);
    writeChars (htmlString, chan, tag, -1);
    writeChars (htmlString, chan, ">",  1);
    /* The following code is problematic because of things like
       <i>s</i><sub>2</sub> */
/*      if (node->nextSibling && node->nextSibling->nodeType != TEXT_NODE) { */
/*          writeChars (htmlString, chan, "\n", 1); */
/*      } */
}


/*----------------------------------------------------------------------------
|   tcldom_initNamespaceHandling
|
\---------------------------------------------------------------------------*/
static
void tcldom_initNamespaceHandling (
    domNode *node
)
{
    domAttrNode *attr;
    domNS *ns;
    
    if (node->nodeType == ATTRIBUTE_NODE) {
        attr = (domAttrNode*)node;
        ns = attr->parentNode->ownerDocument->namespaces;
    } else
    if (node->nodeType == ELEMENT_NODE) {
        ns = node->ownerDocument->namespaces;
    } else {
       return;
    }
    while (ns) {
        ns->used = 0;   /* reset used/seen flag for later use */
        ns = ns->next;
    }                                                   
}


/*----------------------------------------------------------------------------
|   tcldom_treeAsXML
|
\---------------------------------------------------------------------------*/
static
void tcldom_treeAsXML (
    Tcl_Obj    *xmlString,
    domNode    *node,
    int         indent,
    int         level,
    int         doIndent,
    Tcl_Channel chan
)
{
    domAttrNode *attrs;
    domNode     *child;
    domNS       *ns, *ans;
    int          first, hasElements, i, newNs;
        
    if (node->nodeType == TEXT_NODE) {
        if (node->nodeFlags & DISABLE_OUTPUT_ESCAPING) {
            writeChars (xmlString, chan, ((domTextNode*)node)->nodeValue, 
                        ((domTextNode*)node)->valueLength);
        } else {
            tcldom_AppendEscaped (xmlString, chan,
                                  ((domTextNode*)node)->nodeValue, 
                                  ((domTextNode*)node)->valueLength);
        }
        return;
    }

    if (node->nodeType == CDATA_SECTION_NODE) {
        writeChars (xmlString, chan, "<![CDATA[", 9);       
        writeChars (xmlString, chan, ((domTextNode*)node)->nodeValue, 
                                     ((domTextNode*)node)->valueLength);
        writeChars (xmlString, chan, "]]>", 3); 
        return;
    }

    if (node->nodeType == COMMENT_NODE) {
        writeChars (xmlString, chan, "<!--", 4);
        writeChars (xmlString, chan, ((domTextNode*)node)->nodeValue,
   		                     ((domTextNode*)node)->valueLength);
        writeChars (xmlString, chan, "-->", 3);
        return;

    }

    if ((indent != -1) && doIndent) {
        for(i=0; i<level; i++) {
            writeChars (xmlString, chan, "        ", indent);
        }
    }

    if (node->nodeType == PROCESSING_INSTRUCTION_NODE) {
        writeChars (xmlString, chan, "<?", 2);
        writeChars (xmlString, chan, ((domProcessingInstructionNode*)node)->targetValue,
                                     ((domProcessingInstructionNode*)node)->targetLength);
        writeChars (xmlString, chan, " ", 1);
        writeChars (xmlString, chan, ((domProcessingInstructionNode*)node)->dataValue,
                                    ((domProcessingInstructionNode*)node)->dataLength);
        writeChars (xmlString, chan, "?>", 2);
        if (indent != -1) writeChars (xmlString, chan, "\n", 1); 
        return;
    }

    writeChars (xmlString, chan, "<", 1);
    writeChars (xmlString, chan, node->nodeName, -1);
    
    newNs = 0; 
    ns = node->ownerDocument->namespaces;    
    while ((ns != NULL) && (ns->index != node->namespace)) {
        ns = ns->next;
    }
    if (ns) {
        if (!ns->used) {
            /* first seen this element namespace --> print definition */
            writeChars (xmlString, chan, " ", 1);
            if (ns->prefix && (ns->prefix[0] !='\0')) {
                writeChars (xmlString, chan, "xmlns:", 6);
                writeChars (xmlString, chan, ns->prefix, -1);
            } else {
                writeChars (xmlString, chan, "xmlns", 5);
            }
            writeChars (xmlString, chan, "=\"", 2);
            writeChars (xmlString, chan, ns->uri, -1);
            writeChars (xmlString, chan, "\"", 1);
            ns->used = 1;
            newNs = 1;
        }
    }
    
    attrs = node->firstAttr;
    while (attrs) {
        ans = node->ownerDocument->namespaces;    
        while ((ans != NULL) && (ans->index != attrs->namespace)) {
            ans = ans->next;
        }
        if (ans) {
            if (!ans->used) {
                /* first seen this element namespace --> print definition */
                writeChars (xmlString, chan, " ", 1);
                if (ans->prefix && (ans->prefix[0] !='\0')) {
                    writeChars (xmlString, chan, "xmlns:", 6);
                    writeChars (xmlString, chan, ans->prefix, -1);
                } else {
                    writeChars (xmlString, chan, "xmlns", 5);
                }
                writeChars (xmlString, chan, "=\"", 2);
                writeChars (xmlString, chan, ans->uri, -1);
                writeChars (xmlString, chan, "\"", 1);
            }
        }
        writeChars (xmlString, chan, " ", 1);
        writeChars (xmlString, chan, attrs->nodeName, -1);
        writeChars (xmlString, chan, "=\"", 2);
        tcldom_AppendEscaped (xmlString, chan, attrs->nodeValue, -1);
        writeChars (xmlString, chan, "\"", 1);
        attrs = attrs->nextSibling;
    }

    hasElements = 0;
    first       = 1; 
    doIndent    = 1;
    
    if (node->nodeType == ELEMENT_NODE) {
        child = node->firstChild;
        while (child != NULL) {

            if ( (child->nodeType == ELEMENT_NODE)
               ||(child->nodeType == PROCESSING_INSTRUCTION_NODE) ) 
            {
                hasElements = 1;
            } 
            if (first) {
                writeChars (xmlString, chan, ">", 1);
                if ((indent != -1) && hasElements) {
                    writeChars (xmlString, chan, "\n", 1);
                }
            }
            first = 0;
            tcldom_treeAsXML (xmlString, child, indent, level+1, doIndent, chan);
            doIndent = 0;
            if ( (child->nodeType == ELEMENT_NODE) 
               ||(child->nodeType == PROCESSING_INSTRUCTION_NODE) ) 
            {
               doIndent = 1;
            }
            child = child->nextSibling;
        }
    }

    if (first) {
        if (indent != -1) {
            writeChars (xmlString, chan, "/>\n", 3);
        } else {
            writeChars (xmlString, chan, "/>",   2);
        }
    } else {
        if ((indent != -1) && hasElements) {
            for(i=0; i<level; i++) {
                writeChars (xmlString, chan, "        ", indent);
            }
        } 
        writeChars (xmlString, chan, "</", 2);
        writeChars (xmlString, chan, node->nodeName, -1);
        if (indent != -1) {
            writeChars (xmlString, chan, ">\n", 2);
        } else {
            writeChars (xmlString, chan, ">",   1);
        }    
    }
    if (ns) {
        if (newNs) ns->used = 0;
    }
}

/*----------------------------------------------------------------------------
|   findBaseURI   
|
\---------------------------------------------------------------------------*/
char *findBaseURI (
    domNode *node
)
{
    char *baseURI = NULL;
    Tcl_HashEntry *entryPtr;

    do {
        if (node->nodeFlags & HAS_BASEURI) {
            entryPtr = Tcl_FindHashEntry (node->ownerDocument->baseURIs,
                                          (char*)node->nodeNumber);
            baseURI = (char *)Tcl_GetHashValue (entryPtr);
            break;
        }
        if (node->previousSibling) {
            node = node->previousSibling;
        }
        else {
            node = node->parentNode;
        }
    } while (node);
    return baseURI;
}

/*----------------------------------------------------------------------------
|   tcldom_NodeObjCmd   
|
\---------------------------------------------------------------------------*/
int tcldom_NodeObjCmd (
    ClientData  clientData,
    Tcl_Interp *interp,
    int         objc,
    Tcl_Obj    * CONST objv[] 
) 
{
    GetTcldomTSD()
    domNode     *node, *child, *refChild, *oldChild;
    domDocument *xsltDoc, *resultDoc;
    domNS       *ns;
    domAttrNode *attrs;
    domException exception;
    char         tmp[200], objCmdName[40], prefix[MAX_PREFIX_LEN],
                *method, *nodeName, *str, *localName, *channelId,
                *attr_name, *attr_val, *filter, *option, *errMsg, *uri;
    int          result, length, methodIndex, i, line, column, indent, mode;
    Tcl_Obj     *namePtr, *resultPtr;
    Tcl_Obj     *mobjv[MAX_REWRITE_ARGS];
    Tcl_CmdInfo  cmdInfo;
    Tcl_Channel  chan = (Tcl_Channel) NULL;
    Tcl_HashEntry *entryPtr;

    static char *nodeMethods[] = {
        "firstChild",      "nextSibling",    "getAttribute",    "nodeName",
        "nodeValue",       "nodeType",       "attributes",      "asList",
        "find",            "setAttribute",   "removeAttribute", "parentNode",
        "previousSibling", "lastChild",      "appendChild",     "removeChild",
        "hasChildNodes",   "localName",      "childNodes",      "ownerDocument",
        "insertBefore",    "replaceChild",   "getLine",         "getColumn",  
        "asXML",           "appendFromList", "child",           "fsibling",
        "psibling",        "descendant",     "ancestor",        "text",
        "root",            "hasAttribute",   "cloneNode",       "appendXML",
        "target",          "data",           "selectNodes",     "namespaceURI",
        "getAttributeNS",  "setAttributeNS", "hasAttributeNS",  "removeAttributeNS", 
        "asHTML",          "prefix",         "getBaseURI",      "appendFromScript", 
        "xslt",            "toXPath",        "delete",          "getElementById",
        "getElementsByTagName",              "getElementsByTagNameNS",             
        NULL
    };
    enum nodeMethod {
        m_firstChild,      m_nextSibling,    m_getAttribute,    m_nodeName,
        m_nodeValue,       m_nodeType,       m_attributes,      m_asList,
        m_find,            m_setAttribute,   m_removeAttribute, m_parentNode,
        m_previousSibling, m_lastChild,      m_appendChild,     m_removeChild,
        m_hasChildNodes,   m_localName,      m_childNodes,      m_ownerDocument,
        m_insertBefore,    m_replaceChild,   m_getLine,         m_getColumn,
        m_asXML,           m_appendFromList, m_child,           m_fsibling,
        m_psibling,        m_descendant,     m_ancestor,        m_text,
        m_root,            m_hasAttribute,   m_cloneNode,       m_appendXML,
        m_target,          m_data,           m_selectNodes,     m_namespaceURI,
        m_getAttributeNS,  m_setAttributeNS, m_hasAttributeNS,  m_removeAttributeNS,
        m_asHTML,          m_prefix,         m_getBaseURI,      m_appendFromScript,
        m_xslt,            m_toXPath,        m_delete,          m_getElementById,
        m_getElementsByTagName,              m_getElementsByTagNameNS,     
    };


    node = (domNode*) clientData;
    TSD(dontCreateObjCommands) = 0;
    if (node == NULL) {  
        TSD(dontCreateObjCommands) = 1;
        nodeName = Tcl_GetStringFromObj (objv[1], NULL);
        node = tcldom_getNodeFromName (interp, nodeName, &errMsg);
        if (node == NULL) {
            SetResult ( errMsg );
            return TCL_ERROR;
        } 
        objc--;
        objv++;
    }
    CheckArgs(2,10,1,node_usage);
    if (Tcl_GetIndexFromObj(interp, objv[1], nodeMethods, "method", 0,
            &methodIndex) != TCL_OK) {

        method = Tcl_GetStringFromObj (objv[1], NULL);
        if (*method != '@') {
            /*--------------------------------------------------------
            |   not a getAttribute short cut:
            |   try to find method implemented as normal Tcl proc
            \-------------------------------------------------------*/
            result = 0;
            if (node->nodeType == ELEMENT_NODE) {
                /*----------------------------------------------------
                |   try to find Tcl level node specific method proc
                |
                |       ::dom::domNode::<nodeName>::<method>
                |
                \---------------------------------------------------*/
                sprintf (tmp, "::dom::domNode::%s::%s",
                              (char*)node->nodeName, method);
                DBG(fprintf(stderr, "testing %s\n", tmp);)
                result = Tcl_GetCommandInfo (interp, tmp, &cmdInfo);
            }
            if (!result) {
                /*----------------------------------------------------
                |   try to find Tcl level general method proc
                |
                |       ::dom::domNode::<method>
                |
                \---------------------------------------------------*/
                sprintf (tmp, "::dom::domNode::%s",method);
                DBG(fprintf(stderr, "testing %s\n", tmp);)
                result = Tcl_GetCommandInfo (interp, tmp, &cmdInfo);
            }
            if (!result) {
                SetResult ( node_usage);
                return TCL_ERROR;
            }  
            if (!cmdInfo.isNativeObjectProc) {
                SetResult ( "can't access Tcl level method!");
                return TCL_ERROR;   
            }
            if (objc >= MAX_REWRITE_ARGS) {
                SetResult ( "too many args to call Tcl level method!");
                return TCL_ERROR;
            }
            mobjv[0] = objv[1];
            mobjv[1] = objv[0];
            for (i=2; i<objc; i++) mobjv[i] = objv[i];
            return (cmdInfo.objProc (cmdInfo.objClientData, interp, objc, mobjv));    
        }

        /*--------------------------------------------------------
        |   @<attributeName>: try to look up attribute
        \-------------------------------------------------------*/
        Tcl_ResetResult (interp);
        CheckArgs(2,3,1,"@<attributeName> ?defaultvalue?");
        if ((node->nodeType != ELEMENT_NODE) &&
	    (node->nodeType != PROCESSING_INSTRUCTION_NODE)) {
	   SetResult ( "NOT_AN_ELEMENT : there are no attributes");
	   return TCL_ERROR;
        } 
        attrs = node->firstAttr;
        while (attrs && strcmp(attrs->nodeName, &(method[1]) )) {
            attrs = attrs->nextSibling;
        }
        if (attrs) {
            SetResult ( attrs->nodeValue );
        } else {       
            if (objc == 3) {        
                SetResult ( Tcl_GetStringFromObj (objv[2], NULL) );
            } else {
                sprintf (tmp, "attribute %80.80s not found!", &(method[1]) );
                SetResult ( tmp);
                return TCL_ERROR;
            }
        }
        return TCL_OK;
    }

    /*----------------------------------------------------------------------
    |   dispatch the node object method
    |
    \---------------------------------------------------------------------*/
    switch ((enum nodeMethod) methodIndex ) {

        case m_toXPath:
            SetResult ( xpathNodeToXPath(node) );
            return TCL_OK;
            
        case m_xslt:
            nodeName = Tcl_GetStringFromObj (objv[2], NULL);
            xsltDoc = tcldom_getDocumentFromName (interp, nodeName, &errMsg);
            if (xsltDoc == NULL) {
                SetResult ( errMsg );
                return TCL_ERROR;
            }
            result = xsltProcess (xsltDoc, node, 
                                 tcldom_xpathFuncCallBack,  interp,
                                 &errMsg, &resultDoc);
            if (result < 0) {
                SetResult ( errMsg );
                free (errMsg);
                return TCL_ERROR;
            }
            return tcldom_returnDocumentObj(
                       interp, resultDoc, (objc == 4), objv[3]
                   );  
            
        case m_selectNodes: 
            CheckArgs(3,4,2,"xpathQuery");
            if (objc == 4) {
                return tcldom_selectNodes( interp, node, objv[2], objv[3]);
            } else {       
                return tcldom_selectNodes( interp, node, objv[2], NULL );
            }
            
        case m_find:
            CheckArgs(4,5,2,"attrName attrVal ?nodeObjVar?");
            attr_name = Tcl_GetStringFromObj (objv[2], NULL); 
            attr_val  = Tcl_GetStringFromObj (objv[3], &length); 
            return tcldom_returnNodeObj( 
                       interp,
                       tcldom_find (node, attr_name, attr_val, length),
                       (objc == 5),
                       objv[4]
                   );

        case m_child:
            CheckArgs(3,6,2,"instance|all ?type? ?attr value?");
            return tcldom_xpointerSearch (interp, XP_CHILD, node, objc, objv);

        case m_descendant:
            CheckArgs(3,6,2,"instance|all ?type? ?attr value?");
            return tcldom_xpointerSearch (interp, XP_DESCENDANT, node, objc, objv);

        case m_ancestor:
            CheckArgs(3,6,2,"instance|all ?type? ?attr value?");
            return tcldom_xpointerSearch (interp, XP_ANCESTOR, node, objc, objv);

        case m_fsibling:
            CheckArgs(3,6,2,"instance|all ?type? ?attr value?");
            return tcldom_xpointerSearch (interp, XP_FSIBLING, node, objc, objv);

        case m_psibling:
            CheckArgs(3,6,2,"instance|all ?type? ?attr value?");
            return tcldom_xpointerSearch (interp, XP_PSIBLING, node, objc, objv);
            
        case m_root:
            CheckArgs(2,3,2,"?nodeObjVar?");
            while (node->parentNode) {
                node = node->parentNode;
            }
            return tcldom_returnNodeObj( 
                       interp, node, (objc == 3), objv[2]
            );
            
        case m_text:
            CheckArgs(2,2,2,"");
            if (node->nodeType != ELEMENT_NODE) {
                SetResult ( "NOT_AN_ELEMENT");
                return TCL_ERROR;
            }
            Tcl_ResetResult (interp);
            child = node->firstChild;
            while (child) {
                if ((child->nodeType == TEXT_NODE) ||
                    (child->nodeType == CDATA_SECTION_NODE)) {
                    Tcl_AppendToObj (Tcl_GetObjResult (interp), 
                                     ((domTextNode*)child)->nodeValue, 
                                     ((domTextNode*)child)->valueLength);
                }
                child = child->nextSibling;
            }
            return TCL_OK;
            
        case m_attributes:        
            CheckArgs(2,3,2,"?nameFilter?");
            if (node->nodeType != ELEMENT_NODE) {
                SetResult ( "NOT_AN_ELEMENT : there are no attributes");
                return TCL_ERROR;
            }
            if (objc == 3) {
                filter = Tcl_GetStringFromObj (objv[2], NULL); 
            } else {
                filter = "*";
            }
            Tcl_ResetResult(interp);
            resultPtr = Tcl_GetObjResult(interp);

            attrs = node->firstAttr;
            while (attrs != NULL) {
                if (Tcl_StringMatch( (char*)attrs->nodeName, filter)) {
                
                    if (attrs->namespace == 0) {
                        namePtr = Tcl_NewStringObj ((char*)attrs->nodeName, -1);
                    } else {
                        domSplitQName((char*)attrs->nodeName, prefix, &localName);
                        mobjv[0] = Tcl_NewStringObj( (char*)localName, -1);
                        mobjv[1] = Tcl_NewStringObj( domNamespacePrefix((domNode*)attrs), -1); 
                        mobjv[2] = Tcl_NewStringObj( domNamespaceURI((domNode*)attrs), -1);
                        namePtr  = Tcl_NewListObj(3, mobjv);
                    }                    
                    result = Tcl_ListObjAppendElement(interp, resultPtr,
                                                      namePtr);
                    if (result != TCL_OK) {
                        Tcl_DecrRefCount (namePtr);
                        return result;
                    }
                }
                attrs = attrs->nextSibling;
            }
            break;

        case m_asList:
            Tcl_SetObjResult (interp, tcldom_treeAsTclList(interp, node) );
            break;

        case m_asXML:
            if ((objc != 2) && (objc != 4) && (objc != 6)) {
                Tcl_WrongNumArgs(interp, 2, objv, "?-indent <0..8>? ?-channel <channelID>?");
                return TCL_ERROR;
            }
            indent = 4;
            while (objc > 2) {
                option = Tcl_GetStringFromObj (objv[2], NULL);
                if (strcmp (option, "-indent") == 0)  {
                    if (strcmp("none", Tcl_GetStringFromObj (objv[3], NULL))==0) {
                        indent = -1;
                    } 
                    else if (strcmp("no", Tcl_GetStringFromObj (objv[3], NULL))==0) {
                        indent = -1;
                    } 
                    else if (Tcl_GetIntFromObj(interp, objv[3], &indent) != TCL_OK) {
                        SetResult ( "indent must be an integer (0..8) or 'no'/'none'");
                        return TCL_ERROR;
                    }
                    objc -= 2;
                    objv += 2;
                    continue;
                }
                else if (strcmp (option, "-channel") == 0) {
                    channelId = Tcl_GetStringFromObj (objv[3], NULL);
                    chan = Tcl_GetChannel (interp, channelId, &mode);
                    if (chan == (Tcl_Channel) NULL) {
                        return TCL_ERROR;
                    }
                    if ((mode & TCL_WRITABLE) == 0) {
                        Tcl_AppendResult(interp, "channel \"", channelId,
                                "\" wasn't opened for writing", (char *) NULL);
                        return TCL_ERROR;
                    }
                    objc -= 2;
                    objv += 2;
                    continue;
                }
                else {
                    SetResult ("-indent and -channel are the only recognized options");
                    return TCL_ERROR;
                }
            }
            if (indent > 8)  indent = 8;
            if (indent < -1) indent = -1;            
            resultPtr = Tcl_GetObjResult(interp);
            Tcl_SetStringObj (resultPtr, "", -1);
            tcldom_initNamespaceHandling(node);
            tcldom_treeAsXML(resultPtr, node, indent, 0, 1, chan);
            break;

        case m_asHTML:
            if ((objc != 2) && (objc != 4)) {
                Tcl_WrongNumArgs(interp, 2, objv, "?-channel <channelId>?");
                return TCL_ERROR;
            }
            if (objc == 4) {
                option = Tcl_GetStringFromObj (objv[2], NULL);
                if (strcmp (option, "-channel") != 0) {
                    SetResult ("-channel <channelId> is the only recognized option");
                    return TCL_ERROR;
                }
                channelId = Tcl_GetStringFromObj (objv[3], NULL);
                chan = Tcl_GetChannel (interp, channelId, &mode);
                if (chan == (Tcl_Channel) NULL) {
                    return TCL_ERROR;
                }
                if ((mode & TCL_WRITABLE) == 0) {
                    Tcl_AppendResult(interp, "channel \"", channelId,
                                     "\" wasn't opened for writing", (char *) NULL);
                    return TCL_ERROR;
                }
            }
            resultPtr = Tcl_GetObjResult(interp);
            Tcl_SetStringObj (resultPtr, "", -1);
            tcldom_treeAsHTML(resultPtr, node, chan);
            break;

        case m_getAttribute:
            CheckArgs(3,3,2,"attrName");
            if (node->nodeType != ELEMENT_NODE) {
                SetResult ( "NOT_AN_ELEMENT : there are no attributes");
                return TCL_ERROR;
            }
            attr_name = Tcl_GetStringFromObj (objv[2], NULL);
            attrs = node->firstAttr;
            while (attrs && strcmp(attrs->nodeName, attr_name)) {
                attrs = attrs->nextSibling;
            }
            if (attrs) {
                SetResult ( attrs->nodeValue );
                return TCL_OK;
            }
            if (objc == 4) {        
                SetResult ( Tcl_GetStringFromObj (objv[3], NULL) );
                return TCL_OK;
            } else {
                sprintf (tmp, "attribute %80.80s not found!", attr_name);
                SetResult ( tmp);
                return TCL_ERROR;
            }
            break;
            
        case m_getAttributeNS:
            CheckArgs(4,4,2,"uri localName");
            if (node->nodeType != ELEMENT_NODE) {
                SetResult ( "NOT_AN_ELEMENT : there are no attributes");
                return TCL_ERROR;
            }
            uri = Tcl_GetStringFromObj (objv[2], NULL);
            localName = Tcl_GetStringFromObj (objv[3], NULL);
            attrs = node->firstAttr;
            while (attrs) {
                domSplitQName (attrs->nodeName, prefix, &str);
                if (strcmp(localName,str)==0) {
                    ns = domGetNamespaceByIndex(node->ownerDocument, attrs->namespace);
                    if (strcmp(ns->uri, uri)==0) {
                        SetResult ( attrs->nodeValue );
                        return TCL_OK;
                    }
                }
                attrs = attrs->nextSibling;
            }
            sprintf (tmp, "attribute with localName %80.80s not found!",localName);
            SetResult (tmp);
            return TCL_ERROR;

#if 0
            uri = Tcl_GetStringFromObj (objv[2], NULL);
            attr_name = Tcl_GetStringFromObj (objv[3], NULL);
            domSplitQName ((char*)attr_name, prefix, &localName);
            ns = domLookupNamespace (node->ownerDocument, prefix, uri);
            if (ns == NULL) {
                SetResult ("namespace not found!");
                return TCL_ERROR;
            }                                                                 
            attrs = node->firstAttr;
            while (attrs && 
                   strcmp(attrs->nodeName, attr_name) && 
                   (attrs->namespace != ns->index)
            ) {
                attrs = attrs->nextSibling;
            }
            if (attrs) {
                SetResult ( attrs->nodeValue );
                return TCL_OK;
            }
            if (objc == 5) {        
                SetResult ( Tcl_GetStringFromObj (objv[4], NULL) );
                return TCL_OK;
            } else {
                sprintf (tmp, "attribute %80.80s not found!", attr_name);
                SetResult ( tmp);
                return TCL_ERROR;
            }
            break;
#endif

        case m_setAttribute:
            if (node->nodeType != ELEMENT_NODE) {
                SetResult ( "NOT_AN_ELEMENT : there are no attributes");
                return TCL_ERROR;
            }
            if ((objc < 2) || ((objc % 2)!=0)) {
                SetResult ( "attrName value  pairs expected");
                return TCL_ERROR;
            }
            for ( i = 2;  i < objc; ) {
                attr_name = Tcl_GetStringFromObj (objv[i++], NULL);
                attr_val  = Tcl_GetStringFromObj (objv[i++], NULL);
                domSetAttribute (node, attr_name, attr_val);
            }
            return tcldom_returnNodeObj (interp, node, 0, NULL);
            
        case m_setAttributeNS:
            CheckArgs(5,5,2,"uri attrName attrVal");
            if (node->nodeType != ELEMENT_NODE) {
                SetResult ( "NOT_AN_ELEMENT : there are no attributes");
                return TCL_ERROR;
            }
            uri       = Tcl_GetStringFromObj (objv[2], NULL);
            attr_name = Tcl_GetStringFromObj (objv[3], NULL);
            attr_val  = Tcl_GetStringFromObj (objv[4], NULL);
            domSetAttributeNS (node, attr_name, attr_val, uri);
            return tcldom_returnNodeObj (interp, node, 0, NULL);

        case m_hasAttribute:
            CheckArgs(3,3,2,"attrName");
            if (node->nodeType != ELEMENT_NODE) {		
                SetResult ( "NOT_AN_ELEMENT : there are no attributes");
                return TCL_ERROR;
            }
            attr_name = Tcl_GetStringFromObj (objv[2], NULL);
            attrs = node->firstAttr;
            while (attrs && strcmp(attrs->nodeName, attr_name)) {
                attrs = attrs->nextSibling;
            }
            if (attrs) {
                SetResult ( "1" );
                return TCL_OK;
            }        
            SetResult ( "0");
            return TCL_OK;
            
        case m_hasAttributeNS:
            CheckArgs(4,4,2,"uri localName");
            if (node->nodeType != ELEMENT_NODE) {		
                SetResult ( "NOT_AN_ELEMENT : there are no attributes");
                return TCL_ERROR;
            }
            uri = Tcl_GetStringFromObj (objv[2], NULL);
            localName = Tcl_GetStringFromObj (objv[3], NULL);
            attrs = node->firstAttr;
            while (attrs) {
                domSplitQName (attrs->nodeName, prefix, &str);
                if (strcmp(localName,str)==0) {
                    ns = domGetNamespaceByIndex(node->ownerDocument, attrs->namespace);
                    if (strcmp(ns->uri, uri)==0) {
                        SetResult("1");
                        return TCL_OK;
                    }
                }
                attrs = attrs->nextSibling;
            }
#if 0              
            attr_name = Tcl_GetStringFromObj (objv[3], NULL);
            domSplitQName ((char*)attr_name, prefix, &localName);
            ns = domLookupNamespace (node->ownerDocument, prefix, uri);
            if (ns == NULL) {
                SetResult ("0");
                return TCL_OK;
            }                                                                 
            attrs = node->firstAttr;
            while (attrs && 
                   strcmp(attrs->nodeName, attr_name) && 
                   (attrs->namespace != ns->index)
            ) {
                attrs = attrs->nextSibling;
            }
            if (attrs) {
                SetResult ( "1" );
                return TCL_OK;
            }        
#endif            
            SetResult ( "0");
            return TCL_OK;

        case m_removeAttribute:
            CheckArgs(3,3,2,"attrName");
            if (node->nodeType != ELEMENT_NODE) {		
                SetResult ( "NOT_AN_ELEMENT : there are no attributes");
                return TCL_ERROR;
            }
            attr_name = Tcl_GetStringFromObj (objv[2], NULL); 
            result = domRemoveAttribute (node, attr_name);
            if (result) {
                SetResult ( "can't remove attribute '");
                AppendResult (attr_name);
                AppendResult ("'");
                return TCL_ERROR;
            }
            return tcldom_returnNodeObj (interp, node, 0, NULL);
            
        case m_removeAttributeNS:
            CheckArgs(4,4,2,"uri attrName");
            if (node->nodeType != ELEMENT_NODE) {		
                SetResult ( "NOT_AN_ELEMENT : there are no attributes");
                return TCL_ERROR;
            }
            uri = Tcl_GetStringFromObj (objv[2], NULL);
            localName = Tcl_GetStringFromObj (objv[3], NULL); 
            result = domRemoveAttributeNS (node, uri, localName);
            if (result < 0) {
                SetResult ( "can't remove attribute with localName '");
                AppendResult (localName);
                AppendResult ("'");
                return TCL_ERROR;
            }
            return tcldom_returnNodeObj (interp, node, 0, NULL);

        case m_nextSibling:
            return tcldom_returnNodeObj( 
                       interp, node->nextSibling, (objc == 3), objv[2]
            );

        case m_previousSibling:
            return tcldom_returnNodeObj( 
                       interp, node->previousSibling, (objc == 3), objv[2]
            );

        case m_firstChild:
            if (node->nodeType == ELEMENT_NODE) {
                return tcldom_returnNodeObj( 
                           interp, node->firstChild, (objc == 3), objv[2]
                );
            }
            return tcldom_returnNodeObj( 
                           interp, NULL, (objc == 3), objv[2]
            );

        case m_lastChild:
            if (node->nodeType == ELEMENT_NODE) {
                return tcldom_returnNodeObj( 
                           interp, node->lastChild, (objc == 3), objv[2]
                );
            }
            return tcldom_returnNodeObj( 
                           interp, NULL, (objc == 3), objv[2]
            );

        case m_parentNode:
            return tcldom_returnNodeObj( 
                       interp, node->parentNode, (objc == 3), objv[2]
            );

        case m_appendFromList: 
            CheckArgs(3,3,2,"list");
            return tcldom_appendFromTclList( interp, node, objv[2] );

        case m_appendFromScript: 
            if (nodecmd_appendFromTclScript (interp, node, objv[2]) != TCL_OK) {
                return TCL_ERROR;
            }
            return tcldom_returnNodeObj (interp, node, 0, NULL);
            /* return tcldom_appendFromTclScript( interp, node, objv[2] ); */

        case m_appendXML: 
            CheckArgs(3,3,2,"xmlString");
            return tcldom_appendXML( interp, node, objv[2] );

        case m_appendChild:
            CheckArgs(3,3,2,"nodeToAppend");
            nodeName = Tcl_GetStringFromObj (objv[2], NULL);
            child = tcldom_getNodeFromName (interp, nodeName, &errMsg);
            if (child == NULL) {
                SetResult ( errMsg );
                return TCL_ERROR;
            } 
            if (node->parentNode && (node->parentNode == child->parentNode)) {
                SetResult ( "child already appended!");
                return TCL_ERROR;            
            }
            exception = domAppendChild (node, child); 
            if (exception != OK) {
                SetResult ( domException2String(exception));
                return TCL_ERROR;
            }
            return tcldom_returnNodeObj (interp, child, 0, NULL);

        case m_cloneNode:
            CheckArgs(2,3,2,"?-deep?");
            if (objc == 3) {
                if (strcmp(Tcl_GetStringFromObj (objv[2], NULL),"-deep")==0) {
                    return tcldom_returnNodeObj (interp, domCloneNode(node,1), 0, NULL);
                }
                SetResult ( "unknown option! Options: ?-deep? ");
                return TCL_ERROR; 
            }
            return tcldom_returnNodeObj (interp, domCloneNode(node, 0), 0, NULL);

        case m_removeChild:
            CheckArgs(3,3,2,"childToRemove");
            nodeName = Tcl_GetStringFromObj (objv[2], NULL);
            child = tcldom_getNodeFromName (interp, nodeName, &errMsg);
            if (child == NULL) {
                SetResult ( errMsg );
                return TCL_ERROR;
            } 
            result = domRemoveChild (node, child);
            if (result) {
                SetResult ( "NOT_FOUND_ERR : can't remove child '");
                AppendResult (nodeName);
                AppendResult ("'");
                return TCL_ERROR;
            }
            return tcldom_returnNodeObj (interp, child, 0, NULL);
            
        case m_insertBefore:
            CheckArgs(4,4,2,"childToInsert refChild");
            nodeName = Tcl_GetStringFromObj (objv[2], NULL);
            child = tcldom_getNodeFromName (interp, nodeName, &errMsg);
            if (child == NULL) {
                SetResult ( errMsg );
                return TCL_ERROR;
            } 
            if (node->parentNode && (node->parentNode == child->parentNode)) {
                SetResult ( "child already appended!");
                return TCL_ERROR;            
            }

            nodeName = Tcl_GetStringFromObj (objv[3], NULL);
            refChild = tcldom_getNodeFromName (interp, nodeName, &errMsg);
            if (refChild == NULL) {
                SetResult ( errMsg );
                return TCL_ERROR;
            } 
            exception = domInsertBefore (node, child, refChild);
            if (exception != OK) {
                SetResult ( domException2String(exception));
                return TCL_ERROR;
            }
            return tcldom_returnNodeObj (interp, child, 0, NULL);
            
        case m_replaceChild:
            nodeName = Tcl_GetStringFromObj (objv[2], NULL);
            child = tcldom_getNodeFromName (interp, nodeName, &errMsg);
            if (child == NULL) {
                SetResult ( errMsg );
                return TCL_ERROR;
            } 

            nodeName = Tcl_GetStringFromObj (objv[3], NULL);
            oldChild = tcldom_getNodeFromName (interp, nodeName, &errMsg);
            if (oldChild == NULL) {
                SetResult ( errMsg );
                return TCL_ERROR;
            } 
            exception = domReplaceChild (node, child, oldChild);
            if (exception != OK) {
                SetResult ( domException2String(exception));
                return TCL_ERROR;
            }
            return tcldom_returnNodeObj (interp, oldChild, 0, NULL);

        case m_hasChildNodes:
            if (node->nodeType == ELEMENT_NODE) {
                SetIntResult( node->firstChild ? 1 : 0);
            } else {
                SetIntResult( 0 );
            }
            break;

        case m_childNodes:
            resultPtr = Tcl_GetObjResult(interp);
            if (node->nodeType == ELEMENT_NODE) {
                child = node->firstChild;
                while (child != NULL) {
                    tcldom_createNodeObj (interp, child, objCmdName);
                    namePtr = Tcl_NewStringObj (objCmdName, -1);
                    result  = Tcl_ListObjAppendElement(interp, resultPtr,
                                                           namePtr);
                    if (result != TCL_OK) {
                        Tcl_DecrRefCount (namePtr);
                        return result;
                    }
                    child = child->nextSibling;
                }
            }
            break;

        case m_getElementsByTagName:
            return tcldom_getElementsByTagName (
                       interp, Tcl_GetStringFromObj (objv[2], NULL), node, -1
            );
            
        case m_getElementsByTagNameNS:
            CheckArgs(4,4,2,"uri name");
            uri = Tcl_GetStringFromObj (objv[2], NULL);
            str = Tcl_GetStringFromObj (objv[3], NULL);
            domSplitQName ((char*)str, prefix, &localName);
            ns = domLookupNamespace (node->ownerDocument, prefix, uri);
            if (ns == NULL) {
                SetResult ("namespace not found");
                return TCL_ERROR;
            }                                               
            return tcldom_getElementsByTagName (
                       interp, str, node, ns->index
            );

        case m_getElementById:
            CheckArgs(3,3,2,"id");
            str = Tcl_GetStringFromObj(objv[2], NULL);
            entryPtr = Tcl_FindHashEntry (node->ownerDocument->ids, str);
            if (entryPtr) {
                return tcldom_returnNodeObj (interp, 
                                             (domNode*) Tcl_GetHashValue (entryPtr), 
                                             0, NULL);
            }                                                                                  
            SetResult ( "id not found");
            return TCL_ERROR;

        case m_nodeName:  
            if (node->nodeType == ELEMENT_NODE) {                
                SetResult ( (char*)node->nodeName);
            } else
            if (node->nodeType == TEXT_NODE) {                
                SetResult ("#text");                
            } else 
            if (node->nodeType == PROCESSING_INSTRUCTION_NODE) {
                Tcl_SetStringObj (Tcl_GetObjResult (interp), 
                                  ((domProcessingInstructionNode*)node)->targetValue, 
                                  ((domProcessingInstructionNode*)node)->targetLength);
            } else {
                SetResult ("");                
            }
            break;

        case m_nodeValue:
            if (node->nodeType == ELEMENT_NODE) {
                Tcl_SetStringObj (Tcl_GetObjResult (interp), "", 0);
            } else {
                Tcl_SetStringObj (Tcl_GetObjResult (interp), 
                                  ((domTextNode*)node)->nodeValue, 
                                  ((domTextNode*)node)->valueLength);
            }
            if (objc == 3) {
                attr_val = Tcl_GetStringFromObj(objv[2], &length);
                exception = domSetNodeValue(node, attr_val, length);
                if (exception != OK) {
                    SetResult ( domException2String(exception));
                    return TCL_ERROR;
                }
            }
            break;

        case m_nodeType:
           switch (node->nodeType) {
               case ELEMENT_NODE:  
                    SetResult ( "ELEMENT_NODE");
                    break;
               case ATTRIBUTE_NODE:  
                    SetResult ( "ATTRIBUTE_NODE");
                    break;
               case TEXT_NODE:  
                    SetResult ( "TEXT_NODE");
                    break;
               case CDATA_SECTION_NODE:  
                    SetResult ( "CDATA_SECTION_NODE");
                    break;
               case COMMENT_NODE:  
                    SetResult ( "COMMENT_NODE");
                    break;
	       case PROCESSING_INSTRUCTION_NODE:
                    SetResult ( "PROCESSING_INSTRUCTION_NODE");
                    break;
               default:
                    SetResult ( "unknown nodeType!");
                    return TCL_ERROR;
            }
            break;

        case m_prefix:
            str = domNamespacePrefix(node);
            if (str) {
                SetResult (str);
            } else {
                SetResult ("");
            }
            return TCL_OK;
            
        case m_namespaceURI:
            str = domNamespaceURI(node);
            if (str) {
                SetResult (str);
            } else {
                SetResult ("");
            }
            return TCL_OK;
            
        case m_localName:
            if (node->nodeType == ELEMENT_NODE) {                
                if (node->namespace != 0) {
                    SetResult ( domGetLocalName((char*)node->nodeName) );
                    break;
                }
            }
            SetResult ("");
            break;
            
        case m_ownerDocument:
            return tcldom_returnDocumentObj( 
                       interp, node->ownerDocument, (objc == 3), objv[2]
            );
         
        case m_target:
            if (node->nodeType != PROCESSING_INSTRUCTION_NODE) {
                SetResult ( "not a PROCESSING_INSTRUCTION_NODE!");
                return TCL_ERROR;
            } else {
                Tcl_SetStringObj (Tcl_GetObjResult (interp), 
                                  ((domProcessingInstructionNode*)node)->targetValue, 
                                  ((domProcessingInstructionNode*)node)->targetLength);
            }
            break;

        case m_delete:
            domDeleteNode (node, tcldom_docDeleteNode, interp);
            break;
                        
        case m_data:
            if (node->nodeType == PROCESSING_INSTRUCTION_NODE) {
                Tcl_SetStringObj (Tcl_GetObjResult (interp), 
                                  ((domProcessingInstructionNode*)node)->dataValue, 
                                  ((domProcessingInstructionNode*)node)->dataLength);
            } else
            if (node->nodeType == TEXT_NODE || node->nodeType == CDATA_SECTION_NODE) {
                Tcl_SetStringObj (Tcl_GetObjResult (interp), 
                                  ((domTextNode*)node)->nodeValue, 
                                  ((domTextNode*)node)->valueLength);
            } else {
                SetResult ("not a TEXT_NODE / CDATA_SECTION_NODE / PROCESSING_INSTRUCTION_NODE !");
                return TCL_ERROR;
            }
            break;

        case m_getLine:
            if (domGetLineColumn (node, &line, &column) < 0) {
                SetResult ( "no line/column information available!");
                return TCL_ERROR;                                
            }
            SetIntResult (line);
            break;

        case m_getColumn:
            if (domGetLineColumn (node, &line, &column) < 0) {
                SetResult ( "no line/column information available!");
                return TCL_ERROR;                                
            }
            SetIntResult (column);
            break;

        case m_getBaseURI:
            str = findBaseURI (node);
            if (!str) {
                SetResult ("no base URI information available!");
                return TCL_ERROR;
            } else {
                SetResult (str);
            }
            break;
    }
    return TCL_OK;
}


/*----------------------------------------------------------------------------
|   tcldom_DocObjCmd
|
\---------------------------------------------------------------------------*/
static
int tcldom_DocObjCmd (
    ClientData  clientData,
    Tcl_Interp *interp,
    int         objc,
    Tcl_Obj    *objv[]
) 
{
    TcldomDocDeleteInfo * dinfo;
    domDocument         * doc;
    char                * method, *tag, *data, *target, *uri, tmp[100], objCmdName[40];
    int                   methodIndex, result, data_length, target_length, i;
    domNode             * n;    
    Tcl_CmdInfo           cmdInfo;
    Tcl_Obj             * mobjv[MAX_REWRITE_ARGS];    


    static char *docMethods[] = {
        "documentElement", "getElementsByTagName",       "delete",
        "createElement",   "createCDATASection",         "createTextNode", 
        "createComment",   "createProcessingInstruction", 
        "createElementNS", "getDefaultOutputMethod",
        NULL
    };
    enum docMethod {
        m_documentElement,  m_getElementsByTagName,       m_delete,
        m_createElement,    m_createCDATASection,         m_createTextNode,
        m_createComment,    m_createProcessingInstruction,
        m_createElementNS,  m_getdefaultoutputmethod
    };
   
    if (objc < 2) {
        SetResult ( domObj_usage);
        return TCL_ERROR;
    }
    method = Tcl_GetStringFromObj (objv[1], NULL);    
    if (Tcl_GetIndexFromObj(interp, objv[1], docMethods, "method", 0,
                            &methodIndex) != TCL_OK) 
    {
        /*--------------------------------------------------------
        |   try to find method implemented as normal Tcl proc
        \-------------------------------------------------------*/
        sprintf (tmp, "::dom::domDoc::%s",method);
        DBG(fprintf(stderr, "testing %s\n", tmp);)
        result = Tcl_GetCommandInfo (interp, tmp, &cmdInfo);
        if (!result) {
            SetResult ( domObj_usage);
            return TCL_ERROR;
        }  
        if (!cmdInfo.isNativeObjectProc) {
            SetResult ( "can't access Tcl level method!");
            return TCL_ERROR;   
        }
        if (objc >= MAX_REWRITE_ARGS) {
            SetResult ( "too many args to call Tcl level method!");
            return TCL_ERROR;
        }
        mobjv[0] = objv[1];
        mobjv[1] = objv[0];
        for (i=2; i<objc; i++) mobjv[i] = objv[i];
        return (cmdInfo.objProc (cmdInfo.objClientData, interp, objc, mobjv));    
    }

    CheckArgs (2,4,1,domObj_usage);
    
    /*----------------------------------------------------------------------
    |   dispatch the doc object method
    |
    \---------------------------------------------------------------------*/
    dinfo = (TcldomDocDeleteInfo*) clientData; 
    doc = dinfo->document;        
    
    switch ((enum docMethod) methodIndex ) {
    
        case m_documentElement:
            return tcldom_returnNodeObj( 
                    interp, doc->documentElement, (objc == 3), objv[2]
            );
  
        case m_getElementsByTagName:
            CheckArgs (3,3,2,"elementName");
            return tcldom_getElementsByTagName (interp, 
                                                Tcl_GetStringFromObj (objv[2], NULL),
                                                doc->documentElement, -1
            );
   
        case m_createElement:
            CheckArgs (3,4,2,"elementName ?newObjVar?");
            tag = Tcl_GetStringFromObj (objv[2], NULL);
            n = domNewElementNode(doc, tag, ELEMENT_NODE);
            return tcldom_returnNodeObj (interp,
                                         n, (objc == 4), objv[3]
            );
       
        case m_createElementNS:
            CheckArgs (4,5,2,"elementName uri ?newObjVar?");
            uri = Tcl_GetStringFromObj (objv[2], NULL);
            tag = Tcl_GetStringFromObj (objv[3], NULL);
            n = domNewElementNodeNS(doc, tag, uri, ELEMENT_NODE);
            return tcldom_returnNodeObj (interp,
                                         n, (objc == 5), objv[4]
            );

        case m_createTextNode:
            CheckArgs (3,4,2,"data ?newObjVar?");
            data = Tcl_GetStringFromObj (objv[2], &data_length);
            n = (domNode*)domNewTextNode(doc, data, data_length, TEXT_NODE);
            return tcldom_returnNodeObj (interp, 
                                         n, (objc == 4), objv[3]
            );
 
        case m_createCDATASection:
            CheckArgs (3,4,2,"data ?newObjVar?");
            data = Tcl_GetStringFromObj (objv[2], &data_length);
            n = (domNode*)domNewTextNode(doc, data, data_length, CDATA_SECTION_NODE);
            return tcldom_returnNodeObj (interp, 
                                         n, (objc == 4), objv[3]
            );
       
        case m_createComment:
            CheckArgs (3,4,2,"data ?newObjVar?");
            data = Tcl_GetStringFromObj (objv[2], &data_length);
            n = (domNode*)domNewTextNode(doc, data, data_length, COMMENT_NODE);
            return tcldom_returnNodeObj (interp, 
                                         n, (objc == 4), objv[3]
            );

        case m_createProcessingInstruction:
            CheckArgs (4,5,2,"target data ?newObjVar?");
            target = Tcl_GetStringFromObj (objv[2], &target_length);
            data   = Tcl_GetStringFromObj (objv[3], &data_length);
            n = (domNode*)domNewProcessingInstructionNode(doc, 
                                                          target, target_length, 
                                                          data,   data_length);
            return tcldom_returnNodeObj (interp,
                                         n, (objc == 5), objv[4]
            );
            
        case m_delete:
            CheckArgs (2,2,2,"");
            sprintf (objCmdName, "domDoc%d", doc->documentNumber );
            Tcl_DeleteCommand ( interp, objCmdName );
            SetResult ( "");
            return TCL_OK;

        case m_getdefaultoutputmethod:
            CheckArgs (2,2,2,"");
            if (doc->nodeFlags & OUTPUT_DEFAULT_XML) {
                SetResult ("xml");
            } else 
            if (doc->nodeFlags & OUTPUT_DEFAULT_HTML) {
                SetResult ("html");
            } else 
            if (doc->nodeFlags & OUTPUT_DEFAULT_TEXT) {
                SetResult ("text");
            } else 
            if (doc->nodeFlags & OUTPUT_DEFAULT_UNKOWN) {
                SetResult ("unknown");
            } else {
                SetResult ("none");
            }
            return TCL_OK;
            
    }
    SetResult ( domObj_usage);
    return TCL_ERROR;
}





/*----------------------------------------------------------------------------
|   tcldom_createDocument
|
\---------------------------------------------------------------------------*/
static 
int tcldom_createDocument (
    ClientData  clientData,
    Tcl_Interp *interp,
    int         objc,
    Tcl_Obj    * const objv[]
) 
{
    int          setVariable = 0;
    domDocument *doc;
    Tcl_Obj     *newObjName = NULL;    
    
    
    CheckArgs (2,3,1,"docElemName ?newObjVar?");
    
    if (objc == 3) {    
        newObjName = objv[2];
        setVariable = 1;
    }
    doc = domCreateDocument ( Tcl_GetStringFromObj (objv[1], NULL) );
    return tcldom_returnDocumentObj( 
                 interp, doc, setVariable, newObjName
    );
}


/*----------------------------------------------------------------------------
|   tcldom_setResultEncoding
|
\---------------------------------------------------------------------------*/
static 
int tcldom_setResultEncoding (
    ClientData  clientData,
    Tcl_Interp *interp,
    int         objc,
    Tcl_Obj    * const objv[]
) 
{
    GetTcldomTSD()
    TEncoding *encoding;
    char      *encodingName;
    
    CheckArgs (1,2,1,"?encodingName?");
    if (objc == 1) { 
        if (TSD(Encoding_to_8bit) == NULL) {
            Tcl_AppendResult(interp, "UTF-8", NULL);   
        } else {
            Tcl_AppendResult(interp, TSD(Encoding_to_8bit->name), NULL);
        }
        return TCL_OK;
    }
    encodingName = Tcl_GetStringFromObj (objv[1], NULL);
    if ( (strcmp(encodingName, "UTF-8")==0)
       ||(strcmp(encodingName, "UTF8")==0)
       ||(strcmp(encodingName, "utf-8")==0)
       ||(strcmp(encodingName, "utf8")==0)) {
        
        TSD(Encoding_to_8bit) = NULL;
    } else {
        encoding = tdom_GetEncoding ( encodingName );
        if (encoding == NULL) {
             Tcl_AppendResult(interp, "encoding not found", NULL);            
             return TCL_ERROR;         
        }
        TSD(Encoding_to_8bit) = encoding;
    }
    return TCL_OK;
}


/*----------------------------------------------------------------------------
|   tcldom_parse
|
\---------------------------------------------------------------------------*/
static 
int tcldom_parse (
    ClientData  clientData,
    Tcl_Interp *interp,
    int         objc,
    Tcl_Obj    * const objv[]
) 
{
    GetTcldomTSD()
    char        *xml_string, *option, *errStr, *channelId, *baseURI = NULL;
    int          xml_string_len, mode;
    int          ignoreWhiteSpaces   = 1;
    int          takeSimpleParser    = 0;
    int          takeHTMLParser      = 0;
    int          takeNameSpaceParser = 0;
    int          setVariable         = 0;
    int          feedbackAfter       = 0;
    domDocument *doc;
    Tcl_Obj     *newObjName = NULL, *extResolver = NULL;
    XML_Parser   parser;
    Tcl_Channel  chan = (Tcl_Channel) NULL;
    
    
    while (objc > 1) {
        option = Tcl_GetStringFromObj( objv[1], NULL);
        if (strcmp(option,"-keepEmpties")==0) {
            ignoreWhiteSpaces = 0;
            objv++;  objc--; continue;
        }
        if (strcmp(option,"-simple")==0) {
            takeSimpleParser = 1;
            objv++;  objc--; continue;
        }
        if (strcmp(option,"-html")==0) {
            takeSimpleParser = 1;
            takeHTMLParser = 1;
            objv++;  objc--; continue;
        }
        if (strcmp(option,"-ns")==0) {
            takeNameSpaceParser = 1;
            objv++;  objc--; continue;
        }
        if (strcmp(option,"-feedbackAfter")==0) {
            objv++; objc--;
            Tcl_GetIntFromObj (interp, objv[1], &feedbackAfter);
            objv++; objc--;
            continue;
        }
        if (strcmp(option, "-channel")==0) {
            objv++; objc--;
            if (objc > 1) {
                channelId = Tcl_GetStringFromObj (objv[1], NULL);
            } else {
                SetResult (dom_usage);
                return TCL_ERROR;
            }
            chan = Tcl_GetChannel (interp, channelId, &mode);
            if (chan == (Tcl_Channel) NULL) {
                return TCL_ERROR;
            }
            if ((mode & TCL_READABLE) == 0) {
                Tcl_AppendResult(interp, "channel \"", channelId,
                                 "\" wasn't opened for reading", (char *) NULL);
                return TCL_ERROR;
            }
            objv++; objc--;
            continue;
        }
        if (strcmp(option, "-baseurl")==0) {
            objv++; objc--;
            if (objc > 1) {
                baseURI = Tcl_GetStringFromObj (objv[1], NULL);
            } else {
                SetResult (dom_usage);
                return TCL_ERROR;
            }
            objv++; objc--;
            continue;
        }
        if (strcmp(option, "-externalentitycommand")==0) {
            objv++; objc--;
            if (objc > 1) {
                extResolver = objv[1];
                Tcl_IncrRefCount (objv[1]);
            } else {
                SetResult (dom_usage);
                return TCL_ERROR;
            }
            objv++; objc--;
            continue;
        }
        if (objc == 2 || objc == 3) {
            break;
        }
        SetResult (dom_usage);
        return TCL_ERROR;
    }
    
    if (chan == NULL) {
        if (objc < 2) {
            SetResult (dom_usage);
            return TCL_ERROR;
        }
        xml_string = TCLGETBYTES( objv[1], &xml_string_len);
        if (objc == 3) {    
            newObjName = objv[2];
            setVariable = 1;
        }
    } else {
        xml_string = NULL;
        if (takeSimpleParser || takeHTMLParser) {
            Tcl_AppendResult(interp, 
                "simple/HTML parser don't allow channel reading!", NULL);
            return TCL_ERROR;                                 
        }
        if (objc == 2) {    
            newObjName = objv[1];
            setVariable = 1;
        }
    }

    
    if (takeSimpleParser) {
    
        char s[50];
        int  byteIndex, i;
        
        errStr = NULL;
        if (takeHTMLParser) {
            doc = HTML_SimpleParseDocument(xml_string, ignoreWhiteSpaces, 
                                           &byteIndex, &errStr);
        } else {
            doc = XML_SimpleParseDocument(xml_string, ignoreWhiteSpaces, 
                                          &byteIndex, &errStr);
        }
        if (errStr != NULL) {
        
            Tcl_ResetResult(interp);
            sprintf(s, "%d", byteIndex);
            Tcl_AppendResult(interp, "error \"", errStr,
                                           "\" at position ", s, NULL);
            if (byteIndex != -1) {
                Tcl_AppendResult(interp, "\n\"", NULL);
                s[1] = '\0';
                for (i=-80; i < 80; i++) {
                    if ((byteIndex+i)>=0) {
                        if (xml_string[byteIndex+i]) {
                            s[0] = xml_string[byteIndex+i];
                            Tcl_AppendResult(interp, s, NULL);
                            if (i==0) {
                                Tcl_AppendResult(interp, " <--Error-- ", NULL);
                            }
                        } else {
                            break;
                        }
                    }
                }
                Tcl_AppendResult(interp, "\"",NULL);            
            }
            if (takeHTMLParser) {
                free(errStr);
            }
            return TCL_ERROR;
        }
        return tcldom_returnDocumentObj( 
                interp, doc, setVariable, newObjName
        );
    }
    
#ifdef TDOM_NO_EXPAT
    Tcl_AppendResult(interp, "tDOM was compiled without Expat!", NULL);
    return TCL_ERROR;
#else        
    if (takeNameSpaceParser) {
        parser = XML_ParserCreateNS(NULL, ':');
    } else {
        parser = XML_ParserCreate(NULL);
    }

    doc = domReadDocument (parser, xml_string, 
                                   xml_string_len,
                                   ignoreWhiteSpaces, 
                                   TSD(Encoding_to_8bit),
                                   TSD(storeLineColumn),
                                   feedbackAfter,
                                   chan,
                                   baseURI,
                                   extResolver,
                                   interp);
    if (doc == NULL) {
        char s[50];
        long byteIndex, i;
        
        Tcl_ResetResult(interp);
        sprintf(s, "%d", XML_GetCurrentLineNumber(parser));
        Tcl_AppendResult(interp, "error \"", XML_ErrorString(XML_GetErrorCode(parser)),
                                 "\" at line ", s, " character ", NULL);
        sprintf(s, "%d", XML_GetCurrentColumnNumber(parser));
        Tcl_AppendResult(interp, s, NULL);
        byteIndex = XML_GetCurrentByteIndex(parser);
        if ((byteIndex != -1) && (chan == NULL)) {
             Tcl_AppendResult(interp, "\n\"", NULL);
             s[1] = '\0';
             for (i=-20; i < 40; i++) {
                 if ((byteIndex+i)>=0) {
                     if (xml_string[byteIndex+i]) {
                         s[0] = xml_string[byteIndex+i];
                         Tcl_AppendResult(interp, s, NULL);
                         if (i==0) {
                             Tcl_AppendResult(interp, " <--Error-- ", NULL);
                         }
                     } else {
                         break;
                     }
                 }
             }
             Tcl_AppendResult(interp, "\"",NULL);            
        }
        XML_ParserFree(parser); 
        return TCL_ERROR;
    }
    XML_ParserFree(parser); 
    
    return tcldom_returnDocumentObj( 
               interp, doc, setVariable, newObjName
    );
#endif
    
}



/*----------------------------------------------------------------------------
|   tcldom_domCmd
|
\---------------------------------------------------------------------------*/
int tcldom_domCmd (
    ClientData   clientData,
    Tcl_Interp * interp,
    int          objc,
    Tcl_Obj    * CONST objv[]
) 
{
    GetTcldomTSD()
    char        * method, tmp[300];
    int           methodIndex, result, i, bool;
    Tcl_CmdInfo   cmdInfo;
    Tcl_Obj     * mobjv[MAX_REWRITE_ARGS];
        
    static char *domMethods[] = {
        "createDocument",    "createNodeCmd",     "parse", 
        "setResultEncoding", "setStoreLineColumn",
        NULL
    };
    enum domMethod {
        m_createDocument,    m_createNodeCmd,     m_parse, 
        m_setResultEncoding, m_setStoreLineColumn
    };
   


    if (objc < 2) {
        SetResult ( dom_usage);
        return TCL_ERROR;
    }
    method = Tcl_GetStringFromObj (objv[1], NULL);    
    if (Tcl_GetIndexFromObj(interp, objv[1], domMethods, "method", 0,
                            &methodIndex) != TCL_OK) 
    {
        /*--------------------------------------------------------
        |   try to find method implemented as normal Tcl proc
        \-------------------------------------------------------*/
        if ((strlen (method)-1) >= 300) {
            SetResult ( "too long method name!");
            return TCL_ERROR;
        }
        sprintf (tmp, "::dom::DOMImplementation::%s",method);
        DBG(fprintf(stderr, "testing %s\n", tmp);)
        result = Tcl_GetCommandInfo (interp, tmp, &cmdInfo);
        if (!result) {
            SetResult ( dom_usage);
            return TCL_ERROR;
        }  
        if (!cmdInfo.isNativeObjectProc) {
            SetResult ( "can't access Tcl level method!");
            return TCL_ERROR;   
        }
        if (objc >= MAX_REWRITE_ARGS) {
            SetResult ( "too many args to call Tcl level method!");
            return TCL_ERROR;   
        }
        mobjv[0] = objv[1];
        mobjv[1] = objv[0];
        for (i=2; i<objc; i++) mobjv[i] = objv[i];
        return (cmdInfo.objProc (cmdInfo.objClientData, interp, objc, mobjv));
    }    
    CheckArgs (2,10,1,dom_usage);
    switch ((enum domMethod) methodIndex ) {
       
        case m_createDocument:
            return tcldom_createDocument (clientData, interp, --objc, objv+1 );

        case m_createNodeCmd:
            return nodecmd_createNodeCmd (clientData, interp, --objc, objv+1 );
            
        case m_parse:
            return tcldom_parse (clientData, interp, --objc, objv+1 );

        case m_setResultEncoding:
            return tcldom_setResultEncoding (clientData, interp, --objc, objv+1 );
            
        case m_setStoreLineColumn:
            SetIntResult (TSD(storeLineColumn));
            if (objc == 3) {
                Tcl_GetBooleanFromObj (interp, objv[2], &bool);
                TSD(storeLineColumn) = bool;
            }
            return TCL_OK;
    }
    SetResult ( dom_usage);
    return TCL_ERROR;
}


#ifndef TDOM_NO_UNKNOWN_CMD

/*----------------------------------------------------------------------------
|   tcldom_unknownCmd
|
\---------------------------------------------------------------------------*/
int tcldom_unknownCmd (
    ClientData   clientData,
    Tcl_Interp * interp,
    int          objc,
    Tcl_Obj    * CONST objv[]
) 
{
    int          len, i, rc, openedParen, count, args;
    char        *cmd, *dot, *paren, *arg[MAX_REWRITE_ARGS], *object, *method;
    Tcl_DString  callString;    
    Tcl_CmdInfo  cmdInfo;
    Tcl_Obj     *vector[2+MAX_REWRITE_ARGS];
    Tcl_Obj     **objvCall;   


    cmd = Tcl_GetStringFromObj (objv[1], &len);        

    DBG(fprintf(stderr, "tcldom_unknownCmd: cmd=-%s- \n", cmd));

    dot = strchr(cmd,'.');                
    if ((dot != NULL) && (dot != cmd)) {

        object = cmd;
        cmd    = dot+1;
        *dot   = '\0';
        dot    = strchr(cmd,'.'); 
        
        while (dot != NULL) {

            method = cmd;
            paren = strchr(cmd,'('); 
            args = 0;
            if (paren && (paren < dot)) {
                *paren = '\0';
                paren++;
                arg[args] = paren;
                openedParen = 1;
                while (*paren) {
                    if (*paren == '\\') {
                        (void) Tcl_Backslash(paren, &count);
                        paren += count;             
                    } else if (*paren == ')') {
                        openedParen--;
                        if (openedParen==0) {
                            *paren = '\0';
                            args++;
                            break;
                        }
                    } else if (*paren == '(') {
                        openedParen++;
                        paren++; 
                    } else if (*paren == ',') { 
                        *paren = '\0';
                        arg[++args] = paren+1;
                        if (args >= MAX_REWRITE_ARGS) {
                            SetResult ( "too many args");
                            return TCL_ERROR;
                        }
                        paren++; 
                    } else {
                        paren++; 
                    }
                }
                if (openedParen!=0) {
                    SetResult ( "mismatched (");
                    return TCL_ERROR;
                }
            }
            cmd    = dot+1;
            *dot   = '\0';

            DBG(fprintf (stderr, "method=-%s- \n", method);
                fprintf (stderr, "rest=-%s- \n", cmd);
                for(i=0; i<args; i++) {
                    fprintf(stderr, "args %d =-%s- \n", i, arg[i]);
                }
            )

            /*---------------------------------------------------------
            |   intermediate call
            \--------------------------------------------------------*/
            rc = Tcl_GetCommandInfo (interp, object, &cmdInfo);
            if (rc && cmdInfo.isNativeObjectProc) {
                vector[0] = Tcl_NewStringObj (object, -1);
                vector[1] = Tcl_NewStringObj (method, -1);
                for(i=0; i<args; i++) {
                    vector[2+i] = Tcl_NewStringObj (arg[i], -1);
                }
                rc = cmdInfo.objProc (cmdInfo.objClientData, interp, 2 + args, vector);
                if (rc != TCL_OK) {
                   return rc;
                }
                for(i=args+1; i >= 0; i--) {
                    Tcl_DecrRefCount (vector[i]);
                }
            } else {
                Tcl_DStringInit (&callString);
                Tcl_DStringAppendElement (&callString, object);
                Tcl_DStringAppendElement (&callString, method);
                for(i=0; i<args; i++) {
                    Tcl_DStringAppendElement (&callString, arg[i] );
                }
                rc = Tcl_Eval (interp, Tcl_DStringValue (&callString));
                Tcl_DStringFree (&callString);
                if (rc != TCL_OK) {
                   return rc;
                }
            }
            /* get the new object returned from above call */
            object = Tcl_GetStringResult (interp);
            dot = strchr(cmd,'.'); 
        }

        method = cmd;
            paren = strchr(cmd,'('); 
            args = 0;
            if (paren) {
                *paren = '\0';
                paren++;
                arg[args] = paren;
                openedParen = 1;
                while (*paren) {
                    if (*paren == '\\') {
                        (void) Tcl_Backslash(paren, &count);
                        paren += count;             
                    } else if (*paren == ')') {
                        openedParen--;
                        if (openedParen==0) {
                            *paren = '\0';
                            args++;
                            break;
                        }
                    } else if (*paren == '(') {
                        openedParen++;
                        paren++; 
                    } else if (*paren == ',') { 
                        *paren = '\0';
                        arg[++args] = paren+1;
                        if (args >= MAX_REWRITE_ARGS) {
                            SetResult ( "too many args");
                            return TCL_ERROR;
                        }
                        paren++; 
                    } else {
                        paren++; 
                    }
                }
                if (openedParen!=0) {
                    SetResult ( "mismatched (");
                    return TCL_ERROR;
                }
            }
            DBG(fprintf (stderr, "method=-%s- \n", method);
                fprintf (stderr, "rest=-%s- \n", cmd);
                for(i=0; i<args; i++) {
                    fprintf(stderr, "args %d =-%s- \n", i, arg[i]);
                }
            )

        /*----------------------------------------------------------------
        |   final call
        \---------------------------------------------------------------*/
        rc = Tcl_GetCommandInfo (interp, object, &cmdInfo);
        if (rc && cmdInfo.isNativeObjectProc) {

            objvCall = (Tcl_Obj**)Tcl_Alloc (sizeof (Tcl_Obj*) * (objc+args));
            
            objvCall[0] = Tcl_NewStringObj (object, -1);
            objvCall[1] = Tcl_NewStringObj (method, -1);
            for(i=0; i<args; i++) {
                objvCall[2+i] = Tcl_NewStringObj (arg[i], -1);
            }
            for (i=2; i<objc; i++) {
                objvCall[i+args] = objv[i];
            }
            rc = cmdInfo.objProc(cmdInfo.objClientData, interp, objc+args, objvCall);
            for(i=objc+args-1; i >= 0; i--) {
                Tcl_DecrRefCount (objvCall[i]);
            }
            Tcl_Free ( (void*)objvCall);
            
        } else {
            Tcl_DStringInit (&callString);
            Tcl_DStringAppendElement (&callString, object);
            Tcl_DStringAppendElement (&callString, method);
            for(i=2; i<objc; i++) {
                Tcl_DStringAppendElement (&callString, 
                                          Tcl_GetStringFromObj (objv[i], NULL));
            }
            rc = Tcl_Eval (interp, Tcl_DStringValue (&callString));
            Tcl_DStringFree (&callString);
        }
        return rc;
                    
    } else {
    
        /*----------------------------------------------------------------
        |   call the original unknown function 
        |
        \---------------------------------------------------------------*/
        Tcl_DStringInit (&callString);
        Tcl_DStringAppendElement (&callString, "unknown_tdom");
        for(i=1; i<objc; i++) {
            Tcl_DStringAppendElement (&callString, 
                                      Tcl_GetStringFromObj (objv[i], NULL));
        }
        rc = Tcl_Eval (interp, Tcl_DStringValue (&callString));
        Tcl_DStringFree (&callString);
        return rc;
    }
}

#endif

