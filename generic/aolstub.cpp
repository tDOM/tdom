/*
 * aolstub.cpp --
 *
 * Adds interface for loading the extension into the AOLserver.
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 *
 * Rcsid: @(#)$Id$
 * ---------------------------------------------------------------------------
 */

#if defined (NS_AOLSERVER)
#include <ns.h>

int Ns_ModuleVersion = 1;

/*
 *----------------------------------------------------------------------------
 *
 * NsTdom_Init --
 *
 *    Loads the package for the first time, i.e. in the startup thread.
 *
 * Results:
 *    Standard Tcl result
 *
 * Side effects:
 *    Package initialized. Tcl commands created.
 *
 *----------------------------------------------------------------------------
 */

static int
NsTdom_Init (Tcl_Interp *interp, void *context)
{
    int ret = Tdom_Init(interp);

    if (ret != TCL_OK) {
        Ns_Log(Warning, "can't load module %s: %s", 
               (char *)context, Tcl_GetStringResult(interp));
    } else {
        Ns_Log(Notice, "%s module", (char*)context);
    }

    return ret;
}

/*
 *----------------------------------------------------------------------------
 *
 * Ns_ModuleInit --
 *
 *    Called by the AOLserver when loading shared object file.
 *
 * Results:
 *    Standard AOLserver result
 *
 * Side effects:
 *    Many. Depends on the package.
 *
 *----------------------------------------------------------------------------
 */

int
Ns_ModuleInit(char *hServer, char *hMod)
{
    return (Ns_TclInitInterps(hServer, NsTdom_Init, (void*)hMod) == TCL_OK)
        ? NS_OK : NS_ERROR; 
}

#endif /* NS_AOLSERVER */

/* EOF $RCSfile$ */

/* Emacs Setup Variables */
/* Local Variables:      */
/* mode: C               */
/* indent-tabs-mode: nil */
/* c-basic-offset: 4     */
/* End:                  */
