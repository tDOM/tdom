
/*----------------------------------------------------------------------------
|   Copyright (c) 1999 Jochen Loewer (loewerj@hotmail.com)
|-----------------------------------------------------------------------------
|
|   $Header$
|
| 
|   A (partial) XPath implementation (lexer/parser/evaluator) for tDOM, 
|   the DOM implementation for Tcl.
|   Based on the August 13 working draft of the W3C 
|   (http://www.w3.org/1999/08/WD-xpath-19990813.html)
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
|
|
|   $Log$
|   Revision 1.8  2002/06/02 06:36:24  zoran
|   Added thread safety with capability of sharing DOM trees between
|   threads and ability to read/write-lock DOM documents
|
|   Revision 1.7  2002/05/11 16:57:29  rolf
|   Made variable/parameter namespace aware.
|
|   Revision 1.6  2002/05/10 20:29:31  rolf
|   Made key names namespace aware.
|
|   Revision 1.5  2002/05/04 01:30:10  rolf
|   Inlined xpathRSInit (for speed).
|
|   Revision 1.4  2002/05/01 00:55:41  rolf
|   Introduced AxisDescendantLit and AxisDescendantOrSelfLit (a bit of a,
|   aehm, let't call it workaround). With that, wie can distinguish
|   between // and descencant or descendant-or-self. And reinsert the
|   handling of the predicate filter with respect to the child axes for //
|   in xpathEvalStepAndPredicates().
|
|   Revision 1.3  2002/04/28 22:20:30  rolf
|   Added full qualified and namespace wildcard attribute queries. Fixed
|   a Bug with following axis expression starting from an attribute. Added
|   handling of UnaryExpr to xpathEvalStep(). Improved xpathGetPrio().
|
|   Revision 1.2  2002/03/21 01:47:22  rolf
|   Collected the various nodeSet Result types into "nodeSetResult" (there
|   still exists a seperate emptyResult type). Reworked
|   xpathEvalStep. Fixed memory leak in xpathMatches, added
|   rsAddNodeFast(), if it's known for sure, that the node to add isn't
|   already in the nodeSet.
|
|   Revision 1.1.1.1  2002/02/22 01:05:35  rolf
|   tDOM0.7test with Jochens first set of patches
|
|
|
|   written by Jochen Loewer
|   July, 1999
|
\---------------------------------------------------------------------------*/


#ifndef __DOMXPATH_H__
#define __DOMXPATH_H__

#include <dom.h>


/*----------------------------------------------------------------------------
|   Macros
|
\---------------------------------------------------------------------------*/
#define XPATH_OK             0
#define XPATH_LEX_ERR       -1
#define XPATH_SYNTAX_ERR    -2
#define XPATH_EVAL_ERR      -3
#define XPATH_VAR_NOT_FOUND -4

#if defined (__sun__) 
#include <ieeefp.h>
#define isinf(d) ((fpclass(d)==FP_PINF)?1:((fpclass(d)==FP_NINF)?-1:0))
#endif

/*----------------------------------------------------------------------------
|   Types for abstract syntax trees
|
\---------------------------------------------------------------------------*/
typedef enum {
    Int, Real, Str, Mult, Div, Mod, UnaryMinus, IsNSElement,
    IsNode, IsComment, IsText, IsPI, IsSpecificPI, IsElement,
    IsFQElement, GetVar, GetFQVar, Literal, ExecFunction, Pred, EvalSteps,
    SelectRoot, CombineSets, Add, Substract, Less, LessOrEq,
    Greater, GreaterOrEq, Equal,  NotEqual, And, Or, IsNSAttr, IsAttr,
    AxisAncestor, AxisAncestorOrSelf, AxisAttribute, AxisChild,
    AxisDescendant, AxisDescendantOrSelf, AxisFollowing,
    AxisFollowingSibling, AxisNamespace, AxisParent,
    AxisPreceding, AxisPrecedingSibling, AxisSelf,
    GetContextNode, GetParentNode, AxisDescendantOrSelfLit,
    AxisDescendantLit,
        
    CombinePath, IsRoot, ToParent, ToAncestors, FillNodeList,
    FillWithCurrentNode,
    ExecIdKey
    
} astType;


