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

#define SetResult(str) Tcl_ResetResult(interp); \
                     Tcl_SetStringObj(Tcl_GetObjResult(interp), (str), -1)

#define SetIntResult(i) Tcl_ResetResult(interp); \
                     Tcl_SetIntObj(Tcl_GetObjResult(interp), (i))
                     
#define SetDoubleResult(d) Tcl_ResetResult(interp); \
                     Tcl_SetDoubleObj(Tcl_GetObjResult(interp), (d))

#define SetBooleanResult(i) Tcl_ResetResult(interp); \
                     Tcl_SetBooleanObj(Tcl_GetObjResult(interp), (i))
 
#define AppendResult(str) {Tcl_Obj *o = Tcl_GetObjResult(interp); \
                     if (Tcl_IsShared(o)) { \
                          o = Tcl_DuplicateObj(o); \
                          Tcl_SetObjResult(interp, o); \
                     } \
                     Tcl_AppendToObj(o, (str), -1);}

#define CheckArgs(min,max,n,msg) \
                     if ((objc < min) || (objc >max)) { \
                         Tcl_WrongNumArgs(interp, n, objv, msg); \
                         return TCL_ERROR; \
                     }
#if TclOnly8Bits
#define writeChars(var,chan,buf,len)  (chan) ? \
                     ((void)Tcl_Write ((chan), (buf), (len) )) : \
                     (Tcl_AppendToObj ((var), (buf), (len) ));
#else
#define writeChars(var,chan,buf,len)  (chan) ? \
                     ((void)Tcl_WriteChars ((chan), (buf), (len) )) : \
                     (Tcl_AppendToObj ((var), (buf), (len) ));
#endif


/*----------------------------------------------------------------------------
|   Module Globals
|
\---------------------------------------------------------------------------*/
#ifndef TCL_THREADS
    static TEncoding *Encoding_to_8bit      = NULL;
    static int        storeLineColumn       = 0;
    static int        dontCreateObjCommands = 0;
#   define TSD(x)     x
#   define GetTcldomTSD()
#else
    typedef struct ThreadSpecificData {
        TEncoding *Encoding_to_8bit;
        int        storeLineColumn;
        int        dontCreateObjCommands;
    } ThreadSpecificData;
    static Tcl_ThreadDataKey dataKey;
    static Tcl_HashTable     sharedDocs;
    static Tcl_Mutex         tableMutex;
    static int               tcldomInitialized;
#   define TSD(x)            tsdPtr->x
#   define GetTcldomTSD()  ThreadSpecificData *tsdPtr = \
                                (ThreadSpecificData*)   \
                                Tcl_GetThreadData(      \
                                    &dataKey,           \
                                    sizeof(ThreadSpecificData));
#endif /* TCL_THREADS */

static char dom_usage[] =
                "Usage dom <subCommand> <args>, where subCommand can be:    \n"
                "          parse ?-keepEmpties? ?-channel <channel> ?-baseurl <baseurl>?  \n"
                "                ?-feedbackAfter <#Bytes>? ?-externalentitycommand <cmd>? \n"
                "                ?-simple? ?-html? ?<xml>? ?<objVar>? \n"
                "          createDocument docElemName ?objVar?              \n"
                TDomThreaded(
                "          attachDocument docObjCommand ?objVar?            \n"
                )
                "          createNodeCmd ?-returnNodeCmd? (element|comment|text|cdata|pi)Node cmdName \n"
                "          setResultEncoding ?encodingName?                 \n"
                "          setStoreLineColumn ?boolean?                     \n"
                "          isCharData string                                \n"
                "          isName string                                    \n"
                "          isQName string                                   \n"
                "          isNCName string                                  \n"
                ;

static char domObj_usage[] =
                "Usage docObj <method> <args>, where method can be:\n"
                "          documentElement ?objVar?                \n"
                "          getElementsByTagName name               \n"
                "          getElementsByTagNameNS uri localname    \n"
                "          createElement tagName ?objVar?          \n"
                "          createElementNS uri tagName ?objVar?    \n"
                "          createCDATASection data ?objVar?        \n"
                "          createTextNode text ?objVar?            \n"
                "          createComment text ?objVar?             \n"
                "          createProcessingInstruction target data ?objVar? \n"
                "          asXML ?-indent <none,0..8>? ?-channel <channelId>? ?-escapeNonASCII?\n" 
                "          asHTML ?-channel <channelId>? ?-escapeNonASCII? ?-htmlEntities?\n"
                "          getDefaultOutputMethod                  \n"
                "          delete                                  \n"
                "          xslt ?-parameters parameterList? <xsltDocNode>\n"
                TDomThreaded(
                "          readlock                                \n"
                "          writelock                               \n"
                )
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
                "    getElementsByTagNameNS uri localname \n"
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
                "    asXML ?-indent <none,0..8>? ?-channel <channelId>? ?-escapeNonASCII? \n"
                "    asHTML ?-channel <channelId>? ?-escapeNonASCII? ?-htmlEntities?\n"
                "    appendFromList nestedList   \n"
                "    appendFromScript script     \n"
                "    appendXML xmlString         \n"
                "    selectNodes xpathQuery ?typeVar? \n"
                "    toXPath                     \n"
                "    disableOutputEscaping ?boolean? \n"
                "    xslt ?-parameters parameterList? <xsltDocNode>\n"
                TDomThreaded(
                "    readlock                    \n"
                "    writelock                   \n"
                )
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
static int tcldom_DocObjCmd (ClientData clientData,
                             Tcl_Interp *interp,
                             int         objc,
                             Tcl_Obj    *objv[]);

static void tcldom_docCmdDeleteProc (ClientData  clientData);

static char * tcldom_docTrace (ClientData    clientData,
                               Tcl_Interp   *interp,
                               CONST84 char *name1,
                               CONST84 char *name2,
                               int           flags);

#ifdef TCL_THREADS

static int tcldom_EvalLocked (Tcl_Interp*  interp,
                              Tcl_Obj**    objv,
                              domDocument* doc,
                              int          flag);


/*----------------------------------------------------------------------------
|   tcldom_finalize
|   Activated in application exit handler to delete shared document table
|   Table entries are deleted by the object command deletion callbacks,
|   so at this time, table should be empty. If not, we will leave some
|   memory leaks. This is not fatal, though: we're exiting the app anyway. 
\---------------------------------------------------------------------------*/
static void 
tcldom_finalize(
    ClientData unused
)
{
    Tcl_MutexLock(&tableMutex);
    Tcl_DeleteHashTable(&sharedDocs);
    Tcl_MutexUnlock(&tableMutex);
}

/*----------------------------------------------------------------------------
|   tcldom_initialize
|   Activated at module load to initialize shared document table
\---------------------------------------------------------------------------*/

void tcldom_initialize()
{
    if (!tcldomInitialized) {
        Tcl_MutexLock(&tableMutex);
        if (!tcldomInitialized) {
            Tcl_InitHashTable(&sharedDocs, TCL_STRING_KEYS);
            Tcl_CreateExitHandler(tcldom_finalize, NULL);
            tcldomInitialized = 1;
        }
        Tcl_MutexUnlock(&tableMutex);
    }
}
#endif /* TCL_THREADS */


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
        NODE_CMD(objCmdName, node);
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

    DBG(fprintf (stderr, "--> tcldom_docCmdDeleteProc doc 0x%x !\n", dinfo->document);)
    if (dinfo->traceVarName) {
        DBG(fprintf (stderr, "--> tcldom_docCmdDeleteProc calling Tcl_UntraceVar ...\n");)
        Tcl_UntraceVar (dinfo->interp, dinfo->traceVarName,
                                TCL_TRACE_WRITES |  TCL_TRACE_UNSETS,
                                tcldom_docTrace, clientData);
        FREE(dinfo->traceVarName);
        dinfo->traceVarName = NULL;
    }

    TDomThreaded(
    {
        Tcl_HashEntry *entryPtr;
        char objCmdName[40];

        Tcl_MutexLock(&tableMutex);
        if(--dinfo->document->refCount > 0) {
            Tcl_MutexUnlock(&tableMutex);
            return; /* While doc has still users attached */
        }
        DOC_CMD(objCmdName, dinfo->document);
        entryPtr = Tcl_FindHashEntry(&sharedDocs, objCmdName);
        if (entryPtr) {
            Tcl_DeleteHashEntry(entryPtr);
            DBG(fprintf(stderr, "--> document %s deleted from the shared table\n",
                        objCmdName);)
        }
        Tcl_MutexUnlock(&tableMutex);
    }
    )

    /* delete DOM tree */
    domFreeDocument (dinfo->document, tcldom_docDeleteNode, dinfo->interp );
    FREE((void*)dinfo);
}


