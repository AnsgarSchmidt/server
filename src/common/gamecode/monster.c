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

#include <config.h>
#include "eressea.h"
#include "monster.h"

/* gamecode includes */
#include "economy.h"

/* triggers includes */
#include <triggers/removecurse.h>

/* attributes includes */
#include <attributes/targetregion.h>
#include <attributes/hate.h>
#include <attributes/aggressive.h>

/* spezialmonster */
#include <spells/alp.h>

/* kernel includes */
#include <kernel/build.h>
#include <kernel/faction.h>
#include <kernel/give.h>
#include <kernel/item.h>
#include <kernel/message.h>
#include <kernel/movement.h>
#include <kernel/names.h>
#include <kernel/order.h>
#include <kernel/pathfinder.h>
#include <kernel/pool.h>
#include <kernel/race.h>
#include <kernel/region.h>
#include <kernel/reports.h>
#include <kernel/skill.h>
#include <kernel/unit.h>

/* util includes */
#include <attrib.h>
#include <base36.h>
#include <event.h>
#include <rand.h>

/* libc includes */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define UNDEAD_REPRODUCTION         0 	/* vermehrung */
#define MOVECHANCE                  25	/* chance fuer bewegung */

#define MAXILLUSION_TEXTS   3

static boolean
is_waiting(const unit * u)
{
	if (fval(u, UFL_ISNEW)) return true;
	if (get_keyword(u->lastorder)==K_WAIT) return true;

	return false;
}

static boolean
monster_attack(unit * u, const unit * target)
{
	char zText[20];

	if (u->region!=target->region) return false;
	if (!cansee(u->faction, u->region, target, 0)) return false;
	if (is_waiting(u)) return false;

	sprintf(zText, "%s %s",
		locale_string(u->faction->locale, keywords[K_ATTACK]), unitid(target));
	addlist(&u->orders, parse_order(zText, u->faction->locale));
	return true;
}

void
taxed_by_monster(unit * u)
{
  const char * zText = locale_string(u->faction->locale, keywords[K_TAX]);
  addlist(&u->orders, parse_order(zText, u->faction->locale));
}

static boolean
get_money_for_dragon(region * r, unit * u, int wanted)
{
	unit *u2;
	int n;

	/* attackiere bewachende einheiten */

	for (u2 = r->units; u2; u2 = u2->next)
		if (u2 != u && getguard(u2)&GUARD_TAX)
			monster_attack(u, u2);

	/* treibe steuern bei den bauern ein */

	taxed_by_monster(u);

	/* falls das genug geld ist, bleibt das monster hier */

	if (rmoney(r) >= wanted)
		return true;

	/* attackiere so lange die fremden, alteingesessenen einheiten mit geld
	 * (einfach die reihenfolge der einheiten), bis das geld da ist. n
	 * zaehlt das so erhaltene geld. */

	n = 0;
	for (u2 = r->units; u2; u2 = u2->next)
		if (u2->faction != u->faction && get_money(u2)) {
			if (monster_attack(u, u2)) {
				n += get_money(u2);

				if (n > wanted)
					break;
			}
		}
	/* falls die einnahmen erreicht werden, bleibt das monster noch eine
	 * runde hier. */

	if (n + rmoney(r) >= wanted)
		return true;

	/* falls das geld, das wir haben, und das der angriffe zum leben
	 * reicht, koennen wir uns fortbewegen. deswegen duerfen wir
	 * steuereinnahmen nicht beruecksichtigen - wir bewegen uns ja fort. */

	if (get_money(u) + n >= MAINTENANCE)
		return false;

	/* falls wir auch mit angriffe nicht genug geld zum wandern haben,
	 * muessen wir wohl oder uebel hier bleiben. vielleicht wurden ja genug
	 * steuern eingetrieben */

	return true;
}

static int
money(region * r)
{
	unit *u;
	int m;

	m = rmoney(r);
	for (u = r->units; u; u = u->next)
		m += get_money(u);
	return m;
}

