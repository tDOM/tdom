/* This code implements a set of tDOM C Handlers that can be
   dynamically loaded into a tclsh with already loaded tDOM
   package. This parser extension does some tests according to the DTD
   against the data within an XML document.

   Copyright (c) 2001 Rolf Ade */

#include <tclexpat.h>
#include <string.h>
#include <mcheck.h>

#define Tcl_Alloc(x) malloc((x))
#define Tcl_Free(x)  free((x))
#define Tcl_Realloc(x,y) realloc((x),(y))

/* The inital stack sizes must be at least 1 */
#define TNC_INITCONTENTSTACKSIZE 512

/* To enable some debugging output at stdout use this.
   But beware: this debugging output isn't systematic
   and only understandable, if you know the internals
   of tnc. */
/*  #define TNC_DEBUG */

/* The elements of TNC_Content carry exactly the same information
   as expats XML_Content. But the element is identified by his
   Tcl_HashEntry entry within the "tagNames" Hashtable (see TNC_Data)
   and not the element name. This should be much more efficient. */
typedef struct TNC_cp TNC_Content;

struct TNC_cp
{
    enum XML_Content_Type   type;
    enum XML_Content_Quant  quant;
    Tcl_HashEntry          *nameId;
    unsigned int            numchildren;
    TNC_Content            *children;
};

typedef struct TNC_contentStack
{
    TNC_Content  *model;
    int           activeChild;
    int           deep;
    int           alreadymatched;
} TNC_ContentStack;


typedef struct TNC_data
{
    char             *doctypeName;
    int               ignoreWhiteCDATAs;
    int               ignorePCDATA;
    Tcl_HashTable    *tagNames;
    Tcl_HashTable    *attDefsTables;
    Tcl_HashTable    *entityDecls;
    Tcl_HashTable    *notationDecls;
    Tcl_HashTable    *ids;
    Tcl_Interp       *interp;
    Tcl_Obj          *expatObj;
    int               contentStackSize;
    int               contentStackPtr;
    TNC_ContentStack *contentStack;
} TNC_Data;

typedef enum TNC_attType {
    TNC_ATTTYPE_CDATA,
    TNC_ATTTYPE_ID,
    TNC_ATTTYPE_IDREF,
    TNC_ATTTYPE_IDREFS,
    TNC_ATTTYPE_ENTITY,
    TNC_ATTTYPE_ENTITIES,
    TNC_ATTTYPE_NMTOKEN,
    TNC_ATTTYPE_NMTOKENS,
    TNC_ATTTYPE_NOTATION,
    TNC_ATTTYPE_ENUMERATION,
} TNC_AttType;

typedef struct TNC_elemAttInfo
{
    Tcl_HashTable *attributes;
    int            nrOfreq;
    int            nrOfIdAtts;
} TNC_ElemAttInfo;

typedef struct TNC_attDecl
{
    TNC_AttType    att_type;
    char          *dflt;
    int            isrequired;
    Tcl_HashTable *lookupTable;   /* either NotationTypes or enum values */
} TNC_AttDecl;

typedef struct TNC_entityInfo
{
    int    is_notation;
    char  *notationName;
} TNC_EntityInfo;

typedef Tcl_HashEntry TNC_NameId;

static const int zero = 0;
static const int one = 1;
static char tnc_usage[] =
               "Usage tnc <expat parser obj> <subCommand>, where subCommand can be: \n"
               "        enable    \n"
               "        remove    \n"
               ;

enum TNC_Error {
    TNC_ERROR_NONE,
    TNC_ERROR_DUPLICATE_ELEMENT_DECL,
    TNC_ERROR_DUPLICATE_MIXED_ELEMENT,
    TNC_ERROR_UNKNOWN_ELEMENT,
    TNC_ERROR_EMPTY_ELEMENT,
    TNC_ERROR_DISALLOWED_PCDATA,
    TNC_ERROR_DISALLOWED_CDATA,
    TNC_ERROR_NO_DOCTYPE_DECL,
    TNC_ERROR_WRONG_ROOT_ELEMENT,
    TNC_ERROR_NO_ATTRIBUTES,
    TNC_ERROR_UNKOWN_ATTRIBUTE,
    TNC_ERROR_WRONG_FIXED_ATTVALUE,
    TNC_ERROR_MISSING_REQUIRED_ATTRIBUTE,
    TNC_ERROR_MORE_THAN_ONE_ID_ATT,
    TNC_ERROR_ID_ATT_DEFAULT,
    TNC_ERROR_DUPLICATE_ID_VALUE,
    TNC_ERROR_UNKOWN_ID_REFERRED,
    TNC_ERROR_ENTITY_ATTRIBUTE,
    TNC_ERROR_ENTITIES_ATTRIBUTE,
    TNC_ERROR_ATT_ENTITY_DEFAULT_MUST_BE_DECLARED,
    TNC_ERROR_NOTATION_REQUIRED,
    TNC_ERROR_NOTATION_MUST_BE_DECLARED,
    TNC_ERROR_UNIMPOSSIBLE_DEFAULT,
    TNC_ERROR_ENUM_ATT_WRONG_VALUE,
    TNC_ERROR_NMTOKEN_REQUIRED,
    TNC_ERROR_NAME_REQUIRED,
    TNC_ERROR_ELEMENT_NOT_ALLOWED_HERE,
    TNC_ERROR_ELEMENT_CAN_NOT_END_HERE,
    TNC_ERROR_INTERNAL
};

const char *
TNC_ErrorString (int code)
{
    static const char *message[] = {
        "No error.",
        "Element declared more than once.",
        "The same name must not appear more than once in \n\tone mixed-content declaration.",
        "No declaration for this element.",
        "Element is declared to be empty, but isn't.",
        "PCDATA not allowed here.",
        "CDATA section not allowed here.",
        "No DOCTYPE declaration.",
        "Root element doesn't match DOCTYPE name.",
        "No attributes defined for this element.",
        "Unknown attribute for this element.",
        "Attribute value must match the FIXED default.",
        "Required attribute missing.",
        "Only one attribute with type ID allowed.",
        "No default value allowed for attribute type ID.",
        "ID attribute values must be unique within the document.",
        "Unknown ID referred.",
        "Attribute value has to be a unparsed entity.",
        "Attribute value has to be a sequence of unparsed entities.",
        "The defaults of attributes with type ENTITY or ENTITIES\nhas to be unparsed entities.",
        "Attribute value has to be one of the allowed notations.",
        "Every used NOTATION must be declared.",
        "Attribute default is not one of the allowed values",
        "Attribute hasn't one of the allowed values.",
        "Attribute value has to be a NMTOKEN.",
        "Atrribute value has to be a Name.",
        "Element is not allowed here.",
        "Element can not end here (required element(s) missing).",
        "Can only handle UTF8 chars up to 3 bytes length."
    };
    if (code > 0 && code < sizeof(message)/sizeof(message[0]))
        return message[code];
    return 0;
}

#define UTF8_1BYTE_CHAR(c) ( 0    == ((c) & 0x80))
#define UTF8_2BYTE_CHAR(c) ( 0xC0 == ((c) & 0xE0))
#define UTF8_3BYTE_CHAR(c) ( 0xE0 == ((c) & 0xF0))
#define UTF8_4BYTE_CHAR(c) ( 0xF0 == ((c) & 0xF8))

/* The following 2 defines are out of the expat code */

/* A 2 byte UTF-8 representation splits the characters 11 bits
between the bottom 5 and 6 bits of the bytes.
We need 8 bits to index into pages, 3 bits to add to that index and
5 bits to generate the mask. */
#define UTF8_GET_NAMING2(pages, byte) \
    (namingBitmap[((pages)[(((byte)[0]) >> 2) & 7] << 3) \
                      + ((((byte)[0]) & 3) << 1) \
                      + ((((byte)[1]) >> 5) & 1)] \
         & (1 << (((byte)[1]) & 0x1F)))

/* A 3 byte UTF-8 representation splits the characters 16 bits
between the bottom 4, 6 and 6 bits of the bytes.
We need 8 bits to index into pages, 3 bits to add to that index and
5 bits to generate the mask. */
#define UTF8_GET_NAMING3(pages, byte) \
  (namingBitmap[((pages)[((((byte)[0]) & 0xF) << 4) \
                             + ((((byte)[1]) >> 2) & 0xF)] \
                       << 3) \
                      + ((((byte)[1]) & 3) << 1) \
                      + ((((byte)[2]) >> 5) & 1)] \
         & (1 << (((byte)[2]) & 0x1F)))

#define UTF8_CHAR_LEN(c) \
  if (UTF8_1BYTE_CHAR ((c))) clen = 1 ; \
  else if (UTF8_2BYTE_CHAR ((c)))  clen = 2 ; \
  else if (UTF8_3BYTE_CHAR ((c)))  clen = 3 ; \
  else {signalNotValid (userData, TNC_ERROR_INTERNAL); return;}

#define UTF8_CHAR_LEN_COPY(c) \
  if (UTF8_1BYTE_CHAR ((c))) clen = 1; \
  else if (UTF8_2BYTE_CHAR ((c))) clen = 2; \
  else if (UTF8_3BYTE_CHAR ((c))) clen = 3; \
  else {signalNotValid (userData, TNC_ERROR_INTERNAL); free (copy); return;}


#define UTF8_GET_NAMING_NMTOKEN(p, n) \
  ((n) == 1 \
  ? nameChar7Bit[(int)(*(p))] \
  : ((n) == 2 \
    ? UTF8_GET_NAMING2(nmstrtPages, (const unsigned char *)(p)) \
    : ((n) == 3 \
      ? UTF8_GET_NAMING3(nmstrtPages, (const unsigned char *)(p)) \
      : 0)))

#define UTF8_GET_NAMING_NAME(p, n) \
  ((n) == 1 \
  ? nameStart7Bit[(int)(*(p))] \
  : ((n) == 2 \
    ? UTF8_GET_NAMING2(namePages, (const unsigned char *)(p)) \
    : ((n) == 3 \
      ? UTF8_GET_NAMING3(namePages, (const unsigned char *)(p)) \
      : 0)))


#include "../../expat-1.95.1/nametab.h"

static const unsigned char nameChar7Bit[] = {
/* 0x00 */    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
/* 0x08 */    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
/* 0x10 */    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
/* 0x18 */    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
/* 0x20 */    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
/* 0x28 */    0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x00,
/* 0x30 */    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
/* 0x38 */    0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
/* 0x40 */    0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
/* 0x48 */    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
/* 0x50 */    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
/* 0x58 */    0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x01,
/* 0x60 */    0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
/* 0x68 */    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
/* 0x70 */    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
/* 0x78 */    0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
};

