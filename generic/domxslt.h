/*----------------------------------------------------------------------------
|   Copyright (c) 2000 Jochen Loewer (loewerj@hotmail.com)
|-----------------------------------------------------------------------------
|
|   $Header$
|
| 
|   A (partial) XSLT implementation for tDOM, according to the W3C 
|   recommendation (16 Nov 1999, 
|   http://www.w3.org/TR/1999/REC-xslt-19991116).
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
|   Revision 1.1.1.1  2002/02/22 01:05:35  rolf
|   tDOM0.7test with Jochens first set of patches
|
|   Revision 1.1  2002/02/04 08:08:19  jolo
|   Initial revision
|
|
|
|   written by Jochen Loewer
|   June, 2000
|
\---------------------------------------------------------------------------*/

#ifndef __DOMXSLT_H__
#define __DOMXSLT_H__

#include <dom.h>
#include <domxpath.h>

/*----------------------------------------------------------------------------
|   Prototypes
|
\---------------------------------------------------------------------------*/
int xsltProcess (domDocument       * xsltDoc,
                 domNode           * xmlNode,
                 xpathFuncCallback   funcCB,
                 void              * clientData,
                 char             ** errMsg,
                 domDocument      ** resultDoc   
                );

void sortByDocOrder (xpathResultSet *rs);

#endif