static direction_t
richest_neighbour(region * r, int absolut)
{

	/* m - maximum an Geld, d - Richtung, i - index, t = Geld hier */

	double m;
	double t;
	direction_t d = NODIRECTION, i;

	if (absolut == 1 || rpeasants(r) == 0) {
		m = (double) money(r);
	} else {
		m = (double) money(r) / (double) rpeasants(r);
	}

	/* finde die region mit dem meisten geld */

	for (i = 0; i != MAXDIRECTIONS; i++)
		if (rconnect(r, i) && rterrain(rconnect(r, i)) != T_OCEAN) {
			if (absolut == 1 || rpeasants(r) == 0) {
				t = (double) money(rconnect(r, i));
			} else {
				t = (double) money(rconnect(r, i)) / (double) rpeasants(r);
			}

			if (t > m) {
				m = t;
				d = i;
			}
		}
	return d;
}

static boolean
room_for_race_in_region(region *r, const race * rc)
{
	unit *u;
	int  c = 0;

	for(u=r->units;u;u=u->next) {
		if(u->race == rc) c += u->number;
	}

	if(c > (rc->splitsize*2))
		return false;

	return true;
}

static direction_t
random_neighbour(region * r, unit *u)
{
	direction_t i;
	region *rc;
	int rr, c = 0, c2 = 0;

	/* Nachsehen, wieviele Regionen in Frage kommen */

	for (i = 0; i != MAXDIRECTIONS; i++) {
		rc = rconnect(r, i);
		if (rc && can_survive(u, rc)) {
			if(room_for_race_in_region(rc, u->race)) {
				c++;
			}
			c2++;
		}
	}

	if (c == 0) {
		if(c2 == 0) {
			return NODIRECTION;
		} else {
			c  = c2;
			c2 = 0;		/* c2 == 0 -> room_for_race nicht beachten */
		}
	}

	/* Zuf�llig eine ausw�hlen */

	rr = rand() % c;

	/* Durchz�hlen */

	c = -1;
	for (i = 0; i != MAXDIRECTIONS; i++) {
		rc = rconnect(r, i);
		if (rc && can_survive(u, rc)) {
			if(c2 == 0) {
				c++;
			} else if(room_for_race_in_region(rc, u->race)) {
				c++;
			}
			if (c == rr) return i;
		}
	}

	assert(1 == 0);				/* Bis hierhin sollte er niemals kommen. */
	return NODIRECTION;
}

static direction_t
treeman_neighbour(region * r)
{
	direction_t i;
	int rr;
	int c = 0;

	/* Nachsehen, wieviele Regionen in Frage kommen */

	for (i = 0; i != MAXDIRECTIONS; i++) {
		if (rconnect(r, i)
			&& rterrain(rconnect(r, i)) != T_OCEAN
			&& rterrain(rconnect(r, i)) != T_GLACIER
			&& rterrain(rconnect(r, i)) != T_DESERT) {
			c++;
		}
	}

	if (c == 0) {
		return NODIRECTION;
	}
	/* Zuf�llig eine ausw�hlen */

	rr = rand() % c;

	/* Durchz�hlen */

	c = -1;
	for (i = 0; i != MAXDIRECTIONS; i++) {
		if (rconnect(r, i)
			&& rterrain(rconnect(r, i)) != T_OCEAN
			&& rterrain(rconnect(r, i)) != T_GLACIER
			&& rterrain(rconnect(r, i)) != T_DESERT) {
			c++;
			if (c == rr) {
				return i;
			}
		}
	}

	assert(1 == 0);				/* Bis hierhin sollte er niemals kommen. */
	return NODIRECTION;
}

static void
move_monster(region * r, unit * u)
{
  direction_t d = NODIRECTION;

  switch(old_race(u->race)) {
    case RC_FIREDRAGON:
    case RC_DRAGON:
    case RC_WYRM:
      d = richest_neighbour(r, 1);
      break;
    case RC_TREEMAN:
      d = treeman_neighbour(r);
      break;
    default:
      d = random_neighbour(r,u);
      break;
  }

  /* falls kein geld gefunden wird, zufaellig verreisen, aber nicht in
  * den ozean */

  if (d == NODIRECTION)
    return;

  sprintf(buf, "%s %s", locale_string(u->faction->locale, keywords[K_MOVE]), locale_string(u->faction->locale, directions[d]));

  addlist(&u->orders, parse_order(buf, u->faction->locale));
}