static const unsigned char nameStart7Bit[] = {
/* 0x00 */    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
/* 0x08 */    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
/* 0x10 */    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
/* 0x18 */    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
/* 0x20 */    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
/* 0x28 */    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
/* 0x30 */    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
/* 0x38 */    0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
/* 0x40 */    0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
/* 0x48 */    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
/* 0x50 */    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
/* 0x58 */    0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x01,
/* 0x60 */    0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
/* 0x68 */    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
/* 0x70 */    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
/* 0x78 */    0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
};

static void
signalNotValid (userData, code)
    void        *userData;
    int          code;
{
    TNC_Data *tncdata = (TNC_Data *) userData;
    TclGenExpatInfo *expat;
    char s[255];

    expat = GetExpatInfo (tncdata->interp, tncdata->expatObj);
    expat->status = TCL_ERROR;
    sprintf (s, "Validation error at line %d, character %d:\n\t%s",
             XML_GetCurrentLineNumber (expat->parser),
             XML_GetCurrentColumnNumber (expat->parser),
             TNC_ErrorString (code));
    expat->result = Tcl_NewStringObj (s, -1);
    Tcl_IncrRefCount (expat->result);
}


/*
 *----------------------------------------------------------------------------
 *
 * TncStartDoctypeDeclHandler --
 *
 *	This procedure is called for the start of the DOCTYPE
 *      declaration.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Stores the doctype Name in the TNC_data.
 *
 *----------------------------------------------------------------------------
 */

void
TncStartDoctypeDeclHandler (userData, doctypeName, sysid, pubid, has_internal_subset)
    void       *userData;
    const char *doctypeName;
    const char *sysid;
    const char *pubid;
    int         has_internal_subset;
{
    TNC_Data *tncdata = (TNC_Data *) userData;

#ifdef TNC_DEBUG
    printf ("TncStartDoctypeDeclHandler start\n");
#endif
    tncdata->doctypeName = strdup (doctypeName);
}


/*
 *----------------------------------------------------------------------------
 *
 * TncFreeTncModel --
 *
 *	This helper procedure frees recursively TNC_Contents.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Frees memory.
 *
 *----------------------------------------------------------------------------
 */

static void
TncFreeTncModel (tmodel)
    TNC_Content *tmodel;
{
    int i;

    if (tmodel->children) {
        for (i = 0; i < tmodel->numchildren; i++) {
            TncFreeTncModel (&tmodel->children[i]);
        }
        Tcl_Free ((char *) tmodel->children);
    }
}


/*
 *----------------------------------------------------------------------------
 *
 * TncRewriteModel --
 *
 *	This helper procedure creates recursively a TNC_Content from
 *      a XML_Content and frees the XML_Content (TODO).
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Allocates memory for the TNC_Content  models and frees
 *      the XML_Content models (TODO).
 *
 *----------------------------------------------------------------------------
 */

static void
TncRewriteModel (emodel, tmodel, tagNames)
    XML_Content   *emodel;
    TNC_Content   *tmodel;
    Tcl_HashTable *tagNames;
{
    Tcl_HashEntry *entryPtr;
    int i;

    tmodel->type = emodel->type;
    tmodel->quant = emodel->quant;
    tmodel->numchildren = emodel->numchildren;
    switch (emodel->type) {
    case XML_CTYPE_MIXED:
        tmodel->nameId = NULL;
        if (emodel->quant == XML_CQUANT_REP) {
            tmodel->children = (TNC_Content *)
                Tcl_Alloc (sizeof (TNC_Content) * emodel->numchildren);
            for (i = 0; i < emodel->numchildren; i++) {
                TncRewriteModel (&emodel->children[i], &tmodel->children[i],
                                 tagNames);
            }
        } else {
            tmodel->children = NULL;
        }
        break;
    case XML_CTYPE_ANY:
    case XML_CTYPE_EMPTY:
        tmodel->nameId = NULL;
        tmodel->children = NULL;
        break;
    case XML_CTYPE_SEQ:
    case XML_CTYPE_CHOICE:
        tmodel->nameId = NULL;
        tmodel->children = (TNC_Content *)
            Tcl_Alloc (sizeof (TNC_Content) * emodel->numchildren);
        for (i = 0; i < emodel->numchildren; i++) {
            TncRewriteModel (&emodel->children[i], &tmodel->children[i],
                             tagNames);
        }
        break;
    case XML_CTYPE_NAME:
        entryPtr = Tcl_FindHashEntry (tagNames, emodel->name);
        /* Notice, that it is possible for entryPtr to be NULL.
           This means, a content model uses a not declared element.
           This is legal even in valid documents. (Of course, if the
           undeclared element actually shows up in the document
           that would make the document invalid.) See rec 3.2

           QUESTION: Should there be a flag to enable a warning,
           when a declaration contains an element type for which
           no declaration is provided, as rec 3.2 metioned?
           This would be the appropriated place to omit the
           warning. */
        tmodel->nameId = entryPtr;
        tmodel->children = NULL;
    }
    /* TODO: free XML_Content model. */
}


/*
 *----------------------------------------------------------------------------
 *
 * TncEndDoctypeDeclHandler --
 *
 *	This procedure is called at the end of the DOCTYPE
 *      declaration, after processing any external subset.
 *      It rewrites the XML_Content models to TNC_Content
 *      models and frees the XML_Content models.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Rewrites the XML_Content models to TNC_Content
 *      models and frees the XML_Content models.
 *
 *----------------------------------------------------------------------------
 */

void
TncEndDoctypeDeclHandler (userData)
    void *userData;
{
    TNC_Data *tncdata = (TNC_Data *) userData;
    Tcl_HashEntry *entryPtr;
    Tcl_HashSearch search;
    XML_Content   *emodel;
    TNC_Content   *tmodel = NULL;

#ifdef TNC_DEBUG
printf ("'zero' pointer %p\n", &zero);
printf ("'one' pointer %p\n", &one);
#endif


    entryPtr = Tcl_FirstHashEntry (tncdata->tagNames, &search);
    while (entryPtr != NULL) {
#ifdef TNC_DEBUG
        printf ("name: %-20s   nameId: %p\n",
                Tcl_GetHashKey (tncdata->tagNames, entryPtr),
                entryPtr);
#endif
        emodel = (XML_Content*) Tcl_GetHashValue(entryPtr);
        tmodel = Tcl_Alloc (sizeof (TNC_Content));
        TncRewriteModel (emodel, tmodel, tncdata->tagNames);
        Tcl_SetHashValue (entryPtr, tmodel);
        free (emodel);
        entryPtr = Tcl_NextHashEntry (&search);
    }
    /* Checks, if every used notation name is in deed declared */
    entryPtr = Tcl_FirstHashEntry (tncdata->notationDecls, &search);
    while (entryPtr != NULL) {
#ifdef TNC_DEBUG
        printf ("check notation name %s\n",
                Tcl_GetHashKey (tncdata->notationDecls, entryPtr));
        printf ("value %p\n", Tcl_GetHashValue (entryPtr));
#endif
        if (Tcl_GetHashValue (entryPtr) != &one) {
            signalNotValid (userData, TNC_ERROR_NOTATION_MUST_BE_DECLARED);
            return;
        }
        entryPtr = Tcl_NextHashEntry (&search);
    }
    /* Checks, if every used entity name is indeed declared */
    entryPtr = Tcl_FirstHashEntry (tncdata->entityDecls, &search);
    while (entryPtr != NULL) {
        if (Tcl_GetHashValue (entryPtr) == &zero) {
            signalNotValid (userData,
                            TNC_ERROR_ATT_ENTITY_DEFAULT_MUST_BE_DECLARED);
            return;
        }
        entryPtr = Tcl_NextHashEntry (&search);
    }
}


/*
 *----------------------------------------------------------------------------
 *
 * TncEntityDeclHandler --
 *
 *	This procedure is called for every entity declaration.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Stores either the name of the entity and
 *      type information in a lookup table.
 *
 *----------------------------------------------------------------------------
 */

void
TncEntityDeclHandler (userData, entityName, is_parameter_entity, value,
                       value_length, base, systemId, publicId, notationName)
    void *userData;
    const char *entityName;
    int is_parameter_entity;
    const char *value;
    int value_length;
    const char *base;
    const char *systemId;
    const char *publicId;
    const char *notationName;
{
    TNC_Data *tncdata = (TNC_Data *) userData;
    Tcl_HashEntry *entryPtr, *entryPtr1;
    int newPtr;
    TNC_EntityInfo *entityInfo;


    /* expat collects entity definitions internaly by himself. So this is
       maybe superfluous, if it possible to access the expat internal
       represention. To study this is left to the reader. */

    if (is_parameter_entity) return;
    entryPtr = Tcl_CreateHashEntry (tncdata->entityDecls, entityName, &newPtr);
    /* multiple declaration of the same entity are allowed; first
       definition wins (rec. 4.2) */
    if (!newPtr) {
        /* Eventually, an attribute declaration with type ENTITY or ENTITIES
           has used this (up to the attribute declaration undeclared) ENTITY
           within his default value. In this case, the hash value have to
           be &zero and the entity must be a unparsed entity. */
        if (Tcl_GetHashValue (entryPtr) == &zero) {
            if (notationName == NULL) {
                signalNotValid (userData,
                                TNC_ERROR_ATT_ENTITY_DEFAULT_MUST_BE_DECLARED);
                return;
            }
            newPtr = 1;
        }
    }
    if (newPtr) {
        entityInfo = (TNC_EntityInfo *) Tcl_Alloc (sizeof (TNC_EntityInfo));
        if (notationName != NULL) {
            entityInfo->is_notation = 1;
            entryPtr1 = Tcl_CreateHashEntry (tncdata->notationDecls,
                                             notationName, &newPtr);
            if (newPtr) {
                Tcl_SetHashValue (entryPtr1, &zero);
            }
            entityInfo->notationName = strdup (notationName);
        }
        else {
            entityInfo->is_notation = 0;
        }
        Tcl_SetHashValue (entryPtr, entityInfo);
    }
}

/*
 *----------------------------------------------------------------------------
 *
 * TncNotationDeclHandler --
 *
 *	This procedure is called for every notation declaration.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Stores the notationName in the notationDecls table with value
 *      one.
 *
 *----------------------------------------------------------------------------
 */

void
TncNotationDeclHandler (userData, notationName, base, systemId, publicId)
    void       *userData;
    const char *notationName;
    const char *base;
    const char *systemId;
    const char *publicId;
{
    TNC_Data *tncdata = (TNC_Data *) userData;
    Tcl_HashEntry *entryPtr;
    int newPtr;

    entryPtr = Tcl_CreateHashEntry (tncdata->notationDecls,
                                    notationName,
                                    &newPtr);
#ifdef TNC_DEBUG
    printf ("Notation %s declared\n", notationName);
#endif
    Tcl_SetHashValue (entryPtr, &one);
}



