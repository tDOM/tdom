/*
 * tclexpat.c --
 *
 *   A Tcl interface to James Clark's expat XML parser
 *
 *        Copyright (c) 1998 Steve Ball, Zveno Pty Ltd
 *
 *   with modifications
 *   by Jochen Loewer(loewerj@hotmail.com) (July 1999)
 *   by ericm@scriptics.com, 1999.6.25
 *   by Rolf Ade (rolf@pointsman.de) (2000, 2001)
 *
 *
 * Zveno Pty Ltd makes this software and associated documentation
 * available free of charge for any purpose.  You may make copies
 * of the software but you must include all of this notice on any copy.
 *
 * Zveno Pty Ltd does not warrant that this software is error free
 * or fit for any purpose.  Zveno Pty Ltd disclaims any liability for
 * all claims, expenses, losses, damages and costs any user may incur
 * as a result of using, copying or modifying the software.
 *
 * Jochen Loewer does not warrant that this software is error free
 * or fit for any purpose.  Jochen Loewer disclaims any liability for
 * all claims, expenses, losses, damages and costs any user may incur
 * as a result of using, copying or modifying the software.
 *
 */


/*----------------------------------------------------------------------------
|   Includes
|
\---------------------------------------------------------------------------*/
#include <tcl.h>
#include <string.h>
#include <dom.h>
#include <tclexpat.h>
#include <fcntl.h>

#ifdef _MSC_VER
#include <io.h>
#endif

#ifdef _POSIX_SOURCE
#include <unistd.h>
#endif

/* Used internal als status, like TCL_OK, TCL_ERROR etc.  As a
   consequent, application specific error codes must be at least
   greater than 5 */
#define ERROR_IN_EXTREFHANDLER 5

#define READ_SIZE (1024*8)
#ifndef O_BINARY
#ifdef _O_BINARY
#define O_BINARY _O_BINARY
#else
#define O_BINARY 0
#endif
#endif


/*----------------------------------------------------------------------------
|   Macros
|
\---------------------------------------------------------------------------*/
#define DBG(x)
#define SetResult(interp,str) \
                     (Tcl_SetStringObj (Tcl_GetObjResult (interp), (str), -1))
#define SetIntResult(interp,i) \
                     (Tcl_SetIntObj (Tcl_GetObjResult (interp), (i) ))
#define AppendResult(interp,str) \
                     (Tcl_AppendToObj (Tcl_GetObjResult (interp), (str), -1))
#define CheckArgs(min,max,n,msg) \
                     if ((objc < min) || (objc >max)) { \
                         Tcl_WrongNumArgs(interp, n, objv, msg); \
                         return TCL_ERROR; \
                     }
#define CheckDefaultTclHandlerSet \
                      if (!activeTclHandlerSet) { \
                         activeTclHandlerSet = CreateTclHandlerSet("default");\
                         tmpTclHandlerSet = expat->firstTclHandlerSet; \
                         expat->firstTclHandlerSet = activeTclHandlerSet; \
                         activeTclHandlerSet->nextHandlerSet = tmpTclHandlerSet; \
                      }

/*----------------------------------------------------------------------------
|   typedefs
|
\---------------------------------------------------------------------------*/

typedef enum {
    EXPAT_INPUT_STRING,
    EXPAT_INPUT_CHANNEL,
    EXPAT_INPUT_FILENAME
} TclExpat_InputType;



/*----------------------------------------------------------------------------
|   local globals
|
\---------------------------------------------------------------------------*/

static int uniqueCounter = 0;  /* Counter to generate unique command names
                                */
TDomThreaded(static Tcl_Mutex counterMutex;) /* Protect the counter (zv) */

/*----------------------------------------------------------------------------
|   Prototypes for procedures defined later in this file:
|
\---------------------------------------------------------------------------*/
int             TclExpatObjCmd _ANSI_ARGS_((ClientData dummy,
                    Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[]));
static int      TclExpatInstanceCmd _ANSI_ARGS_((ClientData dummy,
                    Tcl_Interp *interp, int objc, struct Tcl_Obj *CONST objv[]));
static void     TclExpatDeleteCmd _ANSI_ARGS_((ClientData clientData));

static Tcl_Obj* FindUniqueCmdName _ANSI_ARGS_((Tcl_Interp *interp));
static int      TclExpatCheckWhiteData _ANSI_ARGS_((char *pc, int len));

static int      TclExpatCreateParser _ANSI_ARGS_((Tcl_Interp *interp,
                    TclGenExpatInfo *expat ));
static void     TclExpatFreeParser  _ANSI_ARGS_((TclGenExpatInfo *expat));
static int      TclExpatParse _ANSI_ARGS_((Tcl_Interp *interp,
                    TclGenExpatInfo *expat, char *data, int len,
                                     TclExpat_InputType type));
static int      TclExpatConfigure _ANSI_ARGS_((Tcl_Interp *interp,
                    TclGenExpatInfo *expat, int objc, Tcl_Obj *CONST objv[]));
static int      TclExpatCget _ANSI_ARGS_((Tcl_Interp *interp,
                    TclGenExpatInfo *expat, int objc, Tcl_Obj *CONST objv[]));

static int	TclExpatGet _ANSI_ARGS_((Tcl_Interp *interp,
		    TclGenExpatInfo *expat, int objc, Tcl_Obj *CONST objv[]));
static void	TclExpatDispatchPCDATA _ANSI_ARGS_((TclGenExpatInfo *expat));
static void TclGenExpatElementStartHandler _ANSI_ARGS_((void *userdata,
                                                        const XML_Char *name,
                                                        const XML_Char **atts));
static void TclGenExpatElementEndHandler _ANSI_ARGS_((void *userData,
                                                      const XML_Char *name));
static void TclGenExpatCharacterDataHandler _ANSI_ARGS_((void *userData,
                                                         const XML_Char *s,
                                                         int len));

static void 	TclGenExpatProcessingInstructionHandler _ANSI_ARGS_((
	    	    void *userData, const XML_Char *target,
	    	    const XML_Char *data));
static int 	TclGenExpatExternalEntityRefHandler _ANSI_ARGS_((
	    	    XML_Parser parser, const XML_Char *openEntityNames,
	    	    const XML_Char *base, const XML_Char *systemId,
	    	    const XML_Char *publicId));
static void 	TclGenExpatDefaultHandler _ANSI_ARGS_ ((void *userData,
	    	    const XML_Char *s, int len));
static void 	TclGenExpatNotationDeclHandler _ANSI_ARGS_ ((void *userData,
		    const XML_Char *notationName, const XML_Char *base,
		    const XML_Char *systemId, const XML_Char *publicId));
static int	TclGenExpatUnknownEncodingHandler _ANSI_ARGS_ ((
		    void *encodingHandlerData, const XML_Char *name,
		    XML_Encoding *info));

static void  TclGenExpatStartNamespaceDeclHandler _ANSI_ARGS_((void *userdata,
                                                               const XML_Char *prefix,
                                                               const XML_Char *uri));
static void  TclGenExpatEndNamespaceDeclHandler _ANSI_ARGS_((void *userData,
                                                          const XML_Char *prefix));


/* Following added by ericm@scriptics, 1999.6.25 */
/* Prototype definition for the TclExpat comment handler */
static void 	TclGenExpatCommentHandler _ANSI_ARGS_ ((void *userData,
						     const XML_Char *data));
/* Prototype for TclExpat Not Standalone Handler */
static int 	TclGenExpatNotStandaloneHandler _ANSI_ARGS_ ((void *userData));

/* Prototype for TclExpat {Start|End}CdataSectionHandler */
static void 	TclGenExpatStartCdataSectionHandler _ANSI_ARGS_((void *userData));
static void 	TclGenExpatEndCdataSectionHandler _ANSI_ARGS_((void *userData));

/* Added by ericm@scriptics.com, 1999.09.13 */
/* Prototype for TclExpat (Element|Attlist) Declaration Handlers */
static void     TclGenExpatElementDeclHandler _ANSI_ARGS_((void *userData,
                    const XML_Char *name, XML_Content *model));
static void     TclGenExpatAttlistDeclHandler _ANSI_ARGS_((void *userData,
                    const XML_Char *elname, const XML_Char *name,
                    const XML_Char *type, const XML_Char *dflt,
                    int isrequired));
/* Prototypes for the TclExpat Doctype Decl handlers */
static void     TclGenExpatStartDoctypeDeclHandler _ANSI_ARGS_((void *userData,
                    const XML_Char *doctypeName, const XML_Char *sysid,
                    const XML_Char *pubid, int has_internal_subset));
static void     TclGenExpatEndDoctypeDeclHandler _ANSI_ARGS_((void *userData));
static void     TclGenExpatXmlDeclHandler _ANSI_ARGS_((void *userData,
                                                       const XML_Char *version,
                                                       const XML_Char *encoding,
                                                       int standalone));
static void     TclGenExpatEntityDeclHandler _ANSI_ARGS_((void *userData,
                                                          const XML_Char *entityname,
                                                          int is_param,
                                                          const XML_Char *value,
                                                          int length,
                                                          CONST XML_Char *base,
                                                          CONST XML_Char *systemId,
                                                          CONST XML_Char *publicId,
                                                          CONST XML_Char *notationName));


/*
 *----------------------------------------------------------------------------
 *
 * CreateTclHandlerSet --
 *
 *	Malloc's and initializes a tclHandlerSet.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Mallocs memory for the structure and the 'name' field, sets all
 *      handler scripts to NULL and inits some other fields.
 *
 *----------------------------------------------------------------------------
 */

static TclHandlerSet*
CreateTclHandlerSet (name)
    char *name;
{
    TclHandlerSet *handlerSet;

    handlerSet = (TclHandlerSet*) MALLOC (sizeof (TclHandlerSet)); \
    handlerSet->name                      = tdomstrdup (name);
    handlerSet->ignoreWhiteCDATAs         = 0;
    handlerSet->status                    = TCL_OK;
    handlerSet->continueCount             = 0;
    handlerSet->nextHandlerSet            = NULL;

    handlerSet->elementstartcommand      = NULL;
    handlerSet->elementendcommand        = NULL;
    handlerSet->startnsdeclcommand       = NULL;
    handlerSet->endnsdeclcommand         = NULL;
    handlerSet->datacommand              = NULL;
    handlerSet->picommand                = NULL;
    handlerSet->defaultcommand           = NULL;
    handlerSet->notationcommand          = NULL;
    handlerSet->externalentitycommand    = NULL;
    handlerSet->unknownencodingcommand   = NULL;
    handlerSet->commentCommand           = NULL;
    handlerSet->notStandaloneCommand     = NULL;
    handlerSet->startCdataSectionCommand = NULL;
    handlerSet->endCdataSectionCommand   = NULL;
    handlerSet->elementDeclCommand       = NULL;
    handlerSet->attlistDeclCommand       = NULL;
    handlerSet->startDoctypeDeclCommand  = NULL;
    handlerSet->endDoctypeDeclCommand    = NULL;
    handlerSet->xmlDeclCommand           = NULL;
    handlerSet->entityDeclCommand        = NULL;
    return handlerSet;
}

/*
 *----------------------------------------------------------------------------
 *
 * CHandlerSetCreate --
 *
 *	Initializes a CHandlerSet.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Mallocs memory for the 'name' of the structure, sets all
 *      handler functions to NULL and inits some other fields.
 *
 *----------------------------------------------------------------------------
 */

CHandlerSet*
CHandlerSetCreate (name)
    char *name;
{
    CHandlerSet *handlerSet;

    handlerSet = (CHandlerSet *) MALLOC (sizeof (CHandlerSet));
    handlerSet->name                     = tdomstrdup (name);
    handlerSet->ignoreWhiteCDATAs        = 0;
    handlerSet->nextHandlerSet           = NULL;

    handlerSet->userData                 = NULL;

    handlerSet->resetProc                = NULL;
    handlerSet->freeProc                 = NULL;
    handlerSet->initParseProc            = NULL;
    handlerSet->parserResetProc          = NULL;

    handlerSet->elementstartcommand      = NULL;
    handlerSet->elementendcommand        = NULL;
    handlerSet->startnsdeclcommand       = NULL;
    handlerSet->endnsdeclcommand         = NULL;
    handlerSet->datacommand              = NULL;
    handlerSet->picommand                = NULL;
    handlerSet->defaultcommand           = NULL;
    handlerSet->notationcommand          = NULL;
    handlerSet->externalentitycommand    = NULL;
    handlerSet->unknownencodingcommand   = NULL;
    handlerSet->commentCommand           = NULL;
    handlerSet->notStandaloneCommand     = NULL;
    handlerSet->startCdataSectionCommand = NULL;
    handlerSet->endCdataSectionCommand   = NULL;
    handlerSet->elementDeclCommand       = NULL;
    handlerSet->attlistDeclCommand       = NULL;
    handlerSet->startDoctypeDeclCommand  = NULL;
    handlerSet->endDoctypeDeclCommand    = NULL;
    handlerSet->xmlDeclCommand           = NULL;
    handlerSet->entityDeclCommand        = NULL;
    return handlerSet;
}

/*
 *----------------------------------------------------------------------------
 *
 * TclExpatObjCmd --
 *
 *	Creation command for expat class.
 *
 * Results:
 *	The name of the newly created parser instance.
 *
 * Side effects:
 *	This creates an expat parser.
 *
 *----------------------------------------------------------------------------
 */

int
TclExpatObjCmd(dummy, interp, objc, objv)
     ClientData dummy;
     Tcl_Interp *interp;
     int objc;
     Tcl_Obj *CONST objv[];
{
  TclGenExpatInfo *genexpat;
  int ns_mode = 0;
  char *nsoption;


  /*
   * Create the data structures for this parser.
   */

  if (!(genexpat = (TclGenExpatInfo *) MALLOC(sizeof(TclGenExpatInfo)))) {
    FREE( (char*) genexpat);
    Tcl_SetResult(interp, "unable to create parser", NULL);
    return TCL_ERROR;
  }
  genexpat->interp = interp;

  /*
   * Find unique command name
   */
  if (objc < 2) {
    genexpat->name = FindUniqueCmdName(interp);
  } else {
    genexpat->name = objv[1];
    if (*(Tcl_GetStringFromObj(genexpat->name, NULL)) != '-') {
      Tcl_IncrRefCount(genexpat->name);
      objv++;
      objc--;
    } else {
      genexpat->name = FindUniqueCmdName(interp);
    }
  }

  genexpat->firstTclHandlerSet = NULL;
  genexpat->firstCHandlerSet   = NULL;
  genexpat->eContents          = NULL;

  if (objc > 1) {
      nsoption = Tcl_GetStringFromObj(objv[1], NULL);
      if (strcmp(nsoption,"-namespace")==0) {
          ns_mode = 1;
          objv++;
          objc--;
      }
  }
  genexpat->ns_mode = ns_mode;
  genexpat->nsSeparator = ':';

  if (TclExpatCreateParser(interp, genexpat) != TCL_OK) {
    FREE( (char*) genexpat);
    return TCL_ERROR;
  }

  /*
   * Register a Tcl command for this parser instance.
   */

  Tcl_CreateObjCommand(interp, Tcl_GetStringFromObj(genexpat->name,NULL),
                               TclExpatInstanceCmd, (ClientData) genexpat,
                               TclExpatDeleteCmd);
  /*
   * Handle configuration options
   */

  if (objc > 1) {
      if (TclExpatConfigure(interp, genexpat, objc - 1, objv + 1) != TCL_OK) {
          return TCL_ERROR;
      }
  }

  Tcl_SetObjResult(interp, genexpat->name);

  return TCL_OK;
}


/*
 *----------------------------------------------------------------------------
 *
 * FindUniqueCmdName --
 *
 *	Generate new command name in caller's namespace.
 *
 * Results:
 *	Returns newly allocated Tcl object containing name.
 *
 * Side effects:
 *	Allocates Tcl object.
 *
 *----------------------------------------------------------------------------
 */

static Tcl_Obj *
FindUniqueCmdName(interp)
     Tcl_Interp *interp;
{
  Tcl_Obj *name;
  Tcl_CmdInfo info;
  char s[20];

  name = Tcl_NewStringObj("", 0);
  Tcl_IncrRefCount(name);

  do {
    TDomThreaded(Tcl_MutexLock(&counterMutex);)
    sprintf(s, "xmlparser%d", uniqueCounter++);
    TDomThreaded(Tcl_MutexUnlock(&counterMutex);)
    Tcl_SetStringObj(name, s, -1);

  } while (Tcl_GetCommandInfo(interp, Tcl_GetStringFromObj(name, NULL), &info));

  return name;
}