/* Wir machen das mal autoconf-style: */
#ifndef HAVE_DRAND48
#define drand48() (((double)rand()) / RAND_MAX)
#endif

static int
dragon_affinity_value(region *r, unit *u)
{
	int m = count_all_money(r);

	if(u->race == new_race[RC_FIREDRAGON]) {
		return (int)(normalvariate(m,m/2));
	} else {
		return (int)(normalvariate(m,m/4));
	}
}

static attrib *
set_new_dragon_target(unit * u, region * r, int range)
{
	int max_affinity = 0;
	region *max_region = NULL;

#if 1
  region_list * rptr, * rlist = regions_in_range(r, range, allowed_dragon);
  for (rptr=rlist;rptr;rptr=rptr->next) {
    region * r2 = rptr->data;
    int affinity = dragon_affinity_value(r2, u);
    if (affinity > max_affinity) {
      max_affinity = affinity;
      max_region = r2;
    }
  }

  free_regionlist(rlist);
#else
  int x, y;
  for (x = r->x - range; x < r->x + range; x++) {
		for (y = r->y - range; y < r->y + range; y++) {
      region * r2 = findregion(x, y);
      if (r2!=NULL) {
        int affinity = dragon_affinity_value(r2, u);
        if (affinity > max_affinity) {
          if (koor_distance (r->x, r->y, x, y) <= range && path_exists(r, r2, range, allowed_dragon)) 
          {
            max_affinity = affinity;
            max_region = r2;
          }
				}
			}
		}
	}
#endif
	if (max_region && max_region != r) {
		attrib * a = a_find(u->attribs, &at_targetregion);
		if (!a) {
			a = a_add(&u->attribs, make_targetregion(max_region));
		} else {
			a->data.v = max_region;
		}
		sprintf(buf, "Kommt aus: %s, Will nach: %s", 
      regionname(r, u->faction), regionname(max_region, u->faction));
		usetprivate(u, buf);
		return a;
	}
	return NULL;
}

static boolean
set_movement_order(unit * u, const region * target, int moves, boolean (*allowed)(const region *, const region *))
{
	region * r = u->region;
	region ** plan = path_find(r, target, DRAGON_RANGE*5, allowed);
	int position = 0;
	char * c;

	if (plan==NULL) {
		return false;
	}

	strcpy(buf, locale_string(u->faction->locale, keywords[K_MOVE]));
	c = buf + strlen(buf);

	while (position!=moves && plan[position+1]) {
		region * prev = plan[position];
		region * next = plan[++position];
		direction_t dir = reldirection(prev, next);
		assert(dir!=NODIRECTION && dir!=D_SPECIAL);
		*c++ = ' ';
		strcpy(c, locale_string(u->faction->locale, directions[dir]));
		c += strlen(c);
	}

	set_order(&u->lastorder, parse_order(buf, u->faction->locale));
  free_order(u->lastorder); /* parse_order & set_order have both increased the refcount */
	return true;
}

static void
monster_seeks_target(region *r, unit *u)
{
	direction_t d;
	unit *target = NULL;
	int dist, dist2;
	direction_t i;
	region *nr;

	/* Das Monster sucht ein bestimmtes Opfer. Welches, steht
	 * in einer Referenz/attribut
	 * derzeit gibt es nur den alp
	 */

	switch( old_race(u->race) ) {
		case RC_ALP:
			target = alp_target(u);
			break;
		default:
			assert(!"Seeker-Monster gibt kein Ziel an");
	}

	/* TODO: pr�fen, ob target �berhaupt noch existiert... */
	if(!target) return; /* this is a bug workaround! remove!! */

	if(r == target->region ) { /* Wir haben ihn! */
		if (u->race == new_race[RC_ALP]) {
			alp_findet_opfer(u, r);
		}
		else {
			assert(!"Seeker-Monster hat keine Aktion fuer Ziel");
		}
		return;
	}

	/* Simpler Ansatz: Nachbarregion mit gerinster Distanz suchen.
	 * Sinnvoll momentan nur bei Monstern, die sich nicht um das
	 * Terrain k�mmern.  Nebelw�nde & Co machen derzeit auch nix...
	 */
	dist2 = distance(r, target->region);
	d = NODIRECTION;
	for( i = 0; i < MAXDIRECTIONS; i++ ) {
		nr = rconnect(r, i);
		assert(nr);
		dist = distance(nr, target->region);
		if( dist < dist2 ) {
			dist2 = dist;
			d = i;
		}
	}
	assert(d != NODIRECTION );

	if (u->race == new_race[RC_ALP]) {
		if( (u->age % 2) )		/* bewegt sich nur jede zweite Runde */
			d = NODIRECTION;
	}

	if( d == NODIRECTION )
		return;
	sprintf(buf, "%s %s", locale_string(u->faction->locale, keywords[K_MOVE]), locale_string(u->faction->locale, directions[d]));
	addlist(&u->orders, parse_order(buf, u->faction->locale));
}

