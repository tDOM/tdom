/*----------------------------------------------------------------------------
|   Copyright (c) 2000 Jochen Loewer (loewerj@hotmail.com)
|-----------------------------------------------------------------------------
|
|   $Header$
|
| 
|   A (partial) XSLT implementation for tDOM, according to the W3C 
|   recommendation (16 Nov 1999).
|   See http://www.w3.org/TR/1999/REC-xslt-19991116 for details.
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
|   Portions created by Jochen Loewer are Copyright (C) 1999, 2000
|   Jochen Loewer. All Rights Reserved.
|
|   Contributor(s):
|       Aug01    Rolf Ade   xsl:include, xsl:import, xsl:apply-imports,
|                           document() plus several bug fixes
|
|       Fall/Winter 01 Rolf Ade rewrite of xsl:number, xsl:key/key(),
|                               handling of toplevel var/parameter,
|                               plenty of fixes and enhancements all
|                               over the place.
|
|
|   written by Jochen Loewer
|   June, 2000
|
\---------------------------------------------------------------------------*/



/*----------------------------------------------------------------------------
|   Includes
|
\---------------------------------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <limits.h>
#include <ctype.h>
#include <dom.h>
#include <domxpath.h>
#include <domxslt.h>
#include <tcl.h>         /* for hash tables */


/*----------------------------------------------------------------------------
|   Defines
|
\---------------------------------------------------------------------------*/
#define XSLT_NAMESPACE  "http://www.w3.org/1999/XSL/Transform"
#define PRIO_UNKNOWN    -111.0


/*----------------------------------------------------------------------------
|   Macros
|
\---------------------------------------------------------------------------*/
#define DBG(x)            
#define TRACE(x)          DBG(fprintf(stderr,(x)))
#define TRACE1(x,a)       DBG(fprintf(stderr,(x),(a)))
#define TRACE2(x,a,b)     DBG(fprintf(stderr,(x),(a),(b)))
#define TRACE3(x,a,b,c)   DBG(fprintf(stderr,(x),(a),(b),(c)))
#define TRACE4(x,a,b,c,d) DBG(fprintf(stderr,(x),(a),(b),(c),(d)))

#define CHECK_RC          if (rc < 0) return rc
#define CHECK_RC1(x)      if (rc < 0) {ckfree((char*)(x)); return rc;}       
#define SET_TAG(t,n,s,v)  if (strcmp(n,s)==0) { t->info = v; return v; }

#if defined(_MSC_VER)
# define STRCASECMP(a,b)  stricmp (a,b)
#else
# define STRCASECMP(a,b)  strcasecmp (a,b)
#endif

extern void printAst (int depth, ast t);

/*--------------------------------------------------------------------------
|   xsltTag
|
\-------------------------------------------------------------------------*/
typedef enum {

    unknown = 1,
    applyImports, applyTemplates, attribute, attributeSet, callTemplate, 
    choose, comment, copy, copyOf, decimalFormat, element, fallback, forEach,
    xsltIf, import,
    include, key, message, namespaceAlias, number, output, otherwise,
    param, procinstr,
    preserveSpace, sort, stylesheet, stripSpace, text, template,
    transform, valueOf, variable, when, withParam

} xsltTag;


/*--------------------------------------------------------------------------
|   xsltAttr
|
\-------------------------------------------------------------------------*/
typedef enum {

    a_caseorder = 1, 
    a_count, a_dataType, a_disableOutputEscaping,
    a_doctypePublic, a_doctypeSystem, a_elements, a_encoding,
    a_format, a_from, a_href, a_lang, a_level, a_match, a_mediaType, a_method,
    a_mode, a_name, a_namespace, a_order, a_prio, a_select, a_space,
    a_terminate, a_test, a_use, a_useAttributeSets, a_value, 
    a_groupingSeparator, a_groupingSize,    
    a_decimalSeparator, a_infinity, a_minusSign, a_nan, a_percent, 
    a_perMille, a_zeroDigit, a_digit, a_patternSeparator, a_version,
    a_excludeResultPrefixes, a_extensionElementPrefixes,
    a_stylesheetPrefix, a_resultPrefix

} xsltAttr;



/*--------------------------------------------------------------------------
|   xsltTemplate
|
\-------------------------------------------------------------------------*/
typedef struct xsltTemplate {

    char    * match;
    char    * name;
    ast       ast;
    char    * mode;
    double    prio;
    domNode * content;
    double    precedence;
        
    struct xsltTemplate *next;
    
} xsltTemplate;


/*--------------------------------------------------------------------------
|   xsltAttrSet
|
\-------------------------------------------------------------------------*/
typedef struct xsltAttrSet {

    char    * name;
    domNode * content;
        
    struct xsltAttrSet *next;
    
} xsltAttrSet;

/*--------------------------------------------------------------------------
|   xsltKeyValue
|
\-------------------------------------------------------------------------*/
typedef struct xsltKeyValue {

    domNode       * node;

    struct xsltKeyValue * next;
    
} xsltKeyValue;

/*--------------------------------------------------------------------------
|   xsltKeyValues
|
\-------------------------------------------------------------------------*/
typedef struct xsltKeyValues {

    xsltKeyValue  * value;
    xsltKeyValue  * lastvalue;

} xsltKeyValues;


/*--------------------------------------------------------------------------
|   xsltKeyInfo
|
\-------------------------------------------------------------------------*/
typedef struct xsltKeyInfo {

    domNode       * node;
    char          * match;
    ast             matchAst;
    char          * use;
    ast             useAst;
    
    struct xsltKeyInfo * next;
    
} xsltKeyInfo;

/*--------------------------------------------------------------------------
|   xsltVariable
|
\-------------------------------------------------------------------------*/
typedef struct xsltVariable {

    char           * name;
    domNode        * node;
    xpathResultSet   rs;
    
} xsltVariable;


/*--------------------------------------------------------------------------
|   xsltVarFrame
|
\-------------------------------------------------------------------------*/
typedef struct xsltVarFrame {

    xsltVariable        * vars;
    int                   polluted;
    int                   nrOfVars;
    int                   varStartIndex;

} xsltVarFrame;


/*--------------------------------------------------------------------------
|   xsltExcludeNS
|
\-------------------------------------------------------------------------*/
typedef struct xsltExcludeNS
{
    char                 * uri;

    struct xsltExcludeNS * next;

} xsltExcludeNS;


/*--------------------------------------------------------------------------
|   xsltSubDocs
|
\-------------------------------------------------------------------------*/
typedef struct xsltSubDoc 
{
    domDocument        * doc;
    char               * baseURI;
    Tcl_HashTable        keyData;
    xsltExcludeNS      * excludeNS;

    struct xsltSubDoc  * next;

} xsltSubDoc;


/*--------------------------------------------------------------------------
|   xsltTopLevelVar
|
\-------------------------------------------------------------------------*/
typedef struct xsltTopLevelVar
{

    domNode                 * node;
    int                       isParameter; 
    double                    precedence;

} xsltTopLevelVar;


/*--------------------------------------------------------------------------
|   xsltVarInProcess
|
\-------------------------------------------------------------------------*/
typedef struct xsltVarInProcess

{
    char                    *name;

    struct xsltVarInProcess *next;

} xsltVarInProcess;


/*--------------------------------------------------------------------------
|   xsltDecimalFormat
|
\-------------------------------------------------------------------------*/
typedef struct xsltDecimalFormat
{
    char   * name;
    char     decimalSeparator;
    char     groupingSeparator;
    char   * infinity;
    char     minusSign;
    char   * NaN;
    char     percent;
    char     zeroDigit;
    char     patternSeparator;
    
    struct xsltDecimalFormat * next;
    
} xsltDecimalFormat;


/*--------------------------------------------------------------------------
|   xsltWSInfo
|
\-------------------------------------------------------------------------*/
typedef struct xsltWSInfo
{

    int            hasData;
    double         wildcardPrec;
    Tcl_HashTable  NCNames;
    Tcl_HashTable  FQNames;
    Tcl_HashTable  NSWildcards;

} xsltWSInfo;


typedef struct xsltNSAlias
{
    
    char    *fromUri;
    char    *toUri;
    double   precedence;
    
    struct xsltNSAlias *next;
    
} xsltNSAlias;


/*--------------------------------------------------------------------------
|   xsltState
|
\-------------------------------------------------------------------------*/
typedef struct {

    xsltTemplate      * templates;
    xsltTemplate      * lastTemplate;
    xsltWSInfo          stripInfo;
    xsltWSInfo          preserveInfo;
    domNode           * xmlRootNode;
    domDocument       * resultDoc;
    domNode           * lastNode;
    xsltVarFrame      * varFrames;
    xsltVarFrame      * varFramesStack;
    int                 varFramesStackPtr;
    int                 varFramesStackLen;
    xsltVariable      * varStack;
    int                 varStackPtr;
    int                 varStackLen;
    xsltAttrSet       * attrSets;
    Tcl_HashTable       xpaths;
    Tcl_HashTable       pattern;
    Tcl_HashTable       formats;
    Tcl_HashTable       topLevelVars;
    Tcl_HashTable       keyInfos;
    xsltNSAlias       * nsAliases;
    int                 nsUniqeNr;
    xsltVarInProcess  * varsInProcess;
    char              * outputMethod;
    char              * outputEncoding;
    char              * outputMediaType;
    xpathCBs            cbs;
    xpathFuncCallback   orig_funcCB;
    void              * orig_funcClientData;
    xsltDecimalFormat * decimalFormats;
    domNode           * current;
    xsltSubDoc        * subDocs;
    xsltTemplate      * currentTplRule;
    domNode           * currentXSLTNode;
    domDocument       * xsltDoc;

} xsltState;


typedef enum {
    latin_number, latin_upper, latin_lower, 
    roman_upper, roman_lower
} xsltNumberingType;


/*--------------------------------------------------------------------------
|   xsltNumberFormatToken
|
\-------------------------------------------------------------------------*/
typedef struct {

    xsltNumberingType  type;
    int                minlength;
    char              *sepStart;
    int                sepLen;

} xsltNumberFormatToken;


/*--------------------------------------------------------------------------
|   xsltNumberFormat
|
\-------------------------------------------------------------------------*/
typedef struct {

    char                  *formatStr;
    int                    prologLen;
    xsltNumberFormatToken *tokens;
    int                    maxtokens;
    char                  *epilogStart;
    int                    epilogLen;

} xsltNumberFormat;


/*--------------------------------------------------------------------------
|   Prototypes
|
\-------------------------------------------------------------------------*/
int ApplyTemplates ( xsltState *xs, xpathResultSet *context, 
                     domNode *currentNode, int currentPos,
                     domNode *actionNode, xpathResultSet *nodeList, 
                     char *mode, char **errMsg);

int ApplyTemplate (  xsltState *xs, xpathResultSet *context,
                     domNode *currentNode, domNode *exprContext,
                     int currentPos, char *mode, char **errMsg);
                     
static int ExecActions (xsltState *xs, xpathResultSet *context,
                        domNode *currentNode, int currentPos, 
                        domNode *actionNode,  char **errMsg);

static domDocument * getExternalDocument (Tcl_Interp *interp, xsltState *xs,
                                          domDocument *xsltDoc, char *baseURI,
                                          char *href, char **errMsg);


/*----------------------------------------------------------------------------
|   printXML
|
\---------------------------------------------------------------------------*/
static void printXML (domNode *node, int level, int maxlevel) {

    domTextNode *tnode;
    domProcessingInstructionNode *pi;
    char tmp[80];
    int  i, l, n;
    
    n = 0;
    while (node) {

        for (i=0;i<level;i++) fprintf(stderr, "  ");
        if (node->nodeType == ELEMENT_NODE) {
            if (node->firstChild && node->firstChild->nodeType == TEXT_NODE) {
                tnode = (domTextNode*)node->firstChild;
                l = tnode->valueLength;
                if (l > 30) l = 30;
                memmove(tmp, tnode->nodeValue, l);
                tmp[l] = '\0';
                fprintf(stderr, "<%s/%d> '%s'\n", node->nodeName, node->nodeNumber, tmp);
            } else {
                tmp[0] = '\0';
                if ((level>=maxlevel) && (node->firstChild)) {
                    strcpy( tmp, "...");
                }
                fprintf(stderr, "<%s/%d> %s\n", node->nodeName, node->nodeNumber, tmp);
            }
            if (level<maxlevel) {
                if (node->firstChild) printXML(node->firstChild, level+1, maxlevel);
            }
        }
        if (node->nodeType == TEXT_NODE) {
            tnode = (domTextNode*)node;
            l = tnode->valueLength;
            if (l > 70) l = 70;
            memmove(tmp, tnode->nodeValue, l);
            tmp[l] = '\0';
            fprintf(stderr, "'%s'\n", tmp);
        }
        if (node->nodeType == COMMENT_NODE) {
            tnode = (domTextNode*)node;
            l = tnode->valueLength;
            memmove (tmp, "<!--", 4);
            if (l >70) l = 70;
            memmove (&tmp[4], tnode->nodeValue, l);
            memmove (&tmp[4+l], "-->", 3);
            tmp[7+l] = '\0';
            fprintf(stderr, "'%s'\n", tmp);
        }
        if (node->nodeType == PROCESSING_INSTRUCTION_NODE) {
            pi = (domProcessingInstructionNode*)node;
            l = pi->targetLength;
            if (l > 70) l = 70;
            memmove(tmp, pi->targetValue, l);
            tmp[l] = '\0';
            fprintf(stderr, "<?%s ", tmp);
            l = pi->dataLength;
            if (l > 70) l = 70;
            memmove(tmp, pi->dataValue, l);
            tmp[l] = '\0';
            fprintf(stderr, "%s?>\n", tmp);
        }
        node = node->nextSibling;
        n++;
        if (n>8) { fprintf(stderr, "...\n"); return; }
    }
}

/*----------------------------------------------------------------------------
|   reportError
|
\---------------------------------------------------------------------------*/
static void
reportError (
    domNode * node,
    char    * str, 
    char   ** errMsg)
{
    Tcl_DString dStr;
    char *v, *baseURI, buffer[1024];
    domLineColumn  *lc;
    
    Tcl_DStringInit (&dStr);
    baseURI = findBaseURI (node);
    if (baseURI) {
        Tcl_DStringAppend (&dStr, "In entity ", 10);
        Tcl_DStringAppend (&dStr, baseURI, -1);
    }
    if (node->nodeFlags & HAS_LINE_COLUMN) {
        v = (char *)node + sizeof(domNode);
        lc = (domLineColumn *)v;
        sprintf (buffer, " at line %d, column %d:\n%s", lc->line, lc->column,
                 str);

        Tcl_DStringAppend (&dStr, buffer, -1);
    } else {
        Tcl_DStringAppend (&dStr, ": ", 2);
        Tcl_DStringAppend (&dStr, str, -1);
    }
    *errMsg = strdup (Tcl_DStringValue (&dStr));
    Tcl_DStringFree (&dStr);
}

/*----------------------------------------------------------------------------
|   getAttr
|
\---------------------------------------------------------------------------*/
static char * getAttr (
    domNode  *node,
    char     *name,
    xsltAttr  attrTypeNo
)
{
    domAttrNode *attr;
    
    attr = node->firstAttr; 
    while (attr) {
    
        if (attr->info == attrTypeNo) {
            return attr->nodeValue;
        } else if (attr->info == 0) {
            if (strcmp ((char*)attr->nodeName, name)==0) {
                attr->info = attrTypeNo;
                return attr->nodeValue;
            }
        }
        attr = attr->nextSibling;
    }
    return NULL;
}


/*----------------------------------------------------------------------------
|   getTag
|
\---------------------------------------------------------------------------*/
static xsltTag getTag (
    domNode  *node
)
{
    char *name;
  
    if (node->nodeType != ELEMENT_NODE) {
        return unknown;
    }
    if (node->info != 0) {
        return (xsltTag)node->info;
    }
    name = domNamespaceURI(node);
    if ((name == NULL) || (strcmp(name, XSLT_NAMESPACE)!=0)) {
       node->info = (int)unknown;
       return unknown;
    }    
    name = domGetLocalName(node->nodeName);

    switch (*name) {
        case 'a': SET_TAG(node,name,"apply-imports",  applyImports);
                  SET_TAG(node,name,"apply-templates",applyTemplates);
                  SET_TAG(node,name,"attribute",      attribute);
                  SET_TAG(node,name,"attribute-set",  attributeSet);
                  break;
        case 'c': SET_TAG(node,name,"call-template",  callTemplate);
                  SET_TAG(node,name,"choose",         choose);
                  SET_TAG(node,name,"comment",        comment);
                  SET_TAG(node,name,"copy",           copy);
                  SET_TAG(node,name,"copy-of",        copyOf);
                  break;
        case 'd': SET_TAG(node,name,"decimal-format", decimalFormat);
                  break;
        case 'e': SET_TAG(node,name,"element",        element);
                  break;
        case 'f': SET_TAG(node,name,"fallback",       fallback);
                  SET_TAG(node,name,"for-each",       forEach);
                  break;
        case 'i': SET_TAG(node,name,"if",             xsltIf);
                  SET_TAG(node,name,"import",         import);
                  SET_TAG(node,name,"include",        include);
                  break;
        case 'k': SET_TAG(node,name,"key",            key);
                  break;
        case 'm': SET_TAG(node,name,"message",        message);
                  break;
        case 'n': SET_TAG(node,name,"namespace-alias",namespaceAlias);
                  SET_TAG(node,name,"number",         number);
                  break;
        case 'o': SET_TAG(node,name,"output",         output);
                  SET_TAG(node,name,"otherwise",      otherwise);
                  break;
        case 'p': SET_TAG(node,name,"param",          param);
                  SET_TAG(node,name,"preserve-space", preserveSpace);
                  SET_TAG(node,name,"processing-instruction", procinstr);
                  break;
        case 's': SET_TAG(node,name,"sort",           sort);
                  SET_TAG(node,name,"stylesheet",     stylesheet);
                  SET_TAG(node,name,"strip-space",    stripSpace);
                  break;
        case 't': SET_TAG(node,name,"template",       template);
                  SET_TAG(node,name,"text",           text);
                  SET_TAG(node,name,"transform",      transform);
                  break;
        case 'v': SET_TAG(node,name,"value-of",       valueOf);
                  SET_TAG(node,name,"variable",       variable);
                  break;
        case 'w': SET_TAG(node,name,"when",           when);
                  SET_TAG(node,name,"with-param",     withParam);
                  break;
    } 
    node->info = (int)unknown;
    return unknown;    
}


/*----------------------------------------------------------------------------
|   xsltPopVarFrame
|
\---------------------------------------------------------------------------*/
static void xsltPopVarFrame (
    xsltState  * xs
)
{
    int             i;

    if (xs->varFrames) {    
        if (xs->varFrames->nrOfVars) {
            for (i = xs->varFrames->varStartIndex;
                 i < xs->varFrames->varStartIndex + xs->varFrames->nrOfVars;
                 i++) {
                xpathRSFree (&((&xs->varStack[i])->rs));
            }
        }
        xs->varStackPtr -= xs->varFrames->nrOfVars;
        xs->varFramesStackPtr--;
        xs->varFrames = &(xs->varFramesStack[xs->varFramesStackPtr]);
    }
}


/*----------------------------------------------------------------------------
|   xsltPushVarFrame
|
\---------------------------------------------------------------------------*/
static void xsltPushVarFrame (
    xsltState  * xs
)
{
    xsltVarFrame  * currentFrame;
    
    xs->varFramesStackPtr++;
    if (xs->varFramesStackPtr >= xs->varFramesStackLen) {
        xs->varFramesStack = (xsltVarFrame *) realloc (xs->varFramesStack,
                                                   sizeof (xsltVarFrame)
                                                  * 2 * xs->varFramesStackLen);
        xs->varFramesStackLen *= 2;
    }
    currentFrame = &(xs->varFramesStack[xs->varFramesStackPtr]);
    currentFrame->polluted = 0;
    currentFrame->nrOfVars = 0;
    currentFrame->varStartIndex = -1;

    xs->varFrames = currentFrame;
    
}


/*----------------------------------------------------------------------------
|   xsltAddExternalDocument
|
\---------------------------------------------------------------------------*/
static int xsltAddExternalDocument (
    xsltState       * xs,
    char            * baseURI,
    char            * str,
    xpathResultSet  * result,
    char           ** errMsg
)
{
    xsltSubDoc  * sdoc;    
    domDocument * extDocument;
    int           found;
    
    found = 0;
    sdoc = xs->subDocs;
    while (sdoc) {
        if (strcmp (sdoc->baseURI, str)==0) {
            rsAddNode (result, sdoc->doc->rootNode);
            found = 1;
            break;
        }
        sdoc = sdoc->next;
    }
    if (!found) {
        if (!xs->xsltDoc->extResolver) {
            *errMsg = strdup("need resolver Script to include Stylesheet! (use \"-externalentitycommand\")");
            return -1;
        }
        extDocument = getExternalDocument (
                         (Tcl_Interp*)xs->orig_funcClientData,
                         xs, xs->xsltDoc, baseURI, str, errMsg);
        if (extDocument) {
            rsAddNode (result, extDocument->rootNode);
        } else {
            return -1;
        }
    }
    return found;
}