/*
 *----------------------------------------------------------------------------
 *
 * TclExpatCreateParser --
 *
 *	Create the expat parser and initialise (some of) the TclExpatInfo
 *	structure.
 *
 *	Note that callback commands are not affected by this routine,
 *	to allow a reset to leave these intact.
 *
 * Results:
 *	New parser instance created and initialised.
 *
 * Side effects:
 *	Creates an expat parser.
 *	Modifies TclExpatInfo fields.
 *
 *----------------------------------------------------------------------------
 */

static int
TclExpatCreateParser(interp, expat)
     Tcl_Interp *interp;
     TclGenExpatInfo *expat;
{

  if (expat->ns_mode) {
      if (!(expat->parser = XML_ParserCreateNS(NULL,expat->nsSeparator))) {
          Tcl_SetResult(interp, "unable to create expat parserNs", NULL);
          return TCL_ERROR;
      }
  } else {
      if (!(expat->parser = XML_ParserCreate(NULL))) {
          Tcl_SetResult(interp, "unable to create expat parser", NULL);
          return TCL_ERROR;
      }
  }

  expat->final         = 1;
  expat->status        = TCL_OK;
  expat->needWSCheck   = 0;
  expat->result        = NULL;
  expat->cdata         = NULL;

  /*
   * Set handlers for the parser to routines in this module.
   */

  XML_SetElementHandler(expat->parser,
			(XML_StartElementHandler) TclGenExpatElementStartHandler,
			(XML_EndElementHandler) TclGenExpatElementEndHandler);
  XML_SetNamespaceDeclHandler(expat->parser,
			(XML_StartNamespaceDeclHandler) TclGenExpatStartNamespaceDeclHandler,
			(XML_EndNamespaceDeclHandler) TclGenExpatEndNamespaceDeclHandler);
  XML_SetCharacterDataHandler(expat->parser,
			      (XML_CharacterDataHandler) TclGenExpatCharacterDataHandler);
  XML_SetProcessingInstructionHandler(expat->parser,
				      (XML_ProcessingInstructionHandler) TclGenExpatProcessingInstructionHandler);
  XML_SetDefaultHandlerExpand(expat->parser,
			(XML_DefaultHandler) TclGenExpatDefaultHandler);

  XML_SetNotationDeclHandler(expat->parser,
			     (XML_NotationDeclHandler) TclGenExpatNotationDeclHandler);
  XML_SetExternalEntityRefHandler(expat->parser,
				  (XML_ExternalEntityRefHandler) TclGenExpatExternalEntityRefHandler);
  XML_SetUnknownEncodingHandler(expat->parser,
				(XML_UnknownEncodingHandler) TclGenExpatUnknownEncodingHandler,
				(void *) expat);


  XML_SetCommentHandler(expat->parser, TclGenExpatCommentHandler);

  XML_SetNotStandaloneHandler(expat->parser, TclGenExpatNotStandaloneHandler);

  XML_SetCdataSectionHandler(expat->parser, TclGenExpatStartCdataSectionHandler,
	  TclGenExpatEndCdataSectionHandler);

  XML_SetElementDeclHandler(expat->parser, TclGenExpatElementDeclHandler);

  XML_SetAttlistDeclHandler(expat->parser, TclGenExpatAttlistDeclHandler);

  XML_SetDoctypeDeclHandler(expat->parser,
	  TclGenExpatStartDoctypeDeclHandler,
	  TclGenExpatEndDoctypeDeclHandler);

  XML_SetXmlDeclHandler (expat->parser, TclGenExpatXmlDeclHandler);

  XML_SetEntityDeclHandler (expat->parser,
                            TclGenExpatEntityDeclHandler);

  XML_SetUserData(expat->parser, (void *) expat);

  return TCL_OK;
}

/*
 *----------------------------------------------------------------------------
 *
 * TclExpatFreeParser --
 *
 *	Destroy the expat parser structure and frees the stored content models,
 *      if there one.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Frees any memory allocated for the XML parser and (if still present)
 *      the stored content models.
 *
 *----------------------------------------------------------------------------
 */

static void
TclExpatFreeParser(expat)
     TclGenExpatInfo *expat;
{
  ExpatElemContent *eContent, *eContentSave;

  XML_ParserFree(expat->parser);
  if (expat->cdata != NULL) {
      Tcl_DecrRefCount(expat->cdata);
  }
  expat->parser = NULL;

  eContent = expat->eContents;
  while (eContent) {
      free(eContent->content); /* This *must* be done with free() */
      eContentSave = eContent;
      eContent = eContent->next;
      FREE((char *) eContentSave);
  }
  expat->eContents = NULL;
}

/*
 *----------------------------------------------------------------------------
 *
 * TclExpatInstanceCmd --
 *
 *	Implements instance command for expat class objects.
 *
 * Results:
 *	Depends on the method.
 *
 * Side effects:
 *	Depends on the method.
 *
 *----------------------------------------------------------------------------
 */

static int
TclExpatInstanceCmd (clientData, interp, objc, objv)
     ClientData clientData;
     Tcl_Interp *interp;
     int objc;
     Tcl_Obj *CONST objv[];
{
  TclGenExpatInfo *expat = (TclGenExpatInfo *) clientData;
  char *data;
  int len = 0, optionIndex, result = TCL_OK;
  CHandlerSet *activeCHandlerSet;
  ExpatElemContent *eContent, *eContentSave;

  static CONST84 char *options[] = {
      "configure", "cget", "free", "get",
      "parse", "parsechannel", "parsefile", "reset", NULL
  };
  enum options {
      EXPAT_CONFIGURE, EXPAT_CGET, EXPAT_FREE, EXPAT_GET,
      EXPAT_PARSE, EXPAT_PARSECHANNEL, EXPAT_PARSEFILE, EXPAT_RESET
  };


  CheckArgs (2,20,1,"method ?args?");
  if (Tcl_GetIndexFromObj(interp, objv[1], options, "option", 0,
			  &optionIndex) != TCL_OK) {
    return TCL_ERROR;
  }

  switch ((enum options) optionIndex) {
    case EXPAT_CONFIGURE:

      result = TclExpatConfigure(interp, expat, objc - 2, objv + 2);
      break;

    case EXPAT_CGET:

      result = TclExpatCget(interp, expat, objc - 2, objv + 2);
      break;

    case EXPAT_FREE:

	/* ericm@scriptics.com, 1999.9.13 */
	Tcl_DeleteCommand(interp, Tcl_GetStringFromObj(expat->name,NULL));
	result = TCL_OK;
	break;

    case EXPAT_GET:

      /* ericm@scriptics.com, 1999.6.28 */
      result = TclExpatGet(interp, expat, objc - 2, objv + 2);
      break;

    case EXPAT_PARSE:

      CheckArgs (3,3,2,"<XML-String>");
      data = Tcl_GetStringFromObj(objv[2], &len);
      result = TclExpatParse(interp, expat, data, len, EXPAT_INPUT_STRING);
      break;

    case EXPAT_PARSECHANNEL:

      CheckArgs (3,3,2,"<Tcl-Channel>");
      data = Tcl_GetStringFromObj (objv[2], NULL);
      result = TclExpatParse(interp, expat, data, len, EXPAT_INPUT_CHANNEL);
      break;

    case EXPAT_PARSEFILE:

      CheckArgs (3,3,2, "<filename>");
      data = Tcl_GetStringFromObj (objv[2], NULL);
      result = TclExpatParse (interp, expat, data, len, EXPAT_INPUT_FILENAME);
      break;

    case EXPAT_RESET:

      CheckArgs (2,2,1,"");

      /*
       * Destroy the parser and create a fresh one.
       */
      TclExpatFreeParser(expat);
      TclExpatCreateParser(interp, expat);
      eContent = expat->eContents;
      while (eContent) {
          free(eContent->content); /* This *must* be done with free() */
          eContentSave = eContent;
          eContent = eContent->next;
          FREE((char *) eContentSave);
      }
      expat->eContents = NULL;

      activeCHandlerSet = expat->firstCHandlerSet;
      while (activeCHandlerSet) {
          if (activeCHandlerSet->parserResetProc) {
              activeCHandlerSet->parserResetProc (expat->parser,
                                                  activeCHandlerSet->userData);
          }
          if (activeCHandlerSet->resetProc) {
              activeCHandlerSet->resetProc (interp, 
                                            activeCHandlerSet->userData);
          }
          activeCHandlerSet = activeCHandlerSet->nextHandlerSet;
      }

      break;

    default:

      Tcl_SetResult(interp, "unknown method", NULL);
      return TCL_ERROR;

  }

  return result;
}


/*
 *----------------------------------------------------------------------------
 *
 * TclExpatParse --
 *
 *	Wrapper to invoke expat parser and check return result.
 *
 * Results:
 *     TCL_OK if no errors, TCL_ERROR otherwise.
 *
 * Side effects:
 *     Sets interpreter result as appropriate.
 *
 *----------------------------------------------------------------------------
 */

static int
TclExpatParse (interp, expat, data, len, type)
     Tcl_Interp *interp;
     TclGenExpatInfo *expat;
     char *data;
     int len;
     TclExpat_InputType type;
{
  int result, mode, bytesread, done;
  char s[255], buf[8*1024];
  int fd;
  XML_Parser  parser;
  Tcl_Channel channel = NULL;
  CHandlerSet *activeCHandlerSet;
#if !TclOnly8Bits
  Tcl_Obj       *bufObj;
  Tcl_DString    dStr;
  int            useBinary;
  char          *str;
#endif

  expat->status = TCL_OK;
  if (expat->result != NULL) {
    Tcl_DecrRefCount(expat->result);
  }

  activeCHandlerSet = expat->firstCHandlerSet;
  while (activeCHandlerSet) {
      if (activeCHandlerSet->initParseProc) {
          activeCHandlerSet->initParseProc ((const TclGenExpatInfo *) expat,
                                            activeCHandlerSet->userData);
      }
      activeCHandlerSet = activeCHandlerSet->nextHandlerSet;
  }

  expat->result = NULL;
  Tcl_ResetResult (interp);
  result = 1;
  switch (type) {

  case EXPAT_INPUT_STRING:
      result = XML_Parse(expat->parser,
                         data, len,
                         expat->final);
      break;

  case EXPAT_INPUT_CHANNEL:
      channel = Tcl_GetChannel (interp, data, &mode);
      if (channel == NULL) {
          Tcl_ResetResult (interp);
          Tcl_AppendResult (interp, "\"", data, "\" isn't a Tcl channel in this interpreter", (char *) NULL);
          return TCL_ERROR;
      }
      if (!(mode & TCL_READABLE)) {
          Tcl_ResetResult (interp);
          Tcl_AppendResult (interp, "channel \"", data, "wasn't opened for reading", (char *) NULL);
          return TCL_ERROR;
      }
#if !TclOnly8Bits
      Tcl_DStringInit (&dStr);
      if (Tcl_GetChannelOption (interp, channel, "-encoding", &dStr) != TCL_OK) {
          return TCL_ERROR;
      }
      if (strcmp (Tcl_DStringValue (&dStr), "binary")==0 ) useBinary = 1;
      else useBinary = 0;
      Tcl_DStringFree (&dStr);
      if (useBinary) {
          do {
              bytesread = Tcl_Read (channel, buf, sizeof (buf));
              done = bytesread < sizeof (buf);
              if (done) {
                  result = XML_Parse (expat->parser, buf, bytesread, done);
              } else {
                  if (!XML_Parse (expat->parser, buf, bytesread, done)) {
                      result = 0;
                      break;
                  }
              }
          } while (!done);
      } else {
          bufObj = Tcl_NewObj();
          Tcl_IncrRefCount (bufObj);
          Tcl_SetObjLength (bufObj, 6144);
          do {
              len = Tcl_ReadChars (channel, bufObj, 1024, 0);
              done = (len < 1024);
              str = Tcl_GetStringFromObj (bufObj, &len);
              if (!XML_Parse (expat->parser, str, len, done)) {
                  result = 0;
                  break;
              }
          } while (!done);
          result = 1;
          Tcl_DecrRefCount (bufObj);
      }
#else
      do {
          bytesread = Tcl_Read (channel, buf, sizeof (buf));
          done = bytesread < sizeof (buf);
          if (done) {
              result = XML_Parse (expat->parser, buf, bytesread, done);
          } else {
              if (!XML_Parse (expat->parser, buf, bytesread, done)) {
                  result = 0;
                      break;
              }
          }
      } while (!done);
#endif
      break;

  case EXPAT_INPUT_FILENAME:
      fd = open(data, O_BINARY|O_RDONLY);
      if (fd < 0) {
          Tcl_ResetResult (interp);
          Tcl_AppendResult (interp, "error opening file \"",
                            data, "\"", (char *) NULL);
          return TCL_ERROR;
      }
      parser = expat->parser;
      for (;;) {
          int nread;
          char *fbuf = XML_GetBuffer (parser, READ_SIZE);
          if (!fbuf) {
              close (fd);
              Tcl_ResetResult (interp);
              Tcl_SetResult (interp, "Out of memory\n", NULL);
              return TCL_ERROR;
          }
          nread = read(fd, fbuf, READ_SIZE);
          if (nread < 0) {
              close (fd);
              Tcl_ResetResult (interp);
              Tcl_AppendResult (interp, "error reading from file \"",
                                data, "\"", (char *) NULL);
              return TCL_ERROR;
          }
          if (!XML_ParseBuffer (parser, nread, nread == 0)) {
              close (fd);
              result = 0;
              break;
          }
          if (nread == 0) {
              close(fd);
              break;
          }
      }
      break;
  }

  if (!result) {
      if (expat->status == ERROR_IN_EXTREFHANDLER) {
          Tcl_SetObjResult (interp, expat->result);
      }
      else {
          Tcl_ResetResult(interp);
          sprintf(s, "%d", XML_GetCurrentLineNumber(expat->parser));
          Tcl_AppendResult(interp, "error \"",
                           XML_ErrorString(XML_GetErrorCode(expat->parser)),
                           "\" at line ", s, " character ", NULL);
          sprintf(s, "%d", XML_GetCurrentColumnNumber(expat->parser));
          Tcl_AppendResult(interp, s, NULL);
      }
      return TCL_ERROR;
  }
  switch (expat->status) {
    case TCL_OK:
    case TCL_BREAK:
    case TCL_CONTINUE:
      Tcl_ResetResult(interp);
      return TCL_OK;

    case TCL_ERROR:
      Tcl_SetObjResult(interp, expat->result);
      return TCL_ERROR;

    default:
      /*
       * Propagate application-specific error condition.
       * Patch by Marshall Rose <mrose@dbc.mtview.ca.us>
       */
      Tcl_SetObjResult(interp, expat->result);
      return expat->status;
  }

}



/*
 *----------------------------------------------------------------------------
 *
 * TclExpatConfigure --
 *
 *	Implements instance command for expat class objects.
 *
 * Results:
 *	Depends on the method.
 *
 * Side effects:
 *	Depends on the method.
 *
 *----------------------------------------------------------------------------
 */

