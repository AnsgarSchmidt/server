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

#define SAFE_COASTS /* Schiffe an der K�ste treiben nicht ab */

#include <config.h>
#include "eressea.h"
#include "movement.h"

#include "alchemy.h"
#include "border.h"
#include "build.h"
#include "building.h"
#include "curse.h"
#include "faction.h"
#include "item.h"
#include "karma.h"
#include "magic.h"
#include "message.h"
#include "plane.h"
#include "race.h"
#include "region.h"
#include "render.h"
#include "spell.h"
#include "ship.h"
#include "skill.h"
#include "unit.h"

/* util includes */
#include <goodies.h>

/* libc includes */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* attributes includes */
#include <attributes/follow.h>
#include <attributes/targetregion.h>
#include <attributes/at_movement.h>

/* TODO: boder_type::move() must be able to change target (wisps) */
extern border_type bt_wisps;
extern item_type it_demonseye;
/* ------------------------------------------------------------- */

typedef struct traveldir {
	int no;
	direction_t dir;
	int age;
} traveldir;

attrib_type at_traveldir = {
	"traveldir",
	DEFAULT_INIT,
	DEFAULT_FINALIZE,
	DEFAULT_AGE,					/* Weil normales Aging an ung�nstiger Stelle */
	DEFAULT_WRITE,
	DEFAULT_READ
};


void
a_traveldir_new_init(attrib *a)
{
	a->data.v = calloc(1, sizeof(traveldir));
}

void
a_traveldir_new_finalize(attrib *a)
{
	free(a->data.v);
}

int
a_traveldir_new_age(attrib *a)
{
	traveldir *t = (traveldir *)(a->data.v);

	t->age--;
	if(t->age == 0) return 0;
	return 1;
}

int
a_traveldir_new_read(attrib *a, FILE *f)
{
	traveldir *t = (traveldir *)(a->data.v);
	int       no, age, dir;

	fscanf(f, "%d %d %d", &no, &dir, &age);
	t->no  = no;
	t->dir = (direction_t)dir;
	t->age = age;
	return AT_READ_OK;
}

void
a_traveldir_new_write(const attrib *a, FILE *f)
{
	  traveldir *t = (traveldir *)(a->data.v);
	  fprintf(f, "%d %d %d ", t->no, (int)t->dir, t->age);
}

attrib_type at_traveldir_new = {
	"traveldir_new",
	a_traveldir_new_init,
	a_traveldir_new_finalize,
	a_traveldir_new_age,
	a_traveldir_new_write,
	a_traveldir_new_read
};

/* ------------------------------------------------------------- */

direction_t
getdirection(const struct locale * lang)
{
	return finddirection(getstrtoken(), lang);
}
/* ------------------------------------------------------------- */

static attrib_type at_driveweight = {
	"driveweight", NULL, NULL, NULL, NULL, NULL
};

static boolean
entrance_allowed(const struct unit * u, const struct region * r)
{
#ifdef REGIONOWNERS
  unit * owner = region_owner(r);
  if (owner==NULL || u->faction==owner->faction) return true;
  if (alliedunit(owner, u->faction, HELP_TRAVEL)) return true;
  if (is_enemy(u->faction, owner->faction)) return true;
  return false;
#endif
  return true;
}

int
personcapacity(const unit *u)
{
#if RACE_CAPACITY
	int cap = u->race->weight+u->race->capacity;
#else
	int cap = u->race->weight+540;

	if (old_race(u->race) == RC_TROLL)
		cap += 540;
#if RACE_ADJUSTMENTS
	else if(old_race(u->race) == RC_GOBLIN)
		cap -= 100;
#endif
#endif
	
	if (fspecial(u->faction, FS_QUICK))
		cap -= 200;

	return cap;
}

static int
eff_weight(const unit *u)
{
	attrib *a = a_find(u->attribs, &at_driveweight);

	if (a) return weight(u) + a->data.i;

	return weight(u);
}

int
ridingcapacity(unit * u)
{
	int n;
	int wagen, pferde;

	n = 0;

	/* Man tr�gt sein eigenes Gewicht plus seine Kapazit�t! Die Menschen
	 * tragen nichts (siehe walkingcapacity). Ein Wagen z�hlt nur, wenn er
	 * von zwei Pferden gezogen wird */

	pferde = min(get_item(u, I_HORSE), effskill(u, SK_RIDING) * u->number * 2);
	if(fval(u->race, RCF_HORSE)) pferde += u->number;

	/* maximal diese Pferde k�nnen zum Ziehen benutzt werden */
	wagen = min(pferde / HORSESNEEDED, get_item(u, I_WAGON));

	n = wagen * WAGONCAPACITY + pferde * HORSECAPACITY;
	return n;
}

int
walkingcapacity(unit * u)
{
	int n, personen, pferde, pferde_fuer_wagen;
	int wagen, wagen_ohne_pferde, wagen_mit_pferden, wagen_mit_trollen;
	/* Das Gewicht, welches die Pferde tragen, plus das Gewicht, welches
	 * die Leute tragen */

	pferde = get_item(u, I_HORSE);
	if (fval(u->race, RCF_HORSE)) {
		pferde += u->number;
		personen = 0;
	} else {
		personen = u->number;
	}
	wagen = get_item(u, I_WAGON);

	pferde_fuer_wagen = min(pferde, effskill(u, SK_RIDING) * u->number * 4);

	/* maximal diese Pferde k�nnen zum Ziehen benutzt werden */

	wagen_mit_pferden = min(wagen, pferde_fuer_wagen / HORSESNEEDED);

	n = wagen_mit_pferden * WAGONCAPACITY;

	if (old_race(u->race) == RC_TROLL) {
		/* 4 Trolle ziehen einen Wagen. */
		/* Unbesetzte Wagen feststellen */
		wagen_ohne_pferde = wagen - wagen_mit_pferden;

		/* Genug Trolle, um die Restwagen zu ziehen? */
		wagen_mit_trollen = min(u->number / 4, wagen_ohne_pferde);

		/* Wagenkapazit�t hinzuz�hlen */
		n += wagen_mit_trollen * WAGONCAPACITY;
		wagen_ohne_pferde -= wagen_mit_trollen;
	}

	n += pferde * HORSECAPACITY;
	n += personen * personcapacity(u);
	/* Goliathwasser */
	n += get_effect(u, oldpotiontype[P_STRONG]) * personcapacity(u);
	n += min(get_item(u, I_TROLLBELT), u->number) * STRENGTHCAPACITY;

	return n;
}

int
canwalk(unit * u)
{
	int wagen, maxwagen;
	int pferde, maxpferde;

	/* workaround: monsters are too stupid to drop items, therefore they have
	 * infinite carrying capacity */

	if (u->faction->no == 0) return 0;

	wagen = get_item(u, I_WAGON);
	pferde = get_item(u, I_HORSE);

	maxwagen = effskill(u, SK_RIDING) * u->number * 2;
	if (old_race(u->race) == RC_TROLL) {
		maxwagen = max(maxwagen, u->number / 4);
	}
	maxpferde = effskill(u, SK_RIDING) * u->number * 4 + u->number;

	if (pferde > maxpferde)
		return 2;

	if (walkingcapacity(u) - eff_weight(u) >= 0)
		return 0;

	/* Stimmt das Gewicht, impliziert dies hier, da� alle Wagen ohne
	 * Zugpferde/-trolle als Fracht aufgeladen wurden: zu viele Pferde hat
	 * die Einheit nicht zum Ziehen benutzt, also nicht mehr Wagen gezogen
	 * als erlaubt. */

	if (wagen > maxwagen)
		return 3;
	/* Es mu� nicht zwingend an den Wagen liegen, aber egal... (man
	 * k�nnte z.B. auch 8 Eisen abladen, damit ein weiterer Wagen als
	 * Fracht draufpa�t) */

	return 1;
}

boolean
canfly(unit *u)
{
	if (get_movement(&u->attribs, MV_CANNOTMOVE)) return false;

	if(get_item(u, I_HORSE)) return false;

	if(get_item(u, I_PEGASUS) >= u->number && effskill(u, SK_RIDING) >= 4)
		return true;

	if (fval(u->race, RCF_FLY)) return true;

	if (get_movement(&u->attribs, MV_FLY)) return true;

	return false;
}

boolean
canswim(unit *u)
{
	if (get_movement(&u->attribs, MV_CANNOTMOVE)) return false;

	if (get_item(u, I_HORSE)) return false;

	if (get_item(u, I_DOLPHIN) >= u->number && effskill(u, SK_RIDING) >= 4)
		return true;

	if(fspecial(u->faction, FS_AMPHIBIAN)) return true;

	if (u->race->flags & RCF_FLY) return true;

	if (u->race->flags & RCF_SWIM) return true;

	if (get_movement(&u->attribs, MV_FLY)) return true;

	if (get_movement(&u->attribs, MV_SWIM)) return true;

	return false;
}