unit *
random_unit(const region * r)
{
	int c = 0;
	int n;
	unit *u;

	for (u = r->units; u; u = u->next) {
		if (u->race != new_race[RC_SPELL]) {
			c += u->number;
		}
	}

	if (c == 0) {
		return NULL;
	}
	n = rand() % c;
	c = 0;
	u = r->units;

	while (u && c < n) {
		if (u->race != new_race[RC_SPELL]) {
			c += u->number;
		}
		u = u->next;
	}

	return u;
}

static boolean
random_attack_by_monster(const region * r, unit * u)
{
	boolean success = false;
	unit *target;
	int kill, max;
	int tries = 0;
	int attacked = 0;

	switch (old_race(u->race)) {
	case RC_FIREDRAGON:
		kill = 25;
		max = 50;
		break;
	case RC_DRAGON:
		kill = 100;
		max = 200;
		break;
	case RC_WYRM:
		kill = 400;
		max = 800;
		break;
	default:
		kill = 1;
		max = 1;
	}

	kill *= u->number;
	max *= u->number;

	do {
		tries++;
		target = random_unit(r);
		if (target
		    && target != u
		    && humanoidrace(target->race)
			  && !illusionaryrace(target->race)
		    && target->number <= max)
		{
			if (monster_attack(u, target)) {
				unit * u2;
				success = true;
				for (u2 = r->units; u2; u2 = u2->next) {
					if (u2->faction->no == MONSTER_FACTION
						&& rand() % 100 < 75)
					{
						monster_attack(u2, target);
					}
				}
				attacked += target->number;
			}
		}
	}
	while (attacked < kill && tries < 10);
	return success;
}

static void
eaten_by_monster(unit * u)
{
	int n = 0;
	int horse = 0;

	switch (old_race(u->race)) {
	case RC_FIREDRAGON:
		n = rand()%80 * u->number;
		horse = get_item(u, I_HORSE);
		break;
	case RC_DRAGON:
		n = rand()%200 * u->number;
		horse = get_item(u, I_HORSE);
		break;
	case RC_WYRM:
		n = rand()%500 * u->number;
		horse = get_item(u, I_HORSE);
		break;
	default:
		n = rand()%(u->number/20+1);
	}

	if (n > 0) {
		n = lovar(n);
		n = min(rpeasants(u->region), n);

		if (n > 0) {
			deathcounts(u->region, n);
			rsetpeasants(u->region, rpeasants(u->region) - n);
			add_message(&u->region->msgs, new_message(NULL,
				"eatpeasants%u:unit%i:amount", u, n));
		}
	}
	if (horse > 0) {
		set_item(u, I_HORSE, 0);
		add_message(&u->region->msgs, new_message(NULL,
					"eathorse%u:unit%i:amount", u, horse));
	}
}

static void
absorbed_by_monster(unit * u)
{
	int n;

	switch (old_race(u->race)) {
	default:
		n = rand()%(u->number/20+1);
	}

	if(n > 0) {
		n = lovar(n);
		n = min(rpeasants(u->region), n);
		if(n > 0){
			rsetpeasants(u->region, rpeasants(u->region) - n);
			scale_number(u, u->number + n);
			add_message(&u->region->msgs, new_message(NULL,
				"absorbpeasants%u:unit%i:amount", u, n));
		}
	}
}