/*
 *----------------------------------------------------------------------------
 *
 * TncElementDeclCommand --
 *
 *	This procedure is called for every element declaration.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Stores the tag name of the element in a lookup table.
 *
 *----------------------------------------------------------------------------
 */

void
TncElementDeclCommand (userData, name, model)
    void *userData;
    const char *name;
    XML_Content *model;
{
    TNC_Data *tncdata = (TNC_Data *) userData;
    Tcl_HashEntry *entryPtr;
    int newPtr, i, j;

    entryPtr = Tcl_CreateHashEntry (tncdata->tagNames, name, &newPtr);
    /* "No element type may be declared more than once." (rec. 3.2) */
    if (!newPtr) {
        signalNotValid (userData, TNC_ERROR_DUPLICATE_ELEMENT_DECL);
        return;
    }
    /* "The same name must not appear more than once in a
        single mixed-content declaration." (rec. 3.2.2)
        NOTE: OK, OK, doing it this way may not be optimal or even fast
        in some cases. Please step in with a more fancy solution, if you
        feel the need. */
    if (model->type == XML_CTYPE_MIXED && model->quant == XML_CQUANT_REP) {
        for (i = 0; i < model->numchildren; i++) {
            for (j = i + 1; j < model->numchildren; j++) {
                if (strcmp ((&model->children[i])->name,
                            (&model->children[j])->name) == 0) {
                    signalNotValid (userData,
                                    TNC_ERROR_DUPLICATE_MIXED_ELEMENT);
                    return;
                }
            }
        }
    }
    Tcl_SetHashValue (entryPtr, model);
    return;
}


/*
 *----------------------------------------------------------------------------
 *
 * TncAttDeclCommand --
 *
 *	This procedure is called for *each* attribute in an XML
 *      ATTLIST declaration. It stores the attribute definition in
 *      an element specific hash table.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Stores the tag name of the element in a lookup table.
 *
 *----------------------------------------------------------------------------
 */

void
TncAttDeclCommand (userData, elname, attname, att_type, dflt, isrequired)
    void       *userData;
    const char *elname;
    const char *attname;
    const char *att_type;
    const char *dflt;
    int         isrequired;
{
    TNC_Data *tncdata = (TNC_Data *) userData;
    Tcl_HashEntry *entryPtr, *entryPtr1;
    Tcl_HashTable *elemAtts;
    TNC_ElemAttInfo *elemAttInfo;
    TNC_AttDecl *attDecl;
    TNC_EntityInfo *entityInfo;
    int newPtr, start, i, clen;
    char *copy;

    entryPtr = Tcl_CreateHashEntry (tncdata->attDefsTables, elname, &newPtr);
    if (newPtr) {
        elemAttInfo = (TNC_ElemAttInfo *) Tcl_Alloc (sizeof (TNC_ElemAttInfo));
        elemAtts = (Tcl_HashTable *) Tcl_Alloc (sizeof (Tcl_HashTable));
        Tcl_InitHashTable (elemAtts, TCL_STRING_KEYS);
        elemAttInfo->attributes = elemAtts;
        elemAttInfo->nrOfreq = 0;
        elemAttInfo->nrOfIdAtts = 0;
        Tcl_SetHashValue (entryPtr, elemAttInfo);
    } else {
        elemAttInfo = (TNC_ElemAttInfo *) Tcl_GetHashValue (entryPtr);
        elemAtts = elemAttInfo->attributes;
    }
    entryPtr = Tcl_CreateHashEntry (elemAtts, attname, &newPtr);
    /* Multiple Attribute declarations are allowed, but later declarations
       are ignored. See rec 3.3. */
    if (newPtr) {
        attDecl = (TNC_AttDecl *) Tcl_Alloc (sizeof (TNC_AttDecl));
        if (strcmp (att_type, "CDATA") == 0) {
            attDecl->att_type = TNC_ATTTYPE_CDATA;
        }
        else if (strcmp (att_type, "ID") == 0) {
            if (elemAttInfo->nrOfIdAtts) {
                signalNotValid (userData, TNC_ERROR_MORE_THAN_ONE_ID_ATT);
                return;
            }
            elemAttInfo->nrOfIdAtts++;
            if (dflt != NULL) {
                signalNotValid (userData, TNC_ERROR_ID_ATT_DEFAULT);
                return;
            }
            attDecl->att_type = TNC_ATTTYPE_ID;
        }
        else if (strcmp (att_type, "IDREF") == 0) {
            attDecl->att_type = TNC_ATTTYPE_IDREF;
        }
        else if (strcmp (att_type, "IDREFS") == 0) {
            attDecl->att_type = TNC_ATTTYPE_IDREFS;
        }
        else if (strcmp (att_type, "ENTITY") == 0) {
            attDecl->att_type = TNC_ATTTYPE_ENTITY;
        }
        else if (strcmp (att_type, "ENTITIES") == 0) {
            attDecl->att_type = TNC_ATTTYPE_ENTITIES;
        }
        else if (strcmp (att_type, "NMTOKEN") == 0) {
            attDecl->att_type = TNC_ATTTYPE_NMTOKEN;
        }
        else if (strcmp (att_type, "NMTOKENS") == 0) {
            attDecl->att_type = TNC_ATTTYPE_NMTOKENS;
        }
        else if (strncmp (att_type, "NOTATION(", 9) == 0) {
            /* This is a bit puzzling. expat returns something like
               <!NOTATION gif PUBLIC "gif">
               <!ATTLIST c type NOTATION (gif) #IMPLIED>
               as att_type "NOTATION(gif)". */
            attDecl->att_type = TNC_ATTTYPE_NOTATION;
            attDecl->lookupTable =
                (Tcl_HashTable *) Tcl_Alloc (sizeof (Tcl_HashTable));
            Tcl_InitHashTable (attDecl->lookupTable, TCL_STRING_KEYS);
            copy = strdup (att_type);
            start = i = 9;
            while (i) {
                if (copy[i] == ')') {
                    copy[i] = '\0';
#ifdef TNC_DEBUG
                    printf ("att type NOTATION: notation %s allowed\n",
                            &copy[start]);
#endif
                    Tcl_CreateHashEntry (attDecl->lookupTable,
                                                    &copy[start], &newPtr);
                    entryPtr1 = Tcl_CreateHashEntry (tncdata->notationDecls,
                                                    &copy[start], &newPtr);
                    if (newPtr) {
#ifdef TNC_DEBUG
                        printf ("up to now unknown NOTATION\n");
#endif
                        Tcl_SetHashValue (entryPtr1, &zero);
                    }
#ifdef TNC_DEBUG
                    else {
                        printf ("NOTATION already known, value %p\n",
                                Tcl_GetHashValue (entryPtr1));
                    }
#endif
                    free (copy);
                    break;
                }
                if (copy[i] == '|') {
                    copy[i] = '\0';
#ifdef TNC_DEBUG
                    printf ("att type NOTATION: notation %s allowed\n",
                            &copy[start]);
#endif
                    Tcl_CreateHashEntry (attDecl->lookupTable,
                                                    &copy[start], &newPtr);
                    entryPtr1 = Tcl_CreateHashEntry (tncdata->notationDecls,
                                                    &copy[start], &newPtr);
                    if (newPtr) {
#ifdef TNC_DEBUG
                        printf ("up to now unknown NOTATION\n");
#endif
                        Tcl_SetHashValue (entryPtr1, &zero);
                    }
#ifdef TNC_DEBUG
                    else {
                        printf ("NOTATION already known, value %p\n",
                                Tcl_GetHashValue (entryPtr1));
                    }
#endif
                    start = ++i;
                    continue;
                }
                UTF8_CHAR_LEN_COPY (copy[i]);
                if (!UTF8_GET_NAMING_NMTOKEN (&copy[i], clen)) {
                    signalNotValid (userData, TNC_ERROR_NMTOKEN_REQUIRED);
                    free (copy);
                    return;
                }
                i += clen;
            }
        }
        else {
            /* expat returns something like
               <!ATTLIST a type (  numbered
                   |bullets ) #IMPLIED>
               as att_type "(numbered|bullets)", e.g. in some
               "non-official" normalized way.
               Makes things easier for us. */
            attDecl->att_type = TNC_ATTTYPE_ENUMERATION;
            attDecl->lookupTable =
                (Tcl_HashTable *) Tcl_Alloc (sizeof (Tcl_HashTable));
            Tcl_InitHashTable (attDecl->lookupTable, TCL_STRING_KEYS);
            copy = strdup (att_type);
            start = i = 1;
            while (1) {
                if (copy[i] == ')') {
                    copy[i] = '\0';
                    Tcl_CreateHashEntry (attDecl->lookupTable,
                                                    &copy[start], &newPtr);
                    free (copy);
                    break;
                }
                if (copy[i] == '|') {
                    copy[i] = '\0';
                    Tcl_CreateHashEntry (attDecl->lookupTable,
                                                    &copy[start], &newPtr);
                    start = ++i;
                    continue;
                }
                UTF8_CHAR_LEN_COPY (copy[i]);
                if (!UTF8_GET_NAMING_NMTOKEN (&copy[i], clen)) {
                    signalNotValid (userData, TNC_ERROR_NMTOKEN_REQUIRED);
                    free (copy);
                    return;
                }
                i += clen;
            }
        }
        if (dflt != NULL) {
            switch (attDecl->att_type) {
            case TNC_ATTTYPE_ENTITY:
            case TNC_ATTTYPE_IDREF:
                UTF8_CHAR_LEN (*dflt);
                if (!UTF8_GET_NAMING_NAME (dflt, clen)) {
                    signalNotValid (userData, TNC_ERROR_NAME_REQUIRED);
                    return;
                }
                i = clen;
                while (1) {
                    if (dflt[i] == '\0') {
                        break;
                    }
                    UTF8_CHAR_LEN (dflt[i]);
                    if (!UTF8_GET_NAMING_NMTOKEN (&dflt[i], clen)) {
                        signalNotValid (userData, TNC_ERROR_NAME_REQUIRED);
                        return;
                    }
                    i += clen;
                }
                if (attDecl->att_type == TNC_ATTTYPE_ENTITY) {
                    entryPtr1 = Tcl_CreateHashEntry (tncdata->entityDecls,
                                                     dflt, &newPtr);
                    if (newPtr) {
                        Tcl_SetHashValue (entryPtr1, &zero);
                    } else {
                        entityInfo =
                            (TNC_EntityInfo *) Tcl_GetHashValue (entryPtr1);
                        if (!entityInfo->is_notation) {
                            signalNotValid (userData,TNC_ERROR_ATT_ENTITY_DEFAULT_MUST_BE_DECLARED);
                        }
                    }
                }
                break;
            case TNC_ATTTYPE_IDREFS:
                start = i = 0;
                while (1) {
                    if (dflt[i] == '\0') {
                        break;
                    }
                    if (dflt[i] == ' ') {
                        start = ++i;
                    }
                    if (start == i) {
                        UTF8_CHAR_LEN (dflt[i]);
                        if (!UTF8_GET_NAMING_NAME (&dflt[i], clen)) {
                            signalNotValid (userData, TNC_ERROR_NAME_REQUIRED);
                            return;
                        }
                        i += clen;
                    }
                    else {
                        UTF8_CHAR_LEN (dflt[i]);
                        if (!UTF8_GET_NAMING_NMTOKEN (&dflt[i], clen)) {
                            signalNotValid (userData, TNC_ERROR_NAME_REQUIRED);
                            return;
                        }
                        i += clen;
                    }
                }
                break;
            case TNC_ATTTYPE_ENTITIES:
                copy = strdup (dflt);
                start = i = 0;
                while (1) {
                    if (copy[i] == '\0') {
                        free (copy);
                        break;
                    }
                    if (copy[i] == ' ') {
                        copy[i] = '\0';
                        entryPtr1 = Tcl_CreateHashEntry (tncdata->entityDecls,
                                                         &copy[start],
                                                         &newPtr);
                        if (newPtr) {
                            Tcl_SetHashValue (entryPtr1, &zero);
                        } else {
                            entityInfo =
                                (TNC_EntityInfo *) Tcl_GetHashValue (entryPtr1);
                            if (!entityInfo->is_notation) {
                                signalNotValid (userData,TNC_ERROR_ATT_ENTITY_DEFAULT_MUST_BE_DECLARED);
                            }
                        }
                        start = ++i;
                    }
                    if (start == i) {
                        UTF8_CHAR_LEN_COPY (copy[i]);
                        if (!UTF8_GET_NAMING_NAME (&copy[i], clen)) {
                            signalNotValid (userData, TNC_ERROR_NAME_REQUIRED);
                            free (copy);
                            return;
                        }
                        i += clen;
                    }
                    else {
                        UTF8_CHAR_LEN_COPY (copy[i]);
                        if (!UTF8_GET_NAMING_NMTOKEN (&copy[i], clen)) {
                            signalNotValid (userData, TNC_ERROR_NAME_REQUIRED);
                            free (copy);
                            return;
                        }
                        i += clen;
                    }
                }
                break;
            case TNC_ATTTYPE_NMTOKEN:
                i = 0;
                while (1) {
                    if (dflt[i] == '\0') {
                        break;
                    }
                    UTF8_CHAR_LEN (dflt[i]);
                    if (!UTF8_GET_NAMING_NMTOKEN (&dflt[i], clen)) {
                        signalNotValid (userData, TNC_ERROR_NMTOKEN_REQUIRED);
                        return;
                    }
                    i += clen;
                }
                if (!i) signalNotValid (userData, TNC_ERROR_NMTOKEN_REQUIRED);
                break;
            case TNC_ATTTYPE_NMTOKENS:
                i = 0;
                while (1) {
                    if (dflt[i] == '\0') {
                        break;
                    }
                    if (dflt[i] == ' ') {
                        i++;
                    }
                    UTF8_CHAR_LEN (dflt[i]);
                    if (!UTF8_GET_NAMING_NMTOKEN (&dflt[i], clen)) {
                        signalNotValid (userData, TNC_ERROR_NMTOKEN_REQUIRED);
                        return;
                    }
                    i += clen;
                }
                if (!i) signalNotValid (userData, TNC_ERROR_NMTOKEN_REQUIRED);
                break;
            case TNC_ATTTYPE_NOTATION:
                if (!Tcl_FindHashEntry (attDecl->lookupTable, dflt)) {
                    signalNotValid (userData, TNC_ERROR_UNIMPOSSIBLE_DEFAULT);
                    return;
                }
            case TNC_ATTTYPE_ENUMERATION:
                if (!Tcl_FindHashEntry (attDecl->lookupTable, dflt)) {
                    signalNotValid (userData, TNC_ERROR_UNIMPOSSIBLE_DEFAULT);
                    return;
                }
            case TNC_ATTTYPE_CDATA:
            case TNC_ATTTYPE_ID:
                /* This both cases are only there, to pacify -Wall.
                   CDATA may have any allowed characters (and
                   everything else is detected by extpat).  ID's not
                   allowed to have defaults (handled above). */
                ;
            }
            attDecl->dflt = strdup (dflt);
        }
        else {
            attDecl->dflt = NULL;
        }
        if (isrequired) {
            elemAttInfo->nrOfreq++;
        }
        attDecl->isrequired = isrequired;
        Tcl_SetHashValue (entryPtr, attDecl);
    }
}

