/* vi: set ts=2:
 *
 *	$Id: laws.c,v 1.20 2001/02/12 22:39:56 enno Exp $
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

#include <config.h>
#include <eressea.h>
#include "laws.h"

#include <modules/gmcmd.h>

#ifdef OLD_TRIGGER
# include "old/trigger.h"
#endif

/* kernel includes */
#include <alchemy.h>
#include <border.h>
#include <faction.h>
#include <item.h>
#include <magic.h>
#include <message.h>
#include <save.h>
#include <ship.h>
#include <skill.h>
#include "movement.h"
#include "monster.h"
#include "spy.h"
#include "race.h"
#include "battle.h"
#include "region.h"
#include "unit.h"
#include "plane.h"
#include "study.h"
#include "karma.h"
#include "pool.h"
#include "building.h"
#include "group.h"

/* gamecode includes */
#include "economy.h"
#include "creation.h"
#include "randenc.h"

/* util includes */
#include <event.h>
#include <base36.h>
#include <goodies.h>
#include <rand.h>

/* libc includes */
#include <stdio.h>
#include <math.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>

/* - external symbols ------------------------------------------ */
extern int dropouts[2];
extern int * age;
/* - exported global symbols ----------------------------------- */
boolean nobattle = false;
/* ------------------------------------------------------------- */

static int
findoption(char *s)
{
	return findstr(options, s, MAXOPTIONS);
}

static void
destroyfaction(faction * f)
{
	unit *u;
	faction *ff;

	if( !f->alive ) return;
#ifdef OLD_TRIGGER
	do_trigger(f, TYP_FACTION, TR_DESTRUCT);
#endif

	for (u=f->units;u;u=u->nextF) {
		region * r = u->region;
		unit * au;
		int number = 0;
		struct friend {
			struct friend * next;
			int number;
			faction * faction;
			unit * unit;
		} * friends = NULL;
		for (au=r->units;au;au=au->next) if (au->faction!=f) {
			if (allied(u, au->faction, HELP_ALL)) {
				struct friend * nf, ** fr = &friends;

				while (*fr && (*fr)->faction->no<au->faction->no) fr = &(*fr)->next;
				nf = *fr;
				if (nf==NULL || nf->faction!=au->faction) {
					nf = calloc(sizeof(struct friend), 1);
					nf->next = *fr;
					nf->faction = au->faction;
					nf->unit = au;
					*fr = nf;
				}
				nf->number += au->number;
				number += au->number;
			}
		}
		if (friends && number) {
			struct friend * nf = friends;
			while (nf) {
				resource_t res;
				unit * u2 = nf->unit;
				for (res = 0; res <= R_SILVER; ++res) {
					int n = get_resource(u, res);
					if (n<=0) continue;
					n = n * nf->number / number;
					if (n<=0) continue;
					change_resource(u, res, -n);
					change_resource(u2, res, n);
				}
				number -= nf->number;
				nf = nf->next;
				free(friends);
				friends = nf;
			}
			friends = NULL;
		} else {
			int p = rpeasants(u->region),
				m = rmoney(u->region),
				h = rhorses(u->region);
			if (rterrain(r) != T_OCEAN && !nonplayer(u)) {
				p += u->number;
				m += get_money(u);
				h += get_item(u, I_HORSE);
			}
			rsetpeasants(r, p);
			rsethorses(r, h);
			rsetmoney(r, m);
		}
		set_number(u, 0);
	}
	f->alive = 0;
#ifdef NEW_TRIGGER
	handle_event(&f->attribs, "destroy", f);
#endif
#ifdef OLD_TRIGGER
	change_all_pointers(f, TYP_FACTION, NULL);
#endif
	for (ff = factions; ff; ff = ff->next) {
#ifdef GROUPS
		group *g;
#endif
		ally *sf, *sfn;

		/* Alle HELFE f�r die Partei l�schen */
		for (sf = ff->allies; sf; sf = sf->next) {
			if (sf->faction == f) {
				removelist(&ff->allies, sf);
				break;
			}
		}
#ifdef GROUPS
		for(g=ff->groups; g; g=g->next) {
			for (sf = g->allies; sf;) {
				sfn = sf->next;
				if (sf->faction == f) {
					removelist(&g->allies, sf);
					break;
				}
				sf = sfn;
			}
		}
#endif
	}
}

void
restart(unit *u, int race)
{
#ifdef ALLOW_RESTART
	faction *f = addplayer(u->region, u->faction->email, race)->faction;
	f->magiegebiet = u->faction->magiegebiet;
	destroyfaction(u->faction);
#endif
}

/* ------------------------------------------------------------- */

static void
checkorders(void)
{
	faction *f;

	puts(" - Warne sp�te Spieler...");
	for (f = factions; f; f = f->next)
		if (f->no!=MONSTER_FACTION && turn - f->lastorders == ORDERGAP - 1)
			addstrlist(&f->mistakes,
				"Bitte sende die Befehle n�chste Runde ein, "
				   "wenn du weiterspielen m�chtest.");
}
/* ------------------------------------------------------------- */

static boolean
is_monstrous(unit * u)
{
	return (boolean) (u->faction->no == MONSTER_FACTION || nonplayer(u));
}

static void
get_food(region *r)
{
	unit *u;

	/* 1. Versorgung von eigenen Einheiten */
	for (u = r->units; u; u = u->next) {
		int need = lifestyle(u);

		/* Erstmal zur�cksetzen */
		freset(u, FL_HUNGER);

		need -= get_money(u);
		if (need > 0) {
			unit *v;

			for (v = r->units; need && v; v = v->next)
				if (v->faction == u->faction && !is_monstrous(u)) {
					int give = get_money(v) - lifestyle(v);
					give = max(0, give);
					give = min(need, give);
					if (give) {
						change_money(v, -give);
						change_money(u, give);
						need -= give;
					}
				}
		}
	}

	/* 2. Versorgung durch Fremde: */
	for (u = r->units; u; u = u->next) {
		int need = lifestyle(u);
		/* Negative Silberbetr�ge kommen vor (Bug), deshalb geht
		 * need -= u->money nicht. */

		need -= max(0, get_money(u));

		if (need > 0) {
			unit *v;

			for (v = r->units; need && v; v = v->next)
				if (v->faction != u->faction && allied(v, u->faction, HELP_MONEY)
						&& !is_monstrous(u)) {
					int give = lifestyle(v);
					give = max(0, get_money(v) - give);
					give = min(need, give);

					if (give) {
						change_money(v, -give);
						change_money(u, give);
						need -= give;
						add_spende(v->faction, u->faction, give, r);
					}
				}

			/* Die Einheit hat nicht genug Geld zusammengekratzt und
			 * nimmt Schaden: */

			if (need) hunger(u, need);
		}
	}

	/* 3. Von den �berlebenden das Geld abziehen: */
	for (u = r->units; u; u = u->next) {
		int need = min(get_money(u), lifestyle(u));
		change_money(u, -need);
	}
}

static void
live(region * r)
{
	unit *u, *un;

	get_food(r);

	for (u = r->units; u; u = un) {
		/* IUW: age_unit() kann u l�schen, u->next ist dann
		 * undefiniert, also m�ssen wir hier schon das n�chste
		 * Element bestimmen */
		un = u->next;

		if (!is_monstrous(u)) {
			if (get_effect(u, oldpotiontype[P_FOOL]) > 0) {	/* Trank "Dumpfbackenbrot" */
				skill_t sk, ibest = NOSKILL;
				int best = 0;

				for (sk = 0; sk < MAXSKILLS; sk++) {
					if (get_skill(u, sk) > best) {
						best = get_skill(u, sk);
						ibest = sk;
					}
				}				/* bestes Talent raussuchen */
				if (best > 0) {
					int k;
					int value = get_effect(u, oldpotiontype[P_FOOL]);
					value = min(value, u->number) * 30;
					k = get_skill(u, ibest) - value;
					k = max(k, 0);
					set_skill(u, ibest, k);
					add_message(&u->faction->msgs, new_message(u->faction,
						"dumbeffect%u:unit%i:days%t:skill", u, value, ibest));
				}	/* sonst Gl�ck gehabt: wer nix wei�, kann nix vergessen... */
			}
		}
		age_unit(r, u);
	}
}

/*
 * This procedure calculates the number of emigrating peasants for the given
 * region r. There are two incentives for peasants to emigrate:
 * 1) They prefer the less crowded areas.
 *    Example: mountains, 700 peasants (max  1000), 70% inhabited
 *             plain, 5000 peasants (max 10000), 50% inhabited
 *             700*(PEASANTSWANDER_WEIGHT/100)*((70-50)/100) peasants wander
 *             from mountains to plain.
 *    Effect : peasents will leave densely populated regions.
 * 2) Peasants prefer richer neighbour regions.
 *    Example: region A,  700 peasants, wealth $10500, $15 per head
 *             region B, 2500 peasants, wealth $25000, $10 per head
 *             Some peasants will emigrate from B to A because $15 > $10
 *             exactly: 2500*(PEASANTSGREED_WEIGHT1/100)*((15-10)/100)
 * Not taken in consideration:
 * - movement because of monsters.
 * - movement because of wars
 * - movement because of low loyalty relating to present parties.
 */

void
calculate_emigration(region *r)
{
	direction_t i;
	int maxpeasants_here;
	int maxpeasants[MAXDIRECTIONS];
	double wfactor, gfactor;

	/* Vermeidung von DivByZero */
	maxpeasants_here = max(maxworkingpeasants(r),1);

	for (i = 0; i != MAXDIRECTIONS; i++) {
		region *c = rconnect(r, i);
		if (c && rterrain(c) != T_OCEAN) {
			maxpeasants[i] = maxworkingpeasants(c);
		} else maxpeasants[i] = 0;
	}

	/* calculate emigration for all directions independently */

	for (i = 0; i != MAXDIRECTIONS; i++) {
		region * c = rconnect(r, i);
		if (!c) continue;
		if (fval(r, RF_ORCIFIED)==fval(c, RF_ORCIFIED)) {
			if (landregion(rterrain(c)) && landregion(rterrain(r))) {
				int wandering_peasants;

				/* First let's calculate the peasants who wander off to less inhabited
				 * regions. wfactor indicates the difference of population denity.
				 * Never let more than PEASANTSWANDER_WEIGHT per cent wander off in one
				 * direction. */
				wfactor = ((double) rpeasants(r) / maxpeasants_here -
					((double) rpeasants(c) / maxpeasants[i]));
				wfactor = max(wfactor, 0);
				wfactor = min(wfactor, 1);

				/* Now let's handle the greedy peasants. gfactor indicates the
				 * difference of per-head-wealth. Never let more than
				 * PEASANTSGREED_WEIGHT per cent wander off in one direction. */
				gfactor = (((double) rmoney(c) / max(rpeasants(c), 1)) -
						   ((double) rmoney(r) / max(rpeasants(r), 1))) / 500;
				gfactor = max(gfactor, 0);
				gfactor = min(gfactor, 1);

				wandering_peasants = (int) (rpeasants(r) * (gfactor + wfactor)
						* PEASANTSWANDER_WEIGHT / 100.0);

				r->land->newpeasants -= wandering_peasants;
				c->land->newpeasants += wandering_peasants;
			}
		}
	}
}

/* = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = */

static void
peasants(region * r)
{

	int glueck;
	/* Das Geld, da� die Bauern erwirtschaftet haben unter expandwork, gibt
	 * den Bauern genug f�r 11 Bauern pro Ebene ohne Wald. Der Wald
	 * breitet sich nicht in Gebiete aus, die bebaut werden. */

	int peasants, money, n, dead, satiated;
	attrib * a;

	/* Bauern vermehren sich */

	/* Bis zu 1000 Bauern k�nnen Zwillinge bekommen oder 1000 Bauern
	 * wollen nicht! */

	a = a_find(r->attribs, &at_peasantluck);
	if (!a) {
		glueck = 0;
	} else {
		glueck = a->data.i * 1000;
	}

	peasants = rpeasants(r);

	for (n = peasants; n; n--) {
		if (glueck >= 0) {		/* Sonst keine Vermehrung */
			if (rand() % 100 < PEASANTGROWTH) {
				if ((float) peasants
					/ ((float) production(r) * MAXPEASANTS_PER_AREA)
					< 0.9 || rand() % 100 < PEASANTFORCE) {
					peasants++;
				}
			}
		} else
			glueck++;

		if (glueck > 0) {		/* Doppelvermehrung */
			if (rand() % 100 < PEASANTGROWTH)
				peasants++;
			glueck--;
		}
	}

	/* Alle werden satt, oder halt soviele f�r die es auch Geld gibt */

	money = rmoney(r);
	satiated = min(peasants, money / MAINTENANCE);
	rsetmoney(r, money - satiated * MAINTENANCE);

	/* Von denjenigen, die nicht satt geworden sind, verhungert der
	 * Gro�teil. dead kann nie gr��er als rpeasants(r) - satiated werden,
	 * so dass rpeasants(r) >= 0 bleiben mu�. */

	/* Es verhungert maximal die unterern�hrten Bev�lkerung. */

	dead = 0;
	for (n = min((peasants - satiated), rpeasants(r)); n; n--)
		if (rand() % 100 > STARVATION_SURVIVAL)
			dead++;

	if(dead > 0) {
		peasants -= dead;
		add_message(&r->msgs, new_message(NULL, "phunger%i:dead", dead));
	}
	rsetpeasants(r, peasants);
}

/* ------------------------------------------------------------- */

typedef struct migration {
	struct migration * next;
	region * r;
	int horses;
	int trees;
} migration;

#define MSIZE 1023
migration * migrants[MSIZE];
migration * free_migrants;

static migration *
get_migrants(region * r)
{
	int key = region_hashkey(r);
	int index = key % MSIZE;
	migration * m = migrants[index];
	while (m && m->r != r)
		m = m->next;
	if (m == NULL) {
		/* Es gibt noch keine Migration. Also eine erzeugen
		 */
		m = free_migrants;
		if (!m) m = calloc(1, sizeof(migration));
		else {
			free_migrants = free_migrants->next;
			m->horses = 0;
			m->trees = 0;
		}
		m->r = r;
		m->next = migrants[index];
		migrants[index] = m;
	}
	return m;
}

static void
migrate(region * r)
{
	int key = region_hashkey(r);
	int index = key % MSIZE;
	migration ** hp = &migrants[index];
	fset(r, RF_MIGRATION);
	while (*hp && (*hp)->r != r) hp = &(*hp)->next;
	if (*hp) {
		migration * m = *hp;
		rsethorses(r, rhorses(r) + m->horses);
		/* Was macht das denn hier?
		 * Baumwanderung wird in trees() gemacht.
		 * wer fragt das? Die Baumwanderung war abh�ngig von der
		 * Auswertungsreihenfolge der regionen,
		 * das hatte ich ge�ndert. jemand hat es wieder gel�scht, toll.
		 * ich habe es wieder aktiviert, mu� getestet werden.
		 */
		rsettrees(r, rtrees(r) + m->trees);
		*hp = m->next;
		m->next = free_migrants;
		free_migrants = m;
	}
}

static void
horses(region * r)
{
	double m;
	direction_t n;

	/* Logistisches Wachstum, Optimum bei halbem Maximalbesatz. */
	m = maxworkingpeasants(r)/10;
	m = max(0, m);
	if (m) {
		double gg = (HORSEGROWTH*(rhorses(r)/1.5)*((double)m-(rhorses(r)/1.5))/(double)m)/100.0;
		int g = (int)(normalvariate(gg,gg/4));

		if(g > 0) {
			rsethorses(r, rhorses(r) + g);
		} else if(g < 0) {
			/* Die H�lfte evt �berz�hliger Pferde stirbt nicht, sondern wandert
		 	 * zuf�llig ab. */
			int c = 0;
			g = min(g/-2, rhorses(r));

			/* Die abwandernden Pferde m�ssen auch abgezogen werden? */
			rsethorses(r, rhorses(r) - g);

			for(n = 0; n != MAXDIRECTIONS; n++) {
				region * r2 = rconnect(r, n);
				if(r2 && (terrain[r2->terrain].flags & WALK_INTO))
					c++;
			}
			if(c > 0) {
				g /= c;
				for(n = 0; n != MAXDIRECTIONS; n++) {
					region * r2 = rconnect(r, n);
					if(r2 && (terrain[r2->terrain].flags & WALK_INTO)) {
						if (fval(r2, RF_MIGRATION))
							rsethorses(r2, rhorses(r2) + g);
						else {
							migration * nb;
							/* haben wir die Migration schonmal benutzt?
							 * wenn nicht, m�ssen wir sie suchen.
							 * Wandernde Pferde vermehren sich nicht.
							 */
							nb = get_migrants(r2);
							nb->horses += g;
						}
					}
				}
			}
		}
	}

	/* Pferde wandern in Nachbarregionen.
	 * Falls die Nachbarregion noch berechnet
	 * werden mu�, wird eine migration-Struktur gebildet,
	 * die dann erst in die Berechnung der Nachbarstruktur einflie�t.
	 */

	for(n = 0; n != MAXDIRECTIONS; n++) {
		region * r2 = rconnect(r, n);
		if(r2 && (terrain[r2->terrain].flags & WALK_INTO)) {
			int pt = (rhorses(r) * HORSEMOVE)/100;
			pt = (int)normalvariate(pt, pt/4.0);
			pt = max(0, pt);
			if (fval(r2, RF_MIGRATION))
				rsethorses(r2, rhorses(r2) + pt);
			else {
				migration * nb;
				/* haben wir die Migration schonmal benutzt?
				 * wenn nicht, m�ssen wir sie suchen.
				 * Wandernde Pferde vermehren sich nicht.
				 */
				nb = get_migrants(r2);
				nb->horses += pt;
			}
			/* Wandernde Pferde sollten auch abgezogen werden */
			rsethorses(r, rhorses(r) - pt);
		}
	}
	assert(rhorses(r) >= 0);
}

static void
trees(region * r)
{
	int n, m;
	int tree = rtrees(r);
	direction_t d;

	/* B�ume vermehren sich. m ist die Anzahl B�ume, f�r die es Land
	 * gibt. Der Wald besiedelt keine bebauten Gebiete, wird den Pferden
	 * aber Land wegnehmen. Gibt es zuviele Bauern, werden sie den Wald
	 * nicht f�llen, sondern verhungern. Der Wald kann nur von Spielern gef�llt
	 * werden! Der Wald wandert nicht. Auch bei magischen Terrainver�nderungen
	 * darf m nicht negativ werden! */

	if(production(r) <= 0) return;

	m = production(r) - rpeasants(r)/MAXPEASANTS_PER_AREA;
	m = max(0, m);

	/* Solange es noch freie Pl�tze gibt, darf jeder Baum versuchen, sich
	 * fortzupflanzen. Da B�ume nicht sofort eingehen, wenn es keinen
	 * Platz gibt (wie zB. die Pferde), darf nicht einfach drauflos vermehrt
	 * werden und dann ein min () gemacht werden, sondern es mu� auf diese
	 * Weise vermehrt werden. */

	/* Logistisches Wachstum, Optimum bei halbem Maximalbesatz.
	 * In 'tree' steht, wieviel B�ume hier wachsen sollen. */

	if(m) {
		double g = ((FORESTGROWTH*(tree/1.5)*((double)m-(tree/1.5))/m)/100);
		tree = tree + (int)normalvariate(g,g/4);
		tree = max(0, tree);
	}

	/* B�ume breiten sich in Nachbarregionen aus.
	 * warnung: fr�her kamen b�ume aus nachbarregionen, heute
	 * gehen sie von der aktuellen in die benachbarten.
	 * Die Chance, das ein Baum in eine Region r2 einwandert, ist
	 * (production-rtrees(r2))/10000.
	 * Die Richtung, in der sich ein Baum vermehrt, ist zuf�llig.
	 * Es ibt also genausoviel Versuche, in einen Geltscher zu
	 * wandern, wie in eine ebene - nur halt weniger Erfolge.
	 * Die Summe der Versuche ist rtrees(r), jeder Baum
	 * versucht es also einmal pro Woche, mit maximal 10% chance
	 * (wenn ein voller wald von lauter leeren ebenen umgeben ist)
	 */

	for (d=0;d!=MAXDIRECTIONS;++d) {
		region * r2 = rconnect(r, d);
		if (r2
				&& (terrain[r2->terrain].flags & WALK_INTO)
				&& fval(r2, RF_MALLORN) == fval(r, RF_MALLORN))	{
			/* Da hier rtrees(r2) abgefragt wird, macht es einen Unterschied,
			 * ob das wachstum in r2 schon stattgefunden hat, oder nicht.
			 * leider nicht einfach zu verhindern */
			int pt = (production(r2)-rtrees(r2));
			pt = tree*max(0, pt) /
				(MAXDIRECTIONS*terrain[T_PLAIN].production_max*10);
			if (fval(r2, RF_MIGRATION))
				rsettrees(r2, rtrees(r2) + pt);
			else {
				migration * nb;
				/* haben wir die Migration schonmal benutzt?
				 * wenn nicht, m�ssen wir sie suchen.
				 * Wandernde Pferde vermehren sich nicht.
				 */
				nb = get_migrants(r2);
				nb->trees += pt;
			}
		}
	}
	rsettrees(r, tree);
	assert(tree >= 0);

	/* Jetzt die Kr�utervermehrung. Vermehrt wird logistisch:
	 *
	 * Jedes Kraut hat eine Wahrscheinlichkeit von (100-(vorhandene
	 * Kr�uter))% sich zu vermehren. */

	for(n = rherbs(r); n > 0; n--) {
		if (rand()%100 < (100-rherbs(r))) rsetherbs(r, (short)(rherbs(r)+1));
	}
}

