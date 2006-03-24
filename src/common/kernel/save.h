/* vi: set ts=2:
 *
 *	
 *	Eressea PB(E)M host Copyright (C) 1998-2003
 *      Christian Schlittchen (corwin@amber.kn-bremen.de)
 *      Katja Zedel (katze@felidae.kn-bremen.de)
 *      Henning Peters (faroul@beyond.kn-bremen.de)
 *      Enno Rehling (enno@eressea-pbem.de)
 *      Ingo Wilken (Ingo.Wilken@informatik.uni-oldenburg.de)
 *
 *  based on:
 *
 * Atlantis v1.0  13 September 1993 Copyright 1993 by Russell Wallace
 * Atlantis v1.7                    Copyright 1996 by Alex Schr�der
 *
 * This program may not be used, modified or distributed without
 * prior permission by the authors of Eressea.
 * This program may not be sold or used commercially without prior written
 * permission from the authors.
 */

/* reading and writing the data files, reading the orders */

#ifndef H_KRNL_SAVE
#define H_KRNL_SAVE
#ifdef __cplusplus
extern "C" {
#endif

double version(void);

#define MAX_INPUT_SIZE	DISPLAYSIZE*2
/* Nach MAX_INPUT_SIZE brechen wir das Einlesen der Zeile ab und nehmen an,
 * dass hier ein Fehler (fehlende ") vorliegt */

FILE * cfopen(const char *filename, const char *mode);
int readorders(const char *);
int creategame(void);
extern int readgame(const char * filename, int backup);
int writegame(const char *filename, int quiet);

extern void rsf(FILE * F, char *s, size_t len);

/* Versions�nderungen: */
#define HEX_VERSION 81
extern int data_version;
extern int maxregions;
extern int firstx, firsty;
extern const char *xmlfile;

extern void init_locales(void);
extern int lastturn(void);

extern void read_items(FILE *f, struct item **it);
extern void write_items(FILE *f, struct item *it);

extern const char * datapath(void);

#if RESOURCE_CONVERSION
extern struct attrib_type at_resources;
#endif

extern void writeunit(FILE * stream, const struct unit * u);
extern struct unit * readunit(FILE * stream);

extern void writeregion(FILE * stream, const struct region * r);
extern struct region * readregion(FILE * stream, short x, short y);

extern void writefaction(FILE * stream, const struct faction * f);
extern struct faction * readfaction(FILE * stream);

extern void fwriteorder(FILE * F, const struct order * ord, const struct locale * lang);

extern int a_readint(struct attrib * a, FILE * F);
extern void a_writeint(const struct attrib * a, FILE * F);
extern int a_readshorts(struct attrib * a, FILE * F);
extern void a_writeshorts(const struct attrib * a, FILE * F);
extern int a_readchars(struct attrib * a, FILE * F);
extern void a_writechars(const struct attrib * a, FILE * F);
extern int a_readvoid(struct attrib * a, FILE * F);
extern void a_writevoid(const struct attrib * a, FILE * F);
extern int a_readstring(struct attrib * a, FILE * F);
extern void a_writestring(const struct attrib * a, FILE * F);
extern void a_finalizestring(struct attrib * a);

extern void create_backup(char *file);

#ifdef __cplusplus
}
#endif
#endif