int
canride(unit * u)
{
	int pferde, maxpferde, unicorns, maxunicorns;
	int skill = effskill(u, SK_RIDING);

	unicorns = get_item(u, I_UNICORN);
	pferde = get_item(u, I_HORSE);
	maxunicorns = (skill/5) * u->number;
	maxpferde = skill * u->number * 2;

	if(!(u->race->flags & RCF_HORSE)
		&& ((pferde == 0 && unicorns == 0)
			|| pferde > maxpferde || unicorns > maxunicorns)) {
		return 0;
	}

	if(ridingcapacity(u) - eff_weight(u) >= 0) {
		if(pferde == 0 && unicorns >= u->number && !(u->race->flags & RCF_HORSE)) {
			return 2;
		}
		return 1;
	}

	return 0;
}

boolean
cansail(const region * r, ship * sh)
{
	int n = 0, p = 0;

	 /* sonst ist construction:: size nicht ship_type::maxsize */
	assert(!sh->type->construction || sh->type->construction->improvement==NULL);

	if (sh->type->construction && sh->size!=sh->type->construction->maxsize)
	  return false;
	getshipweight(sh, &n, &p);

	if( is_cursed(sh->attribs, C_SHIP_FLYING, 0) ) {
		if (sh->type->cargo>500*100)
			assert(!"Ein Schiff wurde verzaubert, das zu gro� ist");
		if (n > 10000) return false;
	}
	if (n > shipcapacity(sh)) return false;
	if (p > sh->type->cabins) return false;
	return true;
}

int
enoughsailors(region * r, ship * sh)
{
	int n;
	unit *u;

	n = 0;

	for (u = r->units; u; u = u->next)
		if (u->ship == sh)
			n += eff_skill(u, SK_SAILING, r) * u->number;
	return n >= sh->type->sumskill;
}
/* ------------------------------------------------------------- */

void
do_maelstrom(region *r, unit *u)
{
	int damage;

	damage = rand()%150 - eff_skill(u, SK_SAILING, r)*5;

	if(damage <= 0) {
		add_message(&u->faction->msgs,
			new_message(u->faction, "entermaelstrom%r:region%h:ship%i:damage%i:sink", r, u->ship, damage, 1));
		return;
	}

	damage_ship(u->ship, 0.01*damage);

	if (u->ship->damage >= u->ship->size * DAMAGE_SCALE) {
		ADDMSG(&u->faction->msgs, msg_message("entermaelstrom",
			"region ship damage sink", r, u->ship, damage, 1));
		destroy_ship(u->ship, r);
	} else {
		ADDMSG(&u->faction->msgs, msg_message("entermaelstrom",
			"region ship damage sink", r, u->ship, damage, 0));
	}
}

void
travelthru(unit * u, region * r)
{
	attrib *ru = a_add(&r->attribs, a_new(&at_travelunit));

	ru->data.v = u;
	u->faction->first = 0;
	u->faction->last = 0;
}

ship *
move_ship(ship * sh, region * from, region * to, region ** route)
{
	unit *u = from->units;
	unit **ulist = &to->units;
	direction_t dir;
	attrib			*a;

	if (from!=to) {
		translist(&from->ships, &to->ships, sh);
		sh->region = to;
	}

	while (u) {
		unit *nu = u->next;

		if (u->ship == sh)
		{
			if (route) {
				region **ri = route;
				region **rn;

				while (*ri) {
					rn = ri+1;
					if(*rn) {
						dir = reldirection(*ri, *rn);
						a = a_find((*ri)->attribs, &at_traveldir_new);
						while (a) {
							if(((traveldir *)(a->data.v))->no == sh->no) break;
							a = a->nexttype;
						}
						if(!a) {
							a = a_add(&((*ri)->attribs), a_new(&at_traveldir_new));
							{
								traveldir *t = (traveldir *)(a->data.v);
								t->no = sh->no;
								t->dir = dir;
								t->age = 2;
							}
						} else {
							traveldir *t = (traveldir *)(a->data.v);
							t->dir = dir;
							t->age = 2;
						}
					}
					travelthru(u, *ri++);
				}
			}
			if (from!=to) {
				u->ship = NULL;		/* damit move_unit() kein leave() macht */
				move_unit(u, to, ulist);
				ulist = &u->next;
				u->ship = sh;
			}
			if (route && eff_skill(u, SK_SAILING, from) >= 1) {
				produceexp(u, SK_SAILING, u->number);
			}
		}
		u = nu;
	}
	return sh;
}

void
drifting_ships(region * r)
{
	direction_t d;
	ship *sh, *sh2;
	unit *captain;
	region * route[2] = { 0, 0 };

	if (rterrain(r) == T_OCEAN) {
		for (sh = r->ships; sh;) {
			sh2 = sh->next;

			/* Schiff schon abgetrieben oder durch Zauber gesch�tzt? */
			if (fval(sh, SF_DRIFTED) || is_cursed(sh->attribs, C_SHIP_NODRIFT, 0)) {
				sh = sh2;
				continue;
			}

			/* Kapit�n bestimmen */
			for (captain = r->units; captain; captain = captain->next)
				if (captain->ship == sh
					&& eff_skill(captain, SK_SAILING, r) >= sh->type->cptskill)
					break;
			/* Kapit�n da? Besch�digt? Gen�gend Matrosen?
			 * Gen�gend leicht? Dann ist alles OK. */

			assert(sh->type->construction->improvement==NULL); /* sonst ist construction::size nicht ship_type::maxsize */
			if (captain && sh->size==sh->type->construction->maxsize && enoughsailors(r, sh) && cansail(r, sh)) {
				sh = sh2;
				continue;
			}

			/* Leuchtturm: Ok. */
			if (check_leuchtturm(r, NULL)) {
				sh = sh2;
				continue;
			}

			/* Auswahl einer Richtung: Zuerst auf Land, dann
			 * zuf�llig. Falls unm�gliches Resultat: vergi� es. */

			for (d = 0; d != MAXDIRECTIONS; d++)
				if (rconnect(r, d) && rterrain(rconnect(r, d)) != T_OCEAN)
					break;

			if (d == MAXDIRECTIONS)
				d = (direction_t)(rand() % MAXDIRECTIONS);

			if (!rconnect(r, d)) {
				sh = sh2;
				continue;
			}
			{
				region * r2 = rconnect(r, d);
				int c = 0;
				while (sh->type->coast[c]!=NOTERRAIN) {
					if (rterrain(r2) == sh->type->coast[c]) break;
					++c;
				}
				if (sh->type->coast[c]==NOTERRAIN) {
					sh = sh2;
					continue;
				}
			}
			if(!(terrain[rterrain(rconnect(r, d))].flags & SAIL_INTO)) {
				sh = sh2;
				continue;
			}

			/* Das Schiff und alle Einheiten darin werden nun von r
			 * nach rconnect(r, d) verschoben. Danach eine Meldung. */
#if 0
	/* ich bin fast sicher, das man das nicht braucht. (enno) */
			for (f = factions; f; f = f->next)
				freset(f, FL_DH);
#endif
			if (fval(sh, SF_DRIFTED)) {
				/* wenn schon einmal abgetrieben, dann durchreise eintragen */
				route[0] = r;
				sh = move_ship(sh, r, rconnect(r, d), route);
			} else sh = move_ship(sh, r, rconnect(r, d), NULL);
			if (!sh) {
				sh = sh2;
				continue;
			}

			fset(sh, SF_DRIFTED);

			if (rterrain(rconnect(r, d)) != T_OCEAN) {
				/* Alle Attribute
				 * sicherheitshalber loeschen und
				 * gegebenenfalls neu setzen. */
				sh->coast = back[d];
			}
			damage_ship(sh, 0.02);

			if (sh->damage>=sh->size * DAMAGE_SCALE)
				destroy_ship(sh, rconnect(r, d));

			sh = sh2;
		}
	}
}

char *
coords_or_direction(region *r, faction *f, int dir)
{
	static char lbuf[32];
	plane *pl = getplane(r);

	if(fval(pl, PFL_NOCOORDS)) {
		strcpy(lbuf, locale_string(f->locale, directions[dir]));
	} else {
		sprintf(lbuf, "(%d,%d)",region_x(r,f), region_y(r,f));
	}

	return lbuf;
}

void
ship_in_storm(unit *cap, region *next_point)
{
	int i;

	if(is_cursed(cap->ship->attribs, C_SHIP_NODRIFT, 0)) return;

	add_message(&cap->faction->msgs,
		new_message(cap->faction, "storm%h:ship%r:region%i:sink",
		cap->ship, next_point, cap->ship->damage>=cap->ship->size * DAMAGE_SCALE));

	/* Ver�nderung der Drehrichtung */
	i = rand()%2==0?1:-1;
#if 0 /* make a temporary attribute for this */
	cap->wants += i;
#endif
	/* Merken f�r Sperren des Anlandens. */
	if(i) fset(cap, FL_STORM);
}

#define USE_CREATION 0
/* ------------------------------------------------------------- */

boolean
present(region * r, unit * u)
{
	return (boolean) (u && u->region == r);
}

