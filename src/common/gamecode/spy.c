/* vi: set ts=2:
 *
 *	$Id: spy.c,v 1.5 2001/02/19 16:45:23 katze Exp $
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
#include "eressea.h"
#include "spy.h"

/* kernel includes */
#include "build.h"
#include "creation.h"
#include "economy.h"
#include "item.h"
#include "karma.h"
#include "faction.h"
#include "magic.h"
#include "message.h"
#include "movement.h"
#include "race.h"
#include "region.h"
#include "ship.h"
#include "skill.h"
#include "unit.h"

/* util includes */
#include "vset.h"

/* libc includes */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void
spy(region * r, unit * u)
{
	unit *target;
	int spy, observe;

	target = getunit(r, u);

	if (!target) {
		cmistake(u, findorder(u, u->thisorder), 64, MSG_EVENT);
		return;
	}
	if (!can_contact(r, u, target)) {
		cmistake(u, findorder(u, u->thisorder), 24, MSG_EVENT);
		return;
	}
	if (eff_skill(u, SK_SPY, r) < 1) {
		cmistake(u, findorder(u, u->thisorder), 39, MSG_EVENT);
		return;
	}
	spy = eff_skill(u, SK_SPY, r)
		- (eff_skill(target, SK_OBSERVATION, r) + eff_stealth(target, r) / 2);

	if (spy < 0) {
		add_message(&u->faction->msgs, new_message(u->faction,
			"spyfail%u:spy%u:target", u, target));
	} else if (spy == 0) {
		spy_message(spy, u, target);
	} else if (spy > 0) {
		spy_message(1, u, target);
	}
	observe = eff_skill(target, SK_OBSERVATION, r) - effskill(u, SK_STEALTH);

	if (get_item(u, I_RING_OF_INVISIBILITY) >= u->number &&
		get_item(target, I_AMULET_OF_TRUE_SEEING) == 0) {
		observe = min(observe, 0);
	}
	if (observe > 0)
		add_message(&target->faction->msgs, new_message(target->faction,
		"spydetect%u:spy%u:target", observe>0?u:NULL, target));
}

void
setstealth(unit * u, strlist * S)
{
	char *s;
	char level;
	race_t t;
	attrib *a;

	s = getstrtoken();

	/* Tarne ohne Parameter: Setzt maximale Tarnung */

	if (s == NULL || *s == 0) {
		u_seteffstealth(u, -1);
		return;
	}

	/* D�momen k�nnen sich als anderer race tarnen */

	if (u->race == RC_DAEMON) {
		if ((t = findrace(s)) != NORACE && !race[t].nonplayer) {
			u->irace = t;
			return;
		}
	}

	/* Pseudodrachen auch */

	if (u->race == RC_PSEUDODRAGON || RC_BIRTHDAYDRAGON) {
		t = findrace(s);
		if(t==RC_PSEUDODRAGON||t==RC_FIREDRAGON||t==RC_DRAGON||t==RC_WYRM) {
			u->irace = t;
			return;
		}
	}

	switch(findparam(s)) {
	case P_FACTION:
		/* TARNE PARTEI [NICHT] */
		s = getstrtoken();
		if (findparam(s) == P_NOT) {
			freset(u, FL_PARTEITARNUNG);
		} else {
			fset(u, FL_PARTEITARNUNG);
		}
		break;
	case P_ANY:
		/* TARNE ALLES (was nicht so alles geht?) */
		u_seteffstealth(u, -1);
		break;
	case P_NUMBER:
		/* TARNE ANZAHL [NICHT] */
		if(!fspecial(u->faction, FS_HIDDEN)) {
			cmistake(u, S->s, 277, MSG_EVENT);
			return;
		}
		s = getstrtoken();
		if (findparam(s) == P_NOT) {
			a = a_find(u->attribs, &at_fshidden);
			if(a) a->data.ca[0] = 0;
			if(a->data.i == 0) a_remove(&u->attribs, a);
		} else {
			a = a_find(u->attribs, &at_fshidden);
			if(!a) a = a_add(&u->attribs, a_new(&at_fshidden));
			a->data.ca[0] = 1;
		}
		break;
	case P_ITEMS:
		/* TARNE GEGENST�NDE [NICHT] */
		if(!fspecial(u->faction, FS_HIDDEN)) {
			cmistake(u, S->s, 277, MSG_EVENT);
			return;
		}
		if (findparam(s) == P_NOT) {
			a = a_find(u->attribs, &at_fshidden);
			if(a) a->data.ca[1] = 0;
			if(a->data.i == 0) a_remove(&u->attribs, a);
		} else {
			a = a_find(u->attribs, &at_fshidden);
			if(!a) a = a_add(&u->attribs, a_new(&at_fshidden));
			a->data.ca[1] = 1;
		}
		break;
	default:
		/* Sonst: Tarnungslevel setzen */
		level = (char) atoip(s);
		if (level > effskill(u, SK_STEALTH)) {
			sprintf(buf, "%s kann sich nicht so gut tarnen.", unitname(u));
			mistake(u, S->s, buf, MSG_EVENT);
			return;
		}
		u_seteffstealth(u, level);
	}
	return;
}