/*----------------------------------------------------------------------------
|   xsltNumberFormatTokenizer
|
\---------------------------------------------------------------------------*/
static xsltNumberFormat* xsltNumberFormatTokenizer (
    xsltState  *xs,
    char       *formatStr,
    char      **errMsg
)
{
    char             *p;
    int               hnew, clen, nrOfTokens = 0;
    Tcl_HashEntry    *h;
    xsltNumberFormat *format;
    
    /* TODO: make it l18n aware. */

    h = Tcl_CreateHashEntry (&xs->formats, formatStr, &hnew);
    if (!hnew) {
        return (xsltNumberFormat *) Tcl_GetHashValue (h);
    } else {
        format = (xsltNumberFormat *) Tcl_Alloc (sizeof (xsltNumberFormat));
        memset (format, 0 , sizeof (xsltNumberFormat));
        format->tokens = (xsltNumberFormatToken *) 
            Tcl_Alloc (sizeof (xsltNumberFormatToken) * 20);
        memset (format->tokens, 0, sizeof (xsltNumberFormatToken) * 20);
        format->maxtokens = 20;
        Tcl_SetHashValue (h, format);
        format->formatStr = p = Tcl_GetHashKey (&(xs->formats), h);
    }
    while (*p) {
        clen = UTF8_CHAR_LEN(*p);
        if (!clen) {
            *errMsg = 
                strdup("xsl:number: UTF-8 form of character longer than 3 Byte");
            return NULL;
        }
        if (clen > 1) {
            /* hack: skip over multibyte chars - this may be wrong */
            format->prologLen += clen;
            p += clen;
            continue;
        }
        if (isalnum(*p)) break;
        format->prologLen++;
        p++;
    }

    format->tokens[0].minlength = 1;
    if (!*p) {
        format->tokens[0].type = latin_number;
        return format;
    }

#define addSeperator  \
    p++;                                         \
    if (*p) {                                    \
        format->tokens[nrOfTokens].sepStart = p; \
    }                                            \
    while (*p) {                                 \
        clen = UTF8_CHAR_LEN(*p);                \
        if (!clen) {                             \
            *errMsg = strdup("xsl:number: UTF-8 form of character longer than 3 Byte"); \
            return NULL;                         \
        }                                        \
        if (clen > 1) {                          \
            /* hack: skip over multibyte chars - this may be wrong */ \
            format->tokens[nrOfTokens].sepLen += clen;  \
            p += clen;                           \
            continue;                            \
        }                                        \
        if (isalnum(*p)) break;                  \
        format->tokens[nrOfTokens].sepLen++;     \
        p++;                                     \
    }                                            \
    if (*p) {                                    \
        if (format->tokens[nrOfTokens].sepLen == 0) goto wrongSyntax; \
    }                                            \
    nrOfTokens++;                                \
    if (nrOfTokens == format->maxtokens) {       \
        format->tokens = (xsltNumberFormatToken *) Tcl_Realloc ((char *)format->tokens, sizeof (xsltNumberFormatToken) * format->maxtokens * 2);          \
        format->maxtokens *= 2;                  \
    }                                            \
    format->tokens[nrOfTokens].minlength = 1;    \
    continue;             

    while (*p) {
        if (*p == '0') {
            format->tokens[nrOfTokens].minlength++;
            p++;
            continue;
        }
        if (*p == '1') {
            format->tokens[nrOfTokens].type = latin_number;
            addSeperator;
        }
        if (*p == 'A') {
            if (isalnum(*(p+1))) goto wrongSyntax;
            format->tokens[nrOfTokens].type = latin_upper;
            addSeperator;
        }
        if (*p == 'a') {
            if (isalnum(*(p+1))) goto wrongSyntax;
            format->tokens[nrOfTokens].type = latin_lower;
            addSeperator;
        }
        if (*p == 'I') {
            if (isalnum(*(p+1))) goto wrongSyntax;
            format->tokens[nrOfTokens].type = roman_upper;
            addSeperator;
        }
        if (*p == 'i') {
            if (isalnum(*(p+1))) goto wrongSyntax;
            format->tokens[nrOfTokens].type = roman_lower;
            addSeperator;
        }
        format->tokens[nrOfTokens].type = latin_number;
        while (isalnum(*(p+1))) {
            p++;
        }
        addSeperator;
    }
    format->epilogStart = format->tokens[nrOfTokens-1].sepStart;
    format->tokens[nrOfTokens-1].sepStart = NULL;
    format->epilogLen = format->tokens[nrOfTokens-1].sepLen;
    format->tokens[nrOfTokens-1].sepLen = 0;
    return format;
    
 wrongSyntax:
    *errMsg = strdup("xsl:number: Wrong syntax in format attribute");
    return NULL;
}

/*----------------------------------------------------------------------------
|   formatValue
|
\---------------------------------------------------------------------------*/
static void formatValue (
    xsltNumberFormat *f,
    int              *useFormatToken,
    int               value,
    Tcl_DString      *str,
    char             *groupingSeparator,
    long              groupingSize,
    int               addSeperater
)
{
    int         len, fulllen, gslen, upper = 0, e, m, b, i, z, v;
    char        tmp[80], *pt;
    Tcl_DString tmp1;
    static struct { char *digit; char *ldigit; int value; } RomanDigit[] = {
          { "M" , "m" , 1000, },
          { "CM", "cm",  900, }, 
          { "D" , "d" ,  500, },
          { "CD", "cd",  400, },
          { "C" , "c" ,  100, },
          { "XC", "xc",   90, },
          { "L" , "l" ,   50, },
          { "XL", "xl",   40, },
          { "X" , "x" ,   10, },
          { "IX", "ix",    9, },
          { "V" , "v" ,    5, },
          { "IV", "iv",    4, },
          { "I" , "i" ,    1  }
    };

    switch (f->tokens[*useFormatToken].type) {
    case latin_number:
        sprintf (tmp, "%d", value);
        fulllen = len = strlen (tmp);
        if (f->tokens[*useFormatToken].minlength > fulllen) {
            fulllen = f->tokens[*useFormatToken].minlength;
        }
        if (groupingSeparator) {
            gslen = strlen (groupingSeparator);
            Tcl_DStringInit (&tmp1);
            if (len < f->tokens[*useFormatToken].minlength) {
                for (i = 0; i <  f->tokens[*useFormatToken].minlength - len; i++) {
                    Tcl_DStringAppend (&tmp1, "0", 1);
                }
            }
            Tcl_DStringAppend (&tmp1, tmp, len);
            pt = Tcl_DStringValue (&tmp1);
            len = Tcl_DStringLength (&tmp1);
            m = len % groupingSize;
            if (m) {
                Tcl_DStringAppend (str, pt, m);
                pt += m;
            }
            i = len - m;
            while (i) {
                if (i != len) {
                    Tcl_DStringAppend (str, groupingSeparator, gslen);
                }
                Tcl_DStringAppend (str, pt, groupingSize);
                pt += groupingSize;
                i -= groupingSize;
            }
            Tcl_DStringFree (&tmp1);
        } else {
            for (i = 0; i < fulllen - len; i++) {
                Tcl_DStringAppend (str, "0", 1);
            }
            Tcl_DStringAppend (str, tmp, len);
        }
        goto appendSeperator;
        break;

    case latin_upper:
        upper = 1;
        /* fall thru */
    case latin_lower:
        /* Home grown algorithm. (And I'm really not happy with it.)
           Please let rolf@pointsman.de know how to do this better /
           faster / more clever. */

        if (value == 0) {
            /* Hm, zero can't be expressed with letter sequences... 
               What to do? One of the several cases, not mentioned
               by the spec. */
            return;
        }
        e = 1;
        m = b = 26;
        while (value > m) {
            b *= 26;
            m += b;
            e++;
        }
        m -= b;
        value -= m;
        for (i = 0; i < e; i++) {
            b /= 26;
            z = value / b;
            value = value - z*b;
            if (i < e -1) {
                if (value == 0) {
                    value += b;
                } else {
                    z++;
                }
            }
            if (upper) {
                tmp[i] = 64+z;
            } else {
                tmp[i] = 96+z;
            }
        }
        tmp[i] = '\0';
        break;

    case roman_upper:
        upper = 1;
        /* fall thru */
    case roman_lower:
        /* Algorithm follows the idear of the converter
           at http://mini.net/cgi-bin/wikit/1749.html */

        /* Side note: There exists a rarely used roman notation
           to express figures up to a few millions. Does somebody
           really need this? */
               
        if (value > 3999) {
            /* fall back to latin numbers */
            sprintf (tmp, "%d", value);
            break;
        }
        if (value == 0) {
            /* what to do with zero??? */
            sprintf (tmp, "%d", 0);
            break;
        }
        v = 0;  tmp[0] = '\0';
        while (value > 0) {
            while (value >= RomanDigit[v].value) {
                if (upper) { strcat(tmp,  RomanDigit[v].digit);  }
                      else { strcat(tmp,  RomanDigit[v].ldigit); }
                value -= RomanDigit[v].value;
            }
            v++;
        }  
        break;

    default:
        sprintf (tmp, "%d", value);
        break;
    }
    len = strlen (tmp);
    Tcl_DStringAppend (str, tmp, len);
 appendSeperator:
    if (addSeperater) {
        if (f->tokens[*useFormatToken].sepStart) {
            Tcl_DStringAppend (str, f->tokens[*useFormatToken].sepStart,
                               f->tokens[*useFormatToken].sepLen);
            *useFormatToken += 1;
        } else {
            if (*useFormatToken > 0) {
                Tcl_DStringAppend (str, f->tokens[*useFormatToken-1].sepStart,
                                   f->tokens[*useFormatToken-1].sepLen);
            } else {
                /* insert default seperator '.' */
                Tcl_DStringAppend (str, ".", 1);
            }
        }
    }
    
    return;
}

/*----------------------------------------------------------------------------
|   xsltFormatNumber
|
\---------------------------------------------------------------------------*/
static int xsltFormatNumber (
    double            number,
    char            * formatStr,
    char           ** resultStr,
    int             * resultLen,
    char           ** errMsg
)
{
    char *p, prefix[80], suffix[80], s[240], n[80], f[80];
    int i, l, zl, g, nHash, nZero, fHash, fZero, gLen, isNeg;
      
    DBG(fprintf(stderr, "\nformatStr='%s' \n", formatStr);)
    p = formatStr;
    while (*p) {
        if (*p=='\'') {
            if (*(p+1)!='\0') p++;
        }
        if (*p==';') break;
        p++;
    }
    if ((number < 0.0) && (*p==';')) {
        p++;
        number = -1.0 * number;
    } else {
        p = formatStr;
    }
    i = 0;
    while (*p && (*p!='0') && (*p!='#') && (*p!='.') && (*p!=',')) {
        if (*p=='\'') {
            if (*(p+1)!='\0') p++;
        }
        if (i<79) { prefix[i++] = *p; }
        p++;
    }
    prefix[i] = '\0';
    nHash = nZero = fHash = fZero = 0;
    gLen = -2222;
    while (*p) {
             if (*p=='#') { nHash++; }
        else if (*p=='0') { nZero++; }
        else if (*p==',') { gLen=-1; }
        else break;
        p++; gLen++;
    }
    if (*p && (*p=='.')) {
        p++;
        while (*p && (*p=='0')) { p++; fZero++; }
        while (*p && (*p=='#')) { p++; fHash++; }
    }    
    i = 0;
    while (*p) {
        if (*p=='\'') {
            if (*(p+1)!='\0') p++;
        }
        if (i<79) { suffix[i++] = *p; }
        p++;
    }
    suffix[i] = '\0';
    i = number;

    DBG(fprintf(stderr,"normal part nZero=%d i=%d glen=%d\n", nZero, i, gLen);)

    /* fill in grouping char */
    if (gLen > 0) {
        if (i < 0.0) isNeg = 1;
        else isNeg = 0;
        sprintf(s,"%0*d", nZero, i);    
        l = strlen(s);
        /* if (l > (nHash+nZero)) { l = nHash+nZero; } */
        DBG(fprintf(stderr,"s='%s isNeg=%d'\n", s, isNeg);)
        zl = l + ((l-1-isNeg) / gLen);
        DBG(fprintf(stderr, "l=%d zl=%d \n", l, zl);)
        n[zl--] = '\0';
        p = s + strlen(s) -1;
        g = 0;
        while (zl>=0) {
            g++;
            n[zl--] = *p--;
            if ((g == gLen) && (zl>=1+isNeg)) {
                n[zl--] = ',';
                g = 0;
            }
        }
        DBG(fprintf(stderr,"s='%s' --> n='%s'\n", s, n);)
        
    } else {
        sprintf(n,"%0*d", nZero, i);
        DBG(fprintf(stderr,"n='%s'\n", n);)
    }

    DBG(fprintf(stderr, "number=%f Hash=%d fZero=%d \n", number, fHash, fZero);)
    if ((fHash+fZero) > 0) {
 
        /* format fraction part */
        if (number >= 0.0) {
            sprintf(f,"%0.*f", fZero+fHash, number -i);
        } else {
            sprintf(f,"%0.*f", fZero+fHash, -1.0 * (number -i) );
        }
        l = strlen(f);
        while (l>0 && fHash>0) {   /* strip not need 0's */
            if (f[l-1] == '0') {
                f[l-1]='\0'; l--; fHash--;
            } else {
                break;
            }
        }
        DBG(fprintf(stderr, "f='%s'\n", f);)
        sprintf(s,"%s%s.%s%s", prefix, n, &(f[2]), suffix);
    } else {
        sprintf(s,"%s%s%s", prefix, n, suffix);
    }
    DBG(fprintf(stderr, "returning s='%s' \n\n", s);)
    *resultStr = strdup(s);
    *resultLen = strlen(s);
    return 0;
}

static int buildKeyInfoForDoc (
    xsltSubDoc     *sd,
    char           *keyId,
    Tcl_HashTable  *keyInfos,
    xsltState      *xs,
    char          **errMsg
)
{
    int             hnew, rc, docOrder, i;
    char           *useValue;
    domNode        *node;
    xpathResultSet  rs, context;
    Tcl_HashTable  *valueTable;
    Tcl_HashEntry  *h;
    xsltKeyInfo    *kinfo, *kinfoStart;
    xsltKeyValues  *keyValues;
    xsltKeyValue   *keyValue;
    
    h = Tcl_FindHashEntry (keyInfos, keyId);
    /* key must exist, this is already checked */
    kinfoStart = (xsltKeyInfo *) Tcl_GetHashValue (h);
    
    /* this must be a new entry, no check for hnew==1 needed */
    h = Tcl_CreateHashEntry (&(sd->keyData), keyId, &hnew);
    valueTable = (Tcl_HashTable *) Tcl_Alloc (sizeof (Tcl_HashTable));
    Tcl_InitHashTable (valueTable, TCL_STRING_KEYS);
    Tcl_SetHashValue (h, valueTable);
    
    node = sd->doc->rootNode;
    while (node) {
        kinfo = kinfoStart;
        while (kinfo) {
            rc = xpathMatches (kinfo->matchAst, kinfo->node, node, &(xs->cbs),
                               errMsg);
            if (rc < 0) {
                TRACE1("xpathMatches had errors '%s' \n", *errMsg);
                return rc;
            }
            if (rc > 0) {
                TRACE("found match for key !\n");
                xpathRSInit (&rs);
                xpathRSInit (&context);
                rsAddNode (&context, node);
                DBG(printXML(node, 0, 2);)
                docOrder = 1;
                rc = xpathEvalSteps (kinfo->useAst, &context, node, kinfo->node,
                                     0, &docOrder, &(xs->cbs), &rs, errMsg);
                if (rc != XPATH_OK) {
                    xpathRSFree (&rs);
                    xpathRSFree (&context);
                    return rc;
                }
                DBG(rsPrint(&rs));
                if (rs.type == xNodeSetResult) {
                    for (i = 0; i < rs.nr_nodes; i++) {
                        useValue = xpathFuncStringForNode (rs.nodes[i]);
                        TRACE1("use value = '%s'\n", useValue);
                        keyValue = 
                            (xsltKeyValue *) Tcl_Alloc (sizeof(xsltKeyValue));
                        keyValue->node = node;
                        keyValue->next = NULL;
                        h = Tcl_CreateHashEntry (valueTable, useValue, &hnew);
                        if (hnew) {
                            keyValues = 
                                (xsltKeyValues*)Tcl_Alloc(sizeof (xsltKeyValues));
                            Tcl_SetHashValue (h, keyValues);
                            keyValues->value = keyValue;
                        } else {
                            keyValues = (xsltKeyValues *) Tcl_GetHashValue (h);
                            keyValues->lastvalue->next = keyValue;
                        }
                        keyValues->lastvalue = keyValue;
                        free (useValue);
                    }
                }
                else {
                    useValue = xpathFuncString (&rs);
                    TRACE1("use value = '%s'\n", useValue);
                    keyValue = (xsltKeyValue *) Tcl_Alloc (sizeof(xsltKeyValue));
                    keyValue->node = node;
                    keyValue->next = NULL;
                    h = Tcl_CreateHashEntry (valueTable, useValue, &hnew);
                    if (hnew) {
                        keyValues = 
                            (xsltKeyValues*)Tcl_Alloc (sizeof (xsltKeyValues));
                        Tcl_SetHashValue (h, keyValues);
                        keyValues->value = keyValue;
                    } else {
                        keyValues = (xsltKeyValues *) Tcl_GetHashValue (h);
                        keyValues->lastvalue->next = keyValue;
                    }
                    keyValues->lastvalue = keyValue;
                    free (useValue);
                }
                xpathRSFree( &context );
                xpathRSFree( &rs );                   
            }
            kinfo = kinfo->next;
        }
        if ((node->nodeType == ELEMENT_NODE) && (node->firstChild)) {
            node = node->firstChild;
            continue;
        }
        if (node->nextSibling) {
            node = node->nextSibling;
            continue;
        }
        while ( node->parentNode &&
                (node->parentNode->nextSibling == NULL) ) {
            node = node->parentNode;
        }
        if (node->parentNode) {
            node = node->parentNode->nextSibling;
        } else {
            break;
        }
    }
    return 0;
}


/*  #define GETNUMBERNODE(x) (x)->nodeType == ATTRIBUTE_NODE ? ((domAttrNode * (x))->parentNode : (x) */
/*----------------------------------------------------------------------------
|   sortNodeSetByNodeNumber
|
\---------------------------------------------------------------------------*/
static void sortNodeSetByNodeNumber(
    domNode *nodes[], 
    int      n
)
{
    domNode *tmp;
    int i, j, ln, rn;

    /* TODO: ATTRIBUTE_NODE? */
    while (n > 1) {
        tmp = nodes[0]; nodes[0] = nodes[n/2]; nodes[n/2] = tmp;
        for (i = 0, j = n; ; ) {
            do --j; while (nodes[j]->nodeNumber > nodes[0]->nodeNumber);
            do ++i; while (i < j && nodes[0]->nodeNumber > nodes[i]->nodeNumber);
            if (i >= j)  break;
            tmp = nodes[i]; nodes[i] = nodes[j]; nodes[j] = tmp;
        }
        tmp = nodes[j]; nodes[j] = nodes[0]; nodes[0] = tmp;
        ln = j;
        rn = n - ++j;
        if (ln < rn) {
            sortNodeSetByNodeNumber(nodes, ln);
            nodes += j;
            n = rn;
        } else {
            sortNodeSetByNodeNumber(&(nodes[j]), rn);
            n = ln;
        }
    }
}
        
/*----------------------------------------------------------------------------
|   sortByDocOrder
|
\---------------------------------------------------------------------------*/
void sortByDocOrder (
    xpathResultSet  * rs
)
{
    if (rs->type != xNodeSetResult) return;
    sortNodeSetByNodeNumber(rs->nodes, rs->nr_nodes);
}

/*----------------------------------------------------------------------------
|   StripXMLSpace
|
\---------------------------------------------------------------------------*/
void StripXMLSpace (
    xsltState  * xs,
    domNode    * node
)
{
    domNode       *child, *newChild, *parent;
    int            i, len, onlySpace, strip;
    char          *p, *nsname, *localName, prefix[MAX_PREFIX_LEN];
    double        *f, stripPrecedence, stripPrio;
    Tcl_HashEntry *h;
    Tcl_DString    dStr;

    
    if (node->nodeType == TEXT_NODE) {
        p = ((domTextNode*)node)->nodeValue;
        len = ((domTextNode*)node)->valueLength;
        onlySpace = 1;
        for (i=0; i<len; i++) {           
            if ((*p!=' ') && (*p!='\n') && (*p!='\r') && (*p!='\t')) {
                onlySpace = 0;
                break;
            }
            p++;
        }
        if (onlySpace) {
            parent = node->parentNode;
            while (parent) {                
                p = getAttr(parent,"xml:space", a_space);
                if (p!=NULL) {
                    if (strcmp(p,"preserve")==0) return;
                    if (strcmp(p,"default")==0)  break;
                }
                parent = parent->parentNode;
            }
            DBG(fprintf(stderr, "removing %d(len %d) under '%s' \n", node->nodeNumber, len, node->parentNode->nodeName);)
                domDeleteNode (node, NULL, NULL);
        }
    } else 
    if (node->nodeType == ELEMENT_NODE) {
        if (node->firstChild == NULL) return;
        strip = 0;
        stripPrecedence = 0.0;
        stripPrio       = -0.5;
        if (xs->stripInfo.wildcardPrec > 0) {
            strip = 1;
            stripPrecedence = xs->stripInfo.wildcardPrec;
        }
        nsname = domNamespaceURI (node);
        if (nsname) {
            h = Tcl_FindHashEntry (&(xs->stripInfo.NSWildcards), nsname);
            if (h) {
                strip = 1;
                f = (double *)Tcl_GetHashValue (h);
                if (*f >= stripPrecedence) {
                    stripPrecedence = *f;
                    stripPrio = -0.25;
                }
            }
            domSplitQName (node->nodeName, prefix, &localName);
            Tcl_DStringInit (&dStr);
            Tcl_DStringAppend (&dStr, nsname, -1);
            Tcl_DStringAppend (&dStr, localName, -1);
            h = Tcl_FindHashEntry (&(xs->stripInfo.FQNames),
                                   Tcl_DStringValue (&dStr));
            if (h) {
                strip = 1;
                f = (double *)Tcl_GetHashValue (h);
                if (*f >= stripPrecedence) {
                    stripPrecedence = *f;
                    stripPrio = 0.0;
                }
            }
        } else {
            h = Tcl_FindHashEntry (&(xs->stripInfo.NCNames), node->nodeName);
            if (h) {
                strip = 1;
                f = (double *)Tcl_GetHashValue (h);
                if (*f >= stripPrecedence) {
                    stripPrecedence = *f;
                    stripPrio = 0.0;
                }
            }
        }
        if (strip && xs->preserveInfo.hasData) {
            if (xs->preserveInfo.wildcardPrec > stripPrecedence) {
                strip = 0;
            } else {
                if (nsname) {
                    h = Tcl_FindHashEntry (&(xs->preserveInfo.FQNames),
                                           Tcl_DStringValue (&dStr));
                    if (h) {
                        f = (double *)Tcl_GetHashValue (h);
                        if (*f > stripPrecedence) {
                            strip = 0;
                        } else
                        if (*f == stripPrecedence && stripPrio < 0.0) {
                            strip = 0;
                        }
                    }
                    if (strip) {
                        h = Tcl_FindHashEntry (&(xs->preserveInfo.NSWildcards),
                                               nsname);
                        if (h) {
                            f = (double *)Tcl_GetHashValue (h);
                            if (*f > stripPrecedence) {
                                strip = 0;
                            } else 
                            if (*f == stripPrecedence && stripPrio < -0.25) {
                                strip = 0;
                            }
                        }
                    }
                } else {
                    h = Tcl_FindHashEntry (&(xs->preserveInfo.NCNames),
                                           node->nodeName);
                    if (h) {
                        f = (double *)Tcl_GetHashValue (h);
                        if (*f > stripPrecedence) {
                            strip = 0;
                        } else 
                        if (*f == stripPrecedence && stripPrio < 0.0) {
                            strip = 0;
                        }
                    }
                }
            }
        }
        if (nsname) Tcl_DStringFree (&dStr);
        if (strip) {
            child = node->firstChild;
            while (child) {
                newChild = child->nextSibling;
                StripXMLSpace (xs, child);
                child = newChild;
            }
        } else {
            child = node->firstChild;
            while (child) {
                if (child->nodeType == ELEMENT_NODE) {
                    StripXMLSpace (xs, child);
                }
                child = child->nextSibling;
            }
        }
    }
}