static void
iron(region * r)
{
#ifndef NO_GROWTH
	if (rterrain(r) == T_MOUNTAIN) {
		rsetiron(r, riron(r) + IRONPERTURN);
#if !NEW_LAEN
		if(a_find(r->attribs, &at_laen)) {
			rsetlaen(r, rlaen(r) + rand() % MAXLAENPERTURN);
		}
#endif
	} else if (rterrain(r) == T_GLACIER) {
		rsetiron(r, min(MAXGLIRON, riron(r)+GLIRONPERTURN));
	}
#endif
}
/* ------------------------------------------------------------- */

void
demographics(void)
{
	region *r;

	for (r = regions; r; r = r->next) {
		live(r);
		/* check_split_dragons(); */

		if (rterrain(r) != T_OCEAN) {
			/* die Nachfrage nach Produkten steigt. */
#ifdef NEW_ITEMS
			struct demand * dmd;
			if (r->land) for (dmd=r->land->demands;dmd;dmd=dmd->next) {
				if (dmd->value>0 && dmd->value < MAXDEMAND) {
					int rise = DMRISE;
					if (gebaeude_vorhanden(r, &bt_harbour)) rise = DMRISEHAFEN;
					if (rand() % 100 < rise) dmd->value++;
				}
			}
#else
			item_t n;
			for (n = 0; n != MAXLUXURIES; n++) {
				int d = rdemand(r, n);
				if (d > 0 && d < MAXDEMAND) {
					if (gebaeude_vorhanden(r, &bt_harbour)) {
						if (rand() % 100 < DMRISEHAFEN) {
							d++;
						}
					} else {
						if (rand() % 100 < DMRISE) {
							d++;
						}
					}
				}
				rsetdemand(r, n, (char)d);
			}
#endif
			/* Seuchen erst nachdem die Bauern sich vermehrt haben
			 * und gewandert sind */

			calculate_emigration(r);
			peasants(r);
			plagues(r, false);

			r->age++;
			horses(r);
			trees(r);
			iron(r);

			migrate(r);
		}
	}
	while (free_migrants) {
		migration * m = free_migrants->next;
		free(free_migrants);
		free_migrants = m;
	};
	putchar('\n');

	remove_empty_units();

	puts(" - Einwanderung...");
	for (r = regions; r; r = r->next) {
		if (landregion(rterrain(r))) {
			int rp = rpeasants(r) + r->land->newpeasants;
			rsetpeasants(r, max(0, rp));
			/* Wenn keine Bauer da ist, soll auch kein Geld da sein */
			/* Martin */
			if (rpeasants(r) == 0)
				rsetmoney(r, 0);
		}
	}

	checkorders();
}
/* ------------------------------------------------------------- */

static int
modify(int i)
{
	int c;

	c = i * 2 / 3;

	if (c >= 1) {
		return (c + rand() % c);
	} else {
		return (i);
	}
}

static void
inactivefaction(faction * f)
{
	FILE *inactiveFILE;
	char zText[128];

	sprintf(zText, "%s/%s", datapath(), "/passwd");
	inactiveFILE = fopen(zText, "a");

	fprintf(inactiveFILE, "%s:%s:%d:%d\n",
		factionid(f),
		race[f->race].name[1],
		modify(count_all(f)),
		turn - f->lastorders);

	fclose(inactiveFILE);
}

void
quit(void)
{
	region *r;
	unit *u;
	strlist *S;
	faction *f;
	int frace;

	/* Sterben erst nachdem man allen anderen gegeben hat - bzw. man kann
	 * alles machen, was nicht ein drei�igt�giger Befehl ist. */

	for (r = regions; r; r = r->next)
		for (u = r->units; u; u = u->next)
			for (S = u->orders; S; S = S->next)
				if (igetkeyword(S->s) == K_QUIT) {
					if (strcmp(getstrtoken(), u->faction->passw) == 0) {
						destroyfaction(u->faction);
					} else {
						cmistake(u, S->s, 86, MSG_EVENT);
						printf("	Warnung: STIRB mit falschem Passwort f�r Partei %s: %s\n",
							   factionid(u->faction), S->s);
					}
				} else if(igetkeyword(S->s) == K_RESTART && u->number > 0) {
					if (!landregion(rterrain(r))) {
						cmistake(u, S->s, 242, MSG_EVENT);
						continue;
					}
					if (u->faction->age < 100) {
						cmistake(u, S->s, 241, MSG_EVENT);
						continue;
					}

					frace = findrace(getstrtoken());

					if(frace == NORACE || race[frace].nonplayer) {
						cmistake(u, S->s, 243, MSG_EVENT);
						continue;
					}

					if (strcmp(getstrtoken(), u->faction->passw)) {
						cmistake(u, S->s, 86, MSG_EVENT);
						printf("  Warnung: NEUSTART mit falschem Passwort f�r Partei %s: %s\n",
							   factionid(u->faction), S->s);
						continue;
					}
					restart(u, frace);
				}

	puts(" - beseitige Spieler, die sich zu lange nicht mehr gemeldet haben...");

	remove("inactive");

	for (f = factions; f; f = f->next) if(!fval(f, FL_NOIDLEOUT)) {
		if (turn - f->lastorders >= ORDERGAP) {
			destroyfaction(f);
			continue;
		}
		if (turn - f->lastorders >= (ORDERGAP - 1)) {
			inactivefaction(f);
			continue;
		}
	}

	puts(" - beseitige Spieler, die sich nach der Anmeldung nicht "
		 "gemeldet haben...");

	age = calloc(turn+1, sizeof(int));
	for (f = factions; f; f = f->next) if(!fval(f, FL_NOIDLEOUT)) {
		if (f->age>=0 && f->age <= turn) ++age[f->age];
		if (f->age == 2 || f->age == 3) {
			if (f->lastorders == turn - 2) {
				destroyfaction(f);
				++dropouts[f->age-2];
			}
		}
	}
	/* Clear away debris of destroyed factions */

	puts(" - beseitige leere Einheiten und leere Parteien...");

	remove_empty_units();

	/* Auskommentiert: Wenn factions gel�scht werden, zeigen
	 * Spendenpointer ins Leere. */
	/* remove_empty_factions(); */
}
/* ------------------------------------------------------------- */

/* HELFE partei [<ALLES | SILBER | GIB | KAEMPFE | WAHRNEHMUNG>] [NICHT] */

static void
set_ally(unit * u, strlist * S)
{
	ally * sf, ** sfp;
	faction *f;
	int keyword, not_kw;
	char *s;

	f = getfaction();

	if (f == 0 || f->no == 0) {
		cmistake(u, S->s, 66, MSG_EVENT);
		return;
	}
	if (f == u->faction)
		return;

	s = getstrtoken();

	if (!s[0])
		keyword = P_ANY;
	else
		keyword = findparam(s);

	sfp = &u->faction->allies;
#ifdef GROUPS
	{
		attrib * a = a_find(u->attribs, &at_group);
		if (a) sfp = &((group*)a->data.v)->allies;
	}
#endif
	for (sf=*sfp; sf; sf = sf->next)
		if (sf->faction == f)
			break;	/* Gleich die passende raussuchen, wenn vorhanden */

	not_kw = getparam();		/* HELFE partei [modus] NICHT */

	if (!sf) {
		if (keyword == P_NOT || not_kw == P_NOT) {
			/* Wir helfen der Partei gar nicht... */
			return;
		} else {
			sf = calloc(1, sizeof(ally));
			sf->faction = f;
			sf->status = 0;
			addlist(sfp, sf);
		}
	}
	switch (keyword) {
	case P_NOT:
	case P_NEUTRAL:
		sf->status = 0;
		break;

	case NOPARAM:
		cmistake(u, S->s, 137, MSG_EVENT);
		return;

	case P_FRIEND:
	case P_ANY:
		if (not_kw == P_NOT)
			sf->status = 0;
		else
			sf->status = HELP_ALL;
		break;

	case P_GIB:
		if (not_kw == P_NOT)
			sf->status = sf->status & (HELP_ALL - HELP_GIVE);
		else
			sf->status = sf->status | HELP_GIVE;
		break;

	case P_SILVER:
		if (not_kw == P_NOT)
			sf->status = sf->status & (HELP_ALL - HELP_MONEY);
		else
			sf->status = sf->status | HELP_MONEY;
		break;

	case P_KAEMPFE:
		if (not_kw == P_NOT)
			sf->status = sf->status & (HELP_ALL - HELP_FIGHT);
		else
			sf->status = sf->status | HELP_FIGHT;
		break;

#ifdef HELFE_WAHRNEHMUNG
	case P_OBSERVE:
		if (not_kw == P_NOT)
			sf->status = sf->status & (HELP_ALL - HELP_OBSERVE);
		else
			sf->status = sf->status | HELP_OBSERVE;
		break;
#endif

	case P_GUARD:
		if (not_kw == P_NOT)
			sf->status = sf->status & (HELP_ALL - HELP_GUARD);
		else
			sf->status = sf->status | HELP_GUARD;
		break;
	}

	if (sf->status == 0) {		/* Alle HELPs geloescht */
		removelist(sfp, sf);
	}
}
/* ------------------------------------------------------------- */

void
set_display(region * r, unit * u, strlist * S)
{
	char **s, *s2;

	s = 0;

	switch (getparam()) {
	case P_BUILDING:
	case P_GEBAEUDE:
		if (!u->building) {
			cmistake(u, S->s, 145, MSG_PRODUCE);
			break;
		}
		if (!fval(u, FL_OWNER)) {
			cmistake(u, S->s, 5, MSG_PRODUCE);
			break;
		}
		if (u->building->type == &bt_generic) {
			cmistake(u, S->s, 279, MSG_PRODUCE);
			break;
		}
		if (u->building->type == &bt_monument && u->building->display[0] != 0) {
			cmistake(u, S->s, 29, MSG_PRODUCE);
			break;
		}
		s = &u->building->display;
		break;

	case P_SHIP:
		if (!u->ship) {
			cmistake(u, S->s, 144, MSG_PRODUCE);
			break;
		}
		if (!fval(u, FL_OWNER)) {
			cmistake(u, S->s, 12, MSG_PRODUCE);
			break;
		}
		s = &u->ship->display;
		break;

	case P_UNIT:
		s = &u->display;
		break;

	case P_PRIVAT:
		usetprivate(u, getstrtoken());
		break;

	case P_REGION:
		if (!u->building) {
			cmistake(u, S->s, 145, MSG_EVENT);
			break;
		}
		if (!fval(u, FL_OWNER)) {
			cmistake(u, S->s, 148, MSG_EVENT);
			break;
		}
		if (u->building != largestbuilding(r,false)) {
			cmistake(u, S->s, 147, MSG_EVENT);
			break;
		}
		s = &r->display;
		break;

	default:
		cmistake(u, S->s, 110, MSG_EVENT);
		break;
	}

	if (!s)
		return;

	s2 = getstrtoken();

	if (strlen(s2) >= DISPLAYSIZE) {
		s2[DISPLAYSIZE] = 0;
		cmistake(u, S->s, 3, MSG_EVENT);
	}
	set_string(&(*s), s2);
}
#ifdef GROUPS
void
set_group(unit * u)
{
	char * s = getstrtoken();
	join_group(u, s);
}
#endif

