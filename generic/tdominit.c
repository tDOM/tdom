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
|   Revision 1.6  2002/06/20 13:14:01  loewerj
|   fixed compile warnings
|
|   Revision 1.5  2002/06/02 06:36:24  zoran
|   Added thread safety with capability of sharing DOM trees between
|   threads and ability to read/write-lock DOM documents
|
|   Revision 1.4  2002/05/16 13:16:00  rolf
|   There's something wrong, with the header files (well, at least VC++6.0
|   thinks so). Seems, it works in this include order.
|
|   Revision 1.3  2002/05/16 12:03:30  rolf
|   Corrected tdom stubs table export.
|
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
#include <tdom.h>
#include <tcl.h>
#include <dom.h>
#include <tcldom.h>

extern TdomStubs tdomStubs;

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
     Tcl_Interp *interp; /* Interpreter to initialize. */
{
#ifdef TCL_THREADS
    char *bool = NULL;
#endif /* TCL_THREADS */

#ifdef USE_TCL_STUBS
    Tcl_InitStubs(interp, "8", 0);
#endif

#ifdef TCL_THREADS
    bool = Tcl_GetVar2(interp, "::tcl_platform", "threaded", 0);
    if (bool == NULL || atoi(bool) == 0) { 
        Tcl_SetObjResult(interp, Tcl_NewStringObj(
                         "Tcl core wasn't compiled for multithreading.", -1));
        return TCL_ERROR;
    }
    domModuleInitialize();
    tcldom_initialize();
#else
    domModuleInitialize();
#endif /* TCL_THREADS */

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
#endif
    
#ifdef USE_TCL_STUBS
    Tcl_PkgProvideEx (interp, "tdom", STR_TDOM_VERSION(TDOM_VERSION), 
                      (ClientData) &tdomStubs);
#else
    Tcl_PkgProvide (interp, "tdom", STR_TDOM_VERSION(TDOM_VERSION));
#endif

    return TCL_OK;
}

int
Tdom_SafeInit (interp)
     Tcl_Interp *interp; /* Interpreter to initialise. */
{
    /* nothing special for safe interpreters -> just call Tdom_Init */
    return Tdom_Init (interp);
}

#ifdef NS_AOLSERVER
# include "aolserver.cpp"
#endif