static int
TclExpatConfigure (interp, expat, objc, objv)
     Tcl_Interp *interp;
     TclGenExpatInfo *expat;
     int objc;
     Tcl_Obj *CONST objv[];
{
  static CONST84 char *switches[] = {
    "-final",
    "-baseurl",
    "-elementstartcommand",
    "-elementendcommand",
    "-characterdatacommand",
    "-processinginstructioncommand",
    "-defaultcommand",
    "-notationdeclcommand",
    "-externalentitycommand",
    "-unknownencodingcommand",
    "-startnamespacedeclcommand",
    "-endnamespacedeclcommand",
    "-ignorewhitecdata",
    "-useForeignDTD",

    "-commentcommand",
    "-notstandalonecommand",
    "-startcdatasectioncommand",
    "-endcdatasectioncommand",
    "-elementdeclcommand",
    "-attlistdeclcommand",
    "-startdoctypedeclcommand",
    "-enddoctypedeclcommand",
    "-xmldeclcommand",
    "-paramentityparsing",
    "-entitydeclcommand",
    "-ignorewhitespace",
    "-handlerset",
    "-noexpand",
    (char *) NULL
  };
  enum switches {
    EXPAT_FINAL, EXPAT_BASE,
    EXPAT_ELEMENTSTARTCMD, EXPAT_ELEMENTENDCMD,
    EXPAT_DATACMD, EXPAT_PICMD,
    EXPAT_DEFAULTCMD,
    EXPAT_NOTATIONCMD,
    EXPAT_EXTERNALENTITYCMD, EXPAT_UNKNOWNENCODINGCMD,
    EXPAT_STARTNAMESPACEDECLCMD,
    EXPAT_ENDNAMESPACEDECLCMD,
    EXPAT_IGNOREWHITECDATA,
    EXPAT_USEFOREIGNDTD,

    EXPAT_COMMENTCMD, EXPAT_NOTSTANDALONECMD,
    EXPAT_STARTCDATASECTIONCMD, EXPAT_ENDCDATASECTIONCMD,
    EXPAT_ELEMENTDECLCMD, EXPAT_ATTLISTDECLCMD,
    EXPAT_STARTDOCTYPEDECLCMD, EXPAT_ENDDOCTYPEDECLCMD,
    EXPAT_XMLDECLCMD,
    EXPAT_PARAMENTITYPARSING,
    EXPAT_ENTITYDECLCOMMAND,
    EXPAT_NOWHITESPACE,
    EXPAT_HANDLERSET,
    EXPAT_NOEXPAND
  };
  static CONST84 char *paramEntityParsingValues[] = {
      "always",
      "never",
      "notstandalone",
      (char *) NULL
  };
  enum paramEntityParsingValues {
      EXPAT_PARAMENTITYPARSINGALWAYS,
      EXPAT_PARAMENTITYPARSINGNEVER,
      EXPAT_PARAMENTITYPARSINGNOTSTANDALONE
  };
  int optionIndex, value, bool;
  Tcl_Obj *CONST *objPtr = objv;
  Tcl_CmdInfo cmdInfo;
  int rc;
  char *handlerSetName = NULL;
  TclHandlerSet *tmpTclHandlerSet, *activeTclHandlerSet = NULL;

  if (expat->firstTclHandlerSet 
      && (strcmp ("default", expat->firstTclHandlerSet->name)==0)) {
      activeTclHandlerSet = expat->firstTclHandlerSet;
  }
  while (objc > 1) {
    if (Tcl_GetIndexFromObj(interp, objPtr[0], switches,
			    "switch", 0, &optionIndex) != TCL_OK) {
        return TCL_ERROR;
    }
    switch ((enum switches) optionIndex) {
      case EXPAT_FINAL:			/* -final */

	if (Tcl_GetBooleanFromObj(interp, objPtr[1], &bool) != TCL_OK) {
            return TCL_ERROR;
	}

/*  	if (bool && !expat->final) { */

/*  	  doParse = 1; */

/*  	} else if (!bool && expat->final) { */

	  /*
	   * Reset the parser for new input
	   */

/*  	  TclExpatFreeParser(expat); */
/*  	  TclExpatCreateParser(interp, expat); */
/*  	  doParse = 0; */

/*  	} */
        expat->final = bool;
	break;

      case EXPAT_BASE:			/* -base */

	if (XML_SetBase(expat->parser, Tcl_GetStringFromObj(objPtr[1],NULL))
            == 0) {
            Tcl_SetResult(interp, "unable to set base URL", NULL);
            return TCL_ERROR;
	}
	break;

      case EXPAT_ELEMENTSTARTCMD:	/* -elementstartcommand */

        CheckDefaultTclHandlerSet;
	if (activeTclHandlerSet->elementstartcommand != NULL) {
	  Tcl_DecrRefCount(activeTclHandlerSet->elementstartcommand);
	}

	activeTclHandlerSet->elementstartcommand = objPtr[1];
	Tcl_IncrRefCount(activeTclHandlerSet->elementstartcommand);
        rc = Tcl_GetCommandInfo (interp, Tcl_GetStringFromObj(objPtr[1],NULL),
                                 &cmdInfo);
        if (rc && cmdInfo.isNativeObjectProc) {
            DBG(fprintf(stderr, "cmdInfo.objProc=%x \n", 
                        (unsigned int)cmdInfo.objProc);)
            activeTclHandlerSet->elementstartObjProc = cmdInfo.objProc;
            activeTclHandlerSet->elementstartclientData 
                = cmdInfo.objClientData;
        } else {
            Tcl_SetResult(interp, "couldn't set byte compiled callback", NULL);
            return TCL_ERROR;
        }
	break;

      case EXPAT_ELEMENTENDCMD:		/* -elementendcommand */

        CheckDefaultTclHandlerSet;
	if (activeTclHandlerSet->elementendcommand != NULL) {
	  Tcl_DecrRefCount(activeTclHandlerSet->elementendcommand);
	}

	activeTclHandlerSet->elementendcommand = objPtr[1];
	Tcl_IncrRefCount(activeTclHandlerSet->elementendcommand);
        rc = Tcl_GetCommandInfo (interp, Tcl_GetStringFromObj(objPtr[1],NULL),
                                 &cmdInfo);
        if (rc && cmdInfo.isNativeObjectProc) {
            activeTclHandlerSet->elementendObjProc = cmdInfo.objProc;
            activeTclHandlerSet->elementendclientData = cmdInfo.objClientData;
        } else {
            Tcl_SetResult(interp, "couldn't set byte compiled callback", NULL);
            return TCL_ERROR;
        }
	break;

      case EXPAT_STARTNAMESPACEDECLCMD:	/* -startnamespacedeclcommand */

        CheckDefaultTclHandlerSet;
	if (activeTclHandlerSet->startnsdeclcommand != NULL) {
	  Tcl_DecrRefCount(activeTclHandlerSet->startnsdeclcommand);
	}

	activeTclHandlerSet->startnsdeclcommand = objPtr[1];
	Tcl_IncrRefCount(activeTclHandlerSet->startnsdeclcommand);

	break;

      case EXPAT_ENDNAMESPACEDECLCMD:		/* -endnamespacedeclcommand */

        CheckDefaultTclHandlerSet;
	if (activeTclHandlerSet->endnsdeclcommand != NULL) {
	  Tcl_DecrRefCount(activeTclHandlerSet->endnsdeclcommand);
	}

	activeTclHandlerSet->endnsdeclcommand = objPtr[1];
	Tcl_IncrRefCount(activeTclHandlerSet->endnsdeclcommand);

	break;

      case EXPAT_DATACMD:		/* -characterdatacommand */

        CheckDefaultTclHandlerSet;
	if (activeTclHandlerSet->datacommand != NULL) {
	  Tcl_DecrRefCount(activeTclHandlerSet->datacommand);
	}

	activeTclHandlerSet->datacommand = objPtr[1];
	Tcl_IncrRefCount(activeTclHandlerSet->datacommand);
        rc = Tcl_GetCommandInfo (interp, Tcl_GetStringFromObj(objPtr[1],NULL), &cmdInfo);
        if (rc && cmdInfo.isNativeObjectProc) {
            activeTclHandlerSet->datacommandObjProc = cmdInfo.objProc;
            activeTclHandlerSet->datacommandclientData = cmdInfo.objClientData;
        } else {
            Tcl_SetResult(interp, "couldn't set byte compiled callback", NULL);
            return TCL_ERROR;
        }
	break;

      case EXPAT_PICMD:			/* -processinginstructioncommand */

        CheckDefaultTclHandlerSet;
	if (activeTclHandlerSet->picommand != NULL) {
	  Tcl_DecrRefCount(activeTclHandlerSet->picommand);
	}

	activeTclHandlerSet->picommand = objPtr[1];
	Tcl_IncrRefCount(activeTclHandlerSet->picommand);

	break;

      case EXPAT_DEFAULTCMD:		/* -defaultcommand */

        CheckDefaultTclHandlerSet;
	if (activeTclHandlerSet->defaultcommand != NULL) {
	  Tcl_DecrRefCount(activeTclHandlerSet->defaultcommand);
	}

	activeTclHandlerSet->defaultcommand = objPtr[1];
	Tcl_IncrRefCount(activeTclHandlerSet->defaultcommand);

	break;

      case EXPAT_NOTATIONCMD:			/* -notationdeclcommand */

        CheckDefaultTclHandlerSet;
	if (activeTclHandlerSet->notationcommand != NULL) {
	  Tcl_DecrRefCount(activeTclHandlerSet->notationcommand);
	}

	activeTclHandlerSet->notationcommand = objPtr[1];
	Tcl_IncrRefCount(activeTclHandlerSet->notationcommand);

	break;

      case EXPAT_EXTERNALENTITYCMD:	/* -externalentitycommand */

        CheckDefaultTclHandlerSet;
	if (activeTclHandlerSet->externalentitycommand != NULL) {
	  Tcl_DecrRefCount(activeTclHandlerSet->externalentitycommand);
	}

	activeTclHandlerSet->externalentitycommand = objPtr[1];
	Tcl_IncrRefCount(activeTclHandlerSet->externalentitycommand);

	break;

      case EXPAT_UNKNOWNENCODINGCMD:		/* -unknownencodingcommand */

	/* Not implemented */
	break;

        CheckDefaultTclHandlerSet;
	if (activeTclHandlerSet->unknownencodingcommand != NULL) {
	  Tcl_DecrRefCount(activeTclHandlerSet->unknownencodingcommand);
	}

	activeTclHandlerSet->unknownencodingcommand = objPtr[1];
	Tcl_IncrRefCount(activeTclHandlerSet->unknownencodingcommand);

	break;

      case EXPAT_NOWHITESPACE:
      case EXPAT_IGNOREWHITECDATA:		/* -ignorewhitecdata */

        CheckDefaultTclHandlerSet;
        if (Tcl_GetBooleanFromObj (interp, objPtr[1],
                                   &activeTclHandlerSet->ignoreWhiteCDATAs)
            != TCL_OK) {
            return TCL_ERROR;
        }
        if (activeTclHandlerSet->ignoreWhiteCDATAs) {
            expat->needWSCheck = 1;
        }
	break;

      case EXPAT_USEFOREIGNDTD:                /* -useForeignDTD */
        
        if (Tcl_GetBooleanFromObj (interp, objPtr[1], &bool) != TCL_OK) {
            Tcl_SetResult (interp,
                           "-useForeignDTD needs a boolean argument", NULL);
            return TCL_ERROR;
        }
        /* Cannot be changed after parsing as started (which is kind of
           understandable). We silently ignore return code. */
        XML_UseForeignDTD (expat->parser, bool);
        break;

      case EXPAT_COMMENTCMD:      /* -commentcommand */
	/* ericm@scriptics.com */
        CheckDefaultTclHandlerSet;
	if (activeTclHandlerSet->commentCommand != NULL) {
	  Tcl_DecrRefCount(activeTclHandlerSet->commentCommand);
	}

	activeTclHandlerSet->commentCommand = objPtr[1];
	Tcl_IncrRefCount(activeTclHandlerSet->commentCommand);

	break;

      case EXPAT_NOTSTANDALONECMD:      /* -notstandalonecommand */
	/* ericm@scriptics.com */
        CheckDefaultTclHandlerSet;
	if (activeTclHandlerSet->notStandaloneCommand != NULL) {
	  Tcl_DecrRefCount(activeTclHandlerSet->notStandaloneCommand);
	}

	activeTclHandlerSet->notStandaloneCommand = objPtr[1];
	Tcl_IncrRefCount(activeTclHandlerSet->notStandaloneCommand);

	break;

      case EXPAT_STARTCDATASECTIONCMD:	/* -startcdatasectioncommand */
	/* ericm@scriptics */
        CheckDefaultTclHandlerSet;
	if (activeTclHandlerSet->startCdataSectionCommand != NULL) {
	  Tcl_DecrRefCount(activeTclHandlerSet->startCdataSectionCommand);
	}

	activeTclHandlerSet->startCdataSectionCommand = objPtr[1];
	Tcl_IncrRefCount(activeTclHandlerSet->startCdataSectionCommand);

	break;

      case EXPAT_ENDCDATASECTIONCMD:		/* -endcdatasectioncommand */
	/* ericm@scriptics */
        CheckDefaultTclHandlerSet;
	if (activeTclHandlerSet->endCdataSectionCommand != NULL) {
	  Tcl_DecrRefCount(activeTclHandlerSet->endCdataSectionCommand);
        }

	activeTclHandlerSet->endCdataSectionCommand = objPtr[1];
	Tcl_IncrRefCount(activeTclHandlerSet->endCdataSectionCommand);

	break;

      case EXPAT_ELEMENTDECLCMD:      /* -elementdeclcommand */
	/* ericm@scriptics.com */
        CheckDefaultTclHandlerSet;
	if (activeTclHandlerSet->elementDeclCommand != NULL) {
	  Tcl_DecrRefCount(activeTclHandlerSet->elementDeclCommand);
	}

	activeTclHandlerSet->elementDeclCommand = objPtr[1];
	Tcl_IncrRefCount(activeTclHandlerSet->elementDeclCommand);

	break;

      case EXPAT_ATTLISTDECLCMD:      /* -attlistdeclcommand */
	/* ericm@scriptics.com */
        CheckDefaultTclHandlerSet;
	if (activeTclHandlerSet->attlistDeclCommand != NULL) {
	  Tcl_DecrRefCount(activeTclHandlerSet->attlistDeclCommand);
	}

	activeTclHandlerSet->attlistDeclCommand = objPtr[1];
	Tcl_IncrRefCount(activeTclHandlerSet->attlistDeclCommand);

	break;

      case EXPAT_STARTDOCTYPEDECLCMD:      /* -startdoctypedeclcommand */
	/* ericm@scriptics.com */
        CheckDefaultTclHandlerSet;
	if (activeTclHandlerSet->startDoctypeDeclCommand != NULL) {
	  Tcl_DecrRefCount(activeTclHandlerSet->startDoctypeDeclCommand);
	}

	activeTclHandlerSet->startDoctypeDeclCommand = objPtr[1];
	Tcl_IncrRefCount(activeTclHandlerSet->startDoctypeDeclCommand);

	break;

      case EXPAT_ENDDOCTYPEDECLCMD:      /* -enddoctypedeclcommand */
	/* ericm@scriptics.com */
        CheckDefaultTclHandlerSet;
	if (activeTclHandlerSet->endDoctypeDeclCommand != NULL) {
	  Tcl_DecrRefCount(activeTclHandlerSet->endDoctypeDeclCommand);
	}

	activeTclHandlerSet->endDoctypeDeclCommand = objPtr[1];
	Tcl_IncrRefCount(activeTclHandlerSet->endDoctypeDeclCommand);

	break;

    case EXPAT_XMLDECLCMD:               /* -xmlDeclCommand */
        CheckDefaultTclHandlerSet;
        if (activeTclHandlerSet->xmlDeclCommand != NULL) {
            Tcl_DecrRefCount (activeTclHandlerSet->xmlDeclCommand);
        }

        activeTclHandlerSet->xmlDeclCommand = objPtr[1];
        Tcl_IncrRefCount (activeTclHandlerSet->xmlDeclCommand);

        break;

      case EXPAT_ENTITYDECLCOMMAND: /* -entitydeclcommand */
          CheckDefaultTclHandlerSet;
          if (activeTclHandlerSet->entityDeclCommand != NULL) {
              Tcl_DecrRefCount (activeTclHandlerSet->entityDeclCommand);
          }

          activeTclHandlerSet->entityDeclCommand = objPtr[1];
          Tcl_IncrRefCount (activeTclHandlerSet->entityDeclCommand);

          break;

      case EXPAT_PARAMENTITYPARSING: /* -paramentityparsing */
	  /* ericm@scriptics */
	  if (Tcl_GetIndexFromObj(interp, objPtr[1], paramEntityParsingValues,
		  "value", 0, &value) != TCL_OK) {
              return TCL_ERROR;
	  }
	  switch ((enum paramEntityParsingValues) value) {
	      case EXPAT_PARAMENTITYPARSINGALWAYS:
		  XML_SetParamEntityParsing(expat->parser,
			  XML_PARAM_ENTITY_PARSING_ALWAYS);
		  break;
	      case EXPAT_PARAMENTITYPARSINGNEVER:
		  XML_SetParamEntityParsing(expat->parser,
			  XML_PARAM_ENTITY_PARSING_NEVER);
		  break;
	      case EXPAT_PARAMENTITYPARSINGNOTSTANDALONE:
		  XML_SetParamEntityParsing(expat->parser,
			  XML_PARAM_ENTITY_PARSING_UNLESS_STANDALONE);
		  break;
	  }
	  break;

    case EXPAT_HANDLERSET:
        if ((handlerSetName = Tcl_GetStringFromObj (objPtr[1], NULL)) == NULL) {
            return TCL_ERROR;
        }
        activeTclHandlerSet = expat->firstTclHandlerSet;
        while (activeTclHandlerSet) {
            if (strcmp (handlerSetName, activeTclHandlerSet->name) == 0) {
                break;
            }
            activeTclHandlerSet = activeTclHandlerSet->nextHandlerSet;
        }
        if (!activeTclHandlerSet) {
            activeTclHandlerSet = CreateTclHandlerSet (handlerSetName);
            if (!expat->firstTclHandlerSet) {
                expat->firstTclHandlerSet = activeTclHandlerSet;
            }
            else {
                tmpTclHandlerSet = expat->firstTclHandlerSet;
                while (tmpTclHandlerSet->nextHandlerSet) {
                    tmpTclHandlerSet = tmpTclHandlerSet->nextHandlerSet;
                }
                tmpTclHandlerSet->nextHandlerSet = activeTclHandlerSet;
            }
        }
        break;

    case EXPAT_NOEXPAND:
        DBG(fprintf (stderr,"start noexpand\n");)
        if (Tcl_GetBooleanFromObj (interp, objv[1], &bool) != TCL_OK) {
            DBG(fprintf (stderr,"no boolean arg\n");)
            return TCL_ERROR;
        }
        if (bool) {
            XML_SetDefaultHandler(expat->parser,
                        (XML_DefaultHandler) TclGenExpatDefaultHandler);
        }
        else {
            XML_SetDefaultHandlerExpand(expat->parser,
                                        (XML_DefaultHandler) TclGenExpatDefaultHandler);
        }
        break;

    default:

        return TCL_ERROR;
        break;

    }

    objPtr += 2;
    objc -= 2;

  }

  return TCL_OK;

/*    if (doParse) { */
/*      return TclExpatParse(interp, expat, "", 0); */
/*    } else { */
/*      return TCL_OK; */
/*    } */
}