static int
scareaway(region * r, int anzahl)
{
	int n, p, d = 0, emigrants[MAXDIRECTIONS];
	direction_t i;

	anzahl = min(max(1, anzahl),rpeasants(r));

	/* Wandern am Ende der Woche (normal) oder wegen Monster. Die
	 * Wanderung wird erst am Ende von demographics () ausgefuehrt.
	 * emigrants[] ist local, weil r->newpeasants durch die Monster
	 * vielleicht schon hochgezaehlt worden ist. */

	for (i = 0; i != MAXDIRECTIONS; i++)
		emigrants[i] = 0;

	p = rpeasants(r);
	assert(p >= 0 && anzahl >= 0);
	for (n = min(p, anzahl); n; n--) {
		direction_t dir = (direction_t)(rand() % MAXDIRECTIONS);
		region * c = rconnect(r, dir);

		if (c && landregion(rterrain(c))) {
			d++;
			c->land->newpeasants++;
			emigrants[dir]++;
		}
	}
	rsetpeasants(r, p-d);
	assert(p >= d);
	return d;
}

static void
scared_by_monster(unit * u)
{
	int n;

	switch (old_race(u->race)) {
	case RC_FIREDRAGON:
		n = rand()%160 * u->number;
		break;
	case RC_DRAGON:
		n = rand()%400 * u->number;
		break;
	case RC_WYRM:
		n = rand()%1000 * u->number;
		break;
	default:
		n = rand()%(u->number/4+1);
	}

	if(n > 0) {
		n = lovar(n);
		n = min(rpeasants(u->region), n);
		if(n > 0) {
			n = scareaway(u->region, n);
			if(n > 0) {
				add_message(&u->region->msgs,
							new_message(NULL,
										"fleescared%i:amount%u:unit", n, u));
			}
		}
	}
}

static const char *
random_growl(void)
{
	switch(rand()%5) {
	case 0:
		return "Groammm";
	case 1:
		return "Roaaarrrr";
	case 2:
		return "Chhhhhhhhhh";
	case 3:
		return "Tschrrrkk";
	case 4:
		return "Schhhh";
	}
	return "";
}

extern attrib_type at_direction;

static void
learn_monster(unit *u)
{
	int c = 0;
	int n;
	skill * sv;

	/* Monster lernt ein zuf�lliges Talent aus allen, in denen es schon
	 * Lerntage hat. */

  for (sv = u->skills; sv != u->skills + u->skill_size; ++sv) {
    if (sv->level>0) ++c;
  }

	if(c == 0) return;

	n = rand()%c + 1;
	c = 0;

  for (sv = u->skills; sv != u->skills + u->skill_size; ++sv) {
		if (sv->level>0) {
			if (++c == n) {
				sprintf(buf, "%s %s", locale_string(u->faction->locale, keywords[K_STUDY]),
					skillname(sv->id, u->faction->locale));
				set_order(&u->thisorder, parse_order(buf, u->faction->locale));
        free_order(u->thisorder); /* parse_order & set_order have both increased the refcount */
				break;
			}
		}
	}
}

void
monsters_kill_peasants(void)
{
  region *r;
  unit *u;

  for (r = regions; r; r = r->next) {
    for (u = r->units; u; u = u->next) if(!fval(u, UFL_MOVED)) {
      if(u->race->flags & RCF_SCAREPEASANTS) {
        scared_by_monster(u);
      }
      if(u->race->flags & RCF_KILLPEASANTS) {
        eaten_by_monster(u);
      }
      if(u->race->flags & RCF_ABSORBPEASANTS) {
        absorbed_by_monster(u);
      }
    }
  }
}

static boolean
check_overpopulated(unit *u)
{
  unit *u2;
  int c = 0;

  for(u2 = u->region->units; u2; u2 = u2->next) {
    if(u2->race == u->race && u != u2) c += u2->number;
  }

  if(c > u->race->splitsize * 2) return true;

  return false;
}