void
set_name(region * r, unit * u, strlist * S)
{
	char **s, *s2;
	int i;
	param_t p;
	boolean foreign = false;

	s = 0;

	p = getparam();

	if(p == P_FOREIGN) {
		foreign = true;
		p = getparam();
	}

	switch (p) {
	case P_BUILDING:
	case P_GEBAEUDE:
		if (foreign == true) {
			building *b = getbuilding(r);
			unit *uo;

			if (!b) {
				cmistake(u, S->s, 6, MSG_EVENT);
				break;
			}

			if (b->type == &bt_generic) {
				cmistake(u, S->s, 278, MSG_EVENT);
				break;
			}

			if(!fval(b,FL_UNNAMED)) {
				cmistake(u, S->s, 246, MSG_EVENT);
				break;
			}

			uo = buildingowner(r, b);
			if (uo) {
				if (cansee(uo->faction, r, u, 0)) {
					add_message(&uo->faction->msgs, new_message(uo->faction,
						"renamed_building_seen%b:building%u:renamer%r:region", b, u, r));
				} else {
					add_message(&uo->faction->msgs, new_message(uo->faction,
						"renamed_building_notseen%b:building%r:region", b, r));
				}
			}
			s = &b->name;
		} else {
			if (!u->building) {
				cmistake(u, S->s, 145, MSG_PRODUCE);
				break;
			}
			if (!fval(u, FL_OWNER)) {
				cmistake(u, S->s, 148, MSG_PRODUCE);
				break;
			}
			if (u->building->type == &bt_generic) {
				cmistake(u, S->s, 278, MSG_EVENT);
				break;
			}
			sprintf(buf, "Monument %d", u->building->no);
			if (u->building->type == &bt_monument
				&& !strcmp(u->building->name, buf)) {
				cmistake(u, S->s, 29, MSG_EVENT);
				break;
			}
			s = &u->building->name;
			freset(u->building, FL_UNNAMED);
		}
		break;

	case P_FACTION:
		if (foreign == true) {
			faction *f;

			f = getfaction();
			if (!f) {
				cmistake(u, S->s, 66, MSG_EVENT);
				break;
			}
			if (f->age < 10) {
				cmistake(u, S->s, 248, MSG_EVENT);
				break;
			}
			if (!fval(f,FL_UNNAMED)) {
				cmistake(u, S->s, 247, MSG_EVENT);
				break;
			}
			if (cansee(f, r, u, 0)) {
				add_message(&f->msgs, new_message(f,
					"renamed_faction_seen%u:renamer%r:region", u, r));
			} else {
				add_message(&f->msgs, new_message(f,
					"renamed_faction_notseen%r:region", r));
			}
			s = &f->name;
		} else {
			s = &u->faction->name;
			freset(u->faction, FL_UNNAMED);
		}
		break;

	case P_SHIP:
		if (foreign == true) {
			ship *sh = getship(r);
			unit *uo;

			if (!sh) {
				cmistake(u, S->s, 20, MSG_EVENT);
				break;
			}
			if (!fval(sh,FL_UNNAMED)) {
				cmistake(u, S->s, 245, MSG_EVENT);
				break;
			}
			uo = shipowner(r,sh);
			if (uo) {
				if (cansee(uo->faction, r, u, 0)) {
					add_message(&uo->faction->msgs, new_message(uo->faction,
						"renamed_ship_seen%h:ship%u:renamer%r:region", sh, u, r));
				} else {
					add_message(&uo->faction->msgs, new_message(uo->faction,
						"renamed_ship_notseen%h:ship%r:region", sh, r));
				}
			}
			s = &sh->name;
		} else {
			if (!u->ship) {
				cmistake(u, S->s, 144, MSG_PRODUCE);
				break;
			}
			if (!fval(u, FL_OWNER)) {
				cmistake(u, S->s, 12, MSG_PRODUCE);
				break;
			}
			s = &u->ship->name;
			freset(u->ship, FL_UNNAMED);
		}
		break;

	case P_UNIT:
		if (foreign == true) {
			unit *u2 = getunit(r, u);
			if (!u2 || !cansee(u->faction, r, u2, 0)) {
				cmistake(u, S->s, 64, MSG_EVENT);
				break;
			}
			if (!fval(u,FL_UNNAMED)) {
				cmistake(u, S->s, 244, MSG_EVENT);
				break;
			}
			if (cansee(u2->faction, r, u, 0)) {
				add_message(&u2->faction->msgs, new_message(u2->faction,
					"renamed_seen%u:renamer%u:renamed%r:region", u, u2, r));
			} else {
				add_message(&u2->faction->msgs, new_message(u2->faction,
					"renamed_notseen%u:renamed%r:region", u2, r));
			}
			s = &u2->name;
		} else {
			s = &u->name;
			freset(u, FL_UNNAMED);
		}
		break;

	case P_REGION:
		if (!u->building) {
			cmistake(u, S->s, 145, MSG_EVENT);
			break;
		}
		if (!fval(u, FL_OWNER)) {
			cmistake(u, S->s, 148, MSG_EVENT);
			break;
		}
		if (u->building != largestbuilding(r,false)) {
			cmistake(u, S->s, 147, MSG_EVENT);
			break;
		}
		s = &r->land->name;
		break;

	default:
		cmistake(u, S->s, 109, MSG_EVENT);
		break;
	}

	if (!s)
		return;

	s2 = getstrtoken();

	if (!s2[0]) {
		cmistake(u, S->s, 84, MSG_EVENT);
		return;
	}
	for (i = 0; s2[i]; i++)
		if (s2[i] == '(')
			break;

	if (s2[i]) {
		cmistake(u, S->s, 112, MSG_EVENT);
		return;
	}
	set_string(&(*s), s2);
}
/* ------------------------------------------------------------- */

static void
deliverMail(faction * f, region * r, unit * u, const char *s, unit * receiver)
{
	char message[DISPLAYSIZE + 1];


	strcpy(message, strcheck(s, DISPLAYSIZE));

	if (!receiver) { /* BOTSCHAFT an PARTEI */
		add_message(&f->msgs,
			new_message(f, "unitmessage%r:region%u:unit%s:message", r, u, message));
	}
	else {					/* BOTSCHAFT an EINHEIT */
		unit *emp = receiver;
		if (cansee(f, r, u, 0))
			sprintf(buf, "Eine Botschaft von %s: '%s'", unitname(u), message);
		else
			sprintf(buf, "Eine anonyme Botschaft: '%s'", message);
		addstrlist(&emp->botschaften, buf);
	}
}

static void
mailunit(region * r, unit * u, int n, strlist * S, const char * s)
{
	unit *u2;		/* nur noch an eine Unit m�glich */

	u2=findunitr(r,n);

	if (u2 && cansee(u->faction, r, u2, 0)) {
		deliverMail(u2->faction, r, u, s, u2);
	}
	else
		cmistake(u, S->s, 64, MSG_MESSAGE);
	/* Immer eine Meldung - sonst k�nnte man so getarnte EHs enttarnen:
	 * keine Meldung -> EH hier. */
}

static void
mailfaction(unit * u, int n, strlist * S, const char * s)
{
	faction *f;

	f = findfaction(n);
	if (f && n>0)
		deliverMail(f, u->region, u, s, NULL);
	else
		cmistake(u, S->s, 66, MSG_MESSAGE);
}

void
distributeMail(region * r, unit * u, strlist * S)
{
	unit *u2;
	char *s;
	int n;

	s = getstrtoken();

	/* Falls kein Parameter, ist das eine Einheitsnummer;
	 * das F�llwort "AN" mu� wegfallen, da g�ltige Nummer! */

	if(strcasecmp(s, "an") == 0)
		s = getstrtoken();

	switch (findparam(s)) {

	case P_REGION:				/* k�nnen alle Einheiten in der Region sehen */
		s = getstrtoken();
		if (!s[0]) {
			cmistake(u, S->s, 30, MSG_MESSAGE);
			return;
		} else {
			if (strlen(s) >= DISPLAYSIZE) {
				s[DISPLAYSIZE] = 0;
				cmistake(u, S->s, 111, MSG_MESSAGE);
			}
			sprintf(buf, "von %s: '%s'", unitname(u), s);
			addmessage(r, 0, buf, MSG_MESSAGE, ML_IMPORTANT);
			return;
		}
		break;

	case P_FACTION:
		{
			boolean see = false;

			n = getfactionid();

			for(u2=r->units; u2; u2=u2->next) {
				if(u2->faction->no == n && seefaction(u->faction, r, u2, 0)) {
					see = true;
					break;
				}
			}

			if(see == false) {
				cmistake(u, S->s, 66, MSG_MESSAGE);
				return;
			}

			s = getstrtoken();
			if (!s[0]) {
				cmistake(u, S->s, 30, MSG_MESSAGE);
				return;
			}
			mailfaction(u, n, S, s);
		}
		break;

	case P_UNIT:
		{
			boolean see = false;
			n = getid();

			for(u2=r->units; u2; u2=u2->next) {
				if(u2->no == n && cansee(u->faction, r, u2, 0)) {
					see = true;
					break;
				}
			}

			if(see == false) {
				cmistake(u, S->s, 63, MSG_MESSAGE);
				return;
			}

			s = getstrtoken();
			if (!s[0]) {
				cmistake(u, S->s, 30, MSG_MESSAGE);
				return;
			}
			mailunit(r, u, n, S, s);
		}
		break;

	case P_BUILDING:
	case P_GEBAEUDE:
		{
			building *b = getbuilding(r);

			if(!b) {
				cmistake(u, S->s, 6, MSG_MESSAGE);
				return;
			}

			s = getstrtoken();

			if (!s[0]) {
				cmistake(u, S->s, 30, MSG_MESSAGE);
				return;
			}

			for (u2 = r->units; u2; u2 = u2->next) freset(u2->faction, FL_DH);

			for(u2=r->units; u2; u2=u2->next) {
				if(u2->building == b && !fval(u2->faction, FL_DH)
						&& cansee(u->faction, r, u2, 0)) {
					mailunit(r, u, u2->no, S, s);
					fset(u2->faction, FL_DH);
				}
			}
		}
		break;

	case P_SHIP:
		{
			ship *sh = getship(r);

			if(!sh) {
				cmistake(u, S->s, 20, MSG_MESSAGE);
				return;
			}

			s = getstrtoken();

			if (!s[0]) {
				cmistake(u, S->s, 30, MSG_MESSAGE);
				return;
			}

			for (u2 = r->units; u2; u2 = u2->next) freset(u2->faction, FL_DH);

			for(u2=r->units; u2; u2=u2->next) {
				if(u2->ship == sh && !fval(u2->faction, FL_DH)
						&& cansee(u->faction, r, u2, 0)) {
					mailunit(r, u, u2->no, S, s);
					fset(u2->faction, FL_DH);
				}
			}
		}
		break;

	default:
		cmistake(u, S->s, 149, MSG_MESSAGE);
		return;
	}
}