#ifdef TNC_DEBUG

void
printNameIDs (TNC_Data *tncdata)
{
    Tcl_HashEntry *entryPtr;
    Tcl_HashSearch search;

    for (entryPtr = Tcl_FirstHashEntry (tncdata->tagNames, &search);
         entryPtr != NULL;
         entryPtr = Tcl_NextHashEntry (&search)) {
        printf ("name: %-20s   nameId: %p\n",
                Tcl_GetHashKey (tncdata->tagNames, entryPtr),
                entryPtr);
    }
}

void
printStackElm (TNC_ContentStack *stackelm)
{
    if (stackelm->model->type == XML_CTYPE_NAME) {
        printf ("\tmodel %p\tNAME: %p\n\tactiveChild %d\n\tdeep %d\n\talreadymatched %d\n",
                stackelm->model, stackelm->model->nameId,
                stackelm->activeChild, stackelm->deep, stackelm->alreadymatched);
    }
    else {
        printf ("\tmodel %p\n\tactiveChild %d\n\tdeep %d\n\talreadymatched %d\n",
                stackelm->model, stackelm->activeChild, stackelm->deep,
                stackelm->alreadymatched);
    }
}

void
printTNC_Content (TNC_Content *model)
    {
        printf ("TNC_Content..\n\ttype %d\n\tquant %d\n\tnameId %p\n\tnumchildren %d\n\tchildren %p\n", model->type, model->quant, model->nameId,
            model->numchildren, model->children);
}

void
printContentStack (TNC_Data *tncdata)
{
    TNC_ContentStack stackelm;
    int i;

    printf ("Current contentStack state (used stack slots %d):\n",
            tncdata->contentStackPtr);
    for (i = 0; i < tncdata->contentStackPtr; i++) {
        stackelm = tncdata->contentStack[i];
        printf ("%3d:\n", i);
        printStackElm (&stackelm);
    }
}
#endif /* TNC_DEBUG */


/*
 *----------------------------------------------------------------------------
 *
 * TncProbeElement --
 *
 *	This procedure checks, if the element match the
 *      content model.
 *
 * Results:
 *	1 if the element match,
 *      0 if not.
 *
 * Side effects:
 *	Eventually pushes data to the contentStack (even in
 *      recurive calls).
 *
 *----------------------------------------------------------------------------
 */