static void
plan_dragon(unit * u)
{
  attrib * ta = a_find(u->attribs, &at_targetregion);
  region * r = u->region;
  region * tr = NULL;
  int horses = get_resource(u,R_HORSE);
  int capacity = walkingcapacity(u);
  item ** itmp = &u->items;
  boolean move = false;

  if (horses > 0) {
    change_resource(u, R_HORSE, - min(horses,(u->number*2)));
  }
  while (capacity>0 && *itmp!=NULL) {
    item * itm = *itmp;
    if (itm->type->capacity<itm->type->weight) {
      int weight = itm->number*itm->type->capacity;
      if (weight > capacity) {
        int error = give_item(itm->number, itm->type, u, NULL, NULL);
        if (error!=0) break;
      }
    }
    if (*itmp==itm) itmp=&itm->next;
  }

  if (ta==NULL) {
    move |= (r->land==0 || r->land->peasants==0); /* when no peasants, move */
    move |= (r->land==0 || r->land->money==0); /* when no money, move */
  }
  move |= chance(0.04); /* 4% chance to change your mind */
  if (move) {
    /* dragon gets bored and looks for a different place to go */
    ta = set_new_dragon_target(u, u->region, DRAGON_RANGE);
  }
  else ta = a_find(u->attribs, &at_targetregion);
  if (ta!=NULL) {
    tr = (region *) ta->data.v;
    if (tr==NULL || !path_exists(u->region, tr, DRAGON_RANGE, allowed_dragon)) {
      ta = set_new_dragon_target(u, u->region, DRAGON_RANGE);
      if (ta) tr = findregion(ta->data.sa[0], ta->data.sa[1]);
    }
  }
  if (tr!=NULL) {
    switch(old_race(u->race)) {
    case RC_FIREDRAGON:
      set_movement_order(u, tr, 4, allowed_dragon);
      break;
    case RC_DRAGON:
      set_movement_order(u, tr, 3, allowed_dragon);
      break;
    case RC_WYRM:
      set_movement_order(u, tr, 1, allowed_dragon);
      break;
    }
    if (rand()%100 < 15) {
      /* do a growl */
      if (rname(tr, u->faction->locale)) {
        sprintf(buf,
          "botschaft an region %s~...~%s~etwas~in~%s.",
          estring(random_growl()), u->number==1?"Ich~rieche":"Wir~riechen",
          estring(rname(tr, u->faction->locale)));
        addlist(&u->orders, parse_order(buf, u->faction->locale));
      }
    }
  } else {
    if (!get_money_for_dragon(u->region, u, income(u))) {
      /* money is gone */
      set_new_dragon_target(u, u->region, DRAGON_RANGE);
    }
    else if (u->race != new_race[RC_FIREDRAGON] && u->region->terrain!=T_OCEAN
      && !(terrain[rterrain(u->region)].flags & FORBIDDEN_LAND)) {
        int ra = 20 + rand() % 100;
        if (get_money(u) > ra * 50 + 100 && rand() % 100 < 50)
        {
          const struct item_type * weapon = NULL;
          unit *un;
          un = createunit(u->region, findfaction(MONSTER_FACTION), ra, new_race[RC_DRACOID]);
          name_unit(un);
          change_money(u, -un->number * 50);

          set_level(un, SK_SPEAR, (3 + rand() % 4));
          set_level(un, SK_SWORD, (3 + rand() % 4));
          set_level(un, SK_LONGBOW, (2 + rand() % 3));

          switch (rand() % 3) {
          case 0:
            weapon = olditemtype[I_LONGBOW];
            break;
          case 1:
            weapon = olditemtype[I_SWORD];
            break;
          default:
            weapon = olditemtype[I_SPEAR];
            break;
          }
          i_change(&un->items, weapon, un->number);
          if (weapon->rtype->wtype->flags & WTF_MISSILE) un->status = ST_BEHIND;
          else un->status = ST_FIGHT;
          sprintf(buf, "%s \"%s\"", keywords[K_STUDY], skillname(weapon->rtype->wtype->skill, u->faction->locale));
          set_order(&un->lastorder, parse_order(buf, default_locale));
        }
        if (is_waiting(u)) {
          sprintf(buf, "%s \"%s\"", keywords[K_STUDY], skillname(SK_OBSERVATION, u->faction->locale));
          set_order(&u->thisorder, parse_order(buf, default_locale));
          set_order(&u->lastorder, u->thisorder);
        }
      }
  }
}