void
mail(void)
{
	region *r;
	unit *u;
	strlist *S;

	puts(" - verschicke Botschaften...");

	for (r = regions; r; r = r->next)
		for (u = r->units; u; u = u->next)
			for (S = u->orders; S; S = S->next)
				if (igetkeyword(S->s) == K_MAIL)
					distributeMail(r, u, S);
}
/* ------------------------------------------------------------- */

void
report_option(unit * u, const char * sec, char *cmd)
{
	const messageclass * mc;
	const char *s;

	mc = mc_find(sec);

	if (mc == NULL) {
		cmistake(u, findorder(u, cmd), 135, MSG_EVENT);
		return;
	}
	s = getstrtoken();
	if (s[0])
		set_msglevel(&u->faction->warnings, mc->name, atoi(s));
	else
		set_msglevel(&u->faction->warnings, mc->name, -1);
}

void
set_passw(void)
{
	region *r;
	unit *u;
	strlist *S;
	char *s;
	int o, i;
	magic_t mtyp;

	puts(" - setze Passw�rter, Adressen und Format, Abstimmungen");

	for (r = regions; r; r = r->next)
		for (u = r->units; u; u = u->next)
			for (S = u->orders; S; S = S->next) {
				switch (igetkeyword(S->s)) {
				case NOKEYWORD:
					cmistake(u, S->s, 22, MSG_EVENT);
					break;

				case K_BANNER:
					s = getstrtoken();

					set_string(&u->faction->banner, s);
					add_message(&u->faction->msgs, new_message(u->faction,
						"changebanner%s:value", gc_add(strdup(u->faction->banner))));
					break;

				case K_EMAIL:
					s = getstrtoken();

					if (!s[0]) {
						cmistake(u, S->s, 85, MSG_EVENT);
						break;
					}
					if (strstr(s, "internet:") || strchr(s, ' ')) {
						cmistake(u, S->s, 117, MSG_EVENT);
						break;
					}
					set_string(&u->faction->email, s);
					add_message(&u->faction->msgs, new_message(u->faction,
						"changemail%s:value", gc_add(strdup(u->faction->email))));
					break;

				case K_PASSWORD:
					{
						char pbuf[32];
						int i;

						s = getstrtoken();

						if (!s || !*s) {
							for(i=0; i<6; i++) pbuf[i] = (char)(97 + rand() % 26);
							pbuf[6] = 0;
						} else {
							boolean pwok = true;
							char *c;

							strncpy(pbuf, s, 31);
							pbuf[31] = 0;
							c = pbuf;
							while(*c) {
								if(!isalnum(*c)) pwok = false;
								c++;
							}
							if(pwok == false) {
								cmistake(u, S->s, 283, MSG_EVENT);
								for(i=0; i<6; i++) pbuf[i] = (char)(97 + rand() % 26);
								pbuf[6] = 0;
							}
						}
						set_string(&u->faction->passw, pbuf);
						add_message(&u->faction->msgs, new_message(u->faction,
							"changepasswd%s:value", gc_add(strdup(u->faction->passw))));
					}
					break;

				case K_REPORT:
					s = getstrtoken();
					i = atoi(s);
					sprintf(buf, "%d", i);
					if (!strcmp(buf, s)) {
						/* int level;
						level = geti();
						not implemented yet. set individual levels for f->msglevels */
					} else {
						report_option(u, s, S->s);
					}
					break;

				case K_LOCALE:
					s = getstrtoken();
#define LOCALES
#ifdef LOCALES
					if (find_locale(s)) {
						u->faction->locale = find_locale(s);
					} else {
						cmistake(u, S->s, 257, MSG_EVENT);
					}
#else
					if(strcmp(s, "de") == 0) {
						u->faction->locale = find_locale(s);
					} else {
						cmistake(u, S->s, 257, MSG_EVENT);
					}
#endif
					break;
				case K_SEND:
					s = getstrtoken();
					o = findoption(s);
					if (o == -1) {
						cmistake(u, S->s, 135, MSG_EVENT);
					} else {
						i = (int) pow(2, o);
						if (getparam() == P_NOT) {
							u->faction->options = u->faction->options & ~i;
						} else {
							u->faction->options = u->faction->options | i;
							if(i == O_COMPRESS) u->faction->options &= ~O_BZIP2;
							if(i == O_BZIP2) u->faction->options &= ~O_COMPRESS;
						}
					}
					break;

				case K_MAGIEGEBIET:
					if(u->faction->magiegebiet != 0) {
						mistake(u, S->s, "Die Partei hat bereits ein Magiegebiet",
							MSG_EVENT);
					} else {
						mtyp = getmagicskill();
						if(mtyp == M_NONE) {
							mistake(u, S->s, "Syntax: MAGIEGEBIET <Gebiet>", MSG_EVENT);
						} else {
							region *r2;
							unit   *u2;
							sc_mage *m;
							region *last = lastregion(u->faction);

							u->faction->magiegebiet = mtyp;

							for(r2 = firstregion(u->faction); r2 != last; r2 = r2->next) {
								for(u2 = r->units; u2; u2 = u2->next) {
									if(u2->faction == u->faction
											&& get_skill(u2, SK_MAGIC) > 0){
										m = get_mage(u2);
										m->magietyp = mtyp;
									}
								}
							}
						}
					}
					break;
				}
			}
}

boolean
display_item(faction *f, unit *u, const item_type * itype)
{
	FILE *fp;
	char t[NAMESIZE + 1];
	char filename[MAX_PATH];
	const char *name;

	if (u && *i_find(&u->items, itype) == NULL) return false;
	name = locale_string(NULL, resourcename(itype->rtype, 0));
	sprintf(filename, "%s/%s/items/%s", resourcepath(), locale_name(f->locale), name);
	fp = fopen(filename, "r");
	if (!fp) {
		sprintf(filename, "%s/%s/items/%s", resourcepath(), locale_name(NULL), name);
		fp = fopen(filename, "r");
		if (!fp) return false;
	}

	sprintf(buf, "%s: ", name);

	while (fgets(t, NAMESIZE, fp) != NULL) {
		if (t[strlen(t) - 1] == '\n') {
			t[strlen(t) - 1] = 0;
		}
		strcat(buf, t);
	}
	fclose(fp);

	addmessage(0, f, buf, MSG_EVENT, ML_IMPORTANT);

	return true;
}

boolean
display_potion(faction *f, unit *u, const potion_type * ptype)
{
	attrib *a;

	if(ptype==NULL ||
		2*ptype->level > effskill(u,SK_ALCHEMY))
		return false;

	a = a_find(f->attribs, &at_showitem);
	while (a && a->data.v != ptype) a=a->next;
	if (!a) {
		a = a_add(&f->attribs, a_new(&at_showitem));
		a->data.v = (void*) ptype->itype;
	}

	return true;
}

boolean
display_race(faction *f, unit *u, int i)
{
	FILE *fp;
	char t[NAMESIZE + 1];
	char filename[256];
	const char *name;

	if(u && u->race != i) return false;
	name = race[i].name[0];

	sprintf(filename, "showdata/%s", name);
	fp = fopen(filename, "r");
	if(!fp) return false;

	sprintf(buf, "%s: ", race[i].name[0]);

	while (fgets(t, NAMESIZE, fp) != NULL) {
		if (t[strlen(t) - 1] == '\n') {
			t[strlen(t) - 1] = 0;
		}
		strcat(buf, t);
	}
	fclose(fp);

	addmessage(0, f, buf, MSG_EVENT, ML_IMPORTANT);

	return true;
}