static int
TncProbeElement (nameId, tncdata)
    TNC_NameId *nameId;
    TNC_Data   *tncdata;
{
    TNC_ContentStack *stackelm;
    TNC_Content *activeModel;
    int i, seqstartindex, myStackPtr, zeroMatchPossible, result;

#ifdef TNC_DEBUG
    printf ("TncProbeElement start\n");
    printContentStack (tncdata);
#endif
    myStackPtr = tncdata->contentStackPtr - 1;
    stackelm = &(tncdata->contentStack)[myStackPtr];
    switch (stackelm->model->type) {
    case XML_CTYPE_MIXED:
#ifdef TNC_DEBUG
        printf ("TncProbeElement XML_CTYPE_MIXED\n");
#endif
        for (i = 0; i < stackelm->model->numchildren; i++) {
            if ((&stackelm->model->children[i])->nameId == nameId) {
                return 1;
            }
        }
        return 0;
    case XML_CTYPE_ANY:
#ifdef TNC_DEBUG
        printf ("TncProbeElement XML_CTYPE_ANY\n");
#endif
        return 1;
    case XML_CTYPE_EMPTY:
#ifdef TNC_DEBUG
        printf ("TncProbeElement XML_CTYPE_EMPTY\n");
#endif
        return 0;
    case XML_CTYPE_CHOICE:
#ifdef TNC_DEBUG
        printf ("TncProbeElement XML_CTYPE_CHOICE\n");
#endif
        if (stackelm->alreadymatched) {
            activeModel = &stackelm->model->children[stackelm->activeChild];
            if (activeModel->type == XML_CTYPE_NAME) {
                /* so this stackelement must be the topmost */
                if (activeModel->quant == XML_CQUANT_REP
                    || activeModel->quant == XML_CQUANT_PLUS) {
                    /* the last matched element is multiple, maybe it
                       matches again */
                    if (nameId == activeModel->nameId) {
#ifdef TNC_DEBUG
                        printf ("-->matched! child Nr. %d\n",
                                stackelm->activeChild);
#endif
                        /* stack and activeChild nr. are already OK, just
                           report success. */
                        return 1;
                    }
                }
            }
            /* The active child is a SEQ or CHOICE. */
            if (stackelm->model->quant == XML_CQUANT_NONE ||
                stackelm->model->quant == XML_CQUANT_OPT) {
                /*The child cp's type SEQ or CHOICE keep track by
                  themselve about if they are repeated. Because we are
                  here, they don't.  Since the current cp has already
                  matched and isn't multiple, the current cp as a whole
                  is done.  But no contradiction detected, so return
                  "search futher" */
                return -1;
            }
        }

        /* If one of the alternatives within the CHOICE cp is quant
           REP or OPT, it isn't a contradition to the document structure,
           if the cp doesn't match, even if it is quant
           NONE or PLUS, because of the "zero time" match of this one
           alternative. We use zeroMatchPossible, to know about this.*/
        zeroMatchPossible = 0;
        for (i = 0; i < stackelm->model->numchildren; i++) {
            if ((&stackelm->model->children[i])->type == XML_CTYPE_NAME) {
#ifdef TNC_DEBUG
                printf ("child is type NAME\n");
#endif
                if ((&stackelm->model->children[i])->nameId == nameId) {
#ifdef TNC_DEBUG
                    printf ("-->matched! child Nr. %d\n",i);
#endif
                    (&tncdata->contentStack[myStackPtr])->activeChild = i;
                    (&tncdata->contentStack[myStackPtr])->alreadymatched = 1;
                    return 1;
                }
                else {
                    /* If the name child is optional, we have a
                       candidat for "zero match". */
                    if ((&stackelm->model->children[i])->quant
                        == XML_CQUANT_OPT ||
                        (&stackelm->model->children[i])->quant
                        == XML_CQUANT_REP) {
#ifdef TNC_DEBUG
                        printf ("zero match possible\n");
#endif
                        zeroMatchPossible = 1;
                    }
                }
            }
            else {
#ifdef TNC_DEBUG
                printf ("complex child type\n");
#endif
                if (tncdata->contentStackPtr == tncdata->contentStackSize) {
                    tncdata->contentStack = (TNC_ContentStack *)
                        Tcl_Realloc ((char *)tncdata->contentStack,
                                     sizeof (TNC_Content *) * 2 *
                                     tncdata->contentStackSize);
                    tncdata->contentStackSize *= 2;
                }
                (&tncdata->contentStack[tncdata->contentStackPtr])->model
                    = &stackelm->model->children[i];
                tncdata->contentStack[tncdata->contentStackPtr].activeChild
                    = 0;
                tncdata->contentStack[tncdata->contentStackPtr].deep
                    = stackelm->deep + 1;
                tncdata->contentStack[tncdata->contentStackPtr].alreadymatched
                    = 0;
                tncdata->contentStackPtr++;
                result = TncProbeElement (nameId, tncdata);
                if (result == 1) {
#ifdef TNC_DEBUG
                    printf ("-->matched! child nr. %d\n",i);
#endif
                    (&tncdata->contentStack[myStackPtr])->activeChild = i;
                    (&tncdata->contentStack[myStackPtr])->alreadymatched = 1;
                    return 1;
                }
                /* The child cp says, it doesn't has matched, but says
                   also, it's perfectly OK, if it doesn't at all. So we
                   have a candidat for "zero match". */
                if (result == -1) {
                    zeroMatchPossible = 1;
                }
                tncdata->contentStackPtr--;
            }
        }
        /* OK, nobody has claimed a match. Question is: try futher or is
           this a document structure error. */
        if (zeroMatchPossible ||
            stackelm->alreadymatched ||
            stackelm->model->quant == XML_CQUANT_REP ||
            stackelm->model->quant == XML_CQUANT_OPT) {
            return -1;
        }
#ifdef TNC_DEBUG
        printf ("validation error\n");
#endif
        return 0;
    case XML_CTYPE_SEQ:
#ifdef TNC_DEBUG
        printf ("TncProbeElement XML_CTYPE_SEQ\n");
#endif
        if (stackelm->alreadymatched) {
            activeModel = &stackelm->model->children[stackelm->activeChild];
            if (activeModel->type == XML_CTYPE_NAME) {
                /* so this stackelement must be the topmost */
                if (activeModel->quant == XML_CQUANT_REP
                    || activeModel->quant == XML_CQUANT_PLUS) {
                    /* the last matched element is multiple, maybe it
                       matches again */
                    if (nameId == activeModel->nameId) {
#ifdef TNC_DEBUG
                        printf ("-->matched! child Nr. %d\n",
                                stackelm->activeChild);
#endif
                        /* stack and activeChild nr. are already OK, just
                           report success. */
                        return 1;
                    }
                }
            }
        }

        if (stackelm->alreadymatched) {
            seqstartindex = stackelm->activeChild + 1;
        }
        else {
            seqstartindex = 0;
        }
        /* This time zeroMatchPossible flags, if every of the remaining
           childs - that may every child, if !alreadymatched - doesn't
           must occur.  We assume, the (outstanding childs of, in case
           of alreadymatched) current stackelement model has only
           optional childs, and set to wrong, if we find any
           non-optional child */
        zeroMatchPossible = 1;
        for (i = seqstartindex; i < stackelm->model->numchildren; i++) {
            if ((&stackelm->model->children[i])->type == XML_CTYPE_NAME) {
                if ((&stackelm->model->children[i])->nameId == nameId) {
#ifdef TNC_DEBUG
                    printf ("-->matched! child Nr. %d\n",i);
#endif
                    (&tncdata->contentStack[myStackPtr])->activeChild = i;
                    (&tncdata->contentStack[myStackPtr])->alreadymatched = 1;
                    return 1;
                } else if ((&stackelm->model->children[i])->quant
                           == XML_CQUANT_NONE
                           || (&stackelm->model->children[i])->quant
                               == XML_CQUANT_PLUS) {
                    zeroMatchPossible = 0;
                    break;
                }
            } else {
                if (tncdata->contentStackPtr == tncdata->contentStackSize) {
                    tncdata->contentStack = (TNC_ContentStack *)
                        Tcl_Realloc ((char *)tncdata->contentStack,
                                     sizeof (TNC_Content *) * 2 *
                                     tncdata->contentStackSize);
                    tncdata->contentStackSize *= 2;
                }
                (&tncdata->contentStack[tncdata->contentStackPtr])->model =
                    &stackelm->model->children[i];
                tncdata->contentStack[tncdata->contentStackPtr].activeChild
                    = 0;
                tncdata->contentStack[tncdata->contentStackPtr].deep
                    = stackelm->deep + 1;
                tncdata->contentStack[tncdata->contentStackPtr].alreadymatched
                    = 0;
                tncdata->contentStackPtr++;
                result = TncProbeElement (nameId, tncdata);
                if (result == 1) {
                    (&tncdata->contentStack[myStackPtr])->activeChild = i;
                    (&tncdata->contentStack[myStackPtr])->alreadymatched = 1;
                    return 1;
                }
                tncdata->contentStackPtr--;
                if (result == 0) {
                    zeroMatchPossible = 0;
                    break;
                }
            }
        }
        if (!stackelm->alreadymatched) {
            if (zeroMatchPossible) {
                /* The stackelm hasn't matched, but don't have to
                   after all.  Return try futher */
                return -1;
            } else {
                /* No previous match, but at least one child is
                   necessary. Return depends of the quant of the
                   entire seq */
                if (stackelm->model->quant == XML_CQUANT_NONE ||
                    stackelm->model->quant == XML_CQUANT_PLUS) {
                    /* DTD claims, the seq as to be there, but isn't */
                    return 0;
                } else {
                    /* The seq is optional */
                    return -1;
                }
            }
        }
        if (stackelm->alreadymatched) {
            if (!zeroMatchPossible) {
                /* Some child at the start of the seq has matched in
                   the past, but since zeroMatchPossible has changed
                   to zero, there must be a non-matching non-optional
                   child later. Error in document structure. */
                return 0;
            } else {
                /* OK, SEQ has matched befor. But after the last match, there
                   where no required (quant NONE or PLUS) childs. */
                if (stackelm->model->quant == XML_CQUANT_NONE ||
                    stackelm->model->quant == XML_CQUANT_OPT) {
                    /* The entire seq isn't multiple. Just look futher. */
                    return -1;
                }
            }
        }
        /* The last untreated case is alreadymatched true,
           zeroMatchPossible (of the rest of the seq childs after the
           last match) true and the entire seq may be
           multiple. Therefore start again with activeChild = 0, to
           see, if the current nameId starts a repeated match of the
           seq.  By the way: zeroMatchPossible still has inital value
           1, therefor no second initaliation is needed */
        for (i = 0; i < seqstartindex; i++) {
            if ((&stackelm->model->children[i])->type == XML_CTYPE_NAME) {
                if ((&stackelm->model->children[i])->nameId == nameId) {
#ifdef TNC_DEBUG
                    printf ("-->matched! child Nr. %d\n",i);
#endif
                    (&tncdata->contentStack[myStackPtr])->activeChild = i;
                    (&tncdata->contentStack[myStackPtr])->alreadymatched = 1;
                    return 1;
                } else if ((&stackelm->model->children[i])->quant
                           == XML_CQUANT_NONE
                           || (&stackelm->model->children[i])->quant
                               == XML_CQUANT_PLUS) {
                    zeroMatchPossible = 0;
                    break;
                }
            } else {
                if (tncdata->contentStackPtr == tncdata->contentStackSize) {
                    tncdata->contentStack = (TNC_ContentStack *)
                        Tcl_Realloc ((char *)tncdata->contentStack,
                                     sizeof (TNC_Content *) * 2 *
                                     tncdata->contentStackSize);
                    tncdata->contentStackSize *= 2;
                }
                (&tncdata->contentStack[tncdata->contentStackPtr])->model =
                    &stackelm->model->children[i];
                tncdata->contentStack[tncdata->contentStackPtr].activeChild
                    = 0;
                tncdata->contentStack[tncdata->contentStackPtr].deep
                    = stackelm->deep + 1;
                tncdata->contentStack[tncdata->contentStackPtr].alreadymatched
                    = 0;
                tncdata->contentStackPtr++;
                result = TncProbeElement (nameId, tncdata);
                if (result) {
                    (&tncdata->contentStack[myStackPtr])->activeChild = i;
                    /* alreadymatched is already 1 */
                    return 1;
                }
                tncdata->contentStackPtr--;
                if (result == 0) {
                    /* OK, the seq doesn't match again. But since it have
                       already matched, this isn't return 0 but.. */
                    return -1;
                }
            }
        }
        /* seq doesn't match again and every seq child from the very first
           up to (not including) the last match aren't required. This last
           fact may be nice to know, but after all since the entire seq have
           matched already ... */
        return -1;
    case XML_CTYPE_NAME:
        /* NAME type dosen't occur at top level of a content model and is
           handled in some "shotcut" way directly in the CHOICE and SEQ cases.
           It's only here to pacify gcc -Wall. */
        printf ("error!!! - in TncProbeElement: XML_CTYPE_NAME shouldn't reached in any case.\n");
    default:
        printf ("error!!! - in TncProbeElement: unknown content type: %d\n",
                stackelm->model->type);
    }
    /* not reached */
    printf ("error!!! - in TncProbeElement: end of function reached.\n");
    return 0;
}


/*
 *----------------------------------------------------------------------------
 *
 * TncElementStartCommand --
 *
 *	This procedure is called for every element start event
 *      while parsing XML Data with a "tnc" enabled tclexpat
 *      parser. Checks, if the element can occur here and if it
 *      has an acceptable set of attributes.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Eventually signals application error.
 *
 *----------------------------------------------------------------------------
 */

