/* vi: set ts=2:
 *
 *  Eressea PB(E)M host Copyright (C) 1998-2003
 *      Christian Schlittchen (corwin@amber.kn-bremen.de)
 *      Katja Zedel (katze@felidae.kn-bremen.de)
 *      Henning Peters (faroul@beyond.kn-bremen.de)
 *      Enno Rehling (enno@eressea.de)
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

#ifndef H_KRNL_PLANES
#define H_KRNL_PLANES
#ifdef __cplusplus
extern "C" {
#endif

#define PFL_NOCOORDS        1 /* not in use */
#define PFL_NORECRUITS      2
#define PFL_NOALLIANCES     4
#define PFL_LOWSTEALING     8
#define PFL_NOGIVE         16  /* �bergaben sind unm�glich */
#define PFL_NOATTACK       32  /* Angriffe und Diebst�hle sind unm�glich */
#define PFL_NOTERRAIN      64  /* Terraintyp wird nicht angezeigt TODO? */
#define PFL_NOMAGIC       128  /* Zaubern ist unm�glich */
#define PFL_NOSTEALTH     256  /* Tarnung au�er Betrieb */
#define PFL_NOTEACH       512  /* Lehre au�er Betrieb */
#define PFL_NOBUILD      1024  /* Bauen au�er Betrieb */
#define PFL_NOFEED       2048  /* Kein Unterhalt n�tig */
#define PFL_FRIENDLY     4096  /* everyone is your ally */
#define PFL_NOORCGROWTH  8192  /* orcs don't grow */
#define PFL_NOMONSTERS  16384  /* no monster randenc */
#define PFL_SEESPECIAL  32768  /* far seeing */

typedef struct watcher {
  struct watcher * next;
  struct faction * faction;
  unsigned char mode;
} watcher;

typedef struct plane {
  struct plane *next;
  struct watcher * watchers;
  int id;
  char *name;
  int minx, maxx, miny, maxy;
  unsigned int flags;
  struct attrib *attribs;
} plane;

#define plane_id(pl) ( (pl) ? (pl)->id : 0 )

extern struct plane *planes;

struct plane *getplane(const struct region *r);
struct plane *findplane(int x, int y);
void init_planes(void);
int getplaneid(const struct region *r);
struct plane * getplanebyid(int id);
int region_x(const struct region *r, const struct faction *f);
int region_y(const struct region *r, const struct faction *f);
int plane_center_x(const struct plane *pl);
int plane_center_y(const struct plane *pl);
void set_ursprung(struct faction *f, int id, int x, int y);
struct plane * create_new_plane(int id, const char *name, int minx, int maxx, int miny, int maxy, int flags);
struct plane * getplanebyname(const char *);
struct plane * get_homeplane(void);
extern int rel_to_abs(const struct plane *pl, const struct faction * f, int rel, unsigned char index);
extern boolean is_watcher(const struct plane * p, const struct faction * f);
extern int resolve_plane(variant data, void * addr);
extern void write_plane_reference(const plane * p, struct storage * store);
extern int read_plane_reference(plane ** pp, struct storage * store);
extern int plane_width(const plane * pl);
extern int plane_height(const plane * pl);
void adjust_coordinates(const struct faction *f, int *x, int *y, const struct plane * pl, const struct region * r);
#ifdef __cplusplus
}
#endif
#endif