void
caught_target(region * r, unit * u)
{
	attrib * a = a_find(u->attribs, &at_follow);

	/* Verfolgungen melden */
	/* Misserfolgsmeldung, oder bei erfolgreichem Verfolgen unter
	 * Umstaenden eine Warnung. */

	if (a) {
		unit * target = (unit*)a->data.v;

		if (!present(r, target)) {
			add_message(&u->faction->msgs, new_message(u->faction,
				"followfail%u:unit%u:follower", target, u));
		} else if (!alliedunit(target, u->faction, HELP_ALL)
				   && cansee(target->faction, r, u, 0))
		{
			add_message(&target->faction->msgs, new_message(target->faction,
				"followdetect%u:unit%u:follower", target, u));
		}
	}
}

/* TODO: Unsichtbarkeit bedenken ! */

unit *
bewegung_blockiert_von(unit * reisender, region * r)
{
	unit *u;
	int perception = 0;
	boolean contact = false;
	unit * guard = NULL;

	if (fval(reisender->race, RCF_ILLUSIONARY)) return NULL;
	for (u=r->units;u && !contact;u=u->next) {
		if (getguard(u) & GUARD_TRAVELTHRU) {
			int sk = eff_skill(u, SK_OBSERVATION, r);
			if (get_item(reisender, I_RING_OF_INVISIBILITY) >= reisender->number &&
				!get_item(u, I_AMULET_OF_TRUE_SEEING)) continue;
			if (u->faction==reisender->faction
				|| ucontact(u, reisender)
				|| alliedunit(u, reisender->faction, HELP_GUARD))
			{
				contact = true;
			} else if (sk>=perception) {
				perception = sk;
				guard = u;
			}
		}
	}
	if (!contact && guard) {
		int chance = 30; /* 30% base chance */
		chance += 10 * (perception - eff_stealth(reisender, r));
		chance += 10 * max(guard->number, get_item(guard, I_AMULET_OF_TRUE_SEEING));

		if (rand() % 100 < chance) {
			return guard;
		}
	}
	return NULL;
}

boolean
is_guardian(unit * u2, unit *u, unsigned int mask)
{
	if (u2->faction != u->faction
			&& getguard(u2)&mask
			&& u2->number
			&& !ucontact(u2, u) && !besieged(u2)
			&& alliedunit(u2, u->faction, HELP_GUARD) != HELP_GUARD
#ifdef WACH_WAFF
			&& armedmen(u2)
#endif
			&& cansee(u2->faction, u->region, u, 0)
		) return true;

	return false;
}

unit *
is_guarded(region * r, unit * u, unsigned int mask)
{
	unit *u2;

	for (u2 = r->units; u2; u2 = u2->next)
		if (is_guardian(u2, u, mask)) return u2;
	return NULL;
}

const char *shortdirections[MAXDIRECTIONS] =
{
	"dir_nw",
	"dir_ne",
	"dir_east",
	"dir_se",
	"dir_sw",
	"dir_west"
};

void
cycle_route(unit *u, int gereist)
{
	int cm = 0;
	char tail[1024];
	char neworder[2048];
	char *token;
	direction_t d = NODIRECTION;
	boolean paused = false;
	boolean pause;

	if (igetkeyword(u->thisorder, u->faction->locale) != K_ROUTE) return;
	tail[0] = '\0';

	strcpy(neworder, locale_string(u->faction->locale, keywords[K_ROUTE]));

/*	fprintf(stderr, "Calc route: %s %s\n", unitname(u), regionname(u->region, NULL)); */

	for (cm=0;;++cm) {
		const struct locale * lang = u->faction->locale;
		pause = false;
		token = getstrtoken();
		d = finddirection(token, lang);
		if(d == D_PAUSE) {
			pause = true;
		} else if (d==NODIRECTION) {
			break;
		}
		if (cm<gereist) {
			/* hier sollte keine PAUSE auftreten */
			assert(!pause);
			if (!pause) strcat(strcat(tail, " "), LOC(lang, shortdirections[d]));
		}
		else if (strlen(neworder)>sizeof(neworder)/2) break;
		else if (cm==gereist && !paused && pause) {
			strcat(strcat(tail, " "), LOC(lang, parameters[P_PAUSE]));
			paused=true;
		}
		else if (pause) {
			/* da PAUSE nicht in ein shortdirections[d] umgesetzt wird (ist
			 * hier keine normale direction), muss jede PAUSE einzeln
			 * herausgefiltert und explizit gesetzt werden */
			strcat(strcat(neworder, " "), LOC(lang, parameters[P_PAUSE]));
		} else {
			strcat(strcat(neworder, " "), LOC(lang, shortdirections[d]));
		}
	}

	strcat(neworder, tail);
	set_string(&u->lastorder, neworder);
}

void
init_drive(void)
{
	region *r;
	unit *u, *ut, *u2;
	strlist *S;
	int w;

	for (r=regions; r; r=r->next) {
		/* This is just a simple check for non-corresponding K_TRANSPORT/
		 * K_DRIVE. This is time consuming for an error check, but there
		 * doesn't seem to be an easy way to speed this up. */

		for(u=r->units; u; u=u->next) {
			if(igetkeyword(u->thisorder, u->faction->locale) == K_DRIVE && !fval(u, FL_LONGACTION) && !fval(u, FL_HUNGER)) {
				boolean found = false;
				ut = getunit(r, u->faction);
				if(!ut) {
					cmistake(u, findorder(u, u->thisorder), 63, MSG_MOVE);
					continue;
				}
				for (S = ut->orders; S; S = S->next) {
					if (igetkeyword(S->s, u->faction->locale) == K_TRANSPORT) {
						if (getunit(r, ut->faction) == u) {
							found = true;
							break;
						}
					}
				}
				if(found == false) {
					if(cansee(u->faction, r, ut, 0)) {
						cmistake(u, findorder(u, u->thisorder), 286, MSG_MOVE);
					} else {
						cmistake(u, findorder(u, u->thisorder), 63, MSG_MOVE);
					}
				}
			}
		}

		/* This calculates the weights of all transported units and
		 * adds them to an internal counter which is used by travel() to
		 * calculate effective weight and movement. */

		for (u=r->units; u; u=u->next) {
			w = 0;

			for (S = u->orders; S; S = S->next) if(igetkeyword(S->s, u->faction->locale) == K_TRANSPORT) {
				ut = getunit(r, u->faction);
				if(!ut) continue;

				if (igetkeyword(ut->thisorder, u->faction->locale) == K_DRIVE && !fval(ut, FL_LONGACTION) && !fval(ut, FL_HUNGER)) {
					u2 = getunit(r, u->faction);
					if(u2 == u) {
						w += weight(ut);
						break;
					}
				}
			}

			if(w > 0) a_add(&u->attribs, a_new(&at_driveweight))->data.i = w;
		}
	}
}

int  *storms;

#if 0
/* in arbeit */
#define ETRAVEL_NOTALLOWED 1
#define ETRAVEL_NOSWIM     2
#define ETRAVEL_NORIDE     4
#define ETRAVEL_LEFTSHIP   8

int
cantravel(const unit *u, const region *from, const region * to, boolean (*allowed)(const struct region *, const struct region *))
{
	/* basic allowed function */
	if (allowed && !allowed(from, to)) return ETRAVEL_NOTALLOWED;
	if (u->ship && fval(u, FL_OWNER) && rterrain(from)==T_OCEAN) {
		/* wir sind auf einem schiff und auf dem ozean */
		if (rterrain(to)==T_OCEAN || (old_race(u->race) != RC_AQUARIAN && !dragon(u))) {
			/* Drache oder Meermensch sind wir auch nicht */
			return ETRAVEL_NOSWIM;
		} else if (get_item(u, I_HORSE) > 0) {
			return ETRAVEL_NORIDE|ETRAVEL_NOSWIM;
		}
	}
	if (!u->ship && leftship(u) && is_guarded(from, u, GUARD_LANDING)) {
		/* An Land kein NACH wenn in dieser Runde Schiff VERLASSEN! */
		return ETRAVEL_LEFTSHIP;
	}
}
#endif


static boolean
roadto(const region * r, const region * r2)
{
	/* wenn es hier genug strassen gibt, und verbunden ist, und es dort
	 * genug strassen gibt, dann existiert eine strasse in diese richtung */

	direction_t i;

	if (!r || !r2) return false;

	for (i = 0; i != MAXDIRECTIONS; i++)
		if (rconnect(r, i) == r2) {
			if (terrain[rterrain(r)].roadreq==0) return false;
			if (terrain[rterrain(r2)].roadreq==0) return false;
			if (rroad(r, i) < terrain[rterrain(r)].roadreq) return false;
			if (rroad(r2, back[i]) < terrain[rterrain(r2)].roadreq) return false;
			return true;
		}
	return false;
}