void
TncElementStartCommand (userData, name, atts)
    void *userData;
    const char *name;
    const char **atts;
{
    TNC_Data *tncdata = (TNC_Data *) userData;
    Tcl_HashEntry *entryPtr;
    Tcl_HashTable *elemAtts;
    const char **atPtr;
    TNC_ElemAttInfo *elemAttInfo;
    TNC_AttDecl *attDecl;
    TNC_EntityInfo *entityInfo;
    TNC_Content *model;
    int i, clen, nrOfreq, start, result;
    char *pc, *copy;

#ifdef TNC_DEBUG
    printf ("TncElementStartCommand name: %s\n", name);
#endif
    entryPtr = Tcl_FindHashEntry (tncdata->tagNames, name);
    if (!entryPtr) {
        signalNotValid (userData, TNC_ERROR_UNKNOWN_ELEMENT);
        return;
    }
    model = (TNC_Content *) Tcl_GetHashValue (entryPtr);
    switch (model->type) {
    case XML_CTYPE_MIXED:
    case XML_CTYPE_ANY:
        tncdata->ignoreWhiteCDATAs = 1;
        tncdata->ignorePCDATA = 1;
        break;
    case XML_CTYPE_EMPTY:
        tncdata->ignoreWhiteCDATAs = 0;
        break;
    case XML_CTYPE_CHOICE:
    case XML_CTYPE_SEQ:
        tncdata->ignoreWhiteCDATAs = 1;
        tncdata->ignorePCDATA = 0;
        break;
    case XML_CTYPE_NAME:
        break;
    }

    if (tncdata->contentStackPtr) {
        /* This is the normal case, within some content,
           at least the root element content. */
        while (1) {
            result = TncProbeElement (entryPtr, tncdata);
            if (result == -1) {
                if (tncdata->contentStack[tncdata->contentStackPtr - 1].deep
                    == 0) {
                    signalNotValid (userData,
                                    TNC_ERROR_ELEMENT_NOT_ALLOWED_HERE);
                    return;
                }
                tncdata->contentStackPtr--;
                continue;
            }
            if (result) {
                break;
            }
            if (!result) {
                signalNotValid (userData, TNC_ERROR_ELEMENT_NOT_ALLOWED_HERE);
                return;
            }
        }
        if (tncdata->contentStackPtr == tncdata->contentStackSize) {
            tncdata->contentStackSize *= 2;
            tncdata->contentStack = (TNC_ContentStack *)
                Tcl_Realloc ((char *)tncdata->contentStack,
                             sizeof (TNC_Content *)*tncdata->contentStackSize);
        }
        (&tncdata->contentStack[tncdata->contentStackPtr])->model = model;
        (&tncdata->contentStack[tncdata->contentStackPtr])->activeChild = 0;
        (&tncdata->contentStack[tncdata->contentStackPtr])->deep = 0;
        (&tncdata->contentStack[tncdata->contentStackPtr])->alreadymatched = 0;
        tncdata->contentStackPtr++;
    } else {
        /* This is only in case of the root element */
        if (!tncdata->doctypeName) {
            signalNotValid (userData, TNC_ERROR_NO_DOCTYPE_DECL);
            return;
        }
        if (strcmp (tncdata->doctypeName, name) != 0) {
            signalNotValid (userData, TNC_ERROR_WRONG_ROOT_ELEMENT);
            return;
        }
        (&(tncdata->contentStack)[0])->model = model;
        (&(tncdata->contentStack)[0])->activeChild = 0;
        (&(tncdata->contentStack)[0])->deep = 0;
        (&(tncdata->contentStack)[0])->alreadymatched = 0;
        tncdata->contentStackPtr++;
    }

    entryPtr = Tcl_FindHashEntry (tncdata->attDefsTables, name);
    if (!entryPtr) {
        if (atts[0] != NULL) {
            signalNotValid (userData, TNC_ERROR_NO_ATTRIBUTES);
            return;
        }
    } else {
        elemAttInfo = (TNC_ElemAttInfo *) Tcl_GetHashValue (entryPtr);
        elemAtts = elemAttInfo->attributes;
        nrOfreq = 0;
        for (atPtr = atts; atPtr[0]; atPtr += 2) {
            entryPtr = Tcl_FindHashEntry (elemAtts, atPtr[0]);
            if (!entryPtr) {
                signalNotValid (userData, TNC_ERROR_UNKOWN_ATTRIBUTE);
                return;
            }
            /* NOTE: attribute uniqueness per element is a wellformed
               constrain and therefor done by expat. */
            attDecl = (TNC_AttDecl *) Tcl_GetHashValue (entryPtr);
            switch (attDecl->att_type) {
            case TNC_ATTTYPE_CDATA:
                if (attDecl->isrequired && attDecl->dflt) {
                    if (strcmp (attDecl->dflt, atPtr[1]) != 0) {
                        signalNotValid (userData,
                                        TNC_ERROR_WRONG_FIXED_ATTVALUE);
                        return;
                    }
                }
                break;
            case TNC_ATTTYPE_ID:
                pc = (char*)atPtr[1];
                UTF8_CHAR_LEN (*pc);
                if (!UTF8_GET_NAMING_NAME (pc, clen)) {
                    signalNotValid (userData, TNC_ERROR_NAME_REQUIRED);
                }
                pc += clen;
                while (1) {
                    if (*pc == '\0') {
                        break;
                    }
                    UTF8_CHAR_LEN (*pc);
                    if (!UTF8_GET_NAMING_NMTOKEN (pc, clen)) {
                        signalNotValid (userData, TNC_ERROR_NAME_REQUIRED);
                        return;
                    }
                    pc += clen;
                }
                entryPtr = Tcl_CreateHashEntry (tncdata->ids, atPtr[1], &i);
                if (!i) {
                    if (Tcl_GetHashValue (entryPtr) == &one) {
                        signalNotValid (userData,
                                        TNC_ERROR_DUPLICATE_ID_VALUE);
                        return;
                    }
                }
                Tcl_SetHashValue (entryPtr, &one);
                break;
            case TNC_ATTTYPE_IDREF:
                /* Name type constraint "implicit" checked. If the
                   referenced ID exists, the type must be OK, because the
                   type of the ID's within the document are checked.
                   If there isn't such an ID, it's an error anyway. */
                entryPtr = Tcl_CreateHashEntry (tncdata->ids, atPtr[1], &i);
                if (i) {
                    Tcl_SetHashValue (entryPtr, &zero);
                }
                break;
            case TNC_ATTTYPE_IDREFS:
                copy = strdup (atPtr[1]);
                start = i = 0;
                while (1) {
                    if (copy[i] == '\0') {
                        entryPtr = Tcl_CreateHashEntry (tncdata->ids,
                                                        &copy[start], &result);
                        if (result) {
                            Tcl_SetHashValue (entryPtr, &zero);
                        }
                        free (copy);
                        break;
                    }
                    if (copy[i] == ' ') {
                        copy[i] = '\0';
                        entryPtr = Tcl_CreateHashEntry (tncdata->ids,
                                                        &copy[start], &result);
                        if (result) {
                            Tcl_SetHashValue (entryPtr, &zero);
                        }
                        start = ++i;
                        continue;
                    }
                    i++;
                }
                break;
            case TNC_ATTTYPE_ENTITY:
                /* There is a validity constraint requesting entity attributes
                   values to be type Name. But if there would be an entity
                   declaration that doesn't fit this constraint, expat would
                   have already complained about the definition. So we go the
                   easy way and just look up the att value. If it's declared,
                   type must be OK, if not, it's an error anyway. */
                entryPtr = Tcl_FindHashEntry (tncdata->entityDecls, atPtr[1]);
                if (!entryPtr) {
                    signalNotValid (userData, TNC_ERROR_ENTITY_ATTRIBUTE);
                    return;
                }
                entityInfo = (TNC_EntityInfo *) Tcl_GetHashValue (entryPtr);
                if (!entityInfo->is_notation) {
                    signalNotValid (userData, TNC_ERROR_ENTITY_ATTRIBUTE);
                    return;
                }
                break;
            case TNC_ATTTYPE_ENTITIES:
                /* Normalized by exapt; for type see comment to
                   TNC_ATTTYPE_ENTITIE */
                copy = strdup (atPtr[1]);
                start = i = 0;
                while (1) {
                    if (copy[i] == '\0') {
                        entryPtr = Tcl_FindHashEntry (tncdata->entityDecls,
                                                      &copy[start]);
                        if (!entryPtr) {
                            signalNotValid (userData, TNC_ERROR_ENTITIES_ATTRIBUTE);
                            free (copy);
                            return;
                        }
                        entityInfo = (TNC_EntityInfo *) Tcl_GetHashValue (entryPtr);
                        if (!entityInfo->is_notation) {
                            signalNotValid (userData, TNC_ERROR_ENTITIES_ATTRIBUTE);
                            free (copy);
                            return;
                        }
                        free (copy);
                        break;
                    }
                    if (copy[i] == ' ') {
                        copy[i] = '\0';
                        entryPtr = Tcl_FindHashEntry (tncdata->entityDecls,
                                                      &copy[start]);
                        if (!entryPtr) {
                            signalNotValid (userData, TNC_ERROR_ENTITIES_ATTRIBUTE);
                            free (copy);
                            return;
                        }
                        entityInfo = (TNC_EntityInfo *) Tcl_GetHashValue (entryPtr);
                        if (!entityInfo->is_notation) {
                            signalNotValid (userData, TNC_ERROR_ENTITIES_ATTRIBUTE);
                            free (copy);
                            return;
                        }
                        start = ++i;
                        continue;
                    }
                    i++;
                }
                break;
            case TNC_ATTTYPE_NMTOKEN:
                /* We assume, that the UTF-8 representation of the value is
                   valid (no partial chars, minimum encoding). This makes
                   things a little more easy and faster. I guess (but
                   haven't deeply checked - QUESTION -), expat would have
                   already complained otherwise. */
                pc = (char*)atPtr[1];
                clen = 0;
                while (1) {
                    if (*pc == '\0') {
                        break;
                    }
                    UTF8_CHAR_LEN (*pc);
                    if (!UTF8_GET_NAMING_NMTOKEN (pc, clen)) {
                        signalNotValid (userData, TNC_ERROR_NMTOKEN_REQUIRED);
                        return;
                    }
                    pc += clen;
                }
                if (!clen) 
                    signalNotValid (userData, TNC_ERROR_NMTOKEN_REQUIRED);
                break;
            case TNC_ATTTYPE_NMTOKENS:
                pc = (char*)atPtr[1];
                clen = 0;
                while (1) {
                    if (*pc == '\0') {
                        break;
                    }
                    /* NMTOKENS are normalized by expat, so this should
                       be secure. */
                    if (*pc == ' ') {
                        pc++;
                    }
                    UTF8_CHAR_LEN (*pc);
                    if (!UTF8_GET_NAMING_NMTOKEN (pc, clen)) {
                        signalNotValid (userData, TNC_ERROR_NMTOKEN_REQUIRED);
                        return;
                    }
                    pc += clen;
                }
                if (!clen)
                    signalNotValid (userData, TNC_ERROR_NMTOKEN_REQUIRED);
                break;
            case TNC_ATTTYPE_NOTATION:
                entryPtr = Tcl_FindHashEntry (attDecl->lookupTable, atPtr[1]);
                if (!entryPtr) {
                    signalNotValid (userData, TNC_ERROR_NOTATION_REQUIRED);
                    return;
                }
                break;
            case TNC_ATTTYPE_ENUMERATION:
                if (!Tcl_FindHashEntry (attDecl->lookupTable, atPtr[1])) {
                    signalNotValid (userData, TNC_ERROR_ENUM_ATT_WRONG_VALUE);
                    return;
                }
                break;
            }
            if (attDecl->isrequired) {
                nrOfreq++;
            }
        }
        if (nrOfreq != elemAttInfo->nrOfreq) {
            signalNotValid (userData, TNC_ERROR_MISSING_REQUIRED_ATTRIBUTE);
            return;
        }
    }

#ifdef TNC_DEBUG
    printf ("TncElementStartCommand end\n");
#endif
}