/*----------------------------------------------------------------------------
|   xsltXPathFuncs
|
\---------------------------------------------------------------------------*/
static int xsltXPathFuncs (
    void            * clientData,
    char            * funcName,
    domNode         * ctxNode,
    int               ctxPos,
    xpathResultSet  * ctx,
    int               argc,
    xpathResultSets * argv,
    xpathResultSet  * result,
    char           ** errMsg
)
{
    xsltState     * xs = clientData;
    char          * keyId, *filterValue, *str, *baseURI, tmp[5];
    int             rc, i, len, NaN, freeStr;
    double          n;
    xsltKeyValue  * value;
    xsltKeyValues * keyValues;
    Tcl_HashEntry * h;
    Tcl_HashTable * docKeyData;
    xsltSubDoc    * sdoc;

    /* fprintf(stderr,"xsltXPathFuncs funcName='%s'\n",funcName); */
 
    if (strcmp(funcName, "key")==0) {
        /*--------------------------------------------------------------------
        |   'key' function
        \-------------------------------------------------------------------*/
        DBG(fprintf(stderr,"xslt key function called!\n");)
        if (argc != 2) {
            *errMsg = strdup("key() needs two arguments!");
            return 1;
        }
        /* check, if there is a key definition with the given name */
        keyId = xpathFuncString(argv[0]);
        TRACE1("keyId='%s' \n", keyId);
        h = Tcl_FindHashEntry (&xs->keyInfos, keyId);
        if (!h) {
            *errMsg = strdup("Unkown key in key() function call!");
            free (keyId);
            return 1;
        }

        /* find the doc, the context node belongs to */
        sdoc = xs->subDocs;
        while (sdoc) {
            if (sdoc->doc == ctxNode->ownerDocument) break;
            sdoc = sdoc->next;
        }
        DBG(if (!sdoc) fprintf (stderr, "key() function: ctxNode doesn't belong to a doc out of subDocs!!! This could not happen!. ERROR\n");
            else (fprintf (stderr, "key() function: ctxNode belongs to doc %s\n", sdoc->baseURI));)
        
        h = Tcl_FindHashEntry (&(sdoc->keyData), keyId);
        if (!h) {
            if (buildKeyInfoForDoc(sdoc,keyId,&(xs->keyInfos),xs,errMsg)<0) {
                free (keyId);
                return 1;
            }
            h = Tcl_FindHashEntry (&(sdoc->keyData), keyId);
        }
        free (keyId);
        
        docKeyData = (Tcl_HashTable *) Tcl_GetHashValue (h);

        if (argv[1]->type == xNodeSetResult) {
            for (i = 0; i < argv[1]->nr_nodes; i++) {
                filterValue = xpathFuncStringForNode (argv[1]->nodes[i]);
                TRACE1("filterValue='%s' \n", filterValue); 
                h = Tcl_FindHashEntry (docKeyData, filterValue);
                if (h) {
                    keyValues = (xsltKeyValues *) Tcl_GetHashValue (h);
                    value = keyValues->value;
                    while (value) {
                        rsAddNode(result, value->node);
                        value = value->next;
                    }
                }
                free (filterValue);
            }
            sortByDocOrder (result);
            return 0;
        } else {
           filterValue = xpathFuncString(argv[1]);
           TRACE1("filterValue='%s' \n", filterValue); 
           h = Tcl_FindHashEntry (docKeyData, filterValue);
           if (h) {
               keyValues = (xsltKeyValues *) Tcl_GetHashValue (h);
               value = keyValues->value;
               while (value) {
                   rsAddNode(result, value->node);
                   value = value->next;
               }
           }
           free (filterValue);
           return 0;
        }
    } else
    if (strcmp(funcName, "current")==0) {
        /*--------------------------------------------------------------------
        |   'current' function
        \-------------------------------------------------------------------*/
        DBG(fprintf(stderr, "xsltXPathFuncs 'current' = '%d' \n", xs->current->nodeNumber);)
        rsAddNode(result, xs->current);
        return 0;
    } else 
    if (strcmp (funcName, "format-number")==0) {
        /*--------------------------------------------------------------------
        |   'format-number' function
        \-------------------------------------------------------------------*/
        DBG(fprintf(stderr, "before format-number argc=%d \n", argc);)
        if (argc == 3) {
            *errMsg = strdup("format-number with decimal-format (third paramater) is not yet supported!");
            return 1;  
        }
        if (argc != 2) {
            *errMsg = strdup("format-number: wrong # parameters: format-number(number, string, ?string?)!");
            return 1;  
        }
        NaN = 0;
        n   = xpathFuncNumber (argv[0], &NaN);
        if (NaN) {
            sprintf(tmp, "%f", n);
            if (strcmp(tmp,"nan")==0)  rsSetString (result, "NaN");
            else if (strcmp(tmp,"inf")==0)  rsSetString (result, "Infinity");
            else if (strcmp(tmp,"-inf")==0) rsSetString (result, "-Infinity");
            else *errMsg = strdup("format-number: unrecognized NaN!!! - Please report!");
            return 0;
        }
        str = xpathFuncString (argv[1]);
        DBG(fprintf(stderr, "1 str='%s' \n", str);)
        result->type = StringResult;
        rc = xsltFormatNumber(n,str,&(result->string), &(result->string_len), errMsg);
        CHECK_RC;
        free (str);
        return 0;
        DBG(fprintf(stderr, "after format-number \n");)
    } else 
    if (strcmp (funcName, "document")==0) {
        /*--------------------------------------------------------------------
        |   'document' function
        \-------------------------------------------------------------------*/
        DBG(fprintf(stderr, "xsltXPathFuncs 'document' \n");)
        if (argc == 1) {
            if (argv[0]->type == xNodeSetResult) {
                for (i = 0; i < argv[0]->nr_nodes; i++) {
                    freeStr = 0;
                    if (argv[0]->nodes[i]->nodeType == ATTRIBUTE_NODE) {
                        str = ((domAttrNode*)argv[0]->nodes[i])->nodeValue;
                        baseURI = findBaseURI (((domAttrNode*)argv[0]->nodes[i])->parentNode);
                    } else {
                        str = xpathGetTextValue (argv[0]->nodes[i], &len);
                        freeStr = 1;
                        baseURI = findBaseURI (argv[0]->nodes[i]);
                    }
                    /* the case document('') */
                    if (*str == '\0') {
                        if (freeStr) {
                            free (str);
                            freeStr = 0;
                        }
                        str = baseURI;
                    }
                    if (xsltAddExternalDocument(xs, baseURI, str, 
                                                result, errMsg) < 0) {
                        if (freeStr) free (str);
                        return -1;
                    }
                    if (xs->stripInfo.hasData) {
                        StripXMLSpace (xs, xs->subDocs->doc->documentElement);
                    }
                    if (freeStr) free (str);
                }
            }  
            else {
                freeStr = 1;
                str = xpathFuncString (argv[0]);
                /* TODO. Hack.This could be wrong. document() has to
                   use the baseURI of the stylesheet node with the
                   expression with the document() call. This can
                   clearly be another URI than the URI of the
                   currentTplRule. OK, the typical user won't spread a
                   template over different entities, but even then
                   there is the case of call-template, which doesn't
                   change currentTplRule. At the end there isn't
                   another way as to store the current xslt node in xs
                   (not needed for path expressions). */
                if (xs->currentXSLTNode) {
                    baseURI = findBaseURI (xs->currentXSLTNode);
                } else 
                if (xs->currentTplRule) {
                    baseURI = findBaseURI (xs->currentTplRule->content);
                } else {
                    baseURI = findBaseURI (xs->xsltDoc->rootNode);
                }
                if (*str == '\0') {
                    free (str);
                    freeStr = 0;
                    str = baseURI;
                }
                DBG (fprintf (stderr, "document() call, with 1 string arg = '%s'\n", str);)
                if (xsltAddExternalDocument(xs, baseURI, str, 
                                            result, errMsg) < 0) {
                    if (freeStr) free (str);
                    return -1;
                }
                if (xs->stripInfo.hasData) {
                    StripXMLSpace (xs, xs->subDocs->doc->documentElement);
                }
                if (freeStr) free (str);
            }
        } else
        if (argc == 2) {
            if (argv[1]->type != xNodeSetResult) {
                *errMsg = strdup("second arg of document() has to be a nodeset!");
            }
            if (argv[1]->nodes[0]->nodeType == ATTRIBUTE_NODE) {
                baseURI = findBaseURI (((domAttrNode*)argv[1]->nodes[0])->parentNode);
            } else {
                baseURI = findBaseURI (argv[1]->nodes[0]);
            }
            if (argv[0]->type == xNodeSetResult) {
                for (i = 0; i < argv[0]->nr_nodes; i++) {
                    freeStr = 0;
                    if (argv[0]->nodes[i]->nodeType == ATTRIBUTE_NODE) {
                        str = ((domAttrNode*)argv[0]->nodes[i])->nodeValue;
                    } else {
                        str = xpathGetTextValue (argv[0]->nodes[i], &len);
                        freeStr = 1;
                    }
                    if (*str == '\0') {
                        free (str);
                        freeStr = 0;
                        str = baseURI;
                    }
                    if (xsltAddExternalDocument(xs, baseURI, str, 
                                                result, errMsg) < 0) {
                        if (freeStr) free (str);
                        return -1;
                    }
                    if (xs->stripInfo.hasData) {
                        StripXMLSpace (xs, xs->subDocs->doc->documentElement);
                    }
                    if (freeStr) free (str);
                }
            } else {
                str = xpathFuncString (argv[0]);
                if (xsltAddExternalDocument(xs, baseURI, str, 
                                            result, errMsg) < 0) {
                    free (str);
                    return -1;
                }
                if (xs->stripInfo.hasData) {
                    StripXMLSpace (xs, xs->subDocs->doc->documentElement);
                }
                free (str);
            }
        } else {
            *errMsg = strdup("wrong # of args in document() call!");
            return 1;
        }
        return 0;
     } else {
        /* chain back to original callback */
        if (xs->orig_funcCB) {
            return (xs->orig_funcCB)(xs->orig_funcClientData, funcName,
                                     ctxNode, ctxPos, ctx, argc, argv,
                                     result, errMsg);
        }
    }
    return 0;    
}



/*----------------------------------------------------------------------------
|   evalXPath
|
\---------------------------------------------------------------------------*/
static int evalXPath (
    xsltState       * xs,
    xpathResultSet  * context,
    domNode         * currentNode,
    int               currentPos,
    char            * xpath,    
    xpathResultSet  * rs,
    char           ** errMsg
)
{
    int rc, hnew, docOrder = 1;
    ast t;
    Tcl_HashEntry *h;

    h = Tcl_CreateHashEntry (&(xs->xpaths), xpath, &hnew);
    if (!hnew) {
        t = (ast)Tcl_GetHashValue(h);
    } else {     
        rc = xpathParse(xpath, errMsg, &t, 0);
        CHECK_RC;
        Tcl_SetHashValue(h, t);
    }
    xpathRSInit( rs );

    DBG(fprintf (stderr, "evalXPath evaluating xpath:\n");)
    DBG(printAst(3,t);)
    rc = xpathEvalSteps( t, context, currentNode, xs->currentXSLTNode, currentPos,
                         &docOrder, &(xs->cbs), rs, errMsg); 
    if (rc != XPATH_OK) {
        xpathRSFree( rs );
    } 

    return rc;
}


/*----------------------------------------------------------------------------
|   nodeGreater
|
\---------------------------------------------------------------------------*/
static int nodeGreater (
    int         typeText,
    int         asc,
    int         upperFirst,
    char      * strA,
    char      * strB,
    double      realA,
    double      realB,
    int       * greater
)
{
    int             rc;
#if TclOnly8Bits == 0
    char           *strAptr, *strBptr;
    int             lenA, lenB, len;
    Tcl_UniChar     unicharA, unicharB;
#endif

    *greater = 0;

    if (typeText) {

#if TclOnly8Bits
        /* TODO: this only works for 7 bit ASCII */
        rc = STRCASECMP(strA, strB);
        if (rc == 0) {
            rc = strcmp (strA, strB);
            if (!upperFirst) {
                rc *= -1;
            }
        }
DBG(   fprintf(stderr, "nodeGreater %d <-- strA='%s' strB='%s'\n", rc, strA, strB);)
#else    
        lenA = Tcl_NumUtfChars (strA, -1);
        lenB = Tcl_NumUtfChars (strB, -1);
        len = (lenA < lenB ? lenA : lenB);
        rc = Tcl_UtfNcasecmp (strA, strB, len);
        if (rc == 0) {
            if (lenA > lenB) {
                rc = 1;
            } else if (lenA < lenB) {
                rc = -1;
            }
        }
        if (rc == 0) {
            strAptr = strA;
            strBptr = strB;
            while (len-- > 0) {
                strAptr += Tcl_UtfToUniChar(strAptr, &unicharA);
                strBptr += Tcl_UtfToUniChar(strBptr, &unicharB);
                if (unicharA != unicharB) {
                    rc = unicharA - unicharB;
                    break;
                }
            }
            if (!upperFirst) {
                rc *= -1;
            }
        }
#endif
        if (asc) *greater = (rc > 0);
            else *greater = (rc < 0);

    } else {
DBG(   fprintf(stderr, "nodeGreater  realA='%f' realB='%f'\n",realA, realB);)
        if (isnan (realA) || isnan (realB)) {
            if (asc) {
                if (isnan (realA) && !isnan (realB)) {
                    *greater = 0;
                } else {
                    if (isnan (realB) && !isnan (realA)) *greater = 1;
                }
            } else {
                if (isnan (realA) && !isnan(realB)) {
                    *greater = 1;
                } else {
                    if (isnan (realB) && !isnan(realA)) *greater = 0;
                }
            }
        } else {
            if (asc) *greater = (realA > realB); 
            else *greater = (realA < realB);        
        } 
    }
    return 0;
}

static int fastMergeSort (
    int         txt,
    int         asc,
    int         upperFirst,
    domNode   * a[],
    int       * posa,
    domNode   * b[],
    int       * posb,
    char     ** vs,
    double    * vd,
    char     ** vstmp,
    double    * vdtmp,
    int         size,
    char     ** errMsg
) {
    domNode *tmp;
    int tmpPos, lptr, rptr, middle, i, j, gt, rc;
    char    *tmpVs;
    double   tmpVd;
    
    if (size < 10) {
          /* use simple and fast insertion for small sizes ! */
        for (i = 1; i < size; i++) {
            tmp    = a    [i];
            tmpPos = posa [i];
            tmpVs  = vs   [i];
            tmpVd  = vd   [i];
            j = i; 
            if (j>0) {
                rc = nodeGreater(txt, asc, upperFirst, vs[j-1], tmpVs,
                                   vd[j-1], tmpVd, &gt);
                CHECK_RC;
            }            
            while ( j > 0 && gt) {
                a   [j] = a   [j-1];
                posa[j] = posa[j-1];
                vs  [j] = vs  [j-1];
                vd  [j] = vd  [j-1];
                j--;
                if (j>0) {
                    rc = nodeGreater(txt, asc, upperFirst, vs[j-1], tmpVs,
                                       vd[j-1], tmpVd, &gt);
                    CHECK_RC;
                }
            }
            a   [j] = tmp;
            posa[j] = tmpPos;
            vs  [j] = tmpVs;
            vd  [j] = tmpVd;
        }
        return 0;
    }
    middle = size/2;
 
    rc = fastMergeSort(txt, asc, upperFirst, a, posa, b, posb, vs, vd,
                         vstmp, vdtmp, middle, errMsg);
    CHECK_RC;
    rc = fastMergeSort(txt, asc, upperFirst, a+middle, posa+middle, b+middle,
                         posb+middle, vs+middle, vd+middle, vstmp+middle,
                         vdtmp+middle, size-middle, errMsg);
    CHECK_RC;
 
    lptr = 0;
    rptr = middle;
 
    for (i = 0; i < size; i++) {
        if (lptr == middle) {
            b    [i] = a   [rptr  ];
            posb [i] = posa[rptr  ];
            vstmp[i] = vs  [rptr  ];
            vdtmp[i] = vd  [rptr++];
        } else if (rptr < size) {
            rc = nodeGreater(txt, asc, upperFirst, vs[lptr], vs[rptr],
                             vd[lptr], vd[rptr], &gt); 
            if (gt) {
                b    [i] = a   [rptr  ];
                posb [i] = posa[rptr  ];
                vstmp[i] = vs  [rptr  ];
                vdtmp[i] = vd  [rptr++];
            } else {
                b    [i] = a   [lptr  ];
                posb [i] = posa[lptr  ];
                vstmp[i] = vs  [lptr  ];
                vdtmp[i] = vd  [lptr++];
            }
        } else {
            b    [i] = a   [lptr  ];
            posb [i] = posa[lptr  ];
            vstmp[i] = vs  [lptr  ];
            vdtmp[i] = vd  [lptr++];
        } 
    } 
    memcpy(a,    b,     size*sizeof(domNode*));
    memcpy(posa, posb,  size*sizeof(int*));
    memcpy(vs,   vstmp, size*sizeof(char*));
    memcpy(vd,   vdtmp, size*sizeof(double));
    return 0;
}

static int sortNodeSetFastMerge(
    xsltState * xs,
    int         txt,
    int         asc,
    int         upperFirst,
    domNode   * nodes[], 
    int         n,
    char     ** vs,
    double    * vd,
    int       * pos,
    char     ** errMsg
)
{
    domNode **b;
    int      *posb;
    char    **vstmp;
    double   *vdtmp;
    int       rc;

    b = (domNode **) malloc( n * sizeof(domNode *) );
    if (b == NULL) {
        perror("malloc in sortNodeSetMergeSort");
        exit(1);
    }
    posb = (int *) malloc( n * sizeof(int) );
    if (posb == NULL) {
        perror("malloc in sortNodeSetMergeSort");
        exit(1);
    }
    vstmp = (char **) malloc (sizeof (char *) * n);
    vdtmp = (double *)malloc (sizeof (double) * n);
    
    rc = fastMergeSort(txt, asc, upperFirst, nodes, pos, b, posb, vs, vd, 
                         vstmp, vdtmp, n, errMsg);
    free (posb);
    free (b);
    free (vstmp);
    free (vdtmp);
    CHECK_RC;
    return 0;
}

/*----------------------------------------------------------------------------
|   xsltSetVar
|
\---------------------------------------------------------------------------*/
static int xsltSetVar (
    xsltState       * xs,
    int               forNextLevel,
    char            * variableName,
    xpathResultSet  * context,
    domNode         * node,
    int               currentPos,
    char            * select,
    domNode         * actionNodeChild,
    int               forTopLevel,
    char           ** errMsg
)
{
    xsltVariable   * var;
    int              rc;
    xpathResultSet   rs;
    xsltVarFrame    *tmpFrame = NULL;
    domNode         *fragmentNode, *savedLastNode;
        

    TRACE1("xsltSetVar variableName='%s' \n", variableName);
    if (select!=NULL) {
        if (forNextLevel) {
            /* hide new variable frame */
            xs->varFrames = &(xs->varFramesStack[xs->varFramesStackPtr - 1]);
        }
        TRACE2("xsltSetVar variableName='%s' select='%s'\n", variableName, select);
        xs->current = node;    
        rc = evalXPath (xs, context, node, currentPos, select, &rs, errMsg);
        CHECK_RC;
        if (forNextLevel) {
            /* show new variable frame again */
            xs->varFrames = &(xs->varFramesStack[xs->varFramesStackPtr]);
        }
    } else {
        if (!actionNodeChild) {
            xpathRSInit (&rs);
            rsSetString (&rs, "");
        } else {
            fragmentNode = domNewElementNode(xs->resultDoc, "(fragment)", 
                                             ELEMENT_NODE);
            savedLastNode = xs->lastNode;
            xs->lastNode = fragmentNode;
            /* process the children as well */
            xsltPushVarFrame (xs);
            rc = ExecActions(xs, context, node, currentPos, actionNodeChild, 
                             errMsg);
            xsltPopVarFrame (xs);
            CHECK_RC;
            xpathRSInit(&rs);
            rsAddNode(&rs, fragmentNode);
            xs->lastNode = savedLastNode;
        }
    }
    if (forTopLevel) {
        tmpFrame = xs->varFramesStack;
    } else {
        tmpFrame = xs->varFrames;
    }
    
    xs->varStackPtr++;
    if (xs->varStackPtr >= xs->varStackLen) {
        xs->varStack = (xsltVariable *) realloc (xs->varStack, 
                                                 sizeof (xsltVariable)
                                                 * 2 * xs->varStackLen);
        xs->varStackLen *= 2;
    }
    var = &(xs->varStack[xs->varStackPtr]);
    if (tmpFrame->varStartIndex == -1) {
        tmpFrame->varStartIndex = xs->varStackPtr;
    }
    tmpFrame->nrOfVars++;
    var->name = variableName;
    tmpFrame->polluted = 1;
    var->node  = node;
    var->rs     = rs;
    DBG(rsPrint(&(var->rs)));
    return 0;
}


/*----------------------------------------------------------------------------
|   xsltVarExists
|
\---------------------------------------------------------------------------*/
static int xsltVarExists (
    xsltState  * xs,
    char       * variableName
)
{
    int  i, found = 0;

    if (!xs->varFrames || !xs->varFrames->nrOfVars) return 0;
    
    for (i = xs->varFrames->varStartIndex;
         i < xs->varFrames->varStartIndex + xs->varFrames->nrOfVars;
         i++) {
        if (strcmp((&xs->varStack[i])->name, variableName)==0) {
            found = 1;
            break; /* found the variable */
        }
    }
    if (found) return 1;
    return 0;
}