static int
faction_skill(region * r, faction * f, skill_t skill)
{
	int sk = 0;
	unit *u;

	list_foreach(unit, r->units, u)
		if (u->faction == f)
		{
			int s = eff_skill(u, skill, r);
			sk = max(sk, s);
		}
	list_next(u);
	return sk;
}

static int
crew_skill(region * r, faction * f, ship * sh, skill_t skill)
{
	int sk = 0;
	unit *u;

	list_foreach(unit, r->units, u)
		if (u->ship == sh && u->faction == f) {
			sk = eff_skill(u, skill, r);
			sk = max(skill, sk);
		}
	list_next(u);
	return sk;
}

static int
try_destruction(unit * u, unit * u2, const char *name, int skilldiff)
{
	const char *destruction_success_msg = "%s wurde von %s zerstoert.";
	const char *destruction_failed_msg = "%s konnte %s nicht zerstoeren.";
	const char *destruction_detected_msg = "%s wurde beim Versuch, %s zu zerstoeren, entdeckt.";
	const char *detect_failure_msg = "Es wurde versucht, %s zu zerstoeren.";
	const char *object_destroyed_msg = "%s wurde zerstoert.";

	if (skilldiff == 0) {
		/* tell the unit that the attempt failed: */
		sprintf(buf, destruction_failed_msg, unitname(u), name);
		addmessage(0, u->faction, buf, MSG_EVENT, ML_WARN);
		/* tell the enemy about the attempt: */
		sprintf(buf, detect_failure_msg, name);
		addmessage(0, u2->faction, buf, MSG_EVENT, ML_IMPORTANT);
		return 0;
	}
	if (skilldiff < 0) {
		/* tell the unit that the attempt was detected: */
		sprintf(buf, destruction_detected_msg, unitname(u), name);
		addmessage(0, u->faction, buf, MSG_EVENT, ML_WARN);
		/* tell the enemy whodunit: */
		sprintf(buf, detect_failure_msg, unitname(u2), unitname(u), name);
		addmessage(0, u2->faction, buf, MSG_EVENT, ML_IMPORTANT);
		return 0;
	}
	/* tell the unit that the attempt succeeded */
	sprintf(buf, destruction_success_msg, name, unitname(u));
	addmessage(0, u->faction, buf, MSG_EVENT, ML_INFO);
	sprintf(buf, object_destroyed_msg, name);
	addmessage(0, u2->faction, buf, MSG_EVENT, ML_IMPORTANT);
	return 1;					/* success */
}