/*
 *----------------------------------------------------------------------------
 *
 * TncProbeElementEnd --
 *
 *	This procedure checks, if the current content allows the
 *      the element to end here.
 *
 * Results:
 *	1 if element end is OK,
 *      0 if not.
 *
 * Side effects:
 *	Let the contentStackPtr point to the last current content
 *      model before the element had started.
 *
 *----------------------------------------------------------------------------
 */

static int
TncProbeElementEnd (tncdata)
    TNC_Data *tncdata;
{
    TNC_ContentStack stackelm;
    int i, zeroMatchPossible, seqstartindex;

    stackelm = tncdata->contentStack[tncdata->contentStackPtr - 1];
    switch (stackelm.model->type) {
    case XML_CTYPE_MIXED:
    case XML_CTYPE_ANY:
    case XML_CTYPE_EMPTY:
        return 1;
    case XML_CTYPE_CHOICE:
        if (stackelm.alreadymatched) {
            return 1;
        }

        if (stackelm.model->quant == XML_CQUANT_REP ||
            stackelm.model->quant == XML_CQUANT_OPT) {
            return 1;
        }
        zeroMatchPossible = 0;
        for (i = 0; i < stackelm.model->numchildren; i++) {
            if ((&stackelm.model->children[i])->type == XML_CTYPE_NAME) {
                if ((&stackelm.model->children[i])->quant == XML_CQUANT_OPT ||
                    (&stackelm.model->children[i])->quant == XML_CQUANT_REP) {
                    zeroMatchPossible = 1;
                    break;
                }
            }
            else {
                if (tncdata->contentStackPtr == tncdata->contentStackSize) {
                    tncdata->contentStack = (TNC_ContentStack *)
                        Tcl_Realloc ((char *)tncdata->contentStack,
                                     sizeof (TNC_Content *) * 2 *
                                     tncdata->contentStackSize);
                    tncdata->contentStackSize *= 2;
                }
                (&tncdata->contentStack[tncdata->contentStackPtr])->model
                    = &stackelm.model->children[i];
                tncdata->contentStack[tncdata->contentStackPtr].activeChild
                    = 0;
                tncdata->contentStack[tncdata->contentStackPtr].deep
                    = stackelm.deep + 1;
                tncdata->contentStack[tncdata->contentStackPtr].alreadymatched
                    = 0;
                tncdata->contentStackPtr++;
                if (TncProbeElementEnd (tncdata)) {
                    zeroMatchPossible = 1;
                    tncdata->contentStackPtr--;
                    break;
                }
                tncdata->contentStackPtr--;
            }
        }
        if (zeroMatchPossible) {
            return 1;
        } else {
            return 0;
        }
    case XML_CTYPE_SEQ:
        if (!stackelm.alreadymatched) {
            if (stackelm.model->quant == XML_CQUANT_REP ||
                stackelm.model->quant == XML_CQUANT_OPT) {
                return 1;
            }
        }
        if (!stackelm.alreadymatched) {
            seqstartindex = 0;
        }
        else {
            seqstartindex = stackelm.activeChild + 1;
        }
        for (i = seqstartindex; i < stackelm.model->numchildren; i++) {
            if ((&stackelm.model->children[i])->type == XML_CTYPE_NAME) {
                if ((&stackelm.model->children[i])->quant == XML_CQUANT_OPT ||
                    (&stackelm.model->children[i])->quant == XML_CQUANT_REP) {
                    continue;
                } else {
                    return 0;
                }
            } else {
                if (tncdata->contentStackPtr == tncdata->contentStackSize) {
                    tncdata->contentStack = (TNC_ContentStack *)
                        Tcl_Realloc ((char *)tncdata->contentStack,
                                     sizeof (TNC_Content *) * 2 *
                                     tncdata->contentStackSize);
                    tncdata->contentStackSize *= 2;
                }
                (&tncdata->contentStack[tncdata->contentStackPtr])->model
                    = &stackelm.model->children[i];
                tncdata->contentStack[tncdata->contentStackPtr].activeChild
                    = 0;
                tncdata->contentStack[tncdata->contentStackPtr].deep
                    = stackelm.deep + 1;
                tncdata->contentStack[tncdata->contentStackPtr].alreadymatched
                    = 0;
                tncdata->contentStackPtr++;
                if (TncProbeElementEnd (tncdata)) {
                    tncdata->contentStackPtr--;
                    continue;
                }
                else {
                    tncdata->contentStackPtr--;
                    return 0;
                }
            }
        }
        return 1;
    case XML_CTYPE_NAME:
        /* NAME type dosen't occur at top level of a content model and is
           handled in some "shotcut" way directly in the CHOICE and SEQ cases.
           It's only here to pacify gcc -Wall. */
        printf ("error!!! - in TncProbeElementEnd: XML_CTYPE_NAME shouldn't reached in any case.\n");
    default:
        printf ("error!!! - in TncProbeElementEnd: unknown content type: %d\n",
                stackelm.model->type);
        return 1;
    }
}


/*
 *----------------------------------------------------------------------------
 *
 * TncElementEndCommand --
 *
 *	This procedure is called for every element end event
 *      while parsing XML Data with a "tnc" enabled tclexpat
 *      parser. Checks, if the content model allows the element
 *      to end at this point.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Eventually signals application error.
 *
 *----------------------------------------------------------------------------
 */

void
TncElementEndCommand (userData, name)
    void       *userData;
    const char *name;
{
    TNC_Data *tncdata = (TNC_Data *) userData;
    Tcl_HashEntry *entryPtr;
    Tcl_HashSearch search;

#ifdef TNC_DEBUG
    printf ("TncElementEndCommand start\n");
    printContentStack (tncdata);
#endif
    while (1) {
        if (!TncProbeElementEnd (tncdata, 0)) {
            signalNotValid (userData, TNC_ERROR_ELEMENT_CAN_NOT_END_HERE);
            return;
        }
        if (tncdata->contentStack[tncdata->contentStackPtr - 1].deep == 0) {
            break;
        }
        tncdata->contentStackPtr--;
    }
    /* Remove the content model of the closed element from the stack */
    tncdata->contentStackPtr--;
#ifdef TNC_DEBUG
    printf ("after removing ended element from the stack\n");
    printContentStack (tncdata);
#endif
    if (tncdata->contentStackPtr) {
        switch ((&tncdata->contentStack[tncdata->contentStackPtr - 1])->model->type) {
        case XML_CTYPE_MIXED:
        case XML_CTYPE_ANY:
            tncdata->ignoreWhiteCDATAs = 1;
            tncdata->ignorePCDATA = 1;
            break;
        case XML_CTYPE_EMPTY:
            tncdata->ignoreWhiteCDATAs = 0;
            break;
        case XML_CTYPE_CHOICE:
        case XML_CTYPE_SEQ:
        case XML_CTYPE_NAME:
            tncdata->ignoreWhiteCDATAs = 1;
            tncdata->ignorePCDATA = 0;
            break;
        }
    } else {
        /* This means, the root element is closed,
           therefor the place to check, if every IDREF points
           to a ID. */
        for (entryPtr = Tcl_FirstHashEntry (tncdata->ids, &search);
             entryPtr != NULL;
             entryPtr = Tcl_NextHashEntry (&search)) {
#ifdef TNC_DEBUG
            printf ("check id value %s\n",
                    Tcl_GetHashKey (tncdata->ids, entryPtr));
            printf ("value %p\n", Tcl_GetHashValue (entryPtr));
#endif
            if (Tcl_GetHashValue (entryPtr) != &one) {
                signalNotValid (userData, TNC_ERROR_UNKOWN_ID_REFERRED);
                return;
            }
        }
    }
}


/*
 *----------------------------------------------------------------------------
 *
 * TncCharacterdataCommand --
 *
 *	This procedure is called with a piece of CDATA found in
 *      document.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Eventually signals application error.
 *
 *----------------------------------------------------------------------------
 */

void
TncCharacterdataCommand (userData, data, len)
    void       *userData;
    const char *data;
    int         len;
{
    TNC_Data *tncdata = (TNC_Data *) userData;
    int i;
    char *pc;

    if (!tncdata->ignoreWhiteCDATAs && len > 0) {
        signalNotValid (userData, TNC_ERROR_EMPTY_ELEMENT);
        return;
    }
    if (!tncdata->ignorePCDATA) {
        for (i = 0, pc = (char*)data; i < len; i++, pc++) {
            if ( (*pc == ' ')  ||
                 (*pc == '\n') ||
                 (*pc == '\r') ||
                 (*pc == '\t') ) {
                continue;
            }
            signalNotValid (userData, TNC_ERROR_DISALLOWED_PCDATA);
            return;
        }
    }
}

/*
 *----------------------------------------------------------------------------
 *
 * TncStartCdataSectionHandler --
 *
 *	This procedure is called at the start of a CDATA section
 *      within the document.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Eventually signals application error.
 *
 *----------------------------------------------------------------------------
 */

void
TncStartCdataSectionHandler (userData)
    void *userData;
{
    TNC_Data *tncdata = (TNC_Data *) userData;

    if (!tncdata->ignorePCDATA) {
        signalNotValid (userData, TNC_ERROR_DISALLOWED_CDATA);
    }
}