/*----------------------------------------------------------------------------
|   xsltGetVar
|
\---------------------------------------------------------------------------*/
static int xsltGetVar (
    void           * clientData,
    char           * variableName,
    xpathResultSet * result,
    char           **errMsg
)
{
    xsltState        *xs = clientData;
    xsltVarFrame     *frame;
    xsltVariable     *var;
    int               rc, d, i, j; 
    char             *select;
    Tcl_HashEntry    *h;
    xsltTopLevelVar  *topLevelVar;
    xsltVarInProcess *varInProcess, thisVarInProcess;
    xpathResultSet    nodeList;
    domNode          *savedCurrentXSLTNode;
    Tcl_DString       dErrMsg;
    
    TRACE1("xsltGetVar variableName='%s' \n", variableName);
    
    d = 0;
    if ((xs->varFramesStackPtr > -1) && (&(xs->varFramesStack[xs->varFramesStackPtr]) != xs->varFrames)) {
        d = 1;
    }
    for (i = xs->varFramesStackPtr - d; i >= 0; i--) {
        frame = &(xs->varFramesStack[i]);
        if (!frame->nrOfVars) continue;
        for (j = frame->varStartIndex;
             j < frame->varStartIndex + frame->nrOfVars;
             j++) {
            var = &xs->varStack[j];
            /*TRACE2("is it var %d:'%s' ? \n", d, var->name);*/
            if (strcmp(var->name, variableName)==0) {
                TRACE1("xsltGetVar '%s':\n", variableName);
                DBG(rsPrint(&(var->rs)));
                rsCopy(result, &(var->rs) );
                return XPATH_OK;
            }
        }
    }
    if (xs->varsInProcess) {
        h = Tcl_FindHashEntry (&xs->topLevelVars, variableName);
        if (h) {
            topLevelVar = (xsltTopLevelVar *) Tcl_GetHashValue (h);
            /* check for circular definitions */
            varInProcess = xs->varsInProcess;
            while (varInProcess) {
                if (strcmp(varInProcess->name, variableName)==0) {
                    reportError (topLevelVar->node,
                                 "circular top level variabale definition detected",
                                 errMsg);
                    return XPATH_EVAL_ERR;
                }
                varInProcess = varInProcess->next;
            }
            thisVarInProcess.name = variableName;
            thisVarInProcess.next = xs->varsInProcess;
            xs->varsInProcess = &thisVarInProcess;

            xpathRSInit( &nodeList );
            rsAddNode( &nodeList, xs->xmlRootNode); 
            savedCurrentXSLTNode = xs->currentXSLTNode;
            xs->currentXSLTNode = topLevelVar->node;
            select = getAttr (topLevelVar->node, "select", a_select);
            if (select) {
                rc = xsltSetVar(xs, 0, variableName, &nodeList, 
                                xs->xmlRootNode, 0, 
                                select, NULL, 1, errMsg);
            } else {
                rc = xsltSetVar(xs, 0, variableName, &nodeList, 
                                xs->xmlRootNode, 0,
                                NULL, topLevelVar->node->firstChild, 1, errMsg);
            }
            xpathRSFree ( &nodeList );
            CHECK_RC;
            rc = xsltGetVar (xs, variableName, result, errMsg);
            CHECK_RC;
            /* remove var out of the varsInProcess list. Should be first
               in the list, shouldn't it? */
            varInProcess = xs->varsInProcess;
            if (varInProcess != &thisVarInProcess) {
                fprintf (stderr, "error in top level vars processing\n");
                exit(1);
            }
            xs->varsInProcess = varInProcess->next;
            xs->currentXSLTNode = savedCurrentXSLTNode;
            return XPATH_OK;
        }
    }
    Tcl_DStringInit (&dErrMsg);
    Tcl_DStringAppend (&dErrMsg, "Variable \"", -1);
    Tcl_DStringAppend (&dErrMsg, variableName, -1);
    Tcl_DStringAppend (&dErrMsg, "\" has not been declared.", -1);
    reportError (xs->currentXSLTNode, Tcl_DStringValue (&dErrMsg), errMsg);
    Tcl_DStringFree (&dErrMsg);
    return XPATH_EVAL_ERR;
}



/*----------------------------------------------------------------------------
|   xsltAddTemplate
|
\---------------------------------------------------------------------------*/
static int xsltAddTemplate (
    xsltState *xs,    
    domNode   *node,
    double     precedence,
    char     **errMsg
)
{
    xsltTemplate *tpl;
    char         *prioStr;
    int           rc;
    
    tpl = malloc(sizeof(xsltTemplate));
    
    tpl->match      = getAttr(node,"match", a_match);
    tpl->name       = getAttr(node,"name", a_name);  
    tpl->ast        = NULL;
    tpl->mode       = getAttr(node,"mode", a_mode); 
    tpl->prio       = 0.5;
    tpl->content    = node;
    tpl->precedence = precedence;
    tpl->next       = NULL;

    prioStr = getAttr(node,"priority", a_prio);
    if (prioStr) {
        tpl->prio = (double)atof(prioStr);
    }
    
    TRACE1("compiling XPATH '%s' ...\n", tpl->match);
    if (tpl->match) {
        
        rc = xpathParse(tpl->match, errMsg, &(tpl->ast), 1); 
        CHECK_RC;
        if (!prioStr) {
            tpl->prio = xpathGetPrio(tpl->ast);
            TRACE1("prio = %f for \n", tpl->prio);
            DBG(printAst( 0, tpl->ast);)
            TRACE("\n");
        } else {
            DBG(printAst( 0, tpl->ast);)
        }
    }
    TRACE4("AddTemplate '%s' '%s' '%s' '%2.2f' \n\n",
            tpl->match, tpl->name, tpl->mode, tpl->prio);

    /* append new template at the end of the template list */
    if (xs->lastTemplate == NULL) {
        xs->templates = tpl;
        xs->lastTemplate = tpl;
    } else {
        xs->lastTemplate->next = tpl;
        xs->lastTemplate = tpl;
    }
    return 0;           
}

/*----------------------------------------------------------------------------
|   ExecUseAttributeSets
|
\---------------------------------------------------------------------------*/
int ExecUseAttributeSets (
    xsltState         * xs,
    xpathResultSet    * context,
    domNode           * currentNode,
    int                 currentPos,
    char              * styles,
    char             ** errMsg
)
{
    xsltAttrSet *attrSet;
    char        *pc, *aSet, save, *str;
    int          rc;
        
    pc = styles;
    while (*pc == ' ') pc++;
    aSet = pc;
                    
    while (*pc) {
        while (*pc && (*pc != ' ')) pc++;
        save = *pc;
        *pc = '\0';
        TRACE1("use-attribute-set '%s' \n", aSet);
        attrSet = xs->attrSets;
        while (attrSet) {
            TRACE2("use-Attr: '%s' == '%s' ? \n", attrSet->name, aSet);
            if (strcmp(attrSet->name, aSet)==0) {
                str = getAttr (attrSet->content, "use-attribute-sets",
                               a_useAttributeSets);
                if (str) {
                    rc = ExecUseAttributeSets (xs, context, currentNode,
                                               currentPos, str, errMsg);
                    CHECK_RC;
                }
                rc = ExecActions(xs, context, currentNode, currentPos, 
                                 attrSet->content->firstChild, errMsg);
                CHECK_RC;
                /*  break; */
            }  
            attrSet = attrSet->next;
        }
        *pc = save;
        while (*pc == ' ') pc++;
        aSet = pc;
    }
    return 0; 
}

/*----------------------------------------------------------------------------
|   evalAttrTemplates
|
\---------------------------------------------------------------------------*/
static int evalAttrTemplates (
    xsltState       * xs,
    xpathResultSet  * context,
    domNode         * currentNode,
    int               currentPos,
    char            * str,
    char           ** out,
    char           ** errMsg
)
{
    xpathResultSet  rs;
    char           *tplStart = NULL, *tplResult, *pc, literalChar;
    int             rc, aLen, inTpl = 0, p = 0, inLiteral;
    
    aLen = 500;
    *out = malloc(aLen);
    while (*str) {
        if (inTpl) {
            if (!inLiteral) {
                if (*str == '\'') {
                    inLiteral = 1;
                    literalChar = '\'';
                } else 
                if (*str == '"') {
                    inLiteral = 1;
                    literalChar = '"';
                }
            } else {
                if (*str == literalChar) {
                    inLiteral = 0;
                }
            }
            if (*str == '}' && !inLiteral) {
            
                *str = '\0';
                TRACE1("attrTpl: '%s' \n", tplStart);
                xs->current = currentNode;    
                rc = evalXPath (xs, context, currentNode, currentPos,
                                tplStart, &rs, errMsg);
                *str = '}';
                CHECK_RC;
                tplResult = xpathFuncString( &rs );
                DBG(fprintf(stderr, "attrTpl tplResult='%s' \n", tplResult);)
                xpathRSFree( &rs );
                pc = tplResult;
                while (*pc) {
                   (*out)[p++] = *pc++;
                    if (p>=aLen) { /* enlarge output buffer */
                         *out = realloc(*out, 2*aLen);
                         aLen += aLen;
                    }
                }
                inTpl = 0;
                free(tplResult);
            }
        } else {
            if (*str == '{') {
                if (*(str+1) == '{') {
                    /*-----------------------------------------------------
                    |    read over escaped '{':
                    |        '{{text text}}' -> '{text text}'
                    \----------------------------------------------------*/
                    str++;
                    (*out)[p++] = *str++;
                    if (p>=aLen) { /* enlarge output buffer */
                        *out = realloc(*out, 2*aLen);
                        aLen += aLen;
                    }
                    while (*str && (*str != '}') && (*(str-1) != '}')) {
                        (*out)[p++] = *str++;
                        if (p>=aLen) { /* enlarge output buffer */
                            *out = realloc(*out, 2*aLen);
                            aLen += aLen;
                        }
                    }
                    if (!*str) break;
                } else {
                    tplStart = str+1;
                    inTpl = 1;
                    inLiteral = 0;
                }
            } else {
                if (*str == '}' && *(str+1) == '}') {
                    str++;
                }
                (*out)[p++] = *str;
                if (p>=aLen) { /* enlarge output buffer */
                    *out = realloc(*out, 2*aLen);
                    aLen += aLen;
                }
            }
        }
        str++;
    }
    (*out)[p] = '\0';
    DBG(fprintf(stderr, "evalAttrTemplates out='%s' \n", (*out) );)
    return 0;
}


/*----------------------------------------------------------------------------
|   setParamVars
|
\---------------------------------------------------------------------------*/
static int setParamVars (
    xsltState       * xs,
    xpathResultSet  * context,
    domNode         * currentNode,
    int               currentPos,
    domNode         * actionNode,
    char           ** errMsg
)
{
    domNode *child;
    char    *str, *select;
    int      rc;
    
    child = actionNode->firstChild;
    while (child) {
        if (child->nodeType == ELEMENT_NODE) {
            TRACE1("setParamVars child '%s' \n", child->nodeName);
            if (getTag(child) == withParam) {
                str = getAttr(child, "name", a_name);
                if (str) {
                    TRACE1("setting with-param '%s' \n", str);
                    xs->currentXSLTNode = child;
                    select = getAttr(child, "select", a_select);
                    if (select) {
                        TRACE1("with-param select='%s'\n", select);
                        rc = xsltSetVar(xs, 1, str, context, currentNode, currentPos, 
                                        select, NULL, 0, errMsg);
                    } else {
                        rc = xsltSetVar(xs, 1, str, context, currentNode, currentPos, 
                                        NULL, child->firstChild, 0, errMsg);
                    }
                    CHECK_RC;
                } else {
                    reportError (child, "xsl:with-param: missing mandatory attribute \"name\".", errMsg);
                    return -1;
                }
            }
        }
        child = child->nextSibling;
    }
    return 0;
}


/*----------------------------------------------------------------------------
|   doSortActions
|
\---------------------------------------------------------------------------*/
static int doSortActions (
    xsltState       * xs,
    xpathResultSet  * nodelist,
    domNode         * actionNode,
    xpathResultSet  * context,
    domNode         * currentNode,
    int               currentPos,
    char           ** errMsg    
)
{
    domNode       *child;
    char          *str, *evStr, *select, *lang;
    char         **vs = NULL;
    double        *vd = NULL;
    int            rc = 0, typeText, ascending, upperFirst, *pos = NULL, i, NaN;
    xpathResultSet rs;
    
    child = actionNode->lastChild; /* do it backwards, so that multiple sort
                                      levels are correctly processed */
    while (child) {
        if (child->nodeType == ELEMENT_NODE) {
            TRACE1("doSortActions child '%s' \n", child->nodeName);
            if (getTag(child) == sort) {
                if (child->firstChild) {
                    reportError (child, "xsl:sort has to be empty.", errMsg);
                    return -1;
                }
                typeText  = 1;
                ascending = 1;
                upperFirst = 1;
                select = getAttr(child, "select", a_select);
                if (!select) select = ".";
                xs->currentXSLTNode = child;
                str = getAttr(child, "data-type", a_dataType);
                if (str) {
                    rc = evalAttrTemplates (xs, context, currentNode,
                                            currentPos, str, &evStr, errMsg);
                    CHECK_RC;
                    if (strcmp(evStr,"number")==0) typeText = 0;
                    free (evStr);
                }
                str = getAttr(child, "order", a_order);
                if (str) {
                    rc = evalAttrTemplates (xs, context, currentNode,
                                            currentPos, str, &evStr, errMsg);
                    CHECK_RC;
                    if (strcmp(evStr,"descending")==0) ascending = 0;
                    free (evStr);
                }
                str = getAttr(child, "case-order", a_caseorder);
                if (str) {
                    rc = evalAttrTemplates (xs, context, currentNode,
                                            currentPos, str, &evStr, errMsg);
                    CHECK_RC;
                    if (strcmp(evStr,"lower-first")==0) upperFirst = 0;
                    free (evStr);
                }
                /* jcl: TODO */
                lang = getAttr(child, "lang", a_lang);
                
                TRACE4("sorting with '%s' typeText %d ascending %d nodeSetLen=%d\n", 
                       select, typeText, ascending, nodelist->nr_nodes);
                CHECK_RC;
                if (!pos) 
                    pos = (int*) malloc( sizeof(int) * nodelist->nr_nodes);
                for (i=0; i<nodelist->nr_nodes;i++) pos[i] = i;

                xs->currentXSLTNode = child;
                
                if (!vs) {
                    vs = (char **) malloc (sizeof (char *) * nodelist->nr_nodes);
                    for (i=0; i<nodelist->nr_nodes;i++) vs[i] = NULL;
                    vd = (double *)malloc (sizeof (double) * nodelist->nr_nodes);
                }
                for (i = 0; i < nodelist->nr_nodes; i++) {
                    xpathRSInit (&rs);
                    rc = evalXPath (xs, nodelist, nodelist->nodes[i], i,
                                    select, &rs, errMsg);
                    if (rc < 0) 
                        goto doSortActionCleanUp;
                    
                    if (typeText) {
                        vs[i] = xpathFuncString (&rs);
                    } else {
                        vd[i] = xpathFuncNumber (&rs, &NaN);
                    }
                    xpathRSFree (&rs);
                }
                rc = sortNodeSetFastMerge (xs, typeText, ascending,
                                           upperFirst, nodelist->nodes,
                                           nodelist->nr_nodes, vs, vd,
                                           pos, errMsg);
                if (typeText) {
                    for (i = 0; i < nodelist->nr_nodes; i++) {
                        free (vs[i]);
                    }
                }
                if (rc < 0)
                    goto doSortActionCleanUp;
            }
        }
        child = child->previousSibling;
    }
 doSortActionCleanUp:
    if (pos) free (pos);
    if (vs) free (vs);
    if (vd) free (vd);
    return rc;
}


/*----------------------------------------------------------------------------
|   xsltNumber
|
\---------------------------------------------------------------------------*/
static int xsltNumber (
    xsltState       * xs,
    xpathResultSet  * context,
    domNode         * currentNode,
    int               currentPos,
    domNode         * actionNode,
    char           ** errMsg
)
{
    xpathResultSet    rs;
    int               rc, vs[20], NaN, hnew, i, useFormatToken, vVals = 0;
    int              *v, *vd = NULL;
    long              groupingSize = 0; 
    char             *value, *level, *count, *from, *str, *format;
    char             *groupingSeparator, *groupingSizeStr;
    ast               t_count, t_from;
    domNode          *node, *start;
    Tcl_HashEntry    *h;
    xsltNumberFormat *f;
    Tcl_DString       dStr;
    
    v = vs;
    value = getAttr(actionNode, "value",  a_value);
    str   = getAttr(actionNode, "format", a_format); if (!str) str = "1";
    xs->currentXSLTNode = actionNode;
    rc = evalAttrTemplates( xs, context, currentNode, currentPos,
                            str, &format, errMsg);
    CHECK_RC;
    f = xsltNumberFormatTokenizer (xs, format, errMsg);
    if (!f) {
        free (format);
        return -1;
    }
    groupingSeparator = getAttr(actionNode, "grouping-separator", 
                                a_groupingSeparator);
    if (groupingSeparator) {
        groupingSizeStr = getAttr(actionNode, "grouping-size", a_groupingSize);
        if (groupingSizeStr) {
            groupingSize = strtol (groupingSizeStr, (char **)NULL, 10);
            if (groupingSize <= 0) {
                reportError (actionNode, "xsl:number: do not understand the value of attribute \"grouping-size\"", errMsg);
                return -1;
            }
        } else {
            groupingSeparator = NULL;
        }
    }
    
    if (value) {
        TRACE2("xsltNumber value='%s' format='%s' \n", value, format);
        xs->current = currentNode;
        rc = evalXPath(xs, context, currentNode, currentPos,
                       value, &rs, errMsg);
        CHECK_RC;
        vVals = 1;
        v[0] = xpathRound(xpathFuncNumber( &rs, &NaN ));
        xpathRSFree( &rs );     
    } else {
        level = getAttr(actionNode, "level",  a_level); 
        if (!level) level = "single";
        count = getAttr(actionNode, "count",  a_count);
        from  = getAttr(actionNode, "from",   a_from);
        TRACE3("xsltNumber  format='%s' count='%s' from='%s' \n", format, count, from);
        if (count) {
            h = Tcl_CreateHashEntry (&(xs->pattern), count, &hnew);
            if (!hnew) {
                t_count = (ast) Tcl_GetHashValue (h);
            } else {
                rc = xpathParse (count, errMsg, &t_count, 1);
                CHECK_RC;
                Tcl_SetHashValue (h, t_count);
            }
        } else {
            if (currentNode->nodeType == ELEMENT_NODE) {
                /* TODO: This is wrong. Instead this should use the
                   "expanded-name" of the current node. */
                rc = xpathParse (currentNode->nodeName, errMsg, &t_count, 1);
                CHECK_RC;
            } else 
            if (currentNode->nodeType == ATTRIBUTE_NODE) {
                /* TODO: This is wrong. Instead this should use the
                   "expanded-name" of the current node. */
                Tcl_DStringInit (&dStr);
                Tcl_DStringAppend (&dStr, "@", 1);
                Tcl_DStringAppend (&dStr, currentNode->nodeName, -1);
                rc = xpathParse (Tcl_DStringValue(&dStr), errMsg, &t_count, 1);
                Tcl_DStringFree (&dStr);
                CHECK_RC;
            } else
            /* XPathTokens don't allow the xpath to be a CONST char* */
            if (currentNode->nodeType == COMMENT_NODE) {
                str = strdup ("comment()");
                rc = xpathParse (str, errMsg, &t_count, 1);
                free (str);
                CHECK_RC;
            } else 
            if (currentNode->nodeType == TEXT_NODE) {
                str = strdup ("text()");
                rc = xpathParse (str, errMsg, &t_count, 1);
                free (str);
                CHECK_RC;
            } else 
            if (currentNode->nodeType == PROCESSING_INSTRUCTION_NODE) {
                str = strdup ("processing-instruction()");
                rc = xpathParse (str, errMsg, &t_count, 1);
                free (str);
                CHECK_RC;
            } else {
                reportError (actionNode, "unknown node type!!!", errMsg);
                return -1;
            }
        }
        if (from) {
            h = Tcl_CreateHashEntry (&(xs->pattern), from, &hnew);
            if (!hnew) {
                t_from = (ast) Tcl_GetHashValue (h);
            } else {
                rc = xpathParse (from, errMsg, &t_from, 1);
                CHECK_RC;
                Tcl_SetHashValue (h, t_from);
            }
        }

        if (strcmp (level, "single")==0) {
            node = currentNode;
            start = NULL;
            if (from) {
                while (node) {
                    rc = xpathMatches (t_from, actionNode, node, &(xs->cbs), errMsg);
                    CHECK_RC;
                    if (rc) break;
                    node = node->parentNode;
                }
            }
            node = currentNode;
            while (node != start) {
                rc = xpathMatches (t_count, actionNode, node, &(xs->cbs), errMsg);
                CHECK_RC;
                if (rc) break;
                node = node->parentNode;
            }
            if (node == start) {
                domAppendNewTextNode (xs->lastNode, "", 0, TEXT_NODE, 0);
                free (format);
                return 0;
            } else {
                vVals = 1;
                v[0] = 1;
                node = node->previousSibling;
                while (node) {
                    rc = xpathMatches (t_count, actionNode, node, &(xs->cbs), errMsg);
                    CHECK_RC;
                    if (rc) v[0]++;
                    node = node->previousSibling;
                }
            }
        } else 
        if (strcmp (level, "multiple")==0) {
            xpathRSInit (&rs);
            node = currentNode;
            while (node) {
                if (from) {
                    rc = xpathMatches (t_from, actionNode, node, &(xs->cbs), errMsg);
                    CHECK_RC;
                    if (rc) break;
                }
                rc = xpathMatches (t_count, actionNode, node, &(xs->cbs), errMsg);
                CHECK_RC;
                if (rc) rsAddNode (&rs, node);
                node = node->parentNode;
            }
            if (rs.nr_nodes > 20) {
                vd = (int *) Tcl_Alloc (sizeof (int) * rs.nr_nodes);
                v = vd;
            }
            vVals = rs.nr_nodes;
            v[0] = 0;
            for (i = rs.nr_nodes - 1; i >= 0; i--) {
                node = rs.nodes[i]->previousSibling;
                v[rs.nr_nodes-1-i] = 1;
                while (node) {
                    rc = xpathMatches (t_count, actionNode, node, &(xs->cbs), errMsg);
                    CHECK_RC;
                    if (rc) v[rs.nr_nodes-1-i]++;
                    node = node->previousSibling;
                }
            }
            xpathRSFree (&rs);
        } else 
        if (strcmp (level, "any")==0) {
            v[0] = 0;
            vVals = 1;
            node = currentNode;
            while (node) {
                if (from) {
                    rc = xpathMatches (t_from, actionNode, node, &(xs->cbs), errMsg);
                    CHECK_RC;
                    if (rc) break;
                }
                rc = xpathMatches (t_count, actionNode, node, &(xs->cbs), errMsg);
                CHECK_RC;
                if (rc) v[0]++;
                    
                if (node->previousSibling) {
                    node = node->previousSibling;
                    while ((node->nodeType == ELEMENT_NODE) 
                           && node->lastChild) {
                        node = node->lastChild;
                    }
                    continue;
                }
                node = node->parentNode;
            }
        } else {
            reportError (actionNode, "xsl:number: Wrong \"level\" attribute value!",
                         errMsg);
            return -1;
        }
    }

    Tcl_DStringInit (&dStr);
    useFormatToken = 0;
    if (f->prologLen) {
        Tcl_DStringAppend (&dStr, f->formatStr, f->prologLen);
    }
    for (i = 0; i < vVals -1; i++) {
        formatValue (f, &useFormatToken, v[i], &dStr,
                     groupingSeparator, groupingSize, 1);
    }
    if (vVals > 0) {
        formatValue (f, &useFormatToken, v[vVals-1], &dStr,
                     groupingSeparator, groupingSize, 0);
        if (f->epilogLen) {
            Tcl_DStringAppend (&dStr, f->epilogStart, f->epilogLen);
        }
        domAppendNewTextNode(xs->lastNode, Tcl_DStringValue (&dStr),
                             Tcl_DStringLength (&dStr), TEXT_NODE, 0);
    }
    free (format);
    if (vd) {
        Tcl_Free ((char *)vd);
    }
    Tcl_DStringFree (&dStr);
    return 0;
}


