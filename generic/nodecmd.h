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
|   Revision 1.1.1.1  2002/02/22 01:05:35  rolf
|   tDOM0.7test with Jochens first set of patches
|
|
|
|   written by Zoran Vasiljevic
|   July 12, 2000
|
\---------------------------------------------------------------------------*/


int nodecmd_createNodeCmd (ClientData      dummy,
                           Tcl_Interp    * interp,
                           int             objc,
                           Tcl_Obj *CONST  objv[]);

int nodecmd_appendFromTclScript (Tcl_Interp *interp, 
                                 domNode    *node,
                                 Tcl_Obj    *cmdObj);