/*
 *----------------------------------------------------------------------------
 *
 * FreeTncData
 *
 *	Helper proc, used from TncResetProc and TncFreeProc. Frees all
 *	collected DTD data and the id table.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Frees memory.
 *
 *---------------------------------------------------------------------------- */
static void
FreeTncData (tncdata)
    TNC_Data *tncdata;
{
    Tcl_HashEntry *entryPtr, *attentryPtr;
    Tcl_HashSearch search, attsearch;
    TNC_Content *model;
    TNC_ElemAttInfo *elemAttInfo;
    TNC_EntityInfo *entityInfo;
    TNC_AttDecl *attDecl;

    entryPtr = Tcl_FirstHashEntry (tncdata->tagNames, &search);
    while (entryPtr) {
        model = Tcl_GetHashValue (entryPtr);
        TncFreeTncModel (model);
        Tcl_Free ((char *) model);
        entryPtr = Tcl_NextHashEntry (&search);
    }
    Tcl_DeleteHashTable (tncdata->tagNames);
    entryPtr = Tcl_FirstHashEntry (tncdata->attDefsTables, &search);
    while (entryPtr) {
        elemAttInfo = Tcl_GetHashValue (entryPtr);
        attentryPtr = Tcl_FirstHashEntry (elemAttInfo->attributes, &attsearch);
        while (attentryPtr) {
            attDecl = Tcl_GetHashValue (attentryPtr);
            if (attDecl->att_type == TNC_ATTTYPE_NOTATION ||
                attDecl->att_type == TNC_ATTTYPE_ENUMERATION) {
                Tcl_DeleteHashTable (attDecl->lookupTable);
                Tcl_Free ((char *) attDecl->lookupTable);
            }
            if (attDecl->dflt) {
                free (attDecl->dflt);
            }
            Tcl_Free ((char *) attDecl);
            attentryPtr = Tcl_NextHashEntry (&attsearch);
        }
        Tcl_DeleteHashTable (elemAttInfo->attributes);
        Tcl_Free ((char *) elemAttInfo->attributes);
        Tcl_Free ((char *) elemAttInfo);
        entryPtr = Tcl_NextHashEntry (&search);
    }
    Tcl_DeleteHashTable (tncdata->attDefsTables);
    entryPtr = Tcl_FirstHashEntry (tncdata->entityDecls, &search);
    while (entryPtr) {
        entityInfo = Tcl_GetHashValue (entryPtr);
        if (entityInfo->is_notation) {
            free (entityInfo->notationName);
        }
        Tcl_Free ((char *) entityInfo);
        entryPtr = Tcl_NextHashEntry (&search);
    }
    Tcl_DeleteHashTable (tncdata->entityDecls);
    Tcl_DeleteHashTable (tncdata->notationDecls);
    Tcl_DeleteHashTable (tncdata->ids);
    if (tncdata->doctypeName) {
        free (tncdata->doctypeName);
    }
}


/*
 *----------------------------------------------------------------------------
 *
 * TncResetProc
 *
 *	Called for C handler set specific reset actions in case of
 *      parser reset.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Resets the "userData" of the C handler set parser extension.
 *
 *----------------------------------------------------------------------------
 */

void
TncResetProc (interp, userData)
    Tcl_Interp *interp;
    void *userData;
{
    TNC_Data *tncdata = (TNC_Data *) userData;

    FreeTncData (tncdata);
    Tcl_InitHashTable (tncdata->tagNames, TCL_STRING_KEYS);
    Tcl_InitHashTable (tncdata->attDefsTables, TCL_STRING_KEYS);
    Tcl_InitHashTable (tncdata->entityDecls, TCL_STRING_KEYS);
    Tcl_InitHashTable (tncdata->notationDecls, TCL_STRING_KEYS);
    Tcl_InitHashTable (tncdata->ids, TCL_STRING_KEYS);
    tncdata->doctypeName = NULL;
    tncdata->ignoreWhiteCDATAs = 1;
    tncdata->ignorePCDATA = 0;
    tncdata->contentStackPtr = 0;
}


/*
 *----------------------------------------------------------------------------
 *
 * TncFreeProc
 *
 *	Called for C handler set specific cleanup in case of parser
 *      delete.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	C handler set specific userData gets free'd.
 *
 *----------------------------------------------------------------------------
 */

void
TncFreeProc (interp, userData)
    Tcl_Interp *interp;
    void *userData;
{
    TNC_Data *tncdata = (TNC_Data *) userData;

    FreeTncData (tncdata);
    Tcl_Free ((char *) tncdata->tagNames);
    Tcl_Free ((char *) tncdata->attDefsTables);
    Tcl_Free ((char *) tncdata->entityDecls);
    Tcl_Free ((char *) tncdata->notationDecls);
    Tcl_Free ((char *) tncdata->ids);
    Tcl_Free ((char *) tncdata->contentStack);
    Tcl_Free ((char *) tncdata);
}


/*
 *----------------------------------------------------------------------------
 *
 * TclTncObjCmd --
 *
 *	This procedure is invoked to process the "tnc" command.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	The expat parser object provided as argument is enhanced by
 *      by the "tnc" handler set.
 *
 *----------------------------------------------------------------------------
 */

int
TclTncObjCmd(dummy, interp, objc, objv)
     ClientData dummy;
     Tcl_Interp *interp;
     int objc;
     Tcl_Obj *CONST objv[];
{
    char          *method;
    CHandlerSet   *handlerSet;
    int            methodIndex, result;
    TNC_Data       *tncdata;


    static char *tncMethods[] = {
        "enable",  "remove",
        NULL
    };
    enum tncMethod {
        m_enable, m_remove
    };

    if (objc != 3) {
        Tcl_WrongNumArgs (interp, 1, objv, tnc_usage);
        return TCL_ERROR;
    }

    if (!CheckExpatParserObj (interp, objv[1])) {
        Tcl_SetResult (interp,
                       "First argument has to be a expat parser object", NULL);
        return TCL_ERROR;
    }

    method = Tcl_GetStringFromObj (objv[2], NULL);
    if (Tcl_GetIndexFromObj (interp, objv[2], tncMethods, "method", 0,
                             &methodIndex) != TCL_OK)
    {
        Tcl_SetResult (interp, tnc_usage, NULL);
        return TCL_ERROR;
    }

    switch ((enum tncMethod) methodIndex) {
    case m_enable:
        tncdata = (TNC_Data *) Tcl_Alloc (sizeof (TNC_Data));
        tncdata->tagNames =
            (Tcl_HashTable *) Tcl_Alloc (sizeof (Tcl_HashTable));
        Tcl_InitHashTable (tncdata->tagNames, TCL_STRING_KEYS);
        tncdata->attDefsTables =
            (Tcl_HashTable *) Tcl_Alloc (sizeof (Tcl_HashTable));
        Tcl_InitHashTable (tncdata->attDefsTables, TCL_STRING_KEYS);
        tncdata->entityDecls =
            (Tcl_HashTable *) Tcl_Alloc (sizeof (Tcl_HashTable));
        Tcl_InitHashTable (tncdata->entityDecls, TCL_STRING_KEYS);
        tncdata->notationDecls =
            (Tcl_HashTable *) Tcl_Alloc (sizeof (Tcl_HashTable));
        Tcl_InitHashTable (tncdata->notationDecls, TCL_STRING_KEYS);
        tncdata->ids = (Tcl_HashTable *) Tcl_Alloc (sizeof (Tcl_HashTable));
        Tcl_InitHashTable (tncdata->ids, TCL_STRING_KEYS);
        tncdata->doctypeName = NULL;
        tncdata->interp = interp;
        tncdata->expatObj = objv[1];
        tncdata->ignoreWhiteCDATAs = 1;
        tncdata->ignorePCDATA = 0;
        tncdata->contentStack = (TNC_ContentStack *)
            Tcl_Alloc (sizeof (TNC_ContentStack) * TNC_INITCONTENTSTACKSIZE);
        tncdata->contentStackSize = TNC_INITCONTENTSTACKSIZE;
        tncdata->contentStackPtr = 0;

        handlerSet = CHandlerSetCreate ("tnc");
        handlerSet->userData = tncdata;
        handlerSet->ignoreWhiteCDATAs = 0;
        handlerSet->resetProc = TncResetProc;
        handlerSet->freeProc = TncFreeProc;
        handlerSet->elementDeclCommand = TncElementDeclCommand;
        handlerSet->attlistDeclCommand = TncAttDeclCommand;
        handlerSet->entityDeclCommand = TncEntityDeclHandler;
        handlerSet->notationcommand = TncNotationDeclHandler;
        handlerSet->elementstartcommand = TncElementStartCommand;
        handlerSet->elementendcommand = TncElementEndCommand;
        handlerSet->datacommand = TncCharacterdataCommand;
        handlerSet->startCdataSectionCommand = TncStartCdataSectionHandler;
        handlerSet->startDoctypeDeclCommand = TncStartDoctypeDeclHandler;
        handlerSet->endDoctypeDeclCommand = TncEndDoctypeDeclHandler;

        result = CHandlerSetInstall (interp, objv[1], handlerSet);
        if (result != 0) {
            Tcl_SetResult (interp, "already have tnc C handler set", NULL);
            free (handlerSet->name);
            Tcl_Free ((char *) handlerSet);
            TncFreeProc (interp, tncdata);
            return TCL_ERROR;
        }
        return TCL_OK;
    case m_remove:
        result = CHandlerSetRemove (interp, objv[1], "tnc");
        if (result == 1) {
            /* This should not happen if CheckExpatParserObj() is used. */
            Tcl_SetResult (interp, "argument has to be a expat parser object", NULL);
            return TCL_ERROR;
        }
        if (result == 2) {
            Tcl_SetResult (interp, "expat parser obj hasn't a C handler set named \"tnc\"", NULL);
            return TCL_ERROR;
        }
        return TCL_OK;
    default:
        Tcl_SetResult (interp, "unknown method", NULL);
        return TCL_ERROR;
    }

}

/*
 *----------------------------------------------------------------------------
 *
 * Tnc_Init --
 *
 *	Initialization routine for loadable module
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Defines "tnc" enhancement command for expat parser obj
 *
 *----------------------------------------------------------------------------
 */

#if defined(_MSC_VER)
#  undef TCL_STORAGE_CLASS
#  define TCL_STORAGE_CLASS DLLEXPORT
#endif

EXTERN int
Tnc_Init (interp)
    Tcl_Interp *interp;
{
    Tcl_PkgRequire (interp, "expat", "2.0", 0);
    Tcl_CreateObjCommand (interp, "tnc", TclTncObjCmd, NULL, NULL );
    Tcl_PkgProvide (interp, "tnc", "1.0");
    return TCL_OK;
}