direction_t *
travel(region * first, unit * u, region * next, int flucht)
{
	region *current = first;
	int k, m, i, dh;
	double dk;
	unit *wache;
	region *rv[MAXSPEED];
	strlist *S;
	unit *ut, *u2;
	int gereist = 0;
	static direction_t route[MAXSPEED];
	static boolean init = false;
	static const curse_type * speed_ct;
	static const curse_type * fogtrap_ct;
	if (!init) { 
		init = true; 
		speed_ct = ct_find("speed"); 
		fogtrap_ct = ct_find("fogtrap"); 
	}

	/* tech:
	 *
	 * zu Fu� reist man 1 Region, zu Pferd 2 Regionen. Mit Stra�en reist
	 * man zu Fu� 2, mit Pferden 3 weit.
	 *
	 * Berechnet wird das mit BPs. Zu Fu� hat man 4 BPs, zu Pferd 6.
	 * Normalerweise verliert man 3 BP pro Region, bei Stra�en nur 2 BP.
	 * Au�erdem: Wenn Einheit transportiert, nur halbe BP */

	if (rterrain(current)==T_OCEAN
			&& !(u->race->flags & RCF_FLY) && rterrain(next) != T_OCEAN)
	{ /* Die Einheit kann nicht fliegen, ist im Ozean, und will an Land */
		if (!(u->race->flags & RCF_SWIM) && old_race(u->race) != RC_AQUARIAN) {
			cmistake(u, findorder(u, u->thisorder), 44, MSG_MOVE);
			return NULL;
		} else {
			if (u->ship && get_item(u, I_HORSE) > 0) {
				cmistake(u, findorder(u, u->thisorder), 67, MSG_MOVE);
				return NULL;
			}
		}
	} else if (rterrain(current)!=T_OCEAN) {
		/* An Land kein NACH wenn in dieser Runde Schiff VERLASSEN! */
		if (leftship(u) && is_guarded(current, u, GUARD_LANDING)) {
			cmistake(u, findorder(u, u->thisorder), 70, MSG_MOVE);
			return NULL;
		}
		if (u->ship && !flucht && u->race->flags & RCF_SWIM) {
			cmistake(u, findorder(u, u->thisorder), 143, MSG_MOVE);
			return NULL;
		}
	} else if (rterrain(next) == T_OCEAN && u->ship && fval(u->ship, FL_MOVED)) {
		cmistake(u, findorder(u, u->thisorder), 13, MSG_MOVE);
		return NULL;
	}

	switch (canwalk(u)) {
	case 1:
		cmistake(u, findorder(u, u->thisorder), 57, MSG_MOVE);
		return NULL;
	case 2:
		cmistake(u, findorder(u, u->thisorder), 56, MSG_MOVE);
		return NULL;
	case 3:
		cmistake(u, findorder(u, u->thisorder), 42, MSG_MOVE);
		return NULL;
	}

	/* wir suchen so lange nach neuen Richtungen, wie es geht. Diese werden
	 * dann nacheinander ausgef�hrt. */

	/* im array rv[] speichern wir die Regionen ab, durch die wir wandern. */

	dk = u->race->speed;

	if (speed_ct) {
		curse *c = get_curse(u->attribs, speed_ct);
		if(c) {
			int men = get_cursedmen(u, c);
			dk *= 1.0 + (double)men/(double)u->number;
		}
	}

	switch(canride(u)) {
	case 1:		/* Pferd */
		k = (int)(dk*BP_RIDING);
		break;
	case 2:		/* Einhorn */
		k = (int)(dk*BP_UNICORN);
		break;
	default:
		{
			int mp = 1;

			/* faction special */
			if(fspecial(u->faction, FS_QUICK))
				mp = BP_RIDING;

			/* Siebenmeilentee */
			if (get_effect(u, oldpotiontype[P_FAST]) >= u->number) {
				mp *= 2;
				change_effect(u, oldpotiontype[P_FAST], -u->number);
			}

			/* unicorn in inventory */
			if (u->number <= get_item(u, I_FEENSTIEFEL))
				mp *= 2;

			/* Im Astralraum sind Tyb und Ill-Magier doppelt so schnell.
			 * Nicht kumulativ mit anderen Beschleunigungen! */
			if ( mp == 1 && getplane(next) == astral_plane && is_mage(u)) {
				if(get_mage(u)->magietyp == M_ASTRAL
						|| get_mage(u)->magietyp == M_TRAUM) {
					mp *= 2;
				}
			}
			k = (int)(dk*mp*BP_WALKING);
		}
		break;
	}

	switch(old_race(u->race)) {
	case RC_DRAGON:
	case RC_WYRM:
	case RC_FIREDRAGON:
	case RC_BIRTHDAYDRAGON:
	case RC_PSEUDODRAGON:
		k = BP_DRAGON;
	}

	m = 0;

	/* die n�chste Region, in die man wandert, wird durch movewhere() aus
	 * der letzten Region bestimmt.
	 *
	 * Anfangen tun wir bei r. r2 ist beim ersten Durchlauf schon gesetzt
	 * (Parameter!), das Ziel des Schrittes. m z�hlt die Schritte, k sind
	 * die noch m�glichen Schritte, r3 die letzte g�ltige, befahrene Region. */

	while (next) {
		direction_t dir = reldirection(current, next);
		border * b = get_borders(current, next);

		while (b) {
			if (b->type==&bt_wisps) {
				wall_data * wd = (wall_data*)b->data;
				if (wd->active) {
					region * rl = rconnect(current, (direction_t)((dir+MAXDIRECTIONS-1)%MAXDIRECTIONS));
					region * rr = rconnect(current, (direction_t)((dir+1)%MAXDIRECTIONS));
					int j = rand() % 3;
					if (j==0) break;
					else if (j==1 && rl && landregion(rterrain(rl))==landregion(rterrain(next))) next = rl;
					else if (j==2 && rr && landregion(rterrain(rr))==landregion(rterrain(next))) next = rr;
					break;
				}
			}
			if (b->type->move) b->type->move(b, u, current, next);
			b = b->next;
		}
		if (current!=next) { /* !pause */
			if (roadto(current, next))
				k -= BP_ROAD;
			else
				k -= BP_NORMAL;
			if (k<0) break;
			/* r2 -> Zielregion, r3 -> Momentane Region */
			if (move_blocked(u, current, reldirection(current, next))
					|| curse_active(get_curse(current->attribs, fogtrap_ct)))
			{
				ADDMSG(&u->faction->msgs, msg_message("leavefail", 
					"unit region", u, next));
			}
      if (!entrance_allowed(u, next)) {
				ADDMSG(&u->faction->msgs, msg_message("regionowned", 
					"unit region target", u, current, next));
				break;
      }
			if ((wache = bewegung_blockiert_von(u, current)) != (unit *) NULL
				&& gereist != 0)
			{
				ADDMSG(&u->faction->msgs, msg_message("moveblockedbyguard", 
					"unit region guard", u, current,
					wache));
				break;
			}
			
			if (fval(u->race, RCF_ILLUSIONARY)) {
				curse * c = get_curse(next->attribs, ct_find("antimagiczone"));
				if (curse_active(c)) {
					curse_changevigour(&next->attribs, c, - u->number);
					add_message(&u->faction->msgs, new_message(u->faction,
						"illusionantimagic%u:unit", u));
					destroy_unit(u);
					return route;
				}
			}
			/* Ozeanfelder k�nnen nur von Einheiten mit Schwimmen und ohne
			 * Pferde betreten werden. Drachen k�nnen fliegen. */

			if (rterrain(next) == T_OCEAN && !canswim(u)) {
				plane *pl = getplane(next);
				if(pl && fval(pl, PFL_NOCOORDS)) {
					add_message(&u->faction->msgs, new_message(u->faction,
						"detectoceandir%u:unit%i:direction", u,
						reldirection(current, next)));
				} else {
					add_message(&u->faction->msgs, new_message(u->faction,
						"detectocean%u:unit%r:region", u, next));
				}
				break;
			}

			if (terrain[rterrain(next)].flags & FORBIDDEN_LAND) {
				plane *pl = getplane(next);
				if(pl && fval(pl, PFL_NOCOORDS)) {
					add_message(&u->faction->msgs, new_message(u->faction,
						"detectforbiddendir%u:unit%i:direction", u, reldirection(current, next)));
				} else {
					add_message(&u->faction->msgs, new_message(u->faction,
						"detectforbidden%u:unit%r:region", u, next));
				}
				break;
			}

			if (old_race(u->race) == RC_INSECT && r_insectstalled(next) &&
					!is_cursed(u->attribs, C_KAELTESCHUTZ,0)) {
				add_message(&u->faction->msgs, new_message(u->faction,
					"detectforbidden%u:unit%r:region", u, next));
				break;
			}
			rv[m] = next;
			route[m] = dir;
			m++;
		}
		else break;

		current = next;
		if(!flucht) {
			++gereist;
			if (current==first) break; /* PAUSE beendet die Reise */
			next = movewhere(current, u);
		}
	}
	route[m] = NODIRECTION;

	if (gereist) {
		int mode;
		setguard(u, GUARD_NONE);
		cycle_route(u, gereist);

		buf[0] = 0;

		if (flucht == 1)
			mode = 0;
		else if (canride(u)) {
			mode = 1;
			produceexp(u, SK_RIDING, u->number);
		} else
			mode = 2;

		/* Haben Drachen ihr Ziel erreicht? */
		if (u->faction->no==MONSTER_FACTION) {
			attrib * ta = a_find(u->attribs, &at_targetregion);
			if (ta && current == (region*)ta->data.v) {
				a_remove(&u->attribs, ta);
				set_string(&u->lastorder, "WARTEN");
			}
		}

		/* �ber die letzte Region braucht man keinen Bericht */

		m--;
		if (m > 0) {
			dh = 0;
			travelthru(u, first);
			for (i = 0; i != m; i++) {
				char * p;
				if (dh) {
					if (i == m - 1)
						scat(" und ");
					else
						scat(", ");
				}
				dh = 1;
				travelthru(u, rv[i]);
				p = buf+strlen(buf);
				MSG(("travelthru_trail", "region", rv[i]), p, sizeof(buf)-strlen(buf), u->faction->locale, u->faction);
			}
		}
		add_message(&u->faction->msgs, new_message(
			u->faction, "travel%u:unit%i:mode%r:start%r:end%s:regions",
			u, mode, first, current, strdup(buf)));

		/* und jetzt noch die transportierten Einheiten verschieben */

		for (S = u->orders; S; S = S->next) {
			if (igetkeyword(S->s, u->faction->locale) == K_TRANSPORT) {
				ut = getunit(first, u->faction);
				if (ut) {
					boolean found = false;
					if (igetkeyword(ut->thisorder, u->faction->locale) == K_DRIVE
							&& !fval(ut, FL_LONGACTION) && !fval(ut, FL_HUNGER)) {
						u2 = getunit(first, ut->faction);
						if(u2 == u) {
							found = true;
							add_message(&u->faction->msgs, new_message(
								u->faction, "transport%u:unit%u:target%r:start%r:end",
								u, ut, first, current));
							if(!(terrain[current->terrain].flags & WALK_INTO)
									&& get_item(ut, I_HORSE)) {
								cmistake(u, u->thisorder, 67, MSG_MOVE);
								cmistake(ut, ut->thisorder, 67, MSG_MOVE);
								continue;
							}
							if(can_survive(ut, current)) {
								for (i = 0; i != m; i++)
									travelthru(ut, rv[i]);
								move_unit(ut, current, NULL);
							} else {
								cmistake(u, u->thisorder, 287, MSG_MOVE);
								cmistake(ut, ut->thisorder, 230, MSG_MOVE);
								continue;
							}
						}
					}
					if (!found) {
						if(cansee(u->faction, u->region, ut, 0)) {
							cmistake(u, findorder(u, u->thisorder), 90, MSG_MOVE);
						} else {
							cmistake(u, findorder(u, u->thisorder), 63, MSG_MOVE);
						}
					}
				} else {
					if (ut) {
						cmistake(u, findorder(u, u->thisorder), 63, MSG_MOVE);
					} else {
						cmistake(u, findorder(u, u->thisorder), 99, MSG_MOVE);
					}
				}
			}
		}
		move_unit(u, current, NULL);
	}
	else if (flucht)
		move_unit(u, current, NULL);
	set_string(&u->thisorder, "");
	fset(u, FL_LONGACTION);
	setguard(u, GUARD_NONE);

	if (fval(u, FL_FOLLOWING)) caught_target(current, u);
	return route;
}