/*----------------------------------------------------------------------------
|   tcldom_docTrace
|
\---------------------------------------------------------------------------*/
static
char * tcldom_docTrace (
    ClientData    clientData,
    Tcl_Interp   *interp,
    CONST84 char *name1,
    CONST84 char *name2,
    int           flags
)
{
    TcldomDocDeleteInfo * dinfo;
    char                  objCmdName[40];


    dinfo = (TcldomDocDeleteInfo*) clientData;

    DBG(fprintf (stderr, "--> tcldom_trace %x doc%x !\n", flags, clientData );)
    if (flags & TCL_TRACE_WRITES) {
        return "var is read-only";
    }
    if (flags & TCL_TRACE_UNSETS) {
        DOC_CMD(objCmdName, dinfo->document);
        DBG(fprintf (stderr, "--> tcldom_trace delete %s (addr 0x%x)!\n",
                     objCmdName, dinfo->document);)

       /* delete document by deleting Tcl object command */

       /*
        Tcl_UntraceVar (interp, name1, TCL_TRACE_WRITES |
                                       TCL_TRACE_UNSETS,
                                       tcldom_docTrace, clientData);
       */
        DBG(fprintf (stderr, "--> tcldom_trace calling Tcl_DeleteCommand\n");)
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
    ClientData    clientData,
    Tcl_Interp   *interp,
    CONST84 char *name1,
    CONST84 char *name2,
    int           flags
)
{
    char     objCmdName[40];
    domNode *node;

    DBG(fprintf (stderr, "--> tcldom_nodeTrace %d 0x%x !\n", flags, (int)clientData );)

    node = (domNode*) clientData;

    if (flags & TCL_TRACE_UNSETS) {
        NODE_CMD(objCmdName, node);
        DBG(fprintf (stderr, "--> tcldom_nodeTrace delete domNode %s !\n", objCmdName);)
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
        NODE_CMD(objCmdName, node);
        DBG(fprintf(stderr,"--> creating node %s\n", objCmdName);)
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
    DOC_CMD(objCmdName, doc);
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
                                          (Tcl_VarTraceProc*)tcldom_nodeTrace,
                                          (ClientData) node);
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

        dinfo = (TcldomDocDeleteInfo*)MALLOC(sizeof(TcldomDocDeleteInfo));
        dinfo->interp       = interp;
        dinfo->document     = document;
        dinfo->traceVarName = NULL;

        Tcl_CreateObjCommand (interp, objCmdName,
                              (Tcl_ObjCmdProc *)  tcldom_DocObjCmd,
                              (ClientData)        dinfo,
                              (Tcl_CmdDeleteProc*)tcldom_docCmdDeleteProc);
        TDomThreaded(
            {
                Tcl_HashEntry *entryPtr;
                int newEntry;

                Tcl_MutexLock(&tableMutex);
                ++document->refCount;
                entryPtr = Tcl_CreateHashEntry(&sharedDocs, objCmdName, &newEntry);
                if (newEntry) {
                    Tcl_SetHashValue(entryPtr, (ClientData)dinfo->document);
                }
                Tcl_MutexUnlock(&tableMutex);
                DBG(fprintf(stderr, "--> document 0x%x %s shared table\n",
                            dinfo->document,
                            (newEntry) ? "entered into" : "already in");)
            }
        )

    } else {
        /* reuse old informaion */
        dinfo = (TcldomDocDeleteInfo*)cmd_info.objClientData;
    }
    if (setVariable) {
        objVar = Tcl_GetStringFromObj (var_name, NULL);
        dinfo->traceVarName = tdomstrdup(objVar);
        Tcl_UnsetVar (interp, objVar, 0);
        Tcl_SetVar   (interp, objVar, objCmdName, 0);
        Tcl_TraceVar (interp, objVar, TCL_TRACE_WRITES |
                                      TCL_TRACE_UNSETS,
                                      (Tcl_VarTraceProc*)tcldom_docTrace,
                                      (ClientData) dinfo);
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
    int         nsIndex,
    char       *uri
)
{
    int      result;
    domNode *child;

    /* nsIndex == -1 ==> DOM 1 no NS i.e getElementsByTagName
       nsIndex != -1 are the NS aware cases
       nsIndex == -2 ==> more than one namespace in the document with the 
                         requested namespace, we have to strcmp the URI
                         with the namespace uri of every node
       nsIndex == -3 ==> NS wildcard '*'
       nsIndex == -4 ==> special handled case uri == "", i.e. all
                         nodes not in a namespace */

    while (node) {
        if (node->nodeType != ELEMENT_NODE) {
            node = node->nextSibling;
            continue;
        }
        if ( (nsIndex == -1)
             || (nsIndex == node->namespace)
             || (nsIndex == -3)
             || (nsIndex == -2 
                 && node->namespace 
                 && strcmp (uri, domNamespaceURI (node))==0)
             || (nsIndex == -4
                 && (!node->namespace 
                     || strcmp ("", domNamespaceURI (node))==0)) )
        {
            char prefix[MAX_PREFIX_LEN], *localName;
            if (nsIndex == -1) {
                localName = node->nodeName;
            } else {
                domSplitQName (node->nodeName, prefix, &localName);
            }
            if (Tcl_StringMatch (localName, namePattern)) {
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
            }
        }

        /* recurs to the child nodes */
        child = node->firstChild;
        result = tcldom_getElementsByTagName (interp, namePattern, child,
                                                  nsIndex, uri);
        if (result != TCL_OK) {
            return result;
        }
        node = node->nextSibling;
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

    TDomThreaded (
        {
            Tcl_HashEntry *entryPtr;
            domDocument *tabDoc;

            Tcl_MutexLock(&tableMutex);
            entryPtr = Tcl_FindHashEntry(&sharedDocs, docName);
            if (entryPtr == NULL) {
                Tcl_MutexUnlock(&tableMutex);
                *errMsg = "not a shared document object!";
                return NULL;
            }
            tabDoc = (domDocument*)Tcl_GetHashValue(entryPtr);
            Tcl_MutexUnlock(&tableMutex);
            if (doc != tabDoc) {
                panic("document mismatch; doc=%x, in table=%x\n", doc, tabDoc);
            }
        }
    )

    return doc;
}


/*----------------------------------------------------------------------------
|   tcldom_appendXML
|
\---------------------------------------------------------------------------*/
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


    xml_string = Tcl_GetStringFromObj( obj, &xml_string_len);

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
    FREE((void*)doc);

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
    domNodeType  startType;
    int          mixedNodeSet;

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
             
        case NaNResult:
             Tcl_SetStringObj (type, "number", -1);
             Tcl_SetStringObj (value, "NaN", -1);
             break;

        case InfResult:
             Tcl_SetStringObj (type, "number", -1);
             Tcl_SetStringObj (value, "Infinity", -1);
             break;

        case NInfResult:
             Tcl_SetStringObj (type, "number", -1);
             Tcl_SetStringObj (value, "-Infinity", -1);
             break;
             
        case StringResult:
             Tcl_SetStringObj (type, "string", -1);
             Tcl_SetStringObj (value, rs->string, rs->string_len);
             break;

        case xNodeSetResult:
             startType = rs->nodes[0]->nodeType;
             mixedNodeSet = 0;
             for (i=0; i<rs->nr_nodes; i++) {
                 if (rs->nodes[i]->nodeType != startType) mixedNodeSet = 1;

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
             if (mixedNodeSet) {
                 Tcl_SetStringObj (type, "mixed", 5);
             } else {
                 if (startType == ATTRIBUTE_NODE)
                     Tcl_SetStringObj (type, "attrnodes",-1);
                 else
                     Tcl_SetStringObj (type, "nodes", 5);
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
    domNode         *exprContext,
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
    int          objc, rc, i, errStrLen, listLen, intValue, res;
    double       doubleValue;
    domNode     *node;

    DBG(fprintf(stderr,
                "tcldom_xpathFuncCallBack functionName=%s position=%d argc=%d\n",
                functionName, position, argc);)

    sprintf (tclxpathFuncName, "::dom::xpathFunc::%s", functionName);
    DBG(fprintf(stderr, "testing %s\n", tclxpathFuncName);)
    rc = Tcl_GetCommandInfo (interp, tclxpathFuncName, &cmdInfo);
    if (!rc) {
        *errMsg = (char*)MALLOC (80 + strlen (functionName));
        strcpy (*errMsg, "Unknown XPath function: \"");
        strcat (*errMsg, functionName);
        strcat (*errMsg, "\"!");
        return XPATH_EVAL_ERR;
    }
    if (!cmdInfo.isNativeObjectProc) {
        *errMsg = (char*)tdomstrdup("can't access Tcl level method!");
        return XPATH_EVAL_ERR;
    }
    if ( (5+(2*argc)) >= MAX_REWRITE_ARGS) {
        *errMsg = (char*)tdomstrdup("too many args to call Tcl level method!");
        return XPATH_EVAL_ERR;
    }
    objc = 0;
    objv[objc] = Tcl_NewStringObj(tclxpathFuncName, -1);
    Tcl_IncrRefCount(objv[objc++]);
    tcldom_createNodeObj (interp, ctxNode, objCmdName);
    objv[objc] = Tcl_NewStringObj (objCmdName, -1);
    Tcl_IncrRefCount(objv[objc++]);

    objv[objc] = Tcl_NewIntObj (position);
    Tcl_IncrRefCount(objv[objc++]);

    type  = Tcl_NewObj();
    value = Tcl_NewObj();
    tcldom_xpathResultSet (interp, nodeList, type, value);
    objv[objc] = type;
    Tcl_IncrRefCount(objv[objc++]);
    objv[objc] = value;
    Tcl_IncrRefCount(objv[objc++]);

    for (i=0; i<argc; i++) {
        type  = Tcl_NewObj();
        value = Tcl_NewObj();
        tcldom_xpathResultSet (interp, args[i], type, value);
        objv[objc] = type;
        Tcl_IncrRefCount(objv[objc++]);
        objv[objc] = value;
        Tcl_IncrRefCount(objv[objc++]);
    }
    rc = (cmdInfo.objProc (cmdInfo.objClientData, interp, objc, objv));
    if (rc == TCL_OK) {
        xpathRSInit (result);
        resultPtr = Tcl_GetObjResult(interp);
        rc = Tcl_ListObjLength (interp, resultPtr, &listLen);
        if (rc == TCL_OK) {
            if (listLen == 1) {
                rsSetString (result, Tcl_GetStringFromObj(resultPtr, NULL) );
                res = XPATH_OK;
                goto funcCallCleanup;
            }
            if (listLen != 2) {
                *errMsg = (char*)tdomstrdup("wrong return tuple! Must be {type value} !");
                res = XPATH_EVAL_ERR;
                goto funcCallCleanup;
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
                    *errMsg = tdomstrdup("value not a node list!");
                    res = XPATH_EVAL_ERR;
                    goto funcCallCleanup;
                }
                for (i=0; i < listLen; i++) {
                    rc = Tcl_ListObjIndex (interp, value, i, &nodeObj);
                    nodeName = Tcl_GetStringFromObj (nodeObj, NULL);
                    node = tcldom_getNodeFromName (interp, nodeName, &errStr);
                    if (node == NULL) {
                        *errMsg = tdomstrdup(errStr);
                        res = XPATH_EVAL_ERR;
                        goto funcCallCleanup;
                    }
                    rsAddNode (result, node);
                }
                sortByDocOrder (result);
            } else
            if (strcmp(typeStr, "attrnodes")==0) {
                *errMsg = tdomstrdup("attrnodes not implemented yet!");
                res = XPATH_EVAL_ERR;
                goto funcCallCleanup;
            } else
            if (strcmp(typeStr, "attrvalues")==0) {
                rsSetString(result, Tcl_GetStringFromObj(value,NULL) );
            } else {
                *errMsg = (char*)MALLOC (80 + strlen (typeStr)
                                         + strlen (functionName));
                strcpy (*errMsg, "Unknown type of return value \"");
                strcat (*errMsg, typeStr);
                strcat (*errMsg, "\" from tcl coded XPath function \"");
                strcat (*errMsg, functionName);
                strcat (*errMsg, "\"!");
                res = XPATH_EVAL_ERR;
                goto funcCallCleanup;
            }
        } else {
            DBG(fprintf(stderr, "ListObjLength != TCL_OK --> returning XPATH_EVAL_ERR \n");)
            res = XPATH_EVAL_ERR;
            goto funcCallCleanup;
        }
        Tcl_ResetResult (interp);
        res = XPATH_OK;
    } else {
        errStr = Tcl_GetStringFromObj( Tcl_GetObjResult(interp), &errStrLen);
        *errMsg = (char*)MALLOC(120+strlen(functionName) + errStrLen);
        strcpy(*errMsg, "Tcl error while executing XPATH extension function '");
        strcat(*errMsg, functionName );
        strcat(*errMsg, "':\n" );
        strcat(*errMsg, errStr);
        Tcl_ResetResult (interp);
        DBG( fprintf(stderr, "returning XPATH_EVAL_ERR \n"); )
        res = XPATH_EVAL_ERR;
    }
 funcCallCleanup:
    for (i = 0; i < objc; i++) {
        Tcl_DecrRefCount(objv[i]);
    }
    return res;
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

    if (node->ownerDocument->nodeFlags & NEEDS_RENUMBERING) {
        domRenumberTree (node->ownerDocument->rootNode);
        node->ownerDocument->nodeFlags &= ~NEEDS_RENUMBERING;
    }
    rc = xpathEval (node, node, xpathQuery, &cbs, &errMsg, &rs);

    if (rc != XPATH_OK) {
        xpathRSFree( &rs );
        SetResult ( errMsg);
        DBG( fprintf(stderr, "errMsg = %s \n", errMsg); )
        if (errMsg) FREE(errMsg);
        return TCL_ERROR;
    }
    if (errMsg) FREE(errMsg);
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
    if (length != 3) {
        SetResult ("invalid element node list format!");
        return TCL_ERROR;
    }
    newnode = domNewElementNode(node->ownerDocument, tag_name, ELEMENT_NODE);
    domAppendChild (node, newnode);

    /*-------------------------------------------------------------------------
    |   create atributes
    \------------------------------------------------------------------------*/
    if ((rc = Tcl_ListObjIndex (interp, lnode, 1, &attrListObj)) != TCL_OK) {
        return rc;
    }
    if ((rc = Tcl_ListObjLength(interp, attrListObj, &attrLength)) != TCL_OK) {
        return rc;
    }
    if (attrLength % 2) {
        SetResult ("invalid attributes list format!");
        return TCL_ERROR;
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

    /*-------------------------------------------------------------------------
    |   add child nodes
    \------------------------------------------------------------------------*/
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
    int         value_length,
    int         forAttr,
    int         escapeNonASCII,
    int         htmlEntities
)
{
#define APESC_BUF_SIZE 512
#define AP(c)  *b++ = c;
#define AE(s)  pc1 = s; while(*pc1) *b++ = *pc1++;
    char  buf[APESC_BUF_SIZE+80], *b, *bLimit,  *pc, *pc1, *pEnd, charRef[10];
    int   charDone;
#if !TclOnly8Bits
    int i, clen = 0;
    Tcl_UniChar uniChar;
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
        } else
        if (forAttr && (*pc == '\n')) { AP('&') AP('#') AP('x') AP('A') AP(';')
        } else
        {
            charDone = 0;
            if (htmlEntities) {
                charDone = 1;
#if TclOnly8Bits
                switch ((unsigned int)*pc)
#else           
                Tcl_UtfToUniChar (pc, &uniChar);
                switch (uniChar) 
#endif
                {
                case 0240: AE("&nbsp;"); break;     
                case 0241: AE("&iexcl;"); break;    
                case 0242: AE("&cent;"); break;     
                case 0243: AE("&pound;"); break;    
                case 0244: AE("&curren;"); break;   
                case 0245: AE("&yen;"); break;      
                case 0246: AE("&brvbar;"); break;   
                case 0247: AE("&sect;"); break;     
                case 0250: AE("&uml;"); break;      
                case 0251: AE("&copy;"); break;     
                case 0252: AE("&ordf;"); break;     
                case 0253: AE("&laquo;"); break;    
                case 0254: AE("&not;"); break;      
                case 0255: AE("&shy;"); break;      
                case 0256: AE("&reg;"); break;      
                case 0257: AE("&macr;"); break;     
                case 0260: AE("&deg;"); break;      
                case 0261: AE("&plusmn;"); break;   
                case 0262: AE("&sup2;"); break;     
                case 0263: AE("&sup3;"); break;     
                case 0264: AE("&acute;"); break;    
                case 0265: AE("&micro;"); break;    
                case 0266: AE("&para;"); break;     
                case 0267: AE("&middot;"); break;   
                case 0270: AE("&cedil;"); break;    
                case 0271: AE("&sup1;"); break;     
                case 0272: AE("&ordm;"); break;     
                case 0273: AE("&raquo;"); break;    
                case 0274: AE("&frac14;"); break;   
                case 0275: AE("&frac12;"); break;   
                case 0276: AE("&frac34;"); break;   
                case 0277: AE("&iquest;"); break;   
                case 0300: AE("&Agrave;"); break;   
                case 0301: AE("&Aacute;"); break;   
                case 0302: AE("&Acirc;"); break;    
                case 0303: AE("&Atilde;"); break;   
                case 0304: AE("&Auml;"); break;     
                case 0305: AE("&Aring;"); break;    
                case 0306: AE("&AElig;"); break;    
                case 0307: AE("&Ccedil;"); break;   
                case 0310: AE("&Egrave;"); break;   
                case 0311: AE("&Eacute;"); break;   
                case 0312: AE("&Ecirc;"); break;    
                case 0313: AE("&Euml;"); break;     
                case 0314: AE("&Igrave;"); break;   
                case 0315: AE("&Iacute;"); break;   
                case 0316: AE("&Icirc;"); break;    
                case 0317: AE("&Iuml;"); break;     
                case 0320: AE("&ETH;"); break;      
                case 0321: AE("&Ntilde;"); break;   
                case 0322: AE("&Ograve;"); break;   
                case 0323: AE("&Oacute;"); break;   
                case 0324: AE("&Ocirc;"); break;    
                case 0325: AE("&Otilde;"); break;   
                case 0326: AE("&Ouml;"); break;     
                case 0327: AE("&times;"); break;    
                case 0330: AE("&Oslash;"); break;   
                case 0331: AE("&Ugrave;"); break;   
                case 0332: AE("&Uacute;"); break;   
                case 0333: AE("&Ucirc;"); break;    
                case 0334: AE("&Uuml;"); break;     
                case 0335: AE("&Yacute;"); break;   
                case 0336: AE("&THORN;"); break;    
                case 0337: AE("&szlig;"); break;    
                case 0340: AE("&agrave;"); break;   
                case 0341: AE("&aacute;"); break;   
                case 0342: AE("&acirc;"); break;    
                case 0343: AE("&atilde;"); break;   
                case 0344: AE("&auml;"); break;     
                case 0345: AE("&aring;"); break;    
                case 0346: AE("&aelig;"); break;    
                case 0347: AE("&ccedil;"); break;   
                case 0350: AE("&egrave;"); break;   
                case 0351: AE("&eacute;"); break;   
                case 0352: AE("&ecirc;"); break;    
                case 0353: AE("&euml;"); break;     
                case 0354: AE("&igrave;"); break;   
                case 0355: AE("&iacute;"); break;   
                case 0356: AE("&icirc;"); break;    
                case 0357: AE("&iuml;"); break;     
                case 0360: AE("&eth;"); break;      
                case 0361: AE("&ntilde;"); break;   
                case 0362: AE("&ograve;"); break;   
                case 0363: AE("&oacute;"); break;   
                case 0364: AE("&ocirc;"); break;    
                case 0365: AE("&otilde;"); break;   
                case 0366: AE("&ouml;"); break;     
                case 0367: AE("&divide;"); break;   
                case 0370: AE("&oslash;"); break;   
                case 0371: AE("&ugrave;"); break;   
                case 0372: AE("&uacute;"); break;   
                case 0373: AE("&ucirc;"); break;    
                case 0374: AE("&uuml;"); break;     
                case 0375: AE("&yacute;"); break;   
                case 0376: AE("&thorn;"); break;    
                case 0377: AE("&yuml;"); break;     
#if !TclOnly8Bits
                /* "Special" chars, according to XHTML xhtml-special.ent */
                case 338: AE("&OElig;"); break;
                case 339: AE("&oelig;"); break;
                case 352: AE("&Scaron;"); break;
                case 353: AE("&scaron;"); break;
                case 376: AE("&Yuml;"); break;
                case 710: AE("&circ;"); break;
                case 732: AE("&tilde;"); break;
                case 8194: AE("&ensp;"); break;
                case 8195: AE("&emsp;"); break;
                case 8201: AE("&thinsp;"); break;
                case 8204: AE("&zwnj;"); break;
                case 8205: AE("&zwj;"); break;
                case 8206: AE("&lrm;"); break;
                case 8207: AE("&rlm;"); break;
                case 8211: AE("&ndash;"); break;
                case 8212: AE("&mdash;"); break;
                case 8216: AE("&lsquo;"); break;
                case 8217: AE("&rsquo;"); break;
                case 8218: AE("&sbquo;"); break;
                case 8220: AE("&ldquo;"); break;
                case 8221: AE("&rdquo;"); break;
                case 8222: AE("&bdquo;"); break;
                case 8224: AE("&dagger;"); break;
                case 8225: AE("&Dagger;"); break;
                case 8240: AE("&permil;"); break;
                case 8249: AE("&lsaquo;"); break;
                case 8250: AE("&rsaquo;"); break;
                case 8364: AE("&euro;"); break;
                /* "Symbol" chars, according to XHTML xhtml-symbol.ent */
                case 402: AE("&fnof;"); break;     
                case 913: AE("&Alpha;"); break;    
                case 914: AE("&Beta;"); break;     
                case 915: AE("&Gamma;"); break;    
                case 916: AE("&Delta;"); break;    
                case 917: AE("&Epsilon;"); break;  
                case 918: AE("&Zeta;"); break;     
                case 919: AE("&Eta;"); break;      
                case 920: AE("&Theta;"); break;    
                case 921: AE("&Iota;"); break;     
                case 922: AE("&Kappa;"); break;    
                case 923: AE("&Lambda;"); break;   
                case 924: AE("&Mu;"); break;       
                case 925: AE("&Nu;"); break;       
                case 926: AE("&Xi;"); break;       
                case 927: AE("&Omicron;"); break;  
                case 928: AE("&Pi;"); break;       
                case 929: AE("&Rho;"); break;      
                case 931: AE("&Sigma;"); break;    
                case 932: AE("&Tau;"); break;      
                case 933: AE("&Upsilon;"); break;  
                case 934: AE("&Phi;"); break;      
                case 935: AE("&Chi;"); break;      
                case 936: AE("&Psi;"); break;      
                case 937: AE("&Omega;"); break;    
                case 945: AE("&alpha;"); break;    
                case 946: AE("&beta;"); break;     
                case 947: AE("&gamma;"); break;    
                case 948: AE("&delta;"); break;    
                case 949: AE("&epsilon;"); break;  
                case 950: AE("&zeta;"); break;     
                case 951: AE("&eta;"); break;      
                case 952: AE("&theta;"); break;    
                case 953: AE("&iota;"); break;     
                case 954: AE("&kappa;"); break;    
                case 955: AE("&lambda;"); break;   
                case 956: AE("&mu;"); break;       
                case 957: AE("&nu;"); break;       
                case 958: AE("&xi;"); break;       
                case 959: AE("&omicron;"); break;  
                case 960: AE("&pi;"); break;       
                case 961: AE("&rho;"); break;      
                case 962: AE("&sigmaf;"); break;   
                case 963: AE("&sigma;"); break;    
                case 964: AE("&tau;"); break;      
                case 965: AE("&upsilon;"); break;  
                case 966: AE("&phi;"); break;      
                case 967: AE("&chi;"); break;      
                case 968: AE("&psi;"); break;      
                case 969: AE("&omega;"); break;    
                case 977: AE("&thetasym;"); break; 
                case 978: AE("&upsih;"); break;    
                case 982: AE("&piv;"); break;      
                case 8226: AE("&bull;"); break;     
                case 8230: AE("&hellip;"); break;   
                case 8242: AE("&prime;"); break;    
                case 8243: AE("&Prime;"); break;    
                case 8254: AE("&oline;"); break;    
                case 8260: AE("&frasl;"); break;    
                case 8472: AE("&weierp;"); break;   
                case 8465: AE("&image;"); break;    
                case 8476: AE("&real;"); break;     
                case 8482: AE("&trade;"); break;    
                case 8501: AE("&alefsym;"); break;  
                case 8592: AE("&larr;"); break;     
                case 8593: AE("&uarr;"); break;     
                case 8594: AE("&rarr;"); break;     
                case 8595: AE("&darr;"); break;     
                case 8596: AE("&harr;"); break;     
                case 8629: AE("&crarr;"); break;    
                case 8656: AE("&lArr;"); break;     
                case 8657: AE("&uArr;"); break;     
                case 8658: AE("&rArr;"); break;     
                case 8659: AE("&dArr;"); break;     
                case 8660: AE("&hArr;"); break;     
                case 8704: AE("&forall;"); break;   
                case 8706: AE("&part;"); break;     
                case 8707: AE("&exist;"); break;    
                case 8709: AE("&empty;"); break;    
                case 8711: AE("&nabla;"); break;    
                case 8712: AE("&isin;"); break;     
                case 8713: AE("&notin;"); break;    
                case 8715: AE("&ni;"); break;       
                case 8719: AE("&prod;"); break;     
                case 8721: AE("&sum;"); break;      
                case 8722: AE("&minus;"); break;    
                case 8727: AE("&lowast;"); break;   
                case 8730: AE("&radic;"); break;    
                case 8733: AE("&prop;"); break;     
                case 8734: AE("&infin;"); break;    
                case 8736: AE("&ang;"); break;      
                case 8743: AE("&and;"); break;      
                case 8744: AE("&or;"); break;       
                case 8745: AE("&cap;"); break;      
                case 8746: AE("&cup;"); break;      
                case 8747: AE("&int;"); break;      
                case 8756: AE("&there4;"); break;   
                case 8764: AE("&sim;"); break;      
                case 8773: AE("&cong;"); break;     
                case 8776: AE("&asymp;"); break;    
                case 8800: AE("&ne;"); break;       
                case 8801: AE("&equiv;"); break;    
                case 8804: AE("&le;"); break;       
                case 8805: AE("&ge;"); break;       
                case 8834: AE("&sub;"); break;      
                case 8835: AE("&sup;"); break;      
                case 8836: AE("&nsub;"); break;     
                case 8838: AE("&sube;"); break;     
                case 8839: AE("&supe;"); break;     
                case 8853: AE("&oplus;"); break;    
                case 8855: AE("&otimes;"); break;   
                case 8869: AE("&perp;"); break;     
                case 8901: AE("&sdot;"); break;     
                case 8968: AE("&lceil;"); break;    
                case 8969: AE("&rceil;"); break;    
                case 8970: AE("&lfloor;"); break;   
                case 8971: AE("&rfloor;"); break;   
                case 9001: AE("&lang;"); break;     
                case 9002: AE("&rang;"); break;     
                case 9674: AE("&loz;"); break;      
                case 9824: AE("&spades;"); break;   
                case 9827: AE("&clubs;"); break;    
                case 9829: AE("&hearts;"); break;   
                case 9830: AE("&diams;"); break;    
#endif                    
                default: charDone = 0; 
                }
#if !TclOnly8Bits
                if (charDone) {
                    clen = UTF8_CHAR_LEN(*pc);
                    pc += (clen - 1);
                }
#endif
            }
#if TclOnly8Bits
            if (!charDone) {
                if (escapeNonASCII && ((unsigned char)*pc > 127)) {
                    sprintf (charRef, "%d", uniChar);
                    for (i = 0; i < 2; i++) {
                        AP(charRef[i]);
                    }
                } else {
                    AP(*pc);
                }
            }
#else
            if (!charDone) {
                if ((unsigned char)*pc > 127) {
                    clen = UTF8_CHAR_LEN(*pc);
                    if (!clen) {
                        fprintf (stderr, "can only handle UTF-8 chars up to 3 bytes long.");
                        exit(1);
                    }
                    if (escapeNonASCII) {
                        Tcl_UtfToUniChar (pc, &uniChar);
                        AP('&') AP('#')
                            sprintf(charRef, "%d", uniChar);
                        for (i = 0; i < strlen(charRef); i++) {
                            AP(charRef[i]);
                        }
                        AP(';')
                        pc += (clen - 1);
                    } else {
                        for (i = 0; i < clen; i++) {
                            AP(*pc);
                            pc++;
                        }
                        pc--;
                    }
                } else {
                    AP(*pc);
                }
            }
#endif
        }
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
    Tcl_Channel  chan,
    int          escapeNonASCII,
    int          htmlEntities
)
{
    int          empty;
    domNode     *child;
    domAttrNode *attrs;
    char         tag[80], attrName[80];

    if (node->nodeType == PROCESSING_INSTRUCTION_NODE) return;

    if (node->nodeType == TEXT_NODE) {
        if (node->nodeFlags & DISABLE_OUTPUT_ESCAPING) {
            writeChars (htmlString, chan, ((domTextNode*)node)->nodeValue,
                        ((domTextNode*)node)->valueLength);
        } else {
            tcldom_AppendEscaped (htmlString, chan,
                                  ((domTextNode*)node)->nodeValue,
                                  ((domTextNode*)node)->valueLength, 0,
                                  escapeNonASCII, htmlEntities);
        }
        return;
    }

    if (node->nodeType == CDATA_SECTION_NODE) {
            tcldom_AppendEscaped (htmlString, chan,
                                  ((domTextNode*)node)->nodeValue,
                                  ((domTextNode*)node)->valueLength, 0,
                                  escapeNonASCII, htmlEntities);
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
        tcldom_AppendEscaped (htmlString, chan, attrs->nodeValue, -1, 1,
                              escapeNonASCII, htmlEntities);
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
            tcldom_treeAsHTML (htmlString, child, chan, escapeNonASCII,
                               htmlEntities);
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
            tcldom_treeAsHTML (htmlString, child, chan, escapeNonASCII,
                               htmlEntities);
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
    Tcl_Channel chan,
    int         escapeNonASCII
)
{
    domAttrNode *attrs;
    domNode     *child;
    int          first, hasElements, i;

    if (node->nodeType == TEXT_NODE) {
        if (node->nodeFlags & DISABLE_OUTPUT_ESCAPING) {
            writeChars (xmlString, chan, ((domTextNode*)node)->nodeValue,
                        ((domTextNode*)node)->valueLength);
        } else {
            tcldom_AppendEscaped (xmlString, chan,
                                  ((domTextNode*)node)->nodeValue,
                                  ((domTextNode*)node)->valueLength, 0,
                                  escapeNonASCII, 0);
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

    attrs = node->firstAttr;
    while (attrs) {
        writeChars (xmlString, chan, " ", 1);
        writeChars (xmlString, chan, attrs->nodeName, -1);
        writeChars (xmlString, chan, "=\"", 2);
        tcldom_AppendEscaped (xmlString, chan, attrs->nodeValue, -1, 1,
                              escapeNonASCII, 0);
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
            tcldom_treeAsXML (xmlString, child, indent, level+1, doIndent,
                              chan, escapeNonASCII);
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
    domNode       *orgNode;
    
    orgNode = node;
    do {
        if (node->nodeFlags & HAS_BASEURI) {
            entryPtr = Tcl_FindHashEntry (&node->ownerDocument->baseURIs,
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
    if (!baseURI) {
        node = orgNode->ownerDocument->rootNode;
        if (node->nodeFlags & HAS_BASEURI) {
            entryPtr = Tcl_FindHashEntry (&node->ownerDocument->baseURIs,
                                          (char*)node->nodeNumber);
            baseURI = (char *)Tcl_GetHashValue (entryPtr);
        }
    }
    return baseURI;
}

/*----------------------------------------------------------------------------
|   serializeAsXML
|
\---------------------------------------------------------------------------*/
static int serializeAsXML (
    domNode    *node,
    Tcl_Interp *interp,
    int         objc,
    Tcl_Obj    *CONST objv[]
)
{
    char       *option, *channelId;
    int         indent, mode, escapeNonASCII = 0;
    Tcl_Obj    *resultPtr;
    Tcl_Channel chan = (Tcl_Channel) NULL;


    if (objc > 7) {
        Tcl_WrongNumArgs(interp, 2, objv,
                  "?-indent <0..8>? ?-channel <channelID>? ?-escapeNonASCII");
        return TCL_ERROR;
    }
    indent = 4;
    while (objc > 2) {
        option = Tcl_GetStringFromObj (objv[2], NULL);
        if (strcmp (option, "-indent") == 0)  {
            if (objc < 4) {
                SetResult ("-indent have an argument (0..8 or 'no'/'none')");
                return TCL_ERROR;
            }
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
            if (objc < 4) {
                SetResult ("-channel must have a channeldID as argument");
                return TCL_ERROR;
            }
            channelId = Tcl_GetStringFromObj (objv[3], NULL);
            chan = Tcl_GetChannel (interp, channelId, &mode);
            if (chan == (Tcl_Channel) NULL) {
                SetResult ("-channel must have a channeldID as argument");
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
        else if (strcmp (option, "-escapeNonASCII")==0) {
            escapeNonASCII = 1;
            objc--;
            objv++;
            continue;
        }
        else {
            Tcl_ResetResult (interp);
            Tcl_AppendResult (interp, "Unknown option \"", option, "\"", NULL);
            return TCL_ERROR;
        }
    }
    if (indent > 8)  indent = 8;
    if (indent < -1) indent = -1;

    resultPtr = Tcl_NewStringObj ("", 0);
    tcldom_treeAsXML(resultPtr, node, indent, 0, 1, chan, escapeNonASCII);
    Tcl_AppendResult (interp, Tcl_GetStringFromObj (resultPtr, NULL), NULL);
    Tcl_DecrRefCount (resultPtr);
    return TCL_OK;
}

/*----------------------------------------------------------------------------
|   serializeAsHTML
|
\---------------------------------------------------------------------------*/
static int serializeAsHTML (
    domNode    *node,
    Tcl_Interp *interp,
    int         objc,
    Tcl_Obj    *CONST objv[]
)
{
    char       *option, *channelId;
    int         mode, escapeNonASCII = 0, htmlEntities = 0;
    Tcl_Obj    *resultPtr;
    Tcl_Channel chan = (Tcl_Channel) NULL;

    if (objc > 6) {
        Tcl_WrongNumArgs(interp, 2, objv,
                   "?-channel <channelId>? ?-escapeNonASCII? ?-htmlEntities?");
        return TCL_ERROR;
    }
    while (objc > 2) {
        option = Tcl_GetStringFromObj (objv[2], NULL);
        if (strcmp (option, "-channel")==0) {
            if (objc < 4) {
                SetResult ("-channel must have a channeldID as argument");
                return TCL_ERROR;
            }
            channelId = Tcl_GetStringFromObj (objv[3], NULL);
            chan = Tcl_GetChannel (interp, channelId, &mode);
            if (chan == (Tcl_Channel) NULL) {
                SetResult ("-channel must have a channeldID as argument");
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
        else if (strcmp (option, "-escapeNonASCII")==0) {
            escapeNonASCII = 1;
            objc--;
            objv++;
            continue;
        } 
        else if (strcmp (option, "-htmlEntities")==0) {
            htmlEntities = 2;
            objc--;
            objv++;
            continue;
        } 
        else {
            Tcl_ResetResult (interp);
            Tcl_AppendResult (interp, "Unknown option \"", option, "\"", NULL);
            return TCL_ERROR;
        }
    }
    resultPtr = Tcl_NewStringObj ("", 0);
    tcldom_treeAsHTML(resultPtr, node, chan, escapeNonASCII, htmlEntities);
    Tcl_AppendResult (interp, Tcl_GetStringFromObj (resultPtr, NULL), NULL);
    Tcl_DecrRefCount (resultPtr);
    return TCL_OK;
}

/*----------------------------------------------------------------------------
|   applyXSLT
|
\---------------------------------------------------------------------------*/
static int applyXSLT (
    domNode     *node,
    Tcl_Interp  *interp,
    int          objc,
    Tcl_Obj     *CONST objv[]
    )
{
    char        *str, **parameters, *errMsg;
    Tcl_Obj     *objPtr, *localListPtr = (Tcl_Obj *)NULL;
    int          i, result, length;
    domDocument *xsltDoc, *resultDoc;

    str = Tcl_GetStringFromObj (objv[2], NULL);
    parameters = NULL;
    if ((str[0] == '-') && (strcmp (str, "-parameters")==0)) {
        if (objc < 5) {
            Tcl_WrongNumArgs (interp, 2, objv, "?-parameters parameterList? xsltDoc ?resultVar?");
            return TCL_ERROR;
        }
        if (Tcl_ListObjLength (interp, objv[3], &length) != TCL_OK) {
            SetResult ("ill-formed parameters list: the -parameters option needs a list of parameter name and parameter value pairs");
            return TCL_ERROR;
        }
        if (length % 2) {
            SetResult ("parameter value missing: the -parameters option needs a list of parameter name and parameter value pairs");
            return TCL_ERROR;
        }
        localListPtr = Tcl_DuplicateObj (objv[3]);
        Tcl_IncrRefCount (localListPtr);
        parameters =  (char **)MALLOC(sizeof (char **)*(length+1));
        for (i = 0; i < length; i ++) {
            Tcl_ListObjIndex (interp, localListPtr, i, &objPtr);
            parameters[i] = Tcl_GetStringFromObj (objPtr, NULL);
        }
        parameters[length] = NULL;
        objc -= 2;
        objv += 2;
        str = Tcl_GetStringFromObj (objv[2], NULL);
    }
    xsltDoc = tcldom_getDocumentFromName (interp, str, &errMsg);
    if (xsltDoc == NULL) {
        SetResult ( errMsg );
        if (parameters) {
            Tcl_DecrRefCount (localListPtr);
            FREE((char *) parameters);
        }
        return TCL_ERROR;
    }
    result = xsltProcess (xsltDoc, node, parameters,
                          tcldom_xpathFuncCallBack,  interp,
                          &errMsg, &resultDoc);
    if (parameters) {
        Tcl_DecrRefCount (localListPtr);
        FREE((char *) parameters);
    }
    if (result < 0) {
        SetResult ( errMsg );
        FREE(errMsg);
        return TCL_ERROR;
    }
    return tcldom_returnDocumentObj (interp, resultDoc, (objc == 4),
                                     (objc == 4) ? objv[3] : NULL);
}   

/*----------------------------------------------------------------------------
|   tcldom_NodeObjCmd
|
\---------------------------------------------------------------------------*/
int tcldom_NodeObjCmd (
    ClientData  clientData,
    Tcl_Interp *interp,
    int         objc,
    Tcl_Obj    *CONST objv[]
)
{
    GetTcldomTSD()
    domNode     *node, *child, *refChild, *oldChild;
    domNS       *ns;
    domAttrNode *attrs;
    domException exception;
    char         tmp[200], objCmdName[40], prefix[MAX_PREFIX_LEN],
                *method, *nodeName, *str, *localName,
                *attr_name, *attr_val, *filter, *errMsg, *uri;
    int          result, length, methodIndex, i, line, column;
    int          nsIndex, bool;
    Tcl_Obj     *namePtr, *resultPtr;
    Tcl_Obj     *mobjv[MAX_REWRITE_ARGS];
    Tcl_CmdInfo  cmdInfo;
    Tcl_HashEntry *entryPtr;

    static CONST84 char *nodeMethods[] = {
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
        "disableOutputEscaping",
#ifdef TCL_THREADS
        "readlock",        "writelock",
#endif
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
        m_disableOutputEscaping
#ifdef TCL_THREADS
        ,m_readlock,        m_writelock
#endif
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
    if (objc < 2) {
        SetResult (node_usage);
        return TCL_ERROR;
    }
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
        if (node->nodeType != ELEMENT_NODE) {
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
            CheckArgs(2,2,2,"");
            SetResult ( xpathNodeToXPath(node) );
            return TCL_OK;

        case m_xslt:
            return applyXSLT (node, interp, objc, objv);

        case m_selectNodes:
            CheckArgs(3,4,2,"xpathQuery");
            if (objc == 4) {
                return tcldom_selectNodes (interp, node, objv[2], objv[3]);
            } else {
                return tcldom_selectNodes (interp, node, objv[2], NULL );
            }

        case m_find:
            CheckArgs(4,5,2,"attrName attrVal ?nodeObjVar?");
            attr_name = Tcl_GetStringFromObj (objv[2], NULL);
            attr_val  = Tcl_GetStringFromObj (objv[3], &length);
            return tcldom_returnNodeObj(
                       interp,
                       tcldom_find (node, attr_name, attr_val, length),
                       (objc == 5), (objc == 5) ? objv[4] : NULL);

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
            return tcldom_returnNodeObj(interp, node, (objc == 3),
                                        (objc == 3) ? objv[2] : NULL);

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
            CheckArgs(2,2,2,"");
            Tcl_SetObjResult (interp, tcldom_treeAsTclList(interp, node) );
            break;

        case m_asXML:
            Tcl_ResetResult (interp);
            if (serializeAsXML (node, interp, objc, objv) != TCL_OK) {
                return TCL_ERROR;
            }
            break;

        case m_asHTML:
            Tcl_ResetResult (interp);
            if (serializeAsHTML (node, interp, objc, objv) != TCL_OK) {
                return TCL_ERROR;
            }
            break;

        case m_getAttribute:
            CheckArgs(3,4,2,"attrName ?defaultValue?");
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
                SetResult (Tcl_GetStringFromObj (objv[3], NULL) );
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
            if ((objc < 4) || ((objc % 2)!=0)) {
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
            if (node->nodeType != ELEMENT_NODE) {
                SetResult ( "NOT_AN_ELEMENT : there are no attributes");
                return TCL_ERROR;
            }
            if ((objc < 5) || (((objc - 2) % 3) != 0)) {
                SetResult ( "uri attrName value triples expected");
                return TCL_ERROR;
            }
            for ( i = 2; i < objc; ) {
                uri       = Tcl_GetStringFromObj (objv[i++], NULL);
                attr_name = Tcl_GetStringFromObj (objv[i++], NULL);
                attr_val  = Tcl_GetStringFromObj (objv[i++], NULL);
                attrs = domSetAttributeNS (node, attr_name, attr_val, uri, 0);
                if (!attrs) {
                    if (uri[0]) {
                        SetResult ("A attribute in a namespace must have a prefix");
                    } else {
                        SetResult ("For all prefixed attributes with prefixes other than 'xml' or 'xmlns' you have to provide a namespace URI");
                    }
                    return TCL_ERROR;
                }
            }
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
            CheckArgs(2,3,2,"?nodeObjVar?");
            return tcldom_returnNodeObj (interp, node->nextSibling, (objc == 3),
                                         (objc == 3) ? objv[2] : NULL);

        case m_previousSibling:
            CheckArgs(2,3,2,"?nodeObjVar?");
            return tcldom_returnNodeObj (interp, node->previousSibling, (objc == 3),
                                         (objc == 3) ? objv[2] : NULL);

        case m_firstChild:
            CheckArgs(2,3,2,"?nodeObjVar?");
            if (node->nodeType == ELEMENT_NODE) {
                return tcldom_returnNodeObj (interp, node->firstChild, (objc == 3),
                                             (objc == 3) ? objv[2] : NULL);
            }
            return tcldom_returnNodeObj (interp, NULL, (objc == 3),
                                         (objc == 3) ? objv[2] : NULL);

        case m_lastChild:
            CheckArgs(2,3,2,"?nodeObjVar?");
            if (node->nodeType == ELEMENT_NODE) {
                return tcldom_returnNodeObj (interp, node->lastChild, (objc == 3),
                                             (objc == 3) ? objv[2] : NULL);
            }
            return tcldom_returnNodeObj (interp, NULL, (objc == 3),
                                         (objc == 3) ? objv[2] : NULL);

        case m_parentNode:
            CheckArgs(2,3,2,"?nodeObjVar?");
            return tcldom_returnNodeObj (interp, node->parentNode, (objc == 3),
                                         (objc == 3) ? objv[2] : NULL);

        case m_appendFromList:
            CheckArgs(3,3,2,"list");
            return tcldom_appendFromTclList (interp, node, objv[2]);

        case m_appendFromScript:
            CheckArgs(3,3,2,"script");
            if (nodecmd_appendFromScript (interp, node, objv[2]) != TCL_OK) {
                return TCL_ERROR;
            }
            return tcldom_returnNodeObj (interp, node, 0, NULL);

        case m_appendXML:
            CheckArgs(3,3,2,"xmlString");
            return tcldom_appendXML (interp, node, objv[2]);

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
                SetResult (domException2String(exception));
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
            CheckArgs(4,4,2,"new old");
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
            CheckArgs (2,2,2,"");
            if (node->nodeType == ELEMENT_NODE) {
                SetIntResult( node->firstChild ? 1 : 0);
            } else {
                SetIntResult( 0 );
            }
            break;

        case m_childNodes:
            CheckArgs (2,2,2,"");
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
            CheckArgs (3,3,2,"elementName");
            if (node->nodeType != ELEMENT_NODE) {
                SetResult ("Node must be an element node.");
                return TCL_ERROR;
            }
            Tcl_ResetResult (interp);
            return tcldom_getElementsByTagName (interp,
                                                Tcl_GetStringFromObj (objv[2],
                                                                      NULL),
                                                node->firstChild, -1, NULL);

        case m_getElementsByTagNameNS:
            CheckArgs(4,4,2,"uri localname");
            if (node->nodeType != ELEMENT_NODE) {
                SetResult ("Node must be an element node.");
                return TCL_ERROR;
            }
            uri = Tcl_GetStringFromObj (objv[2], NULL);
            str = Tcl_GetStringFromObj (objv[3], NULL);
            nsIndex = -1;
            if (uri[0] == '*' && uri[1] == '\0') {
                nsIndex = -3;
            } else if (uri[0] == '\0') {
                /* all elements not in a namespace */
                nsIndex = -4;
            } else {
                for (i = 0; i <= node->ownerDocument->nsptr; i++) {
                    if (strcmp (node->ownerDocument->namespaces[i]->uri,
                                uri)==0) {
                        if (nsIndex != -1) {
                            /* OK, this is one of the 'degenerated' (though
                               legal) documents, which bind the same URI
                               to different prefixes. */
                            nsIndex = -2;
                            break;
                        }
                        nsIndex = node->ownerDocument->namespaces[i]->index;
                    }
                }
            }
            if (nsIndex == -1) {
                /* There isn't such a namespace declared in this document.
                   Since getElementsByTagNameNS doesn't raise an execption
                   short cut: return empty result */
                Tcl_ResetResult (interp);
                return TCL_OK;
            }
            return tcldom_getElementsByTagName (interp, str, node->firstChild,
                                                nsIndex, uri);
            
        case m_getElementById:
            CheckArgs(3,3,2,"id");
            str = Tcl_GetStringFromObj(objv[2], NULL);
            entryPtr = Tcl_FindHashEntry (&node->ownerDocument->ids, str);
            if (entryPtr) {
                return tcldom_returnNodeObj (interp,
                                             (domNode*) Tcl_GetHashValue (entryPtr),
                                             0, NULL);
            }
            SetResult ( "id not found");
            return TCL_ERROR;

        case m_nodeName:
            CheckArgs (2,2,2,"");
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
            CheckArgs (2,3,2,"?newValue?");
            if (node->nodeType == ELEMENT_NODE) {
                Tcl_SetStringObj (Tcl_GetObjResult (interp), "", 0);
            } else if (node->nodeType == PROCESSING_INSTRUCTION_NODE) {
                Tcl_SetStringObj (Tcl_GetObjResult (interp),
                                  ((domProcessingInstructionNode*)node)->dataValue,
                                  ((domProcessingInstructionNode*)node)->dataLength);
                
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
            CheckArgs (2,2,2,"");
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
            CheckArgs (2,2,2,"");
            str = domNamespacePrefix(node);
            if (str) {
                SetResult (str);
            } else {
                SetResult ("");
            }
            return TCL_OK;

        case m_namespaceURI:
            CheckArgs (2,2,2,"");
            str = domNamespaceURI(node);
            if (str) {
                SetResult (str);
            } else {
                SetResult ("");
            }
            return TCL_OK;

        case m_localName:
            CheckArgs (2,2,2,"");
            if (node->nodeType == ELEMENT_NODE) {
                if (node->namespace != 0) {
                    SetResult ( domGetLocalName((char*)node->nodeName) );
                    break;
                }
            }
            SetResult ("");
            break;

        case m_ownerDocument:
            CheckArgs (2,3,2,"?docObjVar?");
            return tcldom_returnDocumentObj(interp, node->ownerDocument, (objc == 3),
                                            (objc == 3) ? objv[2] : NULL);

        case m_target:
            CheckArgs (2,2,2,"");
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
            CheckArgs (2,2,2,"");
            domDeleteNode (node, tcldom_docDeleteNode, interp);
            break;

        case m_data:
            CheckArgs (2,2,2,"");
            if (node->nodeType == PROCESSING_INSTRUCTION_NODE) {
                Tcl_SetStringObj (Tcl_GetObjResult (interp),
                                  ((domProcessingInstructionNode*)node)->dataValue,
                                  ((domProcessingInstructionNode*)node)->dataLength);
            } else
            if (   node->nodeType == TEXT_NODE 
                || node->nodeType == CDATA_SECTION_NODE
                || node->nodeType == COMMENT_NODE) {
                Tcl_SetStringObj (Tcl_GetObjResult (interp),
                                  ((domTextNode*)node)->nodeValue,
                                  ((domTextNode*)node)->valueLength);
            } else {
                SetResult ("not a TEXT_NODE / CDATA_SECTION_NODE / COMMENT_NODE / PROCESSING_INSTRUCTION_NODE !");
                return TCL_ERROR;
            }
            break;

        case m_getLine:
            CheckArgs (2,2,2,"");
            if (domGetLineColumn (node, &line, &column) < 0) {
                SetResult ( "no line/column information available!");
                return TCL_ERROR;
            }
            SetIntResult (line);
            break;

        case m_getColumn:
            CheckArgs (2,2,2,"");
            if (domGetLineColumn (node, &line, &column) < 0) {
                SetResult ( "no line/column information available!");
                return TCL_ERROR;
            }
            SetIntResult (column);
            break;

        case m_getBaseURI:
            CheckArgs (2,2,2,"");
            str = findBaseURI (node);
            if (!str) {
                SetResult ("no base URI information available!");
                return TCL_ERROR;
            } else {
                SetResult (str);
            }
            break;

        case m_disableOutputEscaping:
            CheckArgs (2,3,2,"?boolean?");
            if (node->nodeType != TEXT_NODE) {
                SetResult ("not a TEXT_NODE!");
                return TCL_ERROR;
            }
            SetIntResult (
                (((node->nodeFlags & DISABLE_OUTPUT_ESCAPING) == 0) ? 0 : 1));
            if (objc == 3) {
                result = Tcl_GetBooleanFromObj (interp, objv[2], &bool);
                if (result != TCL_OK) {
                    SetResult ("second arg must be a boolean value1");
                }
                if (bool) {
                    node->nodeFlags |= DISABLE_OUTPUT_ESCAPING;
                } else {
                    node->nodeFlags &= (~DISABLE_OUTPUT_ESCAPING);
                }
            }
            break;
            
        TDomThreaded(
        case m_writelock:
            CheckArgs(3,3,2,"script");
            return tcldom_EvalLocked(interp, (Tcl_Obj**)objv, node->ownerDocument,
                                     LOCK_WRITE);

        case m_readlock:
            CheckArgs(3,3,2,"script");
            return tcldom_EvalLocked(interp, (Tcl_Obj**)objv, node->ownerDocument,
                                     LOCK_READ);
        )
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
    char                * method, *tag, *data, *target, *uri, tmp[100];
    char                  objCmdName[40], *str;
    int                   methodIndex, result, data_length, target_length, i;
    int                   nsIndex;
    domNode             * n;
    Tcl_CmdInfo           cmdInfo;
    Tcl_Obj             * mobjv[MAX_REWRITE_ARGS];


    static CONST84 char *docMethods[] = {
        "documentElement", "getElementsByTagName",       "delete",
        "createElement",   "createCDATASection",         "createTextNode",
        "createComment",   "createProcessingInstruction",
        "createElementNS", "getDefaultOutputMethod",     "asXML",
        "asHTML",          "getElementsByTagNameNS",     "xslt", 
#ifdef TCL_THREADS
        "readlock", "writelock",
#endif
        NULL
    };
    enum docMethod {
        m_documentElement,  m_getElementsByTagName,       m_delete,
        m_createElement,    m_createCDATASection,         m_createTextNode,
        m_createComment,    m_createProcessingInstruction,
        m_createElementNS,  m_getdefaultoutputmethod,     m_asXML,
        m_asHTML,           m_getElementsByTagNameNS,     m_xslt
#ifdef TCL_THREADS
        ,m_readlock, m_writelock
#endif
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

    CheckArgs (2,6,1,domObj_usage);

    /*----------------------------------------------------------------------
    |   dispatch the doc object method
    |
    \---------------------------------------------------------------------*/
    dinfo = (TcldomDocDeleteInfo*) clientData;
    doc = dinfo->document;

    switch ((enum docMethod) methodIndex ) {

        case m_documentElement:
            CheckArgs (2,3,2,"");
            return tcldom_returnNodeObj (interp, doc->documentElement,
                                         (objc == 3), (objc == 3) ? objv[2] : NULL);

        case m_getElementsByTagName:
            CheckArgs (3,3,2,"elementName");
            return tcldom_getElementsByTagName (interp,
                                                Tcl_GetStringFromObj (objv[2],
                                                                      NULL),
                                                doc->documentElement, -1,
                                                NULL);

        case m_getElementsByTagNameNS:
            CheckArgs(4,4,2,"uri localname");
            uri = Tcl_GetStringFromObj (objv[2], NULL);
            str = Tcl_GetStringFromObj (objv[3], NULL);
            nsIndex = -1;
            if (uri[0] == '*' && uri[1] == '\0') {
                nsIndex = -3;
            } else if (uri[0] == '\0') {
                /* all elements not in a namespace i.e. */
                nsIndex = -4;
            } else {
                for (i = 0; i <= doc->nsptr; i++) {
                    if (strcmp (doc->namespaces[i]->uri, uri)==0) {
                        if (nsIndex != -1) {
                            /* OK, this is one of the 'degenerated' (though
                               legal) documents, which bind the same URI
                               to different prefixes. */
                            nsIndex = -2;
                            break;
                        }
                        nsIndex = doc->namespaces[i]->index;
                    }
                }
            }
            if (nsIndex == -1) {
                /* There isn't such a namespace declared in this document.
                   Since getElementsByTagNameNS doesn't raise an execption
                   short cut: return empty result */
                Tcl_ResetResult (interp);
                return TCL_OK;
            }
            return tcldom_getElementsByTagName (interp, str, 
                                                doc->documentElement, nsIndex,
                                                uri);
            
        case m_createElement:
            CheckArgs (3,4,2,"elementName ?newObjVar?");
            tag = Tcl_GetStringFromObj (objv[2], NULL);
            n = domNewElementNode(doc, tag, ELEMENT_NODE);
            return tcldom_returnNodeObj (interp, n, (objc == 4),
                                         (objc == 4) ? objv[3] : NULL);

        case m_createElementNS:
            CheckArgs (4,5,2,"elementName uri ?newObjVar?");
            uri = Tcl_GetStringFromObj (objv[2], NULL);
            tag = Tcl_GetStringFromObj (objv[3], NULL);
            n = domNewElementNodeNS(doc, tag, uri, ELEMENT_NODE);
            return tcldom_returnNodeObj (interp, n, (objc == 5),
                                         (objc == 5) ? objv[4] : NULL);

        case m_createTextNode:
            CheckArgs (3,4,2,"data ?newObjVar?");
            data = Tcl_GetStringFromObj (objv[2], &data_length);
            n = (domNode*)domNewTextNode(doc, data, data_length, TEXT_NODE);
            return tcldom_returnNodeObj (interp, n, (objc == 4),
                                         (objc == 4) ? objv[3] : NULL);

        case m_createCDATASection:
            CheckArgs (3,4,2,"data ?newObjVar?");
            data = Tcl_GetStringFromObj (objv[2], &data_length);
            n = (domNode*)domNewTextNode(doc, data, data_length, CDATA_SECTION_NODE);
            return tcldom_returnNodeObj (interp, n, (objc == 4),
                                         (objc == 4) ? objv[3] : NULL);

        case m_createComment:
            CheckArgs (3,4,2,"data ?newObjVar?");
            data = Tcl_GetStringFromObj (objv[2], &data_length);
            n = (domNode*)domNewTextNode(doc, data, data_length, COMMENT_NODE);
            return tcldom_returnNodeObj (interp, n, (objc == 4),
                                         (objc == 4) ? objv[3] : NULL);

        case m_createProcessingInstruction:
            CheckArgs (4,5,2,"target data ?newObjVar?");
            target = Tcl_GetStringFromObj (objv[2], &target_length);
            data   = Tcl_GetStringFromObj (objv[3], &data_length);
            n = (domNode*)domNewProcessingInstructionNode(doc,
                                                          target, target_length,
                                                          data,   data_length);
            return tcldom_returnNodeObj (interp, n, (objc == 5),
                                         (objc == 5) ? objv[4] : NULL);

        case m_delete:
            CheckArgs (2,2,2,"");
            DOC_CMD(objCmdName, doc);
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
            if (doc->nodeFlags & OUTPUT_DEFAULT_UNKNOWN) {
                SetResult ("unknown");
            } else {
                SetResult ("none");
            }
            return TCL_OK;

        case m_asXML:
            Tcl_ResetResult (interp);
            n = doc->rootNode->firstChild;
            while (n) {
                if (serializeAsXML (n, interp, objc, objv) != TCL_OK) {
                    return TCL_ERROR;
                }
                n = n->nextSibling;
            }
            return TCL_OK;

        case m_asHTML:
            Tcl_ResetResult (interp);
            n = doc->rootNode->firstChild;
            while (n) {
                if (serializeAsHTML (n, interp, objc, objv) != TCL_OK) {
                    return TCL_ERROR;
                }
                n = n->nextSibling;
            }
            return TCL_OK;

        case m_xslt:
            return applyXSLT ((domNode *) doc, interp, objc, objv);

        TDomThreaded(
        case m_writelock:
            CheckArgs(3,3,2,"script");
            return tcldom_EvalLocked(interp, (Tcl_Obj**)objv, doc, LOCK_WRITE);

        case m_readlock:
            CheckArgs(3,3,2,"script");
            return tcldom_EvalLocked(interp, (Tcl_Obj**)objv, doc, LOCK_READ);
        )
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
    doc = domCreateDocument ( interp, NULL,
                              Tcl_GetStringFromObj (objv[1], NULL) );
    if (!doc) return TCL_ERROR;
    return tcldom_returnDocumentObj(
                 interp, doc, setVariable, newObjName
    );
}

/*----------------------------------------------------------------------------
|   tcldom_createDocumentNS
|
\---------------------------------------------------------------------------*/
static
int tcldom_createDocumentNS (
    ClientData  clientData,
    Tcl_Interp *interp,
    int         objc,
    Tcl_Obj    * const objv[]
)
{
    int          setVariable = 0;
    domDocument *doc;
    Tcl_Obj     *newObjName = NULL;


    CheckArgs (3,4,1,"uri docElemName ?newObjVar?");

    if (objc == 4) {
        newObjName = objv[3];
        setVariable = 1;
    }
    doc = domCreateDocument ( interp, Tcl_GetStringFromObj (objv[1], NULL),
                              Tcl_GetStringFromObj (objv[2], NULL) );
    if (!doc) return TCL_ERROR;
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
        xml_string = Tcl_GetStringFromObj( objv[1], &xml_string_len);
        if (objc == 3) {
            newObjName = objv[2];
            setVariable = 1;
        }
    } else {
        xml_string = NULL;
        if (takeSimpleParser || takeHTMLParser) {
            Tcl_AppendResult(interp,
                "simple and/or HTML parser(s) don't support channel reading", NULL);
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
                                          baseURI, extResolver,
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
                FREE(errStr);
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
    parser = XML_ParserCreate(NULL);

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
    char        *localName, prefix[MAX_PREFIX_LEN];
    int           methodIndex, result, i, bool;
    Tcl_CmdInfo   cmdInfo;
    Tcl_Obj     * mobjv[MAX_REWRITE_ARGS];

    static CONST84 char *domMethods[] = {
        "createDocument",  "createDocumentNS",  "createNodeCmd",
        "parse",           "setResultEncoding", "setStoreLineColumn",
        "isCharData",      "isName",            "isQName",
        "isNCName", 
#ifdef TCL_THREADS
        "attachDocument",
#endif
        NULL
    };
    enum domMethod {
        m_createDocument,    m_createDocumentNS,  m_createNodeCmd,
        m_parse,             m_setResultEncoding, m_setStoreLineColumn,
        m_isCharData,        m_isName,            m_isQName,
        m_isNCName
#ifdef TCL_THREADS
        ,m_attachDocument
#endif
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
            return tcldom_createDocument (clientData, interp, --objc, objv+1);

        case m_createDocumentNS:
            return tcldom_createDocumentNS (clientData, interp, --objc, objv+1);
                                            
        case m_createNodeCmd:
            return nodecmd_createNodeCmd (clientData, interp, --objc, objv+1);

        case m_parse:
            return tcldom_parse (clientData, interp, --objc, objv+1);

#ifdef TCL_THREADS
        case m_attachDocument:
            {
                char *cmdName, *errMsg;
                domDocument *doc;
                if (objc < 3) {
                    SetResult(dom_usage);
                    return TCL_ERROR;
                }
                cmdName = Tcl_GetStringFromObj(objv[2], NULL);
                doc = tcldom_getDocumentFromName(interp, cmdName, &errMsg);
                if (doc == NULL) {
                    SetResult(errMsg);
                    return TCL_ERROR;
                }
                return tcldom_returnDocumentObj(interp, doc, (objc == 4),
                                                (objc == 4) ? objv[3] : NULL);
            }
            break;
#endif

        case m_setResultEncoding:
            return tcldom_setResultEncoding (clientData, interp, --objc, objv+1);

        case m_setStoreLineColumn:
            SetIntResult (TSD(storeLineColumn));
            if (objc == 3) {
                Tcl_GetBooleanFromObj (interp, objv[2], &bool);
                TSD(storeLineColumn) = bool;
            }
            return TCL_OK;

        case m_isCharData:
            CheckArgs(3,3,2,"string");
            SetBooleanResult (domIsChar (Tcl_GetStringFromObj(objv[2],NULL)));
            return TCL_OK;
            
        case m_isName:
            CheckArgs(3,3,2,"string");
            SetBooleanResult (domIsNAME (Tcl_GetStringFromObj(objv[2],NULL)));
            return TCL_OK;
            
        case m_isQName:
            CheckArgs(3,3,2,"string");
            if ((Tcl_GetStringFromObj(objv[2], NULL))[0] == ':') {
                SetBooleanResult (0);
            } else {
                domSplitQName (Tcl_GetStringFromObj(objv[2],NULL), prefix,
                               &localName);
                if (prefix[0]) {
                    SetBooleanResult ((domIsNCNAME(prefix) 
                                       && domIsNCNAME(localName)));
                } else {
                    SetBooleanResult (domIsNCNAME (localName));
                }
            }
            return TCL_OK;

        case m_isNCName:
            CheckArgs(3,3,2,"string");
            SetBooleanResult (domIsNCNAME(Tcl_GetStringFromObj(objv[2],NULL)));
            return TCL_OK;

    }
    SetResult ( dom_usage);
    return TCL_ERROR;
}

#ifdef TCL_THREADS
/*----------------------------------------------------------------------------
|   tcldom_EvalLocked
|
\---------------------------------------------------------------------------*/
static
int tcldom_EvalLocked (
    Tcl_Interp*  interp,
    Tcl_Obj**    objv,
    domDocument* doc,
    int          flag
)
{
    int ret = TCL_OK;
    domlock *dl = doc->lock;

    domLocksLock(dl, flag);

    Tcl_AllowExceptions(interp);
    ret = Tcl_EvalObj(interp, objv[2]);
    if (ret == TCL_ERROR) {
        char msg[64 + TCL_INTEGER_SPACE];
        sprintf(msg, "\n    (\"%s %s\" body line %d)",
                Tcl_GetStringFromObj(objv[0], NULL),
                Tcl_GetStringFromObj(objv[1], NULL), interp->errorLine);
        Tcl_AddErrorInfo(interp, msg);
    }

    domLocksUnlock(dl);

    return (ret == TCL_BREAK) ? TCL_OK : ret;
}
#endif /* TCL_THREADS */

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

            objvCall = (Tcl_Obj**)MALLOC(sizeof (Tcl_Obj*) * (objc+args));

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
            FREE((void*)objvCall);

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