/*----------------------------------------------------------------------------
|   CopyNS
|
\---------------------------------------------------------------------------*/
static void
copyNS (
    domNode *from,
    domNode *to
    )
{
    domNode     *n;
    domAttrNode *attr;
    
    n = from;
    while (n) {
        attr = n->firstAttr;
        while (attr && (attr->nodeFlags & IS_NS_NODE)) {
            domAddNSToNode (to,
                            n->ownerDocument->namespaces[attr->namespace-1]);
            attr = attr->nextSibling;
        }
        n = n->parentNode;
    }
}

/*----------------------------------------------------------------------------
|   ExecAction
|
\---------------------------------------------------------------------------*/
static int ExecAction (
    xsltState       * xs,
    xpathResultSet  * context,
    domNode         * currentNode,
    int               currentPos,
    domNode         * actionNode,
    char           ** errMsg
)
{
    domNode        *child, *n, *savedLastNode, *fragmentNode;
    xsltTemplate   *tpl, *currentTplRule, *tplChoosen;
    domAttrNode    *attr;
    domTextNode    *tnode;
    domNS          *ns;
    xsltSubDoc     *sDoc;
    xsltExcludeNS  *excludeNS;
    xsltNSAlias    *nsAlias;
    Tcl_DString     dStr;
    domProcessingInstructionNode *pi;      
    xpathResultSet  rs, nodeList;
    char           *str, *str2, *mode, *select, *pc;
    char           *nsAT, *nsStr;
    char           *uri, *localName, prefix[MAX_PREFIX_LEN];
    int             rc, b, i, len, disableEsc = 0;
    double          currentPrio, currentPrec;

    if (actionNode->nodeType == TEXT_NODE) {
        domAppendNewTextNode(xs->lastNode, 
                             ((domTextNode*)actionNode)->nodeValue,
                             ((domTextNode*)actionNode)->valueLength,
                             TEXT_NODE, 0);
        return 0;
    }
    if (actionNode->nodeType != ELEMENT_NODE) return 0;

    TRACE1("\nExecAction '%s' \n", actionNode->nodeName);
    DBG (printXML (currentNode, 3, 5);)
    xs->currentXSLTNode = actionNode;
    switch ( getTag(actionNode) ) {
    
        case applyImports:
            if (actionNode->firstChild) {
                reportError(actionNode, "xsl:apply-imports has to be empty!", errMsg);
                return -1;
            }
            if (!xs->currentTplRule) {
                reportError(actionNode, "xsl:apply-imports not allowed here!", errMsg);
                return -1;
            }
            tplChoosen = NULL;
            currentPrio = -100000.0;
            currentPrec = 0.0;
            mode = xs->currentTplRule->mode;
            TRACE2("apply-imports: current template precedence='%f' mode='%s'\n", xs->currentTplRule->precedence, xs->currentTplRule->mode);
            for (tpl = xs->templates; tpl != NULL; tpl = tpl->next) {
                TRACE3("find tpl match='%s' mode='%s' name='%s'\n",
                       tpl->match, tpl->mode, tpl->name);
                /* exclude those, which don't match the current mode 
                   and the currentTplRule */
                if (   ( mode && !tpl->mode)
                       || (!mode &&  tpl->mode)
                       || ( mode &&  tpl->mode && (strcmp(mode,tpl->mode)!=0))
                       || (tpl == xs->currentTplRule)
                    ) {
                    TRACE("doesn't match mode\n");
                    continue; 
                }
                TRACE4("tpl has prio='%f' precedence='%f', currentPrio='%f', currentPrec='%f'\n", tpl->prio, tpl->precedence, currentPrio, currentPrec);
                if (tpl->match && tpl->precedence < xs->currentTplRule->precedence
                    && tpl->precedence >= currentPrec) {
                    if (tpl->precedence > currentPrec 
                        || tpl->prio >= currentPrio) {
                        rc = xpathMatches (tpl->ast, actionNode, currentNode,
                                           &(xs->cbs), errMsg);
                        CHECK_RC;
                        if (rc == 0) continue;
                        TRACE3("matching '%s': %f > %f ? \n", tpl->match, tpl->prio , currentPrio);
                        tplChoosen = tpl;
                        currentPrec = tpl->precedence;
                        currentPrio = tpl->prio;
                        TRACE1("TAKING '%s' \n", tpl->match);
                    }
                }
            }
            if (tplChoosen == NULL) {

                TRACE("nothing matches -> execute built-in template \n");
                
                /*--------------------------------------------------------------------
                |   execute built-in template
                \-------------------------------------------------------------------*/
                if (currentNode->nodeType == TEXT_NODE) {
                    domAppendNewTextNode(xs->lastNode,
                                         ((domTextNode*)currentNode)->nodeValue,
                                         ((domTextNode*)currentNode)->valueLength, 
                                         TEXT_NODE, 0);
                    return 0;
                } else
                if (currentNode->nodeType == ELEMENT_NODE) {
                    child = currentNode->firstChild;
                } else 
                if (currentNode->nodeType == ATTRIBUTE_NODE) {
                    domAppendNewTextNode (xs->lastNode,
                                          ((domAttrNode*)currentNode)->nodeValue,
                                          ((domAttrNode*)currentNode)->valueLength,
                                          TEXT_NODE, 0);
                    return 0;
                } else {
                    return 0; /* for all other node we don't have to recurse deeper */
                }
                xpathRSInit( &rs );
                while (child) {    
                    rsAddNodeFast ( &rs, child);
                    child = child->nextSibling; 
                }
                rc = ApplyTemplates (xs, context, currentNode, currentPos,
                                actionNode, &rs, mode, errMsg);
                xpathRSFree( &rs );
                CHECK_RC;

                break;
            }
            
            xsltPushVarFrame (xs);
            currentTplRule = xs->currentTplRule;
            xs->currentTplRule = tplChoosen;
            rc = ExecActions(xs, context, currentNode, currentPos, 
                             tplChoosen->content->firstChild, errMsg);
            CHECK_RC;
            xs->currentTplRule = currentTplRule;
            xsltPopVarFrame (xs);
            break;
            
        case applyTemplates:
            mode   = getAttr(actionNode, "mode", a_mode);
            select = getAttr(actionNode, "select", a_select);
            if (!select) { 
                xpathRSInit (&rs);
                if (currentNode->nodeType == ELEMENT_NODE) {
                    child = currentNode->firstChild;
                    while (child) {
                        rsAddNodeFast (&rs, child);
                        child = child->nextSibling;
                    }
                }
            } else {
                xpathRSInit( &nodeList );
                rsAddNode( &nodeList, currentNode );
                DBG(rsPrint( &nodeList ));
                TRACE2("applyTemplates: select = '%s' mode='%s'\n", select, mode);
                xs->current = currentNode;
                rc = evalXPath(xs, &nodeList, currentNode, 1, select, &rs, errMsg);
                xpathRSFree( &nodeList ); 
                CHECK_RC;
                TRACE1("applyTemplates: evalXPath for select = '%s' gave back:\n", select);
                DBG(rsPrint(&rs));
            }
            
            rc = doSortActions (xs, &rs, actionNode, context, currentNode,
                                currentPos, errMsg);
            CHECK_RC;
            /* should not be necessary, because every node set is
               returned already in doc Order */
            /*  if (!sorted) sortByDocOrder(&rs); */
            
            TRACE1("                evalXPath for select = '%s': (SORTED)\n", select);
            DBG(rsPrint(&rs));

            rc = ApplyTemplates(xs, context, currentNode, currentPos,
                                actionNode, &rs, mode, errMsg); 
            CHECK_RC;
            xpathRSFree( &rs ); 
            break;
            
        case attribute:
            if (xs->lastNode->firstChild) {
                /* Adding an Attribute to an element after
                   children have been added to it is an error.
                   Ignore the attribute. */
                break;
            }
            nsAT = getAttr(actionNode, "namespace", a_namespace);
            str = getAttr(actionNode, "name", a_name);
            if (!str) {
                reportError (actionNode, "xsl:attribute: missing mandatory attribute \"name\".", errMsg);
                return -1;
            }
            
            rc = evalAttrTemplates( xs, context, currentNode, currentPos,
                                    str, &str2, errMsg);
            CHECK_RC;
            nsStr = NULL;
            domSplitQName (str2, prefix, &localName);
            if (prefix[0] != '\0') {
                if (strcmp (prefix, "xmlns")==0) goto ignoreAttribute;
            } else {
                if (strcmp (str2, "xmlns")==0) goto ignoreAttribute;
            }
            Tcl_DStringInit (&dStr);
            if (nsAT) {
                rc = evalAttrTemplates( xs, context, currentNode, currentPos,
                                        nsAT, &nsStr, errMsg);
                CHECK_RC;
                if (nsStr[0] == '\0') {
                    if (prefix[0] != '\0') {
                        Tcl_DStringAppend (&dStr, localName, -1);
                    } else {
                        Tcl_DStringAppend (&dStr, str2, -1);
                    }
                    free (nsStr);
                    nsStr = NULL;
                } else {
                    if (prefix[0] == '\0') {
                        ns = domLookupURI (xs->lastNode, nsStr);
                        if (ns && (ns->prefix[0] != '\0')) {
                            Tcl_DStringAppend (&dStr, ns->prefix, -1);
                            Tcl_DStringAppend (&dStr, ":", 1);
                        } else {
                            sprintf (prefix, "ns%d", xs->nsUniqeNr);
                            xs->nsUniqeNr++;
                            Tcl_DStringAppend (&dStr, prefix, -1);
                            Tcl_DStringAppend (&dStr, ":", 1);
                        }
                    }
                    Tcl_DStringAppend (&dStr, str2, -1);
                }
            } else {
                if (prefix[0] != '\0') {
                    ns = domLookupPrefix (actionNode, prefix);
                    if (ns) nsStr = strdup (ns->uri);
                    else goto ignoreAttribute;
                } 
                Tcl_DStringAppend (&dStr, str2, -1);
            }
                
            savedLastNode = xs->lastNode;
            xs->lastNode  = domNewElementNode (xs->resultDoc,  
                                               "container", ELEMENT_NODE);
            xsltPushVarFrame (xs);
            rc = ExecActions(xs, context, currentNode, currentPos,
                             actionNode->firstChild, errMsg);
            xsltPopVarFrame (xs);
            CHECK_RC;
            pc = xpathGetTextValue (xs->lastNode, &len);
            DBG(fprintf (stderr, "xsl:attribute: create attribute \"%s\" with value \"%s\" in namespace \"%s\"\n", Tcl_DStringValue (&dStr), pc, nsStr);)
            domSetAttributeNS (savedLastNode, Tcl_DStringValue (&dStr), pc,
                               nsStr, 1);
            free(pc);
            Tcl_DStringFree (&dStr);
            domDeleteNode (xs->lastNode, NULL, NULL);
            xs->lastNode = savedLastNode;
    ignoreAttribute:
            if (nsStr) free (nsStr);
            free(str2);
            break;
            
        case attributeSet: return 0;
            
        case callTemplate:
            tplChoosen = NULL;
            currentPrec = INT_MIN;
            str = getAttr(actionNode, "name", a_name);
            if (!str) {
                reportError (actionNode,
                             "xsl:call-template must have a \"name\" attribute",
                             errMsg);
                return -1;
            }
            for( tpl = xs->templates; tpl != NULL; tpl = tpl->next) {
                if (tpl->name && (strcmp(tpl->name,str)==0)) {
                    if (tpl->precedence > currentPrec) {
                        tplChoosen = tpl;
                        currentPrec = tpl->precedence;
                    }
                }
            }
            if (tplChoosen) {
                xsltPushVarFrame (xs);
                TRACE3("call template %s match='%s' name='%s' \n", str, tplChoosen->match, tplChoosen->name);
                DBG(printXML(xs->lastNode, 0, 2);)
                rc = setParamVars (xs, context, currentNode, currentPos,
                                   actionNode, errMsg);
                CHECK_RC; 
                rc = ExecActions(xs, context, currentNode, currentPos, 
                                 tplChoosen->content->firstChild, errMsg);
                TRACE2("called template '%s': ApplyTemplate/ExecActions rc = %d \n", str, rc);
                CHECK_RC;    
                xsltPopVarFrame (xs);
                DBG(printXML(xs->lastNode, 0, 2);)
            } else {
                reportError (actionNode, 
                             "xsl:call-template has called a non existend template!",
                             errMsg);
                return -1;
            }
            break;

       case choose:
            for( child = actionNode->firstChild;  child != NULL;
                 child = child->nextSibling) 
            {
                if (child->nodeType != ELEMENT_NODE) continue;
                switch (getTag(child)) {
                    case when:
                        str = getAttr(child, "test", a_test);
                        if (str) {
                            TRACE1("checking when test '%s' \n", str);
                            xs->current = currentNode;
                            rc = evalXPath(xs, context, currentNode, currentPos,
                                           str, &rs, errMsg);
                            CHECK_RC;
                            b = xpathFuncBoolean( &rs );
                            xpathRSFree( &rs ); 
                            if (b) {
                                TRACE("test is true!\n");
                                /* process the children as well */
                                xsltPushVarFrame (xs);
                                rc = ExecActions(xs, context,
                                                 currentNode, currentPos,
                                                 child->firstChild, errMsg);
                                xsltPopVarFrame (xs);
                                CHECK_RC;
                                return 0;
                            }                            
                        } else {
                            reportError (child, "xsl:when: missing mandatory attribute \"test\".", errMsg);
                            return -1;
                        }
                        break;
                        
                    case otherwise:
                        /* process the children as well */
                        xsltPushVarFrame (xs);
                        rc = ExecActions(xs, context, currentNode, currentPos,
                                         child->firstChild, errMsg);
                        xsltPopVarFrame (xs);
                        CHECK_RC;
                        return 0;
                        
                    default:
                        reportError (actionNode,
                                     "only otherwise or when allowed in choose!",
                                     errMsg);
                        return -1;
                }
            }

            break;
            
        case comment:
            fragmentNode = domNewElementNode(xs->resultDoc, "(fragment)",
                                             ELEMENT_NODE);
            savedLastNode = xs->lastNode;
            xs->lastNode = fragmentNode;
            xsltPushVarFrame (xs);
            rc = ExecActions(xs, context, currentNode, currentPos,
                             actionNode->firstChild, errMsg);
            xsltPopVarFrame (xs);
            CHECK_RC;
            child = fragmentNode->firstChild;
            while (child) {
                if (child->nodeType != TEXT_NODE) {
                    domDeleteNode (fragmentNode, NULL, NULL);
                    reportError (actionNode, "xsl:comment must not create nodes other than text nodes.", errMsg);
                    return -1;
                }
                child = child->nextSibling;
            }
            str = xpathGetTextValue (fragmentNode, &len);
            pc = str;
            i = 0;
            while (i < len) {
                if (*pc == '-') {
                    if (i == len - 1) {
                        reportError (actionNode, "The text produced by xsl:comment must not end with the '-' character.", errMsg);
                        domDeleteNode (fragmentNode, NULL, NULL);
                        free (str);
                        return -1;
                    }
                    pc++; i++;
                    if (*pc == '-') {
                        reportError (actionNode, "The text produced by xsl:comment must not contain the string \"--\"", errMsg);
                        domDeleteNode (fragmentNode, NULL, NULL);
                        free (str);
                        return -1;
                    }
                }
                pc++; i++;
            }
            xs->lastNode = savedLastNode;
            domAppendNewTextNode(xs->lastNode, str, len, COMMENT_NODE, 0);
            domDeleteNode (fragmentNode, NULL, NULL);
            free (str);
            break;
            
        case copy:
            DBG(if (currentNode->nodeType == ATTRIBUTE_NODE) {
                    fprintf(stderr, "copy '%s' \n", ((domAttrNode*)currentNode)->nodeName);
                } else {
                    fprintf(stderr, "copy '%s' \n", currentNode->nodeName);
                })
            if (currentNode->nodeType == TEXT_NODE) {
                DBG(fprintf(stderr, "node is TEXT_NODE \n");)
                tnode = (domTextNode*)currentNode;
                n = (domNode*)
                    domAppendNewTextNode(xs->lastNode, 
                                         tnode->nodeValue,
                                         tnode->valueLength,
                                         TEXT_NODE, 0);
            } else
            if (currentNode->nodeType == ELEMENT_NODE) {
                DBG(fprintf(stderr, "node is ELEMENT_NODE \n");)
                if (currentNode != currentNode->ownerDocument->rootNode) {
                    n = domAppendNewElementNode(xs->lastNode, 
                                                currentNode->nodeName,
                                                domNamespaceURI(currentNode) );
                    savedLastNode = xs->lastNode;
                    xs->lastNode = n;
                    str = getAttr(actionNode, "use-attribute-sets",
                              a_useAttributeSets);
                    copyNS (currentNode, xs->lastNode);
                    if (str) {
                        rc = ExecUseAttributeSets (xs, context, currentNode,
                                                   currentPos, str, errMsg);
                        CHECK_RC;
                    }
                }
            } else 
            if (currentNode->nodeType == PROCESSING_INSTRUCTION_NODE) {
                pi = (domProcessingInstructionNode*)currentNode;
                n = (domNode*)
                    domNewProcessingInstructionNode (xs->lastNode->ownerDocument,
                                                     pi->targetValue,
                                                     pi->targetLength, 
                                                     pi->dataValue,
                                                     pi->dataLength);
                domAppendChild (xs->lastNode, n);
                
            } else 
            if (currentNode->nodeType == COMMENT_NODE) {
                DBG(fprintf(stderr, "node is COMMENT_NODE \n");)
                tnode = (domTextNode *)currentNode;
                n = (domNode *) domAppendNewTextNode (xs->lastNode,
                                                      tnode->nodeValue,
                                                      tnode->valueLength,
                                                      COMMENT_NODE, 0);
            } else 
            if (currentNode->nodeType == ATTRIBUTE_NODE) {
                DBG(fprintf(stderr, "node is ATTRIBUTE_NODE \n");)
                if (xs->lastNode->firstChild) {
                    /* Adding an Attribute to an element after
                       children have been added to it is an error.
                       Ignore the attribute. */
                    break;
                }
                attr = (domAttrNode *)currentNode;
                domSetAttributeNS (xs->lastNode, attr->nodeName,
                                   attr->nodeValue, 
                                   domNamespaceURI (currentNode), 1);
            }

            /* process the children only for root and element nodes */
            if (currentNode->nodeType == ELEMENT_NODE) {
                xsltPushVarFrame (xs);
                rc = ExecActions(xs, context, currentNode, currentPos,
                                 actionNode->firstChild, errMsg);
                xsltPopVarFrame (xs);
                CHECK_RC;
                xs->lastNode = savedLastNode;
            }
            break;
            
        case copyOf:            
            if (actionNode->firstChild) {
                reportError (actionNode, "xsl:copy-of has to be empty.", errMsg);
                return -1;
            }
            select = getAttr(actionNode, "select", a_select);
            if (!select) {
                reportError (actionNode, "xsl:copy-of: missing mandatory attribute \"select\".", errMsg);
                return -1;
            }
            
            xs->current = currentNode;
            rc = evalXPath(xs, context, currentNode, currentPos, select,
                           &rs, errMsg);
            CHECK_RC;            
            TRACE1(" copyOf select='%s':\n", select);
            DBG(rsPrint(&rs));
            if (rs.type == xNodeSetResult) {
                for (i=0; i<rs.nr_nodes; i++) { 
                    if (rs.nodes[i]->nodeType == ATTRIBUTE_NODE) {
                        attr = (domAttrNode*)rs.nodes[i];
                        ns = domGetNamespaceByIndex (attr->parentNode->ownerDocument, attr->namespace);
                        if (ns) uri = ns->uri;
                        else uri = NULL;
                        domSetAttributeNS(xs->lastNode, attr->nodeName,
                                          attr->nodeValue, uri, 1);
                    } else {
                        if (*(rs.nodes[i]->nodeName) == '(' &&
                            ((strcmp(rs.nodes[i]->nodeName,"(fragment)")==0)
                             || (strcmp(rs.nodes[i]->nodeName,"(rootNode)")==0))) {
                            child = rs.nodes[i]->firstChild;
                            while (child) {
                                domCopyTo(child, xs->lastNode, 1);
                                child = child->nextSibling;
                            }
                        } else {
                            domCopyTo (rs.nodes[i], xs->lastNode, 1);
                        }
                    }
                }
            } else {
                str = xpathFuncString( &rs );
                TRACE1("copyOf: xpathString='%s' \n", str);
                domAppendNewTextNode(xs->lastNode, str, strlen(str),
                                     TEXT_NODE, 0);
                free(str);
            }
            xpathRSFree( &rs );               
            break;
            
        case decimalFormat: return 0;
        
        case element:
            nsAT = getAttr(actionNode, "namespace", a_namespace);
            str  = getAttr(actionNode, "name", a_name);
            if (!str) {
                reportError (actionNode, "xsl:element: missing mandatory attribute \"name\".", errMsg);
                return -1;
            }
            
            rc = evalAttrTemplates( xs, context, currentNode, currentPos,
                                    str, &str2, errMsg);
            CHECK_RC;
            if (!domIsNAME (str2)) {
                reportError (actionNode, "xsl:element: Element name is not a valid QName.", errMsg);
                return -1;
            }
            nsStr = NULL;
            if (nsAT) {
                rc = evalAttrTemplates( xs, context, currentNode, currentPos,
                                        nsAT, &nsStr, errMsg);
                CHECK_RC;
            } else {
                domSplitQName (str2, prefix, &localName);
                if (prefix[0] != '\0') {
                    if (!domIsNCNAME (localName)) {
                        reportError (actionNode, "xsl:element: Element name is not a valid QName.", errMsg);
                        return -1;
                    }
                    ns = domLookupPrefix (actionNode, prefix);
                    if (ns) nsStr = ns->uri;
                    else {
                        reportError (actionNode, "xsl:element: there isn't a URI associated with the prefix of the element name.", errMsg);
                        return -1;
                    }
                }
            }
            savedLastNode = xs->lastNode;
            xs->lastNode = domAppendNewElementNode (xs->lastNode, str2, nsStr);
            free(str2);
            str = getAttr(actionNode, "use-attribute-sets", a_useAttributeSets);
            if (str) {
                TRACE1("use-attribute-sets = '%s' \n", str);
                rc = ExecUseAttributeSets (xs, context, currentNode, currentPos,
                                           str, errMsg);
                CHECK_RC;
            }
            /* process the children as well */
            if (actionNode->firstChild) {
                xsltPushVarFrame (xs);
                rc = ExecActions(xs, context, currentNode, currentPos,
                                 actionNode->firstChild, errMsg);
                xsltPopVarFrame (xs);
            }
            xs->lastNode = savedLastNode;
            CHECK_RC;
            break;
            
        case fallback: return 0;
            
        case forEach:
            select = getAttr(actionNode, "select", a_select);
            if (!select) {
                reportError (actionNode, "xsl:for-each: The select attribute is required.", errMsg);
                return -1;
            }
            DBG (
              if (currentNode->nodeType == ELEMENT_NODE) {
                  fprintf (stderr, "forEach select from Element Node '%s' %d:\n", currentNode->nodeName, currentNode->nodeNumber);
                  if (currentNode->firstChild) {
                      fprintf(stderr, "forEach select from child '%s' %d:\n", currentNode->firstChild->nodeName, currentNode->firstChild->nodeNumber);
                  }
              } else if (currentNode->nodeType == ATTRIBUTE_NODE) {
                  fprintf (stderr, "forEach select from Attribute Node '%s' Value '%s'\n", ((domAttrNode *)currentNode)->nodeName, ((domAttrNode *)currentNode)->nodeValue);
              } else {
                  fprintf (stderr, "forEach select from nodetype %d\n", currentNode->nodeType);
              }
            )
            xpathRSInit( &nodeList );
            rsAddNode( &nodeList, currentNode );
            DBG(rsPrint( &nodeList ));
            xs->current = currentNode;
            rc = evalXPath(xs, &nodeList, currentNode, 1, select, &rs, errMsg);
            CHECK_RC;
            TRACE1("forEach: evalXPath for select = '%s' gave back:\n", select);
            DBG(rsPrint(&rs));

            if (rs.type == xNodeSetResult) {
                rc = doSortActions (xs, &rs, actionNode, context, currentNode,
                                    currentPos, errMsg);
                CHECK_RC;
                /* should not be necessary, because every node set is
                   returned already in doc Order */
                /*  if (!sorted) sortByDocOrder(&rs); */

                TRACE1(" forEach for select = '%s': (SORTED)\n", select);
                DBG(rsPrint(&rs));
                currentTplRule = xs->currentTplRule;
                xs->currentTplRule = NULL;
                xsltPushVarFrame (xs);
                for (i=0; i<rs.nr_nodes; i++) {
                    /* process the children as well */
                    rc = ExecActions(xs, &rs, rs.nodes[i], i,
                                     actionNode->firstChild, errMsg);
                    CHECK_RC;
                    if (xs->varFrames->polluted) {
                        xsltPopVarFrame (xs);
                        xsltPushVarFrame (xs);
                    }
                }
                xsltPopVarFrame (xs);
                xs->currentTplRule = currentTplRule;
            } else {
                if (rs.type != EmptyResult) {
                    reportError (actionNode, "The \"select\" expression of xsl:for-each elements must evaluate to a node set.", errMsg);
                    xpathRSFree (&rs);
                    return -1;
                }
            }
            xpathRSFree( &rs );
            xpathRSFree (&nodeList);
            break;
            
        case xsltIf:
            str = getAttr(actionNode, "test", a_test);
            if (str) {
                xs->current = currentNode;
                rc = evalXPath(xs, context, currentNode, currentPos, str, 
                               &rs, errMsg);
                CHECK_RC;
                b = xpathFuncBoolean( &rs );
                xpathRSFree( &rs ); 
                if (b) {
                    /* process the children as well */
                    xsltPushVarFrame (xs);
                    rc = ExecActions(xs, context, currentNode, currentPos,
                                     actionNode->firstChild, errMsg);
                    xsltPopVarFrame (xs);
                    CHECK_RC;
                }
            } else {
                reportError (actionNode, "xsl:if: missing mandatory attribute \"test\".", errMsg);
                return -1;
            }
            break;

        case import:
        case include:
        case key:
            return 0;
                        
        case message:
            str  = getAttr(actionNode,"terminate", a_terminate); 
            if (!str)  str = "no";
            fragmentNode = domNewElementNode(xs->resultDoc, "(fragment)", ELEMENT_NODE);
            savedLastNode = xs->lastNode;
            xs->lastNode = fragmentNode;
            xsltPushVarFrame (xs);
            rc = ExecActions(xs, context, currentNode, currentPos,
                             actionNode->firstChild, errMsg);
            xsltPushVarFrame (xs);
            CHECK_RC;
            
            str2 = xpathGetTextValue(fragmentNode, &len);
            fprintf (stderr, "xsl:message %s\n", str2);
            free (str2);
            xs->lastNode = savedLastNode;
            domDeleteNode (fragmentNode, NULL, NULL);
            if (strcmp (str, "yes")==0) {
                reportError (actionNode, "xsl:message with attribute \"terminate\"=\"yes\"", errMsg);
                return -1;
            }
            return 0;
            
        case namespaceAlias: return 0;
        
        case number:
            if (actionNode->firstChild) {
                reportError (actionNode, "xsl:number has to be empty.", errMsg);
                return -1;
            }
            rc = xsltNumber(xs, context, currentNode, currentPos,
                            actionNode, errMsg);
            CHECK_RC;
            break;
        
        case output:
        case otherwise:
            return 0;
             
        case param:
            str = getAttr(actionNode, "name", a_name);
            if (str) {
                TRACE1("setting param '%s' ??\n", str);
                if (!xsltVarExists(xs, str)) {
                    TRACE1("setting param '%s': yes \n", str);
                    select = getAttr(actionNode, "select", a_select);
                    if (select) {
                        TRACE1("param select='%s'\n", select);
                        rc = xsltSetVar(xs, 0, str, context, currentNode, currentPos, 
                                        select, NULL, 0, errMsg);
                    } else {
                        rc = xsltSetVar(xs, 0, str, context, currentNode,
                                        currentPos, NULL, 
                                        actionNode->firstChild, 0, errMsg);
/*                          if (actionNode->firstChild) { */
/*                              rc = xsltSetVar(xs, 0, str, context, currentNode, */
/*                                              currentPos, NULL,  */
/*                                              actionNode->firstChild, 0, errMsg); */
/*                          } else { */
/*                              rc = xsltSetVar(xs, 0, str, context, currentNode,  */
/*                                              currentPos, "", NULL, 0, errMsg); */
/*                          } */
                    }
                    CHECK_RC;
                }
            } else {
                reportError (actionNode, "xsl:param: missing mandatory attribute \"name\".", errMsg);
                return -1;
            }
            break;
            
        case preserveSpace: return 0;
        
        case procinstr:
            str = getAttr(actionNode, "name", a_name);
            if (str) {
                rc = evalAttrTemplates( xs, context, currentNode, currentPos,
                                        str, &str2, errMsg);
                /* TODO: no processing of content template? */
                pc = xpathGetTextValue (actionNode, &len);
                n = (domNode*)domNewProcessingInstructionNode(
                                 xs->resultDoc, str2, strlen(str), pc, len);
                domAppendChild(xs->lastNode, n);
                free(str2);
                free(pc);
            } else {
                reportError (actionNode, "xsl:processing-instruction: missing mandatory attribute \"name\".", errMsg);
                return -1;
            }
            break;
            
        case sort:
        case stylesheet: 
        case stripSpace:
        case template:
            return 0;
        
        case text:
            str = getAttr(actionNode, "disable-output-escaping", a_disableOutputEscaping);
            if (str) {
                if (strcmp (str, "yes")==0) disableEsc = 1;
            }
            pc = xpathGetTextValue (actionNode, &len);
            DBG(fprintf(stderr, "text: pc='%s'%d \n", pc, len);)
            domAppendNewTextNode(xs->lastNode, pc, len, TEXT_NODE, disableEsc);
            free (pc);
            break;
            
        case transform: return 0;
        
        case valueOf:
            if (actionNode->firstChild) {
                reportError (actionNode, "xsl:value-of has to be empty.", errMsg);
                return -1;
            }
            str = getAttr(actionNode, "disable-output-escaping", a_disableOutputEscaping);
            if (str) {
                if (strcmp (str, "yes")==0) disableEsc = 1;
            }
            str = getAttr(actionNode, "select", a_select);
            if (str) {
                TRACE1("valueOf: str='%s' \n", str);
                xs->current = currentNode;
                rc = evalXPath(xs, context, currentNode, currentPos, str,
                               &rs, errMsg);
                CHECK_RC;
                DBG(rsPrint(&rs));
                str = xpathFuncString( &rs );
                TRACE1("valueOf: xpathString='%s' \n", str);
                domAppendNewTextNode(xs->lastNode, str, strlen(str), 
                                     TEXT_NODE, disableEsc);
                xpathRSFree( &rs ); 
                free(str);
            } else {
                reportError (actionNode,
                             "xsl:value-of must have a \"select\" attribute!",
                             errMsg);
                return -1;
            }
            break;
            
        case variable:
            str = getAttr(actionNode, "name", a_name);
            if (str) {
                if (xsltVarExists (xs, str)) {
                    reportError (actionNode, 
                                 "Variable is already declared in this template", 
                                 errMsg);
                    return -1;
                }
                select = getAttr(actionNode, "select", a_select);
                if (select) {
                    TRACE1("variable select='%s'\n", select);
                    rc = xsltSetVar(xs, 0, str, context, currentNode, currentPos, 
                                    select, NULL, 0, errMsg);
                } else {
                    rc = xsltSetVar(xs, 0, str, context, currentNode, currentPos, 
                                    NULL, actionNode->firstChild, 0, errMsg);
                }
                CHECK_RC;
            } else {
                reportError (actionNode,
                             "xsl:variable must have a \"name\" attribute!",
                             errMsg);
                return -1;
            }
            break;
            
        case when:
        case withParam:
            return 0;
            
        default:
            savedLastNode = xs->lastNode;
            DBG(fprintf(stderr,
                        "append new tag '%s' uri='%s' \n", actionNode->nodeName,
                        domNamespaceURI(actionNode) ););
            xs->lastNode = domAppendLiteralNode (xs->lastNode, actionNode);
            n = actionNode;
            sDoc = xs->subDocs;
            while (sDoc) {
                if (sDoc->doc == actionNode->ownerDocument) break;
                sDoc = sDoc->next;
            }
            if (!sDoc) {
                *errMsg = strdup ("Internal Error");
                return -1;
            }
            while (n) {
                attr = n->firstAttr;
                while (attr && (attr->nodeFlags & IS_NS_NODE)) {
                    /* xslt namespace isn't copied */
                    if (strcmp (attr->nodeValue, XSLT_NAMESPACE)==0){
                        attr = attr->nextSibling;
                        continue;
                    }
                    ns = actionNode->ownerDocument->namespaces[attr->namespace-1];
                    uri = ns->uri;
                    nsAlias = xs->nsAliases;
                    while (nsAlias) {
                        if (strcmp (nsAlias->fromUri, ns->uri)==0) {
                            ns->uri = nsAlias->toUri;
                            break;
                        }
                        nsAlias = nsAlias->next;
                    }
                    excludeNS = sDoc->excludeNS;
                    while (excludeNS) {
                        if (excludeNS->uri) {
                            if (strcmp (excludeNS->uri, ns->uri)==0) break;
                        } else {
                            if (ns->prefix[0] == '\0') break;
                        }
                        excludeNS = excludeNS->next;
                    }
                    if (!excludeNS) {
                        domAddNSToNode (xs->lastNode, ns);
                    }
                    ns->uri = uri;
                    attr = attr->nextSibling;
                }
                n = n->parentNode;
            }
            /* It's not clear, what to do, if the literal result
               element is in a namespace, that should be excluded. We
               follow saxon and xalan, which both add the namespace of
               the literal result element always to the result tree,
               to ensure, that the result tree is conform to the XML
               namespace recommendation. See the more detailed
               discussion in the file discretionary-behavior */
            if (actionNode->namespace) {
                ns = actionNode->ownerDocument->namespaces[actionNode->namespace-1];
                uri = ns->uri;
                nsAlias = xs->nsAliases;
                while (nsAlias) {
                    if (strcmp (nsAlias->fromUri, ns->uri)==0) {
                        ns->uri = nsAlias->toUri;
                        break;
                    }
                    nsAlias = nsAlias->next;
                }
                domAddNSToNode (xs->lastNode, ns);
                ns->uri = uri;
            }

            n = xs->lastNode;
            /* process the attributes */
            attr = actionNode->firstAttr;
            while (attr) {
                if (attr->nodeFlags & IS_NS_NODE) {
                    attr = attr->nextSibling;
                    continue;
                }
                /* TODO: xsl:exclude-result-prefixes attribute on literal
                         elements on the ancestor-or-self axis */
                uri = domNamespaceURI((domNode*)attr);
                if (uri && strcmp(uri, XSLT_NAMESPACE)==0) {
                    domSplitQName((char*)attr->nodeName, prefix, &localName);
                    if (strcmp(localName,"use-attribute-sets")==0) {
                        str = attr->nodeValue;
                        rc = ExecUseAttributeSets (xs, context, currentNode,
                                                   currentPos, str, errMsg);
                        CHECK_RC;
                    }
                } else {
                    rc = evalAttrTemplates( xs, context, currentNode,
                                            currentPos, attr->nodeValue, &str,
                                            errMsg);
                    CHECK_RC;
                    if (uri) {
                        nsAlias = xs->nsAliases;
                        while (nsAlias) {
                            if (strcmp (nsAlias->fromUri, uri)==0) {
                                uri = nsAlias->toUri;
                                break;
                            }
                            nsAlias = nsAlias->next;
                        }
                    }
                    domSetAttributeNS (n, attr->nodeName, str, uri, 1);
                    free(str);
                }
                attr = attr->nextSibling;
            } 
            /* process the children as well */
            rc = ExecActions(xs, context, currentNode, currentPos,
                             actionNode->firstChild, errMsg);
            CHECK_RC;
            xs->lastNode = savedLastNode;
            return 0;
    }
    return 0;
}


