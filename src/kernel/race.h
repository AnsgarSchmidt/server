/*
Copyright (c) 1998-2010, Enno Rehling <enno@eressea.de>
                         Katja Zedel <katze@felidae.kn-bremen.de
                         Christian Schlittchen <corwin@amber.kn-bremen.de>

Permission to use, copy, modify, and/or distribute this software for any
purpose with or without fee is hereby granted, provided that the above
copyright notice and this permission notice appear in all copies.

THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
**/

#ifndef H_KRNL_RACE_H
#define H_KRNL_RACE_H
#ifdef __cplusplus
extern "C" {
#endif

#include "magic.h"              /* wegen MAXMAGIETYP */

#define AT_NONE 0
#define AT_STANDARD	1
#define AT_DRAIN_EXP 2
#define AT_DRAIN_ST 3
#define AT_NATURAL 4
#define AT_DAZZLE 5
#define AT_SPELL 6
#define AT_COMBATSPELL 7
#define AT_STRUCTURAL 8

#define GOLEM_IRON   4          /* Anzahl Eisen in einem Eisengolem */
#define GOLEM_STONE  4          /* Anzahl Steine in einem Steingolem */

#define RACESPOILCHANCE 5       /* Chance auf rassentypische Beute */

  typedef struct att {
    int type;
    union {
      const char *dice;
      const struct spell *sp;
    } data;
    int flags;
    int level;
  } att;

  struct param;

  extern int num_races;

  typedef struct race {
    struct param *parameters;
    const char *_name[4];       /* neu: name[4]v�lker */
    float magres;
    float maxaura;              /* Faktor auf Maximale Aura */
    float regaura;              /* Faktor auf Regeneration */
    float recruit_multi;        /* Faktor f�r Bauernverbrauch */
    int index;
    int recruitcost;
    int maintenance;
    int splitsize;
    int weight;
    int capacity;
    float speed;
    float aggression;           /* chance that a monster will attack */
    int hitpoints;
    const char *def_damage;
    char armor;
    int at_default;             /* Angriffsskill Unbewaffnet (default: -2) */
    int df_default;             /* Verteidigungsskill Unbewaffnet (default: -2) */
    int at_bonus;               /* Ver�ndert den Angriffsskill (default: 0) */
    int df_bonus;               /* Ver�ndert den Verteidigungskill (default: 0) */
    const spell *precombatspell;
    struct att attack[10];
    char bonus[MAXSKILLS];
    signed char *study_speed;   /* study-speed-bonus in points/turn (0=30 Tage) */
    boolean __remove_me_nonplayer;
    int flags;
    int battle_flags;
    int ec_flags;
    race_t oldfamiliars[MAXMAGIETYP];

    const char *(*generate_name) (const struct unit *);
    const char *(*describe) (const struct unit *, const struct locale *);
    void (*age) (struct unit * u);
     boolean(*move_allowed) (const struct region *, const struct region *);
    struct item *(*itemdrop) (const struct race *, int size);
    void (*init_familiar) (struct unit *);

    const struct race *familiars[MAXMAGIETYP];
    struct attrib *attribs;
    struct race *next;
  } race;

  typedef struct race_list {
    struct race_list *next;
    const struct race *data;
  } race_list;

  extern void racelist_clear(struct race_list **rl);
  extern void racelist_insert(struct race_list **rl, const struct race *r);

  extern struct race_list *get_familiarraces(void);
  extern struct race *races;

  extern struct race *rc_find(const char *);
  extern const char *rc_name(const struct race *, int);
  extern struct race *rc_add(struct race *);
  extern struct race *rc_new(const char *zName);
  extern int rc_specialdamage(const race *, const race *,
    const struct weapon_type *);

/* Flags */
#define RCF_PLAYERRACE     (1<<0)       /* can be played by a player. */
#define RCF_KILLPEASANTS   (1<<1)       /* T�ten Bauern. D�monen werden nicht �ber dieses Flag, sondern in randenc() behandelt. */
#define RCF_SCAREPEASANTS  (1<<2)
#define RCF_CANSTEAL       (1<<3)
#define RCF_MOVERANDOM     (1<<4)
#define RCF_CANNOTMOVE     (1<<5)
#define RCF_LEARN          (1<<6)       /* Lernt automatisch wenn struct faction == 0 */
#define RCF_FLY            (1<<7)       /* kann fliegen */
#define RCF_SWIM           (1<<8)       /* kann schwimmen */
#define RCF_WALK           (1<<9)       /* kann �ber Land gehen */
#define RCF_NOLEARN        (1<<10)      /* kann nicht normal lernen */
#define RCF_NOTEACH        (1<<11)      /* kann nicht lehren */
#define RCF_HORSE          (1<<12)      /* Einheit ist Pferd, sozusagen */
#define RCF_DESERT         (1<<13)      /* 5% Chance, das Einheit desertiert */
#define RCF_ILLUSIONARY    (1<<14)      /* (Illusion & Spell) Does not drop items. */
#define RCF_ABSORBPEASANTS (1<<15)      /* T�tet und absorbiert Bauern */
#define RCF_NOHEAL         (1<<16)      /* Einheit kann nicht geheilt werden */
#define RCF_NOWEAPONS      (1<<17)      /* Einheit kann keine Waffen benutzen */
#define RCF_SHAPESHIFT     (1<<18)      /* Kann TARNE RASSE benutzen. */
#define RCF_SHAPESHIFTANY  (1<<19)      /* Kann TARNE RASSE "string" benutzen. */
#define RCF_UNDEAD         (1<<20)      /* Undead. */
#define RCF_DRAGON         (1<<21)      /* Drachenart (f�r Zauber) */
#define RCF_COASTAL        (1<<22)      /* kann in Landregionen an der K�ste sein */
#define RCF_UNARMEDGUARD   (1<<23)      /* kann ohne Waffen bewachen */
#define RCF_CANSAIL        (1<<24)      /* Einheit darf Schiffe betreten */
#define RCF_INVISIBLE      (1<<25)      /* not visible in any report */
#define RCF_SHIPSPEED      (1<<26)      /* race gets +1 on shipspeed */
#define RCF_STONEGOLEM     (1<<27)      /* race gets stonegolem properties */
#define RCF_IRONGOLEM      (1<<28)      /* race gets irongolem properties */

/* Economic flags */
#define GIVEITEM       (1<<1)   /* gibt Gegenst�nde weg */
#define GIVEPERSON     (1<<2)   /* �bergibt Personen */
#define GIVEUNIT       (1<<3)   /* Einheiten an andere Partei �bergeben */
#define GETITEM        (1<<4)   /* nimmt Gegenst�nde an */
#define ECF_REC_HORSES     (1<<6)       /* Rekrutiert aus Pferden */
#define ECF_REC_ETHEREAL   (1<<7)       /* Rekrutiert aus dem Nichts */
#define ECF_REC_UNLIMITED  (1<<8)       /* Rekrutiert ohne Limit */

/* Battle-Flags */
#define BF_EQUIPMENT    (1<<0)  /* Kann Ausr�stung benutzen */
#define BF_NOBLOCK      (1<<1)  /* Wird in die R�ckzugsberechnung nicht einbezogen */
#define BF_RES_PIERCE   (1<<2)  /* Halber Schaden durch PIERCE */
#define BF_RES_CUT      (1<<3)  /* Halber Schaden durch CUT */
#define BF_RES_BASH     (1<<4)  /* Halber Schaden durch BASH */
#define BF_INV_NONMAGIC (1<<5)  /* Immun gegen nichtmagischen Schaden */
#define BF_CANATTACK    (1<<6)  /* Kann keine ATTACKIERE Befehle ausfuehren */

  extern int unit_old_max_hp(struct unit *u);
  extern const char *racename(const struct locale *lang, const struct unit *u,
    const race * rc);

#define omniscient(f) (((f)->race)==new_race[RC_ILLUSION] || ((f)->race)==new_race[RC_TEMPLATE])

#define playerrace(rc) (fval((rc), RCF_PLAYERRACE))
#define dragonrace(rc) ((rc) == new_race[RC_FIREDRAGON] || (rc) == new_race[RC_DRAGON] || (rc) == new_race[RC_WYRM] || (rc) == new_race[RC_BIRTHDAYDRAGON])
#define humanoidrace(rc) (fval((rc), RCF_UNDEAD) || (rc)==new_race[RC_DRACOID] || playerrace(rc))
#define illusionaryrace(rc) (fval(rc, RCF_ILLUSIONARY))

  extern boolean allowed_dragon(const struct region *src,
    const struct region *target);

  extern boolean r_insectstalled(const struct region *r);

  extern void add_raceprefix(const char *);
  extern char **race_prefixes;

  extern void write_race_reference(const struct race *rc,
    struct storage *store);
  extern variant read_race_reference(struct storage *store);

  extern const char *raceprefix(const struct unit *u);

  extern void give_starting_equipment(const struct equipment *eq,
    struct unit *u);

#ifdef __cplusplus
}
#endif
#endif