/*
 *----------------------------------------------------------------------------
 *
 * TclExpatCget --
 *
 *	Returns setting of configuration option.
 *	Not yet implemented.
 *
 * Results:
 *	Option value.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------------
 */

static int
TclExpatCget (interp, expat, objc, objv)
     Tcl_Interp *interp;
     TclGenExpatInfo *expat;
     int objc;
     Tcl_Obj *CONST objv[];
{
  Tcl_SetResult(interp, "method not implemented", NULL);
  return TCL_ERROR;
}

/*
 *----------------------------------------------------------------------------
 *
 * TclExpatGet --
 *
 *	Returns runtime parser information, depending on option
 *      ericm@scriptics.com, 1999.6.28
 *
 * Results:
 *	Option value.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------------
 */
static int
TclExpatGet (interp, expat, objc, objv)
     Tcl_Interp *interp;
     TclGenExpatInfo *expat;
     int objc;
     Tcl_Obj *CONST objv[];
{
  static CONST84 char *getSwitches[] = {
    "-specifiedattributecount",
    "-currentbytecount",
    "-currentlinenumber",
    "-currentcolumnnumber",
    "-currentbyteindex",
    (char *) NULL
  };
  enum getSwitch {
    EXPAT_SPECIFIEDATTRCOUNT,
    EXPAT_CURRENTBYTECOUNT,
    EXPAT_CURRENTLINENUMBER,
    EXPAT_CURRENTCOLUMNNUMBER,
    EXPAT_CURRENTBYTEINDEX
  };
  int switchIndex;
  Tcl_Obj *resultPtr;

  if (objc > 1) {
    Tcl_SetResult(interp, "Only one value may be requested at a time",
		  TCL_STATIC);
    return TCL_ERROR;
  }

  if (Tcl_GetIndexFromObj(interp, objv[0], getSwitches,
			  "switch", 0, &switchIndex) != TCL_OK) {
    return TCL_ERROR;
  }
  resultPtr = Tcl_GetObjResult(interp);

  switch ((enum getSwitch) switchIndex) {

    case EXPAT_SPECIFIEDATTRCOUNT:

      Tcl_SetIntObj(resultPtr, XML_GetSpecifiedAttributeCount(expat->parser));
      break;

    case EXPAT_CURRENTBYTECOUNT:

      Tcl_SetIntObj(resultPtr, XML_GetCurrentByteCount(expat->parser));
      break;

    case EXPAT_CURRENTLINENUMBER:

      Tcl_SetIntObj(resultPtr, XML_GetCurrentLineNumber(expat->parser));
      break;

    case EXPAT_CURRENTCOLUMNNUMBER:

      Tcl_SetIntObj(resultPtr, XML_GetCurrentColumnNumber(expat->parser));
      break;

    case EXPAT_CURRENTBYTEINDEX:

      Tcl_SetLongObj(resultPtr, XML_GetCurrentByteIndex(expat->parser));
      break;

    default:

      return TCL_ERROR;
  }

  return TCL_OK;
}


/*
 *----------------------------------------------------------------------------
 *
 * TclExpatHandlerResult --
 *
 *	Manage the result of the application callback.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	Further invocation of callback scripts may be inhibited.
 *
 *----------------------------------------------------------------------------
 */

static void
TclExpatHandlerResult(expat, handlerSet, result)
     TclGenExpatInfo *expat;
     TclHandlerSet *handlerSet;
     int result;
{
  switch (result) {
    case TCL_OK:
      handlerSet->status = TCL_OK;
      break;

    case TCL_CONTINUE:
      /*
       * Skip callbacks until the matching end element event
       * occurs for the currently open element.
       * Keep a reference count to handle nested
       * elements.
       */
      handlerSet->status = TCL_CONTINUE;
      handlerSet->continueCount = 1;
      break;

    case TCL_BREAK:
      /*
       * Skip all further callbacks, but return OK.
       */
      handlerSet->status = TCL_BREAK;
      break;

    case TCL_ERROR:
      /*
       * Skip all further callbacks, and return error.
       */
      expat->status = TCL_ERROR;
      expat->result = Tcl_GetObjResult(expat->interp);
      Tcl_IncrRefCount(expat->result);
    default:
      /*
       * Skip all further callbacks, set return value and return error.
       */
      expat->status = result;
      expat->result = Tcl_GetObjResult(expat->interp);
      Tcl_IncrRefCount(expat->result);
      break;
  }
}

/*
 *----------------------------------------------------------------------------
 *
 * TclGenExpatElementStartHandler --
 *
 *	Called by expat for each start tag.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	Callback scripts are invoked.
 *
 *----------------------------------------------------------------------------
 */

static void
TclGenExpatElementStartHandler(userData, name, atts)
     void *userData;
     const char *name;
     const char **atts;
{
  TclGenExpatInfo *expat = (TclGenExpatInfo *) userData;
  Tcl_Obj *atList = NULL;
  const char **atPtr;
  int result;
  Tcl_Obj *vector[3];
  TclHandlerSet *activeTclHandlerSet;
  CHandlerSet *activeCHandlerSet;

  if (expat->status != TCL_OK) {
      return;
  }

  activeTclHandlerSet = expat->firstTclHandlerSet;
  while (activeTclHandlerSet) {
      switch (activeTclHandlerSet->status) {
      case TCL_CONTINUE :
          /*
           * We're currently skipping elements looking for the
           * close of the continued element.
           */

          activeTclHandlerSet->continueCount++;
          goto nextTcl;
          break;
      case TCL_BREAK:
          goto nextTcl;
          break;
      default:
          ;
      }

      if (activeTclHandlerSet->status == TCL_CONTINUE) {
      }

      if (activeTclHandlerSet->elementstartcommand == NULL) {
          goto nextTcl;
      }

      /*
       * Convert the attribute list into a Tcl key-value paired list.
       */

      if (atList == NULL) {
          atList = Tcl_NewListObj(0, NULL);
          Tcl_IncrRefCount (atList);
          for (atPtr = atts; atPtr[0] && atPtr[1]; atPtr += 2) {
              Tcl_ListObjAppendElement(expat->interp, atList, Tcl_NewStringObj((char *)atPtr[0], strlen(atPtr[0])));
              Tcl_ListObjAppendElement(expat->interp, atList, Tcl_NewStringObj((char *)atPtr[1], strlen(atPtr[1])));
          }
          vector[2] = atList;
      }

      vector[0] = activeTclHandlerSet->elementstartcommand;
      Tcl_IncrRefCount (vector[0]);
      vector[1] = Tcl_NewStringObj((char *)name, -1);
      Tcl_IncrRefCount (vector[1]);
      result = activeTclHandlerSet->elementstartObjProc(activeTclHandlerSet->elementstartclientData, expat->interp, 3, vector);
      TclExpatHandlerResult(expat, activeTclHandlerSet, result);
      Tcl_DecrRefCount (vector[0]);
      Tcl_DecrRefCount (vector[1]);
  nextTcl:
      activeTclHandlerSet = activeTclHandlerSet->nextHandlerSet;
  }
  if (atList) {
      Tcl_DecrRefCount (atList);
  }
  
  activeCHandlerSet = expat->firstCHandlerSet;
  while (activeCHandlerSet) {
      if (activeCHandlerSet->elementstartcommand) {
          activeCHandlerSet->elementstartcommand (activeCHandlerSet->userData,
                                                  name, atts);
      }
      activeCHandlerSet = activeCHandlerSet->nextHandlerSet;
  }

  return;
}


/*
 *----------------------------------------------------------------------------
 *
 * TclGenExpatElementEndHandler --
 *
 *	Called by expat for each end tag.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	Callback scripts are invoked.
 *
 *----------------------------------------------------------------------------
 */

static void
TclGenExpatElementEndHandler(userData, name)
     void *userData;
     CONST char *name;
{
  TclGenExpatInfo *expat = (TclGenExpatInfo *) userData;
  int result;
  Tcl_Obj *vector[2], *ename = NULL;
  TclHandlerSet *activeTclHandlerSet;
  CHandlerSet *activeCHandlerSet;

  if (expat->status != TCL_OK) {
      return;
  }

  activeTclHandlerSet = expat->firstTclHandlerSet;
  while (activeTclHandlerSet) {
      switch (activeTclHandlerSet->status) {
      case TCL_CONTINUE:
          /*
           * We're currently skipping elements looking for the
           * end of the currently open element.
           */

          if (!--(activeTclHandlerSet->continueCount)) {
              activeTclHandlerSet->status = TCL_OK;
              goto nextTcl;
          }
          break;
      case TCL_BREAK:
          goto nextTcl;
          break;
      default:
          ;
      }

      if (activeTclHandlerSet->elementendcommand == NULL) {
          goto nextTcl;
      }

      if (ename == NULL) {
          ename = Tcl_NewStringObj ((char *)name, -1);
          Tcl_IncrRefCount (ename);
      } else {
          Tcl_SetStringObj (ename, (char *)name, -1);
      }
      vector[0] = activeTclHandlerSet->elementendcommand;
      vector[1] = ename;
      Tcl_Preserve((ClientData) expat->interp);
      result = activeTclHandlerSet->elementendObjProc(activeTclHandlerSet->elementendclientData, expat->interp, 2, vector);
      Tcl_Release((ClientData) expat->interp);
      TclExpatHandlerResult(expat, activeTclHandlerSet, result);
  nextTcl:
      activeTclHandlerSet = activeTclHandlerSet->nextHandlerSet;
  }
  if (ename) {
      Tcl_DecrRefCount (ename);
  }

  activeCHandlerSet = expat->firstCHandlerSet;
  while (activeCHandlerSet) {
      if (activeCHandlerSet->elementendcommand ) {
          activeCHandlerSet->elementendcommand (activeCHandlerSet->userData,
                                                name);
      }
      activeCHandlerSet = activeCHandlerSet->nextHandlerSet;
  }

  return;
}


/*
 *----------------------------------------------------------------------------
 *
 * TclGenExpatStartNamespaceDeclHandler --
 *
 *	Called by expat for each start tag.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	Callback scripts are invoked.
 *
 *----------------------------------------------------------------------------
 */

static void
TclGenExpatStartNamespaceDeclHandler(userData, prefix, uri)
     void       *userData;
     const char *prefix;
     const char *uri;
{
  TclGenExpatInfo *expat = (TclGenExpatInfo *) userData;
  Tcl_Obj      *cmdPtr;
  int           result;
  TclHandlerSet *activeTclHandlerSet;
  CHandlerSet *activeCHandlerSet;

  if (expat->status != TCL_OK) {
      return;
  }

  activeTclHandlerSet = expat->firstTclHandlerSet;
  while (activeTclHandlerSet) {

      switch (activeTclHandlerSet->status) {
      case TCL_CONTINUE:
          /*
           * We're currently skipping elements looking for the
           * close of the continued element.
           */

          activeTclHandlerSet->continueCount++;
          goto nextTcl;
          break;
      case TCL_BREAK:
          goto nextTcl;
          break;
      default:
          ;
      }

      if (activeTclHandlerSet->startnsdeclcommand == NULL) {
          goto nextTcl;
      }

      /*
       * Take a copy of the callback script so that arguments may be appended.
       */

      cmdPtr = Tcl_DuplicateObj(activeTclHandlerSet->startnsdeclcommand);
      Tcl_IncrRefCount(cmdPtr);
      Tcl_Preserve((ClientData) expat->interp);

      Tcl_ListObjAppendElement(expat->interp, cmdPtr,
                               Tcl_NewStringObj((char *)prefix, -1));
      Tcl_ListObjAppendElement(expat->interp, cmdPtr,
                               Tcl_NewStringObj((char *)uri,    -1));

      /*
       * It would be desirable to be able to terminate parsing
       * if the return result is TCL_ERROR or TCL_BREAK.
       */
#if (TCL_MAJOR_VERSION == 8)
      result = Tcl_GlobalEvalObj(expat->interp, cmdPtr);
#else
      result = Tcl_EvalObj(expat->interp, cmdPtr, TCL_EVAL_GLOBAL);
#endif /* TCL_MAJOR_VERSION == 8 */

      Tcl_DecrRefCount(cmdPtr);
      Tcl_Release((ClientData) expat->interp);

      TclExpatHandlerResult(expat, activeTclHandlerSet, result);
  nextTcl:
      activeTclHandlerSet = activeTclHandlerSet->nextHandlerSet;
  }

  activeCHandlerSet = expat->firstCHandlerSet;
  while (activeCHandlerSet) {
      if (activeCHandlerSet->startnsdeclcommand) {
          activeCHandlerSet->startnsdeclcommand (activeCHandlerSet->userData,
                                                 prefix, uri);
      }
      activeCHandlerSet = activeCHandlerSet->nextHandlerSet;
  }

  return;
}

/*
 *----------------------------------------------------------------------------
 *
 * TclGenExpatEndNamespaceDeclHandler --
 *
 *	Called by expat for each end tag.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	Callback scripts are invoked.
 *
 *----------------------------------------------------------------------------
 */

static void
TclGenExpatEndNamespaceDeclHandler(userData, prefix)
     void       *userData;
     CONST char *prefix;
{
  TclGenExpatInfo *expat = (TclGenExpatInfo *) userData;
  Tcl_Obj *cmdPtr;
  int result;
  TclHandlerSet *activeTclHandlerSet;
  CHandlerSet *activeCHandlerSet;

  if (expat->status != TCL_OK) {
      return;
  }

  activeTclHandlerSet = expat->firstTclHandlerSet;
  while (activeTclHandlerSet) {

      switch (activeTclHandlerSet->status) {
      case TCL_CONTINUE:
          /*
           * We're currently skipping elements looking for the
           * end of the currently open element.
           */

          if (!--(activeTclHandlerSet->continueCount)) {
              activeTclHandlerSet->status = TCL_OK;
          }
          goto nextTcl;
          break;
      case TCL_BREAK:
          goto nextTcl;
          break;
      default:
          ;
      }

      if (activeTclHandlerSet->endnsdeclcommand == NULL) {
          goto nextTcl;
      }

      /*
       * Take a copy of the callback script so that arguments may be appended.
       */

      cmdPtr = Tcl_DuplicateObj(activeTclHandlerSet->endnsdeclcommand);
      Tcl_IncrRefCount(cmdPtr);
      Tcl_Preserve((ClientData) expat->interp);

      Tcl_ListObjAppendElement(expat->interp, cmdPtr, Tcl_NewStringObj((char *)prefix, -1));

      /*
       * It would be desirable to be able to terminate parsing
       * if the return result is TCL_ERROR or TCL_BREAK.
       */
#if (TCL_MAJOR_VERSION == 8)
      result = Tcl_GlobalEvalObj(expat->interp, cmdPtr);
#else
      result = Tcl_EvalObj(expat->interp, cmdPtr, TCL_EVAL_GLOBAL);
#endif /* if TCL_MAJOR_VERSION == 8 */

      Tcl_DecrRefCount(cmdPtr);
      Tcl_Release((ClientData) expat->interp);

      TclExpatHandlerResult(expat, activeTclHandlerSet, result);
  nextTcl:
      activeTclHandlerSet = activeTclHandlerSet->nextHandlerSet;
  }
  activeCHandlerSet = expat->firstCHandlerSet;
  while (activeCHandlerSet) {
      if (activeCHandlerSet->endnsdeclcommand) {
          activeCHandlerSet->endnsdeclcommand (activeCHandlerSet->userData,
                                               prefix);
      }
      activeCHandlerSet = activeCHandlerSet->nextHandlerSet;
  }

  return;
}