/*----------------------------------------------------------------------------
|   ExecActions
|
\---------------------------------------------------------------------------*/
static int ExecActions (
    xsltState       * xs,
    xpathResultSet  * context,
    domNode         * currentNode,
    int               currentPos,
    domNode         * actionNode,
    char           ** errMsg
)
{
    domNode *savedLastNode, *savedCurrentNode;
    int rc;
    
    savedLastNode    = xs->lastNode;
    savedCurrentNode = xs->current;
    
    while (actionNode) {
        xs->current = currentNode;
        rc = ExecAction (xs, context, currentNode, currentPos, actionNode, errMsg);
        CHECK_RC;
        actionNode = actionNode->nextSibling;
    }
    xs->lastNode = savedLastNode;
    xs->current  = savedCurrentNode;
    return 0;
}

/*----------------------------------------------------------------------------
|   ApplyTemplate
|
\---------------------------------------------------------------------------*/
int ApplyTemplate (
    xsltState      * xs,
    xpathResultSet * context,
    domNode        * currentNode,
    domNode        * exprContext,
    int              currentPos,
    char           * mode,
    char          ** errMsg
)
{
    xsltTemplate   * tpl;
    xsltTemplate   * tplChoosen, *currentTplRule;
    domNode        * child;
    xpathResultSet   rs;
    int              rc;
    double           currentPrio, currentPrec;


    TRACE2("\n\nApplyTemplate mode='%s' currentPos=%d \n", mode, currentPos);
    DBG(printXML (currentNode, 0, 1);)
    
    /*--------------------------------------------------------------
    |   find template
    \-------------------------------------------------------------*/
    tplChoosen  = NULL;
    currentPrio = -100000.0;
    currentPrec = 0.0;
    
    
    for( tpl = xs->templates; tpl != NULL; tpl = tpl->next) {
    
        TRACE3("find tpl match='%s' mode='%s' name='%s'\n",
               tpl->match, tpl->mode, tpl->name);
        
        /* exclude those, which don't match the current mode */
        if (   ( mode && !tpl->mode)
            || (!mode &&  tpl->mode)
            || ( mode &&  tpl->mode && (strcmp(mode,tpl->mode)!=0))
        ) {
            TRACE("doesn't match mode\n");
            continue; /* doesn't match mode */
        }
        TRACE4("tpl has prio='%f' precedence='%f', currentPrio='%f', currentPrec='%f'\n", tpl->prio, tpl->precedence, currentPrio, currentPrec);
        /* According to xslt rec 5.5: First trest precedence */
        if (tpl->match &&  tpl->precedence >= currentPrec) {
            /* Higher precedence wins always. If precedences are equal,
               use priority for decision. */
            if (tpl->precedence > currentPrec ||  tpl->prio >= currentPrio) {
                
                TRACE2("testing XLocPath '%s' for node %d \n", tpl->match, currentNode->nodeNumber);
                /* Short cut for simple common cases */
                switch (tpl->ast->type) {
                case IsElement:
                    if (currentNode->nodeType == ELEMENT_NODE) {
                        if (tpl->ast->strvalue[0] != '*' 
                            && strcmp (tpl->ast->strvalue, currentNode->nodeName)!=0) 
                            continue;
                    } else continue;
                    break;
                case IsText:
                    if (currentNode->nodeType != TEXT_NODE)
                        continue;
                    break;
                case IsAttr:
                    if (currentNode->nodeType != ATTRIBUTE_NODE) 
                        continue;
                    break;
                default:
                    /* to pacify gcc -Wall */
                    ;
                }
                rc = xpathMatches ( tpl->ast, exprContext, currentNode, &(xs->cbs), errMsg);
                TRACE1("xpathMatches = %d \n", rc);
                if (rc < 0) {
                    TRACE1("xpathMatches had errors '%s' \n", *errMsg);
                    return rc;
                }
                if (rc == 0) continue;
                TRACE3("matching '%s': %f > %f ? \n", tpl->match, tpl->prio , currentPrio);
                currentPrio = tpl->prio;
                currentPrec = tpl->precedence;
                tplChoosen = tpl;
                TRACE1("TAKING '%s' \n", tpl->match);
            }
        }
    }

    if (tplChoosen == NULL) {
        TRACE("nothing matches -> execute built-in template \n");

        /*--------------------------------------------------------------------
        |   execute built-in template
        \-------------------------------------------------------------------*/
        if (currentNode->nodeType == TEXT_NODE) {
            domAppendNewTextNode(xs->lastNode,
                                 ((domTextNode*)currentNode)->nodeValue,
                                 ((domTextNode*)currentNode)->valueLength, 
                                 TEXT_NODE, 0);
            return 0;
        } else
        if (currentNode->nodeType == DOCUMENT_NODE) {
            child = ((domDocument*)currentNode)->documentElement;
        } else
        if (currentNode->nodeType == ELEMENT_NODE) {
            child = currentNode->firstChild;
        } else 
        if (currentNode->nodeType == ATTRIBUTE_NODE) {
            domAppendNewTextNode (xs->lastNode,
                                  ((domAttrNode*)currentNode)->nodeValue,
                                  ((domAttrNode*)currentNode)->valueLength,
                                  TEXT_NODE, 0);
            return 0;
        } else {
            return 0; /* for all other node we don't have to recurse deeper */
        }
        xpathRSInit( &rs );
        while (child) {    
            rsAddNodeFast ( &rs, child);
            child = child->nextSibling; 
        }
        rc = ApplyTemplates (xs, context, currentNode, currentPos, exprContext,
                             &rs, mode, errMsg);
        xpathRSFree( &rs );
        CHECK_RC;

    } else {
        TRACE1("tplChoosen '%s' \n", tplChoosen->match);
        currentTplRule = xs->currentTplRule;
        xs->currentTplRule = tplChoosen;
        DBG(printXML (tplChoosen->content->firstChild, 0, 1);)
        rc = ExecActions(xs, context, currentNode, currentPos, 
                         tplChoosen->content->firstChild, errMsg);
        TRACE1("ApplyTemplate/ExecActions rc = %d \n", rc);
        CHECK_RC;
        xs->currentTplRule = currentTplRule;
    }
    return 0;
}


/*----------------------------------------------------------------------------
|   ApplyTemplates
|
\---------------------------------------------------------------------------*/
int ApplyTemplates (
    xsltState      * xs,
    xpathResultSet * context,
    domNode        * currentNode,
    int              currentPos,
    domNode        * actionNode,
    xpathResultSet * nodeList,
    char           * mode,
    char          ** errMsg
)
{
    domNode  * savedLastNode;
    int        i, rc, needNewVarFrame = 1;
        
    if (nodeList->type == xNodeSetResult) {
        savedLastNode = xs->lastNode;
        for (i=0; i < nodeList->nr_nodes; i++) {
            if (needNewVarFrame) {
                xsltPushVarFrame (xs);
                rc = setParamVars (xs, context, currentNode, currentPos,
                                   actionNode, errMsg);
                CHECK_RC;
                xs->varFrames->polluted = 0;
            }
            rc = ApplyTemplate (xs, nodeList, nodeList->nodes[i], actionNode, i, 
                                mode, errMsg);
            CHECK_RC;
            if (xs->varFrames->polluted) {
                xsltPopVarFrame (xs);
                needNewVarFrame = 1;
            } else needNewVarFrame = 0;
        }
        if (!needNewVarFrame) {
            xsltPopVarFrame (xs);
        }
        xs->lastNode = savedLastNode;            
    } else {
        TRACE("ApplyTemplates: nodeList not a NodeSetResult !!!\n");
        DBG(rsPrint(nodeList);)
    }
    return 0;
}


