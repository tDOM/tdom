/*----------------------------------------------------------------------------
|   Copyright (c) 1999-2001 Jochen Loewer (loewerj@hotmail.com)
|-----------------------------------------------------------------------------
|
|   $Header$
|
|
|   A (partial) XPath implementation (lexer/parser/evaluator) for tDOM,
|   the DOM implementation for Tcl.
|   Based on November 16 1999 Recommendation of the W3C
|   (http://www.w3.org/TR/1999/REC-xslt-19991116) 
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
|   Portions created by Jochen Loewer are Copyright (C) 1999 - 2001
|   Jochen Loewer. All Rights Reserved.
|
|   Contributor(s):
|       April00  Rolf Ade   Add support for following/preceding/
|                           precedingSibling axis plus several
|                           bug fixes
|
|       Aug00    Rolf Ade   Rewrite of comparisons plus several
|                           bug fixes/reports
|
|       Aug01    Rolf Ade   id(), unparsed-entity(), lang(), fixes
|
|   written by Jochen Loewer
|   July, 1999
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


/*----------------------------------------------------------------------------
|   Macros
|
\---------------------------------------------------------------------------*/
#define JDBG(x)          
#define DBG(x)           
#define DDBG(x)          
#define TRACE(x)         DDBG(fprintf(stderr,(x)))
#define TRACE1(x,a)      DDBG(fprintf(stderr,(x),(a)))
#define TRACE2(x,a,b)    DDBG(fprintf(stderr,(x),(a),(b)))
#define TRACE3(x,a,b,c)  DDBG(fprintf(stderr,(x),(a),(b),(c)))

#define INITIAL_SIZE     100

#define ADD_TOKEN(t)  if ((l+1)>=allocated) {                               \
                          tokens=(XPathTokens)realloc(tokens, 2*allocated   \
                                                      *sizeof(XPathToken)); \
                          allocated = allocated * 2;                        \
                      }                                                     \
                      tokens[l].token     = (t);                            \
                      tokens[l++].pos     = i;                              \
                      tokens[l].token     = EOS;                            \
                      tokens[l].strvalue  = NULL;                           \
                      tokens[l].intvalue  = 0;                              \
                      tokens[l].realvalue = 0.0;


#define DeclProduction(name) static ast name (int *l,XPathTokens tokens,char **errMsg)

#define Production(name)  static ast name (int *l,XPathTokens tokens,char **errMsg) \
                          { char *__func = #name;                                   \
                            ast a = NULL;                                           \
                            TRACE2("\nProduction "#name": start l=%d next:%s\n",    \
                                                *l,token2str[tokens[*l].token]);

#define EndProduction       TRACE3("EndProd %s: start l=%d next:%s\n",    \
                                __func, *l,token2str[tokens[*l].token]);  \
                            DDBG(printAst(0,a);)                          \
                            return a;                                     \
                          }

#define LA                tokens[*l].token
#define LA2               tokens[*l+1].token
#define LA3               tokens[*l+2].token
/* #define Recurse(p)        rc=p(l,tokens,errMsg);if(rc==NULL)return rc;*/
#define Recurse(p)        p(l,tokens,errMsg)