boolean
ship_ready(region * r, unit * u)
{
	if (!fval(u, FL_OWNER)) {
		cmistake(u, findorder(u, u->thisorder), 146, MSG_MOVE);
		return false;
	}
	if (eff_skill(u, SK_SAILING, r) < u->ship->type->cptskill) {
		sprintf(buf, "Der Kapit�n mu� mindestens Segeln %d haben, "
				"um %s zu befehligen ", u->ship->type->cptskill,
				u->ship->type->name[1]);
		mistake(u, u->thisorder, buf, MSG_MOVE);
		return false;
	}
	assert(u->ship->type->construction->improvement==NULL); /* sonst ist construction::size nicht ship_type::maxsize */
	if (u->ship->size!=u->ship->type->construction->maxsize) {
		cmistake(u, findorder(u, u->thisorder), 15, MSG_MOVE);
		return false;
	}
	if (!enoughsailors(r, u->ship)) {
		cmistake(u, findorder(u, u->thisorder), 1, MSG_MOVE);
/*		mistake(u, u->thisorder,
				"Auf dem Schiff befinden sich zuwenig erfahrene Seeleute.", MSG_MOVE); */
		return false;
	}
	if (!cansail(r, u->ship)) {
		if( is_cursed(u->ship->attribs, C_SHIP_FLYING, 0) )
			cmistake(u, findorder(u, u->thisorder), 17, MSG_MOVE);
		else
			cmistake(u, findorder(u, u->thisorder), 18, MSG_MOVE);
		return false;
	}
	return true;
}

unit *
owner_buildingtyp(const region * r, const building_type * bt)
{
	building *b;
	unit *owner;

	for (b = rbuildings(r); b; b = b->next) {
		owner = buildingowner(r, b);
		if (b->type == bt && owner != NULL) {
			if (b->size >= bt->maxsize) {
				return owner;
			}
		}
	}

	return NULL;
}

boolean
buildingtype_exists(const region * r, const building_type * bt)
{
	building *b;

	for (b = rbuildings(r); b; b = b->next) {
		if (b->type == bt) {
			if (b->size >= bt->maxsize) {
				return true;
			}
		}
	}

	return false;
}

boolean
check_working_buildingtype(const region * r, const building_type * bt)
{
	building *b;

	for (b = rbuildings(r); b; b = b->next) {
		if (b->type == bt) {
			if (b->size >= bt->maxsize && fval(b, BLD_WORKING)) {
				return true;
			}
		}
	}

	return false;
}

/* Pr�ft, ob Ablegen von einer K�ste in eine der erlaubten Richtungen erfolgt. */

boolean
check_takeoff(ship *sh, region *from, region *to)
{
	direction_t dir;
	direction_t coast, coastl, coastr;

	if (rterrain(from)!=T_OCEAN && sh->coast != NODIRECTION) {
		coast = sh->coast;
		dir   = reldirection(from, to);

		coastr  = (direction_t)((coast+1) % MAXDIRECTIONS);
		coastl  = (direction_t)((coast+MAXDIRECTIONS-1) % MAXDIRECTIONS);

		if(dir != coast && dir != coastl && dir != coastr
			&& check_working_buildingtype(from, bt_find("harbour")) == false)
		{
			return false;
		}
	}

	return true;
}

boolean
ship_allowed(const struct ship_type * type, region * r)
{
	int c = 0;
	if (check_working_buildingtype(r, bt_find("harbour"))) return true;
	for (c=0;type->coast[c]!=NOTERRAIN;++c) {
		if (type->coast[c]==rterrain(r)) return true;
	}
	return false;
}

