/* vi: set ts=2:
 *
 *	Eressea PB(E)M host Copyright (C) 1998-2000
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

#ifndef FACTYPES_H
#define FACTYPES_H
#include <config.h>
#include "magic.h"

#define AT_NONE 0
#define AT_STANDARD	1
#define AT_DRAIN_EXP 2
#define AT_DRAIN_ST 3
#define AT_NATURAL 4
#define AT_DAZZLE 5
#define AT_SPELL 6
#define AT_COMBATSPELL 7
#define AT_STRUCTURAL 8

#define GOLEM_IRON   5          /* Anzahl Eisen in einem Eisengolem */
#define GOLEM_STONE 10          /* Anzahl Steine in einem Steingolem */

typedef struct att {
	int type;
	union {
		const char * dice;
		int i;
	} data;
	int flags;
} att;

typedef struct racedata racedata;
struct racedata {
	const char *name[4]; /* neu: name[4]v�lker */
	double magres;
	double maxaura; /* Faktor auf Maximale Aura */
	double regaura; /* Faktor auf Regeneration */
	int rekrutieren;
	int maintenance;
	int splitsize;
	int weight;
	double speed;
	int hitpoints;
	const char *def_damage;
	char armor;
	char at_default; /* Angriffsskill Unbewaffnet (default: -2)*/
	char df_default; /* Verteidigungsskill Unbewaffnet (default: -2)*/
	char at_bonus;   /* Ver�ndert den Angriffsskill (default: 0)*/
	char df_bonus;   /* Ver�ndert den Verteidigungskill (default: 0)*/
	att  attack[6];
	char bonus[MAXSKILLS];
	boolean nonplayer;
	int flags;
	int battle_flags;
	int ec_flags;
	race_t familiars[MAXMAGIETYP];
	char *(*generate_name) (struct unit *);
	void (*age_function)(struct unit *u);
	boolean (*move_allowed)(struct region *, struct region *);
};

/* Flags */
#define KILL_PEASANTS   (1<<0)		/* T�ten Bauern. D�monen werden nicht �ber
											 							dieses Flag, sondern in randenc() behandelt. */
#define SCARE_PEASANTS  (1<<1)
#define ATTACK_RANDOM   (1<<2)
#define MOVE_RANDOM     (1<<3)
#define CANNOT_MOVE     (1<<4)
#define SEEK_TARGET     (1<<5)		/* sucht ein bestimmtes Opfer */
#define LEARN           (1<<6) 	/* Lernt automatisch wenn struct faction == 0 */
#define FLY             (1<<7)   /* kann fliegen */
#define SWIM            (1<<8)   /* kann schwimmen */
#define WALK            (1<<9)   /* kann �ber Land gehen */
#define NOLEARN         (1<<10) 	/* kann nicht normal lernen */
#define NOTEACH         (1<<11) 	/* kann nicht lehren */
#define HORSE           (1<<12)  /* Einheit ist Pferd, sozusagen */
#define DESERT          (1<<13)  /* 5% Chance, das Einheit desertiert */
#define DRAGON_LIMIT    (1<<14)  /* Kann nicht aus Gletscher in Ozean */
#define ABSORB_PEASANTS (1<<15)  /* T�tet und absorbiert Bauern */
#define NOHEAL          (1<<16)  /* Einheit kann nicht geheilt werden */

/* Economic flags */
#define NOGIVE         (1<<0)   /* gibt niemals nix */
#define GIVEITEM       (1<<1)   /* gibt Gegenst�nde weg */
#define GIVEPERSON     (1<<2)   /* �bergibt Personen */
#define GIVEUNIT       (1<<3)   /* Einheiten an andere Partei �bergeben */
#define GETITEM        (1<<4)   /* nimmt Gegenst�nde an */
#define HOARDMONEY     (1<<5)   /* geben niemals Silber weg */
#define CANGUARD       (1<<6)   /* bewachen auch ohne Waffen */
#define REC_HORSES     (1<<7)   /* Rekrutiert aus Pferden */

/* Battle-Flags */
#define BF_EQUIPMENT				(1<<0)
	/* Kann Ausr�stung benutzen */
#define BF_MAGIC_EQUIPMENT	(1<<1)
	/* Kann magische Gegenst�nde (keine Waffen und R�stungen)benutzen */
#define BF_DRAIN_STRENGTH		(1<<2)
	/* Treffer entzieht Attack/Defense */
#define BF_DRAIN_EXP				(1<<3)
	/* Treffer entzieht Talenttage */
#define BF_INV_NONMAGIC			(1<<4)
	/* Immun gegen nichtmagischen Schaden */
#define BF_NOBLOCK					(1<<5)
	/* Wird in die R�ckzugsberechnung nicht einbezogen */
#define BF_RES_PIERCE				(1<<6)
	/* Halber Schaden durch PIERCE */
#define BF_RES_CUT					(1<<6)
	/* Halber Schaden durch CUT */
#define BF_RES_BASH					(1<<6)
	/* Halber Schaden durch BASH */

extern struct racedata race[];
void give_starting_equipment(struct region *r, struct unit *u);
void give_latestart_bonus(struct region *r, struct unit *u, int b);
int unit_old_max_hp(struct unit * u);
boolean is_undead(struct unit *u);

#define dragon(u) ((u)->race == RC_FIREDRAGON || (u)->race == RC_DRAGON || (u)->race == RC_WYRM || (u)->race == RC_BIRTHDAYDRAGON)
#define humanoid(u) (u->race==RC_UNDEAD || u->race==RC_DRACOID || !race[(u)->race].nonplayer)
#define nonplayer(u) (race[(u)->race].nonplayer)
#define nonplayer_race(r) (race[r].nonplayer)
#define illusionary(u) ((u)->race==RC_ILLUSION)

extern boolean allowed_dragon(const struct region * src, const struct region * target);

#endif