/*
 *----------------------------------------------------------------------------
 *
 * TclExpatCheckWhiteData --
 *
 *	Called by expat for character data.
 *
 * Results:
 *	1 if string contains just white characters
 *
 *----------------------------------------------------------------------------
 */

static int
TclExpatCheckWhiteData (pc, len)
     char         *pc;
     int           len;
{
    for (; len > 0; len--, pc++) {
        if ( (*pc != ' ')  &&
             (*pc != '\t') &&
             (*pc != '\n') &&
             (*pc != '\r') ) {
            return 0;
        }
    }
    return 1;
}


/*
 *----------------------------------------------------------------------------
 *
 * TclGenExpatCharacterDataHandler --
 *
 *	Called by expat for character data.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	Callback script is invoked.
 *
 *----------------------------------------------------------------------------
 */

static void
TclGenExpatCharacterDataHandler(userData, s, len)
     void *userData;
     CONST char *s;
     int len;
{
  TclGenExpatInfo *expat = (TclGenExpatInfo *) userData;
  int result, onlyWhiteSpace = 0;
  Tcl_Obj *vector[2];
  TclHandlerSet *activeTclHandlerSet;
  CHandlerSet *activeCHandlerSet;

  if (expat->status != TCL_OK) {
      return;
  }

  if (expat->needWSCheck) {
      onlyWhiteSpace = TclExpatCheckWhiteData ((char*)s, len);
  }

  activeTclHandlerSet = expat->firstTclHandlerSet;
  while (activeTclHandlerSet) {

      switch (activeTclHandlerSet->status) {
      case TCL_CONTINUE:
      case TCL_BREAK:
          goto nextTcl;
          break;
      default:
          ;
      }

      if (activeTclHandlerSet->datacommand == NULL) {
          goto nextTcl;
      }

      /*
       * Check whether we are in 'trim' mode
       */
      if (activeTclHandlerSet->ignoreWhiteCDATAs && onlyWhiteSpace) {
          goto nextTcl;
      }

      vector[0] = activeTclHandlerSet->datacommand;
      vector[1] = Tcl_NewStringObj ((char *)s, len);
      Tcl_Preserve((ClientData) expat->interp);
      result = activeTclHandlerSet->datacommandObjProc(activeTclHandlerSet->datacommandclientData, expat->interp, 2, vector);
      Tcl_Release((ClientData) expat->interp);

      TclExpatHandlerResult(expat, activeTclHandlerSet, result);
  nextTcl:
      activeTclHandlerSet = activeTclHandlerSet->nextHandlerSet;
  }

  activeCHandlerSet = expat->firstCHandlerSet;
  while (activeCHandlerSet) {
      if (activeCHandlerSet->datacommand) {
          activeCHandlerSet->datacommand (activeCHandlerSet->userData,
                                          s, len);
      }
      activeCHandlerSet = activeCHandlerSet->nextHandlerSet;
  }
  return;
}



/*
 *----------------------------------------------------------------------------
 *
 * TclExpatDispatchPCDATA --
 *
 *	Called to check whether any accumulated character data
 *	exists, and if so invoke the callback.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	Callback script evaluated.
 *
 *----------------------------------------------------------------------------
 */

/* de: HAS TO BE FINISHED */
static void
TclExpatDispatchPCDATA(expat)
     TclGenExpatInfo *expat;
{
  Tcl_Obj *cmdPtr;
  char *cd;
  int result, len, onlyWhiteSpace;
  TclHandlerSet *activeTclHandlerSet;

  if (expat->cdata       == NULL ||
      expat->status      != TCL_OK
  ) {
    return;
  }

  /*
   * Optionally ignore white-space-only PCDATA
   */
  cd = Tcl_GetStringFromObj(expat->cdata, &len);
  onlyWhiteSpace = TclExpatCheckWhiteData (cd, len);

  activeTclHandlerSet = expat->firstTclHandlerSet;
  while (activeTclHandlerSet) {
      /*
       * Take a copy of the callback script so that arguments may be appended.
       */

      cmdPtr = Tcl_DuplicateObj(activeTclHandlerSet->datacommand);
      Tcl_IncrRefCount(cmdPtr);
      Tcl_Preserve((ClientData) expat->interp);

      if (Tcl_ListObjAppendElement(expat->interp, cmdPtr, expat->cdata) != TCL_OK) {
          expat->status = TCL_ERROR;
          return;
      }

      /*
       * It would be desirable to be able to terminate parsing
       * if the return result is TCL_ERROR or TCL_BREAK.
       */
      result = Tcl_GlobalEvalObj(expat->interp, cmdPtr);

      Tcl_DecrRefCount(cmdPtr);
      Tcl_Release((ClientData) expat->interp);

      TclExpatHandlerResult(expat, activeTclHandlerSet, result);
      activeTclHandlerSet = activeTclHandlerSet->nextHandlerSet;
  }
  return;
}


/*
 *----------------------------------------------------------------------------
 *
 * TclGenExpatProcessingInstructionHandler --
 *
 *	Called by expat for processing instructions.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	Callback scripts are invoked.
 *
 *----------------------------------------------------------------------------
 */

static void
TclGenExpatProcessingInstructionHandler(userData, target, data)
     void *userData;
     CONST char *target;
     CONST char *data;
{
  TclGenExpatInfo *expat = (TclGenExpatInfo *) userData;
  Tcl_Obj *cmdPtr;
  int result;
  TclHandlerSet *activeTclHandlerSet;
  CHandlerSet *activeCHandlerSet;

  TclExpatDispatchPCDATA(expat);

  if (expat->status != TCL_OK) {
      return;
  }

  activeTclHandlerSet = expat->firstTclHandlerSet;
  while (activeTclHandlerSet) {

      switch (activeTclHandlerSet->status) {
      case TCL_CONTINUE:
      case TCL_BREAK:
          goto nextTcl;
          break;
      default:
          ;
      }

      if (activeTclHandlerSet->picommand == NULL) {
          goto nextTcl;
      }

      /*
       * Take a copy of the callback script so that arguments may be appended.
       */

      cmdPtr = Tcl_DuplicateObj(activeTclHandlerSet->picommand);
      Tcl_IncrRefCount(cmdPtr);
      Tcl_Preserve((ClientData) expat->interp);

      Tcl_ListObjAppendElement(expat->interp, cmdPtr, Tcl_NewStringObj((char *)target, strlen(target)));
      Tcl_ListObjAppendElement(expat->interp, cmdPtr, Tcl_NewStringObj((char *)data, strlen(data)));

      /*
       * It would be desirable to be able to terminate parsing
       * if the return result is TCL_ERROR or TCL_BREAK.
       */
#if (TCL_MAJOR_VERSION == 8)
      result = Tcl_GlobalEvalObj(expat->interp, cmdPtr);
#else
      result = Tcl_EvalObj(expat->interp, cmdPtr, TCL_EVAL_GLOBAL);
#endif /* if TCL_MAJOR_VERSION == 8 */

      Tcl_DecrRefCount(cmdPtr);
      Tcl_Release((ClientData) expat->interp);

      TclExpatHandlerResult(expat, activeTclHandlerSet, result);
  nextTcl:
      activeTclHandlerSet = activeTclHandlerSet->nextHandlerSet;
  }

  activeCHandlerSet = expat->firstCHandlerSet;
  while (activeCHandlerSet) {
      if (activeCHandlerSet->picommand) {
          activeCHandlerSet->picommand (activeCHandlerSet->userData,
                                        target, data);
      }
      activeCHandlerSet = activeCHandlerSet->nextHandlerSet;
  }
  return;
}

/*
 *----------------------------------------------------------------------------
 *
 * TclGenExpatDefaultHandler --
 *
 *	Called by expat for processing data which has no other handler.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	Callback scripts are invoked.
 *
 *----------------------------------------------------------------------------
 */

static void
TclGenExpatDefaultHandler(userData, s, len)
     void *userData;
     CONST char *s;
     int len;
{
  TclGenExpatInfo *expat = (TclGenExpatInfo *) userData;
  Tcl_Obj *cmdPtr;
  int result;
  TclHandlerSet *activeTclHandlerSet;
  CHandlerSet *activeCHandlerSet;

  TclExpatDispatchPCDATA(expat);

  if (expat->status != TCL_OK) {
      return;
  }

  activeTclHandlerSet = expat->firstTclHandlerSet;
  while (activeTclHandlerSet) {

      switch (activeTclHandlerSet->status) {
      case TCL_CONTINUE:
      case TCL_BREAK:
          goto nextTcl;
          break;
      default:
          ;
      }

      if (activeTclHandlerSet->defaultcommand == NULL) {
          goto nextTcl;
      }

      /*
       * Take a copy of the callback script so that arguments may be appended.
       */

      cmdPtr = Tcl_DuplicateObj(activeTclHandlerSet->defaultcommand);
      Tcl_IncrRefCount(cmdPtr);
      Tcl_Preserve((ClientData) expat->interp);

      Tcl_ListObjAppendElement(expat->interp, cmdPtr, Tcl_NewStringObj((char *)s, len));

      /*
       * It would be desirable to be able to terminate parsing
       * if the return result is TCL_ERROR or TCL_BREAK.
       */
#if (TCL_MAJOR_VERSION == 8)
      result = Tcl_GlobalEvalObj(expat->interp, cmdPtr);
#else
      result = Tcl_EvalObj(expat->interp, cmdPtr, TCL_EVAL_GLOBAL);
#endif /* if TCL_MAJOR_VERSION == 8 */

      Tcl_DecrRefCount(cmdPtr);
      Tcl_Release((ClientData) expat->interp);

      TclExpatHandlerResult(expat, activeTclHandlerSet, result);
  nextTcl:
      activeTclHandlerSet = activeTclHandlerSet->nextHandlerSet;
  }

  activeCHandlerSet = expat->firstCHandlerSet;
  while (activeCHandlerSet) {
      if (activeCHandlerSet->defaultcommand) {
          activeCHandlerSet->defaultcommand (activeCHandlerSet->userData,
                                             s, len);
      }
      activeCHandlerSet = activeCHandlerSet->nextHandlerSet;
  }

  return;
}

/*
 *----------------------------------------------------------------------------
 *
 * TclGenExpatEntityDeclHandler --
 *
 *	Called by expat for processing an unparsed entity references.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	Callback scripts are invoked.
 *
 *----------------------------------------------------------------------------
 */

static void
TclGenExpatEntityDeclHandler(userData, entityname, is_param, value, length, base, systemId, publicId, notationName)
     void *userData;
     CONST char *entityname;
     int         is_param;
     CONST char *value;
     int         length;
     CONST char *base;
     CONST char *systemId;
     CONST char *publicId;
     CONST char *notationName;
{
  TclGenExpatInfo *expat = (TclGenExpatInfo *) userData;
  Tcl_Obj *cmdPtr;
  int result;
  TclHandlerSet *activeTclHandlerSet;
  CHandlerSet *activeCHandlerSet;

  TclExpatDispatchPCDATA(expat);

  if (expat->status != TCL_OK) {
      return;
  }

  activeTclHandlerSet = expat->firstTclHandlerSet;
  while (activeTclHandlerSet) {

      switch (activeTclHandlerSet->status) {
      case TCL_CONTINUE:
      case TCL_BREAK:
          goto nextTcl;
          break;
      default:
          ;
      }

      if (activeTclHandlerSet->entityDeclCommand == NULL) {
          goto nextTcl;
      }

      /*
       * Take a copy of the callback script so that arguments may be appended.
       */

      cmdPtr = Tcl_DuplicateObj(activeTclHandlerSet->entityDeclCommand);
      Tcl_IncrRefCount(cmdPtr);
      Tcl_Preserve((ClientData) expat->interp);

      Tcl_ListObjAppendElement(expat->interp, cmdPtr, Tcl_NewStringObj((char *)entityname, strlen(entityname)));
      Tcl_ListObjAppendElement (expat->interp, cmdPtr, Tcl_NewIntObj (is_param));
      if (value == NULL) {
          Tcl_ListObjAppendElement (expat->interp, cmdPtr, Tcl_NewListObj (0, NULL));
      }
      else {
          Tcl_ListObjAppendElement (expat->interp, cmdPtr, Tcl_NewStringObj ((char *) value, length));
      }
      if (base == NULL) {
          Tcl_ListObjAppendElement (expat->interp, cmdPtr, Tcl_NewListObj (0, NULL));
      }
      else {
          Tcl_ListObjAppendElement(expat->interp, cmdPtr, Tcl_NewStringObj((char *)base, strlen(base)));
      }
      if (systemId == NULL) {
          Tcl_ListObjAppendElement (expat->interp, cmdPtr, Tcl_NewListObj (0, NULL));
      } else {
          Tcl_ListObjAppendElement(expat->interp, cmdPtr, Tcl_NewStringObj((char *)systemId, strlen(systemId)));
      }
      if (publicId == NULL) {
          Tcl_ListObjAppendElement(expat->interp, cmdPtr, Tcl_NewListObj(0, NULL));
      } else {
          Tcl_ListObjAppendElement(expat->interp, cmdPtr, Tcl_NewStringObj((char *)publicId, strlen(publicId)));
      }
      if (notationName == NULL) {
          Tcl_ListObjAppendElement(expat->interp, cmdPtr, Tcl_NewListObj(0, NULL));
      } else {
          Tcl_ListObjAppendElement(expat->interp, cmdPtr, Tcl_NewStringObj((char *)notationName, strlen(notationName)));
      }

      /*
       * It would be desirable to be able to terminate parsing
       * if the return result is TCL_ERROR or TCL_BREAK.
       */
#if (TCL_MAJOR_VERSION == 8)
      result = Tcl_GlobalEvalObj(expat->interp, cmdPtr);
#else
      result = Tcl_EvalObj(expat->interp, cmdPtr, TCL_EVAL_GLOBAL);
#endif /* if TCL_MAJOR_VERSION == 8 */

      Tcl_DecrRefCount(cmdPtr);
      Tcl_Release((ClientData) expat->interp);

      TclExpatHandlerResult(expat, activeTclHandlerSet, result);
  nextTcl:
      activeTclHandlerSet = activeTclHandlerSet->nextHandlerSet;
  }

  activeCHandlerSet = expat->firstCHandlerSet;
  while (activeCHandlerSet) {
      if (activeCHandlerSet->entityDeclCommand) {
          activeCHandlerSet->entityDeclCommand (activeCHandlerSet->userData,
                                                entityname, is_param, value,
                                                length, base, systemId,
                                                publicId, notationName);
      }
      activeCHandlerSet = activeCHandlerSet->nextHandlerSet;
  }
  return;
}

/*
 *----------------------------------------------------------------------------
 *
 * TclGenExpatNotationDeclHandler --
 *
 *	Called by expat for processing a notation declaration.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	Callback scripts are invoked.
 *
 *----------------------------------------------------------------------------
 */