direction_t *
sail(region * starting_point, unit * u, region * next_point, boolean move_on_land)
{
	region *current_point, *last_point;
	unit *u2, *hafenmeister;
	item * trans = NULL;
	int st, first = 1;
	int k, m, l, i, step = 0;
	int stormchance;
	region *rv[MAXSPEED + 1];
	region *tt[MAXSPEED + 1]; /* travelthru */
	static direction_t route[MAXSPEED+1]; /* route[i] := direction from tt[i] to tt[i-1] */
	static boolean init = false;
	static const curse_type * fogtrap_ct;
	if (!init) { 
		init = true; 
		fogtrap_ct = ct_find("fogtrap"); 
	}

	if (!ship_ready(starting_point, u))
		return NULL;

	/* Wir suchen so lange nach neuen Richtungen, wie es geht. Diese werden
	 * dann nacheinander ausgef�hrt. */

	k = shipspeed(u->ship, u);

	l = 0;
	m = 0;

	last_point = starting_point;
	current_point = starting_point;

	/* die n�chste Region, in die man segelt, wird durch movewhere() aus der
	 * letzten Region bestimmt.
	 *
	 * Anfangen tun wir bei starting_point. next_point ist beim ersten
	 * Durchlauf schon gesetzt (Parameter!). l z�hlt gesichtete
	 * K�stenstreifen, rv[] speichert die gesichteten K�stenstreifen, m
	 * z�hlt befahrene Felder, current_point ist die letzte g�ltige,
	 * befahrene Region. */
	tt[step] = starting_point;

	while (current_point!=next_point && m < k && next_point)
	{
		direction_t dir = reldirection(current_point, next_point);

		if(terrain[rterrain(next_point)].flags & FORBIDDEN_LAND) {
			plane *pl = getplane(next_point);
			if(pl && fval(pl, PFL_NOCOORDS)) {
				add_message(&u->faction->msgs, new_message(u->faction,
					"sailforbiddendir%h:ship%i:direction",
					u->ship, reldirection(current_point,next_point)));
			} else {
				add_message(&u->faction->msgs, new_message(u->faction,
					"sailforbidden%h:ship%r:region", u->ship, next_point));
			}
			break;
		}

		if( !is_cursed(u->ship->attribs, C_SHIP_FLYING, 0)
				&& !(u->ship->type->flags & SFL_FLY)) {
			if (rterrain(current_point) != T_OCEAN
			    && rterrain(next_point) != T_OCEAN) {
				plane *pl = getplane(next_point);
				if(pl && fval(pl, PFL_NOCOORDS)) {
					sprintf(buf, "Die %s entdeckt, da� im %s Festland ist.",
						shipname(u->ship), locale_string(u->faction->locale, directions[dir]));
				} else {
					sprintf(buf, "Die %s entdeckt, da� (%d,%d) Festland ist.",
						 	shipname(u->ship), region_x(next_point,u->faction),
						 	region_y(next_point,u->faction));
				}
				addmessage(0, u->faction, buf, MSG_MOVE, ML_WARN);
				break;
			}
			if (!(u->ship->type->flags & SFL_OPENSEA) && rterrain(next_point) == T_OCEAN) {
				direction_t d;
				for (d=0;d!=MAXDIRECTIONS;++d) {
					region * rc = rconnect(next_point, d);
					if (rterrain(rc) != T_OCEAN) break;
				}
				if (d==MAXDIRECTIONS) {
					/* Schiff kann nicht aufs offene Meer */
					cmistake(u, findorder(u, u->thisorder), 249, MSG_MOVE);
					break;
				}
			}
			if(rterrain(current_point) != T_OCEAN
					&& rterrain(next_point) == T_OCEAN) {
				if(check_takeoff(u->ship, current_point, next_point) == false) {
					/* Schiff kann nicht ablegen */
					cmistake(u, findorder(u, u->thisorder), 182, MSG_MOVE);
					break;
				}
			}

			if(move_on_land == false && rterrain(next_point) != T_OCEAN) {
				break;
			}

			/* Falls Blockade, endet die Seglerei hier */

			if (move_blocked(u, current_point, reldirection(current_point, next_point))
					|| curse_active(get_curse(current_point->attribs, fogtrap_ct))) {
					add_message(&u->faction->msgs, new_message(u->faction,
						"sailfail%h:ship%r:region", u->ship, current_point));
				break;
			}
			if (!ship_allowed(u->ship->type, next_point)) {
				if(fval(u, FL_STORM)) {
					if(!check_leuchtturm(current_point, NULL)) {
						add_message(&u->faction->msgs, new_message(u->faction,
							"sailnolandingstorm%h:ship", u->ship));
						damage_ship(u->ship, 0.10);
						if (u->ship->damage>=u->ship->size * DAMAGE_SCALE) {
							add_message(&u->faction->msgs, new_message(u->faction,
								"shipsink%h:ship", u->ship));
						}
					}
				} else {
					add_message(&u->faction->msgs, new_message(u->faction,
						"sailnolanding%h:ship%r:region", u->ship, next_point));
					damage_ship(u->ship, 0.10);
					if (u->ship->damage>=u->ship->size * DAMAGE_SCALE) {
						add_message(&u->faction->msgs, new_message(u->faction,
							"shipsink%h:ship", u->ship));
					}
				}
				break;
			}
			if(terrain[rterrain(next_point)].flags & FORBIDDEN_LAND) {
				plane *pl = getplane(next_point);
				if(pl && fval(pl, PFL_NOCOORDS)) {
					add_message(&u->faction->msgs, new_message(u->faction,
						"sailforbiddendir%h:ship%i:direction",
						u->ship, reldirection(current_point,next_point)));
/*					sprintf(buf, "Die Mannschaft der %s weigert sich, in die Feuerwand "
						"im %s zu fahren.",
						shipname(u->ship), directions[dir]); */
				} else {
					add_message(&u->faction->msgs, new_message(u->faction,
						"sailforbidden%h:ship%r:region", u->ship, next_point));
				}
				break;
			}

			if(is_cursed(next_point->attribs, C_MAELSTROM, 0)) {
				do_maelstrom(next_point, u);
			}

			stormchance = storms[month(0)] * 5 / shipspeed(u->ship, u);
			if(check_leuchtturm(next_point, NULL)) stormchance /= 3;
#ifdef SAFE_COASTS
			/* Sturm nur, wenn n�chste Region Hochsee ist. */
			if(rand()%10000 < stormchance) {
				direction_t d;
				for (d=0;d!=MAXDIRECTIONS;++d) {
					region * r = rconnect(next_point, d);
					if (rterrain(r)!=T_OCEAN) break;
				}
				if (d==MAXDIRECTIONS)
					ship_in_storm(u, next_point);
			}
#else
			/* Sturm nur, wenn n�chste Region keine Landregion ist. */
			if(rand()%10000 < stormchance && next_point->terrain == T_OCEAN) {
				ship_in_storm(u, next_point);
			}
#endif
		} /* endif !flying */

		/* Falls kein Problem, eines weiter ziehen */
		route[step] = dir;
		tt[step] = current_point;       /* travelthrough */
		fset(u->ship, FL_MOVED);
		step++;
		m++;

		last_point = current_point;
		current_point = next_point;

		/* Falls eine K�stenregion, dann in rv[] aufnehmen (die letzte
		 * K�ste wird nachher nicht aufgelistet werden, wenn das
		 * Schiff dort seine Runde beendent!). */
		if (rterrain(current_point) != T_OCEAN) {
			rv[l] = current_point;
			l++;
		}
		if (rterrain(current_point) != T_OCEAN && !is_cursed(u->ship->attribs, C_SHIP_FLYING, 0)) break;
		next_point = movewhere(current_point, u);
	}

	route[step] = NODIRECTION;
	/* at this point, step is the number of steps made. tt[step] is the final region. */

	/* Nun enth�lt current_point die Region, in der das Schiff seine Runde
	 * beendet hat. Wir generieren hier ein Ereignis f�r den Spieler, das
	 * ihm sagt, bis wohin er gesegelt ist, falls er �berhaupt vom Fleck
	 * gekommen ist. Das ist nicht der Fall, wenn er von der K�ste ins
	 * Inland zu segeln versuchte */

	if (fval(u->ship, FL_MOVED)) {
		ship * sh = u->ship;
		/* nachdem alle Richtungen abgearbeitet wurden, und alle Einheiten
		 * transferiert wurden, kann der aktuelle Befehl gel�scht werden. */
		cycle_route(u, step);
		set_string(&u->thisorder, "");
		if (current_point->terrain != T_OCEAN && !is_cursed(sh->attribs, C_SHIP_FLYING, 0)) {
			sh->coast = reldirection(current_point, last_point);
		} else {
			sh->coast = NODIRECTION;
		}

		sprintf(buf, "Die %s ", shipname(sh));
		if( is_cursed(sh->attribs, C_SHIP_FLYING, 0) )
			scat("fliegt");
		else
			scat("segelt");
		scat(" von ");
		scat(regionid(starting_point));
		scat(" nach ");
		scat(regionid(current_point));

		/* Falls es Kuesten gibt, denen man entlang gefahren ist,
		 * sollen sie aufgefuehrt werden. Falls wir aber unseren Zug in
		 * einer Kuestenregion beendet haben, fuehren wir sie nicht mit
		 * auf - sie wird sowieso in allen Details im Report erwaehnt
		 * werden. */

		if (rterrain(current_point) != T_OCEAN)
			l--;

		if (l > 0) {
			if( is_cursed(sh->attribs, C_SHIP_FLYING, 0) )
				scat(". Dabei flog es �ber ");
			else {
				scat(". Dabei segelte es entlang der ");
				if (l > 1)
					scat("K�sten");
				else
					scat("K�ste");
				scat(" von ");
			}

			for (i = 0; i != l; i++) {
				scat(regionid(rv[i]));
				if (i < l - 2)
					scat(", ");
				else if (i == l - 2)
					scat(" und ");
			}
		}
		scat(".");

		addmessage(0, u->faction, buf, MSG_MOVE, ML_INFO);

		/* Das Schiff und alle Einheiten darin werden nun von
		 * starting_point nach current_point verschoben */

		/* Verfolgungen melden */
		if (fval(u, FL_FOLLOWING)) caught_target(current_point, u);

		tt[step] = NULL;
		sh = move_ship(sh, starting_point, current_point, tt);

		/* Hafengeb�hren ? */

		hafenmeister = owner_buildingtyp(current_point, bt_find("harbour"));
		if (sh && hafenmeister != NULL) {
			item * itm;
			assert(trans==NULL);
			for (u2 = current_point->units; u2; u2 = u2->next) {
				if (u2->ship == u->ship &&
						!alliedunit(hafenmeister, u->faction, HELP_GUARD)) {


					if (effskill(hafenmeister, SK_OBSERVATION) > effskill(u2, SK_STEALTH)) {
						for (itm=u2->items; itm; itm=itm->next) {
							const luxury_type * ltype = resource2luxury(itm->type->rtype);
							if (ltype!=NULL && itm->number>0) {
								st = itm->number * effskill(hafenmeister, SK_TRADE) / 50;
								st = min(itm->number, st);

								if (st > 0) {
									i_change(&u2->items, itm->type, -st);
									i_change(&hafenmeister->items, itm->type, st);
									i_add(&trans, i_new(itm->type, st));
								}
							}
						}
					}
				}
			}
			if (trans) {
				sprintf(buf, "%s erhielt ", hafenmeister->name);
				for (itm = trans; itm; itm=itm->next) {
					if (first != 1) {
						if (itm->next!=NULL && itm->next->next==NULL) {
							scat(" und ");
						} else {
							scat(", ");
						}
					}
					first = 0;
					icat(trans->number);
					scat(" ");
					if (itm->number == 1) {
						scat(locale_string(default_locale, resourcename(itm->type->rtype, 0)));
					} else {
						scat(locale_string(default_locale, resourcename(itm->type->rtype, NMF_PLURAL)));
					}
				}
				scat(" von der ");
				scat(shipname(u->ship));
				scat(".");
				addmessage(0, u->faction, buf, MSG_COMMERCE, ML_INFO);
				addmessage(0, hafenmeister->faction, buf, MSG_INCOME, ML_INFO);
				while (trans) i_remove(&trans, trans);
			}
		}
	}
	return route;
}