static void
sink_ship(region * r, ship * sh, const char *name, char spy, unit * saboteur)
{
	const char *person_lost_msg = "- %d Person von %s ertrinkt; %s.";
	const char *persons_lost_msg = "- %d Personen von %s ertrinken; %s.";
	const char *unit_dies_msg = "Die Einheit wird ausgeloescht";
	const char *unit_lives_msg = "Die Einheit rettet sich nach ";
	const char *unit_intact_msg = "%s ueberlebt unbeschadet und rettet sich nach %s.";
	const char *ship_sinks_msg = "%s versinkt im Ozean.";
	const char *enemy_discovers_spy_msg = "%s wurde beim versenken von %s entdeckt.";
	const char *spy_discovered_msg = "%s entdeckte %s beim versenken von %s.";
	unit **ui;
	region *safety = r;
	faction *f;
	int i;
	direction_t d;
	unsigned int index;
	int chance;
	int money, count;
	char buffer[DISPLAYSIZE + 1];
	vset informed;
	vset survivors;

	vset_init(&informed);
	vset_init(&survivors);

	/* figure out what a unit's chances of survival are: */
	chance = 0;
	if (rterrain(r) != T_OCEAN)
		chance = CANAL_SWIMMER_CHANCE;
	else
		for (d = 0; d != MAXDIRECTIONS; ++d)
			if (rterrain(rconnect(r, d)) != T_OCEAN && !move_blocked(NULL, r, d)) {
				safety = rconnect(r, d);
				chance = OCEAN_SWIMMER_CHANCE;
				break;
			}
	for (ui = &r->units; *ui; ui = &(*ui)->next) {
		unit *u = *ui;

		/* inform this faction about the sinking ship: */
		vset_add(&informed, u->faction);
		if (u->ship == sh) {
			int dead = 0;

			/* if this fails, I misunderstood something: */
			for (i = 0; i != u->number; ++i)
				if (rand() % 100 >= chance)
					++dead;

			if (dead != u->number)
				/* she will live. but her items get stripped */
			{
				vset_add(&survivors, u);
				if (dead > 0) {
					strcat(strcpy(buffer, unit_lives_msg), regionid(safety));
					sprintf(buf, (dead == 1) ? person_lost_msg : persons_lost_msg,
							dead, unitname(u), buffer);
				} else
					sprintf(buf, unit_intact_msg, unitname(u), regionid(safety));
				addmessage(0, u->faction, buf, MSG_EVENT, ML_WARN);
				set_leftship(u, u->ship);
				u->ship = 0;
				if (r != safety)
					setguard(u, GUARD_NONE);
				while (u->items) i_remove(&u->items, u->items);
				move_unit(u, safety, NULL);
			} else {
				sprintf(buf, (dead == 1) ? person_lost_msg : persons_lost_msg,
						dead, unitname(u), unit_dies_msg);
			}
			if (dead == u->number)
				/* the poor creature, she dies */
			{
				*ui = u->next;
				destroy_unit(u);
			}
		}
	}

	/* inform everyone, and reduce money to the absolutely necessary
	 * amount: */
	while (informed.size != 0) {
		unit *lastunit = 0;

		f = (faction *) informed.data[0];
		money = 0;
		count = 0;
		/* find out how much money this faction still has: */
		for (index = 0; index != survivors.size; ++index) {
			unit *u = (unit *) survivors.data[index];

			if (u->faction == f) {
				count += u->number;
				money += get_money(u);
				lastunit = u;
			}
		}
		/* 'money' shall be the maintenance-surplus of the survivng
		 * units: */
		money = money - count * MAINTENANCE;
		for (index = 0; money > 0; ++index) {
			int remove;
			unit *u = (unit *) survivors.data[index];

			assert(index < survivors.size);
			if (u->faction == f && !nonplayer(u)) {
				remove = min(get_money(u), money);
				money -= remove;
				change_money(u, -remove);
			}
		}
		/* finally, report to this faction that the ship sank: */
		sprintf(buf, ship_sinks_msg, name);
		addmessage(0, f, buf, MSG_EVENT, ML_WARN);
		vset_erase(&informed, f);
		if (spy == 1 && f != saboteur->faction &&
			faction_skill(r, f, SK_OBSERVATION) - eff_skill(saboteur, SK_STEALTH, r) > 0) {
			/* the unit is discovered */
			sprintf(buf, spy_discovered_msg, lastunit, unitname(saboteur), name);
			addmessage(0, f, buf, MSG_EVENT, ML_IMPORTANT);
			sprintf(buf, enemy_discovers_spy_msg, unitname(saboteur), name);
			addmessage(0, saboteur->faction, buf, MSG_EVENT, ML_MISTAKE);
		}
	}
	/* finally, get rid of the ship */
	destroy_ship(sh, r);
	vset_destroy(&informed);
	vset_destroy(&survivors);
}

void
sabotage(region * r, unit * u)
{
	char *s;
	int i;
	ship *sh;
	unit *u2;
	char buffer[DISPLAYSIZE];

	s = getstrtoken();

	i = findparam(s);

	switch (i) {
	case P_SHIP:
		sh = u->ship;
		if (!sh) {
			cmistake(u, findorder(u, u->thisorder), 144, MSG_EVENT);
			return;
		}
		u2 = shipowner(r, sh);
		strcat(strcpy(buffer, shipname(sh)), sh->type->name[0]);
		if (try_destruction(u, u2, buffer, eff_skill(u, SK_SPY, r)
						  - crew_skill(r, u2->faction, sh, SK_OBSERVATION))) {
			sink_ship(r, sh, buffer, 1, u);
		}
		break;
#if 0
	case P_BUILDING:
		if(u->building) {
			/* TODO: Geb�ude zerst�ren */
		} else {
			building *b = getbuilding(r);
			unit *owner;

			if(!b) {
				cmistake(u, findorder(u, u->thisorder), 6, MSG_EVENT);
				return;
			}
			owner = buildingowner(r, b);

			if(owner == NULL || eff_skill(u, SK_STEALTH, r) > wahrnehmung(r,owner->faction) {
				/* Besser Curse benutzen, einfacher */
				a = a_add(b->attribs, a_new(&at_sabotaged));
				a->data.i = 1+rand()%(eff_skll(u, SK_SPY, r));
			} else if(owner) { /* Ertappt */
				add_message(&u->faction->msgs,
					new_message(u->faction, "sabot_building_fail%u:unit", u);
				add_message(&r->msgs,
					new_message(owner->faction, "sabot_building_detect%u:unit", u);
			} else {
			}
		}
		break;
#endif
	default:
		cmistake(u, findorder(u, u->thisorder), 9, MSG_EVENT);
		return;
	}

	return;
}