/*----------------------------------------------------------------------------
|   fillElementList
|
\---------------------------------------------------------------------------*/
void fillElementList (
    xsltWSInfo   * wsinfo,
    double         precedence,
    domNode      * node,
    char         * str
)
{
    char *pc, *start, save;
    char *localName, prefix[MAX_PREFIX_LEN];
    double *f;
    int   count, hnew;
    Tcl_HashEntry *h;
    Tcl_DString dStr;
    domNS  *ns;
    
    
    count = 0;
    pc = str;
    while (*pc) {
        while (*pc == ' ') pc++;
        if (*pc == '\0') break;
        while (*pc && (*pc != ' ')) pc++;
        count++;
    }

    pc = str;
    while (*pc) {
        while (*pc == ' ') pc++;
        start = pc;
        if (*pc == '\0') break;
        while (*pc && (*pc != ' ')) pc++;
        save = *pc;
        *pc = '\0';
        wsinfo->hasData = 1;
        if (strcmp (start, "*")==0) {
            if (wsinfo->wildcardPrec < precedence) {
                wsinfo->wildcardPrec = precedence;
            }
        } else {
            domSplitQName (start, prefix, &localName);
            if (prefix[0] != '\0') {
                ns = domLookupPrefix (node, prefix);
                if (ns) {
                    if (strcmp (localName, "*")==0) {
                        h = Tcl_CreateHashEntry (&(wsinfo->NSWildcards), ns->uri,
                                                 &hnew);
                        if (!hnew) {
                            f = (double *)Tcl_GetHashValue (h);
                            if (*f < precedence) { *f = precedence; }
                        } else {
                            f = (double *) Tcl_Alloc (sizeof (double));
                            *f = precedence;
                            Tcl_SetHashValue (h, f);
                        }
                    } else {
                        Tcl_DStringInit (&dStr);
                        Tcl_DStringAppend (&dStr, ns->uri, -1);
                        Tcl_DStringAppend (&dStr, localName, -1);
                        h = Tcl_CreateHashEntry (&(wsinfo->FQNames), 
                                                 Tcl_DStringValue (&dStr), &hnew);
                        if (!hnew) {
                            f = (double *)Tcl_GetHashValue (h);
                            if (*f < precedence) { *f = precedence; }
                        } else {
                            f = (double *) Tcl_Alloc (sizeof (double));
                            *f = precedence;
                            Tcl_SetHashValue (h, f);
                        }
                        Tcl_DStringFree (&dStr);
                    }
                } else {
                    /* ??? error? */
                }
            } else {
                h = Tcl_CreateHashEntry (&(wsinfo->NCNames), start, &hnew);
                if (!hnew) {
                    f = (double *)Tcl_GetHashValue (h);
                    if (*f < precedence) { *f = precedence; }
                } else {
                    f = (double *) Tcl_Alloc (sizeof (double));
                    *f = precedence;
                    Tcl_SetHashValue (h, f);
                }
            }
        }
        *pc = save;                   
    }
}    


/*----------------------------------------------------------------------------
|   StripSpace
|
\---------------------------------------------------------------------------*/
void StripSpace (
    xsltState  * xs,
    domNode    * node
)
{
    domNode *child, *newChild, *parent;
    int     i, len, onlySpace;
    char   *p;
    
    if (node->nodeType == TEXT_NODE) {
        p = ((domTextNode*)node)->nodeValue;
        len = ((domTextNode*)node)->valueLength;
        onlySpace = 1;
        for (i=0; i<len; i++) {           
            if ((*p!=' ') && (*p!='\n') && (*p!='\r') && (*p!='\t')) {
                onlySpace = 0;
                break;
            }
            p++;
        }
        if (onlySpace) {
            if (node->parentNode && (getTag(node->parentNode) == text)) {
                /* keep white texts below xsl:text elements */
                return;
            }
            parent = node->parentNode;
            while (parent) {                
                p = getAttr(parent,"xml:space", a_space);
                if (p!=NULL) {
                    if (strcmp(p,"preserve")==0) return;
                    if (strcmp(p,"default")==0)  break;
                }
                parent = parent->parentNode;
            }
            DBG(fprintf(stderr, "removing %d(len %d) under '%s' \n", node->nodeNumber, len, node->parentNode->nodeName);)
            domDeleteNode (node, NULL, NULL);
        }
    } else 
    if (node->nodeType == ELEMENT_NODE) {
        child = node->firstChild;
        while (child) {
            newChild = child->nextSibling;
            StripSpace (xs, child);
            child = newChild;
        }
    }
}


/*----------------------------------------------------------------------------
|   addExcludeNS
|
\---------------------------------------------------------------------------*/
static int
parseList (
    xsltSubDoc  *docData,
    domNode     *xsltRoot,
    char        *str,
    char       **errMsg
    )
{
    xsltExcludeNS *excludeNS;
    char          *pc, *start, save;
    domNS         *ns;

    if (str) {
        pc = str;
        while (*pc) {
            while (*pc == ' ') pc++;
            if (*pc == '\0') break;
            start = pc;
            while (*pc && (*pc != ' ')) pc++;
            save = *pc;
            *pc = '\0';
            excludeNS = (xsltExcludeNS *) Tcl_Alloc (sizeof (xsltExcludeNS));
            excludeNS->next = docData->excludeNS;
            docData->excludeNS = excludeNS;
            if (strcmp (start, "#default")==0) {
                ns = domLookupPrefix (xsltRoot, "");
                if (!ns) {
                    *errMsg = strdup ("All prefixes listed in exclude-result-prefixes and extension-element-prefixes must be bound to a namespace.");
                    return -1;
                }
                excludeNS->uri = NULL;
            } else {
                ns = domLookupPrefix (xsltRoot, start);
                if (!ns) {
                    *errMsg = strdup ("All prefixes listed in exclude-result-prefixes and extension-element-prefixes must be bound to a namespace.");
                    return -1;
                }
                excludeNS->uri = strdup (ns->uri);
            }
            *pc = save;
        }
    }
    return 1;
}

static int
addExcludeNS (
    xsltSubDoc  *docData,
    domNode     *xsltRoot,
    char       **errMsg
    )
{
    char *str;
    int   rc;

    str = getAttr (xsltRoot, "exclude-result-prefixes", 
                   a_excludeResultPrefixes);
    rc = parseList (docData, xsltRoot, str, errMsg);
    CHECK_RC;
    
    str = getAttr (xsltRoot, "extension-element-prefixes", 
                   a_extensionElementPrefixes);
    rc = parseList (docData, xsltRoot, str, errMsg);
    CHECK_RC;
    return 1;
}

/*----------------------------------------------------------------------------
|   getExternalDocument
|
\---------------------------------------------------------------------------*/
static domDocument *
getExternalDocument (
    Tcl_Interp  *interp,
    xsltState   *xs,
    domDocument *xsltDoc,
    char        *baseURI,
    char        *href,
    char       **errMsg
    )
{
    Tcl_Obj      *cmdPtr, *resultObj, *extbaseObj, *xmlstringObj;
    Tcl_Obj      *channelIdObj, *resultTypeObj;
    int           len, mode, result, storeLineColumn;
    char         *resultType, *extbase, *xmlstring, *channelId;
    domDocument  *doc;
    xsltSubDoc   *sdoc;
    XML_Parser    parser;
    Tcl_Channel   chan;
    
    cmdPtr = Tcl_DuplicateObj (xsltDoc->extResolver);
    Tcl_IncrRefCount (cmdPtr);
    if (baseURI) {
        Tcl_ListObjAppendElement(interp, cmdPtr,
                                 Tcl_NewStringObj (baseURI,
                                                   strlen(baseURI)));
    } else {
        Tcl_ListObjAppendElement(interp, cmdPtr,
                                 Tcl_NewStringObj ("", 0));
    }
    Tcl_ListObjAppendElement (interp, cmdPtr,
                              Tcl_NewStringObj (href, strlen (href)));
    Tcl_ListObjAppendElement (interp, cmdPtr,
                              Tcl_NewStringObj ("", 0));
            
    /* result = Tcl_EvalObjEx (interp, cmdPtr,
     *                          TCL_EVAL_DIRECT | TCL_EVAL_GLOBAL);     
     *  runs on Tcl 8.0 also: 
     */
    result = Tcl_GlobalEvalObj(interp, cmdPtr);

    Tcl_DecrRefCount (cmdPtr);
    if (result != TCL_OK) {
        goto wrongScriptResult;
    }
    
    resultObj = Tcl_GetObjResult (interp);
    result = Tcl_ListObjLength (interp, resultObj, &len);
    if ((result != TCL_OK) || (len != 3)) {
        goto wrongScriptResult;
    }
    result = Tcl_ListObjIndex (interp, resultObj, 0, &resultTypeObj);
    if (result != TCL_OK) {
        goto wrongScriptResult;
    }
    resultType = Tcl_GetStringFromObj (resultTypeObj, NULL);
    if (strcmp (resultType, "string") == 0) {
        result = Tcl_ListObjIndex (interp, resultObj, 2, &xmlstringObj);
        xmlstring = Tcl_GetStringFromObj (xmlstringObj, NULL);
        len = strlen (xmlstring);
        chan = NULL;
    } else if (strcmp (resultType, "channel") == 0) {
        xmlstring = NULL;
        len = 0;
        result = Tcl_ListObjIndex (interp, resultObj, 2, &channelIdObj);
        channelId = Tcl_GetStringFromObj (channelIdObj, NULL);
        chan = Tcl_GetChannel (interp, channelId, &mode);
        if (chan == (Tcl_Channel) NULL) {
            goto wrongScriptResult;
        }
        if ((mode & TCL_READABLE) == 0) {
            *errMsg = strdup("-externalentitycommand returned a channel that wasn't opened for reading");
            return NULL;
        }
    } else if (strcmp (resultType, "filename") == 0) {
          *errMsg = strdup("-externalentitycommand result type \"filename\" not yet implemented");
          return NULL;
    } else {
        goto wrongScriptResult;
    }
    result = Tcl_ListObjIndex (interp, resultObj, 1, &extbaseObj);
    extbase = Tcl_GetStringFromObj (extbaseObj, NULL);

    sdoc = xs->subDocs;
    while (sdoc) {
        if (strcmp(sdoc->baseURI, extbase) == 0) {
            return sdoc->doc;
        }
        sdoc = sdoc->next;
    }

    if (xsltDoc->documentElement->nodeFlags & HAS_LINE_COLUMN) {
        storeLineColumn = 1;
    } else {
        storeLineColumn = 0;
    }
    
    parser = XML_ParserCreate (NULL);
    
    /* keep white space, no fiddling with the encoding (is this
       a good idea?) */
    doc = domReadDocument (parser, xmlstring, len, 0, 0, storeLineColumn, 0,
                           chan, extbase, xsltDoc->extResolver, interp);
    
    if (doc == NULL) {
        *errMsg = strdup (XML_ErrorString (XML_GetErrorCode (parser)));
        XML_ParserFree (parser);
        return NULL;
    }
    XML_ParserFree (parser);

    /* TODO: If the stylesheet use the
       literal-result-element-as-stylesheet form, rewrite it to a
       "ordinary" stylesheet with root element xsl:stylesheet, with
       one template child with match pattern "/". */

    sdoc = (xsltSubDoc*) Tcl_Alloc (sizeof (xsltSubDoc));
    sdoc->doc = doc;
    sdoc->baseURI = strdup (extbase);
    Tcl_InitHashTable (&(sdoc->keyData), TCL_STRING_KEYS);
    sdoc->excludeNS = NULL;
    if (addExcludeNS (sdoc, doc->documentElement, errMsg) < 0) {
        return NULL;
    }
    sdoc->next = xs->subDocs;
    xs->subDocs = sdoc;
    
    return doc;
    
 wrongScriptResult:
    *errMsg = strdup(Tcl_GetStringFromObj(Tcl_GetObjResult(interp), NULL));
    return NULL;
}

/*----------------------------------------------------------------------------
|   processTopLevelVars
|
\---------------------------------------------------------------------------*/
static int processTopLevelVars (
    domNode       * xmlNode,
    xsltState     * xs,
    char         ** parameters,
    char         ** errMsg
    )
{
    int                rc, i;
    char              *select, *str;
    xpathResultSet     nodeList, rs;    
    Tcl_HashEntry     *entryPtr;
    Tcl_HashSearch     search;        
    xsltTopLevelVar   *topLevelVar;
    xsltVarInProcess   varInProcess;
    xsltVariable      *var;
    Tcl_DString        dStr;
    
    xpathRSInit (&nodeList);
    rsAddNode (&nodeList, xmlNode); 
    
    if (parameters) {
        i = 0;
        while (parameters[i]) {
            entryPtr = Tcl_FindHashEntry (&xs->topLevelVars, parameters[i]);
            if (!entryPtr) {
                Tcl_DStringInit (&dStr);
                Tcl_DStringAppend (&dStr, "There isn't a parameter named \"", -1);
                Tcl_DStringAppend (&dStr, parameters[i], -1);
                Tcl_DStringAppend (&dStr, "\" defined at top level in the stylesheet.", -1);
                *errMsg = strdup (Tcl_DStringValue (&dStr));
                Tcl_DStringFree (&dStr);
                return -1;
            }
            topLevelVar = (xsltTopLevelVar *) Tcl_GetHashValue (entryPtr);
            if (!topLevelVar->isParameter) {
                Tcl_DStringInit (&dStr);
                Tcl_DStringAppend (&dStr, "\"", 1);
                Tcl_DStringAppend (&dStr, parameters[i], -1);
                Tcl_DStringAppend (&dStr, "\" is defined as variable, not as parameter.", -1);
                *errMsg = strdup (Tcl_DStringValue (&dStr));
                Tcl_DStringFree (&dStr);
                return -1;
            }
            if (xsltVarExists (xs, parameters[i])) {
                i += 2;
                continue;
            }

            xpathRSInit (&rs);
            rsSetString (&rs, parameters[i+1]);

            xs->varStackPtr++;
            if (xs->varStackPtr >= xs->varStackLen) {
                xs->varStack = (xsltVariable *) realloc (xs->varStack, 
                                                         sizeof (xsltVariable)
                                                         * 2*xs->varStackLen);
                xs->varStackLen *= 2;
            }
            var = &(xs->varStack[xs->varStackPtr]);
            if (!xs->varFramesStack->nrOfVars) {
                xs->varFramesStack->varStartIndex = xs->varStackPtr;
            }
            xs->varFramesStack->nrOfVars++;
            var->name   = strdup (parameters[i]);
            var->node   = topLevelVar->node;
            var->rs     = rs;

            i += 2;
        }
    }
    for (entryPtr = Tcl_FirstHashEntry(&xs->topLevelVars, &search);
            entryPtr != (Tcl_HashEntry*) NULL;
            entryPtr = Tcl_NextHashEntry(&search)) {
        topLevelVar = (xsltTopLevelVar *)Tcl_GetHashValue (entryPtr);
        str = Tcl_GetHashKey (&xs->topLevelVars, entryPtr);
        if (xsltVarExists (xs, str)) {
            continue;
        }
        varInProcess.name = str;
        varInProcess.next = NULL;
        xs->varsInProcess = &varInProcess;
        
        xs->currentXSLTNode = topLevelVar->node;
        select = getAttr (topLevelVar->node, "select", a_select);
        if (select) {
            rc = xsltSetVar(xs, 0, str, &nodeList, xmlNode, 0, 
                            select, NULL, 1, errMsg);
        } else {
            rc = xsltSetVar(xs, 0, str, &nodeList, xmlNode, 0,
                            NULL, topLevelVar->node->firstChild, 1, errMsg);
        }
        CHECK_RC;
    }
    xpathRSFree (&nodeList);
    xs->currentXSLTNode = NULL;
    xs->varsInProcess = NULL;
    return 0;
}

/*----------------------------------------------------------------------------
|   processTopLevel
|
\---------------------------------------------------------------------------*/
static int processTopLevel (
    Tcl_Interp    * interp,
    domNode       * xsltDocumentElement,
    domNode       * xmlNode,
    xsltState     * xs,
    double          precedence,
    double        * precedenceLowBound,
    char         ** errMsg
)
{ 
    domNode           *node;
    domDocument       *extStyleSheet;
    int                rc, hnew;
    double             childPrecedence, childLowBound;
    char              *str, *name, *match, *use, *baseURI, *href;
    xsltAttrSet       *attrSet;
    xsltKeyInfo       *keyInfo;
    xpathResultSet     nodeList;
    xsltDecimalFormat *df;
    xsltTopLevelVar   *topLevelVar;
    xsltNSAlias       *nsAlias;
    domNS             *ns, *nsFrom, *nsTo;
    Tcl_HashEntry     *h;

    xpathRSInit( &nodeList );
    rsAddNode( &nodeList, xmlNode); 

    DBG(fprintf (stderr, "start processTopLevel. precedence: %f precedenceLowBound %f\n", precedence, *precedenceLowBound);); 
    node = xsltDocumentElement->firstChild;
    while (node) {
        switch ( getTag(node) ) {
        
            case attributeSet:
                str = getAttr(node, "name", a_name);
                if (str) {
                    if (xs->attrSets) {
                        attrSet = xs->attrSets;
                        while (attrSet->next) attrSet = attrSet->next;
                        attrSet->next = (xsltAttrSet*)malloc(sizeof(xsltAttrSet));
                        attrSet = attrSet->next;
                    } else {
                        attrSet = (xsltAttrSet*)malloc(sizeof(xsltAttrSet));
                        xs->attrSets = attrSet;
                    }
                    attrSet->next    = NULL;
                    attrSet->content = node;
                    attrSet->name    = str;
                } else {
                    reportError (node, "xsl:attribute-set: missing mandatory attribute \"name\".", errMsg);
                    return -1;
                }
                break;

            case param:
                str = getAttr(node, "name", a_name);
                if (!str) {
                    reportError (node, "xsl:param: missing mandatory attribute \"name\".",
                                 errMsg);
                    return -1;
                }
                h = Tcl_CreateHashEntry (&(xs->topLevelVars), str, &hnew);
                if (!hnew) {
                    topLevelVar = (xsltTopLevelVar *)Tcl_GetHashValue (h);
                    /* Since imported stylesheets are processed at the
                       point at which they encounters the definitions are
                       already in increasing order of import precedence.
                       Therefor we have only to check, if there is a
                       top level var or parm with the same precedence */ 
                    if (topLevelVar->precedence == precedence) {
                        reportError (node, "There is already a variable or parameter with this name with the same import precedence.", errMsg);
                        return -1;
                    }
                } else {
                    topLevelVar = malloc (sizeof (xsltTopLevelVar));
                    Tcl_SetHashValue (h, topLevelVar);
                }
                topLevelVar->node = node;
                topLevelVar->isParameter = 1;
                topLevelVar->precedence = precedence;
                
                break;
                
            case decimalFormat:
                if (node->firstChild) {
                    reportError (node, "xsl:decimal-format has to be empty.", errMsg);
                    return -1;
                }
                str = getAttr(node, "name", a_name);
                if (str) {
                    /* a named decimal format */
                    df = xs->decimalFormats->next;
                    while (df) {
                        if (strcmp(df->name, str)==0) {
                            /* already existing, override it */
                            break;
                        }
                        df = df->next;
                    }
                    if (df == NULL) {
                        df = malloc(sizeof(xsltDecimalFormat));
                        df->name = strdup(str);
                        /* prepend into list of decimal format 
                           after the default one */
                        df->next = xs->decimalFormats->next;
                        xs->decimalFormats->next = df;
                    }
                } else {
                    /* definitions for default decimal format */
                    df = xs->decimalFormats;
                }
                str = getAttr(node, "decimal-separator",  a_decimalSeparator);
                str = getAttr(node, "grouping-separator", a_groupingSeparator);
                str = getAttr(node, "infinity",           a_infinity);
                str = getAttr(node, "minus-sign",         a_minusSign);
                str = getAttr(node, "NaN",                a_nan);
                str = getAttr(node, "percent",            a_percent);
                str = getAttr(node, "per-mille",          a_perMille);
                str = getAttr(node, "zero-digit",         a_zeroDigit);
                str = getAttr(node, "digit",              a_digit);
                str = getAttr(node, "pattern-separator",  a_patternSeparator);
                break;

            case import:
                if (node->firstChild) {
                    reportError (node, "xsl:import has to empty!", errMsg);
                    return -1;
                }
                if (!node->ownerDocument->extResolver) {
                    reportError (node, "need resolver Script to include Stylesheet! (use \"-externalentitycommand\")", errMsg);
                    return -1;
                }
                baseURI = findBaseURI (node);
                href = getAttr (node, "href", a_href);
                if (!href) {
                    reportError (node, "xsl:import: missing mandatory attribute \"href\".",
                                 errMsg);
                    return -1;
                }
                extStyleSheet = getExternalDocument (interp, xs,
                                                       node->ownerDocument, 
                                                       baseURI, href, errMsg);
                if (!extStyleSheet) {
                    return -1;
                }
                childPrecedence = (precedence + *precedenceLowBound) / 2;
                childLowBound = *precedenceLowBound;
                rc = processTopLevel (interp, extStyleSheet->documentElement,
                                      xmlNode, xs, childPrecedence,
                                      &childLowBound, errMsg);
                *precedenceLowBound = childPrecedence;
                if (rc != 0) {
                    return rc;
                }
                break;
                
            case include:
                if (node->firstChild) {
                    reportError (node, "xsl:include has to be empty.", errMsg);
                    return -1;
                }
                if (!node->ownerDocument->extResolver) {
                    reportError (node, "need resolver Script to include Stylesheet. (use \"-externalentitycommand\")", errMsg);
                    return -1;
                }
                baseURI = findBaseURI (node);
                href = getAttr (node, "href", a_href);
                if (!href) {
                    reportError (node, "xsl:include: missing mandatory attribute \"href\".",
                                 errMsg);
                    return -1;
                }
                extStyleSheet = getExternalDocument (interp, xs,
                                                     node->ownerDocument, 
                                                     baseURI, href, errMsg);
                if (!extStyleSheet) {
                    return -1;
                }
                xs->currentXSLTNode = extStyleSheet->documentElement;
                rc = processTopLevel (interp, extStyleSheet->documentElement,
                                      xmlNode, xs, precedence,
                                      precedenceLowBound, errMsg);
                if (rc != 0) {
                    return rc;
                }
                break;
                
            case key:
                if (node->firstChild) {
                    reportError (node, "xsl:key has to be empty.", errMsg);
                    return -1;
                }
                name = getAttr(node, "name", a_name);
                if (!name) {
                    reportError (node, "xsl:key: missing mandatory attribute \"name\".", errMsg);
                    return -1;
                }
                match = getAttr(node, "match", a_match);
                if (!match) {
                    reportError (node, "xsl:key: missing mandatory attribute \"match\".", errMsg);
                    return -1;
                }
                use = getAttr(node, "use", a_use);
                if (!use) {
                    reportError (node, "xsl:key: missing mandatory attribute \"use\".", errMsg);
                    return -1;
                }

                keyInfo = (xsltKeyInfo *) ckalloc(sizeof(xsltKeyInfo));
                keyInfo->node = node;
                rc = xpathParse (match, errMsg, &(keyInfo->matchAst), 1);
                CHECK_RC1(keyInfo);
                keyInfo->use       = use;
                rc = xpathParse (use, errMsg, &(keyInfo->useAst), 0);
                CHECK_RC1(keyInfo);
                h = Tcl_CreateHashEntry (&(xs->keyInfos), name, &hnew);
                if (hnew) {
                    keyInfo->next  = NULL;
                } else {
                    keyInfo->next  = (xsltKeyInfo *)Tcl_GetHashValue (h);
                }
                Tcl_SetHashValue (h, keyInfo);
                break;

            case namespaceAlias:
                if (node->firstChild) {
                    reportError (node, "xsl:namespace-alias has to be empty.",
                                 errMsg);
                    return -1;
                }
                
                str = getAttr (node, "stylesheet-prefix", a_stylesheetPrefix);
                if (!str) {
                    reportError (node, "xsl:namespace-alias: missing mandatory attribute \"stylesheet-prefix\".", errMsg);
                    return -1 ;
                }
                if (strcmp (str, "#default")==0) {
                    str = NULL;
                    nsFrom = domLookupPrefix (node, "");
                } else {
                    nsFrom = domLookupPrefix (node, str);
                }
                if (!nsFrom) {
                    reportError (node, "xsl:namespace-alias: no namespace bound to the \"stylesheet-prefix\".", errMsg);
                    return -1;
                }

                str = getAttr (node, "result-prefix", a_resultPrefix);
                if (!str) {
                    reportError (node, "xsl:namespace-alias: missing mandatory attribute \"result-prefix\".", errMsg);
                    return -1;
                }
                if (strcmp (str, "#default")==0) {
                    nsTo = domLookupPrefix (node, "");
                } else {
                    nsTo = domLookupPrefix (node, str);
                }
                if (!nsTo) {
                    reportError (node, "xsl:namespace-alias: no namespace bound to the \"result-prefix\".", errMsg);
                    return -1;
                }

                nsAlias = xs->nsAliases;
                while (nsAlias) {
                    if (strcmp (nsAlias->fromUri, nsFrom->uri)==0) {
                        if (nsAlias->precedence > precedence) {
                            return 0;
                        }
                        break;
                    }
                    nsAlias = nsAlias->next;
                }
                if (nsAlias) {
                    free (nsAlias->toUri);
                } else {
                    nsAlias = (xsltNSAlias *) Tcl_Alloc (sizeof (xsltNSAlias));
                    nsAlias->fromUri = strdup (nsFrom->uri);
                    nsAlias->next = xs->nsAliases;
                    xs->nsAliases = nsAlias;
                }
                nsAlias->toUri = strdup (nsTo->uri);
                break;
                
            case output:
                if (node->firstChild) {
                    reportError (node, "xsl:output has to be empty.", errMsg);
                    return -1;
                }
                str = getAttr(node, "method", a_method);
                if (str) { xs->outputMethod    = strdup(str); }
                str = getAttr(node, "encoding", a_encoding);
                if (str) { xs->outputEncoding  = strdup(str); }
                str = getAttr(node, "media-type", a_mediaType);
                if (str) { xs->outputMediaType = strdup(str); }
                str = getAttr(node, "doctype-public", a_doctypePublic);
                str = getAttr(node, "doctype-system", a_doctypeSystem);
                break;
                
            case preserveSpace:
                if (node->firstChild) {
                    reportError (node, "xsl:preserve-space has to be empty.", errMsg);
                    return -1;
                }
                str = getAttr(node, "elements", a_elements);
                if (str) {
                    fillElementList(&(xs->preserveInfo), precedence, node, str);
                } else {
                    reportError (node, "xsl:preserve-space: missing required attribute \"elements\".", errMsg);
                    return -1;
                }
                break;
                
            case stripSpace:
                if (node->firstChild) {
                    reportError (node, "xsl:strip-space has to be empty.", errMsg);
                    return -1;
                }
                str = getAttr(node, "elements", a_elements);
                if (str) {
                    fillElementList(&(xs->stripInfo), precedence, node, str);
                } else {
                    reportError (node, "xsl:strip-space: missing required attribute \"elements\".", errMsg);
                    return -1;
                }
                break;
                
            case template:
                rc = xsltAddTemplate (xs, node, precedence, errMsg);
                CHECK_RC;
                break;
                
            case variable:
                str = getAttr(node, "name", a_name);
                if (!str) {
                    reportError (node, "xsl:variable must have a \"name\" attribute.",
                                 errMsg);
                    return -1;
                }
                h = Tcl_CreateHashEntry (&(xs->topLevelVars), str, &hnew);
                if (!hnew) {
                    topLevelVar = (xsltTopLevelVar *)Tcl_GetHashValue (h);
                    /* Since imported stylesheets are processed at the
                       point at which they encounters the definitions are
                       already in increasing order of import precedence.
                       Therefor we have only to check, if there is a
                       top level var or parm with the same precedence */
                    if (topLevelVar->precedence == precedence) {
                        reportError (node, "There is already a variable or parameter with this name with the same import precedence.", errMsg);
                        return -1;
                    }
                } else {
                    topLevelVar = malloc (sizeof (xsltTopLevelVar));
                    Tcl_SetHashValue (h, topLevelVar);
                }
                topLevelVar->node = node;
                topLevelVar->isParameter = 0;
                topLevelVar->precedence = precedence;
                
                break;
                
            default:
                if (node->nodeType == ELEMENT_NODE) {
                    if (!node->namespace) {
                        reportError (node, "Top level elements must have a non-null namespace URI.", errMsg);
                        return -1;
                    }
                    if (strcmp (XSLT_NAMESPACE, domNamespaceURI (node))==0) {
                        reportError (node, "Unknown XSLT element.", errMsg);
                        return -1;
                    }
                }
                break;
        }
        node = node->nextSibling;
    }
    xpathRSFree (&nodeList);
    return 0;
}