unit *
kapitaen(region * r, ship * sh)
{
	unit *u;

	for (u = r->units; u; u = u->next) {
		if (u->ship == sh && eff_skill(u, SK_SAILING, r) >= sh->type->cptskill)
			return u;
	}

	return NULL;
}

/* Segeln, Wandern, Reiten */

static void
move(region * r, unit * u, boolean move_on_land)
{
	region *r2;
	direction_t * route;

	r2 = movewhere(r, u);

	if (!r2) {
		cmistake(u, findorder(u, u->thisorder), 71, MSG_MOVE);
		return;
	}
	else if (u->ship && fval(u, FL_OWNER))
		route = sail(r, u, r2, move_on_land);
	else
		route = travel(r, u, r2, 0);

	if (i_get(u->items, &it_demonseye)) {
		direction_t d;
		for (d=0;d!=MAXDIRECTIONS;++d) {
			region * rc = rconnect(r2,d);
			if (rc) {
				sprintf(buf, "Im %s ist eine ungeheure magische Pr�senz zu versp�ren.",
						locale_string(u->faction->locale, directions[back[d]]));
				addmessage(rc, NULL, buf, MSG_EVENT, ML_IMPORTANT);
			}
		}
	}
	set_string(&u->thisorder, "");
	/* F�r den Silberpool: Bewegung ist fertig, Silber darf abgezogen werden */
	if (fval(u, FL_FOLLOWED) && route && route[0]!=NODIRECTION) {
		unit *up=r->units;

		while (up) {
			unit *un = up->next;

			if (fval(up, FL_FOLLOWING) && !fval(up, FL_MOVED)) {
				attrib * a = a_find(up->attribs, &at_follow);
				if (a && a->data.v==u) {
					/* wir basteln ihm ein NACH */
					int k, i = 0;
					strcpy(buf, locale_string(up->faction->locale, keywords[K_MOVE]));
					while (route[i]!=NODIRECTION)
						strcat(strcat(buf, " "), locale_string(up->faction->locale, directions[route[i++]]));
					set_string(&up->thisorder, buf);
					k = igetkeyword(up->thisorder, up->faction->locale);
					assert(k==K_MOVE);
					move(r, up, true);
				}
			}
			up = un;
		}
	}
	if (u->region!=r) fset(u, FL_LONGACTION);
}

attrib_type at_piracy_direction = {
	"piracy_direction",
	DEFAULT_INIT,
	DEFAULT_FINALIZE,
	DEFAULT_AGE,
	NO_WRITE,
	NO_READ
};

void
piracy(unit *u)
{
	region *r = u->region;
	ship *sh = u->ship, *sh2;
	direction_t dir, target_dir = NODIRECTION;
	int         aff[MAXDIRECTIONS];
	int         saff = 0;
	int					*il;
	char        *s;
	boolean     all = true;
	attrib      *a;

	if(!sh) {
		cmistake(u, findorder(u, u->thisorder), 144, MSG_MOVE);
		return;
	}

	if(!fval(u, FL_OWNER)) {
		cmistake(u, findorder(u, u->thisorder), 46, MSG_MOVE);
		return;
	}

	/* Feststellen, ob schon ein anderer alliierter Pirat ein
	 * Ziel gefunden hat. */

	il = intlist_init();

	s = getstrtoken();
	while(s && *s) {
		il = intlist_add(il, atoi(s));
		all = false;
		s = getstrtoken();
	}

	for(a =  a_find(r->attribs, &at_piracy_direction); a; a=a->nexttype) {
		faction *f = findfaction(a->data.sa[0]);

		if (alliedunit(u, f, HELP_FIGHT)
				&& intlist_find(il, a->data.sa[1])) {
			target_dir = (direction_t)a->data.sa[1];
			break;
		}
	}

	/* Wenn nicht, sehen wir, ob wir ein Ziel finden. */

	if (target_dir == NODIRECTION) {

		/* Einheit ist also Kapit�n. Jetzt gucken, in wievielen
		 * Nachbarregionen potentielle Opfer sind. */

		for(dir = 0; dir < MAXDIRECTIONS; dir++) {
			region *rc = rconnect(r, dir);
			aff[dir] = 0;
			if(rc && (terrain[rterrain(rc)].flags & SWIM_INTO)
					&& check_takeoff(sh, r, rc) == true) {

				for(sh2 = rc->ships; sh2; sh2 = sh2->next) {
					unit * cap = shipowner(rc, sh2);
					if (cap && (intlist_find(il, cap->faction->no) || all)) {
						aff[dir]++;
					}
				}

				/* Und aufaddieren. */
				saff += aff[dir];
			}
		}

		if(saff != 0) {
			saff = rand()%saff;
			for(dir = 0; dir < MAXDIRECTIONS; dir++) {
				if(saff < aff[dir]) break;
				saff -= aff[dir];
			}
			target_dir = dir;
			a = a_add(&r->attribs, a_new(&at_piracy_direction));
			a->data.sa[0] = (short)u->faction->no;
			a->data.sa[1] = (short)target_dir;
		}
	}

	free(il);

	/* Wenn kein Ziel gefunden, entsprechende Meldung generieren */
	if(target_dir == NODIRECTION) {
		add_message(&u->faction->msgs,
			new_message(u->faction, "piratenovictim%h:ship%r:region", sh, r));
		return;
	}

	/* Meldung generieren */
	add_message(&u->faction->msgs, new_message(u->faction,
		"piratesawvictim%h:ship%r:region%d:dir", sh, r, target_dir));

	/* Befehl konstruieren */
	sprintf(buf, "%s %s", locale_string(u->faction->locale, keywords[K_MOVE]),
		locale_string(u->faction->locale, directions[target_dir]));
	set_string(&u->thisorder, buf);

	/* Bewegung ausf�hren */
	igetkeyword(u->thisorder, u->faction->locale);	/* NACH ignorieren */
	move(r, u, true);
}

static void
age_traveldir(region *r)
{
	attrib *a = a_find(r->attribs, &at_traveldir);

	while(a) {
		a->data.ca[3]--;
		if(a->data.ca[3] <= 0) {
			attrib *an = a->nexttype;
			a_remove(&r->attribs, a);
			a = an;
		} else {
			a = a->nexttype;
		}
	}
}

static direction_t
hunted_dir(attrib *at, int id)
{
	attrib *a = a_find(at, &at_traveldir_new);

	while(a) {
		traveldir *t = (traveldir *)(a->data.v);
		if(t->no == id) return t->dir;
		a = a->nexttype;
	}

	return NODIRECTION;
}

boolean
hunt(unit *u)
{
	region *rc = u->region;
	int moves, id;
	char command[256];
	direction_t dir;

	if(!u->ship) {
		cmistake(u, findorder(u, u->thisorder), 144, MSG_MOVE);
		return false;
	} else if(!fval(u, FL_OWNER)) {
		cmistake(u, findorder(u, u->thisorder), 146, MSG_MOVE);
		return false;
	} else if(attacked(u)) {
		cmistake(u, findorder(u, u->thisorder), 52, MSG_MOVE);
		return false;
	} else if(u->race->flags & RCF_CANNOTMOVE) {
		cmistake(u, findorder(u, u->thisorder), 55, MSG_MOVE);
		return false;
	}

	id = getshipid();

	if(id <= 0) {
		cmistake(u,  findorder(u, u->thisorder), 20, MSG_MOVE);
		return false;
	}

	dir = hunted_dir(rc->attribs, id);

	if(dir == NODIRECTION) {
		cmistake(u,  findorder(u, u->thisorder), 20, MSG_MOVE);
		return false;
	}

	sprintf(command, "%s %s", locale_string(u->faction->locale, keywords[K_MOVE]),
		locale_string(u->faction->locale, directions[dir]));
	moves = 1;

	rc = rconnect(rc, dir);
	while(moves < shipspeed(u->ship, u)
			&& (dir = hunted_dir(rc->attribs, id)) != NODIRECTION) {
		strcat(command, " ");
		strcat(command, locale_string(u->faction->locale, directions[dir]));
		moves++;
		rc = rconnect(rc, dir);
	}

	/* In command steht jetzt das NACH-Kommando. */

	igetkeyword(command, u->faction->locale);	/* NACH ignorieren und Parsing initialisieren. */
	move(u->region, u, false);								/* NACH ausf�hren */
	fset(u, FL_LONGACTION);										/* Von Hand setzen, um Endlosschleife zu vermeiden, 
																							wenn Verfolgung nicht erfolgreich */
	return true;															/* true -> Einheitenliste von vorne durchgehen */
}