static void
TclGenExpatNotationDeclHandler(userData, notationName, base, systemId, publicId)
     void *userData;
     CONST char *notationName;
     CONST char *base;
     CONST char *systemId;
     CONST char *publicId;
{
  TclGenExpatInfo *expat = (TclGenExpatInfo *) userData;
  Tcl_Obj *cmdPtr;
  int result;
  TclHandlerSet *activeTclHandlerSet;
  CHandlerSet *activeCHandlerSet;

  TclExpatDispatchPCDATA(expat);

  if (expat->status != TCL_OK) {
      return;
  }

  activeTclHandlerSet = expat->firstTclHandlerSet;
  while (activeTclHandlerSet) {

      switch (activeTclHandlerSet->status) {
      case TCL_CONTINUE:
      case TCL_BREAK:
          goto nextTcl;
          break;
      default:
          ;
      }
      if (activeTclHandlerSet->notationcommand == NULL) {
          goto nextTcl;
      }

      /*
       * Take a copy of the callback script so that arguments may be appended.
       */

      cmdPtr = Tcl_DuplicateObj(activeTclHandlerSet->notationcommand);
      Tcl_IncrRefCount(cmdPtr);
      Tcl_Preserve((ClientData) expat->interp);

      Tcl_ListObjAppendElement(expat->interp, cmdPtr, Tcl_NewStringObj((char *)notationName, strlen(notationName)));
      Tcl_ListObjAppendElement(expat->interp, cmdPtr, Tcl_NewStringObj((char *)base, strlen(base)));
      if (systemId == NULL) {
          Tcl_ListObjAppendElement(expat->interp, cmdPtr, Tcl_NewListObj(0, NULL));
      } else {
          Tcl_ListObjAppendElement(expat->interp, cmdPtr, Tcl_NewStringObj((char *)systemId, strlen(systemId)));
      }
      if (publicId == NULL) {
          Tcl_ListObjAppendElement(expat->interp, cmdPtr, Tcl_NewListObj(0, NULL));
      } else {
          Tcl_ListObjAppendElement(expat->interp, cmdPtr, Tcl_NewStringObj((char *)publicId, strlen(publicId)));
      }

      /*
       * It would be desirable to be able to terminate parsing
       * if the return result is TCL_ERROR or TCL_BREAK.
       */
#if (TCL_MAJOR_VERSION == 8)
      result = Tcl_GlobalEvalObj(expat->interp, cmdPtr);
#else
      result = Tcl_EvalObj(expat->interp, cmdPtr, TCL_EVAL_GLOBAL);
#endif /* if TCL_MAJOR_VERSION == 8 */

      Tcl_DecrRefCount(cmdPtr);
      Tcl_Release((ClientData) expat->interp);

      TclExpatHandlerResult(expat, activeTclHandlerSet, result);
  nextTcl:
      activeTclHandlerSet = activeTclHandlerSet->nextHandlerSet;
  }

  activeCHandlerSet = expat->firstCHandlerSet;
  while (activeCHandlerSet) {
      if (activeCHandlerSet->notationcommand) {
          activeCHandlerSet->notationcommand (activeCHandlerSet->userData,
                                              notationName, base,
                                              systemId, publicId);
      }
      activeCHandlerSet = activeCHandlerSet->nextHandlerSet;
  }
  return;
}

/*
 *----------------------------------------------------------------------------
 *
 * TclGenExpatUnknownEncodingHandler --
 *
 *	Called by expat for processing a reference to a character in an
 *	unknown encoding.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	Callback scripts are invoked.
 *
 *----------------------------------------------------------------------------
 */

static int
TclGenExpatUnknownEncodingHandler(encodingHandlerData, name, info)
     void *encodingHandlerData;
     CONST char *name;
     XML_Encoding *info;
{
  TclGenExpatInfo *expat = (TclGenExpatInfo *) encodingHandlerData;
  CHandlerSet *activeCHandlerSet;

  TclExpatDispatchPCDATA(expat);

  if (expat->status != TCL_OK) {
      return 1;
  }

  if (expat->firstTclHandlerSet) {
      Tcl_SetResult(expat->interp, "not implemented", NULL);
      return 0;
  }

  activeCHandlerSet = expat->firstCHandlerSet;
  while (activeCHandlerSet) {
      if (activeCHandlerSet->unknownencodingcommand) {
          activeCHandlerSet->unknownencodingcommand (activeCHandlerSet->userData,
                                             name, info);
      }
      activeCHandlerSet = activeCHandlerSet->nextHandlerSet;
  }
  return 1;
}

/*
 *----------------------------------------------------------------------------
 *
 * TclGenExpatExternalEntityRefHandler --
 *
 *	Called by expat for processing external entity references.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	Callback scripts are invoked.
 *
 *----------------------------------------------------------------------------
 */
static int
TclGenExpatExternalEntityRefHandler(parser, openEntityNames, base,
	systemId, publicId)
     XML_Parser parser;
     CONST char *openEntityNames;
     CONST char *base;
     CONST char *systemId;
     CONST char *publicId;
{
  TclGenExpatInfo *expat = (TclGenExpatInfo *) XML_GetUserData(parser);
  Tcl_Obj *cmdPtr, *resultObj, *resultTypeObj, *extbaseObj, *dataObj;
  int result, len, mode, done, fd;
  TclHandlerSet *activeTclHandlerSet;
  CHandlerSet *activeCHandlerSet;
  XML_Parser extparser, oldparser = NULL;
  char s[255], buf[8*1024], *dataStr, *resultType, *extbase;
  TclExpat_InputType inputType;
  Tcl_Channel chan = (Tcl_Channel) NULL;


  TclExpatDispatchPCDATA(expat);

  if (expat->status != TCL_OK) {
      return 1;
  }

  activeTclHandlerSet = expat->firstTclHandlerSet;
  while (activeTclHandlerSet) {

      switch (activeTclHandlerSet->status) {
      case TCL_CONTINUE:
      case TCL_BREAK:
          goto nextTcl;
          break;
      default:
          ;
      }
      if (activeTclHandlerSet->externalentitycommand == NULL) {
          goto nextTcl;
      }

      /*
       * Take a copy of the callback script so that arguments may be appended.
       */

      cmdPtr = Tcl_DuplicateObj(activeTclHandlerSet->externalentitycommand);
      Tcl_IncrRefCount(cmdPtr);
      Tcl_Preserve((ClientData) expat->interp);

      if (base) {
          Tcl_ListObjAppendElement(expat->interp, cmdPtr,
	      Tcl_NewStringObj((char *)base, strlen(base)));
      } else {
          Tcl_ListObjAppendElement(expat->interp, cmdPtr,
	      Tcl_NewStringObj("", 0));
      }

      if (systemId) {
          Tcl_ListObjAppendElement(expat->interp, cmdPtr,
              Tcl_NewStringObj((char *)systemId, strlen(systemId)));
      } else {
          Tcl_ListObjAppendElement(expat->interp, cmdPtr,
              Tcl_NewStringObj("", 0));
      }

      if (publicId) {
          Tcl_ListObjAppendElement(expat->interp, cmdPtr,
	      Tcl_NewStringObj((char *)publicId, strlen(publicId)));
      } else {
          Tcl_ListObjAppendElement(expat->interp, cmdPtr,
	      Tcl_NewStringObj("", 0));
      }

      result = Tcl_GlobalEvalObj(expat->interp, cmdPtr);

      Tcl_DecrRefCount(cmdPtr);
      Tcl_Release((ClientData) expat->interp);

      switch (result) {
      case TCL_OK:
          break;
      case TCL_CONTINUE:
          goto nextTcl;
          break;
      case TCL_ERROR:
          TclExpatHandlerResult (expat, activeTclHandlerSet,
                                 ERROR_IN_EXTREFHANDLER);
          return 0;
      default:
          TclExpatHandlerResult (expat, activeTclHandlerSet, result);
          return 0;
      }

      extparser = XML_ExternalEntityParserCreate (parser, openEntityNames, 0);

      resultObj = Tcl_GetObjResult (expat->interp);
      Tcl_IncrRefCount (resultObj);

      result = Tcl_ListObjLength (expat->interp, resultObj, &len);
      if ((result != TCL_OK) || (len != 3)) {
          goto wrongScriptResult;
      }
      result = Tcl_ListObjIndex (expat->interp, resultObj, 0, &resultTypeObj);
      if (result != TCL_OK) {
          goto wrongScriptResult;
      }
      resultType = Tcl_GetStringFromObj (resultTypeObj, NULL);
      if (strcmp (resultType, "string") == 0) {
          inputType = EXPAT_INPUT_STRING;
      } else if (strcmp (resultType, "channel") == 0) {
          inputType = EXPAT_INPUT_CHANNEL;
      } else if (strcmp (resultType, "filename") == 0) {
          inputType = EXPAT_INPUT_FILENAME;
      } else {
          goto wrongScriptResult;
      }

      result = Tcl_ListObjIndex (expat->interp, resultObj, 1, &extbaseObj);
      if (result != TCL_OK) {
          goto wrongScriptResult;
      }
      extbase = Tcl_GetStringFromObj (extbaseObj, NULL);

      if (!extparser) {
          Tcl_DecrRefCount (resultObj);
          Tcl_SetResult (expat->interp,
                         "unable to create expat external entity parser",
                         NULL);
          TclExpatHandlerResult(expat, activeTclHandlerSet,
                                ERROR_IN_EXTREFHANDLER);
          return 0;
      }

      oldparser = expat->parser;
      expat->parser = extparser;
      XML_SetBase (extparser, extbase);

      result = Tcl_ListObjIndex (expat->interp, resultObj, 2, &dataObj);
      if (result != TCL_OK) {
          goto wrongScriptResult;
      }

      activeCHandlerSet = expat->firstCHandlerSet;
      while (activeCHandlerSet) {
          if (activeCHandlerSet->parserResetProc) {
              activeCHandlerSet->parserResetProc (extparser,
                                                  activeCHandlerSet->userData);
          }
          activeCHandlerSet = activeCHandlerSet->nextHandlerSet;
      }

      dataStr = Tcl_GetStringFromObj (dataObj, &len);
      switch (inputType) {
      case EXPAT_INPUT_STRING:
          result = XML_Parse (extparser, dataStr, len, 1);
          break;

      case EXPAT_INPUT_CHANNEL:
          chan = Tcl_GetChannel (expat->interp, dataStr, &mode);
          if (chan == (Tcl_Channel) NULL) {
              goto wrongScriptResult;
          }
          if (!(mode & TCL_READABLE)) {
              Tcl_DecrRefCount (resultObj);
              Tcl_UnregisterChannel (expat->interp, chan);
              Tcl_ResetResult (expat->interp);
              Tcl_AppendResult (expat->interp, "channel \"", dataStr,
                                "\" returned by the externalentitycommand ",
                                "wasn't opened for reading", (char *) NULL);
              TclExpatHandlerResult (expat, activeTclHandlerSet,
                                     ERROR_IN_EXTREFHANDLER);
              XML_ParserFree (extparser);
              expat->parser = oldparser;
              return 0;
          }
          result = 1;
          do {
              len = Tcl_Read (chan, buf, sizeof (buf));
              done = len < sizeof (buf);
              if (!XML_Parse (extparser, buf, len, done)) {
                  result = 0;
                  break;
              }
          } while (!done);
          Tcl_UnregisterChannel (expat->interp, chan);
          break;

      case EXPAT_INPUT_FILENAME:
          fd = open(dataStr, O_BINARY|O_RDONLY);
          if (fd < 0) {
              Tcl_DecrRefCount (resultObj);
              Tcl_ResetResult (expat->interp);
              Tcl_AppendResult (expat->interp, "error opening file \"",
                                dataStr, "\"", (char *) NULL);
              XML_ParserFree (extparser);
              expat->parser = oldparser;
              return 0;
          }
          result = 1;
          for (;;) {
              int nread;
              char *fbuf = XML_GetBuffer (extparser, READ_SIZE);
              if (!fbuf) {
                  close (fd);
                  Tcl_ResetResult (expat->interp);
                  Tcl_SetResult (expat->interp, "Out of memory\n", NULL);
                  return TCL_ERROR;
              }
              nread = read(fd, fbuf, READ_SIZE);
              if (nread < 0) {
                  close (fd);
                  Tcl_ResetResult (expat->interp);
                  Tcl_AppendResult (expat->interp,
                                    "error reading from file \"",
                                    dataStr, "\"", (char *) NULL);
                  return TCL_ERROR;
              }
              if (!XML_ParseBuffer (extparser, nread, nread == 0)) {
                  close (fd);
                  result = 0;
                  break;
              }
              if (nread == 0) {
                  close(fd);
                  break;
              }
          }
          break;
      }

      Tcl_DecrRefCount (resultObj);
      if (!result) {
          Tcl_ResetResult (expat->interp);
          sprintf(s, "%d", XML_GetCurrentLineNumber(extparser));
          Tcl_AppendResult(expat->interp, "Not wellformed error \"",
                           XML_ErrorString(XML_GetErrorCode(extparser)),
                           "\" while parsing external entity: \n\t",
                           systemId, "\nat line ", s, " character ", NULL);
          sprintf(s, "%d", XML_GetCurrentColumnNumber(extparser));
          Tcl_AppendResult(expat->interp, s, NULL);
          XML_ParserFree (extparser);
          expat->parser = oldparser;
          TclExpatHandlerResult(expat, activeTclHandlerSet,
                                ERROR_IN_EXTREFHANDLER);
          return 0;
      }

      XML_ParserFree (extparser);
      expat->parser = oldparser;

      activeCHandlerSet = expat->firstCHandlerSet;
      while (activeCHandlerSet) {
          if (activeCHandlerSet->parserResetProc) {
              activeCHandlerSet->parserResetProc (oldparser,
                                                  activeCHandlerSet->userData);
          }
          activeCHandlerSet = activeCHandlerSet->nextHandlerSet;
      }

      TclExpatHandlerResult(expat, activeTclHandlerSet, TCL_OK);
      return 1;

  nextTcl:
      activeTclHandlerSet = activeTclHandlerSet->nextHandlerSet;
  }

  activeCHandlerSet = expat->firstCHandlerSet;
  while (activeCHandlerSet) {
      if (activeCHandlerSet->externalentitycommand) {
          if (activeCHandlerSet->externalentitycommand (
              activeCHandlerSet->userData, openEntityNames, base, systemId,
              publicId)) {
              return 1;
          }
      }
      activeCHandlerSet = activeCHandlerSet->nextHandlerSet;
  }

  return 0;

 wrongScriptResult:
  Tcl_DecrRefCount (resultObj);
  Tcl_ResetResult (expat->interp);
  XML_ParserFree (extparser);
  if (oldparser) {
      expat->parser = oldparser;
  }
  Tcl_AppendResult (expat->interp, "The -externalentitycommand script has to return a Tcl list with 3 elements.\n",
               "Synatx: {string|channel|filename <baseurl> <data>}\n", NULL);
  TclExpatHandlerResult (expat, activeTclHandlerSet,
                         ERROR_IN_EXTREFHANDLER);
  return 0;
}

/*
 *----------------------------------------------------------------------------
 *
 * TclGenExpatCommentHandler --
 *
 *	Called by expat to handle comments encountered while parsing
 *      Added by ericm@scriptics.com, 1999.6.25.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	Callback scripts are invoked.
 *
 *----------------------------------------------------------------------------
 */
static void
TclGenExpatCommentHandler(userData, data)
    void *userData;
    const char *data;
{
  TclGenExpatInfo *expat = (TclGenExpatInfo *) userData;
  Tcl_Obj *cmdPtr;
  int result;
  TclHandlerSet *activeTclHandlerSet;
  CHandlerSet *activeCHandlerSet;


  TclExpatDispatchPCDATA(expat);

  if (expat->status != TCL_OK) {
      return;
  }

  activeTclHandlerSet = expat->firstTclHandlerSet;
  while (activeTclHandlerSet) {

      switch (activeTclHandlerSet->status) {
      case TCL_CONTINUE:
      case TCL_BREAK:
          goto nextTcl;
          break;
      default:
          ;
      }

      if (activeTclHandlerSet->commentCommand == NULL) {
          goto nextTcl;
      }

      cmdPtr = Tcl_DuplicateObj(activeTclHandlerSet->commentCommand);
      Tcl_IncrRefCount(cmdPtr);
      Tcl_Preserve((ClientData) expat->interp);

      Tcl_ListObjAppendElement(expat->interp, cmdPtr,
                               Tcl_NewStringObj((char *)data, strlen(data)));

      result = Tcl_GlobalEvalObj(expat->interp, cmdPtr);

      Tcl_DecrRefCount(cmdPtr);
      Tcl_Release((ClientData) expat->interp);

      TclExpatHandlerResult(expat, activeTclHandlerSet, result);
  nextTcl:
      activeTclHandlerSet = activeTclHandlerSet->nextHandlerSet;
  }

  activeCHandlerSet = expat->firstCHandlerSet;
  while (activeCHandlerSet) {
      if (activeCHandlerSet->commentCommand) {
          activeCHandlerSet->commentCommand (activeCHandlerSet->userData,
                                             data);
      }
      activeCHandlerSet = activeCHandlerSet->nextHandlerSet;
  }
  return;
}
/*
 *----------------------------------------------------------------------------
 *
 * TclGenExpatNotStandaloneHandler --
 *
 *	Called by expat to handle "not standalone" documents (ie, documents
 *      that have an external subset or a reference to a parameter entity,
 *      but do not have standalone="yes")
 *      Added by ericm@scriptics.com, 1999.6.25.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	Callback scripts are invoked.
 *
 *----------------------------------------------------------------------------
 */