void
instant_orders(void)
{
	region *r;
	unit *u;
	strlist *S;
	char *s;
	char *param;
	spell *spell;
#ifdef NEW_ITEMS
	const item_type * itype;
#else
	potion_t potion;
	item_t item;
#endif
	const potion_type * ptype;
	faction *f;
	attrib *a;
	race_t race;
	int level = 0;	/* 0 = MAX */

	puts(" - Kontakte, Hilfe, Status, Kampfzauber, Texte, Bewachen (aus), Zeigen");

	for (f = factions; f; f = f->next) {
#ifdef NEW_ITEMS
		a=a_find(f->attribs, &at_showitem);
		while(a!=NULL) {
			const item_type * itype = (const item_type *)a->data.v;
			const potion_type * ptype = resource2potion(itype->rtype);
			if (ptype!=NULL) {
				attrib * n = a->nexttype;
				/* potions werden separat behandelt */
				display_item(f, NULL, (const item_type *)a->data.v);
				a_remove(&f->attribs, a);
				a = n;
			} else a = a->nexttype;
		}
#else
		while((a=a_find(f->attribs, &at_show_item_description))!=NULL) {
			display_item(f, NULL, (item_t)(a->data.i));
			a_remove(&f->attribs, a);
		}
#endif
	}

	for (r = regions; r; r = r->next)
		for (u = r->units; u; u = u->next) {
#ifdef GROUPS
			for (S = u->orders; S; S = S->next)
			{
				if (igetkeyword(S->s)==K_GROUP) {
					set_group(u);
				}
			}
#endif
			for (S = u->orders; S; S = S->next)

				switch (igetkeyword(S->s)) {
				case K_URSPRUNG:
					{
						int px, py;

						px = atoi(getstrtoken());
						py = atoi(getstrtoken());

						set_ursprung(u->faction, getplaneid(r), px, py);
					}
					break;

				case K_ALLY:
					set_ally(u, S);
					break;

				case K_SETSTEALTH:
					setstealth(u, S);
					break;

				case K_STATUS:
					param = getstrtoken();
					switch (findparam(param)) {
					case P_NOT:
						u->status = ST_AVOID;
						break;

					case P_BEHIND:
						u->status = ST_BEHIND;
						break;

					case P_FLEE:
						u->status = ST_FLEE;
						break;

					case P_VORNE:
						u->status = ST_FIGHT;
						break;
#ifdef NOAID
					case P_HELP:
						param = getstrtoken();
						if(findparam(param) == P_NOT) {
							fset(u, FL_NOAID);
						} else {
							fset(u, FL_NOAID);
						}
						break;
#endif

					default:
						if (strlen(param)) {
							mistake(u, S->s, "unbekannter Kampfstatus", MSG_EVENT);
						} else {
							u->status = ST_FIGHT;
						}
					}
					break;


				/* KAMPFZAUBER [[STUFE n] "<Spruchname>"] [NICHT] */
				case K_COMBAT:

					s = getstrtoken();

					/* KAMPFZAUBER [NICHT] l�scht alle gesetzten Kampfzauber */
					if (!s || *s == 0 || findparam(s) == P_NOT) {
						unset_combatspell(u, 0);
						break;
					}

					/* Optional: STUFE n */
					if (findparam(s) == P_LEVEL) {
						/* Merken, setzen kommt erst sp�ter */
						s = getstrtoken();
						level = atoi(s);
						level = max(0, level);
						s = getstrtoken();
					}

					spell = find_spellbyname(u, s);

					if(!spell){
						cmistake(u, S->s, 169, MSG_MAGIC);
						break;
					}

					/* KAMPFZAUBER "<Spruchname>" NICHT  l�scht diesen speziellen
					 * Kampfzauber */
					s = getstrtoken();
					if(findparam(s) == P_NOT){
						unset_combatspell(u,spell);
						break;
					}

					/* KAMPFZAUBER "<Spruchname>"  setzt diesen Kampfzauber */
					set_combatspell(u,spell, S->s, level);
					break;

				case K_DISPLAY:
					set_display(r, u, S);
					break;
				case K_NAME:
					set_name(r, u, S);
					break;

				case K_GUARD:
					if (fval(u, FL_HUNGER)) {
						cmistake(u, S->s, 223, MSG_EVENT);
						break;
					}
					if (getparam() == P_NOT)
						setguard(u, GUARD_NONE);
					break;

				case K_RESHOW:
					s = getstrtoken();

					if(findparam(s) == P_ANY) {
						s = getstrtoken();
						if(findparam(s) == P_ZAUBER) {
							a_removeall(&u->faction->attribs, &at_seenspell);
						} else {
							cmistake(u, S->s, 222, MSG_EVENT);
						}
						break;
					}

					spell = find_spellbyname(u, s);
					race = findrace(s);
					itype = finditemtype(s, u->faction->locale);
					if (spell == NULL && itype == NULL && race == NORACE)
					{
						cmistake(u, S->s, 21, MSG_EVENT);
						break;
					} else if (knowsspell(r, u, spell)) {
						attrib *a = a_find(u->faction->attribs, &at_seenspell);
						while (a && a->data.i!=spell->id) a = a->nexttype;
						if (a!=NULL) a_remove(&u->faction->attribs, a);
					} else if (itype != NULL) {
						if((ptype = resource2potion(item2resource(itype))) != NULL) {
							if(display_potion(u->faction, u, ptype) == false) {
								cmistake(u, S->s, 21, MSG_EVENT);
							}
						} else {
							if(display_item(u->faction, u, itype) == false) {
								cmistake(u, S->s, 21, MSG_EVENT);
							}
						}
					} else if (race < MAXRACES){
						if (display_race(u->faction, u, (int)race) == false){
							cmistake(u, S->s, 21, MSG_EVENT);
						}
					}
					break;
				}
		}

}
/* ------------------------------------------------------------- */
/* Beachten: einige Monster sollen auch unbewaffent die Region bewachen
 * k�nnen */
void
remove_unequipped_guarded(void)
{
	region *r;
	unit *u;

	for (r = regions; r; r = r->next)
		for (u = r->units; u; u = u->next) {
			int g = getguard(u);
			if (g && !armedmen(u))
				setguard(u, GUARD_NONE);
		}
}

void
bewache_an(void)
{
	region *r;
	unit *u;
	strlist *S;

	/* letzte schnellen befehle - bewache */
	for (r = regions; r; r = r->next)
		for (u = r->units; u; u = u->next)
			if (!fval(u, FL_MOVED)) {
				for (S = u->orders; S; S = S->next) {
					if (igetkeyword(S->s) == K_GUARD && getparam() != P_NOT) {
						if (rterrain(r) != T_OCEAN) {
							if (!illusionary(u) && u->race != RC_SPELL) {
#ifdef WACH_WAFF
								if (!armedmen(u)) {
									mistake(u, S->s,
										"Die Einheit ist nicht bewaffnet und kampff�hig", MSG_EVENT);
									continue;
								}
#endif
								guard(u, GUARD_ALL);
							} else {
								cmistake(u, S->s, 95, MSG_EVENT);
							}
						} else {
							cmistake(u, S->s, 2, MSG_EVENT);
						}
					}
				}
			}

}

void
sinkships(void)
{
	region *r;

	/* Unbemannte Schiffe k�nnen sinken */
	for (r = regions; r; r = r->next) {
		ship *sh;

		list_foreach(ship, r->ships, sh) {
			if (rterrain(r) == T_OCEAN && (!enoughsailors(r, sh) || !kapitaen(r, sh))) {
				/* Schiff nicht seet�chtig */
				damage_ship(sh, 0.30);
			}
			if (!shipowner(r, sh)) {
				damage_ship(sh, 0.05);
			}
			if (sh->damage >= sh->size * DAMAGE_SCALE)
				destroy_ship(sh, r);
		}
		list_next(sh);
	}
}

/* The following functions do not really belong here: */
#include "eressea.h"
#include "build.h"

static void
reorder_owners(region * r)
{
	unit ** up=&r->units, ** useek;
	building * b=NULL;
	ship * sh=NULL;
	size_t len = listlen(r->units);

	for (b = r->buildings;b;b=b->next) {
		unit ** ubegin = up;
		unit ** uend = up;

		useek = up;
		while (*useek) {
			unit * u = *useek;
			if (u->building==b) {
				unit ** insert;
				if (fval(u, FL_OWNER)) insert=ubegin;
				else insert = uend;
				if (insert!=useek) {
					*useek = u->next; /* raus aus der liste */
					u->next = *insert;
					*insert = u;
				}
				if (insert==uend) uend=&u->next;
			}
			if (*useek==u) useek = &u->next;
		}
		up = uend;
	}

	useek=up;
	while (*useek) {
		unit * u = *useek;
		assert(!u->building);
		if (u->ship==NULL) {
			if (fval(u, FL_OWNER)) {
				log_warning(("[reorder_owners] Einheit %s war Besitzer von nichts.\n", unitname(u)));
				freset(u, FL_OWNER);
			}
			if (useek!=up) {
				*useek = u->next; /* raus aus der liste */
				u->next = *up;
				*up = u;
			}
			up = &u->next;
		}
		if (*useek==u) useek = &u->next;
	}

	for (sh = r->ships;sh;sh=sh->next) {
		unit ** ubegin = up;
		unit ** uend = up;

		useek = up;
		while (*useek) {
			unit * u = *useek;
			if (u->ship==sh) {
				unit ** insert;
				if (fval(u, FL_OWNER)) insert=ubegin;
				else insert = uend;
				if (insert!=useek) {
					*useek = u->next; /* raus aus der liste */
					u->next = *insert;
					*insert = u;
				}
				if (insert==uend) uend=&u->next;
			}
			if (*useek==u) useek = &u->next;
		}
		up = uend;
	}
	assert(len==listlen(r->units));
}

#if 0
static void
reorder_owners(region * r)
{
	unit * us[4096];
	unit * u, **ui = &r->units;
	unit ** first;
	building * b;
	ship * sh;
	int i = 0;
	int end;

	if (rbuildings(r)==NULL && r->ships==NULL) return;
	for (u=r->units;u;u=u->next) us[i++] = u;
	end = i;
	for (b=rbuildings(r);b;b=b->next) {
		first = NULL;
		for (i=0;i!=end;++i) if (us[i] && us[i]->building==b) {
			if (!first) first = ui;
			if (fval(us[i], FL_OWNER) && first != ui) {
				us[i]->next = *first;
				*first = us[i];
			} else {
				*ui = us[i];
				ui = &us[i]->next;
			}
			us[i] = NULL;
		}
		u = buildingowner(r, b);
		if (!fval(u, FL_OWNER)) {
			fprintf(stderr, "WARNING: Geb�ude %s hatte keinen Besitzer. Setze %s\n", buildingname(b), unitname(u));
			fset(u, FL_OWNER);
		}
	}
	for (i=0;i!=end;++i) if (us[i] && us[i]->ship==NULL) {
		*ui = us[i];
		ui = &us[i]->next;
		us[i] = NULL;
	}
	for (sh=r->ships;sh;sh=sh->next) {
		first = NULL;
		for (i=0;i!=end;++i) if (us[i] && us[i]->ship==sh) {
			if (!first) first = ui;
			if (fval(us[i], FL_OWNER) && first != ui) {
				us[i]->next = *first;
				*first = us[i];
			} else {
				*ui = us[i];
				ui = &us[i]->next;
			}
			us[i] = NULL;
		}
		u = shipowner(r, sh);
		if (!fval(u, FL_OWNER)) {
			fprintf(stderr, "WARNING: Das Schiff %s hatte keinen Besitzer. Setze %s\n", shipname(sh), unitname(u));
			fset(u, FL_OWNER);
		}
	}
	*ui = NULL;
}
#endif

static attrib_type at_number = {
	"faction_renum", 
	NULL, NULL, NULL, NULL, NULL,
	ATF_UNIQUE
};

static void
renumber_factions(void)
	/* gibt parteien neue nummern */
{
	struct renum {
		struct renum * next;
		int want;
		faction * faction;
		attrib * attrib;
	} * renum = NULL, * rp;
	faction * f;
	for (f=factions;f;f=f->next) {
		attrib * a = a_find(f->attribs, &at_number);
		int want;
		struct renum ** rn;
		faction * old;

		if (!a) continue;
		want = a->data.i;
		if (fval(f, FF_NEWID)) {
			sprintf(buf, "NUMMER PARTEI %s: Die Partei kann nicht mehr als einmal ihre Nummer wecheln", itoa36(want));
			addmessage(0, f, buf, MSG_MESSAGE, ML_IMPORTANT);
		}
		old = findfaction(want);
		if (old) {
			a_remove(&f->attribs, a);
			sprintf(buf, "Die Nummer %s wird von einer anderen Partei benutzt.", itoa36(want));
			addmessage(0, f, buf, MSG_MESSAGE, ML_IMPORTANT);
			continue;
		}
		if (!faction_id_is_unused(want)) {
			a_remove(&f->attribs, a);
			sprintf(buf, "Die Nummer %s wurde schon einmal von einer anderen Partei benutzt.", itoa36(want));
			addmessage(0, f, buf, MSG_MESSAGE, ML_IMPORTANT);
			continue;
		}
		for (rn=&renum; *rn; rn=&(*rn)->next) {
			if ((*rn)->want>=want) break;
		}
		if (*rn && (*rn)->want==want) {
			a_remove(&f->attribs, a);
			sprintf(buf, "Die Nummer %s wurde bereits einer anderen Partei zugeteilt.", itoa36(want));
			addmessage(0, f, buf, MSG_MESSAGE, ML_IMPORTANT);
		} else {
			struct renum * r = calloc(sizeof(struct renum), 1);
			r->next = *rn;
			r->attrib = a;
			r->faction = f;
			r->want = want;
			*rn = r;
		}
	}
	for (rp=renum;rp;rp=rp->next) {
		a_remove(&rp->faction->attribs, rp->attrib);
		rp->faction->no = rp->want;
		register_faction_id(rp->want);
		fset(rp->faction, FF_NEWID);
	}
	while (renum) {
		rp = renum->next;
		free(renum);
		renum = rp;
	}
}

