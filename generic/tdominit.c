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
#include <dom.h>
#include <tcldom.h>


/*
 *----------------------------------------------------------------------------
 *
 * Tdom_Init --
 *
 *	Initialization routine for loadable module
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Defines "expat"/"dom" commands in the interpreter.
 *
 *----------------------------------------------------------------------------
 */

int
Tdom_Init (interp)
     Tcl_Interp *interp; /* Interpreter to initialise. */
{

#ifdef USE_TCL_STUBS
    Tcl_InitStubs(interp, "8.3", 0);
#endif
    domModuleInitialize();

#ifndef TDOM_NO_UNKNOWN_CMD
    Tcl_Eval (interp,"rename unknown unknown_tdom");   
    Tcl_CreateObjCommand (interp, "unknown", tcldom_unknownCmd,  NULL, NULL );
#endif

    Tcl_CreateObjCommand (interp, "dom",     tcldom_domCmd,      NULL, NULL );
    Tcl_CreateObjCommand (interp, "domNode", tcldom_NodeObjCmd,  NULL, NULL );
    Tcl_CreateObjCommand (interp, "tdom",    TclTdomObjCmd,      NULL, NULL );

#ifndef TDOM_NO_EXPAT    
    Tcl_CreateObjCommand (interp, "expat",       TclExpatObjCmd, NULL, NULL );
    Tcl_CreateObjCommand (interp, "xml::parser", TclExpatObjCmd, NULL, NULL );
    Tcl_PkgProvide(interp, "expat", "2.0");      
#endif
    
    Tcl_PkgProvide (interp, "stackedtdom", "0.1");    
    Tcl_PkgProvide (interp, "tdom",  STR_TDOM_VERSION(TDOM_VERSION));

    return TCL_OK;
}

int
Tdom_SafeInit (interp)
     Tcl_Interp *interp; /* Interpreter to initialise. */
{
    /* nothing special for safe interpreters -> just call Tdom_Init */
    return Tdom_Init (interp);
}