void
plan_monsters(void)
{
  region *r;
  faction *f = findfaction(MONSTER_FACTION);

  if (!f) return;
  f->lastorders = turn;

  for (r = regions; r; r = r->next) {
    unit *u;
    for (u = r->units; u; u = u->next) {
      boolean is_moving = false;
      attrib * ta;

      /* Ab hier nur noch Befehle f�r NPC-Einheiten. */
      if (u->faction->no != MONSTER_FACTION) continue;

      if (u->status>ST_BEHIND) u->status = ST_FIGHT; /* all monsters fight */
      /* Monster bekommen jede Runde ein paar Tage Wahrnehmung dazu */
      produceexp(u, SK_OBSERVATION, u->number);

      /* Haben Drachen ihr Ziel erreicht? */
      ta = a_find(u->attribs, &at_targetregion);
      if (ta) {
        if (u->region == (region*)ta->data.v) {
          a_remove(&u->attribs, ta);
          set_order(&u->lastorder, parse_order(keywords[K_WAIT], u->faction->locale));
          free_order(u->lastorder); /* parse_order & set_order have each increased the refcount */
        }
        else {
          is_moving = true;
        }
      }

      ta = a_find(u->attribs, &at_hate);
      if (ta && !is_waiting(u)) {
        unit * tu = (unit *)ta->data.v;
        if (tu && tu->region==r) {
          sprintf(buf, "%s %s", locale_string(u->faction->locale, keywords[K_ATTACK]), itoa36(tu->no));
          addlist(&u->orders, parse_order(buf, u->faction->locale));
        } else if (tu) {
          tu = findunitg(ta->data.i, NULL);
          if (tu) set_movement_order(u, tu->region, 2, allowed_walk);
        }
        else a_remove(&u->attribs, ta);
      }

      if (!(fval(u, UFL_ISNEW)) && r->terrain != T_OCEAN) { /* Monster bewachen immer */
        const char * cmd = locale_string(u->faction->locale, keywords[K_GUARD]);
        addlist(&u->orders, parse_order(cmd, u->faction->locale));
      }

      /* Diese Verkettung ist krank und sollte durch eine 'vern�nftige KI'
      * ersetzt werden. */

      if( (u->race->flags & RCF_MOVERANDOM)
        && (rand()%100<MOVECHANCE || check_overpopulated(u))) {
          move_monster(r, u);
        } else {
          boolean done = false;
          if((u->race->flags & RCF_ATTACKRANDOM) && is_moving == false)
          {
            double probability;
            attrib *a = a_find(u->attribs, &at_aggressive);

            if (a) {
              probability = a->data.flt;
            } else {
              probability = MONSTERATTACK;
            }

            if(chance(probability)) {
              done = random_attack_by_monster(r, u);
            }
          }
          if (!done) {
            if(u->race == new_race[RC_SEASERPENT]) {
              set_order(&u->thisorder, parse_order(keywords[K_PIRACY], default_locale));
              set_order(&u->lastorder, u->thisorder);
              free_order(u->lastorder); /* parse_order & set_order have both increased the refcount */
            } else if(u->race->flags & RCF_LEARN) {
              learn_monster(u);
            }
          }
        }

        /* Ab hier noch nicht generalisierte Spezialbehandlungen. */

        switch (old_race(u->race)) {
        case RC_ALP:
          monster_seeks_target(r, u);
          break;
        case RC_FIREDRAGON:
        case RC_DRAGON:
        case RC_WYRM:
          plan_dragon(u);
          break;
        }
    }
  }
}

void
age_unit(region * r, unit * u)
{
  if (u->race == new_race[RC_SPELL]) {
    if (--u->age <= 0) {
      destroy_unit(u);
    }
  } else {
    ++u->age;
    if (u->race->age) {
      u->race->age(u);
    }
  }
}