typedef struct astElem {
    astType         type;
    struct astElem *child;
    struct astElem *next;
    char           *strvalue;
    int             intvalue;
    double          realvalue;
} astElem;

typedef astElem *ast;


/*----------------------------------------------------------------------------
|   Types for XPath result set
|
\---------------------------------------------------------------------------*/
typedef enum { 
    EmptyResult, BoolResult, IntResult, RealResult, StringResult, 
    xNodeSetResult
} xpathResultType;


typedef struct xpathResultSet {

    xpathResultType type;
    char           *string;
    int             string_len;
    int             intvalue;
    double          realvalue;          
    domNode       **nodes;
    int             nr_nodes;
    int             allocated;

} xpathResultSet;

typedef xpathResultSet *xpathResultSets;

typedef int (*xpathFuncCallback) 
                (void *clientData, char *functionName, 
                 domNode *ctxNode, int position, xpathResultSet *nodeList,
                 domNode *exprContext, int argc, xpathResultSets *args,
                 xpathResultSet *result, char  **errMsg);
                              
typedef int (*xpathVarCallback) 
                (void *clientData, char *variableName, char *varURI,
                 xpathResultSet *result, char  **errMsg);
                              
typedef struct xpathCBs {               /* all xpath callbacks + clientData */

    xpathVarCallback    varCB;
    void              * varClientData;
    xpathFuncCallback   funcCB;
    void              * funcClientData;

} xpathCBs;


/*----------------------------------------------------------------------------
|   Prototypes
|
\---------------------------------------------------------------------------*/
int    xpathParse   (char *xpath, char **errMsg, ast *t, int asPattern);
void   xpathFreeAst (ast t);
double xpathGetPrio (ast t);
int    xpathEval    (domNode *node, domNode *exprContext, char *xpath, xpathCBs *cbs,
                     char **errMsg, xpathResultSet *rs
                     );
int    xpathMatches (ast steps, domNode * exprContext, domNode *nodeToMatch,
                     xpathCBs *cbs, char **errMsg 
                    );
                     
int xpathEvalSteps (ast steps, xpathResultSet *nodeList,
                    domNode *currentNode, domNode *exprContext, int currentPos,
                    int *docOrder,
                    xpathCBs *cbs,
                    xpathResultSet *result, char **errMsg);
                    
#define xpathRSInit(x) (x)->type = EmptyResult; (x)->nr_nodes = 0;
/*  void   xpathRSInit (xpathResultSet *rs ); */
void   xpathRSFree (xpathResultSet *rs );

int    xpathFuncBoolean  (xpathResultSet *rs);
double xpathFuncNumber   (xpathResultSet *rs, int *NaN);
char * xpathFuncString   (xpathResultSet *rs); 
char * xpathFuncStringForNode (domNode *node);
int    xpathRound        (double r);

char * xpathGetTextValue (domNode *node, int *strLen);

char * xpathNodeToXPath  (domNode *node);
    
void rsSetBool      ( xpathResultSet *rs, int          i    );
void rsSetInt       ( xpathResultSet *rs, int          i    );
void rsSetReal      ( xpathResultSet *rs, double       d    );
void rsSetString    ( xpathResultSet *rs, char        *s    );
void rsAddNode      ( xpathResultSet *rs, domNode     *node );
void rsAddNodeFast  ( xpathResultSet *rs, domNode     *node );
void rsAddAttrNode  ( xpathResultSet *rs, domAttrNode *node );
void rsAddAttrValue ( xpathResultSet *rs, domAttrNode *node );

/*??*/ void rsPrint ( xpathResultSet *rs );
void rsCopy         ( xpathResultSet *to, xpathResultSet *from );

#endif