/*----------------------------------------------------------------------------
|   xsltFreeStats
|
\---------------------------------------------------------------------------*/
static void 
xsltFreeState (
    xsltState      * xs
) {
    xsltDecimalFormat *df,  *dfsave;
    xsltKeyInfo       *ki,  *kisave;
    xsltKeyValues     *kvalues;
    xsltKeyValue      *kv,  *kvsave;
    xsltSubDoc        *sd,  *sdsave;
    xsltAttrSet       *as,  *assave;
    xsltTemplate      *tpl, *tplsave;
    xsltNumberFormat  *nf;
    ast                t;
    xsltTopLevelVar   *tlv;
    xsltNSAlias       *nsAlias, *nsAliasSave;
    xsltExcludeNS     *excludeNS, *excludeNSsave;
    Tcl_HashEntry     *entryPtr, *entryPtr1;
    Tcl_HashSearch     search, search1;        
    Tcl_HashTable     *htable;
    double            *f;

    for (entryPtr = Tcl_FirstHashEntry(&xs->xpaths, &search);
            entryPtr != (Tcl_HashEntry*) NULL;
            entryPtr = Tcl_NextHashEntry(&search)) {
        t = (ast) Tcl_GetHashValue (entryPtr);
        xpathFreeAst (t);
    }
    Tcl_DeleteHashTable(&xs->xpaths); 
    
    for (entryPtr = Tcl_FirstHashEntry(&xs->pattern, &search);
            entryPtr != (Tcl_HashEntry*) NULL;
            entryPtr = Tcl_NextHashEntry(&search)) {
        t = (ast) Tcl_GetHashValue (entryPtr);
        xpathFreeAst (t);
    }
    Tcl_DeleteHashTable(&xs->pattern); 
    
    for (entryPtr = Tcl_FirstHashEntry(&xs->formats, &search);
            entryPtr != (Tcl_HashEntry*) NULL;
            entryPtr = Tcl_NextHashEntry(&search)) {
        nf = (xsltNumberFormat *) Tcl_GetHashValue (entryPtr);
        Tcl_Free ((char *)nf->tokens);
        Tcl_Free ((char *)nf);
    }
    Tcl_DeleteHashTable(&xs->formats); 

    if (&xs->topLevelVars) {
        for (entryPtr = Tcl_FirstHashEntry(&xs->topLevelVars, &search);
             entryPtr != (Tcl_HashEntry*) NULL;
             entryPtr = Tcl_NextHashEntry(&search)) {
            tlv = (xsltTopLevelVar *) Tcl_GetHashValue (entryPtr);
            Tcl_Free ((char *)tlv);
        }
        Tcl_DeleteHashTable (&xs->topLevelVars);
    }

    /*--- free key definition information ---*/
    for (entryPtr = Tcl_FirstHashEntry (&xs->keyInfos, &search);
         entryPtr != (Tcl_HashEntry*) NULL;
         entryPtr = Tcl_NextHashEntry (&search)) {
        ki = (xsltKeyInfo *) Tcl_GetHashValue (entryPtr);
        while (ki) {
            kisave = ki;
            ki = ki->next;
            ckfree ((char*)kisave);
        }
    }
    Tcl_DeleteHashTable (&xs->keyInfos);

    /*--- free sub documents ---*/
    sd = xs->subDocs;
    while (sd && sd->next && sd->next->next) {
        sdsave = sd;
        sd = sd->next;
        free(sdsave->baseURI);
        for (entryPtr = Tcl_FirstHashEntry (&sdsave->keyData, &search);
             entryPtr != (Tcl_HashEntry*) NULL;
             entryPtr = Tcl_NextHashEntry (&search)) {
            htable = (Tcl_HashTable *) Tcl_GetHashValue (entryPtr);
            for (entryPtr1 = Tcl_FirstHashEntry (htable, &search1);
                 entryPtr1 != (Tcl_HashEntry*) NULL;
                 entryPtr1 = Tcl_NextHashEntry (&search1)) {
                kvalues = (xsltKeyValues *) Tcl_GetHashValue (entryPtr1);
                kv = kvalues->value;
                while (kv) {
                    kvsave = kv;
                    kv = kv->next;
                    Tcl_Free ((char*)kvsave);
                }
                Tcl_Free ((char*)kvalues);
            }
            Tcl_DeleteHashTable (htable);
            Tcl_Free ((char*)htable);
        }
        excludeNS = sdsave->excludeNS;
        while (excludeNS) {
            if (excludeNS->uri) free (excludeNS->uri);
            excludeNSsave = excludeNS;
            excludeNS = excludeNS->next;
            Tcl_Free ((char *)excludeNSsave);
        }
        Tcl_Free((char*)sdsave);
    }
    
    nsAlias = xs->nsAliases;
    while (nsAlias) {
        nsAliasSave = nsAlias;
        nsAlias = nsAlias->next;
        if (nsAliasSave->fromUri) free(nsAliasSave->fromUri);
        if (nsAliasSave->toUri) free(nsAliasSave->toUri);
        Tcl_Free ((char *) nsAliasSave);
    }
        
    /*--- free decimal formats ---*/
    df = xs->decimalFormats;
    while (df) {
        dfsave = df;
        df = df->next;
        if (dfsave->name) free(dfsave->name);
        free(dfsave->infinity);
        free(dfsave->NaN);
        free(dfsave);
    }
    
    /*--- free attribute sets ---*/
    as = xs->attrSets;
    while (as) {
       assave = as;
       as = as->next;
       free(assave);
    }
    
    /*--- free templates ---*/
    tpl = xs->templates;
    while (tpl) {
       tplsave = tpl;
       if (tpl->ast) xpathFreeAst (tpl->ast);
       tpl = tpl->next;
       free(tplsave);
    }
    
    for (entryPtr = Tcl_FirstHashEntry (&(xs->stripInfo.NCNames), &search);
         entryPtr != (Tcl_HashEntry*) NULL;
         entryPtr = Tcl_NextHashEntry (&search)) {
        f = (double *) Tcl_GetHashValue (entryPtr);
        Tcl_Free ((char *)f);
    }
    Tcl_DeleteHashTable (&(xs->stripInfo.NCNames));

    for (entryPtr = Tcl_FirstHashEntry (&(xs->stripInfo.FQNames), &search);
         entryPtr != (Tcl_HashEntry*) NULL;
         entryPtr = Tcl_NextHashEntry (&search)) {
        f = (double *) Tcl_GetHashValue (entryPtr);
        Tcl_Free ((char *)f);
    }
    Tcl_DeleteHashTable (&(xs->stripInfo.FQNames));

    for (entryPtr = Tcl_FirstHashEntry (&(xs->stripInfo.NSWildcards), &search);
         entryPtr != (Tcl_HashEntry*) NULL;
         entryPtr = Tcl_NextHashEntry (&search)) {
        f = (double *) Tcl_GetHashValue (entryPtr);
        Tcl_Free ((char *)f);
    }
    Tcl_DeleteHashTable (&(xs->stripInfo.NSWildcards));

    for (entryPtr = Tcl_FirstHashEntry (&(xs->preserveInfo.NCNames), &search);
         entryPtr != (Tcl_HashEntry*) NULL;
         entryPtr = Tcl_NextHashEntry (&search)) {
        f = (double *) Tcl_GetHashValue (entryPtr);
        Tcl_Free ((char *)f);
    }
    Tcl_DeleteHashTable (&(xs->preserveInfo.NCNames));

    for (entryPtr = Tcl_FirstHashEntry (&(xs->preserveInfo.FQNames), &search);
         entryPtr != (Tcl_HashEntry*) NULL;
         entryPtr = Tcl_NextHashEntry (&search)) {
        f = (double *) Tcl_GetHashValue (entryPtr);
        Tcl_Free ((char *)f);
    }
    Tcl_DeleteHashTable (&(xs->preserveInfo.FQNames));

    for (entryPtr = Tcl_FirstHashEntry (&(xs->preserveInfo.NSWildcards), &search);
         entryPtr != (Tcl_HashEntry*) NULL;
         entryPtr = Tcl_NextHashEntry (&search)) {
        f = (double *) Tcl_GetHashValue (entryPtr);
        Tcl_Free ((char *)f);
    }
    Tcl_DeleteHashTable (&(xs->preserveInfo.NSWildcards));

    free (xs->varFramesStack);
    free (xs->varStack);
    if (xs->outputMethod) free(xs->outputMethod);
    if (xs->outputEncoding) free(xs->outputEncoding);
    if (xs->outputMediaType) free(xs->outputMediaType);
}

/*----------------------------------------------------------------------------
|   xsltProcess
|
\---------------------------------------------------------------------------*/
int xsltProcess (
    domDocument       * xsltDoc,
    domNode           * xmlNode,
    char             ** parameters,
    xpathFuncCallback   funcCB,
    void              * clientData,
    char             ** errMsg,
    domDocument      ** resultDoc   
)
{ 
    xpathResultSet  nodeList;
    domNode        *node;
    int             rc;
    char           *str;
    double          precedence, precedenceLowBound;
    xsltState       xs;
    xsltSubDoc     *sdoc;
    
    *errMsg = NULL;  
    
    xmlNode = xmlNode->ownerDocument->rootNode; /* jcl: hack, should pass document */
    DBG(printXML(xmlNode, 0, 1);)

    xs.cbs.varCB           = xsltGetVar;
    xs.cbs.varClientData   = (void*)&xs;
    xs.cbs.funcCB          = xsltXPathFuncs;
    xs.cbs.funcClientData  = &xs;
    xs.orig_funcCB         = funcCB;
    xs.orig_funcClientData = clientData;
    xs.varFrames           = NULL;
    xs.varFramesStack      = (xsltVarFrame *) malloc (sizeof (xsltVarFrame) * 4);
    xs.varFramesStackPtr   = -1;
    xs.varFramesStackLen   = 4;
    xs.varStack            = (xsltVariable *) malloc (sizeof (xsltVariable) * 8);
    xs.varStackPtr         = -1;
    xs.varStackLen         = 8;
    xs.templates           = NULL;
    xs.lastTemplate        = NULL;
    xs.resultDoc           = domCreateDoc();
    xs.xmlRootNode         = xmlNode;
    xs.lastNode            = xs.resultDoc->rootNode;
    xs.attrSets            = NULL;
    xs.outputMethod        = NULL;
    xs.outputEncoding      = NULL;
    xs.outputMediaType     = NULL;
    xs.decimalFormats      = malloc(sizeof(xsltDecimalFormat));
    xs.subDocs             = NULL;
    xs.currentTplRule      = NULL;
    xs.currentXSLTNode     = NULL;
    xs.xsltDoc             = xsltDoc;
    xs.varsInProcess       = NULL;
    xs.nsAliases           = NULL;
    xs.nsUniqeNr           = 0;
    Tcl_InitHashTable ( &(xs.stripInfo.NCNames), TCL_STRING_KEYS);
    Tcl_InitHashTable ( &(xs.stripInfo.FQNames), TCL_STRING_KEYS);
    Tcl_InitHashTable ( &(xs.stripInfo.NSWildcards), TCL_STRING_KEYS);
    xs.stripInfo.wildcardPrec = 0.0;
    Tcl_InitHashTable ( &(xs.preserveInfo.NCNames), TCL_STRING_KEYS);
    Tcl_InitHashTable ( &(xs.preserveInfo.FQNames), TCL_STRING_KEYS);
    Tcl_InitHashTable ( &(xs.preserveInfo.NSWildcards), TCL_STRING_KEYS);
    xs.preserveInfo.wildcardPrec = 0.0;
    Tcl_InitHashTable ( &(xs.xpaths), TCL_STRING_KEYS);
    Tcl_InitHashTable ( &(xs.pattern), TCL_STRING_KEYS);
    Tcl_InitHashTable ( &(xs.formats), TCL_STRING_KEYS);
    Tcl_InitHashTable ( &(xs.topLevelVars), TCL_STRING_KEYS);
    Tcl_InitHashTable ( &(xs.keyInfos), TCL_STRING_KEYS);
    xs.decimalFormats->name              = NULL;
    xs.decimalFormats->decimalSeparator  = '.';
    xs.decimalFormats->groupingSeparator = ',';
    xs.decimalFormats->infinity          = strdup("inf");
    xs.decimalFormats->minusSign         = '-';
    xs.decimalFormats->NaN               = strdup("NaN");
    xs.decimalFormats->percent           = '%';
    xs.decimalFormats->zeroDigit         = '0';
    xs.decimalFormats->patternSeparator  = ';';
    xs.decimalFormats->next              = NULL;
    

    xsltPushVarFrame(&xs);
    xpathRSInit( &nodeList );
    rsAddNode( &nodeList, xmlNode); 

    node = xsltDoc->documentElement;

    /* add the xml doc to the doc list */
    sdoc = (xsltSubDoc*) Tcl_Alloc (sizeof (xsltSubDoc));
    sdoc->doc = xmlNode->ownerDocument;
    sdoc->baseURI = findBaseURI (xmlNode);
    Tcl_InitHashTable (&(sdoc->keyData), TCL_STRING_KEYS);
    sdoc->excludeNS = NULL;
    sdoc->next = xs.subDocs;
    xs.subDocs = sdoc;

    /* add the xslt doc to the doc list */
    sdoc = (xsltSubDoc*) Tcl_Alloc (sizeof (xsltSubDoc));
    sdoc->doc = xsltDoc;
    sdoc->baseURI = findBaseURI (xsltDoc->documentElement);
    Tcl_InitHashTable (&(sdoc->keyData), TCL_STRING_KEYS);
    sdoc->excludeNS = NULL;
    sdoc->next = xs.subDocs;
    xs.subDocs = sdoc;

    if ((getTag(node) != stylesheet) && (getTag(node) != transform)) {
        TRACE("no stylesheet node found --> ExecAction ");
        StripSpace (&xs, node);
        xs.currentXSLTNode = node;
        rc = ExecAction (&xs, &nodeList, xmlNode, 1, node, errMsg);
        DBG(fprintf(stderr, "ExecAction: rc=%d \n", rc);)
        CHECK_RC;
        xsltPopVarFrame (&xs);
        xpathRSFree( &nodeList );
        xs.resultDoc->documentElement = xs.resultDoc->rootNode->firstChild;
        xs.resultDoc->nodeFlags |= OUTPUT_DEFAULT_HTML;
        *resultDoc = xs.resultDoc;
        xsltFreeState (&xs);
        return 0;
    } else {
        str = getAttr (node, "version", a_version);
        if (!str) {
            reportError (node, "missing mandatory attribute \"version\".", errMsg);
            return -1;
        }
        rc = addExcludeNS (sdoc, node, errMsg);
        if (rc < 0) {
            xsltFreeState (&xs);
            return rc;
        }
    }
    
    precedence = 1.0;
    precedenceLowBound = 0.0;
    rc = processTopLevel (clientData, node, xmlNode, &xs, precedence,
                          &precedenceLowBound, errMsg);
    if (rc != 0) {
        xsltFreeState (&xs);
        return rc;
    }
    

    /*  strip space, if allowed, from the XSLT documents,
     *  but not from the XML document (last one in list)
     */
    sdoc = xs.subDocs;
#if 1  
    while (sdoc && sdoc->next) {
#else
    while (sdoc) {
#endif    
        StripSpace (&xs, sdoc->doc->documentElement);
        sdoc = sdoc->next;
    }
    if (xs.stripInfo.hasData) {
        StripXMLSpace (&xs, sdoc->doc->documentElement);
    }

    rc = processTopLevelVars (xmlNode, &xs, parameters, errMsg);
    if (rc != 0) {
        xsltFreeState (&xs);
        return rc;
    }

    rc = ApplyTemplates (&xs, &nodeList, xmlNode, 0, node, &nodeList, NULL,
                         errMsg);
    if (rc != 0) {
        xsltFreeState (&xs);
        return rc;
    }

    xsltPopVarFrame (&xs);
    xpathRSFree( &nodeList );

    /* Rudimentary xsl:output support */
    if (xs.outputMethod) {
        if (strcmp (xs.outputMethod, "xml")==0) {
            xs.resultDoc->nodeFlags |= OUTPUT_DEFAULT_XML;
        } else 
        if (strcmp (xs.outputMethod, "html")==0) {
            xs.resultDoc->nodeFlags |= OUTPUT_DEFAULT_HTML;
        } else 
        if (strcmp (xs.outputMethod, "text")==0) {
            xs.resultDoc->nodeFlags |= OUTPUT_DEFAULT_TEXT;
        } else {
            xs.resultDoc->nodeFlags |= OUTPUT_DEFAULT_UNKOWN;
        }
    } else {
        /* default output method */
        node = xs.resultDoc->rootNode->firstChild;
        if (node) {
            if (node->nodeType == TEXT_NODE) {
                char *pc;
                int   i, only_whites;

                only_whites = 1;
                for (i=0, pc = ((domTextNode*)node)->nodeValue; 
                     i < ((domTextNode*)node)->valueLength; 
                     i++, pc++) {
                    if ( (*pc != ' ')  &&
                         (*pc != '\t') &&
                         (*pc != '\n') &&
                         (*pc != '\r') ) {
                        only_whites = 0;
                        break;
                    }
                }
                if (only_whites && node->nextSibling) node = node->nextSibling;
            }
            if (node->nodeType == ELEMENT_NODE) {
                if (STRCASECMP(node->nodeName, "html")==0) {
                    xs.resultDoc->nodeFlags |= OUTPUT_DEFAULT_HTML;
                } else {
                    xs.resultDoc->nodeFlags |= OUTPUT_DEFAULT_XML;
                }
            } else {
                xs.resultDoc->nodeFlags |= OUTPUT_DEFAULT_XML;
            }
        }
    }
    xs.resultDoc->documentElement = xs.resultDoc->rootNode->firstChild;
    *resultDoc = xs.resultDoc;

    xsltFreeState (&xs);
    return 0;

} /* xsltProcess */