#define Consume(tk)        if (tokens[*l].token == tk) {      \
                              TRACE2("Production %s:   %s consumed\n", \
                                 __func, token2str[tokens[*l].token]); \
                              (*l)++;                         \
                          } else {                            \
                              ErrExpected(#tk);               \
                              return NULL;                    \
                          }
#define STRVAL            tokens[(*l)-1].strvalue
#define INTVAL            tokens[(*l)-1].intvalue
#define REALVAL           tokens[(*l)-1].realvalue
#define NEWCONS           ((ast)malloc(sizeof(astElem)))

#define IS_STR(c,s)       (c==*(tokens[(*l)-1].strvalue))&&(strcmp(tokens[(*l)-1].strvalue,s)==0)
#define IS_FUNC(c,s)      ((*(step->strvalue)==(c)) && (strcmp((s),step->strvalue)==0))


#define ErrExpected(msg)  *errMsg = (char*)malloc(255);        \
                          **errMsg = '\0';                     \
                          strcpy(*errMsg, __func);             \
                          strcat(*errMsg, ": Expected " #msg); \
                          return NULL;

#define CHECK_RC          if (rc) return rc
#define checkRsAddNode(rs,node)    if (useFastAdd) rsAddNodeFast( rs,node); \
                                   else rsAddNode (rs,node);

/*----------------------------------------------------------------------------
|   Types for Lexer
|
\---------------------------------------------------------------------------*/
typedef enum {
    LPAR, RPAR, LBRACKET, RBRACKET, DOT, DOTDOT, ATTRIBUTEPREFIX, ATTRIBUTE,
    COMMA,  COLONCOLON, LITERAL, NSPREFIX, NSWC, INTNUMBER, REALNUMBER,
    SLASH, SLASHSLASH,
    PIPE, PLUS, MINUS, EQUAL, NOTEQ, LT, LTE, GT, GTE,
    AND, OR, MOD, DIV, MULTIPLY, FUNCTION, VARIABLE,
    WCARDNAME, COMMENT, TEXT, PINSTR, NODE, AXISNAME, STAR, EOS
} Token;

static char *token2str[] = {
    "LPAR", "RPAR", "LBRACKET", "RBRACKET", "DOT", "DOTDOT", "ATTRIBUTEPREFIX", "ATTRIBUTE",
    "COMMA", "COLONCOLON", "LITERAL", "NSPREFIX", "NSWC", "INTNUMBER", "REALNUMBER",
    "SLASH", "SLASHSLASH",
    "PIPE", "PLUS", "MINUS", "EQUAL", "NOTEQ", "LT", "LTE", "GT", "GTE",
    "AND", "OR", "MOD", "DIV", "MULTIPLY", "FUNCTION", "VARIABLE",
    "WCARDNAME", "COMMENT", "TEXT", "PI", "NODE", "AXISNAME", "STAR", "EOS"
};


typedef struct {

    Token  token;
    char  *strvalue;
    int    intvalue;
    double realvalue;
    int    pos;

} XPathToken;

typedef XPathToken *XPathTokens;


/*----------------------------------------------------------------------------
|   Types for abstract syntax trees
|
\---------------------------------------------------------------------------*/
static char *astType2str[] = {
    "Int", "Real", "Str", "Mult", "Div", "Mod", "UnaryMinus", "IsNSElement",
    "IsNode", "IsComment", "IsText", "IsPI", "IsSpecificPI", "IsElement",
    "IsFQElement", "GetVar", "Literal", "ExecFunction", "Pred", "EvalSteps",
    "SelectRoot", "CombineSets", "Add", "Substract", "Less", "LessOrEq",
    "Greater", "GreaterOrEq", "Equal", "NotEqual", "And", "Or", "IsNSAttr", "IsAttr",
    "AxisAncestor", "AxisAncestorOrSelf", "AxisAttribute", "AxisChild",
    "AxisDescendant", "AxisDescendantOrSelf", "AxisFollowing",
    "AxisFollowingSibling", "AxisNamespace", "AxisParent",
    "AxisPreceding", "AxisPrecedingSilbing", "AxisSelf",
    "GetContextNode", "GetParentNode", "AxisDescendantOrSelfLit",
    "AxisDescendantLit",
    
    "CombinePath", "IsRoot", "ToParent", "ToAncestors", "FillNodeList",
    "FillWithCurrentNode",
    "ExecIdKey"
};

/*----------------------------------------------------------------------------
|   functionTag
|
\---------------------------------------------------------------------------*/
typedef enum {

    f_unknown = 1,
    f_boolean, f_ceiling, f_concat, f_contains, f_count, f_false, f_floor,
    f_generateId, f_id, f_lang, f_last, f_localName, f_name, f_namespaceUri,
    f_normalizeSpace, f_not, f_number, f_position, f_round, f_startsWith,
    f_string, f_stringLength, f_substring, f_substringAfter,
    f_substringBefore, f_sum, f_translate, f_true, f_unparsedEntityUri
    
} functionTag;

/*----------------------------------------------------------------------------
|   Prototypes / Forwards
|
\---------------------------------------------------------------------------*/
DeclProduction(OrExpr);
DeclProduction(Predicate);
DeclProduction(RelativeLocationPath);
DeclProduction(AbsoluteLocationPath);

char *xpathFuncString (xpathResultSet  *rs );
static int xpathEvalStep (ast step, domNode *ctxNode, domNode *exprContext,
                          int position, xpathResultSet *nodeList,
                          xpathCBs *cbs, xpathResultSet *result,
                          int *docOrder, char **errMsg);

int dbonerow = 0;

#ifndef isnan
int isnan(double number) { return (!(number == number)); }
#endif

/*----------------------------------------------------------------------------
|   xpath result set functions
|
\---------------------------------------------------------------------------*/
/*  void xpathRSInit ( xpathResultSet *rs ) { */

/*      rs->type       = EmptyResult; */
/*      rs->realvalue  = 0.0; */
/*      rs->nodes      = NULL; */
/*      rs->string     = NULL; */
/*      rs->allocated  = 0; */
/*      rs->nr_nodes   = 0; */
/*      rs->string_len = 0; */
/*      rs->intvalue   = 0; */
/*  } */
void xpathRSFree ( xpathResultSet *rs ) {

    if (rs->type == xNodeSetResult) {
        if (rs->nodes) free(rs->nodes);
        rs->nodes     = NULL;
        rs->nr_nodes  = 0;
        rs->allocated = 0;
    }
    if (rs->type == StringResult) {
        if (rs->string) free(rs->string);
        rs->string     = NULL;
        rs->string_len = 0;
    }
    rs->type = EmptyResult;
}
void rsPrint ( xpathResultSet *rs ) {
    int i = 0,l;  char tmp[80];
    switch (rs->type) {
        case EmptyResult:
             fprintf(stderr, "empty result \n");
             break;

        case BoolResult:
             fprintf(stderr, "boolean result: %d \n", rs->intvalue);
             break;

        case IntResult:
             fprintf(stderr, "int result: %d \n", rs->intvalue);
             break;

        case RealResult:
             fprintf(stderr, "real result: %f \n", rs->realvalue);
             break;

        case StringResult:
             fprintf(stderr, "string result: -%*s-\n", rs->string_len,
                                                       rs->string);
             break;

        case xNodeSetResult:
             if (!i) fprintf(stderr,"nodeSet result (len %d):\n",rs->nr_nodes);
             for (i=0; i<rs->nr_nodes; i++) {
                 if (rs->nodes[i]->nodeType == ELEMENT_NODE) {
                     fprintf(stderr, "%2d domNode%d %s ",
                             i, rs->nodes[i]->nodeNumber, rs->nodes[i]->nodeName);
                     if (rs->nodes[i]->firstChild && 
                         rs->nodes[i]->firstChild->nodeType == TEXT_NODE) 
                     {
                         l = ((domTextNode*)rs->nodes[i]->firstChild)->valueLength;
                         if (l > 25) l = 25;
                         memcpy(tmp, ((domTextNode*)rs->nodes[i]->firstChild)->nodeValue, l);
                         tmp[l] = '\0';
                         fprintf(stderr, "'%s'", tmp);
                     }
                     fprintf(stderr, "\n");
                 } else
                 if (rs->nodes[i]->nodeType == TEXT_NODE) {
                     l = ((domTextNode*)rs->nodes[i])->valueLength;
                     if (l > 60) l = 60;
                     memcpy(tmp, ((domTextNode*)rs->nodes[i])->nodeValue, l);
                     tmp[l] = '\0';
                     fprintf(stderr, "%2d domNode%d text:'%s' \n",
                             i, rs->nodes[i]->nodeNumber, tmp);
                 } else
                 if (rs->nodes[i]->nodeType == COMMENT_NODE) {
                     l = ((domTextNode*)rs->nodes[i])->valueLength;
                     memcpy (tmp, "<!--", 4);
                     if (l > 60) l = 60;
                     memcpy(&tmp[4], ((domTextNode*)rs->nodes[i])->nodeValue, l);
                     memcpy(&tmp[4+l], "-->", 3);
                     tmp[7+l] = '\0';
                     fprintf(stderr, "%2d domNode%d text:'%s' \n",
                             i, rs->nodes[i]->nodeNumber, tmp);
                 } else
                 if (rs->nodes[i]->nodeType == ATTRIBUTE_NODE) {
                     fprintf(stderr, "%2d Attr %s='%*s'\n", i,
                             ((domAttrNode*)rs->nodes[i])->nodeName,
                             ((domAttrNode*)rs->nodes[i])->valueLength,
                             ((domAttrNode*)rs->nodes[i])->nodeValue);
                 }
             }
             break;
         default:
             fprintf (stderr, "unknown result type: '%d'!!!\n", rs->type);
             break;

    }
}
void rsSetReal ( xpathResultSet *rs, double d) {

    rs->type = RealResult;
    rs->realvalue = d;
}
void rsSetInt ( xpathResultSet *rs, int i) {

    rs->type = IntResult;
    rs->intvalue = i;
}
void rsSetBool ( xpathResultSet *rs, int i) {

    rs->type = BoolResult;
    rs->intvalue = (i ? 1 : 0);
}
void rsSetString ( xpathResultSet *rs, char *s) {

    rs->type = StringResult;
    if (s) {
        rs->string     = strdup(s);
        rs->string_len = strlen(s);
    } else {
        rs->string     = strdup("");
        rs->string_len = 0;
    }
    rs->nr_nodes = 0;
}
void rsAddNode ( xpathResultSet *rs, domNode *node) {

    if ((rs->type != EmptyResult) && (rs->type != xNodeSetResult)) {
        fprintf(stderr, "could not add node to non NodeSetResult xpathResultSet!"); return;
    }
    if (rs->type == EmptyResult) {

        rs->type      = xNodeSetResult;
        rs->nodes     = (domNode**)malloc(INITIAL_SIZE * sizeof(domNode*) );
        rs->allocated = INITIAL_SIZE;
        rs->nr_nodes  = 1;
        rs->nodes[0]  = node;

    } else {
    
        int i;

        /* is node already in result set ? */
        for (i=0; i<rs->nr_nodes; i++) {
            if (rs->nodes[i] == node) {
                return;
            }
        }
        
        if ((rs->nr_nodes+1) >= rs->allocated) {
            rs->nodes = (domNode**)realloc( (void*)rs->nodes,
                                            2 * rs->allocated * sizeof(domNode*) );
            rs->allocated = rs->allocated * 2;
        }
        rs->nodes[rs->nr_nodes++] = node;
    }
}

void rsAddNodeFast ( xpathResultSet *rs, domNode *node) {

    if ((rs->type != EmptyResult) && (rs->type != xNodeSetResult)) {
        fprintf(stderr, "could not add node to non NodeSetResult xpathResultSet!"); return;
    }
    if (rs->type == EmptyResult) {

        rs->type      = xNodeSetResult;
        rs->nodes     = (domNode**)malloc(INITIAL_SIZE * sizeof(domNode*) );
        rs->allocated = INITIAL_SIZE;
        rs->nr_nodes  = 1;
        rs->nodes[0]  = node;

    } else {
        if ((rs->nr_nodes+1) >= rs->allocated) {
            rs->nodes = (domNode**)realloc( (void*)rs->nodes,
                                            2 * rs->allocated * sizeof(domNode*) );
            rs->allocated = rs->allocated * 2;
        }
        rs->nodes[rs->nr_nodes++] = node;
    }
}

void rsCopy ( xpathResultSet *to, xpathResultSet *from ) {

    int i;

    to->type       = from->type;
    to->intvalue   = from->intvalue;
    to->realvalue  = from->realvalue;
    if (to->type == StringResult) {
        to->string     = strdup(from->string);
        to->string_len = from->string_len;
    }
    if (to->type == xNodeSetResult) {
        to->nr_nodes = from->nr_nodes;
        to->nodes = (domNode**)malloc(from->nr_nodes * sizeof(domNode*) );
        for (i=0; i<from->nr_nodes; i++)
            to->nodes[i] = from->nodes[i];
    } 
}


/*----------------------------------------------------------------------------
|   AST construct functions
|
\---------------------------------------------------------------------------*/
static ast New( astType type ) {
    ast t = NEWCONS;
    t->type = type;
    t->next = t->child = NULL;
    t->strvalue  = NULL;
    t->intvalue  = 0;
    t->realvalue = 0.0;
    return t;
}
static ast New1( astType type, ast a) {
    ast t = NEWCONS;
    t->type = type;
    t->next = NULL;
    t->child = a;
    t->strvalue  = NULL;
    t->intvalue  = 0;
    t->realvalue = 0.0;
    return t;
}
static ast New1WithEvalSteps( astType type, ast a) {
    ast t = NEWCONS;
    t->type = type;
    t->next = NULL;
    if (a && a->next) {
        t->child =  New1(EvalSteps,a);
    } else {
        t->child =  a;
    }
    t->strvalue  = NULL;
    t->intvalue  = 0;
    t->realvalue = 0.0;
    return t;
}
static ast New2( astType type, ast a, ast b ) {
    ast t = NEWCONS;

    t->type      = type;
    t->next      = NULL;
    t->strvalue  = NULL;
    t->intvalue  = 0;
    t->realvalue = 0.0;

    if (a && a->next) {
        t->child =  New1(EvalSteps,a);
    } else {
        t->child =  a;
    }
    if (b && b->next) {
        t->child->next = New1(EvalSteps, b);
    } else {
        t->child->next = b;
    }
    return t;
}

static ast NewInt( int i ) {
    ast t = NEWCONS;

    t->type      = Int;
    t->strvalue  = NULL;
    t->intvalue  = i;
    t->realvalue = 0.0;

    t->next = t->child = NULL;
    return t;
}
static ast NewReal( double r ) {
    ast t = NEWCONS;

    t->type      = Real;
    t->strvalue  = NULL;
    t->intvalue  = 0;
    t->realvalue = r;

    t->next = t->child = NULL;
    return t;
}
static ast NewStr( astType type, char *str ) {
    ast t = NEWCONS;

    t->type      = type;
    t->strvalue  = strdup(str);
    t->intvalue  = 0;
    t->realvalue = 0.0;

    t->next = t->child = NULL;
    return t;
}
static ast Append( ast m, ast n ) {
    if (!n) return NULL;
    if (!m) return NULL;

    while (m->next != NULL) m = m->next;
    m->next = n;
    return m;
}
static ast AddChild( ast m, ast child ) {
    if (!child) return NULL;
    if (!m)     return NULL;

    if (m->child == NULL) {
        m->child = child;
    } else {
        ast c = m->child;
        while (c->next != NULL) c = c->next;
        c->next = child;
    }
    return m;
}
static ast AddChildWithEvalSteps( ast m, ast child ) {
    if (!child) return NULL;
    if (!m)     return NULL;

    if (child->next) {
        child =  New1(EvalSteps, child);
    }

    if (m->child == NULL) {
        m->child = child;
    } else {
        ast c = m->child;
        while (c->next != NULL) c = c->next;
        c->next = child;
    }
    /*child->next = NULL;*/
    return m;
}
static void freeAst (ast t)
{
    ast tmp;

    while (t) {
        tmp = t->next;
        if (t->strvalue) free(t->strvalue);
        if (t->child) freeAst (t->child);
        free(t);
        t = tmp;
    }
}
void printAst (int depth, ast t)
{
    int i;

    while (t) {
        for (i=0; i<depth; i++) fprintf(stderr, "   ");
        fprintf(stderr, "%s ", astType2str[t->type]);
        switch (t->type) {

            case Int :        fprintf(stderr, "%d", t->intvalue);   break;
            case Real:        fprintf(stderr, "%f", t->realvalue);  break;
            case IsElement:
            case IsFQElement:    
            case IsNSAttr:
            case IsAttr:
            case ExecFunction:
            case Literal:
            case GetVar:      fprintf(stderr, "'%s'", t->strvalue); break;

            default: break;
        }
        fprintf(stderr, "\n");
        if (t->child) printAst (depth+1, t->child);
        t = t->next;
    }
}




/*----------------------------------------------------------------------------
|   xpathFreeAst
|
\---------------------------------------------------------------------------*/
void xpathFreeAst(
    ast t
)
{
    freeAst(t);
}


/*----------------------------------------------------------------------------
|   xpathLexer
|
\---------------------------------------------------------------------------*/
static XPathTokens xpathLexer (
    char  *xpath,
    char **errMsg
)
{
    int  l, allocated;
    int  i, k, start;
    char delim, *ps, save;
    XPathTokens tokens;
    int token = EOS;


    tokens = (XPathTokens)malloc( INITIAL_SIZE * sizeof(XPathToken) );
    if (tokens == NULL) {
        *errMsg = (char*)strdup("Unable to alloc initial memory!");
        return NULL;
    }
    allocated = INITIAL_SIZE;
    l = 0;
    tokens[l].token     = EOS;
    tokens[l].strvalue  = NULL;
    tokens[l].intvalue  = 0;
    tokens[l].realvalue = 0.0;

    i = 0;
    while (xpath[i]) {
        switch (xpath[i]) {

            case ' ' :
            case '\n':
            case '\r':
            case '\t': i++; continue;

            case '(':  token = LPAR;     break;
            case ')':  token = RPAR;     break;
            case '[':  token = LBRACKET; break;
            case ']':  token = RBRACKET; break;

            case '@':  i++;
                       if ( isNCNameStart (&xpath[i]) ) {
                           ps = &(xpath[i]);
                           i += UTF8_CHAR_LEN (xpath[i]);
                           while (xpath[i] && isNCNameChar (&xpath[i]))
                               i += UTF8_CHAR_LEN (xpath[i]);
                           save = xpath[i];
                           xpath[i] = '\0';
                           tokens[l].strvalue = (char*)strdup(ps);
                           xpath[i] = save;
                           if (save == ':' && xpath[i+1] != ':') {
                               token = ATTRIBUTEPREFIX;
                               ADD_TOKEN (token);
                               if (xpath[i+1] == '*') {
                                   token = ATTRIBUTE;
                                   tokens[l].strvalue = strdup("*");
                                   i++;
                               } else {
                                   ps = &(xpath[++i]);
                                   if (!(isNCNameStart (&xpath[i]))) {
                                       *errMsg = strdup ("Illegal attribute name");
                                       return NULL;
                                   }
                                   i += UTF8_CHAR_LEN (xpath[i]);
                                   while (xpath[i] && isNCNameChar (&xpath[i]))
                                       i += UTF8_CHAR_LEN (xpath[i]);
                                   save = xpath[i];
                                   xpath[i] = '\0';
                                   token = ATTRIBUTE;
                                   tokens[l].strvalue = (char*)strdup(ps);
                                   xpath[i--] = save;
                               }
                           } else {
                               save = xpath[i];
                               xpath[i] = '\0';
                               tokens[l].strvalue = (char*)strdup(ps);
                               xpath[i--] = save;
                               token = ATTRIBUTE;
                           }
                       } else if (xpath[i]=='*') {
                           tokens[l].strvalue = (char*)strdup("*");
                           token = ATTRIBUTE;
                       } else {
                           *errMsg = (char*)strdup("Expected attribute name");
                           return NULL;
                       }; break;

            case ',':  token = COMMA; break;

            case ':':  if (xpath[i+1] == ':') {
                           token = COLONCOLON;
                           i++;
                       } else {
                           /* ??? */
                           token = LITERAL;
                       }; break;

            case '"' :
            case '\'': delim = xpath[i]; start = ++i;
                       while (xpath[i] && (xpath[i] != delim)) i++;
                       if (!xpath[i]) {
                           *errMsg = (char*)strdup("Undetermined string");
                           return NULL;
                       }
                       xpath[i] = '\0'; /* terminate string */
                       tokens[l].strvalue = (char*)strdup(&xpath[start]);
                       token = LITERAL;
                       xpath[i] = delim;
                       break;

            case '/':  if (xpath[i+1] == '/') {
                           token = SLASHSLASH;
                           i++;
                       } else {
                           token = SLASH;
                       };
                       break;

            case '|':  token = PIPE;     break;
            case '+':  token = PLUS;     break;
            case '-':  token = MINUS;    break;
            case '=':  token = EQUAL;    break;
            case '!':  if (xpath[i+1] == '=') {
                           token = NOTEQ;
                           i++;
                       } else {
                           /* ??? */
                           token = LITERAL;
                       }; break;

            case '<':  if (xpath[i+1] == '=') {
                           token = LTE;
                           i++;
                       } else {
                           token = LT;
                       };break;

            case '>':  if (xpath[i+1] == '=') {
                           token = GTE;
                           i++;
                       } else {
                           token = GT;
                       }; break;


            case '*': if ((l>0)
                         && (tokens[l-1].token != COLONCOLON)
                         && (tokens[l-1].token != LPAR)
                         && (tokens[l-1].token != LBRACKET)
                         && (tokens[l-1].token != COMMA)
                         && (tokens[l-1].token != SLASH)
                         && (tokens[l-1].token != SLASHSLASH)
                      ) {
                          token = MULTIPLY;
                      } else {
                          token = WCARDNAME;
                          tokens[l].strvalue = (char*)strdup("*");
                      }; break;

            case '$':  i++;
                       if ( isNameStart (&xpath[i])) {
                           ps = &(xpath[i]);
                           i += UTF8_CHAR_LEN (xpath[i]);
                           while (xpath[i] && isNameChar(&xpath[i]))
                               i +=  UTF8_CHAR_LEN(xpath[i]);
                           save = xpath[i];
                           xpath[i] = '\0';
                           tokens[l].strvalue = (char*)strdup(ps);
                           xpath[i--] = save;
                           token = VARIABLE;
                       } else {
                           *errMsg = (char*)strdup("Expected variable name");
                           return NULL;
                       }; break;

            case '.':  if (xpath[i+1] == '.') {
                           token = DOTDOT;
                           i++;
                           break;
                       } else if (!isdigit(xpath[i+1])) {
                           token = DOT;
                           break;
                       }
                       /* DOT followed by digit, ie a REAL.
                          Handled by default. Fall throu */

            default:   if ( isNCNameStart (&xpath[i])) {
                           ps = &(xpath[i]);
                           i += UTF8_CHAR_LEN (xpath[i]);
                           while (xpath[i] && isNCNameChar(&xpath[i])) {
                               i += UTF8_CHAR_LEN(xpath[i]);
                           }

                           k = i;
                           if (xpath[i] == ':') {
                               if (xpath[i+1] == '*') {
                                   save = xpath[i];
                                   xpath[i] = '\0'; /* terminate */
                                   token = NSWC;
                                   tokens[l].strvalue = strdup (ps);
                                   xpath[i] = save;
                                   i++;
                                   break;
                               }
                               if (xpath[i+1] != ':') {
                                   k++;
                                   if ((xpath[k] ==  ' ') ||
                                       (xpath[k] == '\n') ||
                                       (xpath[k] == '\r') ||
                                       (xpath[k] == '\t')) {
                                       *errMsg = strdup("whitespace after namespace prefix");
                                       return NULL;
                                   }
                                   save = xpath[i];
                                   xpath[i] = '\0'; /* terminate */
                                   token = NSPREFIX;
                                   tokens[l].strvalue = strdup (ps);
                                   xpath[i] = save;
                                   break;
                               }
                           }
                           /* read over white space */
                           while ((xpath[k] == ' ')  ||
                                  (xpath[k] == '\n') ||
                                  (xpath[k] == '\r') ||
                                  (xpath[k] == '\t')   )  k++;
                                  
                           if (xpath[k]=='(') {
                               save = xpath[i];
                               xpath[i] = '\0'; /* terminate */
                               if (strcmp(ps,"text")==0) {
                                   token = TEXT;
                               } else if (strcmp(ps,"node")==0) {
                                   token = NODE;
                               } else if (strcmp(ps,"comment")==0) {
                                   token = COMMENT;
                               } else if (strcmp(ps,"processing-instruction")==0) {
                                   token = PINSTR;
                               } else {
                                   if ((save!='(') && (strcmp(ps,"and")==0)) token = AND;
                                   else
                                   if ((save!='(') && (strcmp(ps,"or")==0))  token = OR;
                                   else
                                   if ((save!='(') && (strcmp(ps,"mod")==0)) token = MOD;
                                   else
                                   if ((save!='(') && (strcmp(ps,"div")==0)) token = DIV;
                                   else {
                                       token = FUNCTION;
                                       tokens[l].strvalue = (char*)strdup(ps);
                                   }
                               }
                               xpath[i] = save;
                           } else if ((xpath[k]==':') && (xpath[k+1]==':')) {
                               token = AXISNAME;
                               save = xpath[i];
                               xpath[i] = '\0'; /* terminate */
                               tokens[l].strvalue = (char*)strdup(ps);
                               xpath[i] = save;
                           } else {
                               save = xpath[i];
                               xpath[i] = '\0';
                               if ((l>0)
                                   && (tokens[l-1].token != COLONCOLON)
                                   && (tokens[l-1].token != LPAR)
                                   && (tokens[l-1].token != LBRACKET)
                                   && (tokens[l-1].token != COMMA)
                                   && (tokens[l-1].token != SLASH)
                                   && (tokens[l-1].token != SLASHSLASH)
                                   && (tokens[l-1].token != PLUS)
                                   && (tokens[l-1].token != MINUS)
                                   && (tokens[l-1].token != EQUAL)
                                   && (tokens[l-1].token != NOTEQ)
                                   && (tokens[l-1].token != LT)
                                   && (tokens[l-1].token != LTE)
                                   && (tokens[l-1].token != GT)
                                   && (tokens[l-1].token != GTE)
                                   && (tokens[l-1].token != AND)
                                   && (tokens[l-1].token != OR)
                                   && (tokens[l-1].token != MOD)
                                   && (tokens[l-1].token != DIV)
                                   && (tokens[l-1].token != MULTIPLY)
                               ) {
                                   if (strcmp(ps,"and")==0) {
                                       token = AND;
                                   } else if (strcmp(ps,"or")==0) {
                                       token = OR;
                                   } else if (strcmp(ps,"mod")==0) {
                                       token = MOD;
                                   } else if (strcmp(ps,"div")==0) {
                                       token = DIV;
                                   } else {
                                       token = WCARDNAME;
                                       tokens[l].strvalue = (char*)strdup(ps);
                                   }
                               } else {
                                   token = WCARDNAME;
                                   tokens[l].strvalue = (char*)strdup(ps);
                               }
                               xpath[i] = save;
                           }
                           i--;
                       } else if (isdigit(xpath[i]) || (xpath[i] == '.')) {
                           if (xpath[i] == '.') {
                               token = REALNUMBER;
                           } else {
                               token = INTNUMBER;
                           }
                           ps = &(xpath[i++]);
                           while (xpath[i] && isdigit(xpath[i]))  i++;
                           if (xpath[i]=='.') {
                               token = REALNUMBER;
                               i++;
                               while (xpath[i] && isdigit(xpath[i]))  i++;
                           }
                           save = xpath[i];
                           xpath[i] = '\0';
                           if (token == INTNUMBER) {
                               tokens[l].intvalue = atoi(ps);
                           }
                           tokens[l].realvalue = (double)atof(ps);
                           xpath[i--] = save;
                       };
                       break;

        } /* switch */
        ADD_TOKEN(token);
        i++;
    }
    ADD_TOKEN(EOS);
    return tokens;

} /* xpathLexer */



/*-----------------------------------------------------------------
|   NodeTest  production
|
\----------------------------------------------------------------*/
Production(NodeTest)

    if (LA==NODE) {
        Consume(NODE);
        Consume(LPAR);
        Consume(RPAR);
        a = New (IsNode);
    } else
    if (LA==TEXT) {
        Consume(TEXT);
        Consume(LPAR);
        Consume(RPAR);
        a = New (IsText);
    } else
    if (LA==COMMENT) {
        Consume(COMMENT);
        Consume(LPAR);
        Consume(RPAR);
        a = New (IsComment);
    } else
    if (LA==PINSTR) {
        Consume(PINSTR);
        Consume(LPAR);
        if (LA==LITERAL) {
            Consume(LITERAL);
            a = NewStr (IsSpecificPI, STRVAL);
        } else {
            a = New (IsPI);
        }
        Consume(RPAR);
    } else
    if (LA==MULTIPLY) {
        Consume(MULTIPLY);
        a = NewStr (IsElement, "*");
    } else
    if (LA==NSPREFIX) {
        ast b;
        Consume (NSPREFIX);
        a = NewStr (IsFQElement, STRVAL);
        Consume (WCARDNAME);
        b = NewStr (IsElement, STRVAL);
        AddChild (a, b);
    } else 
    if (LA==NSWC) {
        Consume (NSWC);
        a = NewStr (IsNSElement, STRVAL);
    } else {
        Consume(WCARDNAME);
        a = NewStr (IsElement, STRVAL);
    }
EndProduction


/*-----------------------------------------------------------------
|   AbbreviatedBasis  production
|
\----------------------------------------------------------------*/
Production(AbbreviatedBasis)
    if (LA==ATTRIBUTE) {
        Consume(ATTRIBUTE);
        a = New1( AxisAttribute, NewStr(IsAttr, STRVAL) );
    } else 
    if (LA==ATTRIBUTEPREFIX) {
        ast b, c;
        Consume(ATTRIBUTEPREFIX);
        a = New (AxisAttribute);
        b = NewStr (IsNSAttr, STRVAL);
        AddChild (a, b);
        Consume(ATTRIBUTE);
        c = NewStr (IsAttr, STRVAL);
        AddChild (b, c);
    }
    else {
        a = New1( AxisChild, Recurse(NodeTest));
    }
EndProduction

/*-----------------------------------------------------------------
|   getFunctionTag
|
\----------------------------------------------------------------*/
static functionTag
getFunctionTag (char *funcName) 
{
    switch (funcName[0]) {
    case 'b':
        if (strcmp (funcName, "boolean")==0) return f_boolean;
        break;
    case 'c':
        if (strcmp (funcName, "ceiling")==0) return f_ceiling;
        else if (strcmp (funcName, "concat")==0) return f_concat;
        else if (strcmp (funcName, "contains")==0) return f_contains;
        else if (strcmp (funcName, "count")==0) return f_count;
        break;
    case 'f':
        if (strcmp (funcName, "false")==0) return f_false;
        else if (strcmp (funcName, "floor")==0) return f_floor;
        break;
    case 'g':
        if (strcmp (funcName, "generate-id")==0) return f_generateId;
        break;
    case 'i':
        if (strcmp (funcName, "id")==0) return f_id;
    case 'l':
        if (strcmp (funcName, "lang")==0) return f_lang;
        else if (strcmp (funcName, "last")==0) return f_last;
        else if (strcmp (funcName, "local-name")==0) return f_localName;
        break;
    case 'n':
        if (strcmp (funcName, "name")==0) return f_name;
        else if (strcmp (funcName, "namespace-uri")==0) return f_namespaceUri;
        else if (strcmp (funcName, "normalize-space")==0) return f_normalizeSpace;
        else if (strcmp (funcName, "not")==0) return f_not;
        else if (strcmp (funcName, "number")==0) return f_number;
        break;
    case 'p':
        if (strcmp (funcName, "position")==0) return f_position;
        break;
    case 'r':
        if (strcmp (funcName, "round")==0) return f_round;
        break;
    case 's':
        if (strcmp (funcName, "starts-with")==0) return f_startsWith;
        else if (strcmp (funcName, "string")==0) return f_string;
        else if (strcmp (funcName, "string-length")==0) return f_stringLength;
        else if (strcmp (funcName, "substring")==0) return f_substring;
        else if (strcmp (funcName, "substring-after")==0) return f_substringAfter;
        else if (strcmp (funcName, "substring-before")==0) return f_substringBefore;
        else if (strcmp (funcName, "sum")==0) return f_sum;
        break;
    case 't':
        if (strcmp (funcName, "translate")==0) return f_translate;
        else if (strcmp (funcName, "true")==0) return f_true;
        break;
    case 'u':
        if (strcmp (funcName, "unparsed-entity-uri")==0) return f_unparsedEntityUri;
        break;
    default:
        break;
    }
    return f_unknown;
}
        
/*-----------------------------------------------------------------
|   FilterExpr  production
|
\----------------------------------------------------------------*/
Production(FilterExpr)

    if (LA==VARIABLE) {
        Consume(VARIABLE);
        a = NewStr( GetVar, STRVAL);

    } else if (LA==LPAR) {
        Consume(LPAR);
        a = New1(EvalSteps, Recurse(OrExpr));
        Consume(RPAR);

    } else if (LA==LITERAL) {
        Consume(LITERAL);
        a = NewStr( Literal, STRVAL);

    } else if (LA==INTNUMBER) {
        Consume(INTNUMBER);
        a = NewInt( INTVAL );

    } else if (LA==REALNUMBER) {
        Consume(REALNUMBER);
        a = NewReal( REALVAL );

    } else if (LA==FUNCTION) {
        Consume(FUNCTION);
        a = NewStr( ExecFunction, STRVAL);
        a->intvalue = getFunctionTag (STRVAL);
        Consume(LPAR);
        if (LA!=RPAR) {
            AddChildWithEvalSteps (a, Recurse(OrExpr));
            while(LA==COMMA) {
                Consume(COMMA);
                AddChildWithEvalSteps  (a, Recurse(OrExpr) );
            }
        }
        Consume(RPAR);
    } else {
        ErrExpected("$var or (expr) or literal or number or func");
    }
    while (LA==LBRACKET) {
        Append( a, New1WithEvalSteps( Pred, Recurse(Predicate)));
    }
EndProduction


/*-----------------------------------------------------------------
|   PathExpr  production
|
\----------------------------------------------------------------*/
Production(PathExpr)

    if ( (LA==VARIABLE)
       ||(LA==LPAR)
       ||(LA==LITERAL)
       ||(LA==INTNUMBER)
       ||(LA==REALNUMBER)
       ||(LA==FUNCTION)
    ) {
        a = Recurse(FilterExpr);
        if (LA==SLASH) {
            Consume(SLASH);
            Append(a, Recurse(RelativeLocationPath));

        } else if (LA==SLASHSLASH) {
            ast b;
            Consume(SLASHSLASH);
            b = Recurse(RelativeLocationPath);
            if (b->type == AxisChild) {
                b->type = AxisDescendant;
            } else {
                Append(a, New( AxisDescendantOrSelf ) );
            }
            Append(a, b );
        }

    } else {
        if ( (LA==SLASH) || (LA==SLASHSLASH)) {
            return Recurse(AbsoluteLocationPath);
        } else {
            return Recurse(RelativeLocationPath);
        }
    }

EndProduction


/*-----------------------------------------------------------------
|   UnionExpr  production
|
\----------------------------------------------------------------*/
Production(UnionExpr)

    a = Recurse(PathExpr);
    while (LA==PIPE) {
        Consume(PIPE);
        a = New2( CombineSets, a, Recurse(PathExpr));
    }

EndProduction


/*-----------------------------------------------------------------
|   UnaryExpr  production
|
\----------------------------------------------------------------*/
Production(UnaryExpr)

    if (LA==MINUS) {
        Consume(MINUS);
        a = Recurse(UnionExpr);
        if ((a->type == Int) && (a->next == NULL)) {
            a->intvalue = a->intvalue * -1;
        } else
        if ((a->type == Real) && (a->next == NULL)) {
            a->realvalue = a->realvalue * -1;
        } else {
            a = New1( UnaryMinus, a);
        }

    } else {
        a = Recurse(UnionExpr);
    }
EndProduction


/*-----------------------------------------------------------------
|   MultiplicativeExpr  production
|
\----------------------------------------------------------------*/
Production(MultiplicativeExpr)

    a = Recurse(UnaryExpr);
    while ( (LA==MULTIPLY)
          ||(LA==DIV)
          ||(LA==MOD)
    ) {
        if (LA==MULTIPLY) {
            Consume(MULTIPLY);
            a = New2( Mult, a, Recurse(UnaryExpr));
        } else if (LA==DIV) {
            Consume(DIV);
            a = New2( Div, a, Recurse(UnaryExpr));
        } else {
            Consume(MOD);
            a = New2( Mod, a, Recurse(UnaryExpr));
        }
    }
EndProduction


/*-----------------------------------------------------------------
|   AdditiveExpr  production
|
\----------------------------------------------------------------*/
Production(AdditiveExpr)

    a = Recurse(MultiplicativeExpr);
    while ( (LA==PLUS)
          ||(LA==MINUS)
    ) {
        if (LA==PLUS) {
            Consume(PLUS);
            a = New2( Add, a, Recurse(MultiplicativeExpr));
        } else {
            Consume(MINUS);
            a = New2( Substract, a, Recurse(MultiplicativeExpr));
        }
    }
EndProduction


/*-----------------------------------------------------------------
|   RelationalExpr  production
|
\----------------------------------------------------------------*/
Production(RelationalExpr)

    a = Recurse(AdditiveExpr);
    while ( (LA==LT)
          ||(LA==LTE)
          ||(LA==GT)
          ||(LA==GTE)
    ) {
        if (LA==LT) {
            Consume(LT);
            a = New2( Less, a, Recurse(AdditiveExpr));
        } else if (LA==LTE) {
            Consume(LTE);
            a = New2( LessOrEq, a, Recurse(AdditiveExpr));
        } else if (LA==GT) {
            Consume(GT);
            a = New2( Greater, a, Recurse(AdditiveExpr));
        } else {
            Consume(GTE);
            a = New2( GreaterOrEq, a, Recurse(AdditiveExpr));
        }
    }
EndProduction


/*-----------------------------------------------------------------
|   EqualityExpr  production
|
\----------------------------------------------------------------*/
Production(EqualityExpr)

    a = Recurse(RelationalExpr);
    while ( (LA==EQUAL)
          ||(LA==NOTEQ)
    ) {
        if (LA==EQUAL) {
            Consume(EQUAL);
            a = New2( Equal, a, Recurse(RelationalExpr));
        } else {
            Consume(NOTEQ);
            a = New2( NotEqual, a, Recurse(RelationalExpr));
        }
    }
EndProduction


/*-----------------------------------------------------------------
|   AndExpr  production
|
\----------------------------------------------------------------*/
Production(AndExpr)

    a = Recurse(EqualityExpr);
    while (LA==AND) {
        Consume(AND);
        a = New2( And, a, Recurse(EqualityExpr));
    }
EndProduction


/*-----------------------------------------------------------------
|   OrExpr  production
|
\----------------------------------------------------------------*/
Production(OrExpr)

    a = Recurse(AndExpr);
    while (LA==OR) {
        Consume(OR);
        a = New2( Or, a, Recurse(AndExpr));
    }
EndProduction


/*-----------------------------------------------------------------
|   Predicate  production
|
\----------------------------------------------------------------*/
Production(Predicate)

    Consume(LBRACKET);
    /*a = Recurse(PredicateExpr);*/
    a = Recurse(OrExpr);
    Consume(RBRACKET);

EndProduction


/*-----------------------------------------------------------------
|   Basis  production
|
\----------------------------------------------------------------*/
Production(Basis)

    if (LA==AXISNAME) {
        astType t;
        Consume(AXISNAME);
        if        (IS_STR('c',"child"))              { t = AxisChild;
        } else if (IS_STR('d',"descendant"))         { t = AxisDescendantLit;
        } else if (IS_STR('d',"descendant-or-self")) { t = AxisDescendantOrSelfLit;
        } else if (IS_STR('s',"self"))               { t = AxisSelf;
        } else if (IS_STR('a',"attribute"))          { t = AxisAttribute;
        } else if (IS_STR('a',"ancestor"))           { t = AxisAncestor;
        } else if (IS_STR('a',"ancestor-or-self"))   { t = AxisAncestorOrSelf;
        } else if (IS_STR('f',"following"))          { t = AxisFollowing;
        } else if (IS_STR('f',"following-sibling"))  { t = AxisFollowingSibling;
        } else if (IS_STR('n',"namespace"))          { t = AxisNamespace;
        } else if (IS_STR('p',"parent"))             { t = AxisParent;
        } else if (IS_STR('p',"preceding"))          { t = AxisPreceding;
        } else if (IS_STR('p',"preceding-sibling"))  { t = AxisPrecedingSibling;
        } else {
            ErrExpected("correct axis name");
        }
        a = New( t );
        Consume(COLONCOLON);
        AddChild( a, Recurse(NodeTest));
    } else {
        a = Recurse(AbbreviatedBasis);
    }
EndProduction


/*-----------------------------------------------------------------
|   Step  production
|
\----------------------------------------------------------------*/
Production(Step)

    if (LA==DOT) {
        Consume(DOT);
        a = New( GetContextNode );
        /* a = New1( AxisSelf, New (IsNode)); */

    } else if (LA==DOTDOT) {
        Consume(DOTDOT);
        a = New( GetParentNode );
        /* a = New1( AxisParent, New (IsNode)); */

    } else {
        a = Recurse(Basis);
        while (LA==LBRACKET) {
            Append( a, New1WithEvalSteps( Pred, Recurse(Predicate)));
        }
    }
EndProduction


/*-----------------------------------------------------------------
|   RelativeLocationPath  production
|
\----------------------------------------------------------------*/
Production(RelativeLocationPath)

    a = Recurse(Step);
    while ((LA==SLASH) || (LA==SLASHSLASH)) {
        if (LA==SLASH) {
            Consume(SLASH);
            Append(a, Recurse(Step));
        } else {
            ast b;
            Consume(SLASHSLASH);
            b = Recurse(Step);
            if (b->type == AxisChild) {
                b->type = AxisDescendant;
            } else {
                Append(a, New( AxisDescendantOrSelf ) );
            }
            Append(a, b );
        }
    }
EndProduction


/*-----------------------------------------------------------------
|   AbsoluteLocationPath  production
|
\----------------------------------------------------------------*/
Production(AbsoluteLocationPath)

    if (LA==SLASH) {
        ast b;

        Consume(SLASH);
        a = New(SelectRoot);
        if ( (LA==AXISNAME)
           ||(LA==WCARDNAME)
           ||(LA==NODE)
           ||(LA==TEXT)
           ||(LA==COMMENT)
           ||(LA==PINSTR)
           ||(LA==DOT)
           ||(LA==DOTDOT)
           ||(LA==ATTRIBUTE)
           ||(LA==ATTRIBUTEPREFIX)  
        ) {
            b =  Recurse(RelativeLocationPath);
            Append(a, b);
        }

    } else if (LA==SLASHSLASH) {
        ast b;

        Consume(SLASHSLASH);
        a = New(SelectRoot);

        b = Recurse(RelativeLocationPath);
        if (b->type == AxisChild) {
            b->type = AxisDescendant;
        } else {
            Append(a, New( AxisDescendantOrSelf ) );
        }

        Append(a, b );

    } else {
        ErrExpected("/ or //");
    }
EndProduction


/*-----------------------------------------------------------------
|   XSLT StepPattern  production
|
\----------------------------------------------------------------*/
static int usesPositionInformation ( ast a) {
    
    if (a->type == ExecFunction) {
        if (   (strcmp(a->strvalue,"position")==0)
            || (strcmp(a->strvalue,"last")==0)
        ) {
            return 1;
        }
    } 
    a = a->child;
    while (a) {
        if (usesPositionInformation(a)) return 1;
        a = a->next;
    }
    return 0;    
}
static int checkPredOptimizability ( ast a ) {

    switch (a->type) {
        case Literal:
        case AxisChild:
        case AxisAttribute:
        case AxisDescendant:
        case AxisDescendantLit:
        case AxisDescendantOrSelf:
        case AxisDescendantOrSelfLit:
        case AxisSelf:
        case IsNode:
        case IsComment:
        case IsText:
        case IsPI:
        case IsSpecificPI:
        case IsElement:
        case GetContextNode: 
            break;
            
        case And:
        case Or:
        case Less:
        case LessOrEq:
        case Greater:
        case GreaterOrEq:
        case Equal:
        case NotEqual:
        case ExecFunction:
            a = a->child;
            while (a) {
                if (usesPositionInformation(a)) return 0;
                a = a->next;
            }
            return 1;

        default: 
            return 0;
    }
    a = a->child;
    while (a) {
        if (!checkPredOptimizability(a)) return 0;
        a = a->next;
    } 
    return 1;
}
static int IsPredOptimizable ( ast a ) {
    int f;
    f = checkPredOptimizability(a);
    if (f) {
       DBG(
         fprintf(stderr, "\nPred is optimizable:\n");
         printAst(0,a);
       )
    }
    return f;
}


/*-----------------------------------------------------------------
|   XSLT StepPattern  production
|
\----------------------------------------------------------------*/
Production(StepPattern)

    if (LA==AXISNAME) {    
        astType t;
        Consume(AXISNAME);
        if        (IS_STR('c',"child"))      { t = AxisChild;
        } else if (IS_STR('a',"attribute"))  { t = AxisAttribute;
        } else {
            ErrExpected("correct axis name (child/attribute)");
        }
        Consume(COLONCOLON);
        a = New1( t, Recurse(NodeTest));
        
    } else
    if (LA==ATTRIBUTE) {
        Consume(ATTRIBUTE);
        a = New1( AxisAttribute, NewStr(IsAttr, STRVAL) );
    } else 
    if (LA==ATTRIBUTEPREFIX) {
        ast b, c;
        Consume(ATTRIBUTEPREFIX);
        a = New ( AxisAttribute);
        b = NewStr (IsNSAttr, STRVAL);
        AddChild (a, b);
        Consume(ATTRIBUTE);
        c = NewStr (IsAttr, STRVAL);
        AddChild (b, c);
    } else {
        a = Recurse(NodeTest);
    }
    { 
        ast b, c;
        int stepIsOptimizable = 1, isFirst = 1;
        while (LA==LBRACKET) {
            b = Recurse (Predicate);
            if (stepIsOptimizable) {
                if (!IsPredOptimizable(b)) stepIsOptimizable = 0;
            }
            if (isFirst) {
                c = New1WithEvalSteps( Pred, b);
                isFirst = 0;
            } else {
                Append (c, New1WithEvalSteps( Pred, b));
            }
        }
        if (!isFirst) {
            if (stepIsOptimizable) {
                Append (a, New (FillWithCurrentNode));
            } else {
                /* copy the step before the Predicate */
                ast aCopy = NEWCONS;
                aCopy->type      = a->type;
                aCopy->next      = NULL;
                aCopy->strvalue  = strdup(a->strvalue);
                aCopy->intvalue  = a->intvalue;
                aCopy->realvalue = a->realvalue;
                aCopy->child     = NULL;
                if (a->child) {
                    ast aCopyChild = NEWCONS;
                    aCopyChild->type      = a->child->type;
                    aCopyChild->next      = NULL;
                    aCopyChild->child     = NULL;
                    aCopyChild->strvalue  = a->child->strvalue;
                    aCopyChild->intvalue  = a->child->intvalue;
                    aCopyChild->realvalue = a->child->realvalue;
                    aCopy->child = aCopyChild;
                }                
                Append( a, New1( FillNodeList, aCopy));
            }
            Append (a, c);
        }
    }

EndProduction


/*-----------------------------------------------------------------
|   XSLT RelativePathPattern  production
|
\----------------------------------------------------------------*/
Production(RelativePathPattern)

    a = Recurse(StepPattern);
    while ((LA==SLASH) || (LA==SLASHSLASH)) {
        ast b;
        if (LA==SLASH) {
            Consume(SLASH);
            b = Recurse(StepPattern);
            Append(b, New(ToParent) ); 
            Append(b, a);
            a = b;
        } else {
            Consume(SLASHSLASH);
            b = Recurse(StepPattern);
            Append(b, New(ToAncestors) ); 
            Append(b, a);
            a = b;
        }
    }  

EndProduction


/*-----------------------------------------------------------------
|   XSLT IdKeyPattern  production
|
\----------------------------------------------------------------*/
Production(IdKeyPattern)

    Consume(FUNCTION);
    if (strcmp(STRVAL, "id" )==0) {
        ast b;
        /* id */
        a = NewStr( ExecIdKey, STRVAL);
        a->intvalue = f_id;
        Consume(LPAR);
        Consume(LITERAL);
        /* STRVAL */
        b = NewStr( Literal, STRVAL);
        AddChild (a, b);
        Consume(RPAR);
    } else {
        ast b;
        /* key */
        a = NewStr( ExecIdKey, STRVAL);
        Consume(LPAR);
        Consume(LITERAL);
        /* STRVAL */
        b = NewStr( Literal, STRVAL);
        AddChild (a, b);
        Consume(COMMA);
        Consume(LITERAL);
        /* STRVAL */
        b = NewStr( Literal, STRVAL);
        AddChild (a, b);        
        Consume(RPAR);
    }

EndProduction


/*-----------------------------------------------------------------
|   XSLT LocationPathPattern  production
|
\----------------------------------------------------------------*/
Production(LocationPathPattern)

    if (LA==SLASH) {
        Consume(SLASH);
        if (LA==EOS || LA==PIPE) {
            a = New(IsRoot);
        } else {
            a = Recurse(RelativePathPattern);
            Append( a, New(ToParent) );
            Append( a, New(IsRoot) ); 
        }
    } else
    if ((LA==FUNCTION)
        && (  (strcmp(tokens[*l].strvalue, "id" )==0)
            ||(strcmp(tokens[*l].strvalue, "key")==0) ) )
    {
        ast b;
        
        b = Recurse(IdKeyPattern);    
        if (LA==SLASH) {
            Consume(SLASH);
            a = Recurse(RelativePathPattern);
            Append( a, New(ToParent) );
        } else 
        if (LA==SLASHSLASH) {
            Consume(SLASHSLASH);
            a = Recurse(RelativePathPattern);
            Append( a, New(ToAncestors) );
        }
        if (!a) {
            a = b;
        } else {
            Append( a, b);
        }
    } else {
        if (LA==SLASHSLASH) {
            Consume(SLASHSLASH);
            a = Recurse(RelativePathPattern);
            Append( a, New(ToAncestors) );
            Append( a, New(IsRoot) );
        } else {
            a = Recurse(RelativePathPattern);
        }
    }

EndProduction

/*-----------------------------------------------------------------
|   XSLT Pattern  production
|
\----------------------------------------------------------------*/
Production(Pattern)

    a = Recurse(LocationPathPattern);
    while (LA==PIPE) {
        Consume(PIPE);
        a = New2( CombinePath, New1(EvalSteps, a), 
                               New1(EvalSteps, Recurse(LocationPathPattern) ) );
    }

EndProduction



/*----------------------------------------------------------------------------
|   xpathFreeTokens
|
\---------------------------------------------------------------------------*/
static
int xpathFreeTokens (
    XPathTokens tokens
)
{
    int i;

    for (i=0; tokens[i].token != EOS; i++) {
        if (tokens[i].strvalue) {
            free (tokens[i].strvalue);
        }
    }
    free (tokens);
    return 0;
}



/*----------------------------------------------------------------------------
|   xpathParse
|
\---------------------------------------------------------------------------*/
int xpathParse (
    char  *xpath,
    char **errMsg,
    ast   *t,
    int    parsePattern
)
{
    XPathTokens tokens;
    int  i, l, len, newlen, slen;
    char tmp[200];

    DDBG(fprintf(stderr, "\nLex output following tokens for '%s':\n", xpath);)
    tokens = xpathLexer(xpath, errMsg);
    if (tokens == NULL) {
        xpathFreeTokens (tokens);
        return XPATH_LEX_ERR;
    }
    DDBG(
        for (i=0; tokens[i].token != EOS; i++) {
            fprintf(stderr, "%3d %-12s %5d %8.3f %5d  %s\n",
                            i,
                            token2str[tokens[i].token-LPAR],
                            tokens[i].intvalue,
                            tokens[i].realvalue,
                            tokens[i].pos,
                            tokens[i].strvalue
            );
        }
    )
    l = 0;

    if (parsePattern) {
        *t = Pattern (&l, tokens, errMsg);
    } else {
        *t = OrExpr (&l, tokens, errMsg);
    }
    if ((*errMsg == NULL) && (tokens[l].token != EOS)) {
        *errMsg = (char*)strdup("Unexpected tokens (beyond end)!");
    }
    if (*errMsg) {
        len    = strlen(*errMsg);
        newlen = strlen(xpath);
        *errMsg = (char*)realloc(*errMsg, len+newlen+10);
        memmove(*errMsg + len, " for '", 6);
        memmove(*errMsg + len+6, xpath, newlen);
        memmove(*errMsg + len+6+newlen, "' ", 3);

        for (i=0; tokens[i].token != EOS; i++) {
            sprintf(tmp, "%s\n%3s%3d %-12s %5d %8.3f %5d  ",
                         (i==0) ? "\n\nParsed symbols:" : "",
                         (i==l) ? "-->" : "   ",
                          i,
                         token2str[tokens[i].token-LPAR],
                         tokens[i].intvalue,
                         tokens[i].realvalue,
                         tokens[i].pos
            );
            len    = strlen(*errMsg);
            newlen = strlen(tmp);
            slen = 0;
            if (tokens[i].strvalue) {
                slen = strlen(tokens[i].strvalue);
            }

            *errMsg = (char*)realloc(*errMsg, len+newlen+slen+1);
            memmove(*errMsg + len, tmp, newlen);
            memmove(*errMsg + len + newlen, tokens[i].strvalue, slen);
            (*errMsg)[len + newlen + slen] = '\0';
        }
    }
    DBG(
        if (parsePattern) {
            fprintf(stderr, "\nPattern AST for '%s': \n", xpath);
            printAst (0, *t);
        } else {
            fprintf(stderr, "AST:\n");
            printAst (0, *t);
        }    
    )
    xpathFreeTokens (tokens);
    if (*errMsg!=NULL) return XPATH_SYNTAX_ERR ;
    return 0;

} /* xpathParse */


/*----------------------------------------------------------------------------
|   xpathNodeTest
|
\---------------------------------------------------------------------------*/
int xpathNodeTest (
    domNode        *node,
    domNode        *exprContext,
    ast             step
)
{
    domNS  *contextNS;
    char   *localName, *nodeUri;

    if (!(step->child)) return 1;
    if (step->child->type == IsElement) {
        if (node->nodeType == ELEMENT_NODE) {
            if ((step->child->strvalue[0] == '*') &&
                (step->child->strvalue[1] == '\0') &&
                (node->ownerDocument->rootNode != node)) return 1;
            if (node->namespace) return 0;
            return (strcmp(node->nodeName, step->child->strvalue)==0);
        }
        return 0;
    } else
    if (step->child->type == IsAttr) {
        if (node->nodeType == ATTRIBUTE_NODE) {
            if (node->nodeFlags & IS_NS_NODE) return 0;
            if (  (step->child->strvalue[0] == '*')
                &&(step->child->strvalue[1] == '\0')
            ) {
                return 1;
            }
            return (strcmp( ((domAttrNode*)node)->nodeName, 
                            step->child->strvalue)==0);
        }
        return 0;
    } else
    if (step->child->type == IsFQElement) {
        if (node->nodeType != ELEMENT_NODE || node->namespace == 0) return 0;
        contextNS = domLookupPrefix (exprContext, step->child->strvalue);
        if (!contextNS) return 0; /* Hmmm, that's more an error, than a not match */
        nodeUri = domNamespaceURI (node);
        if (!nodeUri) return 0;
        if (strcmp (contextNS->uri, nodeUri) != 0) return 0;
        localName = domGetLocalName (node->nodeName);
        if (strcmp (step->child->child->strvalue, localName)==0) return 1;
        return 0;
    } else
    if (step->child->type == IsNSElement) {
        contextNS = domLookupPrefix (exprContext, step->child->strvalue);
        if (!contextNS) return 0; /* Hmmm, that's more an error, than a not match */
        nodeUri = domNamespaceURI (node);
        if (!nodeUri) return 0;
        if (nodeUri && (strcmp (contextNS->uri, nodeUri)==0)) return 1;
        return 0;
    } else
    if (step->child->type == IsNSAttr) {
        if (node->nodeType != ATTRIBUTE_NODE 
            || node->nodeFlags & IS_NS_NODE) return 0;
        contextNS = domLookupPrefix (exprContext, step->child->strvalue);
        if (!contextNS) return 0;
        nodeUri = domNamespaceURI (node);
        if (!nodeUri) return 0;
        if (strcmp (contextNS->uri, nodeUri) != 0) return 0;
        if (strcmp (step->child->child->strvalue, "*")==0) return 1;
        localName = domGetLocalName (((domAttrNode *) node)->nodeName);
        if (strcmp (step->child->child->strvalue, localName)==0) return 1;
        return 0;
    } else
    if (step->child->type == IsNode) {
        DBG(fprintf(stderr, "nodeTest: nodeType=%d \n", node->nodeType);)
        return 1;
    } else
    if (step->child->type == IsText) {
        DBG(fprintf(stderr, "nodeTest: nodeType=%d == %d?? \n", node->nodeType, TEXT_NODE);)
        return (node->nodeType == TEXT_NODE);
    } else
    if (step->child->type == IsPI) {
        return (node->nodeType == PROCESSING_INSTRUCTION_NODE);
    } else
    if (step->child->type == IsSpecificPI) {
        return (strncmp (((domProcessingInstructionNode*)node)->targetValue,
                         step->child->strvalue,
                         ((domProcessingInstructionNode*)node)->targetLength)
            == 0);
    } else  
    if (step->child->type == IsComment) {
        return (node->nodeType == COMMENT_NODE);
    }
    return 1;
}


/*----------------------------------------------------------------------------
|   xpathFuncBoolean
|
\---------------------------------------------------------------------------*/
int xpathFuncBoolean (
    xpathResultSet  *rs
)
{
    switch (rs->type) {
        case BoolResult:         return ( rs->intvalue         );
        case IntResult:          return ( rs->intvalue ? 1 : 0 );
        case RealResult:         return ((rs->realvalue != 0.0 ) && !isnan (rs->realvalue));
        case StringResult:       return ( rs->string_len > 0   );
        case xNodeSetResult:     return ( rs->nr_nodes > 0     );
        default:                 return 0;
    }
}


/*----------------------------------------------------------------------------
|   xpathFuncNumber
|
\---------------------------------------------------------------------------*/
double xpathFuncNumber (
    xpathResultSet  *rs,
    int             *NaN
)
{
    double d;
    char   tmp[80], *pc, *tailptr;

    *NaN = 0;
    switch (rs->type) {
        case BoolResult:   return (rs->intvalue? 1.0 : 0.0);
        case IntResult:    return rs->intvalue;
        case RealResult:   {
            if (isnan(rs->realvalue) || (isinf(rs->realvalue)!=0)) *NaN = 1;
            return rs->realvalue;
        }
        case StringResult:
              strncpy(tmp, rs->string, (rs->string_len<79) ? rs->string_len : 79);
              tmp[(rs->string_len<79) ? rs->string_len : 79] = '\0';
              d = strtod (tmp, &tailptr);
              if (d == 0.0 && tailptr == tmp) {
                  d = strtod ("nan", &tailptr);
                  *NaN = 1;
              } else 
              if (isnan(d)) {
                  *NaN = 1;
              } else 
              if (tailptr) {
                  while (*tailptr) {
                      switch (*tailptr) {
                      case ' ' :
                      case '\n':
                      case '\r':
                      case '\t': tailptr++; continue;
                      default: break; /*do nothing */
                      }
                      d = strtod ("nan", &tailptr);
                      *NaN = 1;
                      break;
                  }
              }
              return d;
        case xNodeSetResult:
              pc = xpathFuncString(rs);
              d = strtod (pc, &tailptr);
              if (d == 0.0 && tailptr == pc) {
                  d = strtod ("nan", &tailptr);
                  *NaN = 1;
              } else 
              if (isnan(d)) {
                  *NaN = 1;
              } else 
              if (tailptr) {
                  while (*tailptr) {
                      switch (*tailptr) {
                      case ' ' :
                      case '\n':
                      case '\r':
                      case '\t': tailptr++; continue;
                      default: break; /*do nothing */
                      }
                      d = strtod ("nan", &tailptr);
                      *NaN = 1;
                      break;
                  }
              }
              free(pc);
              return d;
        default:
              DBG(fprintf(stderr, "funcNumber: default: 0.0\n");)
              d = strtod ("nan", &tailptr);
              *NaN = 1; 
              return d;
    }
}


/*----------------------------------------------------------------------------
|   xpathGetTextValue
|
\---------------------------------------------------------------------------*/
char * xpathGetTextValueForElement (
    domNode *node,
    int     *len
)
{
    char        *pc, *t;
    int          l;
    domNode     *child;

    if (node->nodeType == ELEMENT_NODE) {
        DBG(fprintf(stderr,"GetTextValue: tag='%s' \n", node->nodeName);)
        pc = malloc(1); *pc = '\0'; *len = 0;
        child = node->firstChild;
        while (child) {
            t = xpathGetTextValueForElement(child, &l); 
            pc = (char*)realloc(pc, 1 + *len + l);
            memmove(pc + *len, t, l );
            *len += l;
            pc[*len] = '\0';
            free(t);
            child = child->nextSibling;
        }
    } else
    if (node->nodeType == TEXT_NODE) {

        *len = ((domTextNode*)node)->valueLength;
        pc   = (char*)malloc(1+*len);
        memmove(pc, ((domTextNode*)node)->nodeValue, *len);
        pc[*len] = '\0';
        DBG(fprintf(stderr,"GetTextValue: text='%s' \n", pc);)
    } else {
        pc   = strdup ("");
        *len = 0;
    }
    return pc;
}

char * xpathGetTextValue (
    domNode *node,
    int     *len
)
{
    char         *pc, *t;
    int          l;
    domNode     *child;
    domAttrNode *attr;

    if (node->nodeType == ELEMENT_NODE) {
        DBG(fprintf(stderr,"GetTextValue: tag='%s' \n", node->nodeName);)
        pc = malloc(1); *pc = '\0'; *len = 0;
        child = node->firstChild;
        while (child) {
            t = xpathGetTextValueForElement(child, &l); 
            pc = (char*)realloc(pc, 1 + *len + l);
            memmove(pc + *len, t, l );
            *len += l;
            pc[*len] = '\0';
            free(t);
            child = child->nextSibling;
        }
    } else
    if ((node->nodeType == TEXT_NODE) ||
        (node->nodeType == CDATA_SECTION_NODE) ||
        (node->nodeType == COMMENT_NODE)
    ) {

        *len = ((domTextNode*)node)->valueLength;
        pc   = (char*)malloc(1+*len);
        memmove(pc, ((domTextNode*)node)->nodeValue, *len);
        pc[*len] = '\0';
        DBG(fprintf(stderr,"GetTextValue: text='%s' \n", pc);)
    } else
    if (node->nodeType == PROCESSING_INSTRUCTION_NODE) {
        *len = ((domProcessingInstructionNode*)node)->dataLength;
        pc   = (char*)malloc(1+*len);
        memmove(pc, ((domProcessingInstructionNode*)node)->dataValue, *len);
        pc[*len] = '\0';        
    } else 
    if (node->nodeType == ATTRIBUTE_NODE) {
        attr = (domAttrNode*)node;
        DBG(fprintf(stderr,"GetTextValue: attr='%s' \n", attr->nodeName);)
        pc = malloc(attr->valueLength +1 );
        memmove(pc, attr->nodeValue, attr->valueLength);
        *(pc + attr->valueLength) = '\0';
        *len = attr->valueLength;
    }
    else {
        pc   = strdup ("");
        *len = 0;
    }
    return pc;
}

/*----------------------------------------------------------------------------
|   xpathFuncString
|
\---------------------------------------------------------------------------*/
char * xpathFuncString (
    xpathResultSet  *rs
)
{
    char         tmp[80], *pc;
    int          len;

    switch (rs->type) {
        case BoolResult:
            if (rs->intvalue) return (strdup("true"));
                         else return (strdup("false"));
        case IntResult:
            sprintf(tmp, "%d", rs->intvalue);
            return (strdup(tmp));

        case RealResult:
            sprintf(tmp, "%f", rs->realvalue);
            /* strip trailing 0 and . */
            len = strlen(tmp);
            for (; (len > 0) && (tmp[len-1] == '0'); len--) tmp[len-1] = '\0';
            if ((len > 0) && (tmp[len-1] == '.'))   tmp[len-1] = '\0';

            if (strcmp(tmp,"nan")==0)  return strdup("NaN");
            if (strcmp(tmp,"inf")==0)  return strdup("Infinity");
            if (strcmp(tmp,"-inf")==0) return strdup("-Infinity");
            return (strdup(tmp));

        case StringResult:
            pc = malloc(rs->string_len +1 );
            memmove(pc, rs->string, rs->string_len);
            *(pc + rs->string_len) = '\0';
            return pc;

        case xNodeSetResult:
            if (rs->nr_nodes == 0) {
                pc = strdup ("");
            } else {
                pc = xpathGetTextValue (rs->nodes[0], &len);
            }
            return pc;

        default:
            return (strdup (""));
    }
}

/*----------------------------------------------------------------------------
|   xpathFuncStringForNode
|
\---------------------------------------------------------------------------*/
char * xpathFuncStringForNode (
    domNode *node
)
{
    int          len;

    return xpathGetTextValue (node, &len);
}

/*----------------------------------------------------------------------------
|   xpathFuncNumberForNode
|
\---------------------------------------------------------------------------*/
double xpathFuncNumberForNode (
    domNode *node,
    int      *NaN
)
{
    char        *pc;
    int          len, rc;
    double       d;

    *NaN = 0;
    pc = xpathGetTextValue (node, &len);    
    rc = sscanf (pc,"%lf", &d);
    if (rc != 1) *NaN = 1;
    free(pc);
    return d;
}


/*----------------------------------------------------------------------------
|   xpathArity
|
\---------------------------------------------------------------------------*/
static int xpathArity (
    ast step
)
{
    int parms = 0;

    step = step->child;
    while (step) {
        parms++;
        step = step->next;
    }
    return parms;
}


/*----------------------------------------------------------------------------
|   xpathArityCheck
|
\---------------------------------------------------------------------------*/
static int xpathArityCheck (
    ast    step,
    int    arity,
    char **errMsg
)
{
    int parms = 0;

    step = step->child;
    while (step) {
        parms++;
        step = step->next;
    }
    if (arity!=parms) {
        *errMsg = (char*)strdup("wrong number of parameters!");
        return 1;
    }
    return 0;
}
#define XPATH_ARITYCHECK(s,a,m) if (xpathArityCheck(s,a,m)) return XPATH_EVAL_ERR




/*----------------------------------------------------------------------------
|   xpathRound
|
\---------------------------------------------------------------------------*/
int xpathRound (double r) {
    if (r < 0.0) {
        return floor (r + 0.5);
    } else {
        return (int)(r + 0.5);
    }
}

/*----------------------------------------------------------------------------
|   xpathEvalFunction
|
\---------------------------------------------------------------------------*/
static int 
xpathEvalFunction (
    ast                step,
    domNode           *ctxNode,
    domNode           *exprContext,
    int                position,
    xpathResultSet    *nodeList,
    xpathCBs          *cbs,
    xpathResultSet    *result,
    int               *docOrder,
    char             **errMsg
    )
{
    xpathResultSet   leftResult, rightResult, replaceResult;
    int              i, j, rc, pwhite, len,  NaN;
    char            *replaceStr, *pfrom, *pto, tmp[80], tmp1[80];
    domNode         *node;
    domAttrNode     *attr;
    double           leftReal;
    ast              nextStep;
    int              argc, savedDocOrder, from;
    xpathResultSets *args;
    xpathResultSet  *arg;
    Tcl_HashEntry   *entryPtr;
    int              left = 0, useFastAdd;
    double           dRight = 0.0;
    char            *leftStr = NULL, *rightStr = NULL;
#if TclOnly8Bits
    char            *fStr;
#else 
    int              found;
    int              lenstr, fromlen, utfCharLen;
    char             utfBuf[TCL_UTF_MAX];        
    Tcl_DString      tstr, tfrom, tto, tresult;
    Tcl_UniChar     *ufStr, *upfrom, unichar;
#endif    

    if (result->type == EmptyResult) useFastAdd = 1;
    else useFastAdd = 0;

    switch (step->intvalue) {

    case f_position:
        XPATH_ARITYCHECK(step,0,errMsg);
        if (*docOrder) {
            rsSetInt (result, position+1);
        } else {
            rsSetInt (result, nodeList->nr_nodes - position);
        }
        break;

    case f_last:
        XPATH_ARITYCHECK(step,0,errMsg);
        rsSetInt (result, nodeList->nr_nodes);
        break;

    case f_number:
        xpathRSInit (&leftResult);
        if (xpathArity(step) == 0) {
            /*  no parameter, the context node is the nodeset to
             *  operate with
             */
            rsAddNode( &leftResult, ctxNode);
        } else {
            XPATH_ARITYCHECK(step,1,errMsg);
            xpathRSInit (&leftResult);
            rc = xpathEvalStep( step->child, ctxNode, exprContext, position,
                                nodeList, cbs, &leftResult, docOrder, errMsg);
            if (rc) {
                xpathRSFree( &leftResult );
                return rc;
            }
        }
        leftReal = xpathFuncNumber(&leftResult, &NaN);
        rsSetReal (result, leftReal);
        xpathRSFree( &leftResult );
        break;

    case f_floor:
    case f_ceiling:
    case f_round:
        XPATH_ARITYCHECK(step,1,errMsg);
        xpathRSInit (&leftResult);
        rc = xpathEvalStep( step->child, ctxNode, exprContext, position,
                            nodeList, cbs, &leftResult, docOrder, errMsg);
        if (rc) {
            xpathRSFree( &leftResult );
            return rc;
        }
        leftReal = xpathFuncNumber(&leftResult, &NaN);
    
        if (NaN) { rsSetReal(result, leftReal); return XPATH_OK; }
    
        if      (step->intvalue == f_floor)   leftReal = floor(leftReal);
        else if (step->intvalue == f_ceiling) leftReal = ceil(leftReal);
        else                                  leftReal = xpathRound(leftReal);
    
        rsSetReal (result, leftReal);
        xpathRSFree( &leftResult );
        break;

    case f_boolean:
          XPATH_ARITYCHECK(step,1,errMsg);
          xpathRSInit (&leftResult);
          rc = xpathEvalStep( step->child, ctxNode, exprContext, position,
                              nodeList, cbs, &leftResult, docOrder, errMsg);
          if (rc) {
              xpathRSFree( &leftResult );
              return rc;
          }
          left = xpathFuncBoolean(&leftResult);
          rsSetBool (result, left);
          xpathRSFree( &leftResult );
          break;
          
    case f_string:
    case f_normalizeSpace:
    case f_stringLength:
           xpathRSInit (&leftResult);
           if (step->child == NULL) {
               /*  no parameter, the context node is the nodeset to
                *  operate with
                */
               rsAddNode( &leftResult, ctxNode);
      
           } else {
               XPATH_ARITYCHECK(step,1,errMsg);
               xpathRSInit (&leftResult);
               rc = xpathEvalStep( step->child, ctxNode, exprContext, position,
                                   nodeList, cbs, &leftResult, docOrder, errMsg);
               if (rc) {
                   xpathRSFree( &leftResult );
                   return rc;
               }
               DBG(fprintf(stderr, "normalize-space: \n");
                   rsPrint(&leftResult);
               )
           }
      
           leftStr = xpathFuncString (&leftResult );
           DBG(fprintf(stderr, "leftStr='%s'\n", leftStr);)
           if      (step->intvalue == f_string)
               rsSetString (result, leftStr);
           else if (step->intvalue == f_stringLength) 
               rsSetInt (result, strlen(leftStr));
           else {
               pwhite = 1;
               pfrom = pto = leftStr;
               while (*pfrom) {
                   switch (*pfrom) {
                       case ' ' : case '\n': case '\r': case '\t':
                           if (!pwhite) {
                               *pto++ = ' ';
                               pwhite = 1;
                           }
                           break;
                       default:
                           *pto++ = *pfrom;
                           pwhite = 0;
                           break;
                   }
                   pfrom++;
               }
               if ((pto > leftStr) && (*(pto-1) == ' ')) {
                   pto--;  /* cut last empty space */
               }
               *pto = '\0';
               rsSetString (result, leftStr);
           }
           xpathRSFree( &leftResult );
           free(leftStr);
           break;
      
    case f_not:
           XPATH_ARITYCHECK(step,1,errMsg);
           xpathRSInit (&leftResult);
           rc = xpathEvalStep( step->child, ctxNode, exprContext, position,
                                   nodeList, cbs, &leftResult, docOrder, errMsg);
           if (rc) {
               xpathRSFree (&leftResult);
               return rc;
           }
           left = xpathFuncBoolean(&leftResult);
           xpathRSFree (&leftResult);
           rsSetBool (result, !left);
           break;
      
    case f_true:
           XPATH_ARITYCHECK(step,0,errMsg);
           rsSetBool (result, 1);
           break;

    case f_false:
           XPATH_ARITYCHECK(step,0,errMsg);
           rsSetBool (result, 0);
           break;
      
    case f_id:
           XPATH_ARITYCHECK(step,1,errMsg);
           xpathRSInit (&leftResult);
           rc = xpathEvalStep( step->child, ctxNode, exprContext, position,
                               nodeList, cbs, &leftResult, docOrder, errMsg);
           if (rc) {
               xpathRSFree( &leftResult );
               return rc;
           }
           DBG(fprintf(stderr, "id: \n");
               rsPrint(&leftResult);
           )
           if (leftResult.type == EmptyResult) {
               *errMsg = (char*)strdup ("id() requires an argument!");
               return XPATH_EVAL_ERR;
           }
           if (leftResult.type == xNodeSetResult) {
               for (i=0; i < leftResult.nr_nodes; i++) {
                   leftStr = xpathFuncStringForNode (leftResult.nodes[i]);
                   entryPtr = Tcl_FindHashEntry (ctxNode->ownerDocument->ids,
                                                 leftStr);
                   if (entryPtr) {
                       node = (domNode*) Tcl_GetHashValue (entryPtr);
                       /* Don't report nodes out of the fragment list */
                       if (node->parentNode != NULL || 
                           (node == node->ownerDocument->documentElement)) {
                           checkRsAddNode (result, node);
                       }
                   }
                   free (leftStr);
                   /*xpathRSFree (&newNodeList);*/
               }
           } else {
               leftStr = xpathFuncString (&leftResult);
               from = 0;
               pwhite = 0;
               pfrom = pto = leftStr;
               while (*pto) {
                   switch (*pto) {
                   case ' ' : case '\n': case '\r': case '\t':
                       if (pwhite) {
                           pto++;
                           continue;
                       }
                       *pto = '\0';
                       entryPtr = Tcl_FindHashEntry (ctxNode->ownerDocument->ids,
                                                     pfrom);
                       if (entryPtr) {
                           node = (domNode*) Tcl_GetHashValue (entryPtr);
                           /* Don't report nodes out of the fragment list */
                           if (node->parentNode != NULL || 
                               (node == node->ownerDocument->documentElement)) {
                               checkRsAddNode (result, node);
                           }
                       }
                       pwhite = 1;
                       pto++;
                       continue;
                   default:
                       if (pwhite) {
                           pfrom = pto;
                           pwhite = 0;
                       }
                       pto++;
                   }
               }
               if (!pwhite) {
                   entryPtr = Tcl_FindHashEntry (ctxNode->ownerDocument->ids,
                                                 pfrom);
                   if (entryPtr) {
                       node = (domNode*) Tcl_GetHashValue (entryPtr);
                       /* Don't report nodes out of the fragment list */
                       if (node->parentNode != NULL || 
                           (node == node->ownerDocument->documentElement)) {
                           rsAddNode (result, node);
                       }
                   }
               }
               free (leftStr);
           }
           sortByDocOrder (result);
           xpathRSFree (&leftResult);
           break;
      
    case f_sum:
           XPATH_ARITYCHECK(step,1,errMsg);
           xpathRSInit (&leftResult);
           rc = xpathEvalStep( step->child, ctxNode, exprContext, position,
                               nodeList, cbs, &leftResult, docOrder, errMsg);
           if (rc) {
               xpathRSFree( &leftResult );
               return rc;
           }
           if (leftResult.type != xNodeSetResult) {
               if (leftResult.type == EmptyResult) {
                   rsSetInt (result, 0);
                   xpathRSFree( &leftResult );
                   return XPATH_OK;
               } else {
                   xpathRSFree( &leftResult );
                   *errMsg = (char*)strdup("sum() requires a node set!");
                   xpathRSFree( &leftResult );
                   return XPATH_EVAL_ERR;
               }
           }
      
           xpathRSInit(&rightResult);
           rightResult.nr_nodes = 1;
           rightResult.type     = leftResult.type;
           leftReal = 0.0;
           for (i=0; i<leftResult.nr_nodes; i++) {
               rightResult.nodes = &(leftResult.nodes[i]);
               DBG(fprintf(stderr, "leftReal = %f \n", leftReal);)
               leftReal += xpathFuncNumber(&rightResult, &NaN);
               if (NaN) { 
                   leftReal = strtod ("nan", &leftStr);
                   rsSetReal (result, leftReal);
                   return XPATH_OK;
               }
               DBG(fprintf(stderr, "leftReal = %f \n", leftReal);)
           }
           rsSetReal (result, leftReal);
           xpathRSFree( &leftResult );
           break;
           
    case f_lang:
           XPATH_ARITYCHECK(step,1,errMsg);
           xpathRSInit (&leftResult);
           rc = xpathEvalStep( step->child, ctxNode, exprContext, position,
                               nodeList, cbs, &leftResult, docOrder, errMsg);
           if (rc) {
               xpathRSFree (&leftResult);
               return rc;
           }
           leftStr = xpathFuncString (&leftResult);
           if (ctxNode->nodeType != ELEMENT_NODE) {
               node = ctxNode->parentNode;
           } else {
               node = ctxNode;
           }
           while (node) {
               attr = node->firstAttr;
               while (attr) {
                   if (strcmp (attr->nodeName, "xml:lang")!=0) {
                       attr = attr->nextSibling;
                       continue;
                   }
                   tcldom_tolower (attr->nodeValue, tmp, 80);
                   tcldom_tolower (leftStr, tmp1, 80);
                   if (strcmp (tmp, tmp1)==0) {
                       rsSetBool (result, 1);
                       free (leftStr);
                       xpathRSFree (&leftResult);
                       return XPATH_OK;
                   } else {
                       pfrom = tmp;
                       i = 0;
                       while (*pfrom && i < 79) {
                           if (*pfrom == '-') {
                               *pfrom = '\0';
                               break;
                           }
                           pfrom++;
                           i++;
                       }
                       if (strcmp (tmp, tmp1)==0) {
                           rsSetBool (result, 1);
                           free (leftStr);
                           xpathRSFree (&leftResult);
                           return XPATH_OK;
                       } else {
                           rsSetBool (result, 0);
                           free (leftStr);
                           xpathRSFree (&leftResult);
                           return XPATH_OK;
                       }
                   }
               }
               node = node->parentNode;
           }
           rsSetBool (result, 0);
           free (leftStr);
           xpathRSFree (&leftResult);
           break;
      
    case f_startsWith:
    case f_contains:
    case f_substringBefore:
    case f_substringAfter:
           XPATH_ARITYCHECK(step,2,errMsg);
           xpathRSInit (&leftResult);
           xpathRSInit (&rightResult);
      
           savedDocOrder = *docOrder;
           rc = xpathEvalStep( step->child, ctxNode, exprContext, position, nodeList,
                               cbs, &leftResult, docOrder, errMsg);
           CHECK_RC;
           *docOrder = savedDocOrder;
      
           rc = xpathEvalStep( step->child->next, ctxNode, exprContext, position,
                               nodeList, cbs, &rightResult, docOrder, errMsg);
           CHECK_RC;
           *docOrder = savedDocOrder;
      
      
           DBG(fprintf(stderr, "\nsubstring-* left,right:\n");
               rsPrint(&leftResult);
               rsPrint(&rightResult);
           )
           leftStr  = xpathFuncString( &leftResult  );
           rightStr = xpathFuncString( &rightResult );
           DBG(fprintf(stderr, "substring-* '%s' '%s' \n", leftStr, rightStr);)
           if (step->intvalue == f_contains) {
               if (strstr(leftStr, rightStr) != NULL) {
                   rsSetBool (result, 1);
               } else {
                   rsSetBool (result, 0);
               }
           } else
           if (step->intvalue == f_substringBefore) {
               pfrom = strstr(leftStr, rightStr);
               if (pfrom != NULL) {
                   DBG(fprintf(stderr, "substring-before '%s' '%s' : ", leftStr, rightStr);)
                   *pfrom = '\0';
                   DBG(fprintf(stderr, "'%s' \n", leftStr);)
                   rsSetString (result, leftStr);
               } else {
                   rsSetString (result, "");
               }
           } else
           if (step->intvalue == f_substringAfter) {
               pfrom = strstr(leftStr, rightStr);
               if (pfrom != NULL) {
                   rsSetString (result, pfrom + strlen (rightStr));
               } else {
                   rsSetString (result, "");
               }
           } else {
               /* starts-with */
               i = strlen(rightStr);
               if(strncmp(leftStr, rightStr, i)==0) {
                   rsSetBool (result, 1);
               } else {
                   rsSetBool (result, 0);
               }
           }
           xpathRSFree (&leftResult);
           xpathRSFree (&rightResult);
           free(rightStr);
           free(leftStr);
           break;
      
    case f_concat:
           nextStep = step->child;
           pto = malloc(1);
           *pto = '\0';
           len = 0;
           while (nextStep) {
               xpathRSInit (&leftResult);
               savedDocOrder = *docOrder;
               rc = xpathEvalStep( nextStep, ctxNode, exprContext, position,
                                   nodeList, cbs, &leftResult, docOrder, errMsg);
               CHECK_RC;
               *docOrder = savedDocOrder;
      
               leftStr  = xpathFuncString( &leftResult  );
               pto = (char*)realloc(pto, 1+len+strlen(leftStr));
               memmove(pto + len, leftStr, strlen(leftStr));
               len += strlen(leftStr);
               *(pto + len) = '\0';
               xpathRSFree( &leftResult );
               free(leftStr);
               nextStep = nextStep->next;
           }
           rsSetString (result, pto);
           free(pto);
           break;
           
    case f_substring:
           xpathRSInit (&leftResult);
           savedDocOrder = *docOrder;
           rc = xpathEvalStep( step->child, ctxNode, exprContext, position, nodeList,
                               cbs, &leftResult, docOrder, errMsg);
           CHECK_RC;
           *docOrder = savedDocOrder;
      
           xpathRSInit (&rightResult);
           rc = xpathEvalStep( step->child->next, ctxNode, exprContext, position,
                               nodeList, cbs, &rightResult, docOrder, errMsg);
           CHECK_RC;
           *docOrder = savedDocOrder;
      
           leftStr  = xpathFuncString( &leftResult );
           xpathRSFree (&leftResult);
           from = xpathRound(xpathFuncNumber(&rightResult, &NaN))-1;
           xpathRSFree( &rightResult );
           if (NaN) {
               free (leftStr);
               rsSetString (result, "");
               return XPATH_OK;
           }
      
           if (step->child->next->next) {
               xpathRSInit (&rightResult);
               savedDocOrder = *docOrder;
               rc = xpathEvalStep( step->child->next->next, ctxNode, exprContext, 
                                   position, nodeList, cbs, &rightResult, docOrder,
                                   errMsg);
               CHECK_RC;
               *docOrder = savedDocOrder;
      
               dRight = xpathFuncNumber (&rightResult, &NaN);
               len = xpathRound(dRight);
               xpathRSFree (&rightResult);
               if (NaN) {
                   if (isinf (dRight) == 1) {
                       len = INT_MAX;
                   } else {
                       free (leftStr);
                       rsSetString (result, "");
                       return XPATH_OK;
                   }
               }
               xpathRSFree (&rightResult);
               if (from < 0) {
                   len = len + from;
                   if (len <= 0) {
                       free (leftStr);
                       rsSetString (result, "");
                       return XPATH_OK;
                   }
                   from = 0;
               }
           } else {
               if (from < 0) from = 0;
               len = strlen(leftStr) - from;
           }
      
           if (from >= strlen(leftStr)) {
               rsSetString (result, "");
               free(leftStr);
               return XPATH_OK;
           } else {
               if ( (len == INT_MAX) || ((from + len) > strlen(leftStr)) ) {
                   len =  strlen(leftStr) - from;
               }
           }
           DBG(fprintf(stderr, "substring leftStr='%s' from=%d len=%d \n",
                           leftStr, from, len);
           )
      
           *(leftStr+from+len) = '\0';
           rsSetString (result, (leftStr+from));
           free(leftStr);
           break;
           
    case f_translate:
           XPATH_ARITYCHECK(step,3,errMsg);
           xpathRSInit (&leftResult);
           savedDocOrder = *docOrder;
           rc = xpathEvalStep( step->child, ctxNode, exprContext, position, nodeList,
                               cbs, &leftResult, docOrder, errMsg);
           CHECK_RC;
           *docOrder = savedDocOrder;
           xpathRSInit (&rightResult);
           rc = xpathEvalStep( step->child->next, ctxNode, exprContext, position,
                               nodeList, cbs, &rightResult, docOrder, errMsg);
           CHECK_RC;
           *docOrder = savedDocOrder;
           xpathRSInit (&replaceResult);
           rc = xpathEvalStep( step->child->next->next, ctxNode, exprContext, 
                               position, nodeList, cbs, &replaceResult, docOrder,
                               errMsg);
           CHECK_RC;
           *docOrder = savedDocOrder;
           leftStr    = xpathFuncString( &leftResult    );
           rightStr   = xpathFuncString( &rightResult   );
           replaceStr = xpathFuncString( &replaceResult );
      
      
#if TcOnly8Bits
           len = strlen(replaceStr);
           pfrom = pto = leftStr;
           while (*pfrom) {
               fStr = strchr(rightStr, *pfrom);
               if (fStr == NULL) {
                   *pto++ = *pfrom;
               } else {
                   i = (fStr - rightStr);
                   if (i < len) {
                       *pto++ = *(replaceStr+i);
                   }
               }
               pfrom++;
           }
           *pto = '\0';
           rsSetString (result, leftStr);
#else 
           Tcl_DStringInit (&tstr);
           Tcl_DStringInit (&tfrom);
           Tcl_DStringInit (&tto);
           Tcl_DStringInit (&tresult);
      
           Tcl_UtfToUniCharDString (leftStr, -1, &tstr);
           Tcl_UtfToUniCharDString (rightStr, -1, &tfrom);
           Tcl_UtfToUniCharDString (replaceStr, -1, &tto);
      
           lenstr  = Tcl_DStringLength (&tstr) / sizeof (Tcl_UniChar);
           fromlen = Tcl_DStringLength (&tfrom) / sizeof (Tcl_UniChar); 
           len     = Tcl_DStringLength (&tto) / sizeof (Tcl_UniChar);
           
           upfrom = (Tcl_UniChar *)Tcl_DStringValue (&tstr);
           for (i = 0; i < lenstr; i++) {
               found = 0;
               ufStr = (Tcl_UniChar *)Tcl_DStringValue (&tfrom);
               for (j = 0; j < fromlen; j++) {
                   if (*ufStr == *upfrom) {
                       found = 1;
                       break;
                   }
                   ufStr++;
               }
               if (found) {
                   if (j < len) {
                       unichar = Tcl_UniCharAtIndex (replaceStr, j);
                       utfCharLen = Tcl_UniCharToUtf (unichar, utfBuf);
                       Tcl_DStringAppend (&tresult, utfBuf, utfCharLen);
                   }
               } else {
                   utfCharLen = Tcl_UniCharToUtf (*upfrom, utfBuf);
                   Tcl_DStringAppend (&tresult, utfBuf, utfCharLen);
               }
               upfrom++;
           }
           rsSetString (result, Tcl_DStringValue (&tresult));
           Tcl_DStringFree (&tstr);
           Tcl_DStringFree (&tfrom);
           Tcl_DStringFree (&tto);
           Tcl_DStringFree (&tresult);
#endif               
      
           xpathRSFree( &replaceResult );
           xpathRSFree( &rightResult   );
           xpathRSFree( &leftResult    );
           free(leftStr); free(rightStr); free(replaceStr);
           break;
           
    case f_count:
           XPATH_ARITYCHECK(step,1,errMsg);
           xpathRSInit (&leftResult);
           rc = xpathEvalStep( step->child, ctxNode, exprContext, position,
                               nodeList, cbs, &leftResult, docOrder, errMsg);
           if (rc) {
               xpathRSFree( &leftResult );
               return rc;
           }
           if (leftResult.type == EmptyResult) {
               rsSetInt (result, 0);
               return XPATH_OK;
           } 
           if (leftResult.type != xNodeSetResult) {
               *errMsg = (char*)strdup("count() requires a node set!");
               xpathRSFree( &leftResult );
               return XPATH_EVAL_ERR;
           }
           rsSetInt (result, leftResult.nr_nodes);
           xpathRSFree (&leftResult);
           break;
           
    case f_unparsedEntityUri:
           XPATH_ARITYCHECK(step,1,errMsg);
           xpathRSInit (&leftResult);
           rc = xpathEvalStep( step->child, ctxNode, exprContext, position,
                               nodeList, cbs, &leftResult, docOrder, errMsg);
           if (rc) {
               xpathRSFree( &leftResult );
               return rc;
           }
           leftStr = xpathFuncString (&leftResult);
           entryPtr = Tcl_FindHashEntry (ctxNode->ownerDocument->unparsedEntities, 
                                         leftStr);
           if (entryPtr) {
               rsSetString (result, (char *)Tcl_GetHashValue (entryPtr));
           } else {
               rsSetString (result, "");
           }
           free (leftStr);
           break;
               
    case f_localName:
    case f_name:
    case f_namespaceUri:
    case f_generateId:
           xpathRSInit (&leftResult);
           if (step->child == NULL) {
               /*  no parameter, the context node is the nodeset to
                *  operate with
                */
               rsAddNode( &leftResult, ctxNode);
           } else {
               XPATH_ARITYCHECK(step,1,errMsg);
               xpathRSInit (&leftResult);
               rc = xpathEvalStep( step->child, ctxNode, exprContext, position,
                                   nodeList, cbs, &leftResult, docOrder, errMsg);
               if (rc) {
                   xpathRSFree( &leftResult );
                   return rc;
               }
           }
           if (leftResult.type == EmptyResult) {
               rsSetString (result, "");
               return XPATH_OK;
           }
           
           if (step->intvalue == f_generateId) {
               if (leftResult.type != xNodeSetResult) {
                   *errMsg = (char*)strdup("generate-id() requires a nodeset or no argument!");
                   xpathRSFree (&leftResult);
                   return XPATH_EVAL_ERR;
               }
               if (leftResult.nodes[0]->nodeType == ATTRIBUTE_NODE) {
                   node = ((domAttrNode*)leftResult.nodes[0])->parentNode;
                   i = 0;
                   attr = node->firstAttr;
                   while (attr) {
                       if ((domNode*)attr == leftResult.nodes[0]) break;
                       attr = attr->nextSibling;
                       i++;
                   }
                   sprintf(tmp,"node%d-%d", node->nodeNumber, i);
               } else {
                   sprintf(tmp,"node%d", leftResult.nodes[0]->nodeNumber);
               }
               rsSetString (result, tmp);
           } else
           
           if (step->intvalue == f_namespaceUri) {
               if (leftResult.type != xNodeSetResult) {
                   *errMsg = (char*)strdup("namespace-uri() requires a node set!");
                   xpathRSFree( &leftResult );
                   return XPATH_EVAL_ERR;
               }
               if ( (leftResult.nr_nodes <= 0) 
                    || (   leftResult.nodes[0]->nodeType != ELEMENT_NODE
                        && leftResult.nodes[0]->nodeType != ATTRIBUTE_NODE )
                  )
               {
                   rsSetString (result, "");
               } else {
                   rsSetString (result, domNamespaceURI(leftResult.nodes[0]));
               }
           } else
           
           if (step->intvalue == f_localName) {
               if (leftResult.type != xNodeSetResult) {
                   *errMsg = (char*)strdup("local-name() requires a node set!");
                   xpathRSFree( &leftResult );
                   return XPATH_EVAL_ERR;
               }
               if (leftResult.nodes[0]->nodeType == ELEMENT_NODE) {
                   if (leftResult.nodes[0] == 
                       leftResult.nodes[0]->ownerDocument->rootNode) {
                       rsSetString (result, "");
                   } else {
                       rsSetString (result, domGetLocalName(leftResult.nodes[0]->nodeName));
                   } 
               } else 
               if (leftResult.nodes[0]->nodeType == ATTRIBUTE_NODE) {
                   leftStr = domGetLocalName(((domAttrNode*)leftResult.nodes[0])->nodeName);
                   if (leftStr[0] == 'x' && strcmp(leftStr, "xmlns")==0) {
                       rsSetString (result, "");
                   } else {
                       rsSetString (result, leftStr);
                   }
               } else 
               if (leftResult.nodes[0]->nodeType == PROCESSING_INSTRUCTION_NODE) {
                   if (((domProcessingInstructionNode*)leftResult.nodes[0])->targetLength > 79) {
                       memmove(tmp, ((domProcessingInstructionNode*)leftResult.nodes[0])->targetValue, 79);
                       tmp[79]= '\0';
                   } else {
                       memmove(tmp, ((domProcessingInstructionNode*)leftResult.nodes[0])->targetValue,
                               ((domProcessingInstructionNode*)leftResult.nodes[0])->targetLength);
                       tmp[((domProcessingInstructionNode*)leftResult.nodes[0])->targetLength] = '\0';
                   }
                   rsSetString (result, tmp);
               } else {
                   rsSetString (result, "");
               }
           } else
           
           if (step->intvalue == f_name) {
               if (   leftResult.type != xNodeSetResult ) {
                   *errMsg = (char*)strdup("name() requires a node set!");
                   xpathRSFree( &leftResult );
                   return XPATH_EVAL_ERR;
               }
               if (leftResult.nodes[0]->nodeType == ELEMENT_NODE) {
                   if (leftResult.nodes[0] == 
                       leftResult.nodes[0]->ownerDocument->rootNode) {
                       rsSetString (result, "");
                   } else {
                       rsSetString (result, leftResult.nodes[0]->nodeName);
                   }
               } else 
               if (leftResult.nodes[0]->nodeType == ATTRIBUTE_NODE) {
                   if (leftResult.nodes[0]->nodeFlags & IS_NS_NODE) {
                       if (((domAttrNode *)leftResult.nodes[0])->nodeName[5] == '\0') {
                           rsSetString (result, "");
                       } else {
                           rsSetString (result, &((domAttrNode*)leftResult.nodes[0])->nodeName[6]);
                       }
                   } else {
                       rsSetString (result, ((domAttrNode*)leftResult.nodes[0])->nodeName );
                   }
               } else 
               if (leftResult.nodes[0]->nodeType == PROCESSING_INSTRUCTION_NODE) {
                   if (((domProcessingInstructionNode*)leftResult.nodes[0])->targetLength > 79) {
                       memmove(tmp, ((domProcessingInstructionNode*)leftResult.nodes[0])->targetValue, 79);
                       tmp[79]= '\0';
                   } else {
                       memmove(tmp, ((domProcessingInstructionNode*)leftResult.nodes[0])->targetValue,
                               ((domProcessingInstructionNode*)leftResult.nodes[0])->targetLength);
                       tmp[((domProcessingInstructionNode*)leftResult.nodes[0])->targetLength] = '\0';
                   }
                   rsSetString (result, tmp);
               } else {
                   rsSetString (result, "");
               }
           }
           xpathRSFree( &leftResult );
           break;

    default:
           if (cbs->funcCB != NULL) {
               /* count number of arguments (to be able to allocate later) */
               argc = 0;
               nextStep = step->child;
               while (nextStep) {
                   argc++;
                   nextStep = nextStep->next;
               }
               args = (xpathResultSets*) malloc( (argc+1) * sizeof(xpathResultSets));
               args[0] = NULL;
               argc = 0;
               nextStep = step->child;
               savedDocOrder = *docOrder;
               while (nextStep) {
                   arg = (xpathResultSet*) malloc (sizeof(xpathResultSet));
                   args[argc++] = arg;
                   args[argc]   = NULL;
                   xpathRSInit (arg);
                   rc = xpathEvalStep( nextStep, ctxNode, exprContext, position,
                                       nodeList, cbs, arg, docOrder, errMsg);
                   CHECK_RC;
                   *docOrder = savedDocOrder;
                   nextStep = nextStep->next;
               }
               rc = (cbs->funcCB) (cbs->funcClientData, step->strvalue,
                                   ctxNode, position, nodeList, argc, args,
                                   result, errMsg);
               argc = 0;
               while ( args[argc] != NULL) {
                   xpathRSFree( args[argc++] );
               }
               free(args);
               return rc;
           } else {
               if (strlen(step->strvalue)>50) *(step->strvalue + 50) = '\0';
               sprintf(tmp, "Unknown function '%s'!", step->strvalue);
               *errMsg = (char*)strdup(tmp);
           }
           return XPATH_EVAL_ERR;
    }
    return XPATH_OK;
}

/*----------------------------------------------------------------------------
|   xpathEvalStep
|
\---------------------------------------------------------------------------*/
static int xpathEvalStep (
    ast                step,
    domNode           *ctxNode,
    domNode           *exprContext,
    int                position,
    xpathResultSet    *nodeList,
    xpathCBs          *cbs,
    xpathResultSet    *result,
    int               *docOrder,
    char             **errMsg
)
{
    xpathResultSet   leftResult, rightResult;
    xpathResultSet  *pleftResult, *prightResult, tResult;
    int              i, j, k, rc, res, NaN, switchResult;
    domNode         *node, *child, *startingNode, *ancestor;
    domAttrNode     *attr;
    domNS           *ns;
    int              savedDocOrder;
    unsigned int     leftNodeNr, rightNodeNr;
    int              left = 0, right = 0, useFastAdd;
    double           dLeft = 0.0, dRight = 0.0, dTmp;
    char            *leftStr = NULL, *rightStr = NULL;

    if (result->type == EmptyResult) useFastAdd = 1;
    else useFastAdd = 0;

    switch (step->type) {
        
    case AxisChild:
        DBG(fprintf(stderr, "AxisChild ctxNode->nodeType = %d \n", ctxNode->nodeType);)
        *docOrder = 1;
        if (ctxNode->nodeType != ELEMENT_NODE) return XPATH_OK;
        DBG(fprintf(stderr, "AxisChild: scanning \n");)
        child = ctxNode->firstChild;
        while (child) {
            DBG(fprintf(stderr, "AxisChild: child '%s' %d \n", child->nodeName, child->nodeNumber);)
            if (xpathNodeTest(child, exprContext, step)) {
                DBG(fprintf(stderr, "AxisChild: after node taking child '%s' %d \n", child->nodeName, child->nodeNumber);)
                checkRsAddNode( result, child);
            }
            child = child->nextSibling;
        }
        DBG( fprintf(stderr,"AxisChild result:\n");
             rsPrint(result);
        )
        break;

    case AxisDescendant:
    case AxisDescendantLit:
    case AxisDescendantOrSelf:
    case AxisDescendantOrSelfLit:
        *docOrder = 1;

        if (ctxNode->nodeType == ATTRIBUTE_NODE
            && (
                step->type == AxisDescendantOrSelf
                || step->type == AxisDescendantOrSelfLit)) {
            if (xpathNodeTest(ctxNode, exprContext, step)) 
                rsAddNode( result, ctxNode);
            break;
        }
        if (ctxNode->nodeType != ELEMENT_NODE) return XPATH_OK;
        if (step->type == AxisDescendantOrSelf
            || step->type == AxisDescendantOrSelfLit) {
            if (xpathNodeTest(ctxNode, exprContext, step)) 
                rsAddNode( result, ctxNode);
        }

        startingNode = ctxNode;
        node = ctxNode->firstChild;
        while (node && node != startingNode) {
            if (xpathNodeTest(node, exprContext, step)) checkRsAddNode( result, node);
            if ((node->nodeType == ELEMENT_NODE) && (node->firstChild)) {
                node = node->firstChild;
                continue;
            }
            if (node->nextSibling) {
                node = node->nextSibling;
                continue;
            }
            while ( node->parentNode &&
                    (node->parentNode != startingNode) &&
                    (node->parentNode->nextSibling == NULL) ) {
                
                node = node->parentNode;
            }
            if ((node != startingNode) &&
                (node->parentNode)     &&
                (node->parentNode != startingNode)
                ) {
                node = node->parentNode->nextSibling;
            } else {
                break;
            }
        }
        break;

    case AxisSelf:
        *docOrder = 1;
        DBG(fprintf(stderr, "AxisSelf :: \n");)
        if (xpathNodeTest(ctxNode, exprContext, step)) {
            rsAddNode( result, ctxNode);
        }
        break;
    
    case GetContextNode:
        rsAddNode( result, ctxNode);
        break;

    case AxisAttribute:
        *docOrder = 1;
        DBG(fprintf(stderr, "AxisAttribute %s \n", step->child->strvalue);)
        if (ctxNode->nodeType != ELEMENT_NODE) return XPATH_OK;
        if (step->child->type == IsElement) {
            step->child->type = IsAttr;
        }
        if (step->child->type == IsAttr) {
            if (strcmp(step->child->strvalue, "*")==0) {
                attr = ctxNode->firstAttr;
                while (attr) {
                    if (!(attr->nodeFlags & IS_NS_NODE)) {
                        checkRsAddNode (result, (domNode *)attr);
                    }
                    attr = attr->nextSibling;
                }
            } else {
                attr = ctxNode->firstAttr;
                while (attr) {
                    if (xpathNodeTest( (domNode*)attr, exprContext, step)) 
                        checkRsAddNode (result, (domNode *)attr);
                    attr = attr->nextSibling;
                }
            }
        } else 
        if (step->child->type == IsNSAttr) {
            attr = ctxNode->firstAttr;
            while (attr && (attr->nodeFlags & IS_NS_NODE))
                attr = attr->nextSibling;
            while (attr) {
                if (xpathNodeTest ( (domNode*)attr, exprContext, step))
                    checkRsAddNode (result, (domNode *)attr);
                attr = attr->nextSibling;
            }
        }
        break;

    case AxisParent:
        *docOrder = 1;
        if (ctxNode->nodeType == ATTRIBUTE_NODE) {
            if (xpathNodeTest(((domAttrNode *)ctxNode)->parentNode, exprContext, step)) {
                rsAddNode(result,((domAttrNode *)ctxNode)->parentNode);
            }
        } else {
            if (ctxNode->parentNode) {
                if (xpathNodeTest(ctxNode->parentNode, exprContext, step)) {
                    rsAddNode(result,ctxNode->parentNode);
                }
            } else {
                if (xpathNodeTest (ctxNode->ownerDocument->rootNode, exprContext, 
                                   step)) {
                    rsAddNode (result, ctxNode->ownerDocument->rootNode);
                }
            }
        }
        break;
    
    case GetParentNode:
        if (ctxNode->nodeType == ATTRIBUTE_NODE) {
            rsAddNode(result,((domAttrNode*)ctxNode)->parentNode);
        } else {
            if (ctxNode->parentNode) {
                rsAddNode(result,ctxNode->parentNode);
            } else {
                if (ctxNode != ctxNode->ownerDocument->rootNode) {
                    rsAddNode (result, ctxNode->ownerDocument->rootNode);
                }
            }
        }
        break;

    case AxisAncestor:
    case AxisAncestorOrSelf:
        *docOrder = 0;
        xpathRSInit (&tResult);
        if (step->type == AxisAncestorOrSelf) {
            if (xpathNodeTest(ctxNode, exprContext, step))
                rsAddNodeFast(&tResult, ctxNode);
        }
        if (ctxNode->nodeType == ATTRIBUTE_NODE) {
            ctxNode = ((domAttrNode *)ctxNode)->parentNode;
            if (xpathNodeTest(ctxNode, exprContext, step)) 
                rsAddNodeFast(&tResult, ctxNode);
        }
        while (ctxNode->parentNode) {
            ctxNode = ctxNode->parentNode;
            if (xpathNodeTest(ctxNode, exprContext, step))
                rsAddNodeFast(&tResult, ctxNode);
        }
        if (xpathNodeTest (ctxNode->ownerDocument->rootNode, exprContext, step))
            rsAddNode (&tResult, ctxNode->ownerDocument->rootNode);
        for (i = tResult.nr_nodes - 1; i >= 0;  i--) {
            checkRsAddNode (result, tResult.nodes[i]);
        }
        xpathRSFree (&tResult);
        break;

    case AxisFollowingSibling:
        *docOrder = 1;
        if (ctxNode->nodeType == ATTRIBUTE_NODE) {
            return XPATH_OK;
        }
        while (ctxNode->nextSibling) {
            ctxNode = ctxNode->nextSibling;
            if (xpathNodeTest(ctxNode, exprContext, step)) checkRsAddNode(result, ctxNode);
        }
        break;

    case AxisPrecedingSibling:
        *docOrder = 0;
        if (ctxNode->nodeType == ATTRIBUTE_NODE) {
            return XPATH_OK;
        }
        startingNode = ctxNode;
        if (startingNode->parentNode) {
            node = (startingNode->parentNode)->firstChild;
        } else {
            return XPATH_OK;
        }
        while (node != startingNode) {
            if (xpathNodeTest(node, exprContext, step)) checkRsAddNode(result, node);
            node = node->nextSibling;
        }
        break;
    
    case AxisFollowing:
        *docOrder = 1;
        if (ctxNode->nodeType == ATTRIBUTE_NODE) {
            node = ((domAttrNode *)ctxNode)->parentNode->firstChild;
        } else {
            node = ctxNode;
            if (node->nextSibling) {
                node = node->nextSibling;
            } else {
                while (node->parentNode) {
                    node = node->parentNode;
                    if (node->nextSibling) break;
                }
                if (!node->nextSibling) return XPATH_OK;
                else node = node->nextSibling;
            }
        }
        while (1) {
            if (xpathNodeTest (node, exprContext, step)) checkRsAddNode (result, node);
            if (node->nodeType == ELEMENT_NODE &&
                node->firstChild) {
                node = node->firstChild;
            } else 
            if (node->nextSibling) {
                node = node->nextSibling;
            } else {
                while (node->parentNode) {
                    node = node->parentNode;
                    if (node->nextSibling) break;
                }
                if (!node->nextSibling) break;
                node = node->nextSibling;
            }
        }
        break;

    case AxisPreceding:
        *docOrder = 0;
        if (ctxNode->nodeType == ATTRIBUTE_NODE) {
            ancestor = node = ((domAttrNode *)ctxNode)->parentNode;
        } else {
            ancestor = node = ctxNode;
        }
        i = 0;
        while (node->parentNode) {
            ancestor = node;
            node = node->parentNode;
            i++;
        }
        startingNode = node->firstChild;
        for (; i > 0; i--) {
            if (!startingNode)
                continue;
            if (startingNode->nodeType == ELEMENT_NODE) {
                node = startingNode->firstChild;
            } else {
                node = NULL;
            }
            while (startingNode != ancestor) {
                if (xpathNodeTest(startingNode, exprContext, step))
                    checkRsAddNode(result, startingNode);
                while ((node) && (node != startingNode)) {
                   if (xpathNodeTest(node,exprContext, step)) checkRsAddNode(result, node);
                   if ((node->nodeType == ELEMENT_NODE) &&
                       (node->firstChild)) {
                       node = node->firstChild;
                       continue;
                   }
                   if (node->nextSibling) {
                       node = node->nextSibling;
                       continue;
                   }
                   while ((node->parentNode != startingNode) &&
                          (node->parentNode->nextSibling == NULL)) {

                       node = node->parentNode;
                   }
                   if (node->parentNode != startingNode) {
                       node = node->parentNode->nextSibling;
                   } else {
                       break;
                   }
                }
                startingNode = startingNode->nextSibling;
                if (startingNode->nodeType == ELEMENT_NODE) {
                    node = startingNode->firstChild;
                } else {
                    node = NULL;
                }
            }
            if (ctxNode->nodeType == ATTRIBUTE_NODE) {
                node = ((domAttrNode *)ctxNode)->parentNode;
            } else {
                node = ctxNode;
            }
            for (j = 0; j < i - 1 ; j++) {
                ancestor = node;
                node = node->parentNode;
            }
            if (node->nodeType == ELEMENT_NODE) {
                startingNode = node->firstChild;
            } else {
                startingNode = NULL;
            }
        }
        break;

    case AxisNamespace:
        *docOrder = 1;
        if (ctxNode->nodeType == ELEMENT_NODE) {
            node = ctxNode;
        } else
        if (ctxNode->nodeType == ATTRIBUTE_NODE) {
            node = ((domAttrNode*) ctxNode)->parentNode;
        } else return XPATH_OK;

        while (node) {
            attr = node->firstAttr;
            while (attr && (attr->nodeFlags & IS_NS_NODE)) {
                if (step->child->type == IsElement) {
                    if ((step->child->strvalue[0] != '*')) {
                        ns = domLookupPrefix (exprContext, 
                                              step->child->strvalue);
                        if (!ns) {
                            /* This is more an error: the prefix should be
                               bound to a namespace. TODO: error msg. */
                            return XPATH_OK;
                        }
                        if (strcmp(attr->nodeValue, ns->uri)!=0) {
                            attr = attr->nextSibling;
                            continue;
                        }
                    }
                }
                rc = 0;
                for (i = 0; i < result->nr_nodes; i++) {
                    if (strcmp (attr->nodeName, ((domAttrNode*)result->nodes[i])->nodeName)==0) {
                        rc = 1; break;
                    }
                }
                if (rc) {attr = attr->nextSibling; continue;}
                rsAddNodeFast (result, (domNode *)attr);
                attr = attr->nextSibling;
            }

            if (node == ctxNode->ownerDocument->documentElement) {
                if (ctxNode != ctxNode->ownerDocument->rootNode) {
                    node = ctxNode->ownerDocument->rootNode;
                } else {
                    node = NULL;
                }
            } else {
                node = node->parentNode;
            }
        }
        break;

    case GetVar:
        if (cbs->varCB) {
            rc = (cbs->varCB)(cbs->varClientData, step->strvalue, result, errMsg);
            CHECK_RC;
        }
        break;

    case Literal:
        rsSetString (result, step->strvalue);
        break;

    case Int:
        rsSetInt (result, step->intvalue);
        break;

    case Real:
        rsSetReal (result, step->realvalue);
        break;

    case Add:
    case Substract:
    case Mult:
    case Div:
    case Mod:
        xpathRSInit (&leftResult);
        xpathRSInit (&rightResult);

        savedDocOrder = *docOrder;
        rc = xpathEvalStep( step->child, ctxNode, exprContext, position,
                            nodeList, cbs, &leftResult, docOrder, errMsg);
        CHECK_RC;
        DBG( fprintf(stderr,"left:\n");
             rsPrint(&leftResult);
        )

        *docOrder = savedDocOrder;
        rc = xpathEvalStep( step->child->next, ctxNode, exprContext,  position,
                            nodeList, cbs, &rightResult, docOrder, errMsg);
        CHECK_RC;
        *docOrder = savedDocOrder;

        DBG( fprintf(stderr,"right:\n");
             rsPrint(&rightResult);
        )

        dLeft  = xpathFuncNumber(&leftResult, &NaN);
        if (NaN) { 
            rsSetReal (result, dLeft);
            xpathRSFree (&rightResult);
            xpathRSFree (&leftResult);
            return XPATH_OK;
        }
        dRight = xpathFuncNumber(&rightResult, &NaN);
        if (NaN) {
            rsSetReal (result, dRight);
            xpathRSFree (&rightResult);
            xpathRSFree (&leftResult);
            return XPATH_OK;
        }
        switch (step->type) {
        case Add:       rsSetReal (result, dLeft + dRight); break;
        case Substract: rsSetReal (result, dLeft - dRight); break;
        case Mult:      rsSetReal (result, dLeft * dRight); break;                
        case Div:
        case Mod:       
            if (dRight == 0.0) {
                if (dLeft == 0.0) {
                    dLeft = strtod ("nan", &leftStr);
                    rsSetReal (result, dLeft);
                } else {
                    if (dLeft > 0) {
                        dLeft = strtod ("inf", &leftStr);
                        rsSetReal (result, dLeft);
                    } else {
                        dLeft = strtod ("-inf", &leftStr);
                        rsSetReal (result, dLeft);
                    }
                }
            } else {
                if (step->type == Div) 
                    rsSetReal (result, dLeft / dRight);
                else 
                    rsSetInt  (result, ((int)dLeft) % ((int)dRight));
            }
            break;
        default:        break;
        }
        xpathRSFree (&rightResult);
        xpathRSFree (&leftResult);
        return XPATH_OK;

    case CombineSets:
        xpathRSInit (&leftResult);
        xpathRSInit (&rightResult);

        savedDocOrder = *docOrder;
        rc = xpathEvalStep( step->child, ctxNode, exprContext, position, nodeList,
                               cbs, &leftResult, docOrder, errMsg);
        CHECK_RC;
        DBG( fprintf(stderr,"left:\n");
             rsPrint(&leftResult);
        )

        *docOrder = savedDocOrder;
        rc = xpathEvalStep( step->child->next, ctxNode, exprContext,  position,
                            nodeList, cbs, &rightResult, docOrder, errMsg);
        CHECK_RC;
        *docOrder = savedDocOrder;

        DBG( fprintf(stderr,"right:\n");
             rsPrint(&rightResult);
        )
            
        if (((leftResult.type != xNodeSetResult)
             && (leftResult.type != EmptyResult))
            || 
            ((rightResult.type != xNodeSetResult)
             && (rightResult.type != EmptyResult))) 
        {
            *errMsg = (char*)strdup("| requires node sets!");
            xpathRSFree (&rightResult);
            xpathRSFree (&leftResult);
            return XPATH_EVAL_ERR;
        }
        if (leftResult.type == EmptyResult) {
            rsCopy (result, &rightResult);
            goto combineSetCleanup;
        } 
        if (rightResult.type == EmptyResult) {
            rsCopy (result, &leftResult);
            goto combineSetCleanup;
        }
        *docOrder = 1;
        j = k = 0;
        for (i=0; i<(leftResult.nr_nodes+rightResult.nr_nodes); i++) {
            if (leftResult.nodes[j]->nodeType == ATTRIBUTE_NODE) {
                leftNodeNr = ((domAttrNode *)leftResult.nodes[j])->parentNode->nodeNumber;
                if ((rightResult.nodes[k]->nodeType == ATTRIBUTE_NODE)
                    && (leftNodeNr == ((domAttrNode *)rightResult.nodes[k])->parentNode->nodeNumber)) {
                    /* OK, so this two attributes belongs to
                       the same element. This following
                       "sorting" of attributes is not strictly
                       necessary (see xpath rec 5.: "The
                       relative order of attribute nodes is
                       implementation-dependent").  But since
                       all 'big' players seems to do this sorting, we
                       also want to play nice.*/
                    attr = ((domAttrNode *)leftResult.nodes[j])->parentNode->firstAttr;
                    while (attr) {
                        if (attr == (domAttrNode *)leftResult.nodes[j]) {
                            rsAddNode (result, leftResult.nodes[j]);
                            j++; break;
                        } else 
                            if (attr == (domAttrNode *)rightResult.nodes[k]) {
                                rsAddNode (result, rightResult.nodes[k]);
                                k++; break;
                            }
                        attr = attr->nextSibling;
                    }
                    if (j == leftResult.nr_nodes) break;
                    if (k == rightResult.nr_nodes) break;
                    continue;                            
                } else 
                    if ((rightResult.nodes[k]->nodeType == ELEMENT_NODE)
                        && (leftNodeNr == rightResult.nodes[k]->nodeNumber)) {
                        rsAddNode (result, rightResult.nodes[k]);
                        k++;
                        if (k == rightResult.nr_nodes) break;
                        continue;
                    }
            } else {
                leftNodeNr = (leftResult.nodes[j])->nodeNumber;
            }
            if (rightResult.nodes[k]->nodeType == ATTRIBUTE_NODE) {
                rightNodeNr = ((domAttrNode *)rightResult.nodes[k])->parentNode->nodeNumber;
                if ((leftResult.nodes[j]->nodeType == ELEMENT_NODE)
                    && (rightNodeNr == leftResult.nodes[j]->nodeNumber)) {
                    rsAddNode (result, leftResult.nodes[j]);
                    j++;
                    if (j == leftResult.nr_nodes) break;
                    continue;
                }
            } else {
                rightNodeNr = (rightResult.nodes[k])->nodeNumber;
            }
            if (leftNodeNr < rightNodeNr) {
                rsAddNode (result, leftResult.nodes[j]);
                j++;
                if (j == leftResult.nr_nodes) break;
            } else {
                rsAddNode (result, rightResult.nodes[k]);
                k++;
                if (k == rightResult.nr_nodes) break;
            }
        }
        if (j < leftResult.nr_nodes) {
            for (i=j; i< leftResult.nr_nodes; i++) {
                rsAddNode ( result, leftResult.nodes[i]);
            }
        } else {
            for (i=k; i< rightResult.nr_nodes; i++) {
                rsAddNode ( result, rightResult.nodes[i]);
            }
        }
    combineSetCleanup:
        xpathRSFree (&rightResult);
        xpathRSFree (&leftResult);
        return XPATH_OK;
        
    case And:
    case Or:
    case Equal:
    case NotEqual:
        xpathRSInit (&leftResult);
        xpathRSInit (&rightResult);

        savedDocOrder = *docOrder;
        rc = xpathEvalStep( step->child, ctxNode, exprContext, position, nodeList,
                               cbs, &leftResult, docOrder, errMsg);
        CHECK_RC;
        *docOrder = savedDocOrder;

        DBG( fprintf(stderr,"left:\n");
             rsPrint(&leftResult);
        )

        /*----------------------------------------------
        |   short circuit evalution for AND/OR
        \---------------------------------------------*/
        if (step->type == And) {
            left = xpathFuncBoolean(&leftResult);
            if (!left) {
                /* left result is false, so AND result is also false */
                rsSetBool (result, left);
                xpathRSFree (&leftResult);
                return XPATH_OK;
            }
        }
        if (step->type == Or) {
            left = xpathFuncBoolean(&leftResult);
            if (left) {
                /* left result is true, so OR result is also true */
                rsSetBool (result, left);
                xpathRSFree (&leftResult);
                return XPATH_OK;
            }
        }

        rc = xpathEvalStep( step->child->next, ctxNode, exprContext,  position,
                            nodeList, cbs, &rightResult, docOrder, errMsg);
        CHECK_RC;
        *docOrder = savedDocOrder;

        DBG( fprintf(stderr,"right:\n");
             rsPrint(&rightResult);
        )
        res = 0;
        if ((step->type == And) || (step->type == Or)) {
            right = xpathFuncBoolean(&rightResult);
            if (step->type == And) res = (left && right);
            if (step->type == Or) res = (left || right);
            rsSetBool (result, res);
            xpathRSFree (&rightResult);
            xpathRSFree (&leftResult);
            return XPATH_OK;
        }

        if (   leftResult.type == xNodeSetResult
            || rightResult.type == xNodeSetResult) {
            if (leftResult.type == xNodeSetResult) {
                pleftResult = &leftResult;
                prightResult = &rightResult;
            } else {
                pleftResult = &rightResult;
                prightResult = &leftResult;
            }
            switch (prightResult->type) {
            case EmptyResult:
                res = 0;
                break;
            case xNodeSetResult:
                JDBG( fprintf(stderr,"\nleft+right result:\n");
                     rsPrint(pleftResult);
                     rsPrint(prightResult);
                )
                for (i=0; i < pleftResult->nr_nodes; i++) {
                    leftStr = xpathFuncStringForNode (pleftResult->nodes[i]);
                    for (j=0; j < prightResult->nr_nodes; j++) {
                        rightStr = xpathFuncStringForNode (prightResult->nodes[j]);
                        JDBG(fprintf(stderr, "leftStr='%s' rightStr='%s'\n", leftStr, rightStr);)
                        res = strcmp (leftStr, rightStr);
                        if      (step->type == Equal)    res = (res==0);
                        else if (step->type == NotEqual) res = (res!=0);
                        free (rightStr);
                        if (res) break;
                    }
                    free (leftStr);
                    if (res) break;
                }
                break;
            case BoolResult:
                if (step->type == Equal) res = (prightResult->intvalue != 0);
                else                     res = (prightResult->intvalue == 0);
                break;
            case IntResult:
            case RealResult:
                dRight = xpathFuncNumber (prightResult, &NaN);
                for (i=0; i < pleftResult->nr_nodes; i++) {
                    dLeft = xpathFuncNumberForNode (pleftResult->nodes[i], &NaN);
                    if (step->type == Equal) res = (dLeft == dRight);
                    else                     res = (dLeft != dRight);
                    if (res) break;
                }
                break;
            case StringResult:
                rightStr = xpathFuncString (prightResult);
                for (i=0; i < pleftResult->nr_nodes; i++) {
                    leftStr = xpathFuncStringForNode (pleftResult->nodes[i]);
                    res = strcmp (leftStr, rightStr);
                    if (step->type == Equal) res = (res == 0);
                    else                     res = (res != 0);
                    free (leftStr);
                    if (res) break;
                }
                free (rightStr);
                break;
            }
        } else 
        if (leftResult.type == BoolResult || rightResult.type == BoolResult) {
            left  = xpathFuncBoolean (&leftResult);
            right = xpathFuncBoolean (&rightResult);
            if (step->type == Equal) res = (left == right);
            else                     res = (left != right);
        } else 
        if (
               leftResult.type  == IntResult 
            || leftResult.type  == RealResult
            || rightResult.type == IntResult
            || rightResult.type == RealResult
            ) {
            if (   leftResult.type == EmptyResult 
                || rightResult.type == EmptyResult) {
                res = 0;
            } else {
                dLeft  = xpathFuncNumber (&leftResult, &NaN);
                dRight = xpathFuncNumber (&rightResult, &NaN);
                if (step->type == Equal) res = (dLeft == dRight);
                else                     res = (dLeft != dRight);
            }
        } else {
            if (   leftResult.type == EmptyResult 
                || rightResult.type == EmptyResult) {
                res = 0;
            } else {
                leftStr  = xpathFuncString (&leftResult);
                rightStr = xpathFuncString (&rightResult);
                res = strcmp (leftStr, rightStr);
                if (step->type == Equal) res = (res == 0);
                else                     res = (res != 0);
                free (leftStr);
                free (rightStr);
            }
        }
        rsSetBool (result, res);
        xpathRSFree (&rightResult);
        xpathRSFree (&leftResult);
        return XPATH_OK;

    case Less:
    case LessOrEq:
    case Greater:
    case GreaterOrEq:
        xpathRSInit (&leftResult);
        xpathRSInit (&rightResult);

        savedDocOrder = *docOrder;
        rc = xpathEvalStep( step->child, ctxNode, exprContext, position, nodeList,
                               cbs, &leftResult, docOrder, errMsg);
        CHECK_RC;
        *docOrder = savedDocOrder;

        DBG( fprintf(stderr,"left:\n");
             rsPrint(&leftResult);
        )
        rc = xpathEvalStep( step->child->next, ctxNode, exprContext,  position,
                            nodeList, cbs, &rightResult, docOrder, errMsg);
        CHECK_RC;
        *docOrder = savedDocOrder;

        DBG( fprintf(stderr,"right:\n");
             rsPrint(&rightResult);
        )
        res = 0;

        if (   leftResult.type == xNodeSetResult
            || rightResult.type == xNodeSetResult) {
            if (leftResult.type == xNodeSetResult) {
                pleftResult = &leftResult;
                prightResult = &rightResult;
                switchResult = 0;
            } else {
                pleftResult = &rightResult;
                prightResult = &leftResult;
                switchResult = 1;
            }
            switch (prightResult->type) {
            case EmptyResult:
                res = 0;
                break;
            case xNodeSetResult:
                JDBG( fprintf(stderr,"\nleft+right result:\n");
                     rsPrint(pleftResult);
                     rsPrint(prightResult);
                )
                for (i=0; i < pleftResult->nr_nodes; i++) {
                    dLeft = xpathFuncNumberForNode (pleftResult->nodes[i], &NaN);
                    for (j=0; j < prightResult->nr_nodes; j++) {
                        dRight = xpathFuncNumberForNode (prightResult->nodes[j], &NaN);
                        if (switchResult) {
                            dTmp   = dLeft;
                            dLeft  = dRight;
                            dRight = dTmp;
                        }
                        if      (step->type == Less)     res = (dLeft < dRight);
                        else if (step->type == LessOrEq) res = (dLeft <= dRight);
                        else if (step->type == Greater)  res = (dLeft >  dRight);
                        else                             res = (dLeft >= dRight);
                        
                        if (res) break;
                    }
                    if (res) break;
                }
                break;
            case BoolResult:
                /* pleftResult is a non-emtpy nodeset, therefor: */
                dLeft = 1.0;
                dRight = xpathFuncNumber (prightResult, &NaN);
                if      (step->type == Less)     res = (dLeft < dRight);
                else if (step->type == LessOrEq) res = (dLeft <= dRight);
                else if (step->type == Greater)  res = (dLeft >  dRight);
                else                             res = (dLeft >= dRight);
                
                if (switchResult) {
                    res = !res;
                }
                break;
            case IntResult:
            case RealResult:
            case StringResult:
                dRight = xpathFuncNumber (prightResult, &NaN);
                for (i=0; i < pleftResult->nr_nodes; i++) {
                    dLeft = xpathFuncNumberForNode (pleftResult->nodes[i], &NaN);
                    if (switchResult) {
                        dTmp   = dLeft;
                        dLeft  = dRight;
                        dRight = dTmp;
                    }
                    if      (step->type == Less)     res = (dLeft < dRight);
                    else if (step->type == LessOrEq) res = (dLeft <= dRight);
                    else if (step->type == Greater)  res = (dLeft >  dRight);
                    else                             res = (dLeft >= dRight);

                    if (res) break;
                }
                break;
            }
        } else {
            if (   leftResult.type == EmptyResult 
                || rightResult.type == EmptyResult) {
                res = 0;
            } else {
                dLeft  = xpathFuncNumber (&leftResult, &NaN);
                dRight = xpathFuncNumber (&rightResult, &NaN);
                if      (step->type == Less)     res = (dLeft < dRight);
                else if (step->type == LessOrEq) res = (dLeft <= dRight);
                else if (step->type == Greater)  res = (dLeft >  dRight);
                else                             res = (dLeft >= dRight);
            }
        }
        rsSetBool (result, res);
        xpathRSFree (&rightResult);
        xpathRSFree (&leftResult);
        return XPATH_OK;

    case SelectRoot:
        rsAddNode(result, ctxNode->ownerDocument->rootNode);
        break;

    case UnaryMinus:
        xpathRSInit (&leftResult);
        rc = xpathEvalSteps (step->child, nodeList, ctxNode, exprContext,
                             position, docOrder,cbs, &leftResult, errMsg);
        CHECK_RC;
        rsSetReal (result , -1 * xpathFuncNumber (&leftResult, &NaN));
        xpathRSFree (&leftResult);
        break;
        
    case EvalSteps:
        rc = xpathEvalSteps (step->child, nodeList, ctxNode, exprContext, position,
                             docOrder,cbs, &leftResult, errMsg);
        CHECK_RC;
        if ((result->type != EmptyResult) && (leftResult.type != result->type)) {
            DBG( fprintf (stderr, "EvalSteps:\nresult:\n");
            rsPrint (result);
            fprintf (stderr, "leftResult:\n");
            rsPrint (&leftResult); )
            *errMsg = (char*)strdup("can not merge different result types!");
            return XPATH_EVAL_ERR;
        }
        switch (leftResult.type) {
            case xNodeSetResult:
                for (i=0; i<leftResult.nr_nodes; i++) {
                    DBG(fprintf(stderr, "EvalSteps: adding %d \n", i);)
                    checkRsAddNode (result, leftResult.nodes[i]);
                }
                break;
            case BoolResult:   rsSetBool(result, leftResult.intvalue);
                               break;
            case IntResult:    rsSetInt(result, leftResult.intvalue);
                               break;
            case RealResult:   rsSetReal(result, leftResult.realvalue);
                               break;
            case StringResult: rsSetString (result, leftResult.string);
                               break;
            default:           break;
        }
        xpathRSFree( &leftResult  );
        break;

    case ExecFunction:
    case ExecIdKey:
        rc = xpathEvalFunction (step, ctxNode, exprContext, position, nodeList,
                                cbs, result, docOrder, errMsg);
        CHECK_RC;
    }
    return XPATH_OK;

} /* xpathEvalStep */





/*----------------------------------------------------------------------------
|   xpathEvalPredicate
|
\---------------------------------------------------------------------------*/
static int xpathEvalPredicate (
    ast                steps,
    domNode           *exprContext,
    xpathResultSet    *result,
    xpathResultSet    *stepResult,
    xpathCBs          *cbs,
    int               *docOrder,
    char              **errMsg
)
{
    xpathResultSet predResult, tmpResult;
    int            i, rc, savedDocOrder, useFastAdd;
    
    if (result->nr_nodes == 0) useFastAdd = 1;
    else useFastAdd = 0;
    savedDocOrder = *docOrder;
    while (steps && steps->type == Pred) {
        xpathRSInit (&tmpResult);
        for (i=0; i<stepResult->nr_nodes; i++) {
            xpathRSInit (&predResult);
            rc = xpathEvalStep( steps->child, stepResult->nodes[i], 
                                exprContext, i, stepResult, cbs, &predResult,
                                docOrder, errMsg);
            CHECK_RC;
            *docOrder = savedDocOrder;
            DBG( fprintf(stderr, "after eval for Predicate: \n"); )
            DBG( rsPrint( &predResult); )
                
            if (predResult.type == RealResult) {
                predResult.type = IntResult;
                predResult.intvalue = xpathRound(predResult.realvalue);
            }
            if (predResult.type == IntResult) {
                if (predResult.intvalue < 0) {
                    predResult.intvalue = stepResult->nr_nodes + predResult.intvalue;
                }
                if (savedDocOrder) {
                    if (predResult.intvalue == (i+1)) {
                        rsAddNode (&tmpResult, stepResult->nodes[i]);
                    }
                } else {
                    if (predResult.intvalue == (stepResult->nr_nodes - i)){
                        rsAddNode (&tmpResult, stepResult->nodes[i]);
                    }
                }
            } else if (xpathFuncBoolean(&predResult)) {
                rsAddNode (&tmpResult, stepResult->nodes[i]);
            }
            xpathRSFree (&predResult);
        }
        DBG( fprintf(stderr, "result after Predicate: \n"); )
        DBG( rsPrint( &tmpResult); )

        xpathRSFree( stepResult );
        *stepResult = tmpResult;
        steps = steps->next;
    }
    
    /* add remaining result set to overall result set */
    for (i=0; i<stepResult->nr_nodes; i++) {
        checkRsAddNode (result, stepResult->nodes[i]);
    }

    return 0;
}


/*----------------------------------------------------------------------------
|   xpathEvalStepAndPredicates
|
\---------------------------------------------------------------------------*/
static int xpathEvalStepAndPredicates (
    ast                steps,
    xpathResultSet    *nodeList,
    domNode           *currentNode,
    domNode           *exprContext,
    int                currentPos,
    int               *docOrder,
    xpathCBs          *cbs,
    xpathResultSet    *result,
    char              **errMsg
)
{
    xpathResultSet  stepResult, tmpResult;
    int             rc, i, j;
    domNode        *parent;
    
    if (steps->next && steps->next->type == Pred) {
        xpathRSInit (&stepResult);
        rc = xpathEvalStep( steps, currentNode, exprContext, currentPos, nodeList,
                            cbs, &stepResult, docOrder, errMsg);
        CHECK_RC;
        /* This special handling of the // abbreviation is one of the
           more trickier things in the xpath recommendation. 3.3 says:
           "The Predicate filters the node-set with respect to the
           child axis" */
        if (steps->type == AxisDescendantOrSelf || steps->type == AxisDescendant) {
            for (i = 0; i < stepResult.nr_nodes; i++) {
                xpathRSInit (&tmpResult);
                if (stepResult.nodes[i]->nodeType == ATTRIBUTE_NODE) {
                    parent = ((domAttrNode *)stepResult.nodes[i])->parentNode;
                } else {
                    parent = stepResult.nodes[i]->parentNode;
                }
                for (j = 0; j < stepResult.nr_nodes; j++) {                    
                    if (stepResult.nodes[j]->nodeType == ATTRIBUTE_NODE) {
                        if (((domAttrNode *)stepResult.nodes[j])->parentNode == parent) {
                            rsAddNode (&tmpResult, stepResult.nodes[j]);
                        }
                    } else {
                        if (stepResult.nodes[j]->parentNode == parent) {
                            rsAddNode (&tmpResult, stepResult.nodes[j]);
                        }
                    }
                }
                rc = xpathEvalPredicate (steps->next, exprContext, result, &tmpResult,
                                         cbs, docOrder, errMsg);
                CHECK_RC;
                xpathRSFree (&tmpResult);
            }
        } else {
            rc = xpathEvalPredicate (steps->next, exprContext, result, &stepResult,
                                     cbs, docOrder, errMsg);
            CHECK_RC;
        }
        xpathRSFree (&stepResult);
        sortByDocOrder (result);
        
    } else {
        /* for steps not followed by a predicate immediately add to
           the final result set */
        rc = xpathEvalStep( steps, currentNode, exprContext, currentPos, nodeList,
                            cbs, result, docOrder, errMsg);
        CHECK_RC;
        DBG( rsPrint( result); )
    }
    return 0;
}


/*----------------------------------------------------------------------------
|   xpathEvalSteps
|
\---------------------------------------------------------------------------*/
int xpathEvalSteps (
    ast                steps,
    xpathResultSet    *nodeList,
    domNode           *currentNode,
    domNode           *exprContext,
    int                currentPos,
    int               *docOrder,
    xpathCBs          *cbs,
    xpathResultSet    *result,
    char              **errMsg
)
{
    int i, rc, first = 1;
    xpathResultSet savedContext;

    savedContext = *nodeList;
    xpathRSInit (result);
    while (steps) {
        if (steps->type == Pred) {
            *errMsg = "Pred step not expected now!";
            return XPATH_EVAL_ERR;
        }
        if (first) {
            rc = xpathEvalStepAndPredicates (steps, nodeList, currentNode, 
                                             exprContext, currentPos, docOrder,
                                             cbs, result, errMsg);
            CHECK_RC;
            first = 0;
        } else {
            DBG( fprintf(stderr, "doing location step nodeList->nr_nodes=%d \n",
                                 nodeList->nr_nodes);
            )
            if (result->type != xNodeSetResult) {
                xpathRSFree (result);
                xpathRSInit (result);
                *nodeList = savedContext;
                return 0;
            }
                    
            *nodeList = *result;
            xpathRSInit (result);
            for (i=0; i<nodeList->nr_nodes; i++) {
                rc = xpathEvalStepAndPredicates (steps, nodeList, 
                                                 nodeList->nodes[i], exprContext, i,
                                                 docOrder, cbs,
                                                 result, errMsg);
                CHECK_RC;
            }
            xpathRSFree (nodeList);
        }
        DBG( fprintf(stderr, "result after location step: \n"); )
        DBG( rsPrint( result); )
  
        steps = steps->next;
        /* skip the already processed Predicate parts */
        while (steps && steps->type == Pred) steps = steps->next;
        *docOrder = 1;
    }
    *nodeList = savedContext;
    return 0;
}


/*----------------------------------------------------------------------------
|   xpathEval
|
\---------------------------------------------------------------------------*/
int xpathEval (
    domNode          * node,
    domNode          * exprContext,
    char             * xpath,
    xpathCBs         * cbs,
    char            ** errMsg,
    xpathResultSet   * result
)
{
    xpathResultSet nodeList;
    int            rc, docOrder = 1;
    ast            t;

    *errMsg = NULL;
    rc = xpathParse(xpath, errMsg, &t, 0);
    CHECK_RC;
    
    xpathRSInit( &nodeList);
    rsAddNode( &nodeList, node);

    rc = xpathEvalSteps( t, &nodeList, node, exprContext, 1, &docOrder, cbs, result,
                         errMsg);
    freeAst(t);
    xpathRSFree( &nodeList );
    CHECK_RC;

    DBG(rsPrint( result );)
    return 0;

} /* xpathEval */




/*----------------------------------------------------------------------------
|   xpathMatchesSlow
|
\---------------------------------------------------------------------------*/
int xpathMatchesSlow (
    ast                 steps,
    domNode           * exprContext,
    domNode           * nodeToMatch,
    xpathCBs          * cbs,
    char             ** errMsg
)
{
    xpathResultSet  rs, nodeList;
    domNode        *currentNode;
    int             i, found, rc, docOrder = 1;


    currentNode = nodeToMatch;
    while (1) {
        xpathRSInit( &rs );
        xpathRSInit( &nodeList );
        rsAddNode( &nodeList, currentNode );

        rc = xpathEvalSteps( steps, &nodeList, currentNode, exprContext, 1, &docOrder,
                             cbs, &rs, errMsg);

        if (rc != XPATH_OK) {
            TRACE1("xpathEvalSteps had errors '%s' \n", *errMsg);
            xpathRSFree( &rs );
            xpathRSFree( &nodeList );
            return rc;
        }
        DBG(rsPrint(&rs);)

        if (rs.type == xNodeSetResult) {
            found = 0;
            for (i=0; i<rs.nr_nodes; i++) {
                if (rs.nodes[i] == nodeToMatch) {
                    found = 1;
                    break;
                }
            }
            if (found) {
                xpathRSFree( &rs );
                xpathRSFree( &nodeList );
                return 1;
            }
        }
        xpathRSFree( &rs );
        xpathRSFree( &nodeList );
        if (currentNode == currentNode->ownerDocument->rootNode) return 0;
        if (currentNode->parentNode == NULL) {
            currentNode = currentNode->ownerDocument->rootNode;
        } else {
            currentNode = currentNode->parentNode;
        }
    }
    return 0;
}


static void printNodePath ( domNode *node )
{
    if (node->parentNode) {
        printNodePath(node->parentNode);
    }
    fprintf(stderr, "/");
    if (node->nodeType == ELEMENT_NODE) {
        fprintf(stderr, node->nodeName);
    } else 
    if (node->nodeType == TEXT_NODE) {
        fprintf(stderr, "'%s'", ((domTextNode*)node)->nodeValue);    
    }
}



/*----------------------------------------------------------------------------
|   xpathMatches
|
\---------------------------------------------------------------------------*/
int xpathMatches (
    ast                 steps,
    domNode           * exprContext,
    domNode           * nodeToMatch,
    xpathCBs          * cbs,
    char             ** errMsg
)
{
    xpathResultSet  stepResult, nodeList, newNodeList;
    ast             childSteps;
    int             rc, i, j, currentPos = 0, nodeMatches, docOrder = 1;
    int             useFastAdd;
    char           *localName = NULL, *nodeUri;
    domAttrNode    *attr;
    domNode        *child;
    domNS          *contextNS;

    DBG(printAst(3,steps));
    xpathRSInit (&nodeList);
    while (steps) {
        TRACE1("xpathMatches type=%d \n", steps->type);
        switch (steps->type) {        

            case AxisAttribute:
                if (steps->child->type != IsAttr && steps->child->type != IsNSAttr) {
                    if (steps->child->type == IsElement) {
                        steps->child->type = IsAttr;
                    } else {
                        fprintf(stderr, "strange: AxisAttribute with no IsAttr below!\n");
                        xpathRSFree (&nodeList); return 0;
                    }
                }
                attr = NULL;
                if (nodeToMatch->nodeType == ATTRIBUTE_NODE) {
                    rc = xpathMatches (steps->child, exprContext, nodeToMatch, cbs,
                                       errMsg);
                    DBG(fprintf(stderr, "rc=%d attribute match\n", rc); )
                    if (rc != 1) { xpathRSFree (&nodeList); return 0; }
                    attr = (domAttrNode*) nodeToMatch;
                } else { xpathRSFree (&nodeList); return 0; }
                if (attr == NULL) { xpathRSFree (&nodeList); return 0; }
                break;
                                
            case AxisChild:
                if (steps->child->type == IsElement) {
                    if (nodeToMatch->nodeType != ELEMENT_NODE) {
                        xpathRSFree (&nodeList); return 0;
                    }
                    if (nodeToMatch == nodeToMatch->ownerDocument->rootNode) {
                        xpathRSFree (&nodeList); return 0;
                    }
                    if ((steps->child->strvalue[0] != '*') ||
                        (steps->child->strvalue[1] != '\0')) 
                    {
                        if (nodeToMatch->namespace) return 0;
                        if (strcmp(nodeToMatch->nodeName, steps->child->strvalue)!=0) {
                            xpathRSFree (&nodeList); return 0;
                        }
                    }
                    break;
                }
                if (steps->child->type == IsFQElement) {
                    if (nodeToMatch->nodeType != ELEMENT_NODE) {
                        xpathRSFree (&nodeList); return 0;
                    }
                    contextNS = domLookupPrefix (exprContext, steps->child->strvalue);
                    if (!contextNS) { xpathRSFree (&nodeList); return 0; }
                    nodeUri = domNamespaceURI (nodeToMatch);
                    if (!nodeUri) { xpathRSFree (&nodeList); return 0; }
                    if (strcmp (contextNS->uri, nodeUri) != 0) {
                        xpathRSFree (&nodeList); return 0;
                    }
                    localName = domGetLocalName (nodeToMatch->nodeName);
                    if (!localName) { xpathRSFree (&nodeList); return 0; }
                    if (strcmp (steps->child->child->strvalue, localName)!=0) {
                        xpathRSFree (&nodeList); return 0;
                    }
                    break;
                }
                if (steps->child->type == IsNSElement) {
                    contextNS = domLookupPrefix (exprContext, steps->child->strvalue);
                    if (!contextNS) { xpathRSFree (&nodeList); return 0; }
                    nodeUri = domNamespaceURI (nodeToMatch);
                    if (!nodeUri) { xpathRSFree (&nodeList); return 0; }
                    if (strcmp (contextNS->uri, nodeUri)!=0) {
                        xpathRSFree (&nodeList); return 0;
                    }
                    break;
                }
                fprintf(stderr, "strange: AxisChild with no IsElement, IsFQElement or IsNSElement below!\n");
                return 0;
                    
            case IsElement:
                if (nodeToMatch->nodeType != ELEMENT_NODE) {
                    xpathRSFree (&nodeList); return 0;
                }
                if (nodeToMatch == nodeToMatch->ownerDocument->rootNode) {
                    xpathRSFree (&nodeList); return 0;
                }
                if ((steps->strvalue[0] != '*') ||
                    (steps->strvalue[1] != '\0')) 
                {
                    if (nodeToMatch->namespace) return 0;
                    if (strcmp(nodeToMatch->nodeName, steps->strvalue)!=0) {
                        xpathRSFree (&nodeList); return 0;
                    }
                }
                break;

            case IsFQElement: 
                if (nodeToMatch->nodeType != ELEMENT_NODE) {
                    xpathRSFree (&nodeList); return 0;
                }
                contextNS = domLookupPrefix (exprContext, steps->strvalue);
                if (!contextNS) { xpathRSFree (&nodeList); return 0; }
                nodeUri = domNamespaceURI (nodeToMatch);
                if (!nodeUri) { xpathRSFree (&nodeList); return 0; }
                if (strcmp (contextNS->uri, nodeUri) != 0) {
                    xpathRSFree (&nodeList); return 0;
                }
                localName = domGetLocalName (nodeToMatch->nodeName);
                if (!localName) { xpathRSFree (&nodeList); return 0; }
                if (strcmp (steps->child->strvalue, localName)!=0)  {
                    xpathRSFree (&nodeList); return 0;
                }
                break;
                
            case IsNSElement:
                contextNS = domLookupPrefix (exprContext, steps->strvalue);
                if (!contextNS) { xpathRSFree (&nodeList); return 0; }
                nodeUri = domNamespaceURI (nodeToMatch);
                if (!nodeUri) { xpathRSFree (&nodeList); return 0; }
                if (strcmp (contextNS->uri, nodeUri)!=0) {
                    xpathRSFree (&nodeList); return 0;
                }
                break;
                
            case IsAttr:
                if ((nodeToMatch->nodeType != ATTRIBUTE_NODE)
                    || (nodeToMatch->nodeFlags & IS_NS_NODE)) {
                    xpathRSFree (&nodeList); return 0;
                }
                if (!((steps->strvalue[0] == '*') && (steps->strvalue[1] == '\0')))  {
                    if (strcmp( ((domAttrNode*)nodeToMatch)->nodeName, steps->strvalue)!=0) {
                        xpathRSFree (&nodeList); return 0;
                    }
                }
                break;
                
            case IsNSAttr:
                if ((nodeToMatch->nodeType != ATTRIBUTE_NODE)
                    || (nodeToMatch->nodeFlags & IS_NS_NODE)) {
                    xpathRSFree (&nodeList); return 0;
                }
                contextNS = domLookupPrefix (exprContext, steps->strvalue);
                if (!contextNS) { xpathRSFree (&nodeList); return 0; }
                nodeUri = domNamespaceURI (nodeToMatch);
                if (!nodeUri) { xpathRSFree (&nodeList); return 0; }
                if (strcmp (contextNS->uri, nodeUri) != 0) {
                    xpathRSFree (&nodeList); return 0;
                }
                if (strcmp (steps->child->strvalue, "*")==0) break;
                localName = domGetLocalName (((domAttrNode *)nodeToMatch)->nodeName);
                if (!localName) { xpathRSFree (&nodeList); return 0; }
                if (strcmp (steps->child->strvalue, localName)!=0)  {
                    xpathRSFree (&nodeList); return 0;
                }
                break;
                
            case IsNode:
                DBG(fprintf(stderr, "IsNode: nodeTpye=%d \n", nodeToMatch->nodeType);)
                if (nodeToMatch->nodeType == ATTRIBUTE_NODE) {
                    xpathRSFree (&nodeList); return 0;
                } 
                if ((nodeToMatch->nodeType == ELEMENT_NODE) &&
                    (nodeToMatch->ownerDocument->rootNode == nodeToMatch)) {
                    xpathRSFree (&nodeList); return 0;
                } 
                break;
                
            case IsText:
                if (nodeToMatch->nodeType != TEXT_NODE) {
                    xpathRSFree (&nodeList); return 0;
                }
                break;
                
            case IsPI:
                if (nodeToMatch->nodeType != PROCESSING_INSTRUCTION_NODE) {
                    xpathRSFree (&nodeList); return 0;
                }
                break;
                
            case IsSpecificPI:
                if (nodeToMatch->nodeType != PROCESSING_INSTRUCTION_NODE) {
                    xpathRSFree (&nodeList); return 0;
                }
                if (strncmp(((domProcessingInstructionNode*)nodeToMatch)->targetValue, steps->strvalue, ((domProcessingInstructionNode*)nodeToMatch)->targetLength) != 0) {
                    xpathRSFree (&nodeList); return 0;
                }
                break;
                
            case IsComment:
                if (nodeToMatch->nodeType != COMMENT_NODE) {
                    xpathRSFree (&nodeList); return 0;
                }
                break;
                
            case IsRoot:
                if (nodeToMatch != nodeToMatch->ownerDocument->rootNode) {
                    xpathRSFree (&nodeList); return 0;
                }
                break;
                
            case ToParent:
                if (nodeToMatch->nodeType == ATTRIBUTE_NODE) {
                    nodeToMatch = ((domAttrNode *)nodeToMatch)->parentNode;
                    break;
                }
                if (nodeToMatch == nodeToMatch->ownerDocument->rootNode) {
                    xpathRSFree (&nodeList); return 0;
                }
                if (nodeToMatch->parentNode) {
                    nodeToMatch = nodeToMatch->parentNode;
                } else {
                    nodeToMatch = nodeToMatch->ownerDocument->rootNode;
                }
                break;

            case ToAncestors:
                if (steps->next == NULL) { xpathRSFree (&nodeList); return 1;}
                while (1) {
/*                      if (nodeToMatch->nodeType != ELEMENT_NODE) return 0; */
                    if (nodeToMatch->nodeType == ATTRIBUTE_NODE) {
                        nodeToMatch = ((domAttrNode *)nodeToMatch)->parentNode;
                    } else {
                        if (nodeToMatch == nodeToMatch->ownerDocument->rootNode) {
                            xpathRSFree (&nodeList); return 0;
                        }
                        if (nodeToMatch->parentNode) {
                            nodeToMatch = nodeToMatch->parentNode;
                        } else {
                            nodeToMatch = nodeToMatch->ownerDocument->rootNode;
                        }
                    }
                    if (steps->next == NULL) {
                        xpathRSFree (&nodeList); return 0;
                    }
                    rc = xpathMatches (steps->next, exprContext, nodeToMatch,
                                       cbs, errMsg);
                    if (rc == 1) break;
                }
                break;
                
            case FillWithCurrentNode:
                rsAddNode( &nodeList, nodeToMatch);
                currentPos = 0;
                DBG(
                  fprintf(stderr, "FillWithCurrentNode:\n");
                  rsPrint(&nodeList);
                )                
                break;
                
            case FillNodeList:
                if (nodeToMatch->nodeType == ATTRIBUTE_NODE) {
                    child = (domNode*)  ((domAttrNode*)nodeToMatch)->parentNode->firstAttr;
                    DBG(fprintf(stderr, "FillNodeList all attribute\n");)
                } else {
                    if (nodeToMatch->ownerDocument->rootNode == nodeToMatch) {
                        xpathRSFree (&nodeList); return 0;
                    }
                    DBG(if (nodeToMatch->parentNode) {
                            fprintf(stderr, "FillNodeList on %s/%s...\n", nodeToMatch->parentNode->nodeName,nodeToMatch->nodeName);
                        } else {
                            fprintf(stderr, "FillNodeList on (null)/%s...\n", nodeToMatch->nodeName);
                        })
                    if (nodeToMatch->parentNode == NULL) {
                        child = nodeToMatch->ownerDocument->rootNode->firstChild;
                    } else {
                        child = nodeToMatch->parentNode->firstChild;
                    }
                }
                currentPos = -1;
                i = 0;
                if (nodeList.nr_nodes == 0) useFastAdd = 1;
                else useFastAdd = 0;
                while (child) {
                    rc = xpathMatches (steps->child, exprContext, child, cbs, errMsg);
                    if (rc == 1) {
                        checkRsAddNode( &nodeList, child);
                        if (child == nodeToMatch) currentPos = i;
                        i++;
                    }
                    if (child->nodeType == ATTRIBUTE_NODE) {
                        child = (domNode*) ((domAttrNode*)child)->nextSibling;
                    } else {
                        child = child->nextSibling;
                    }
                }
                DBG(
                  rsPrint(&nodeList);
                  fprintf(stderr, "currentPos = %d \n", currentPos);
                )
                break;
                
            case Pred:
                xpathRSInit (&stepResult);
                DBG(fprintf(stderr, "Befor Pred inner EvalStep. currentPos = %d\n", currentPos);)
                rc = xpathEvalStep (steps->child, nodeToMatch, exprContext, 
                                    currentPos, &nodeList, cbs, &stepResult,
                                    &docOrder, errMsg);
                CHECK_RC;
                DBG( 
                  fprintf(stderr, "Pred inner EvalStep returned\n");
                  rsPrint(&stepResult);
                )
                nodeMatches = 0;

                if (stepResult.type == RealResult) {
                    stepResult.type = IntResult;
                    stepResult.intvalue = xpathRound(stepResult.realvalue);
                }
                if (stepResult.type == IntResult) {
                    if (stepResult.intvalue < 0) {
                        stepResult.intvalue = nodeList.nr_nodes + stepResult.intvalue;
                    }
                    if ((stepResult.intvalue > 0 ) &&
                        (stepResult.intvalue <= nodeList.nr_nodes) &&
                        (stepResult.intvalue == (currentPos+1)) ) {
                         nodeMatches = 1;
                    }
                } else if (xpathFuncBoolean(&stepResult)) {
                    nodeMatches = 1;
                }
                xpathRSFree (&stepResult);
                
                /* if nodeMatches == false we don't have to continue to filter */
                if (!nodeMatches) { 
                    xpathRSFree (&nodeList); return 0;
                }

                /* if the nr_nodes of nodeList is > 1 (ie. we filter a
                   FillNodeList step, not only a FillWithCurrentNode
                   step, build the resulting nodeList context after
                   this predicate */

                if (nodeList.nr_nodes > 1) {
                    xpathRSInit (&newNodeList);
                    currentPos = -1;
                    j = 0;
                    for (i = 0; i < nodeList.nr_nodes; i++) {
                        xpathRSInit (&stepResult);
                        docOrder = 1;
                        rc = xpathEvalStep (steps->child, nodeList.nodes[i],
                                            exprContext, i, &nodeList, cbs,
                                            &stepResult, &docOrder, errMsg);
                        if (rc) {
                            xpathRSFree (&stepResult);
                            xpathRSFree (&nodeList);
                            return rc;
                        }
                        
                        nodeMatches = 0;
                        
                        if (stepResult.type == RealResult) {
                            stepResult.type = IntResult;
                            stepResult.intvalue = xpathRound(stepResult.realvalue);
                        }
                        if (stepResult.type == IntResult) {
                            if (stepResult.intvalue < 0) {
                                stepResult.intvalue = nodeList.nr_nodes + stepResult.intvalue;
                            }
                            if ((stepResult.intvalue > 0 ) &&
                                (stepResult.intvalue <= nodeList.nr_nodes) &&
                                (stepResult.intvalue == (currentPos+1)) ) {
                                nodeMatches = 1;
                            }
                        } else if (xpathFuncBoolean(&stepResult)) {
                            nodeMatches = 1;
                        }
                        if (nodeMatches) {
                            rsAddNode (&newNodeList, nodeList.nodes[i]);
                            if (nodeList.nodes[i] == nodeToMatch) {
                                currentPos = j;
                            }
                            j++;
                        }
                        xpathRSFree (&stepResult);
                    }
                    xpathRSFree (&nodeList);
                    nodeList = newNodeList;
                }
                break;
                
            case CombinePath:
                childSteps = steps->child;
                while (childSteps) {
                    rc = xpathMatches (childSteps->child, exprContext, nodeToMatch,
                                       cbs, errMsg);
                    if (rc == 1) break;
                    childSteps = childSteps->next;
                }
                if (childSteps == NULL) {
                    xpathRSFree (&nodeList); return 0;
                }
                break;
                
            case ExecIdKey:
                xpathRSInit (&stepResult);
                rc = xpathEvalStep (steps, nodeToMatch, exprContext, currentPos,
                                    &nodeList, cbs, &stepResult, &docOrder, errMsg);
                CHECK_RC;
                if (stepResult.type != xNodeSetResult) {
                    xpathRSFree (&stepResult);
                    xpathRSFree (&nodeList); return 0;
                }
                nodeMatches = 0;
                for (i = 0; i < stepResult.nr_nodes; i++) {
                    if (nodeToMatch == stepResult.nodes[i]) {
                        nodeMatches = 1;
                        break;
                    }
                }
                xpathRSFree (&stepResult);
                if (!nodeMatches) { xpathRSFree (&nodeList); return 0; }
                break;
                
            default: 
                fprintf(stderr, "wrong type %d for xpathMatches \n", steps->type);
                fprintf(stderr, "AST:\n");
                printAst (0, steps);                             
                xpathRSFree (&nodeList); return 0;
                break;
        }
        steps = steps->next;
    }
    xpathRSFree (&nodeList);
    return 1;
}

/*----------------------------------------------------------------------------
|   xpathMatches2
|
\---------------------------------------------------------------------------*/
int xpathMatches2 (
    ast                 steps,
    domNode           * nodeToMatch,
    xpathCBs          * cbs,
    char             ** errMsg
)
{
    int rc;

    fprintf(stderr, "xpathMatches: ");
    printNodePath(nodeToMatch); 
    rc = xpathMatches2(steps,nodeToMatch,cbs,errMsg);
    fprintf(stderr, " = %d \n", rc);
    return rc;
}


/*----------------------------------------------------------------------------
|   xpathGetPrioOld
|
\---------------------------------------------------------------------------*/
double xpathGetPrioOld (
    ast steps
)
{
    double prio, max;

    if (!steps) return 0.0;

    DBG(printAst(0, steps);)

    if ((steps->type == AxisChild) && (steps->next == NULL) ) {
        if (steps->child->type == IsElement) {
            if (strcmp(steps->child->strvalue, "*")==0) {
                return -0.5;
            } else {
                return 0.0;
            }
        }
        if ( ( (steps->child->type == IsNode)
             ||(steps->child->type == IsText)
             ||(steps->child->type == IsPI)
             )
             && (steps->next == NULL)
        ) {
            return -0.5;
        }
    }
    if ((steps->type == AxisAttribute) && (steps->next == NULL) ) {
        if (steps->child->type == IsAttr) {
            if (strcmp(steps->child->strvalue, "*")==0) {
                return -0.25;
            } else {
                return 0.0;
            }
        } else
        if (steps->child->type == IsNSAttr) {
            if (strcmp(steps->child->child->strvalue, "*")==0) {
                return -0.25;
            } else {
                return 0.0;
            }
        }
    }

    if (steps->type == CombineSets) {
        max = -0.5;
        steps = steps->child;
        while (steps) {
            prio = xpathGetPrio(steps);
            if (prio > max) max = prio;
            steps = steps->next;
        }
        return max;
    }

    return 0.5;

} /* xpathGetPrioOld */

/*----------------------------------------------------------------------------
|   xpathGetPrio
|
\---------------------------------------------------------------------------*/
double xpathGetPrio (
    ast steps
)
{
    double prio, max;

    if (!steps) return 0.0;

    DBG(printAst(0, steps);)

    if (steps->next == NULL) {
        if (steps->type == IsElement) {
            if (strcmp(steps->strvalue, "*")==0) {
                return -0.5;
            } else {
                return 0.0;
            }
        } else 
        if (steps->type == IsFQElement) {
            return 0.0;
        } else 
        if (steps->type == IsNSElement) {
            return -0.25;
        } else 
        if (steps->type == IsAttr) {
            if (strcmp(steps->strvalue, "*")==0) {
                return -0.5;
            } else {
                return 0.0;
            }
        } else
        if (steps->type == IsNSAttr) {
            if (strcmp(steps->child->strvalue, "*")==0) {
                return -0.25;
            } else {
                return 0.0;
            }
        } else 
        if ( (steps->type == IsNode)
           ||(steps->type == IsText)
           ||(steps->type == IsPI)
           ||(steps->type == IsComment)
           ||(steps->type == IsSpecificPI)
        ) {
            return -0.5;
        }
    }
    if (steps->type == AxisChild 
        || steps->type == AxisAttribute
        || steps->type == EvalSteps) {
        return (xpathGetPrio (steps->child));
    }
    if (steps->type == CombinePath) {
        max = -0.5;
        steps = steps->child;
        while (steps) {
            prio = xpathGetPrio(steps->child);
            if (prio > max) max = prio;
            steps = steps->next;
        }
        return max;
    }
    return 0.5;

} /* xpathGetPrio */


/*----------------------------------------------------------------------------
|   nodeToXPath  -  returns a XPath addressing exactly the given node
|
\---------------------------------------------------------------------------*/
static void nodeToXPath (
    domNode  * node,
    char    ** xpath,
    int      * xpathLen,
    int      * xpathAllocated
)
{
    domNode *parent, *child;
    char    step[200], *nTest;
    int     sameNodes, nodeIndex, len;
    
        
    parent = node->parentNode;
    if (parent == NULL) {

        sprintf(step, "/%s", node->nodeName);

    } else {
            
        nodeToXPath (parent, xpath, xpathLen, xpathAllocated);

        step[0] = '\0';
        switch (node->nodeType) {
        
            case ELEMENT_NODE:
                nodeIndex = 0;
                sameNodes = 0;
                child = parent->firstChild;
                while (child) {
                    if (strcmp(child->nodeName, node->nodeName)==0) {   
                        sameNodes++;
                        if (node == child) nodeIndex = sameNodes;
                        if ((nodeIndex != 0) && (sameNodes > 2)) break;
                    }    
                    child = child->nextSibling;
                }
                if (sameNodes == 1) {
                    sprintf(step, "/%s", node->nodeName);
                } else {
                    sprintf(step, "/%s[%d]", node->nodeName, nodeIndex);
                }
                break;
                
            case TEXT_NODE:
            case COMMENT_NODE:
            case PROCESSING_INSTRUCTION_NODE:
                nodeIndex = 0;
                sameNodes = 0;
                child = parent->firstChild;
                while (child) {
                    if (child->nodeType == node->nodeType) {   
                        sameNodes++;
                        if (node == child) nodeIndex = sameNodes;
                        if ((nodeIndex != 0) && (sameNodes > 2)) break;
                    }    
                    child = child->nextSibling;
                }
                switch (node->nodeType) {
                    case TEXT_NODE:                   nTest = "text()";  break;
                    case COMMENT_NODE:                nTest = "comment"; break;
                    case PROCESSING_INSTRUCTION_NODE: nTest = "processing-instruction"; break;
                    default:                          nTest = "unknownNodeType()";
                }
                if (sameNodes == 1) {
                    sprintf(step, "/child::%s", nTest);
                } else {
                    sprintf(step, "/child::%s[%d]", nTest, nodeIndex);
                }
                break;
                
            default:
                break;
        }
    }
    len = strlen(step);
    if ( (len + *xpathLen) > *xpathAllocated ) {
        *xpathAllocated = *xpathAllocated * 2;
        *xpath = realloc(*xpath, *xpathAllocated + 1);
    }
    strcpy( *xpath + *xpathLen, step);
    *xpathLen += len;
    
} /* nodeToXPath */


/*----------------------------------------------------------------------------
|   xpathNodeToXPath
|
\---------------------------------------------------------------------------*/
char * xpathNodeToXPath (
    domNode *node
)
{
    char  * xpath;
    int     xpathLen, xpathAllocated;
    
    
    xpathAllocated = 100;    
    xpathLen       = 0;
    xpath          = malloc(xpathAllocated + 1);

    nodeToXPath (node, &xpath, &xpathLen, &xpathAllocated);
    
    return xpath;

} /* xpathNodeToXPath */