static int
TclGenExpatNotStandaloneHandler(userData)
    void *userData;
{
  TclGenExpatInfo *expat = (TclGenExpatInfo *) userData;
  Tcl_Obj *cmdPtr;
  int result;
  TclHandlerSet *activeTclHandlerSet;
  CHandlerSet *activeCHandlerSet;

  TclExpatDispatchPCDATA(expat);

  if (expat->status != TCL_OK) {
      return 1;
  }

  activeTclHandlerSet = expat->firstTclHandlerSet;
  while (activeTclHandlerSet) {

      switch (activeTclHandlerSet->status) {
      case TCL_CONTINUE:
      case TCL_BREAK:
          goto nextTcl;
          break;
      default:
          ;
      }

      if (activeTclHandlerSet->notStandaloneCommand == NULL) {
          goto nextTcl;
      }

      cmdPtr = Tcl_DuplicateObj(activeTclHandlerSet->notStandaloneCommand);
      Tcl_IncrRefCount(cmdPtr);
      Tcl_Preserve((ClientData) expat->interp);

      result = Tcl_GlobalEvalObj(expat->interp, cmdPtr);

      Tcl_DecrRefCount(cmdPtr);
      Tcl_Release((ClientData) expat->interp);

      TclExpatHandlerResult(expat, activeTclHandlerSet, result);
  nextTcl:
      activeTclHandlerSet = activeTclHandlerSet->nextHandlerSet;
  }

  activeCHandlerSet = expat->firstCHandlerSet;
  while (activeCHandlerSet) {
      if (activeCHandlerSet->notStandaloneCommand) {
          activeCHandlerSet->notStandaloneCommand (activeCHandlerSet->userData);
      }
      activeCHandlerSet = activeCHandlerSet->nextHandlerSet;
  }
  return 1;
}

/*
 *----------------------------------------------------------------------------
 *
 * TclGenExpatStartCdataSectionHandler --
 *
 *	Called by expat to handle CDATA section starts.
 *      Added by ericm@scriptics.com, 1999.6.25.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	Callback scripts are invoked.
 *
 *----------------------------------------------------------------------------
 */
static void
TclGenExpatStartCdataSectionHandler(userData)
    void *userData;
{
  TclGenExpatInfo *expat = (TclGenExpatInfo *) userData;
  Tcl_Obj *cmdPtr;
  int result;
  TclHandlerSet *activeTclHandlerSet;
  CHandlerSet *activeCHandlerSet;

  TclExpatDispatchPCDATA(expat);

  if (expat->status != TCL_OK) {
      return;
  }

  activeTclHandlerSet = expat->firstTclHandlerSet;
  while (activeTclHandlerSet) {

      switch (activeTclHandlerSet->status) {
      case TCL_CONTINUE:
          /* Currently skipping elements; CDATA Start and End must be
           * inside an element content, so we don't have to fiddle
           * around with continue counting and just go throw. */
      case TCL_BREAK:
          goto nextTcl;
          break;
      default:
          ;
      }

      if (activeTclHandlerSet->startCdataSectionCommand == NULL) {
          goto nextTcl;
      }

      cmdPtr = Tcl_DuplicateObj(activeTclHandlerSet->startCdataSectionCommand);
      Tcl_IncrRefCount(cmdPtr);
      Tcl_Preserve((ClientData) expat->interp);

      result = Tcl_GlobalEvalObj(expat->interp, cmdPtr);

      Tcl_DecrRefCount(cmdPtr);
      Tcl_Release((ClientData) expat->interp);

      TclExpatHandlerResult(expat, activeTclHandlerSet, result);
  nextTcl:
      activeTclHandlerSet = activeTclHandlerSet->nextHandlerSet;
  }

  activeCHandlerSet = expat->firstCHandlerSet;
  while (activeCHandlerSet) {
      if (activeCHandlerSet->startCdataSectionCommand) {
          activeCHandlerSet->startCdataSectionCommand (activeCHandlerSet->userData);
      }
      activeCHandlerSet = activeCHandlerSet->nextHandlerSet;
  }
  return;
}

/*
 *----------------------------------------------------------------------------
 *
 * TclGenExpatEndCdataSectionHandler
 *
 *	Called by expat to handle CDATA section ends
 *      Added by ericm@scriptics.com, 1999.6.25.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	Callback scripts are invoked.
 *
 *----------------------------------------------------------------------------
 */
static void
TclGenExpatEndCdataSectionHandler(userData)
    void *userData;
{
  TclGenExpatInfo *expat = (TclGenExpatInfo *) userData;
  Tcl_Obj *cmdPtr;
  int result;
  TclHandlerSet *activeTclHandlerSet;
  CHandlerSet *activeCHandlerSet;

  TclExpatDispatchPCDATA(expat);

  if (expat->status != TCL_OK) {
      return;
  }

  activeTclHandlerSet = expat->firstTclHandlerSet;
  while (activeTclHandlerSet) {

      switch (activeTclHandlerSet->status) {
      case TCL_CONTINUE:
      case TCL_BREAK:
          goto nextTcl;
          break;
      default:
          ;
      }

      if (activeTclHandlerSet->endCdataSectionCommand == NULL) {
          goto nextTcl;
      }

      cmdPtr = Tcl_DuplicateObj(activeTclHandlerSet->endCdataSectionCommand);
      Tcl_IncrRefCount(cmdPtr);
      Tcl_Preserve((ClientData) expat->interp);

      result = Tcl_GlobalEvalObj(expat->interp, cmdPtr);

      Tcl_DecrRefCount(cmdPtr);
      Tcl_Release((ClientData) expat->interp);

      TclExpatHandlerResult(expat, activeTclHandlerSet, result);
  nextTcl:
      activeTclHandlerSet = activeTclHandlerSet->nextHandlerSet;
  }

  activeCHandlerSet = expat->firstCHandlerSet;
  while (activeCHandlerSet) {
      if (activeCHandlerSet->endCdataSectionCommand) {
          activeCHandlerSet->endCdataSectionCommand (activeCHandlerSet->userData);
      }
      activeCHandlerSet = activeCHandlerSet->nextHandlerSet;
  }
  return;
}


static void
generateModel (interp, rep, model)
    Tcl_Interp  *interp;
    Tcl_Obj     *rep;
    XML_Content *model;
{
    Tcl_Obj *cp, *detail;
    unsigned int      i;


    switch (model->type) {
    case XML_CTYPE_EMPTY:
        Tcl_ListObjAppendElement (interp, rep, Tcl_NewStringObj ("EMPTY", 5));
        break;
    case XML_CTYPE_ANY:
        Tcl_ListObjAppendElement (interp, rep, Tcl_NewStringObj ("ANY", 3));
        break;
    case XML_CTYPE_MIXED:
        Tcl_ListObjAppendElement (interp, rep, Tcl_NewStringObj ("MIXED", 5));
        break;
    case XML_CTYPE_NAME:
        Tcl_ListObjAppendElement (interp, rep, Tcl_NewStringObj ("NAME", 4));
        break;
    case XML_CTYPE_CHOICE:
        Tcl_ListObjAppendElement (interp, rep, Tcl_NewStringObj ("CHOICE", 6));
        break;
    case XML_CTYPE_SEQ:
        Tcl_ListObjAppendElement (interp, rep, Tcl_NewStringObj ("SEQ", 3));
        break;
    }
    switch (model->quant) {
    case XML_CQUANT_NONE:
        Tcl_ListObjAppendElement (interp, rep, Tcl_NewStringObj ("", 0));
        break;
    case XML_CQUANT_OPT:
        Tcl_ListObjAppendElement (interp, rep, Tcl_NewStringObj ("?", 1));
        break;
    case XML_CQUANT_REP:
        Tcl_ListObjAppendElement (interp, rep, Tcl_NewStringObj ("*", 1));
        break;
    case XML_CQUANT_PLUS:
        Tcl_ListObjAppendElement (interp, rep, Tcl_NewStringObj ("+", 1));
        break;
    }

    if (model->name) {
        Tcl_ListObjAppendElement (interp, rep, Tcl_NewStringObj ((char*)model->name, -1));
    }
    else {
        Tcl_ListObjAppendElement (interp, rep, Tcl_NewStringObj ("", 0));
    }
    if (model->numchildren) {
        cp = Tcl_NewListObj (0, NULL);
        for (i = 0; i < model->numchildren; i++) {
            detail = Tcl_NewListObj (0, NULL);
            generateModel (interp, detail, &model->children[i]);
            Tcl_ListObjAppendElement (interp, cp, detail);
        }
        Tcl_ListObjAppendElement (interp, rep, cp);
    }
    else {
        Tcl_ListObjAppendElement (interp, rep, Tcl_NewStringObj ("", 0));
    }
}


/*
 *----------------------------------------------------------------------
 *
 * TclGenExpatElementDeclHandler --
 *
 *	Called by expat to handle <!ELEMENT declarations.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Callback scripts are invoked.
 *
 *----------------------------------------------------------------------
 */

static void
TclGenExpatElementDeclHandler(userData, name, model)
    void *userData;
    const XML_Char *name;
    XML_Content *model;
{
  TclGenExpatInfo *expat = (TclGenExpatInfo *) userData;
  Tcl_Obj *cmdPtr;
  Tcl_Obj *content;
  int result;
  TclHandlerSet *activeTclHandlerSet;
  CHandlerSet *activeCHandlerSet;
  ExpatElemContent *eContent;

  TclExpatDispatchPCDATA(expat);

  eContent = (ExpatElemContent *) MALLOC (sizeof (ExpatElemContent));
  eContent->content = model;
  eContent->next = expat->eContents;
  expat->eContents = eContent;

  if (expat->status != TCL_OK) {
      return;
  }

  activeTclHandlerSet = expat->firstTclHandlerSet;
  while (activeTclHandlerSet) {

      switch (activeTclHandlerSet->status) {
      case TCL_CONTINUE:
          /* Makes not much sense... */
      case TCL_BREAK:
          goto nextTcl;
          break;
      default:
          ;
      }

      if (activeTclHandlerSet->elementDeclCommand == NULL) {
          goto nextTcl;
      }

      cmdPtr = Tcl_DuplicateObj(activeTclHandlerSet->elementDeclCommand);
      Tcl_IncrRefCount(cmdPtr);
      Tcl_ListObjAppendElement(expat->interp, cmdPtr,
                               Tcl_NewStringObj((char *)name, strlen(name)));


      content = Tcl_NewListObj (0, NULL);
      generateModel (expat->interp, content, model);

      Tcl_ListObjAppendElement(expat->interp, cmdPtr, content);

      result = Tcl_GlobalEvalObj(expat->interp, cmdPtr);

      Tcl_DecrRefCount(cmdPtr);

      TclExpatHandlerResult(expat, activeTclHandlerSet, result);
  nextTcl:
      activeTclHandlerSet = activeTclHandlerSet->nextHandlerSet;
  }

  activeCHandlerSet = expat->firstCHandlerSet;
  while (activeCHandlerSet) {
      if (activeCHandlerSet->elementDeclCommand) {
          activeCHandlerSet->elementDeclCommand (activeCHandlerSet->userData,
                                                 name, model);
      }
      activeCHandlerSet = activeCHandlerSet->nextHandlerSet;
  }

  return;
}

/*
 *----------------------------------------------------------------------
 *
 * TclGenExpatAttlistDeclHandler --
 *
 *	Called by expat to handle <!ATTLIST declarations.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Callback scripts are invoked.
 *
 *----------------------------------------------------------------------
 */

static void
TclGenExpatAttlistDeclHandler(userData, elname, name, type, dflt, isrequired)
    void           *userData;
    const XML_Char *elname;
    const XML_Char *name;
    const XML_Char *type;
    const XML_Char *dflt;
    int             isrequired;
{
  TclGenExpatInfo *expat = (TclGenExpatInfo *) userData;
  Tcl_Obj *cmdPtr;
  int result;
  TclHandlerSet *activeTclHandlerSet;
  CHandlerSet *activeCHandlerSet;

  TclExpatDispatchPCDATA(expat);

  if (expat->status != TCL_OK) {
      return;
  }

  activeTclHandlerSet = expat->firstTclHandlerSet;
  while (activeTclHandlerSet) {

      switch (activeTclHandlerSet->status) {
      case TCL_CONTINUE:
          /* Make not much sense... */
      case TCL_BREAK:
          goto nextTcl;
          break;
      default:
          ;
      }

      if (activeTclHandlerSet->attlistDeclCommand == NULL) {
          goto nextTcl;
      }

      cmdPtr = Tcl_DuplicateObj(activeTclHandlerSet->attlistDeclCommand);
      Tcl_IncrRefCount(cmdPtr);
      Tcl_Preserve((ClientData) expat->interp);

      Tcl_ListObjAppendElement(expat->interp, cmdPtr,
                               Tcl_NewStringObj((char *)elname, strlen (elname)));
      Tcl_ListObjAppendElement(expat->interp, cmdPtr,
                               Tcl_NewStringObj((char *)name, strlen (name)));
      Tcl_ListObjAppendElement(expat->interp, cmdPtr,
                               Tcl_NewStringObj((char *)type, strlen (type)));
      if (!dflt) {
          Tcl_ListObjAppendElement (expat->interp, cmdPtr,
                                    Tcl_NewStringObj ("", 0));
      }
      else {
          Tcl_ListObjAppendElement (expat->interp, cmdPtr,
                                    Tcl_NewStringObj ((char*)dflt, strlen (dflt)));
      }
      Tcl_ListObjAppendElement (expat->interp, cmdPtr,
                                Tcl_NewIntObj (isrequired));

      result = Tcl_GlobalEvalObj(expat->interp, cmdPtr);

      Tcl_DecrRefCount(cmdPtr);
      Tcl_Release((ClientData) expat->interp);

      TclExpatHandlerResult(expat, activeTclHandlerSet, result);
  nextTcl:
      activeTclHandlerSet = activeTclHandlerSet->nextHandlerSet;
  }

  activeCHandlerSet = expat->firstCHandlerSet;
  while (activeCHandlerSet) {
      if (activeCHandlerSet->attlistDeclCommand) {
          activeCHandlerSet->attlistDeclCommand (activeCHandlerSet->userData,
                                                 elname, name, type, dflt,
                                                 isrequired);
      }
      activeCHandlerSet = activeCHandlerSet->nextHandlerSet;
  }
  return;
}

/*
 *----------------------------------------------------------------------
 *
 * TclGenExpatStartDoctypeDeclHandler --
 *
 *	Called by expat to handle the start of <!DOCTYPE declarations.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Callback scripts are invoked.
 *
 *----------------------------------------------------------------------
 */

static void
TclGenExpatStartDoctypeDeclHandler(userData, doctypeName, sysid, pubid, has_internal_subset)
    void *userData;
    const XML_Char *doctypeName;
    const XML_Char *sysid;
    const XML_Char *pubid;
    int   has_internal_subset;
{
  TclGenExpatInfo *expat = (TclGenExpatInfo *) userData;
  Tcl_Obj *cmdPtr;
  int result;
  TclHandlerSet *activeTclHandlerSet;
  CHandlerSet *activeCHandlerSet;

  TclExpatDispatchPCDATA(expat);

  if (expat->status != TCL_OK) {
      return;
  }

  activeTclHandlerSet = expat->firstTclHandlerSet;
  while (activeTclHandlerSet) {

      switch (activeTclHandlerSet->status) {
      case TCL_CONTINUE:
          /* Make not much sense... */
      case TCL_BREAK:
          goto nextTcl;
          break;
      default:
          ;
      }

      if (activeTclHandlerSet->startDoctypeDeclCommand == NULL) {
          goto nextTcl;
      }

      cmdPtr = Tcl_DuplicateObj(activeTclHandlerSet->startDoctypeDeclCommand);
      Tcl_IncrRefCount(cmdPtr);
      Tcl_Preserve((ClientData) expat->interp);

      Tcl_ListObjAppendElement(expat->interp, cmdPtr,
          Tcl_NewStringObj((char *)doctypeName, strlen(doctypeName)));
      if (sysid != NULL) {
          Tcl_ListObjAppendElement(expat->interp, cmdPtr,
                               Tcl_NewStringObj((char *)sysid, strlen(sysid)));
      } else {
          Tcl_ListObjAppendElement(expat->interp, cmdPtr,
                               Tcl_NewStringObj("NULL", 4));
      }
      if (pubid != NULL) {
          Tcl_ListObjAppendElement(expat->interp, cmdPtr,
                               Tcl_NewStringObj((char *)pubid, strlen(sysid)));
      } else {
          Tcl_ListObjAppendElement(expat->interp, cmdPtr,
                               Tcl_NewStringObj("NULL", 4));
      }
      Tcl_ListObjAppendElement(expat->interp, cmdPtr,
                           Tcl_NewIntObj(has_internal_subset));

      result = Tcl_GlobalEvalObj(expat->interp, cmdPtr);

      Tcl_DecrRefCount(cmdPtr);
      Tcl_Release((ClientData) expat->interp);

      TclExpatHandlerResult(expat, activeTclHandlerSet, result);
  nextTcl:
      activeTclHandlerSet = activeTclHandlerSet->nextHandlerSet;
  }

  activeCHandlerSet = expat->firstCHandlerSet;
  while (activeCHandlerSet) {
      if (activeCHandlerSet->startDoctypeDeclCommand) {
          activeCHandlerSet->startDoctypeDeclCommand (activeCHandlerSet->userData,
                                                      doctypeName, sysid,
                                                      pubid,
                                                      has_internal_subset);
      }
      activeCHandlerSet = activeCHandlerSet->nextHandlerSet;
  }
  return;
}