static void
reorder(void)
{
	region * r;
	for (r=regions;r;r=r->next) {
		unit * u, ** up=&r->units;
		boolean sorted=false;
		while (*up) {
			u = *up;
			if (!fval(u, FL_MARK)) {
				strlist * o;
				for (o=u->orders;o;o=o->next) {
					if (igetkeyword(o->s)==K_SORT) {
						const char * s = getstrtoken();
						param_t p = findparam(s);
						int id = getid();
						unit **vp, *v = findunit(id);
						if (v==NULL || v->faction!=u->faction || v->region!=r) {
							cmistake(u, o->s, 258, MSG_EVENT);
						} else if (v->building != u->building || v->ship!=u->ship) {
							cmistake(u, o->s, 259, MSG_EVENT);
						} else if (fval(u, FL_OWNER)) {
							cmistake(u, o->s, 260, MSG_EVENT);
						} else if (v == u) {
							cmistake(u, o->s, 10, MSG_EVENT);
						} else {
							switch(p) {
							case P_AFTER:
								*up = u->next;
								u->next = v->next;
								v->next = u;
								break;
							case P_BEFORE:
								if (fval(v, FL_OWNER)) {
									cmistake(u, o->s, 261, MSG_EVENT);
								} else {
									vp=&r->units;
									while (*vp!=v) vp=&(*vp)->next;
									*vp = u;
									*up = u->next;
									u->next = v;
								}
								break;
							}
							fset(u, FL_MARK);
							sorted = true;
						}
						break;
					}
				}
			}
			if (u==*up) up=&u->next;
		}
		if (sorted) for (u=r->units;u;u=u->next) freset(u, FL_MARK);
	}
}

static void
renumber(void)
{
	region *r;
	char   *s;
	strlist *S;
	unit * u;
	int i;

	for (r=regions;r;r=r->next) {
		for (u=r->units;u;u=u->next) {
			faction * f = u->faction;
			for (S = u->orders; S; S = S->next) if (igetkeyword(S->s)==K_NUMBER) {
				s = getstrtoken();
				switch(findparam(s)) {

				case P_FACTION:
					s = getstrtoken();
					if(strlen(s)>4) s[4]=0;
					if (*s) {
						int i = atoi36(s);
						attrib * a = a_find(f->attribs, &at_number);
						if (!a) a = a_add(&f->attribs, a_new(&at_number));
						a->data.i = i;
					}
					break;

				case P_UNIT:
					s = getstrtoken();
					if(*s == 0) {
						i = newunitid();
					} else {
						i = atoi36(s);
						if (i<=0 || i>MAX_UNIT_NR) {
							cmistake(u,S->s,114,MSG_EVENT);
							continue;
						}

						if(forbiddenid(i)) {
							cmistake(u,S->s,116,MSG_EVENT);
							continue;
						}

						if(findunitg(i, r)) {
							cmistake(u,S->s,115,MSG_EVENT);
							continue;
						}
					}
					uunhash(u);
					if (!ualias(u)) {
						attrib *a = a_add(&u->attribs, a_new(&at_alias));
						a->data.i = -u->no;
					}
					u->no = i;
					uhash(u);
					break;

				case P_SHIP:
					if(!u->ship) {
						cmistake(u,S->s,144,MSG_EVENT);
						continue;
					}
					if(!fval(u, FL_OWNER)) {
						cmistake(u,S->s,146,MSG_EVENT);
						continue;
					}
					s = getstrtoken();
					if(*s == 0) {
						i = newcontainerid();
					} else {
						i = atoi36(s);
						if (i<=0 || i>MAX_CONTAINER_NR) {
							cmistake(u,S->s,114,MSG_EVENT);
							continue;
						}
						if (findship(i) || findbuilding(i)) {
							cmistake(u,S->s,115,MSG_EVENT);
							continue;
						}
					}
					sunhash(u->ship);
					u->ship->no = i;
					shash(u->ship);
					break;

				case P_BUILDING:
					if(!u->building) {
						cmistake(u,S->s,145,MSG_EVENT);
						continue;
					}
					if(!fval(u, FL_OWNER)) {
						cmistake(u,S->s,148,MSG_EVENT);
						continue;
					}
					s = getstrtoken();
					if(*s == 0) {
						i = newcontainerid();
					} else {
						i = atoi36(s);
						if (i<=0 || i>MAX_CONTAINER_NR) {
							cmistake(u,S->s,114,MSG_EVENT);
							continue;
						}
						if(findship(i) || findbuilding(i)) {
							cmistake(u,S->s,115,MSG_EVENT);
							continue;
						}
					}
					bunhash(u->building);
					u->building->no = i;
					bhash(u->building);
					break;

				default:
					cmistake(u,S->s,239,MSG_EVENT);
				}
			}
		}
	}
	renumber_factions();
}

static void
ageing(void)
{
	faction *f;
	region *r;

	/* altern spezielle Attribute, die eine Sonderbehandlung brauchen?  */
	for(r=regions;r;r=r->next) {
		unit *u;
		for(u=r->units;u;u=u->next) {
			if (is_cursed(u->attribs, C_OLDRACE, 0)){
				curse *c = get_curse(u->attribs, C_OLDRACE, 0);
				if (c->duration == 1 && !(c->flag & CURSE_NOAGE)) {
					u->race = (race_t)c->effect;
					u->irace = (race_t)c->effect;
				}
			}
		}
	}

	/* Borders */
	age_borders();

	/* Factions */
	for (f=factions;f;f=f->next) {
		a_age(&f->attribs);
		handle_event(&f->attribs, "timer", f);
	}

	/* Regionen */
	for (r=regions;r;r=r->next) {
		building ** bp;
		unit ** up;
		ship ** sp;

		a_age(&r->attribs);
		handle_event(&r->attribs, "timer", r);
		/* Einheiten */
		for (up=&r->units;*up;) {
			unit * u = *up;
			a_age(&u->attribs);
			if (u==*up) handle_event(&u->attribs, "timer", u);
			if (u==*up) up = &(*up)->next;
		}
		/* Schiffe */
		for (sp=&r->ships;*sp;) {
			ship * s = *sp;
			a_age(&s->attribs);
			if (s==*sp) handle_event(&s->attribs, "timer", s);
			if (s==*sp) sp = &(*sp)->next;
		}
		/* Geb�ude */
		for (bp=&r->buildings;*bp;) {
			building * b = *bp;
			a_age(&b->attribs);
			if (b==*bp) handle_event(&b->attribs, "timer", b);
			if (b==*bp) bp = &(*bp)->next;
		}
	}
#ifdef OLD_TRIGGER
	/* timeouts */
	countdown_timeouts();
#endif
}

static int
maxunits(faction *f)
{
	return MAXUNITS + 250 * fspecial(f, FS_ADMINISTRATOR);
}

static void
new_units (void)
{
	region *r;
	unit *u, *u2;
	strlist *S, *S2;

	/* neue einheiten werden gemacht und ihre befehle (bis zum "ende" zu
	 * ihnen rueberkopiert, damit diese einheiten genauso wie die alten
	 * einheiten verwendet werden koennen. */

	for (r = regions; r; r = r->next)
		for (u = r->units; u; u = u->next)
			for (S = u->orders; S;) {
				if ((igetkeyword(S->s) == K_MAKE) && (getparam() == P_TEMP)) {
					const attrib * a;
					int g;
					char * name;
					int alias;
					int mu = maxunits(u->faction);

					if(u->faction->no_units >= mu) {
						sprintf(buf, "Eine Partei darf aus nicht mehr als %d "
														"Einheiten bestehen.", mu);
						mistake(u, S->s, buf, MSG_PRODUCE);
						S = S->next;

						while (S) {
							if (igetkeyword(S->s) == K_END)
								break;
							S2 = S->next;
							removelist(&u->orders, S);
							S = S2;
						}
						continue;
					}
					alias = getid();

					name = getstrtoken();
					if (name && strlen(name)==0) name = NULL;
					u2 = createunitid(r, u->faction, 0, u->faction->race, alias, name);

					a_add(&u2->attribs, a_new(&at_alias))->data.i = alias;
					u_setfaction(u2, u->faction);
					u2->building = u->building;
					u2->ship = u->ship;

					a = a_find(u->attribs, &at_group);
					if (a) {
						group * g = (group*)a->data.v;
						a_add(&u2->attribs, a_new(&at_group))->data.v = g;
					}
					u2->status = u->status;
					g = getguard(u);
					if (g) setguard(u2, g);
					else setguard(u, GUARD_NONE);
					/* Temps von parteigetarnten Einheiten
					 * sind wieder parteigetarnt / Martin */
					if (fval(u, FL_PARTEITARNUNG))
						fset(u2, FL_PARTEITARNUNG);
					/* Daemonentarnung */
					if(u->race == RC_DAEMON)
						u2->irace = u->irace;

					S = S->next;

					while (S) {
						if (igetkeyword(S->s) == K_END)
							break;
						S2 = S->next;
						translist(&u->orders, &u2->orders, S);
						S = S2;
					}
				}
				if (S)
					S = S->next;
			}

	/* im for-loop wuerde S = S->next ausgefuehrt, bevor S geprueft wird.
	 * Wenn S aber schon 0x0 ist, fuehrt das zu einem Fehler. Und wenn wir
	 * den while (S) ganz durchlaufen, wird S = 0x0 sein! Dh. wir
	 * sicherstellen, dass S != 0, bevor wir S = S->next auszufuehren! */

}