void
destroy_damaged_ships(void)
{
	region *r;
	ship   *sh,*shn;

	for(r=regions;r;r=r->next) {
		for(sh=r->ships;sh;) {
			shn = sh->next;
			if (sh->damage>=sh->size * DAMAGE_SCALE) {
				destroy_ship(sh, r);
			}
			sh = shn;
		}
	}
}

boolean
is_disorientated(unit *u)
{
	static boolean init = false;
	static const curse_type * shipconf_ct, * regconf_ct;
	if (!init) { 
		init = true; 
		regconf_ct = ct_find("disorientationzone"); 
		shipconf_ct = ct_find("shipdisorientation"); 
	}
	if (u->ship && curse_active(get_curse(u->ship->attribs, shipconf_ct)))
		return true;

	if (curse_active(get_curse(u->region->attribs, regconf_ct)))
		return true;

	return false;
}

#ifdef TODO /* Wenn Feature ausgearbeitet */
void
regain_orientation(region * r)
{
	ship *sh;
	curse *c;
	unit *u, *cap;

	for(sh = r->ships; sh; sh = sh->next) {
		c = get_curse(sh->attribs, C_DISORIENTATION, 0);
		if(!c) continue;

		/* Das Schiff bekommt seine Orientierung zur�ck, wenn es:
		 * a) An Land treibt.
		 * b) Gl�ck hat.
		 * c) In einer Region mit einem nicht verwirrten alliierten
		 * 		Schiff steht.
		 */

		cap = shipowner(r, sh);

		if(r->terrain != T_OCEAN || rand() % 10 >= storms[month(0)]) {
			remove_curse(&sh->attribs, C_DISORIENTATION, 0);
			add_message(&cap->faction->msgs,
				new_message(cap->faction, "shipnoconf%h:ship", sh));
			continue;
		}

		for(u=r->units;u;u=u->next) {
			if(u != cap
					&& allied(cap, u->faction, HELP_GUARD)
					&& is_disorientated(u) == false) {
				remove_curse(&sh->attribs, C_DISORIENTATION, 0);
				add_message(&cap->faction->msgs,
					new_message(cap->faction, "shipnoconf%h:ship", sh));
				break;
			}
		}
	}
}
#endif

/* Bewegung, Verfolgung, Piraterie */

void
movement(void)
{
	region *r;
	int ships;

	/* Initialize the additional encumbrance by transported units */
	init_drive();

	/* move ships in last phase, others first */
	for (ships=0;ships<=1;++ships) for (r = regions; r; r = r->next) {
		unit ** up = &r->units;
		/* Bewegungen.
		 *
		 * Zuerst m�ssen sich alle Einheiten ohne u->target bewegen
		 * (NACH), dann starten die Verfolger mit u->target (FOLGE) und
		 * h�ren erst auf, wenn sie ihr Ziel erreicht haben.
		 *
		 * neue NEW_FOLLOW-Variante: Verfolger folgen sofort, FL_FOLLOW
		 * ist bereits gesetzt.
		 */
		while (*up) {
			unit *u = *up;
			keyword_t kword;

			if (fval(u, FL_FOLLOWING)) {
				/* skip all followers */
				do {
					u = u->next;
				} while (u && fval(u, FL_FOLLOWING));
				up = &u;
				if (u==NULL) break;
			}
			kword = igetkeyword(u->thisorder, u->faction->locale);
			switch (kword) {
			case K_ROUTE:
			case K_MOVE:
				if (attacked(u)) {
					cmistake(u, findorder(u, u->thisorder), 52, MSG_MOVE);
					set_string(&u->thisorder, "");
					up = &u->next;
				} else if ((u->race->flags & RCF_CANNOTMOVE) 
						|| get_movement(&u->attribs, MV_CANNOTMOVE))
				{
					cmistake(u, findorder(u, u->thisorder), 55, MSG_MOVE);
					set_string(&u->thisorder, "");
					up = &u->next;
				} else {
					boolean moved = true;
					if (ships) {
						if (u->ship && fval(u, FL_OWNER)) move(r, u, true);
						else moved=false;
					} else {
						if (u->ship==NULL || !fval(u, FL_OWNER)) move(r, u, true);
						else moved=false;
					}
					if (moved) {
						set_string(&u->thisorder, "");
						up = &r->units;
					} else {
						up = &u->next;
					}
				}
				break;
			default:
				up = &u->next;
			}
		}
	}

	/* Dann generieren die jagenden Einheiten ihre Befehle und
	 * bewegen sich. */

	for (r = regions; r; r = r->next) {
		unit * u;
		for (u = r->units; u;) {
			unit *u2 = u->next;
			strlist *o;
			param_t p;

			for (o=u->orders;o;o=o->next) {
				if(igetkeyword(o->s, u->faction->locale) == K_FOLLOW) {
					if(attacked(u)) {
						cmistake(u, o->s, 52, MSG_MOVE);
						break;
					} else if((u->race->flags & RCF_CANNOTMOVE)
							|| get_movement(&u->attribs, MV_CANNOTMOVE))
					{
						cmistake(u, o->s, 55, MSG_MOVE);
						break;
					}

					p = getparam(u->faction->locale);
					if(p != P_SHIP) {
						if(p != P_UNIT) {
							cmistake(u, o->s, 240, MSG_MOVE);
						}
						break;
					}

					if (!fval(u, FL_LONGACTION) && !fval(u, FL_HUNGER) && hunt(u)) {
						u2 = r->units;
						break;
					}
				}
			}
			u = u2;
		}
	}

	/* Regul�re Verfolger starten */

	for (r = regions; r; r = r->next) {
		unit * u;
		for (u = r->units; u;) {
			unit * u2 = u->next;
			if (utarget(u)
				&& (igetkeyword(u->thisorder, u->faction->locale) == K_MOVE
				|| igetkeyword(u->thisorder, u->faction->locale) == K_ROUTE)) {
					if (attacked(u)) {
						cmistake(u, findorder(u, u->thisorder), 52, MSG_PRODUCE);
					} else if ((u->race->flags & RCF_CANNOTMOVE)
							|| get_movement(&u->attribs, MV_CANNOTMOVE))
					{
						cmistake(u, findorder(u, u->thisorder), 55, MSG_PRODUCE);
					} else
						move(r, u, true);
			}
			u = u2;
		}

		/* Abtreiben von besch�digten, unterbemannten, �berladenen Schiffen */

		drifting_ships(r);
	}

	/* Piraten und Cleanup */

	for (r = regions; r; r = r->next) {
		unit * u;
		for(u = r->units; u;) {
			unit *un = u->next;
			if(igetkeyword(u->thisorder, u->faction->locale) == K_PIRACY) piracy(u);
			u = un;
		}
		a_removeall(&r->attribs, &at_piracy_direction);
		age_traveldir(r);
	}
}

void
follow(void)
{
	region * r;
	for (r=regions;r;r=r->next) {
		unit * u;
		for (u=r->units;u;u=u->next) {
			attrib * a;
			strlist * o;
			if (fval(u, FL_LONGACTION) || fval(u, FL_HUNGER)) continue;
			a = a_find(u->attribs, &at_follow);
			for (o=u->orders;o;o=o->next) {
				if (igetkeyword(o->s, u->faction->locale) == K_FOLLOW
						&& getparam(u->faction->locale) == P_UNIT) {
					int id = read_unitid(u->faction, r);
					if (id>0) {
						unit * uf = findunit(id);
						if (!a) {
							a = a_add(&u->attribs, make_follow(uf));
						} else {
							a->data.v = uf;
						}
					} else if (a) {
						a_remove(&u->attribs, a);
						a = NULL;
					}
				}
			}
			if (a && !fval(u, FL_MOVED)) {
				unit * u2 = a->data.v;
				if (!u2 || u2->region!=r || !cansee(u->faction, r, u2, 0))
					continue;
				for (o=u2->orders;o;o=o->next) {
					switch (igetkeyword(o->s, u2->faction->locale)) {
					case K_MOVE:
					case K_ROUTE:
					case K_DRIVE:
					case K_FOLLOW:
					case K_PIRACY:
						fset(u, FL_FOLLOWING);
						fset(u2, FL_FOLLOWED);
						set_string(&u->thisorder, "");
						break;
					default:
						continue;
					}
					break;
				}
			}
		}
	}
}