/*
 *----------------------------------------------------------------------
 *
 * TclGenExpatEndDoctypeDeclHandler --
 *
 *	Called by expat to handle the end of <!DOCTYPE declarations.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Callback script is invoked.
 *
 *----------------------------------------------------------------------
 */

static void
TclGenExpatEndDoctypeDeclHandler(userData)
    void *userData;
{
  TclGenExpatInfo *expat = (TclGenExpatInfo *) userData;
  Tcl_Obj *cmdPtr;
  int result;
  TclHandlerSet *activeTclHandlerSet;
  CHandlerSet *activeCHandlerSet;
  ExpatElemContent *eContent, *eContentSave;

  TclExpatDispatchPCDATA(expat);

  if (expat->status != TCL_OK) {
      return;
  }

  activeTclHandlerSet = expat->firstTclHandlerSet;
  while (activeTclHandlerSet) {

      switch (activeTclHandlerSet->status) {
      case TCL_CONTINUE:
      case TCL_BREAK:
          goto nextTcl;
          break;
      default:
          ;
      }

      if (activeTclHandlerSet->endDoctypeDeclCommand == NULL) {
          goto nextTcl;
      }

      cmdPtr = Tcl_DuplicateObj(activeTclHandlerSet->endDoctypeDeclCommand);
      Tcl_IncrRefCount(cmdPtr);
      Tcl_Preserve((ClientData) expat->interp);

      result = Tcl_GlobalEvalObj(expat->interp, cmdPtr);

      Tcl_DecrRefCount(cmdPtr);
      Tcl_Release((ClientData) expat->interp);

      TclExpatHandlerResult(expat, activeTclHandlerSet, result);
  nextTcl:
      activeTclHandlerSet = activeTclHandlerSet->nextHandlerSet;
  }

  activeCHandlerSet = expat->firstCHandlerSet;
  while (activeCHandlerSet) {
      if (activeCHandlerSet->endDoctypeDeclCommand) {
          activeCHandlerSet->endDoctypeDeclCommand (activeCHandlerSet->userData);
      }
      activeCHandlerSet = activeCHandlerSet->nextHandlerSet;
  }

  eContent = expat->eContents;
  while (eContent) {
      free(eContent->content); /* This *must* be done with free() */
      eContentSave = eContent;
      eContent = eContent->next;
      FREE((char *) eContentSave);
  }
  expat->eContents = NULL;
  
  return;
}


/*
 *----------------------------------------------------------------------
 *
 * TclGenExpatXmlDeclHandler --
 *
 *	Called by expat for both XML declarations and text declarations.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Callback script is invoked.
 *
 *----------------------------------------------------------------------
 */

static void
TclGenExpatXmlDeclHandler (userData, version, encoding, standalone)
    void *userData;
    const char *version;
    const char *encoding;
    int   standalone;
{
    TclGenExpatInfo *expat = (TclGenExpatInfo *) userData;
    Tcl_Obj *cmdPtr;
    int result;
    TclHandlerSet *activeTclHandlerSet;
    CHandlerSet *activeCHandlerSet;

    if (expat->status != TCL_OK) {
        return;
    }

  activeTclHandlerSet = expat->firstTclHandlerSet;
  while (activeTclHandlerSet) {

      switch (activeTclHandlerSet->status) {
      case TCL_CONTINUE:
          /* Make not much sense... */
      case TCL_BREAK:
          goto nextTcl;
          break;
      default:
          ;
      }

      if (activeTclHandlerSet->xmlDeclCommand == NULL) {
          goto nextTcl;
      }
      cmdPtr = Tcl_DuplicateObj (activeTclHandlerSet->xmlDeclCommand);
      Tcl_IncrRefCount (cmdPtr);
      Tcl_Preserve ((ClientData) expat->interp);

      Tcl_ListObjAppendElement (expat->interp, cmdPtr,
                                Tcl_NewStringObj ((char*)version, -1));
      Tcl_ListObjAppendElement (expat->interp, cmdPtr,
                                Tcl_NewStringObj ((char*)encoding, -1));
      if (standalone == -1) {
          Tcl_ListObjAppendElement (expat->interp, cmdPtr,
                                    Tcl_NewStringObj ("", 0));
      }
      else  {
          Tcl_ListObjAppendElement (expat->interp, cmdPtr,
                                    Tcl_NewBooleanObj (standalone));
      }

      result = Tcl_GlobalEvalObj (expat->interp, cmdPtr);

      Tcl_DecrRefCount(cmdPtr);
      Tcl_Release((ClientData) expat->interp);

      TclExpatHandlerResult (expat, activeTclHandlerSet, result);
  nextTcl:
      activeTclHandlerSet = activeTclHandlerSet->nextHandlerSet;
  }

  activeCHandlerSet = expat-> firstCHandlerSet;
  while (activeCHandlerSet) {
      if (activeCHandlerSet->xmlDeclCommand) {
          activeCHandlerSet->xmlDeclCommand (activeCHandlerSet->userData,
                                             version, encoding, standalone);
      }
      activeCHandlerSet = activeCHandlerSet->nextHandlerSet;
  }
  return;
}


/*
 *----------------------------------------------------------------------------
 *
 * TclExpatDeleteCmd --
 *
 *	Called when an expat parser is deleted.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	Memory structures are freed.
 *
 *----------------------------------------------------------------------------
 */

static void
TclExpatDeleteCmd(clientData)
     ClientData clientData;
{
  TclGenExpatInfo *expat = (TclGenExpatInfo *) clientData;
  TclHandlerSet *activeTclHandlerSet, *tmpTclHandlerSet;
  CHandlerSet *activeCHandlerSet, *tmpCHandlerSet;

  TclExpatFreeParser(expat);

  Tcl_DecrRefCount(expat->name);

  if (expat->cdata) {
    Tcl_DecrRefCount(expat->cdata);
    expat->cdata = NULL;
  }

  activeTclHandlerSet = expat->firstTclHandlerSet;
  while (activeTclHandlerSet) {
      FREE (activeTclHandlerSet->name);

      if (activeTclHandlerSet->elementstartcommand) {
          Tcl_DecrRefCount(activeTclHandlerSet->elementstartcommand);
      }
      if (activeTclHandlerSet->elementendcommand) {
          Tcl_DecrRefCount(activeTclHandlerSet->elementendcommand);
      }
      if (activeTclHandlerSet->startnsdeclcommand) {
          Tcl_DecrRefCount(activeTclHandlerSet->startnsdeclcommand);
      }
      if (activeTclHandlerSet->endnsdeclcommand) {
          Tcl_DecrRefCount(activeTclHandlerSet->endnsdeclcommand);
      }
      if (activeTclHandlerSet->datacommand) {
          Tcl_DecrRefCount(activeTclHandlerSet->datacommand);
      }
      if (activeTclHandlerSet->picommand) {
          Tcl_DecrRefCount(activeTclHandlerSet->picommand);
      }
      if (activeTclHandlerSet->defaultcommand) {
          Tcl_DecrRefCount(activeTclHandlerSet->defaultcommand);
      }
      if (activeTclHandlerSet->notationcommand) {
          Tcl_DecrRefCount(activeTclHandlerSet->notationcommand);
      }
      if (activeTclHandlerSet->externalentitycommand) {
          Tcl_DecrRefCount(activeTclHandlerSet->externalentitycommand);
      }
      if (activeTclHandlerSet->unknownencodingcommand) {
          Tcl_DecrRefCount(activeTclHandlerSet->unknownencodingcommand);
      }
      if (activeTclHandlerSet->commentCommand) {
          Tcl_DecrRefCount(activeTclHandlerSet->commentCommand);
      }
      if (activeTclHandlerSet->notStandaloneCommand) {
          Tcl_DecrRefCount(activeTclHandlerSet->notStandaloneCommand);
      }
      if (activeTclHandlerSet->startCdataSectionCommand) {
          Tcl_DecrRefCount(activeTclHandlerSet->startCdataSectionCommand);
      }
      if (activeTclHandlerSet->elementDeclCommand) {
          Tcl_DecrRefCount(activeTclHandlerSet->elementDeclCommand);
      }
      if (activeTclHandlerSet->attlistDeclCommand) {
          Tcl_DecrRefCount(activeTclHandlerSet->attlistDeclCommand);
      }
      if (activeTclHandlerSet->startDoctypeDeclCommand) {
          Tcl_DecrRefCount(activeTclHandlerSet->startDoctypeDeclCommand);
      }
      if (activeTclHandlerSet->endDoctypeDeclCommand) {
          Tcl_DecrRefCount(activeTclHandlerSet->endDoctypeDeclCommand);
      }
      if (activeTclHandlerSet->xmlDeclCommand) {
          Tcl_DecrRefCount (activeTclHandlerSet->xmlDeclCommand);
      }
      if (activeTclHandlerSet->entityDeclCommand) {
          Tcl_DecrRefCount (activeTclHandlerSet->entityDeclCommand);
      }

      tmpTclHandlerSet = activeTclHandlerSet;
      activeTclHandlerSet = activeTclHandlerSet->nextHandlerSet;
      FREE ( (char*) tmpTclHandlerSet);
  }

  activeCHandlerSet = expat->firstCHandlerSet;
  while (activeCHandlerSet) {
      FREE (activeCHandlerSet->name);

      if (activeCHandlerSet->freeProc) {
          activeCHandlerSet->freeProc (expat->interp, activeCHandlerSet->userData);
      }

      tmpCHandlerSet = activeCHandlerSet;
      activeCHandlerSet = activeCHandlerSet->nextHandlerSet;
      FREE ( (char*) tmpCHandlerSet);
  }

  FREE( (char*) expat);
}


int
CheckExpatParserObj (interp, nameObj)
    Tcl_Interp *interp;
    Tcl_Obj *CONST nameObj;
{
    Tcl_CmdInfo info;


    if (!Tcl_GetCommandInfo (interp, Tcl_GetStringFromObj (nameObj, NULL),
                             &info)) {
        return 0;
    }
    if (!info.isNativeObjectProc || info.objProc != TclExpatInstanceCmd) {
        return 0;
    }
    return 1;
}

int
CHandlerSetInstall (interp, expatObj, handlerSet)
    Tcl_Interp *interp;
    Tcl_Obj *CONST expatObj;
    CHandlerSet *handlerSet;
{
    Tcl_CmdInfo info;
    TclGenExpatInfo *expat;
    CHandlerSet *activeCHandlerSet;

    if (!Tcl_GetCommandInfo (interp, Tcl_GetStringFromObj (expatObj, NULL),
                             &info)) {
        return 1;
    }
    expat = (TclGenExpatInfo *) info.objClientData;
    if (expat->firstCHandlerSet != NULL) {
        activeCHandlerSet = expat->firstCHandlerSet;
        while (1) {
            if (strcmp (activeCHandlerSet->name, handlerSet->name) == 0) {
                return 2;
            }
            if (activeCHandlerSet->nextHandlerSet == NULL) {
                break;
            }
            else {
                activeCHandlerSet = activeCHandlerSet->nextHandlerSet;
            }
        }
        activeCHandlerSet->nextHandlerSet = handlerSet;
    }
    else {
        expat->firstCHandlerSet = handlerSet;
    }
    if (handlerSet->ignoreWhiteCDATAs) {
        expat->needWSCheck = 1;
    }
    return 0;
}

int
CHandlerSetRemove (interp, expatObj, handlerSetName)
    Tcl_Interp *interp;
    Tcl_Obj *CONST expatObj;
    char *handlerSetName;
{
    Tcl_CmdInfo info;
    TclGenExpatInfo *expat;
    CHandlerSet *activeCHandlerSet, *parentHandlerSet = NULL;

    if (!Tcl_GetCommandInfo (interp, Tcl_GetStringFromObj (expatObj, NULL), &info)) {
        return 1;
    }
    expat = (TclGenExpatInfo *) info.objClientData;
    if (expat->firstCHandlerSet == NULL) {
        return 2;
    }

    activeCHandlerSet = expat->firstCHandlerSet;
    while (activeCHandlerSet) {
        if (strcmp (activeCHandlerSet->name, handlerSetName) == 0) {
            FREE (activeCHandlerSet->name);
            if (activeCHandlerSet->freeProc) {
                activeCHandlerSet->freeProc (interp, activeCHandlerSet->userData);
            }
            if (parentHandlerSet) {
                parentHandlerSet->nextHandlerSet =
                    activeCHandlerSet->nextHandlerSet;
            } else {
                expat->firstCHandlerSet = activeCHandlerSet->nextHandlerSet;
            }
            FREE ( (char*) activeCHandlerSet);
            return 0;
        }
        parentHandlerSet = activeCHandlerSet;
        activeCHandlerSet = activeCHandlerSet->nextHandlerSet;
    }
    return 2;
}

CHandlerSet *
CHandlerSetGet (interp, expatObj, handlerSetName)
    Tcl_Interp *interp;
    Tcl_Obj *CONST expatObj;
    char *handlerSetName;
{
    Tcl_CmdInfo info;
    TclGenExpatInfo *expat;
    CHandlerSet *activeCHandlerSet;

    if (!Tcl_GetCommandInfo (interp, Tcl_GetStringFromObj (expatObj, NULL), &info)) {
        return NULL;
    }
    expat = (TclGenExpatInfo *) info.objClientData;
    if (expat->firstCHandlerSet == NULL) {
        return NULL;
    }
    activeCHandlerSet = expat->firstCHandlerSet;
    while (activeCHandlerSet) {
        if (strcmp (activeCHandlerSet->name, handlerSetName) == 0) {
            return activeCHandlerSet;
        }
        activeCHandlerSet = activeCHandlerSet->nextHandlerSet;
    }
    return NULL;
}

void *
CHandlerSetGetUserData (interp, expatObj, handlerSetName)
    Tcl_Interp *interp;
    Tcl_Obj *CONST expatObj;
    char *handlerSetName;
{
    Tcl_CmdInfo info;
    TclGenExpatInfo *expat;
    CHandlerSet *activeCHandlerSet;

    if (!Tcl_GetCommandInfo (interp, Tcl_GetStringFromObj (expatObj, NULL), &info)) {
        return NULL;
    }
    expat = (TclGenExpatInfo *) info.objClientData;
    if (expat->firstCHandlerSet == NULL) {
        return NULL;
    }
    activeCHandlerSet = expat->firstCHandlerSet;
    while (activeCHandlerSet) {
        if (strcmp (activeCHandlerSet->name, handlerSetName) == 0) {
            return activeCHandlerSet->userData;
        }
        activeCHandlerSet = activeCHandlerSet->nextHandlerSet;
    }
    return NULL;
}

TclGenExpatInfo *
GetExpatInfo (interp, expatObj)
    Tcl_Interp *interp;
    Tcl_Obj *CONST expatObj;
{
    Tcl_CmdInfo info;
    if (!Tcl_GetCommandInfo (interp, Tcl_GetStringFromObj (expatObj, NULL), &info)) {
        return NULL;
    }
    return (TclGenExpatInfo *) info.objClientData;
}