static void
setdefaults (void)
{
	region *r;
	unit *u;
	strlist *S;

	for (r = regions; r; r = r->next){
		for (u = r->units; u; u = u->next) {
			boolean trade = false;

			set_string(&u->thisorder, u->lastorder);
			for(S = u->orders; S; S = S->next) {
				keyword_t keyword = igetkeyword(S->s);

				switch (keyword) {

					/* Wenn gehandelt wird, darf kein langer Befehl ausgef�hrt
					 * werden. Da Handel erst nach anderen langen Befehlen kommt,
					 * mu� das vorher abgefangen werden. Wir merken uns also
					 * hier, ob die Einheit handelt. */

				case K_BUY:
				case K_SELL:
					trade = true;
					break;

					/* Falls wir MACHE TEMP haben, ignorieren wir es. Alle anderen
					 * Arten von MACHE zaehlen aber als neue defaults und werden
					 * behandelt wie die anderen (deswegen kein break nach case
					 * K_MAKE) - und in thisorder (der aktuelle 30-Tage Befehl)
					 * abgespeichert). */

				case K_MAKE:
					if (getparam() == P_TEMP) break;
				case K_BESIEGE:
				case K_ENTERTAIN:
				case K_FOLLOW:
				case K_RESEARCH:
				case K_SPY:
				case K_STEAL:
				case K_SABOTAGE:
				case K_STUDY:
				case K_TAX:
				case K_TEACH:
				case K_ZUECHTE:
				case K_BIETE:
				case K_PIRACY:
					if (idle (u->faction)) {
						set_string (&u->thisorder, keywords[K_WORK]);
						break;
					}
					/* Ab hier Befehle, die auch eine idle
					 * Faction machen darf: */
				case K_CAST:
					/* dient nur dazu, das neben Zaubern kein weiterer Befehl
					 * ausgef�hrt werden kann, Zaubern ist ein kurzer Befehl */
				case K_ROUTE:
				case K_WORK:
				case K_DRIVE:
				case K_MOVE:
					if(fval(u, FL_HUNGER)) {
						set_string(&u->thisorder, keywords[K_WORK]);
					} else {
						set_string(&u->thisorder, S->s);
					}
					break;

					/* Wird je diese Ausschliesslichkeit aufgehoben, muss man aufpassen
					 * mit der Reihenfolge von Kaufen, Verkaufen etc., damit es Spielern
					 * nicht moeglich ist, Schulden zu machen. */
				}
			}

			/* Wenn die Einheit handelt, mu� der Default-Befehl gel�scht
			 * werden. */

			if(trade == true) set_string(&u->thisorder, "");

			/* thisorder kopieren wir nun nach lastorder. in lastorder steht
			 * der DEFAULT befehl der einheit. da MOVE kein default werden
			 * darf, wird MOVE nicht in lastorder kopiert. MACHE TEMP wurde ja
			 * schon gar nicht erst in thisorder kopiert, so dass MACHE TEMP
			 * durch diesen code auch nicht zum default wird Ebenso soll BIETE
			 * nicht hierher, da i.A. die Einheit dann ja weg ist (und damit
			 * die Einheitsnummer ungueltig). Auch Attackiere sollte nie in
			 * den Default �bernommen werden */

			switch (igetkeyword (u->thisorder)) {
				case K_MOVE:
				case K_BIETE:
				case K_ATTACK:
				break;

			default:
				set_string(&u->lastorder, u->thisorder);
			}
			/* Attackiere sollte niemals Default werden */
			if (igetkeyword(u->lastorder) == K_ATTACK)
				set_string(&u->lastorder, keywords[K_WORK]);

		}
	}
}


static int
use_item(unit * u, const item_type * itype, const char * cmd)
{
	int i;

	if (itype->use==NULL) {
		cmistake(u, cmd, 76, MSG_PRODUCE);
		return EUNUSABLE;
	}
	i = new_get_pooled(u, itype->rtype, GET_DEFAULT);

	if (i==0) {
		cmistake(u, cmd, 43, MSG_PRODUCE);
		return ENOITEM;
	}

	if (itype->skill!=NOSKILL
			&& eff_skill(u, itype->skill, u->region)<itype->minskill) {
		cmistake(u, cmd, 50, MSG_PRODUCE);
		return ENOSKILL;
	}
	return itype->use(u, itype, cmd);
}


static int
canheal(const unit *u)
{
	switch(u->race) {
	case RC_DAEMON:
		return 15;
		break;
	case RC_GOBLIN:
		return 20;
		break;
	case RC_TROLL:
		return 15;
		break;
	case RC_FIREDRAGON:
	case RC_DRAGON:
	case RC_WYRM:
		return 10;
		break;
	}
	if (race[u->race].flags & RCF_NOHEAL) return 0;
	return 10;
}

static void
monthly_healing(void)
{
	region *r;
	unit *u;
	int  p;

	for (r = regions; r; r = r->next) {
		for (u = r->units; u; u = u->next) {
			int umhp;

			if((race[u->race].flags & RCF_NOHEAL) || fval(u, FL_HUNGER))
				continue;

			if(rterrain(r) == T_OCEAN && !u->ship && !(race[u->race].flags & RCF_SWIM))
				continue;

			umhp = unit_max_hp(u) * u->number;

			if(fspecial(u->faction, FS_REGENERATION)) {
					u->hp = umhp;
					continue;
			}

			/* Effekt von Elixier der Macht schwindet */
			if (u->hp > umhp) {
				u->hp -= (int) ceil((u->hp - umhp) / 2.0);
				if (u->hp < umhp)
					u->hp = umhp;
			}
			else if (u->hp < umhp && (p=canheal(u)) > 0) {
				/* Mind 1 HP wird pro Runde geheilt, weil angenommen wird,
				   das alle Personen mind. 10 HP haben. */
				int max_unit = max(umhp, u->number * 10);
#ifdef NEW_TAVERN
				struct building * b = inside_building(u);
				const struct building_type * btype = b?b->type:NULL;
				if (btype == &bt_inn) {
					max_unit = max_unit * 3 / 2;
				}
#endif
				/* Aufaddieren der geheilten HP. */
				u->hp = min(u->hp + max_unit/(100/p), umhp);
				if (u->hp < umhp && (rand() % 10 < max_unit % 10))
					++u->hp;
			}
		}
	}
}

static void
defaultorders (void)
{
	region *r;
	unit *u;
	char * c;
	int i;
	strlist *s;
	list_foreach(region, regions, r) {
		list_foreach(unit, r->units, u) {
			list_foreach(strlist, u->orders, s) {
				switch (igetkeyword(s->s)) {
				case K_DEFAULT:
					c = getstrtoken();
					i = atoi(c);
					switch (i) {
					case 0 :
						if (c[0]=='0') set_string(&u->lastorder, getstrtoken());
						else set_string(&u->lastorder, c);
						s->s[0]=0;
						break;
					case 1 :
						sprintf(buf, "%s \"%s\"", keywords[K_DEFAULT], getstrtoken());
						set_string(&s->s, buf);
						break;
					default :
						sprintf(buf, "%s %d \"%s\"", keywords[K_DEFAULT], i-1, getstrtoken());
						set_string(&s->s, buf);
						break;
					}
				}
			}
			list_next(s);
		}
		list_next(u);
	}
	list_next(r);
}

#ifdef SKILLFIX_SAVE
void write_skillfix(void);
#endif

void
processorders (void)
{
	faction *f;
	region *r;
	unit *u;
	strlist *S;

	puts(" - neue Einheiten erschaffen...");
	if (turn == 0) srand(time((time_t *) NULL));
	else srand(turn);
	new_units();
	puts(" - Monster KI...");
	plan_monsters();
	set_passw();		/* und pruefe auf illegale Befehle */
	puts(" - Defaults und Instant-Befehle...");
	setdefaults();
	instant_orders();
	mail();
	puts(" - Altern");

	for (f = factions; f; f = f->next) {
		f->age = f->age + 1;
		if (f->age < IMMUN_GEGEN_ANGRIFF) {
			add_message(&f->msgs, new_message(f,
				"newbieimmunity%i:turns", IMMUN_GEGEN_ANGRIFF - f->age));
		}
	}

	puts(" - Benutzen");

	for (r = regions; r; r = r->next) {
		for (u = r->units; u; u = u->next) {
			for (S = u->orders; S; S = S->next) {
				if (igetkeyword(S->s) == K_USE) {
					char * t = getstrtoken();
					const item_type * itype = finditemtype(t, u->faction->locale);

					if (itype!=NULL) {
						use_item(u, itype, S->s);
					} else {
						cmistake(u, S->s, 43, MSG_PRODUCE);
					}
				}
			}
		}
	}

	puts(" - Kontaktieren, Betreten von Schiffen und Geb�uden (1.Versuch)");
	do_misc(0);
	puts(" - Verlassen");
	do_leave();

	puts(" - Kontakte l�schen");
	remove_contacts();

	puts(" - Jihad-Angriffe");
	jihad_attacks();

	puts(" - Attackieren");
	if(nobattle == false) do_battle();
	if (turn == 0) srand(time((time_t *) NULL));
	else srand(turn);

	puts(" - Heilung");
	monthly_healing();

	puts(" - Belagern");
	do_siege();

	puts(" - Initialisieren des Pools, Reservieren");
	init_pool();

	puts(" - Kontaktieren, Betreten von Schiffen und Geb�uden (2.Versuch)");
	do_misc(1);

	puts(" - Folge setzen");
	follow();

	if (turn == 0) srand(time((time_t *) NULL));
	else srand(turn);

	puts(" - Zerst�ren, Geben, Rekrutieren, Vergessen");
	economics();

	puts(" - Geb�udeunterhalt (1. Versuch)");
	maintain_buildings(false);

	puts(" - Sterben");
	quit();

	puts(" - Zaubern");
	magic();

	puts(" - Lehren");
	teaching();

	puts(" - Lernen");
	learn();

	puts(" - Produzieren, Geldverdienen, Handeln, Anwerben");
	produce();

	puts(" - Schiffe sinken");
 	sinkships();

	puts(" - Bewegungen");
	movement();

	puts(" - Bewache (an)");
	bewache_an();

	puts(" - Zufallsbegegnungen");
	encounters();

	if (turn == 0) srand(time((time_t *) NULL));
	else srand(turn);

	puts(" - Monster fressen und vertreiben Bauern");
	monsters_kill_peasants();

	puts(" - Zufallsereignisse");
	randomevents();

	puts(" - Auraregeneration");
	regeneration_magiepunkte();

#if NEW_LAEN
	puts(" - Laenwachstum");
	growlaen();
#endif

	puts(" - Defaults setzen");
	defaultorders();

	puts(" - Unterhaltskosten, Nachfrage, Seuchen, Wachstum, Auswanderung");
	demographics();

	puts(" - Geb�udeunterhalt (2. Versuch)");
	maintain_buildings(true);

	puts(" - Jihads setzen");
	set_jihad();

	puts(" - neue Nummern und Reihenfolge");
	renumber();
	reorder();

	puts(" - GM Kommandos");
	gmcommands();

	for (r = regions;r;r=r->next) reorder_owners(r);

	puts(" - Attribute altern");
	ageing();

#ifdef REMOVE_ZERO_SKILLS
	puts(" - Skills ohne Talenttage werden gel�scht");
	remove_zero_skills();
#endif
#ifdef SKILLFIX_SAVE
	write_skillfix();
#endif
}

int
count_migrants (const faction * f)
{
#ifndef NDEBUG
	unit *u = f->units;
	int n = 0;
	while (u) {
		assert(u->faction == f);
		if (u->race != f->race && u->race != RC_ILLUSION && u->race != RC_SPELL 
			&& !nonplayer(u) && !(is_cursed(u->attribs, C_SLAVE, 0)))
		{
			n += u->number;
		}
		u = u->nextF;
	}
	if (f->num_migrants != n)
		puts("FEHLER: Anzahl Migranten falsch");
#endif
	return f->num_migrants;
}
