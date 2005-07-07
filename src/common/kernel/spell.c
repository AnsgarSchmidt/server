/* vi: set ts=2:
 *
 *
 *  Eressea PB(E)M host Copyright (C) 1998-2003
 *      Christian Schlittchen (corwin@amber.kn-bremen.de)
 *      Katja Zedel (katze@felidae.kn-bremen.de)
 *      Henning Peters (faroul@beyond.kn-bremen.de)
 *      Enno Rehling (enno@eressea-pbem.de)
 *      Ingo Wilken (Ingo.Wilken@informatik.uni-oldenburg.de)
 *
 * based on:
 *
 * Atlantis v1.0  13 September 1993 Copyright 1993 by Russell Wallace
 * Atlantis v1.7		    Copyright 1996 by Alex Schr�der
 *
 * This program may not be used, modified or distributed without
 * prior permission by the authors of Eressea.
 * This program may not be sold or used commercially without prior written
 * permission from the authors.
 */

#include <config.h>
#include "eressea.h"
#include "spell.h"

/* kernel includes */
/* FIXME: brauchen wir die wirklich alle? */
#include "battle.h" /* f�r lovar */
#include "border.h"
#include "building.h"
#include "curse.h"
#include "faction.h"
#include "goodies.h"
#include "item.h"
#include "karma.h"
#include "magic.h"
#include "message.h"
#include "objtypes.h"
#include "order.h"
#include "plane.h"
#include "pool.h"
#include "race.h"
#include "region.h"
#include "resolve.h"
#include "ship.h"
#include "skill.h"
#include "spy.h"
#include "teleport.h"
#include "terrain.h"
#include "unit.h"

/* spells includes */
#include <spells/alp.h>

/* util includes */
#include <util/umlaut.h>
#include <util/base36.h>
#include <util/message.h>
#include <util/event.h>
#include <util/language.h>
#include <util/rand.h>
#include <util/variant.h>

/* libc includes */
#include <assert.h>
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

/* triggers includes */
#include <triggers/changefaction.h>
#include <triggers/changerace.h>
#include <triggers/createcurse.h>
#include <triggers/createunit.h>
#include <triggers/killunit.h>
#include <triggers/timeout.h>
#include <triggers/unitmessage.h>

/* attributes includes */
#include <attributes/targetregion.h>
#include <attributes/hate.h>
/* ----------------------------------------------------------------------- */

static variant zero_effect = { 0 };

attrib_type at_unitdissolve = {
	"unitdissolve", NULL, NULL, NULL, a_writedefault, a_readdefault
};

#ifdef WDW_PYRAMIDSPELL
attrib_type at_wdwpyramid = {
	"wdwpyramid", NULL, NULL, NULL, a_writedefault, a_readdefault
};
#endif

/* ----------------------------------------------------------------------- */

#ifdef TODO
static const char *
spellcmd(const strarray * sa, const struct locale * lang) {
	int i;
	char * p = buf;
	strcpy(p, locale_string(lang, keywords[K_CAST]));
	p += strlen(p);
	for (i=0;i!=sa->length;++i) {
		*p++ = ' ';
		strcpy(p, sa->strings[i]);
		p += strlen(p);
	}
	return buf;
}

void
report_failure(unit * mage, const strarray * sa) {
	/* Fehler: "Der Zauber schl�gt fehl" */
	cmistake(mage, strdup(spellcmd(sa, mage->faction->locale)), 180, MSG_MAGIC);
}
#else
void
report_failure(unit * mage, struct order * ord) {
	/* Fehler: "Der Zauber schl�gt fehl" */
	cmistake(mage, ord, 180, MSG_MAGIC);
}
#endif

/* ------------------------------------------------------------- */
/* do_shock - Schockt die Einheit, z.B. bei Verlust eines	*/
/* Vertrauten.						   */
/* ------------------------------------------------------------- */

void
do_shock(unit *u, const char *reason)
{
	int i;
	if(u->number == 0) return;

	/* HP - Verlust */
	u->hp = (unit_max_hp(u) * u->number)/10;
	u->hp = max(1, u->hp);
	/* Aura - Verlust */
	if(is_mage(u)) {
		set_spellpoints(u, max_spellpoints(u->region,u)/10);
	}

	/* Evt. Talenttageverlust */
	for (i=0;i!=u->skill_size;++i) if (rand()%5==0) {
		skill * sv = u->skills+i;
		int weeks = (sv->level * sv->level - sv->level) / 2;
		int change = (weeks+9) / 10;
		reduce_skill(u, sv, change);
	}

	/* Dies ist ein Hack, um das skillmod und familiar-Attribut beim Mage
	 * zu l�schen wenn der Familiar get�tet wird. Da sollten wir �ber eine
	 * saubere Implementation nachdenken. */

	if(!strcmp(reason, "trigger")) {
		remove_familiar(u);
	}

	ADDMSG(&u->faction->msgs, msg_message("shock",
		"mage reason", u, strdup(reason)));
}

/* ------------------------------------------------------------- */
/* Spruchanalyse - Ausgabe von curse->info und curse->name       */
/* ------------------------------------------------------------- */

static double
curse_chance(const struct curse * c, double force)
{
  return 1.0 + (force - c->vigour) * 0.1;
}

static void
magicanalyse_region(region *r, unit *mage, double force)
{
	attrib *a;
	boolean found = false;
	const struct locale * lang = mage->faction->locale;

	for (a=r->attribs;a;a=a->next) {
		curse * c = (curse*)a->data.v;
		double probability;
		int mon;

    if (!fval(a->type, ATF_CURSE)) continue;

		/* ist der curse schw�cher als der Analysezauber, so ergibt sich
		 * mehr als 100% probability und damit immer ein Erfolg. */
		probability = curse_chance(c, force);
		mon = c->duration + (rand()%10) - 5;
		mon = max(1, mon);
		found = true;

		if (chance(probability)) { /* Analyse gegl�ckt */
			if(c->flag & CURSE_NOAGE) {
				ADDMSG(&mage->faction->msgs, msg_message(
					"analyse_region_noage", "mage region curse",
					mage, r, LOC(lang, mkname("spell", c->type->cname))));
			} else {
				ADDMSG(&mage->faction->msgs, msg_message(
					"analyse_region_age", "mage region curse months",
					mage, r, LOC(lang, mkname("spell", c->type->cname)), mon));
			}
		} else {
			ADDMSG(&mage->faction->msgs, msg_message(
				"analyse_region_fail", "mage region", mage, r));
		}
	}
	if (!found) {
		ADDMSG(&mage->faction->msgs, msg_message(
			"analyse_region_nospell", "mage region", mage, r));
	}
}

static void
magicanalyse_unit(unit *u, unit *mage, double force)
{
	attrib *a;
	boolean found = false;
	const struct locale * lang = mage->faction->locale;

	for (a=u->attribs;a;a=a->next) {
		curse * c;
		double probability;
		int mon;
		if (!fval(a->type, ATF_CURSE)) continue;

		c = (curse*)a->data.v;
		/* ist der curse schw�cher als der Analysezauber, so ergibt sich
		 * mehr als 100% probability und damit immer ein Erfolg. */
		probability = curse_chance(c, force);
		mon = c->duration + (rand()%10) - 5;
		mon = max(1,mon);

		if (chance(probability)) { /* Analyse gegl�ckt */
			if(c->flag & CURSE_NOAGE){
				ADDMSG(&mage->faction->msgs, msg_message(
					"analyse_unit_noage", "mage unit curse",
					mage, u, LOC(lang, mkname("spell", c->type->cname))));
			}else{
				ADDMSG(&mage->faction->msgs, msg_message(
					"analyse_unit_age", "mage unit curse months",
					mage, u, LOC(lang, mkname("spell", c->type->cname)), mon));
			}
		} else {
			ADDMSG(&mage->faction->msgs, msg_message(
				"analyse_unit_fail", "mage unit", mage, u));
		}
	}
	if (!found) {
		ADDMSG(&mage->faction->msgs, msg_message(
			"analyse_unit_nospell", "mage target", mage, u));
	}
}

static void
magicanalyse_building(building *b, unit *mage, double force)
{
	attrib *a;
	boolean found = false;
	const struct locale * lang = mage->faction->locale;

	for (a=b->attribs;a;a=a->next) {
		curse * c;
		double probability;
		int mon;

    if (!fval(a->type, ATF_CURSE)) continue;

		c = (curse*)a->data.v;
		/* ist der curse schw�cher als der Analysezauber, so ergibt sich
		 * mehr als 100% probability und damit immer ein Erfolg. */
		probability = curse_chance(c, force);
		mon = c->duration + (rand()%10) - 5;
		mon = max(1,mon);

		if (chance(probability)) { /* Analyse gegl�ckt */
			if(c->flag & CURSE_NOAGE){
				ADDMSG(&mage->faction->msgs, msg_message(
					"analyse_building_age", "mage building curse",
					mage, b, LOC(lang, mkname("spell", c->type->cname))));
			}else{
				ADDMSG(&mage->faction->msgs, msg_message(
					"analyse_building_age", "mage building curse months",
					mage, b, LOC(lang, mkname("spell", c->type->cname)), mon));
			}
		} else {
			ADDMSG(&mage->faction->msgs, msg_message(
				"analyse_building_fail", "mage building", mage, b));
		}
	}
	if (!found) {
		ADDMSG(&mage->faction->msgs, msg_message(
			"analyse_building_nospell", "mage building", mage, b));
	}

}

static void
magicanalyse_ship(ship *sh, unit *mage, double force)
{
	attrib *a;
	boolean found = false;
	const struct locale * lang = mage->faction->locale;

	for (a=sh->attribs;a;a=a->next) {
		curse * c;
		double probability;
		int mon;
		if (!fval(a->type, ATF_CURSE)) continue;

		c = (curse*)a->data.v;
		/* ist der curse schw�cher als der Analysezauber, so ergibt sich
		 * mehr als 100% probability und damit immer ein Erfolg. */
		probability = curse_chance(c, force);
		mon = c->duration + (rand()%10) - 5;
		mon = max(1,mon);

		if (chance(probability)) { /* Analyse gegl�ckt */
			if(c->flag & CURSE_NOAGE){
				ADDMSG(&mage->faction->msgs, msg_message(
					"analyse_ship_noage", "mage ship curse",
					mage, sh, LOC(lang, mkname("spell", c->type->cname))));
			}else{
				ADDMSG(&mage->faction->msgs, msg_message(
					"analyse_ship_age", "mage ship curse months",
					mage, sh, LOC(lang, mkname("spell", c->type->cname)), mon));
			}
		} else {
			ADDMSG(&mage->faction->msgs, msg_message(
				"analyse_ship_fail", "mage ship", mage, sh));

		}
	}
	if (!found) {
		ADDMSG(&mage->faction->msgs, msg_message(
			"analyse_ship_nospell", "mage ship", mage, sh));
	}

}

/* ------------------------------------------------------------- */
/* Antimagie - curse aufl�sen */
/* ------------------------------------------------------------- */

/* Wenn der Curse schw�cher ist als der cast_level, dann wird er
 * aufgel�st, bzw seine Kraft (vigour) auf 0 gesetzt.
 * Ist der cast_level zu gering, hat die Antimagie nur mit einer Chance
 * von 100-20*Stufenunterschied % eine Wirkung auf den Curse. Dann wird
 * die Kraft des Curse um die halbe St�rke der Antimagie reduziert.
 * Zur�ckgegeben wird der noch unverbrauchte Rest von force.
 */
double
destr_curse(curse* c, int cast_level, double force)
{
	if (cast_level < c->vigour) { /* Zauber ist nicht stark genug */
		double probability = 0.1 + (cast_level - c->vigour)*0.2;
		/* pro Stufe Unterschied -20% */
		if (chance(probability)) {
			force -= c->vigour;
			if (c->type->change_vigour){
				c->type->change_vigour(c, -(cast_level+1/2));
			} else {
				c->vigour -= cast_level+1/2;
			}
		}
	} else { /* Zauber ist st�rker als curse */
		if (force >= c->vigour){ /* reicht die Kraft noch aus? */
			force -= c->vigour;
			if (c->type->change_vigour){
				c->type->change_vigour(c, -c->vigour);
			} else {
				c->vigour = 0;
			}

		}
	}
	return force;
}

int
destroy_curse(attrib **alist, int cast_level, double force, curse * c)
{
	int succ = 0;
/*	attrib **a = a_find(*ap, &at_curse); */
	attrib ** ap = alist;

	while (*ap && force > 0) {
		curse * c1;
		attrib * a = *ap;
		if (!fval(a->type, ATF_CURSE)) {
			do { ap = &(*ap)->next; } while (*ap && a->type==(*ap)->type);
			continue;
		}
		c1 = (curse*)a->data.v;

		/* Immunit�t pr�fen */
		if (c1->flag & CURSE_IMMUNE) {
			do { ap = &(*ap)->next; } while (*ap && a->type==(*ap)->type);
			continue;
		}

		/* Wenn kein spezieller cursetyp angegeben ist, soll die Antimagie
		 * auf alle Verzauberungen wirken. Ansonsten pr�fe, ob der Curse vom
		 * richtigen Typ ist. */
		if(!c || c==c1) {
			double remain = destr_curse(c1, cast_level, force);
			if (remain < force) {
				succ = cast_level;
				force = remain;
			}
			if (c1->vigour <= 0) {
				a_remove(alist, a);
			}
		}
		if (*ap==a) ap = &a->next;
	}
	return succ;
}

/* ------------------------------------------------------------- */
/* Report a spell's effect to the units in the region.
*/
static void
report_effect(region * r, unit * mage, message * seen, message * unseen)
{
  unit * u;

  /* melden, 1x pro Partei */
  freset(mage->faction, FL_DH);
  for (u = r->units; u; u = u->next ) freset(u->faction, FL_DH);
  for (u = r->units; u; u = u->next ) {
    if (!fval(u->faction, FL_DH) ) {
      fset(u->faction, FL_DH);

      /* Bei Fernzaubern sieht nur die eigene Partei den Magier */
      if (u->faction != mage->faction){
        if (r == mage->region){
          /* kein Fernzauber, pr�fe, ob der Magier �berhaupt gesehen
          * wird */
          if (cansee(u->faction, r, mage, 0)) {
            r_addmessage(r, u->faction, seen);
          } else {
            r_addmessage(r, u->faction, unseen);
          }
        } else { /* Fernzauber, fremde Partei sieht den Magier niemals */
          r_addmessage(r, u->faction, unseen);
        }
      } else { /* Partei des Magiers, sieht diesen immer */
        r_addmessage(r, u->faction, seen);
      }
    }
  }
  /* Ist niemand von der Partei des Magiers in der Region, dem Magier
  * nochmal gesondert melden */
  if (!fval(mage->faction, FL_DH)) {
    add_message(&mage->faction->msgs, seen);
  }
}

/* ------------------------------------------------------------- */
/* Die Spruchfunktionen */
/* ------------------------------------------------------------- */
/* Meldungen:
 *
 * Fehlermeldungen sollten als MSG_MAGIC, level ML_MISTAKE oder
 * ML_WARN ausgegeben werden. (stehen im Kopf der Auswertung unter
 * Zauberwirkungen)

	sprintf(buf, "%s in %s: 'ZAUBER %s': [hier die Fehlermeldung].",
		unitname(mage), regionname(mage->region, mage->faction), sa->strings[0]);
	add_message(0, mage->faction, buf,  MSG_MAGIC, ML_MISTAKE);

 * Allgemein sichtbare Auswirkungen in der Region sollten als
 * Regionsereignisse auch dort auftauchen.

	{
		message * seen = msg_message("harvest_effect", "mage", mage);
		message * unseen = msg_message("harvest_effect", "mage", NULL);
		report_effect(r, mage, seen, unseen);
	}

 * Meldungen an den Magier �ber Erfolg sollten, wenn sie nicht als
 * Regionsereigniss auftauchen, als MSG_MAGIC level ML_INFO unter
 * Zauberwirkungen gemeldet werden. Direkt dem Magier zuordnen (wie
 * Botschaft an Einheit) ist derzeit nicht m�glich.
 * ACHTUNG! r muss nicht die Region des Magier sein! (FARCASTING)
 *
 * Parameter:
 * die Struct castorder *co ist in magic.h deklariert
 * die Parameterliste spellparameter *pa = co->par steht dort auch.
 *
 */

/* ------------------------------------------------------------- */
/* Name:		Vertrauter
 * Stufe:		10
 *
 * Wirkung:
 * Der Magier beschw�rt einen Vertrauten, ein kleines Tier, welches
 * dem Magier zu Diensten ist.  Der Magier kann durch die Augen des
 * Vertrauten sehen, und durch den Vertrauten zaubern, allerdings nur
 * mit seiner halben Stufe. Je nach Vertrautem erh�lt der Magier
 * evtl diverse Skillmodifikationen.  Der Typ des Vertrauten ist
 * zuf�llig bestimmt, wird aber durch Magiegebiet und Rasse beeinflu�t.
 * "Tierische" Vertraute brauchen keinen Unterhalt.
 *
 * Ein paar M�glichkeiten:
 *			 Magieg.	Rasse	Besonderheiten
 * Eule		Tybied	-/-		fliegt, Auraregeneration
 * Rabe	 Ilaun	-/-		fliegt
 * Imp		Draig	-/-		Magieresistenz?
 * Fuchs	Gwyrrd	-/-		Wahrnehmung
 * ????		Cerddor	-/-		???? (Singvogel?, Papagei?)
 * Adler	-/-		-/-		fliegt, +Wahrnehmung, =^=Adlerauge-Spruch?
 * Kr�he	-/-		-/-		fliegt, +Tarnung (weil unauff�llig)
 * Delphin	-/-		Meerm.	schwimmt
 * Wolf		-/-		Ork
 * Hund		-/-		Mensch	kann evtl BEWACHE ausf�hren
 * Ratte	-/-		Goblin
 * Albatros	-/-		-/-		fliegt, kann auf Ozean "landen"
 * Affe		-/-		-/-		kann evtl BEKLAUE ausf�hren
 * Goblin	-/-		!Goblin	normale Einheit
 * Katze	-/-		!Katze	normale Einheit
 * D�mon	-/-		!D�mon	normale Einheit
 *
 * Spezielle V. f�r Katzen, Trolle, Elfen, D�monen, Insekten, Zwerge?
 */

static const race *
select_familiar(const race * magerace, magic_t magiegebiet)
{
  const race * retval = NULL;
	int rnd = rand()%100;
	assert(magerace->familiars[0]);

  do {
  	if (rnd < 3) {
      /* RC_KRAKEN mu� letzter Vertraute sein */
      int rc = RC_HOUSECAT + rand()%(RC_KRAKEN+1-RC_HOUSECAT);
      retval = new_race[rc];
  	} else if (rnd < 80) {
	  	retval = magerace->familiars[0];
	  }
	  retval = magerace->familiars[magiegebiet];
  }
  while (retval->init_familiar==NULL);
  return retval;
}

/* ------------------------------------------------------------- */
/* der Vertraue des Magiers */

boolean
is_familiar(const unit *u)
{
  attrib * a = a_find(u->attribs, &at_familiarmage);
	return i2b(a!=NULL);
}

static void
make_familiar(unit *familiar, unit *mage)
{
	/* skills and spells: */
	familiar->race->init_familiar(familiar);

	/* triggers: */
	create_newfamiliar(mage, familiar);

	/* Hitpoints nach Talenten korrigieren, sonst starten vertraute
	 * mit Ausdauerbonus verwundet */
	familiar->hp = unit_max_hp(familiar);
}

static int
sp_summon_familiar(castorder *co)
{
	unit *familiar;
	region *r = co->rt;
	region *target_region = co->rt;
	unit *mage = (unit *)co->magician;
	int cast_level = co->level;
	const race * rc;
	skill_t sk;
	int dh, dh1;
	direction_t d;
	if (get_familiar(mage) != NULL ) {
		cmistake(mage, co->order, 199, MSG_MAGIC);
		return 0;
	}
	rc = select_familiar(mage->faction->race, mage->faction->magiegebiet);

	if (fval(rc, RCF_SWIM) && !fval(rc, RCF_WALK)) {
		int coasts = is_coastregion(r);

		if (coasts == 0) {
			cmistake(mage, co->order, 229, MSG_MAGIC);
			return 0;
		}

		/* In welcher benachbarten Ozeanregion soll der Familiar erscheinen? */
		coasts = rand()%coasts;
		dh = -1;
		for(d=0; d<MAXDIRECTIONS; d++) {
			if(rconnect(r,d)->terrain == T_OCEAN) {
				dh++;
				if(dh == coasts) break;
			}
		}
		target_region = rconnect(r,d);
	}

	familiar = create_unit(target_region, mage->faction, 1, rc, 0, NULL, mage);
	if (target_region==mage->region) {
		familiar->building = mage->building;
		familiar->ship = mage->ship;
	}
	familiar->status = ST_FLEE;	/* flieht */
	sprintf(buf, "Vertrauter von %s", unitname(mage));
	set_string(&familiar->name, buf);
	if (fval(mage, UFL_PARTEITARNUNG)) fset(familiar, UFL_PARTEITARNUNG);
	fset(familiar, UFL_LOCKED);
	make_familiar(familiar, mage);

	dh = 0;
	dh1 = 0;
	sprintf(buf, "%s ruft einen Vertrauten. %s k�nnen ",
		unitname(mage), LOC(mage->faction->locale, rc_name(rc, 1)));
	for(sk=0;sk<MAXSKILLS;sk++){
		if(rc->bonus[sk] > -5) dh++;
	}
	for(sk=0;sk<MAXSKILLS;sk++) {
		if(rc->bonus[sk] > -5){
			dh--;
			if (dh1 == 0){
				dh1 = 1;
			} else {
				if (dh == 0){
					scat(" und ");
				} else {
					scat(", ");
				}
			}
			scat(skillname(sk, mage->faction->locale));
		}
	}
	scat(" lernen.");
	scat(" ");
	scat("Der Vertraute verleiht dem Magier einen Bonus auf jedes Talent ");
	scat("(ausgenommen Magie), welches der Vertraute beherrscht.");
	scat(" ");
	scat("Das spezielle Band zu seinem Vertrauten erm�glicht dem Magier ");
	scat("auch, Spr�che durch diesen zu wirken. So gezauberte Spr�che ");
	scat("wirken auf die Region des Vertrauten und brauchen keine Fernzauber ");
	scat("zu sein. Die maximale Entfernung daf�r entspricht dem Talent des ");
	scat("Magiers. Einen Spruch durch das Vertrautenband zu richten ist ");
	scat("jedoch gewissen Einschr�nkungen unterworfen. Die Stufe des Zaubers ");
	scat("kann nicht gr��er als das Magietalent des Vertrauten oder das halbe ");
	scat("Talent des Magiers sein. Auch verdoppeln sich die Kosten f�r den ");
	scat("Spruch. (Um einen Zauber durch den Vertrauten zu wirken, gibt ");
	scat("man statt dem Magier dem Vertrauten den Befehl ZAUBERE.)");

	addmessage(r, mage->faction, buf, MSG_MAGIC, ML_INFO);
	return cast_level;
}

/* ------------------------------------------------------------- */
/* Name:	   Zerst�re Magie
 * Wirkung:
 *  Zerst�rt alle Zauberwirkungen auf dem Objekt. Jeder gebrochene
 *  Zauber verbraucht c->vigour an Zauberkraft. Wird der Spruch auf
 *  einer geringeren Stufe gezaubert, als der Zielzauber an c->vigour
 *  hat, so schl�gt die Aufl�sung mit einer von der Differenz abh�ngigen
 *  Chance fehl.  Auch dann wird force verbraucht, der Zauber jedoch nur
 *  abgeschw�cht.
 *
 * Flag:
 *  (FARCASTING|SPELLLEVEL|ONSHIPCAST|TESTCANSEE)
 * */
static int
sp_destroy_magic(castorder *co)
{
	unit *mage = (unit *)co->magician;
	int cast_level = co->level;
	double force = co->force;
	spellparameter *pa = co->par;
	curse * c = NULL;
	char ts[80];
	attrib **ap;
	int obj;
	int succ;

	/* da jeder Zauber force verbraucht und der Zauber auf alles und nicht
	 * nur einen Spruch wirken soll, wird die Wirkung hier verst�rkt */
	force *= 4;

	/* Objekt ermitteln */
	obj = pa->param[0]->typ;

	switch(obj) {
		case SPP_REGION:
		{
			region *tr = pa->param[0]->data.r;
			ap = &tr->attribs;
			strcpy(ts, regionname(tr, mage->faction));
			break;
		}
    case SPP_TEMP:
		case SPP_UNIT:
		{
			unit *u;
			u = pa->param[0]->data.u;
			ap = &u->attribs;
			strcpy(ts, unitname(u));
			break;
		}
		case SPP_BUILDING:
		{
			building *b;
			b = pa->param[0]->data.b;
			ap = &b->attribs;
			strcpy(ts, buildingname(b));
			break;
		}
		case SPP_SHIP:
		{
			ship *sh;
			sh = pa->param[0]->data.sh;
			ap = &sh->attribs;
			strcpy(ts, shipname(sh));
			break;
		}
		default:
			return 0;
	}

  succ = destroy_curse(ap, cast_level, force, c);

	if (succ) {
		ADDMSG(&mage->faction->msgs, msg_message(
			"destroy_magic_effect", "unit region command succ target",
			mage, mage->region, co->order, succ, strdup(ts)));
	} else {
		ADDMSG(&mage->faction->msgs, msg_message(
			"destroy_magic_noeffect", "unit region command",
			mage, mage->region, co->order));
	}

	return max(succ, 1);
}

/* ------------------------------------------------------------- */
/* Name:	  Transferiere Aura
 * Stufe:	 variabel
 * Gebiet:	alle
 * Kategorie:     Einheit, positiv
 * Wirkung:
 *   Mit Hilfe dieses Zauber kann der Magier eigene Aura im Verh�ltnis
 *   2:1 auf einen anderen Magier des gleichen Magiegebietes oder (nur
 *   bei Tybied) im Verh�ltnis 3:1 auf einen Magier eines anderen
 *   Magiegebietes �bertragen.
 *
 * Syntax:
 *  "ZAUBERE <spruchname> <Einheit-Nr> <investierte Aura>"
 *  "ui"
 * Flags:
 *  (UNITSPELL|ONSHIPCAST|ONETARGET)
 * */

static int
sp_transferaura(castorder *co)
{
	int aura, gain, multi = 2;
	unit *mage = (unit *)co->magician;
	int cast_level = co->level;
	spellparameter *pa = co->par;
  unit * u;
  sc_mage * scm_dst, * scm_src = get_mage(mage);

	/* wenn kein Ziel gefunden, Zauber abbrechen */
	if (pa->param[0]->flag == TARGET_NOTFOUND) return 0;

	/* wenn Ziel gefunden, dieses aber Magieresistent war, Zauber
	 * abbrechen aber kosten lassen */
	if(pa->param[0]->flag == TARGET_RESISTS) return cast_level;

	/* Wieviel Transferieren? */
	aura = pa->param[1]->data.i;
  u = pa->param[0]->data.u;
  scm_dst = get_mage(u);

  if (scm_dst==NULL) {
    /* "Zu dieser Einheit kann ich keine Aura �bertragen." */
    cmistake(mage, co->order, 207, MSG_MAGIC);
    return 0;
  } else if (scm_src->magietyp==M_ASTRAL) {
		if (scm_src->magietyp != scm_dst->magietyp) multi = 3;
  } else if (scm_src->magietyp==M_GRAU) {
    if (scm_src->magietyp != scm_dst->magietyp) multi = 4;
  } else if (scm_dst->magietyp!=scm_src->magietyp) {
    /* "Zu dieser Einheit kann ich keine Aura �bertragen." */
    cmistake(mage, co->order, 207, MSG_MAGIC);
    return 0;
  }

  if (aura < multi) {
    /* "Auraangabe fehlerhaft." */
    cmistake(mage, co->order, 208, MSG_MAGIC);
    return 0;
  }

  gain = min(aura, scm_src->spellpoints) / multi;
  scm_src->spellpoints -= gain*multi;
	scm_dst->spellpoints += gain;

/*	sprintf(buf, "%s transferiert %d Aura auf %s", unitname(mage),
			gain, unitname(u)); */
	ADDMSG(&mage->faction->msgs, msg_message(
		"auratransfer_success", "unit target aura", mage, u, gain));
	return cast_level;
}

/* ------------------------------------------------------------- */
/* DRUIDE */
/* ------------------------------------------------------------- */
/* Name:       G�nstige Winde
 * Stufe:      4
 * Gebiet:     Gwyrrd
 * Wirkung:
 * Schiffsbewegung +1, kein Abtreiben.  H�lt (Stufe) Runden an.
 * Kombinierbar mit "Sturmwind" (das +1 wird dadurch aber nicht
 * verdoppelt), und "Luftschiff".
 *
 * Flags:
 * (SHIPSPELL|ONSHIPCAST|SPELLLEVEL|ONETARGET|TESTRESISTANCE)
 */

static int
sp_goodwinds(castorder *co)
{
	region *r = co->rt;
	unit *mage = (unit *)co->magician;
	int cast_level = co->level;
	double power = co->force;
	int duration = cast_level+1;
	spellparameter *pa = co->par;
	ship *sh;
	unit *u;

	/* wenn kein Ziel gefunden, Zauber abbrechen */
	if(pa->param[0]->flag == TARGET_NOTFOUND) return 0;

	sh = pa->param[0]->data.sh;

	/* keine Probleme mit C_SHIP_SPEEDUP und C_SHIP_FLYING */
	/* NODRIFT bewirkt auch +1 Geschwindigkeit */
	create_curse(mage, &sh->attribs, ct_find("nodrift"), power, duration, zero_effect, 0);

	/* melden, 1x pro Partei */
	freset(mage->faction, FL_DH);
	for(u = r->units; u; u = u->next ) freset(u->faction, FL_DH);
	for(u = r->units; u; u = u->next ) {
		if(u->ship != sh )		/* nur den Schiffsbesatzungen! */
			continue;
		if(!fval(u->faction, FL_DH) ) {
			message * m = msg_message("wind_effect", "mage ship", cansee(u->faction, r, mage, 0) ? mage:NULL, sh);
			r_addmessage(r, u->faction, m);
			msg_release(m);
			fset(u->faction, FL_DH);
		}
	}
	if (!fval(mage->faction, FL_DH)) {
		message * m = msg_message("wind_effect", "mage ship", mage, sh);
		r_addmessage(r, mage->faction, m);
		msg_release(m);
	}

	return cast_level;
}

/* ------------------------------------------------------------- */
/* Name:       Magischer Pfad
 * Stufe:      4
 * Gebiet:     Gwyrrd
 * Wirkung:
 *	 f�r Stufe Runden wird eine (magische) Strasse erzeugt, die wie eine
 *	 normale Strasse wirkt.
 *	 Im Ozean schl�gt der Spruch fehl
 *
 * Flags:
 * (FARCASTING|SPELLLEVEL|REGIONSPELL|ONSHIPCAST|TESTRESISTANCE)
 */
static int
sp_magicstreet(castorder *co)
{
  region *r = co->rt;
  unit *mage = (unit *)co->magician;

  if (!landregion(rterrain(r))) {
    cmistake(mage, co->order, 186, MSG_MAGIC);
    return 0;
  }

  /* wirkt schon in der Zauberrunde! */
  create_curse(mage, &r->attribs, ct_find("magicstreet"), co->force, co->level+1, zero_effect, 0);

  /* melden, 1x pro Partei */
  {
    message * seen = msg_message("path_effect", "mage region", mage, r);
    message * unseen = msg_message("path_effect", "mage region", NULL, r);
    report_effect(r, mage, seen, unseen);
    msg_release(seen);
    msg_release(unseen);
  }

  return co->level;
}

/* ------------------------------------------------------------- */
/* Name:       Erwecke Ents
 * Stufe:      10
 * Kategorie:  Beschw�rung, positiv
 * Gebiet:     Gwyrrd
 * Wirkung:
 *  Verwandelt (Stufe) B�ume in eine Gruppe von Ents, die sich f�r Stufe
 *  Runden der Partei des Druiden anschliessen und danach wieder zu
 *  B�umen werden
 * Patzer:
 *  Monster-Ents entstehen
 *
 * Flags:
 * (SPELLLEVEL)
 */
static int
sp_summonent(castorder *co)
{
	region *r = co->rt;
	unit *mage = (unit *)co->magician;
	int cast_level = co->level;
	double power = co->force;
	unit *u;
	attrib *a;
	int ents;

#if GROWING_TREES
	if (rtrees(r,2) == 0) {
#else
	if (rtrees(r) == 0) {
#endif
		cmistake(mage, co->order, 204, MSG_EVENT);
		/* nicht ohne b�ume */
    return 0;
	}

#if GROWING_TREES
	ents = (int)min(power*power, rtrees(r,2));
#else
	ents = (int)min(power*power, rtrees(r));
#endif

	u = create_unit(r, mage->faction, ents, new_race[RC_TREEMAN], 0, LOC(mage->faction->locale, rc_name(new_race[RC_TREEMAN], ents!=1)), mage);

	a = a_new(&at_unitdissolve);
	a->data.ca[0] = 2;	/* An r->trees. */
	a->data.ca[1] = 5;	/* 5% */
	a_add(&u->attribs, a);
	fset(u, UFL_LOCKED);

#if GROWING_TREES
	rsettrees(r, 2, rtrees(r,2) - ents);
#else
	rsettrees(r, rtrees(r) - ents);
#endif

	/* melden, 1x pro Partei */
	{
		message * seen = msg_message("ent_effect", "mage amount", mage, ents);
		message * unseen = msg_message("ent_effect", "mage amount", NULL, ents);
		report_effect(r, mage, seen, unseen);
    msg_release(seen);
    msg_release(unseen);
	}
	return cast_level;
}

/* ------------------------------------------------------------- */
/* Name:       Segne Steinkreis
 * Stufe:      11
 * Kategorie:  Artefakt
 * Gebiet:     Gwyrrd
 * Wirkung:
 *  Es werden zwei neue Geb�ude eingef�hrt: Steinkreis und Steinkreis
 *  (gesegnet). Ersteres kann man bauen, letzteres wird aus einem
 *  fertigen Steinkreis mittels des Zaubers erschaffen.
 *
 * Flags:
 * (BUILDINGSPELL | ONETARGET)
 *
 */
static int
sp_blessstonecircle(castorder *co)
{
	building *b;
	region *r = co->rt;
	unit *mage = (unit *)co->magician;
	int cast_level = co->level;
	spellparameter *p = co->par;

	/* wenn kein Ziel gefunden, Zauber abbrechen */
	if(p->param[0]->flag == TARGET_NOTFOUND) return 0;

	b = p->param[0]->data.b;

	if(b->type != bt_find("stonecircle")) {
		sprintf(buf, "%s ist kein Steinkreis.", buildingname(b));
		mistake(mage, co->order, buf, MSG_MAGIC);
		return 0;
	}

	if(b->size < b->type->maxsize) {
    sprintf(buf, "%s muss vor der Weihe fertiggestellt sein.", buildingname(b));
    mistake(mage, co->order, buf, MSG_MAGIC);
		return 0;
	}

	b->type = bt_find("blessedstonecircle");

	sprintf(buf, "%s weiht %s.", unitname(mage), buildingname(b));
	addmessage(r, 0, buf, MSG_MAGIC, ML_INFO);

	return cast_level;
}

/* ------------------------------------------------------------- */
/* Name:       Mahlstrom
 * Stufe:      15
 * Kategorie:  Region, negativ
 * Gebiet:     Gwyrrd
 * Wirkung:
 *  Erzeugt auf See einen Mahlstrom f�r Stufe-Wochen. Jedes Schiff, das
 *  durch den Mahlstrom segelt, nimmt 0-150% Schaden. (D.h.  es hat auch
 *  eine 1/3-Chance, ohne Federlesens zu sinken.  Der Mahlstrom sollte
 *  aus den Nachbarregionen sichtbar sein.
 *
 * Flags:
 * (OCEANCASTABLE | ONSHIPCAST | REGIONSPELL | TESTRESISTANCE)
 */
static int
sp_maelstrom(castorder *co)
{
	region *r = co->rt;
	unit *mage = (unit *)co->magician;
	int cast_level = co->level;
	curse * c;
	double power = co->force;
  variant effect;
	int duration = (int)power+1;

	if(rterrain(r) != T_OCEAN) {
		cmistake(mage, co->order, 205, MSG_MAGIC);
		/* nur auf ozean */
		return 0;
	}

	/* Attribut auf Region.
	 * Existiert schon ein curse, so wird dieser verst�rkt
	 * (Max(Dauer), Max(St�rke))*/
  effect.i = (int)power;
	c = create_curse(mage, &mage->attribs, ct_find("maelstrom"), power, duration, effect, 0);
	curse_setflag(c, CURSE_ISNEW);

	/* melden, 1x pro Partei */
	{
		message * seen = msg_message("maelstrom_effect", "mage", mage);
		message * unseen = msg_message("maelstrom_effect", "mage", NULL);
		report_effect(r, mage, seen, unseen);
    msg_release(seen);
    msg_release(unseen);
	}

	return cast_level;
}

/* ------------------------------------------------------------- */
/* Name:       Wurzeln der Magie
 * Stufe:      16
 * Kategorie:  Region, neutral
 * Gebiet:     Gwyrrd
 * Wirkung:
 *	 Wandelt einen Wald permanent in eine Mallornregion
 *
 * Flags:
 * (FARCASTING | REGIONSPELL | TESTRESISTANCE)
 */
static int
sp_mallorn(castorder *co)
{
	region *r = co->rt;
	int cast_level = co->level;
	unit *mage = (unit *)co->magician;

	if(!landregion(rterrain(r))) {
		cmistake(mage, co->order, 290, MSG_MAGIC);
		return 0;
	}
	if(fval(r, RF_MALLORN)) {
		cmistake(mage, co->order, 291, MSG_MAGIC);
		return 0;
	}

	/* half the trees will die */
#if GROWING_TREES
	rsettrees(r, 2, rtrees(r,2)/2);
	rsettrees(r, 1, rtrees(r,1)/2);
	rsettrees(r, 0, rtrees(r,0)/2);
#else
	rsettrees(r, rtrees(r)/2);
#endif
	fset(r, RF_MALLORN);

	/* melden, 1x pro Partei */
	{
		message * seen = msg_message("mallorn_effect", "mage", mage);
		message * unseen = msg_message("mallorn_effect", "mage", NULL);
		report_effect(r, mage, seen, unseen);
    msg_release(seen);
    msg_release(unseen);
	}

	return cast_level;
}

/* ------------------------------------------------------------- */
/* Name:       Segen der Erde
 * Stufe:      1
 * Kategorie:  Region, positiv
 * Gebiet:     Gwyrrd
 *
 * Wirkung:
 *  Alle Bauern verdienen Stufe-Wochen 1 Silber mehr.
 *
 * Flags:
 * (FARCASTING | SPELLLEVEL | ONSHIPCAST | REGIONSPELL)
 */
static int
sp_blessedharvest(castorder *co)
{
	region *r = co->rt;
	unit *mage = (unit *)co->magician;
	int cast_level = co->level;
	double power = co->force;
	int duration = (int)power+1;
  variant effect;

	/* Attribut auf Region.
	 * Existiert schon ein curse, so wird dieser verst�rkt
	 * (Max(Dauer), Max(St�rke))*/
  effect.i = 1;
	create_curse(mage,&r->attribs, ct_find("blessedharvest"), power, duration, effect, 0);
	{
		message * seen = msg_message("harvest_effect", "mage", mage);
		message * unseen = msg_message("harvest_effect", "mage", NULL);
		report_effect(r, mage, seen, unseen);
    msg_release(seen);
    msg_release(unseen);
	}

	return cast_level;
}

/* ------------------------------------------------------------- */
/* Name:       Hainzauber
 * Stufe:      2
 * Kategorie:  Region, positiv
 * Gebiet:     Gwyrrd
 * Syntax:     ZAUBER [REGION x y] [STUFE 2] "Hain"
 * Wirkung:
 *     Erschafft Stufe-10*Stufe Jungb�ume
 *
 * Flag:
 * (FARCASTING | SPELLLEVEL | REGIONSPELL | TESTRESISTANCE)
 */

static int
sp_hain(castorder *co)
{
	int trees;
	region *r = co->rt;
	unit *mage = (unit *)co->magician;
	int cast_level = co->level;
	double force = co->force;

	if(!r->land) {
		cmistake(mage, co->order, 296, MSG_MAGIC);
		return 0;
	}
	if (fval(r, RF_MALLORN)) {
		cmistake(mage, co->order, 92, MSG_MAGIC);
		return 0;
	}

	trees = lovar((int)(force * 10 * RESOURCE_QUANTITY)) + (int)force;
#if GROWING_TREES
	rsettrees(r, 1, rtrees(r,1) + trees);
#else
	rsettrees(r, rtrees(r) + trees);
#endif

	/* melden, 1x pro Partei */
	{
		message * seen = msg_message("growtree_effect", "mage amount", mage, trees);
		message * unseen = msg_message("growtree_effect", "mage amount", NULL, trees);
		report_effect(r, mage, seen, unseen);
    msg_release(seen);
    msg_release(unseen);
	}

	return cast_level;
}
/* ------------------------------------------------------------- */
/* Name:       Segne Mallornstecken - Mallorn Hainzauber
 * Stufe:      4
 * Kategorie:  Region, positiv
 * Gebiet:     Gwyrrd
 * Syntax:     ZAUBER [REGION x y] [STUFE 4] "Segne Mallornstecken"
 * Wirkung:
 *     Erschafft Stufe-10*Stufe Jungb�ume
 *
 * Flag:
 * (FARCASTING | SPELLLEVEL | REGIONSPELL | TESTRESISTANCE)
 */

static int
sp_mallornhain(castorder *co)
{
	int trees;
	region *r = co->rt;
	unit *mage = (unit *)co->magician;
	int cast_level = co->level;
	double force = co->force;

	if(!r->land) {
		cmistake(mage, co->order, 296, MSG_MAGIC);
		return 0;
	}
	if (!fval(r, RF_MALLORN)) {
		cmistake(mage, co->order, 91, MSG_MAGIC);
		return 0;
	}

	trees = lovar((int)(force * 10 * RESOURCE_QUANTITY)) + (int)force;
#if GROWING_TREES
	rsettrees(r, 1, rtrees(r,1) + trees);
#else
	rsettrees(r, rtrees(r) + trees);
#endif

	/* melden, 1x pro Partei */
	{
		message * seen = msg_message("growtree_effect", "mage amount", mage, trees);
		message * unseen = msg_message("growtree_effect", "mage amount", NULL, trees);
		report_effect(r, mage, seen, unseen);
    msg_release(seen);
    msg_release(unseen);
	}

	return cast_level;
}

void
patzer_ents(castorder *co)
{
	int ents;
	unit *u;
	region *r = co->rt;
	unit *mage = (unit *)co->magician;
	/* int cast_level = co->level; */
	double force = co->force;

	if(!r->land) {
		cmistake(mage, co->order, 296, MSG_MAGIC);
		return;
	}

	ents = (int)(force*10);
	u = create_unit(r, findfaction(MONSTER_FACTION), ents, new_race[RC_TREEMAN], 0,
		LOC(default_locale, rc_name(new_race[RC_TREEMAN], ents!=1)), NULL);

	/* 'Erfolg' melden */
	ADDMSG(&mage->faction->msgs, msg_message(
				"regionmagic_patzer", "unit region command", mage,
				mage->region, co->order));

	/* melden, 1x pro Partei */
	{
		message * unseen = msg_message("entrise", "region", r);
		report_effect(r, mage, unseen, unseen);
    msg_release(unseen);
	}
}

/* ------------------------------------------------------------- */
/* Name:       Rosthauch
 * Stufe:      3
 * Kategorie:  Einheit, negativ
 * Gebiet:     Gwyrrd
 * Wirkung:
 *  Zerst�rt zwischen Stufe und Stufe*10 Eisenwaffen
 *  Eisenwaffen: I_SWORD, I_GREATSWORD, I_AXE, I_HALBERD
 *
 * Flag:
 * (FARCASTING | SPELLLEVEL | UNITSPELL | TESTCANSEE | TESTRESISTANCE)
 */
/* Syntax: ZAUBER [REGION x y] [STUFE 2] "Rosthauch" 1111 2222 3333 */

static int
sp_rosthauch(castorder *co)
{
	unit *u;
	int ironweapon;
	int i, n;
	int success = 0;
	region *r = co->rt;
	unit *mage = (unit *)co->magician;
	int cast_level = co->level;
	double force = co->force;
	spellparameter *pa = co->par;

	force = rand()%((int)(force * 10)) + force;

	/* fuer jede Einheit */
	for (n = 0; n < pa->length; n++) {
          static const item_type * it_halberd = NULL;
          if (it_halberd==NULL) it_halberd = it_find("halberd");
          if (force<=0) break;

		if(pa->param[n]->flag == TARGET_RESISTS
				|| pa->param[n]->flag == TARGET_NOTFOUND)
			continue;

		u = pa->param[n]->data.u;

		/* Eisenwaffen: I_SWORD, I_GREATSWORD, I_AXE, I_HALBERD (50% Chance)*/
		ironweapon = 0;

		i = min(get_item(u, I_SWORD), (int)force);
		if (i > 0) {
			change_item(u, I_SWORD, -i);
			change_item(u, I_RUSTY_SWORD, i);
			force -= i;
			ironweapon += i;
		}
		i = min(get_item(u, I_GREATSWORD), (int)force);
		if (i > 0){
			change_item(u, I_GREATSWORD, -i);
			change_item(u, I_RUSTY_GREATSWORD, i);
			force -= i;
			ironweapon += i;
		}
		i = min(get_item(u, I_AXE), (int)force);
		if (i > 0){
			change_item(u, I_AXE, -i);
			change_item(u, I_RUSTY_AXE, i);
			force -= i;
			ironweapon += i;
		}
		i = min(i_get(u->items, it_halberd), (int)force);
		if (i > 0){
			if(rand()%100 < 50){
				i_change(&u->items, it_halberd, -i);
				change_item(u, I_RUSTY_HALBERD, i);
				force -= i;
				ironweapon += i;
			}
		}

		if (ironweapon) {
			/* {$mage mage} legt einen Rosthauch auf {target}. {amount} Waffen
			 * wurden vom Rost zerfressen */
			ADDMSG(&mage->faction->msgs, msg_message(
				"rust_effect", "mage target amount", mage, u, ironweapon));
			ADDMSG(&u->faction->msgs, msg_message(
				"rust_effect", "mage target amount",
				cansee(u->faction, r, mage, 0) ? mage:NULL, u, ironweapon));
			success += ironweapon;
		} else {
			/* {$mage mage} legt einen Rosthauch auf {target}, doch der
			 * Rosthauch fand keine Nahrung */
			ADDMSG(&mage->faction->msgs, msg_message(
				"rust_fail", "mage target", mage, u));
		}
	}
	/* in success stehen nun die insgesamt zerst�rten Waffen. Im
	 * ung�nstigsten Fall kann pro Stufe nur eine Waffe verzaubert werden,
	 * darum wird hier nur f�r alle F�lle in denen noch weniger Waffen
	 * betroffen wurden ein Kostennachlass gegeben */
	return min(success, cast_level);
}


/* ------------------------------------------------------------- */
/* Name:       K�lteschutz
 * Stufe:      3
 * Kategorie:  Einheit, positiv
 * Gebiet:     Gwyrrd
 *
 * Wirkung:
 *  sch�tzt ein bis mehrere Einheiten mit bis zu Stufe*10 Insekten vor
 *  den Auswirkungen der K�lte. Sie k�nnen Gletscher betreten und dort
 *  ganz normal alles machen. Die Wirkung h�lt Stufe Wochen an
 *  Insekten haben in Gletschern den selben Malus wie in Bergen. Zu
 *  lange drin, nicht mehr �ndern
 *
 * Flag:
 * (UNITSPELL | SPELLLEVEL | ONSHIPCAST | TESTCANSEE)
 */
/* Syntax: ZAUBER [STUFE n] "K�lteschutz" eh1 [eh2 [eh3 [...]]] */

static int
sp_kaelteschutz(castorder *co)
{
	unit *u;
	int n, i = 0;
	int men;
	region *r = co->rt;
	unit *mage = (unit *)co->magician;
	int cast_level = co->level;
	double force = co->force;
	int duration = max(cast_level, (int)force) + 1;
	spellparameter *pa = co->par;
  variant effect;

	force*=10;	/* 10 Personen pro Force-Punkt */

	/* f�r jede Einheit in der Kommandozeile */
	for (n = 0; n < pa->length; n++) {
		if (force < 1)
			break;

		if(pa->param[n]->flag == TARGET_RESISTS
				|| pa->param[n]->flag == TARGET_NOTFOUND)
			continue;

		u = pa->param[n]->data.u;

		if (force < u->number){
			men = (int)force;
		} else {
			men = u->number;
		}

    effect.i = 1;
		create_curse(mage, &u->attribs, ct_find("insectfur"), cast_level,
				duration, effect, men);

		force -= u->number;
		ADDMSG(&mage->faction->msgs, msg_message(
			"heat_effect", "mage target", mage, u));
		if (u->faction!=mage->faction) ADDMSG(&u->faction->msgs, msg_message(
			"heat_effect", "mage target",
			cansee(u->faction, r, mage, 0) ? mage:NULL, u));
		i = cast_level;
	}
	/* Erstattung? */
	return i;
}

/* ------------------------------------------------------------- */
/* Name:       Verw�nschung, Funkenregen, Naturfreund, ...
 * Stufe:      1
 * Kategorie:  Einheit, rein visuell
 * Gebiet:     Alle
 *
 * Wirkung:
 * Die Einheit wird von einem magischen Effekt heimgesucht, der in ihrer
 * Beschreibung auftaucht, aber nur visuellen Effekt hat.
 *
 * Flag:
 * (UNITSPELL | TESTCANSEE | SPELLLEVEL | ONETARGET)
 */
/* Syntax: ZAUBER "Funkenregen" eh1 */

static int
sp_sparkle(castorder *co)
{
	unit *u;
	unit *mage = (unit *)co->magician;
	int cast_level = co->level;
	spellparameter *pa = co->par;
	int duration = cast_level+1;
  variant effect;

	/* wenn kein Ziel gefunden, Zauber abbrechen */
	if(pa->param[0]->flag == TARGET_NOTFOUND) return 0;

	/* wenn Ziel gefunden, dieses aber Magieresistent war, Zauber
	 * abbrechen aber kosten lassen */
	if(pa->param[0]->flag == TARGET_RESISTS) return cast_level;

	u = pa->param[0]->data.u;
  effect.i = rand();
	create_curse(mage, &u->attribs, ct_find("sparkle"), cast_level,
			duration, effect, u->number);

	ADDMSG(&mage->faction->msgs, msg_message(
		"sparkle_effect", "mage target", mage, u));
	if (u->faction!=mage->faction)
		ADDMSG(&u->faction->msgs, msg_message(
		"sparkle_effect", "mage target", mage, u));

	return cast_level;
}

/* ------------------------------------------------------------- */
/* Name:       Eisengolem
 * Stufe:      2
 * Kategorie:  Beschw�rung, positiv
 * Gebiet:     Gwyrrd
 * Wirkung:
 *   Erschafft eine Einheit Eisengolems mit Stufe*8 Golems.  Jeder Golem
 *   hat jede Runde eine Chance von 15% zu Staub zu zerfallen.  Gibt man
 *   den Golems den Befehl 'mache Schwert/Bih�nder' oder 'mache
 *   Schild/Kettenhemd/Plattenpanzer', so werden pro Golem 5 Eisenbarren
 *   verbaut und der Golem l�st sich auf.
 *
 *   Golems sind zu langsam um wirklich im Kampf von Nutzen zu sein.
 *   Jedoch fangen sie eine Menge Schaden auf und sollten sie zuf�llig
 *   treffen, so ist der Schaden fast immer t�dlich.  (Eisengolem: HP
 *   50, AT 4, PA 2, R�stung 2(KH), 2d10+4 TP, Magieresistenz 0.25)
 *
 *   Golems nehmen nix an und geben nix.  Sie bewegen sich immer nur 1
 *   Region weit und ziehen aus Strassen keinen Nutzen.  Ein Golem wiegt
 *   soviel wie ein Stein. Kann nicht im Sumpf gezaubert werden
 *
 * Flag:
 *  (SPELLLEVEL)
 *
 * #define GOLEM_IRON   4
 */

static int
sp_create_irongolem(castorder *co)
{
	unit *u2;
	attrib *a;
	region *r = co->rt;
	unit *mage = (unit *)co->magician;
	int cast_level = co->level;
	double force = co->force;
  int number = lovar(force*8*RESOURCE_QUANTITY);
  if (number<1) number = 1;

  if (rterrain(r) == T_SWAMP){
		cmistake(mage, co->order, 188, MSG_MAGIC);
		return 0;
	}

	u2 = create_unit(r, mage->faction, number, new_race[RC_IRONGOLEM], 0,
		LOC(mage->faction->locale, rc_name(new_race[RC_IRONGOLEM], 1)), mage);

	set_level(u2, SK_ARMORER, 1);
	set_level(u2, SK_WEAPONSMITH, 1);

	a = a_new(&at_unitdissolve);
	a->data.ca[0] = 0;
	a->data.ca[1] = IRONGOLEM_CRUMBLE;
	a_add(&u2->attribs, a);

	ADDMSG(&mage->faction->msgs,
		msg_message("magiccreate_effect", "region command unit amount object",
		mage->region, co->order, mage, number,
		LOC(mage->faction->locale, rc_name(new_race[RC_IRONGOLEM], 1))));

	return cast_level;
}

/* ------------------------------------------------------------- */
/* Name:       Steingolem
 * Stufe:      1
 * Kategorie:  Beschw�rung, positiv
 * Gebiet:     Gwyrrd
 * Wirkung:
 *  Erschafft eine Einheit Steingolems mit Stufe*5 Golems. Jeder Golem
 *  hat jede Runde eine Chance von 10% zu Staub zu zerfallen.  Gibt man
 *  den Golems den Befehl 'mache Burg' oder 'mache Strasse', so werden
 *  pro Golem 10 Steine verbaut und der Golem l�st sich auf.
 *
 *  Golems sind zu langsam um wirklich im Kampf von Nutzen zu sein.
 *  Jedoch fangen sie eine Menge Schaden auf und sollten sie zuf�llig
 *  treffen, so ist der Schaden fast immer t�dlich.  (Steingolem: HP 60,
 *  AT 4, PA 2, R�stung 4(PP), 2d12+6 TP)
 *
 *  Golems nehmen nix an und geben nix. Sie bewegen sich immer nur 1
 *  Region weit und ziehen aus Strassen keinen Nutzen. Ein Golem wiegt
 *  soviel wie ein Stein.
 *
 *  Kann nicht im Sumpf gezaubert werden
 *
 * Flag:
 *  (SPELLLEVEL)
 *
 * #define GOLEM_STONE 4
 */
static int
sp_create_stonegolem(castorder *co)
{
	unit *u2;
	attrib *a;
	region *r = co->rt;
	unit *mage = (unit *)co->magician;
	int cast_level = co->level;
  int number = lovar(co->force*5*RESOURCE_QUANTITY);
  if (number<1) number = 1;

	if (rterrain(r) == T_SWAMP){
		cmistake(mage, co->order, 188, MSG_MAGIC);
		return 0;
	}

	u2 = create_unit(r, mage->faction, number, new_race[RC_STONEGOLEM], 0,
		LOC(mage->faction->locale, rc_name(new_race[RC_STONEGOLEM], 1)), mage);
	set_level(u2, SK_ROAD_BUILDING, 1);
	set_level(u2, SK_BUILDING, 1);

	a = a_new(&at_unitdissolve);
	a->data.ca[0] = 0;
	a->data.ca[1] = STONEGOLEM_CRUMBLE;
	a_add(&u2->attribs, a);

	ADDMSG(&mage->faction->msgs,
		msg_message("magiccreate_effect", "region command unit amount object",
		mage->region, co->order, mage, number,
		LOC(mage->faction->locale, rc_name(new_race[RC_STONEGOLEM], 1))));

	return cast_level;
}

/* ------------------------------------------------------------- */
/* Name:       Gro�e D�rre
 * Stufe:      17
 * Kategorie:  Region, negativ
 * Gebiet:     Gwyrrd
 *
 * Wirkung:
 *   50% alle Bauern, Pferde, B�ume sterben.
 *   Zu 25% terraform: Gletscher wird mit 50% zu Sumpf, sonst Ozean,
 *   Sumpf wird zu Steppe, Ebene zur Steppe, Steppe zur W�ste.
 * Besonderheiten:
 *  neuer Terraintyp Steppe:
 *  5000 Felder, 500 B�ume, Strasse: 250 Steine. Anlegen wie in Ebene
 *  m�glich
 *
 * Flags:
 * (FARCASTING | REGIONSPELL | TESTRESISTANCE)
 */

static void
destroy_all_roads(region *r)
{
	int i;

	for(i = 0; i < MAXDIRECTIONS; i++){
		rsetroad(r,(direction_t)i, 0);
	}
}

static int
sp_great_drought(castorder *co)
{
	building *b, *b2;
	unit *u;
	boolean terraform = false;
	region *r = co->rt;
	unit *mage = (unit *)co->magician;
	int cast_level = co->level;
	double force = co->force;
	int duration = 2;
  variant effect;

	if(rterrain(r) == T_OCEAN ) {
		cmistake(mage, co->order, 189, MSG_MAGIC);
		/* TODO: vielleicht einen netten Patzer hier? */
		return 0;
	}

  /* sterben */
	rsetpeasants(r, rpeasants(r)/2); /* evtl wuerfeln */
#if GROWING_TREES
	rsettrees(r, 2, rtrees(r,2)/2);
	rsettrees(r, 1, rtrees(r,1)/2);
	rsettrees(r, 0, rtrees(r,0)/2);
#else
	rsettrees(r, rtrees(r)/2);
#endif
	rsethorses(r, rhorses(r)/2);

	/* Arbeitslohn = 1/4 */
  effect.i = 4;
	create_curse(mage, &r->attribs, ct_find("drought"), force, duration, effect, 0);

  /* terraforming */
	if (rand() % 100 < 25){
		terraform = true;

		switch(rterrain(r)){
			case T_PLAIN:
			   rsetterrain(r, T_GRASSLAND);
				destroy_all_roads(r);
				break;

			case T_SWAMP:
			   rsetterrain(r, T_GRASSLAND);
				destroy_all_roads(r);
				break;

			case T_GRASSLAND:
				rsetterrain(r, T_DESERT);
				destroy_all_roads(r);
				break;

			case T_GLACIER:
#if NEW_RESOURCEGROWTH == 0
				rsetiron(r, 0);
				rsetlaen(r, -1);
#endif
				if (rand() % 100 < 50){
					rsetterrain(r, T_SWAMP);
					destroy_all_roads(r);
				} else {   /* Ozean */
					destroy_all_roads(r);
					rsetterrain(r, T_OCEAN);
					/* Einheiten d�rfen hier auf keinen Fall gel�scht werden! */
					for (u = r->units; u; u = u->next) {
						if (u->race != new_race[RC_SPELL] && u->ship == 0) {
							set_number(u, 0);
						}
					}
					for (b = r->buildings; b;){
						b2 = b->next;
						destroy_building(b);
						b = b2;
					}
				}
				break;

			default:
				terraform = false;
				break;
		}
	}

	/* melden, 1x pro partei */
	for (u = r->units; u; u = u->next) freset(u->faction, FL_DH);

	for (u = r->units; u; u = u->next) {
		if (!fval(u->faction, FL_DH)) {
			fset(u->faction, FL_DH);
			sprintf(buf, "%s ruft das Feuer der Sonne auf %s hinab.",
					cansee(u->faction, r, mage, 0)? unitname(mage) : "Jemand",
					regionname(r, u->faction));
			if (rterrain(r) != T_OCEAN){
				if(rterrain(r) == T_SWAMP && terraform){
						scat(" Eis schmilzt und verwandelt sich in Morast. Rei�ende "
								"Str�me sp�len die mageren Felder weg und ers�ufen "
								"Mensch und Tier. Was an Bauten nicht den Fluten zum Opfer "
								"fiel, verschlingt der Morast. Die sengende Hitze ver�ndert "
								"die Region f�r immer.");
				} else {
					scat(" Die Felder verdorren und Pferde verdursten. Die Hungersnot "
							"kostet vielen Bauern das Leben. Vertrocknete B�ume recken "
							"ihre kahlen Zweige in den blauen Himmel, von dem "
							"erbarmungslos die sengende Sonne brennt.");
					if(terraform){
						scat(" Die D�rre ver�nderte die Region f�r immer.");
					}
				}
				addmessage(r, u->faction, buf, MSG_EVENT, ML_WARN);
			} else { /* ist Ozean */
				scat(" Das Eis zerbricht und eine gewaltige Flutwelle verschlingt"
						"die Region.");
				/* es kann gut sein, das in der Region niemand �berlebt, also
				 * besser eine Globalmeldung */
				addmessage(0, u->faction, buf, MSG_EVENT, ML_IMPORTANT);
			}
		}
	}
	if (!fval(mage->faction, FL_DH)){
		ADDMSG(&mage->faction->msgs, msg_message(
			"drought_effect", "mage region", mage, r));
	}
	return cast_level;
}

/* ------------------------------------------------------------- */
/* Name:       'Weg der B�ume'
 * Stufe:      9
 * Kategorie:  Teleport
 * Gebiet:     Gwyrrd
 * Wirkung:
 *  Der Druide kann 5*Stufe GE in die astrale Ebene schicken.
 *  Der Druide wird nicht mitteleportiert, es sei denn, er gibt sich
 *  selbst mit an.
 *  Der Zauber funktioniert nur in W�ldern.
 *
 * Syntax: Zauber "Weg der B�ume" <Einheit> ...
 *
 * Flags:
 * (UNITSPELL | SPELLLEVEL | TESTCANSEE)
 */
static int
sp_treewalkenter(castorder *co)
{
  region *r = co->rt;
  unit *mage = (unit *)co->magician;
  spellparameter *pa = co->par;
  double power = co->force;
  int cast_level = co->level;
  region *rt;
  int remaining_cap;
  int n;
  int erfolg = 0;

  if (getplane(r) != 0) {
    cmistake(mage, co->order, 190, MSG_MAGIC);
    return 0;
  }

  if (!r_isforest(r)) {
    cmistake(mage, co->order, 191, MSG_MAGIC);
    return 0;
  }

  rt = r_standard_to_astral(r);
  if (rt==NULL || is_cursed(rt->attribs, C_ASTRALBLOCK, 0)) {
    cmistake(mage, co->order, 192, MSG_MAGIC);
    return 0;
  }

  remaining_cap = (int)(power * 500);

  /* fuer jede Einheit */
  for (n = 0; n < pa->length; n++) {
    unit * u = pa->param[n]->data.u;
    spllprm * param = pa->param[n];

    if (param->flag & (TARGET_RESISTS|TARGET_NOTFOUND)) {
      continue;
    }

    if (!ucontact(u, mage)) {
      cmistake(mage, co->order, 73, MSG_MAGIC);
    } else {
      int w;
      
      if (!can_survive(u, rt)) {
        cmistake(mage, co->order, 231, MSG_MAGIC);
        continue;
      }

      w = weight(u);
      if (remaining_cap - w < 0) {
        ADDMSG(&mage->faction->msgs, msg_message("fail_tooheavy", 
          "command region unit target", co->order, r, mage, u));
        continue;
      }
      remaining_cap = remaining_cap - w;
      move_unit(u, rt, NULL);
      erfolg = cast_level;

      /* Meldungen in der Ausgangsregion */
      ADDMSG(&r->msgs, msg_message("astral_disappear", "unit", u));

      /* Meldungen in der Zielregion */
      ADDMSG(&rt->msgs, msg_message("astral_appear", "unit", u));
    }
  }
  return erfolg;
}

/* ------------------------------------------------------------- */
/* Name:       'Sog des Lebens'
 * Stufe:      9
 * Kategorie:  Teleport
 * Gebiet:     Gwyrrd
 * Wirkung:
 *  Der Druide kann 5*Stufe GE aus die astrale Ebene schicken.  Der
 *  Druide wird nicht mitteleportiert, es sei denn, er gibt sich selbst
 *  mit an.
 *  Der Zauber funktioniert nur, wenn die Zielregion ein Wald ist.
 *
 * Syntax: Zauber "Sog des Lebens" <Ziel-X> <Ziel-Y> <Einheit> ...
 *
 * Flags:
 * (UNITSPELL|SPELLLEVEL)
 */
static int
sp_treewalkexit(castorder *co)
{
  region *rt;
  region_list *rl, *rl2;
  int tax, tay;
  unit *u, *u2;
  int remaining_cap;
  int n;
  int erfolg = 0;
  region *r = co->rt;
  unit *mage = (unit *)co->magician;
  double power = co->force;
  spellparameter *pa = co->par;
  int cast_level = co->level;

  if(getplane(r) != get_astralplane()) {
    cmistake(mage, co->order, 193, MSG_MAGIC);
    return 0;
  }
  if(is_cursed(r->attribs, C_ASTRALBLOCK, 0)) {
    cmistake(mage, co->order, 192, MSG_MAGIC);
    return 0;
  }

  remaining_cap = (int)(power * 500);

  if(pa->param[0]->typ != SPP_REGION){
    report_failure(mage, co->order);
    return 0;
  }

  /* Koordinaten setzen und Region l�schen f�r �berpr�fung auf
  * G�ltigkeit */
  rt  = pa->param[0]->data.r;
  tax = rt->x;
  tay = rt->y;
  rt  = NULL;

  rl  = astralregions(r, inhabitable);
  rt  = 0;

  rl2 = rl;
  while(rl2) {
    if(rl2->data->x == tax && rl2->data->y == tay) {
      rt = rl2->data;
      break;
    }
    rl2 = rl2->next;
  }
  free_regionlist(rl);

  if(!rt) {
    cmistake(mage, co->order, 195, MSG_MAGIC);
    return 0;
  }

  if (!r_isforest(rt)) {
    cmistake(mage, co->order, 196, MSG_MAGIC);
    return 0;
  }

  /* f�r jede Einheit in der Kommandozeile */
  for (n = 1; n < pa->length; n++) {
    if(pa->param[n]->flag == TARGET_RESISTS
      || pa->param[n]->flag == TARGET_NOTFOUND)
      continue;

    u = pa->param[n]->data.u;

    if (!ucontact(u, mage)) {
      sprintf(buf, "%s hat uns nicht kontaktiert.", unitname(u));
      addmessage(r, mage->faction, buf,  MSG_MAGIC, ML_MISTAKE);
    } else {
      int w = weight(u);
      if (!can_survive(u, rt)) {
        cmistake(mage, co->order, 231, MSG_MAGIC);
      } else if(remaining_cap - w < 0) {
        sprintf(buf, "%s ist zu schwer.", unitname(u));
        addmessage(r, mage->faction, buf,  MSG_MAGIC, ML_MISTAKE);
      } else {
        remaining_cap = remaining_cap - w;
        move_unit(u, rt, NULL);
        erfolg = cast_level;

        /* Meldungen in der Ausgangsregion */

        for (u2 = r->units; u2; u2 = u2->next) freset(u2->faction, FL_DH);
        for(u2 = r->units; u2; u2 = u2->next ) {
          if(!fval(u2->faction, FL_DH)) {
            fset(u2->faction, FL_DH);
            if(cansee(u2->faction, r, u, 0)) {
              sprintf(buf, "%s wird durchscheinend und verschwindet.",
                unitname(u));
              addmessage(r, u2->faction, buf, MSG_EVENT, ML_INFO);
            }
          }
        }

        /* Meldungen in der Zielregion */

        for (u2 = r->units; u2; u2 = u2->next) freset(u2->faction, FL_DH);
        for(u2 = rt->units; u2; u2 = u2->next ) {
          if(!fval(u2->faction, FL_DH)) {
            fset(u2->faction, FL_DH);
            if(cansee(u2->faction, rt, u, 0)) {
              sprintf(buf, "%s erscheint pl�tzlich.", unitname(u));
              addmessage(rt, u2->faction, buf, MSG_EVENT, ML_INFO);
            }
          }
        }
      }
    }
  }
  return erfolg;
}

void
creation_message(unit * mage, item_t i)
{
	region * r = mage->region;
	sprintf(buf, "%s erschafft ein %s.", unitname(mage),
			locale_string(mage->faction->locale, resourcename(olditemtype[i]->rtype, 0)));
	addmessage(r, mage->faction, buf, MSG_MAGIC, ML_INFO);
}

static int
sp_create_sack_of_conservation(castorder *co)
{
	unit *mage = (unit *)co->magician;
	int cast_level = co->level;

	change_item(mage,I_SACK_OF_CONSERVATION,1);

	creation_message(mage, I_SACK_OF_CONSERVATION);

	return cast_level;
}

/* ------------------------------------------------------------- */
/* Name:		   Heiliger Boden
 * Stufe:		   9
 * Kategorie:  perm. Regionszauber
 * Gebiet:     Gwyrrd
 * Wirkung:
 *   Es entstehen keine Untoten mehr, Untote betreten die Region
 *   nicht mehr.
 *
 * ZAUBER "Heiliger Boden"
 * Flags: (0)
 */
static int
sp_holyground(castorder *co)
{
	region *r = co->rt;
	unit *mage = (unit *)co->magician;
	int cast_level = co->level;
	double power = co->force;
	curse * c;
	message * msg = r_addmessage(r, mage->faction, msg_message("holyground", "mage", mage));
	msg_release(msg);

	c = create_curse(mage, &r->attribs, ct_find("holyground"),
		power*power, 1, zero_effect, 0);

	curse_setflag(c, CURSE_NOAGE);

	a_removeall(&r->attribs, &at_deathcount);

	return cast_level;
}

/* ------------------------------------------------------------- */
/* Name:		   Heimstein
 * Stufe:		   7
 * Kategorie:  Artefakt
 * Gebiet:     Gwyrrd
 * Wirkung:
 * Die Burg kann nicht mehr durch Donnerbeben oder andere
 * Geb�udezerst�renden Spr�che kaputt gemacht werden. Auch
 * sch�tzt der Zauber vor Belagerungskatapulten.
 *
 * ZAUBER Heimstein
 * Flags: (0)
 */
static int
sp_homestone(castorder *co)
{
	unit *u;
	curse * c;
	region *r = co->rt;
	unit *mage = (unit *)co->magician;
	int cast_level = co->level;
	double force = co->force;
  variant effect;

	if(!mage->building || mage->building->type != bt_find("castle")){
		cmistake(mage, co->order, 197, MSG_MAGIC);
		return 0;
	}

	c = create_curse(mage, &mage->building->attribs, ct_find("magicwalls"),
			force*force, 1, zero_effect, 0);

	if (c==NULL) {
		cmistake(mage, co->order, 206, MSG_MAGIC);
		return 0;
	}
	curse_setflag(c, CURSE_NOAGE|CURSE_ONLYONE);

	/* Magieresistenz der Burg erh�ht sich um 50% */
  effect.i = 50;
	c = create_curse(mage,  &mage->building->attribs,
		ct_find("magicresistance"), force*force, 1, effect, 0);
	curse_setflag(c, CURSE_NOAGE);

	/* melden, 1x pro Partei in der Burg */
	for (u = r->units; u; u = u->next) freset(u->faction, FL_DH);
	for (u = r->units; u; u = u->next) {
		if (!fval(u->faction, FL_DH)) {
			fset(u->faction, FL_DH);
			if (u->building ==  mage->building) {
				sprintf(buf, "Mit einem Ritual bindet %s die magischen Kr�fte "
						"der Erde in die Mauern von %s", unitname(mage),
						buildingname(mage->building));
				addmessage(r, u->faction, buf, MSG_EVENT, ML_INFO);
			}
		}
	}
	return cast_level;
}



/* ------------------------------------------------------------- */
/* Name:       D�rre
 * Stufe:      13
 * Kategorie:  Region, negativ
 * Gebiet:     Gwyrrd
 * Wirkung:
 *  tempor�r ver�ndert sich das Baummaximum und die maximalen Felder in
 *  einer Region auf die H�lfte des normalen.
 *  Die H�lfte der B�ume verdorren und Pferde verdursten.
 *  Arbeiten bringt nur noch 1/4 des normalen Verdienstes
 *
 * Flags:
 * (FARCASTING|REGIONSPELL|TESTRESISTANCE),
 */
static int
sp_drought(castorder *co)
{
	curse *c;
	unit *u;
	region *r = co->rt;
	unit *mage = (unit *)co->magician;
	int cast_level = co->level;
	double power = co->force;
	int duration = (int)power+1;

	if(rterrain(r) == T_OCEAN ) {
		cmistake(mage, co->order, 189, MSG_MAGIC);
		/* TODO: vielleicht einen netten Patzer hier? */
		return 0;
	}

	/* melden, 1x pro Partei */
	for (u = r->units; u; u = u->next) freset(u->faction, FL_DH);
	for(u = r->units; u; u = u->next ) {
		if(!fval(u->faction, FL_DH) ) {
			fset(u->faction, FL_DH);
			sprintf(buf, "%s verflucht das Land, und eine D�rreperiode beginnt.",
					cansee(u->faction, r, mage, 0) ? unitname(mage) : "Jemand");
			addmessage(r, u->faction, buf, MSG_EVENT, ML_INFO);
		}
	}
	if(!fval(mage->faction, FL_DH)){
		sprintf(buf, "%s verflucht das Land, und eine D�rreperiode beginnt.",
				unitname(mage));
	  addmessage(0, mage->faction, buf, MSG_MAGIC, ML_INFO);
	}

	/* Wenn schon Duerre herrscht, dann setzen wir nur den Power-Level
	 * hoch (evtl dauert dann die Duerre laenger).  Ansonsten volle
	 * Auswirkungen.
	 */
	c = get_curse(r->attribs, ct_find("drought"));
	if (c) {
		c->vigour = max(c->vigour, power);
		c->duration = max(c->duration, (int)power);
	} else {
    variant effect;
		/* Baeume und Pferde sterben */
#if GROWING_TREES
		rsettrees(r, 2, rtrees(r,2)/2);
		rsettrees(r, 1, rtrees(r,1)/2);
		rsettrees(r, 0, rtrees(r,0)/2);
#else
		rsettrees(r, rtrees(r)/2);
#endif
		rsethorses(r, rhorses(r)/2);

    effect.i = 4;
		create_curse(mage, &r->attribs, ct_find("drought"), power, duration, effect, 0);
	}
	return cast_level;
}

/* ------------------------------------------------------------- */
/* Name:       Nebel der Verwirrung
 * Stufe:      14
 * Kategorie:  Region, negativ
 * Gebiet:     Gwyrrd
 * Wirkung:
 *  Alle Regionen innerhalb eines Radius von ((Stufe-15)/2 aufgerundet)
 *  werden von einem verwirrenden Nebel bedeckt.  Innerhalb des Nebels
 *  k�nnen keine Himmelsrichtungen mehr erkannt werden, alle Bewegungen
 *  erfolgen in eine zuf�llige Richtung.
 *  Die Gwyrrd-Variante wirkt nur auf W�lder und Ozeanregionen
 * Flags:
 *  (FARCASTING | SPELLLEVEL)
 * */
static int
sp_fog_of_confusion(castorder *co)
{
	unit *u;
	region *r = co->rt;
	unit *mage = (unit *)co->magician;
	int cast_level = co->level;
	double power = co->force;
	double range;
	int duration;
	region_list *rl,*rl2;

	range = (power-11)/3-1;
	duration = (int)((power-11)/1.5)+1;

	rl = all_in_range(r, (short)range, NULL);

	for(rl2 = rl; rl2; rl2 = rl2->next) {
		curse * c;
    variant effect;

    if(rterrain(rl2->data) != T_OCEAN
				&& !r_isforest(rl2->data)) continue;

		/* Magieresistenz jeder Region pr�fen */
		if (target_resists_magic(mage, r, TYP_REGION, 0)){
			report_failure(mage, co->order);
			continue;
		}

    effect.i = cast_level*5;
		c = create_curse(mage, &rl2->data->attribs,
			ct_find("disorientationzone"), power, duration, effect, 0);
		/* Soll der schon in der Zauberrunde wirken? */
		curse_setflag(c, CURSE_ISNEW);

		for (u = rl2->data->units; u; u = u->next) freset(u->faction, FL_DH);
		for (u = rl2->data->units; u; u = u->next ) {
			if(!fval(u->faction, FL_DH) ) {
				fset(u->faction, FL_DH);
				sprintf(buf, "%s beschw�rt einen Schleier der Verwirrung.",
						cansee(u->faction, r, mage, 0) ? unitname(mage) : "Jemand");
				addmessage(rl2->data, u->faction, buf, MSG_EVENT, ML_INFO);
			}
		}
		if(!fval(mage->faction, FL_DH)){
			sprintf(buf, "%s beschw�rt einen Schleier der Verwirrung.",
					unitname(mage));
		  addmessage(0, mage->faction, buf, MSG_MAGIC, ML_INFO);
		}
	}
	free_regionlist(rl);
	return cast_level;
}

/* ------------------------------------------------------------- */
/* Name:       Bergw�chter
 * Stufe:      9
 * Gebiet:     Gwyrrd
 * Kategorie:  Beschw�rung, negativ
 *
 * Wirkung:
 * Erschafft in Bergen oder Gletschern einen W�chter, der durch bewachen
 * den Eisen/Laen-Abbau f�r nicht-Allierte verhindert.  Bergw�chter
 * verhindern auch Abbau durch getarnte/unsichtbare Einheiten und lassen
 * sich auch durch Belagerungen nicht aufhalten.
 *
 * (Ansonsten in economic.c:manufacture() entsprechend anpassen).
 *
 * F�higkeiten (factypes.c): 50% Magieresistenz, 25 HP, 4d4 Schaden,
 *	 4 R�stung (=PP)
 * Flags:
 * (FARCASTING | SPELLLEVEL | REGIONSPELL | TESTRESISTANCE)
 */
static int
sp_ironkeeper(castorder *co)
{
	unit *keeper;
	region *r = co->rt;
	unit *mage = (unit *)co->magician;
	int cast_level = co->level;

  if (rterrain(r) != T_MOUNTAIN && rterrain(r) != T_GLACIER) {
    report_failure(mage, co->order);
    return 0;
  }

	keeper = create_unit(r, mage->faction, 1, new_race[RC_IRONKEEPER], 0, "Bergw�chter", mage);

	/*keeper->age = cast_level + 2;*/
	guard(keeper, GUARD_MINING);
	fset(keeper, UFL_ISNEW);
	keeper->status = ST_AVOID;	/* kaempft nicht */
	/* Parteitarnen, damit man nicht sofort wei�, wer dahinter steckt */
	fset(keeper, UFL_PARTEITARNUNG);
	{
		trigger * tkill = trigger_killunit(keeper);
		add_trigger(&keeper->attribs, "timer", trigger_timeout(cast_level+2, tkill));
	}

	sprintf(buf, "%s beschw�rt einen Bergw�chter.", unitname(mage));
	addmessage(r, mage->faction, buf, MSG_MAGIC, ML_INFO);

	return cast_level;
}

/* ------------------------------------------------------------- */
/* Name:       Sturmwind - Beschw�re einen Sturmelementar
 * Stufe:      6
 * Gebiet:     Gwyrrd
 *
 * Wirkung:
 * Verdoppelt Geschwindigkeit aller angegebener Schiffe fuer diese
 * Runde.  Kombinierbar mit "G�nstige Winde", aber nicht mit
 * "Luftschiff".
 *
 * Anstelle des alten ship->enchanted benutzen wir einen kurzfristigen
 * Curse.  Das ist zwar ein wenig aufwendiger, aber weitaus flexibler
 * und erlaubt es zB, die Dauer sp�ter problemlos zu ver�ndern.
 *
 * Flags:
 *  (SHIPSPELL|ONSHIPCAST|OCEANCASTABLE|TESTRESISTANCE)
 */

static int
sp_stormwinds(castorder *co)
{
	faction *f;
	ship *sh;
	unit *u;
	int erfolg = 0;
	region *r = co->rt;
	unit *mage = (unit *)co->magician;
	double power = co->force;
	spellparameter *pa = co->par;
	int n, force = (int)power;

	/* melden vorbereiten */
	for(f = factions; f; f = f->next ) freset(f, FL_DH);

	for (n = 0; n < pa->length; n++) {
		if (force<=0) break;

		if(pa->param[n]->flag == TARGET_RESISTS
				|| pa->param[n]->flag == TARGET_NOTFOUND)
			continue;

		sh = pa->param[n]->data.sh;

		/* mit C_SHIP_NODRIFT haben wir kein Problem */
		if(is_cursed(sh->attribs, C_SHIP_FLYING, 0) ) {
			sprintf(buf, "Es ist zu gef�hrlich, diesen Zauber auf ein "
					"fliegendes Schiff zu legen.");
			addmessage(r, mage->faction, buf, MSG_MAGIC, ML_MISTAKE);
			continue;
		}
		if(is_cursed(sh->attribs, C_SHIP_SPEEDUP, 0) ) {
			sprintf(buf, "Auf %s befindet sich bereits ein Zauber", shipname(sh));
			addmessage(r, mage->faction, buf, MSG_MAGIC, ML_MISTAKE);
			continue;
		}

		/* Duration = 1, nur diese Runde */
		create_curse(mage, &sh->attribs, ct_find("stormwind"), power, 1, zero_effect, 0);
		/* Da der Spruch nur diese Runde wirkt wird er nie im Report
		 * erscheinen */
		erfolg++;
		force--;

		/* melden vorbereiten: */
		for(u = r->units; u; u = u->next ) {
			if(u->ship != sh )		/* nur den Schiffsbesatzungen! */
				continue;

			fset(u->faction, FL_DH);
		}

	}
	/* melden, 1x pro Partei auf Schiff und f�r den Magier */
	fset(mage->faction, FL_DH);
	for(u = r->units; u; u = u->next ) {
		if(fval(u->faction, FL_DH)) {
			freset(u->faction, FL_DH);
			if (erfolg > 0){
				sprintf(buf, "%s beschw�rt einen magischen Wind, der die Schiffe "
						"�ber das Wasser treibt.",
						cansee(u->faction, r, mage, 0) ? unitname(mage) : "Jemand");
				addmessage(r, u->faction, buf, MSG_EVENT, ML_INFO);
			}
		}
	}
	return erfolg;
}


/* ------------------------------------------------------------- */
/* Name:       Donnerbeben
 * Stufe:      6
 * Gebiet:     Gwyrrd
 *
 * Wirkung:
 * Zerst�rt Stufe*10 "Steineinheiten" aller Geb�ude der Region, aber nie
 * mehr als 25% des gesamten Geb�udes (aber nat�rlich mindestens ein
 * Stein).
 *
 * Flags:
 *  (FARCASTING|REGIONSPELL|TESTRESISTANCE)
 */
static int
sp_earthquake(castorder *co)
{
	int kaputt;
	building *burg;
	unit *u;
	region *r = co->rt;
	unit *mage = (unit *)co->magician;
	int cast_level = co->level;

	for (burg = r->buildings; burg; burg = burg->next){
		if(burg->size == 0 )
			continue;

		/* Schutzzauber */
		if(is_cursed(burg->attribs, C_MAGICWALLS, 0))
			continue;

		/* Magieresistenz */
		if (target_resists_magic(mage, burg, TYP_BUILDING, 0))
			continue;

		kaputt = min(10 * cast_level, burg->size / 4);
		kaputt = max(kaputt, 1);
		burg->size -= kaputt;
		if(burg->size == 0 ) {
			/* alle Einheiten hinausbef�rdern */
			for(u = r->units; u; u = u->next ) {
				if(u->building == burg ) {
					u->building = 0;
					freset(u, UFL_OWNER);
				}
			}
			/* TODO: sollten die Insassen nicht Schaden nehmen? */
			destroy_building(burg);
		}
	}

	/* melden, 1x pro Partei */
	for (u = r->units; u; u = u->next) freset(u->faction, FL_DH);
	for (u = r->units; u; u = u->next ) {
		if(!fval(u->faction, FL_DH) ) {
			fset(u->faction, FL_DH);
			sprintf(buf, "%s l��t die Erde in %s erzittern.",
					cansee(u->faction, r, mage, 0) ? unitname(mage) : "Jemand",
					regionname(r, u->faction));

			addmessage(r, u->faction, buf, MSG_EVENT, ML_INFO);
		}
	}
	return cast_level;
}


/* ------------------------------------------------------------- */
/* CHAOS / M_CHAOS / Draig */
/* ------------------------------------------------------------- */
void
patzer_peasantmob(castorder *co)
{
	int anteil = 6, n;
	unit *u;
	attrib *a;
	region *r;
	unit *mage = (unit *)co->magician;

	if (mage->region->land){
		r = mage->region;
	} else {
		r = co->rt;
	}

	if (r->land) {
	  faction * f = findfaction(MONSTER_FACTION);
	  const struct locale * lang = f->locale;
	  
	  anteil += rand() % 4;
	  n = rpeasants(r) * anteil / 10;
	  rsetpeasants(r, rpeasants(r) - n);
	  assert(rpeasants(r) >= 0);

	  u = createunit(r, f, n, new_race[RC_PEASANT]);
    fset(u, UFL_ISNEW);
	  set_string(&u->name, "Bauernmob");
	  /* guard(u, GUARD_ALL);  hier zu fr�h! Befehl BEWACHE setzten */
	  addlist(&u->orders, parse_order(LOC(lang, keywords[K_GUARD]), lang));
	  set_order(&u->thisorder, default_order(lang));
	  a = a_new(&at_unitdissolve);
	  a->data.ca[0] = 1;  /* An rpeasants(r). */
	  a->data.ca[1] = 10; /* 10% */
	  a_add(&u->attribs, a);
	  a_add(&u->attribs, make_hate(mage));
	  
	  sprintf(buf, "Ein Bauernmob erhebt sich und macht Jagd auf Schwarzmagier.");
	  addmessage(r, 0, buf, MSG_MAGIC, ML_INFO);
	}
	return;
}


/* ------------------------------------------------------------- */
/* Name:       Waldbrand
 * Stufe:      10
 * Kategorie:  Region, negativ
 * Gebiet:     Draig
 * Wirkung:
 * Vernichtet 10-80% aller Baeume in der Region.  Kann sich auf benachbarte
 * Regionen ausbreiten, wenn diese (stark) bewaldet sind.  F�r jeweils
 * 10 verbrannte Baeume in der Startregion gibts es eine 1%-Chance, dass
 * sich das Feuer auf stark bewaldete Nachbarregionen ausdehnt, auf
 * bewaldeten mit halb so hoher Wahrscheinlichkeit.  Dort verbrennen
 * dann prozentual halbsoviele bzw ein viertel soviele Baeume wie in der
 * Startregion.
 *
 * Im Extremfall: 1250 Baeume in Region, 80% davon verbrennen (1000).
 * Dann breitet es sich mit 100% Chance in stark bewaldete Regionen
 * aus, mit 50% in bewaldete.  Dort verbrennen dann 40% bzw 20% der Baeume.
 * Weiter als eine Nachbarregion breitet sich dass Feuer nicht aus.
 *
 * Sinn: Ein Feuer in einer "stark bewaldeten" Wueste hat so trotzdem kaum
 * eine Chance, sich weiter auszubreiten, waehrend ein Brand in einem Wald
 * sich fast mit Sicherheit weiter ausbreitet.
 *
 * Flags:
 * (FARCASTING | REGIONSPELL | TESTRESISTANCE)
 */
static int
sp_forest_fire(castorder *co)
{
	unit *u;
	region *nr;
	direction_t i;
	region *r = co->rt;
	unit *mage = (unit *)co->magician;
	int cast_level = co->level;
  double probability;
	double percentage = (rand() % 8 + 1) * 0.1;	 /* 10 - 80% */

#if GROWING_TREES
  int vernichtet_schoesslinge = (int)(rtrees(r, 1) * percentage);
	int destroyed = (int)(rtrees(r, 2) * percentage);
#else
	int destroyed = (int)(rtrees(r) * percentage);
#endif

	if (destroyed<1) {
		cmistake(mage, co->order, 198, MSG_MAGIC);
		return 0;
	}

#if GROWING_TREES
	rsettrees(r, 2, rtrees(r,2) - destroyed);
	rsettrees(r, 1, rtrees(r,1) - vernichtet_schoesslinge);
#else
	rsettrees(r, rtrees(r) - destroyed);
#endif
	probability = destroyed * 0.001;	/* Chance, dass es sich ausbreitet */

	/* melden, 1x pro Partei */
	for (u = r->units; u; u = u->next) freset(u->faction, FL_DH);

	for(u = r->units; u; u = u->next ) {
		if(!fval(u->faction, FL_DH) ) {
			fset(u->faction, FL_DH);
			sprintf(buf, "%s erzeugt eine verheerende Feuersbrunst.  %d %s "
					"den Flammen zum Opfer.",
					cansee(u->faction, r, mage, 0) ? unitname(mage) : "Jemand",
					destroyed,
					destroyed == 1 ? "Baum fiel" : "B�ume fielen");
			addmessage(r, u->faction, buf, MSG_EVENT, ML_INFO);
		}
	}
	if(!fval(mage->faction, FL_DH)){
#if GROWING_TREES
		sprintf(buf, "%s erzeugt eine verheerende Feuersbrunst.  %d %s "
				"den Flammen zum Opfer.", unitname(mage), destroyed+vernichtet_schoesslinge,
				destroyed+vernichtet_schoesslinge == 1 ? "Baum fiel" : "B�ume fielen");
#else
		sprintf(buf, "%s erzeugt eine verheerende Feuersbrunst.  %d %s "
				"den Flammen zum Opfer.", unitname(mage), destroyed,
				destroyed == 1 ? "Baum fiel" : "B�ume fielen");
#endif
		addmessage(0, mage->faction, buf, MSG_MAGIC, ML_INFO);
	}

	for(i = 0; i < MAXDIRECTIONS; i++ ) {
		nr = rconnect(r, i);
		assert(nr);
		destroyed = 0;
		vernichtet_schoesslinge = 0;

#if GROWING_TREES
		if(rtrees(nr,2) + rtrees(nr,1) >= 800) {
			if (chance(probability)) {
				destroyed = (int)(rtrees(nr,2) * percentage/2);
				vernichtet_schoesslinge = (int)(rtrees(nr,1) * percentage/2);
			}
		} else if (rtrees(nr,2) + rtrees(nr,1) >= 600) {
			if (chance(probability/2)) {
				destroyed = (int)(rtrees(nr,2) * percentage/4);
				vernichtet_schoesslinge = (int)(rtrees(nr,1) * percentage/4);
			}
		}

		if (destroyed > 0  || vernichtet_schoesslinge > 0) {
      message * m = msg_message("forestfire_spread", "region next trees",
        r, nr, destroyed+vernichtet_schoesslinge);

      add_message(&r->msgs, m);
      add_message(&mage->faction->msgs, m);
      msg_release(m);

      rsettrees(nr, 2, rtrees(nr,2) - destroyed);
      rsettrees(nr, 1, rtrees(nr,1) - vernichtet_schoesslinge);
    }
#else
		if (rtrees(nr) >= 800) {
			if (chance(probability)) destroyed = (int)(rtrees(nr) * percentage/2);
		} else if (rtrees(nr) >= 600) {
			if(chance(probability/2)) destroyed = (int)(rtrees(nr) * percentage/4);
		}

		if (destroyed > 0 ) {
      message * m = msg_message("forestfire_spread", "region next trees",
        r, nr, destroyed);
      add_message(&r->msgs, m);
      add_message(&mage->faction->msgs, m);
      msg_release(m);
			rsettrees(nr, rtrees(nr) - destroyed);
		}
#endif
	}
	return cast_level;
}

/* ------------------------------------------------------------- */
/* Name:       Chaosfluch
 * Stufe:      5
 * Gebiet:     Draig
 * Kategorie:  (Antimagie) Kraftreduzierer, Einheit, negativ
 * Wirkung:
 *  Auf einen Magier gezaubert verhindert/erschwert dieser Chaosfluch
 *  das Zaubern. Patzer werden warscheinlicher.
 *  Jeder Zauber muss erst gegen den Wiederstand des Fluchs gezaubert
 *  werden und schw�cht dessen Antimagiewiederstand um 1.
 *  Wirkt max(Stufe(Magier) - Stufe(Ziel), rand(3)) Wochen
 * Patzer:
 *  Magier wird selbst betroffen
 *
 * Flags:
 *  (UNITSPELL | SPELLLEVEL | ONETARGET | TESTCANSEE | TESTRESISTANCE)
 *
 */
static int
sp_fumblecurse(castorder *co)
{
	unit *target;
	int rx, sx;
	int duration;
	unit *mage = (unit *)co->magician;
	int cast_level = co->level;
	double force = co->force;
  variant effect;
	curse * c;
	spellparameter *pa = co->par;

	/* wenn kein Ziel gefunden, Zauber abbrechen */
	if (pa->param[0]->flag == TARGET_NOTFOUND) return 0;

	target = pa->param[0]->data.u;

	rx = rand()%3;
	sx = cast_level - effskill(target, SK_MAGIC);
	duration = max(sx, rx) + 1;

  effect.i = (int)(force/2);
	c = create_curse(mage, &target->attribs, ct_find("fumble"),
    force, duration, effect, 0);
	if (c == NULL) {
		report_failure(mage, co->order);
		return 0;
	}

	curse_setflag(c, CURSE_ONLYONE);
	ADDMSG(&target->faction->msgs, msg_message(
		"fumblecurse", "unit region", target, target->region));

	return cast_level;
}

void
patzer_fumblecurse(castorder *co)
{
	unit *mage = (unit *)co->magician;
	int cast_level = co->level;
	double force = co->force;
	int duration = (cast_level/2)+1;
  variant effect;
  curse * c;

  effect.i = (int)(force/2);
	c = create_curse(mage, &mage->attribs, ct_find("fumble"), force,
				duration, effect, 0);
	if (c!=NULL) {
		ADDMSG(&mage->faction->msgs, msg_message(
			"magic_fumble", "unit region command",
			mage, mage->region, co->order));
		curse_setflag(c, CURSE_ONLYONE);
	}
	return;
}

/* ------------------------------------------------------------- */
/* Name:       Drachenruf
 * Stufe:      11
 * Gebiet:     Draig
 * Kategorie:  Monster, Beschw�rung, negativ
 *
 * Wirkung:
 *  In einer W�ste, Sumpf oder Gletscher gezaubert kann innerhalb der
 *  n�chsten 6 Runden ein bis 6 Dracheneinheiten bis Gr��e Wyrm
 *  entstehen.
 *
 *  Mit Stufe 12-15 erscheinen Jung- oder normaler Drachen, mit Stufe
 *  16+ erscheinen normale Drachen oder Wyrme.
 *
 * Flag:
 *  (FARCASTING | REGIONSPELL | TESTRESISTANCE)
 */

static int
sp_summondragon(castorder *co)
{
	region *r = co->rt;
	unit *mage = (unit *)co->magician;
	unit *u;
	int cast_level = co->level;
	double power = co->force;
	region_list *rl,*rl2;
	faction *f;
	int time;
	int number;
	const race * race;

	f = findfaction(MONSTER_FACTION);

	if(rterrain(r) != T_SWAMP && rterrain(r) != T_DESERT
			&& rterrain(r) != T_GLACIER){
		report_failure(mage, co->order);
		return 0;
	}

	for(time = 1; time < 7; time++){
		if (rand()%100 < 25){
			switch(rand()%3){
			case 0:
				race = new_race[RC_WYRM];
				number = 1;
				break;

			case 1:
				race = new_race[RC_DRAGON];
				number = 2;
				break;

			case 2:
			default:
				race = new_race[RC_FIREDRAGON];
				number = 6;
				break;
			}
			{
				trigger * tsummon = trigger_createunit(r, f, race, number);
				add_trigger(&r->attribs, "timer", trigger_timeout(time, tsummon));
			}
		}
	}

	rl = all_in_range(r, (short)power, NULL);

	for(rl2 = rl; rl2; rl2 = rl2->next) {
		for(u = rl2->data->units; u; u = u->next) {
			if (u->race == new_race[RC_WYRM] || u->race == new_race[RC_DRAGON]) {
				attrib * a = a_find(u->attribs, &at_targetregion);
				if (!a) {
					a = a_add(&u->attribs, make_targetregion(co->rt));
				} else {
					a->data.v = co->rt;
				}
				sprintf(buf, "Kommt aus: %s, Will nach: %s", regionname(rl2->data, u->faction), regionname(co->rt, u->faction));
				usetprivate(u, buf);
			}
		}
	}

	ADDMSG(&mage->faction->msgs, msg_message(
				"summondragon", "unit region command region",
				mage, mage->region, co->order, co->rt));

	free_regionlist(rl);
	return cast_level;
}

/* ------------------------------------------------------------- */
/* Name:       Feuerwand
 * Stufe:
 * Gebiet:     Draig
 * Kategorie:  Region, negativ
 * Flag:
 * Kosten:     SPC_LINEAR
 * Aura:
 * Komponenten:
 *
 * Wirkung:
 *   eine Wand aus Feuer entsteht in der angegebenen Richtung
 *
 *   Was f�r eine Wirkung hat die?
 */

typedef struct wallcurse {
	curse * buddy;
	border * wall;
} wallcurse;

void
wall_vigour(curse* c, double delta)
{
	wallcurse * wc = (wallcurse*)c->data.v;
	assert(wc->buddy->vigour==c->vigour);
	wc->buddy->vigour += delta;
	if (wc->buddy->vigour<=0) {
		erase_border(wc->wall);
		wc->wall = NULL;
		((wallcurse*)wc->buddy->data.v)->wall = NULL;
	}
}

const curse_type ct_firewall = {
	"Feuerwand",
	CURSETYP_NORM, 0, (M_DURATION | M_VIGOUR | NO_MERGE),
	"Eine Feuerwand blockiert die Ein- und Ausreise",
	NULL, /* curseinfo */
	wall_vigour /* change_vigour */
};

void
cw_init(attrib * a) {
	curse * c;
	curse_init(a);
	c = (curse*)a->data.v;
	c->data.v = calloc(sizeof(wallcurse), 1);
}

void
cw_write(const attrib * a, FILE * f) {
	border * b = ((wallcurse*)((curse*)a->data.v)->data.v)->wall;
	curse_write(a, f);
	fprintf(f, "%d ", b->id);
}

typedef struct bresvole {
	unsigned int id;
	curse * self;
} bresolve;

static void *
resolve_buddy(variant data) {
	bresolve * br = (bresolve*)data.v;
	border * b = find_border(br->id);
	if (b && b->from && b->to) {
		attrib * a = a_find(b->from->attribs, &at_cursewall);
		while (a && a->data.v!=br->self) {
			curse * c = (curse*)a->data.v;
			wallcurse * wc = (wallcurse*)c->data.v;
			if (wc->wall->id==br->id) break;
			a = a->nexttype;
		}
		if (!a) {
			a = a_find(b->to->attribs, &at_cursewall);
			while (a && a->data.v!=br->self) {
				curse * c = (curse*)a->data.v;
				wallcurse * wc = (wallcurse*)c->data.v;
				if (wc->wall->id==br->id) break;
				a = a->nexttype;
			}
		}
		if (a) {
			curse * c = (curse*)a->data.v;
			free(br);
			return c;
		}
	}
	return NULL;
}

int
cw_read(attrib * a, FILE * f)
{
	bresolve * br = calloc(sizeof(bresolve), 1);
	curse * c = (curse*)a->data.v;
	wallcurse * wc = (wallcurse*)c->data.v;
  variant var;

	curse_read(a, f);
	br->self = c;
	fscanf(f, "%u ", &br->id);

  var.i = br->id;
	ur_add(var, (void**)&wc->wall, resolve_borderid);

  var.v = br;
  ur_add(var, (void**)&wc->buddy, resolve_buddy);
	return AT_READ_OK;
}

attrib_type at_cursewall =
{
	"cursewall",
	cw_init,
	curse_done,
	curse_age,
	cw_write,
	cw_read,
	ATF_CURSE
};

static const char *
fire_name(const border * b, const region * r, const faction * f, int gflags)
{
	unused(f);
	unused(r);
	unused(b);
	if (gflags & GF_ARTICLE)
		return "eine Feuerwand";
	else
		return "Feuerwand";
}

static void
wall_init(border * b)
{
	b->data.v = calloc(sizeof(wall_data), 1);
}

static void
wall_destroy(border * b)
{
	free(b->data.v);
}

static void
wall_read(border * b, FILE * f)
{
	wall_data * fd = (wall_data*)b->data.v;
	variant mno;
	assert(fd);
	fscanf(f, "%d %d ", &mno.i, &fd->force);
	fd->mage = findunitg(mno.i, NULL);
	fd->active = true;
  if (!fd->mage) {
    ur_add(mno, (void**)&fd->mage, resolve_unit);
	}
}

static void
wall_write(const border * b, FILE * f)
{
	wall_data * fd = (wall_data*)b->data.v;
	fprintf(f, "%d %d ", fd->mage?fd->mage->no:0, fd->force);
}

static region *
wall_move(const border * b, struct unit * u, struct region * from, struct region * to, boolean routing)
{
  wall_data * fd = (wall_data*)b->data.v;
  if (!routing && fd->active) {
    int hp = dice(3, fd->force) * u->number;
    hp = min (u->hp, hp);
    u->hp -= hp;
    if (u->hp) {
      ADDMSG(&u->faction->msgs, msg_message("firewall_damage",
        "region unit", from, u));
    }
    else ADDMSG(&u->faction->msgs, msg_message("firewall_death", "region unit", from, u));
    if (u->number>u->hp) {
      scale_number(u, u->hp);
      u->hp = u->number;
    }
  }
  return to;
}

border_type bt_firewall = {
  "firewall", VAR_VOIDPTR,
  b_transparent, /* transparent */
  wall_init, /* init */
  wall_destroy, /* destroy */
  wall_read, /* read */
  wall_write, /* write */
  b_blocknone, /* block */
  fire_name, /* name */
  b_rvisible, /* rvisible */
  b_finvisible, /* fvisible */
  b_uinvisible, /* uvisible */
  NULL,
  wall_move
};

static int
sp_firewall(castorder *co)
{
  border * b;
  wall_data * fd;
  attrib * a;
  region *r = co->rt;
  unit *mage = (unit *)co->magician;
  int cast_level = co->level;
  double force = co->force;
  spellparameter *pa = co->par;
  direction_t dir;
  region * r2;

  dir = finddirection(pa->param[0]->data.s, mage->faction->locale);
  if (dir<MAXDIRECTIONS && dir!=NODIRECTION){
    r2 = rconnect(r, dir);
  } else {
    report_failure(mage, co->order);
    return 0;
  }

  if (!r2 || r2==r) {
    report_failure(mage, co->order);
    return 0;
  }

  b = get_borders(r, r2);
  while (b!=NULL) {
    if (b->type == &bt_firewall) break;
	b = b->next;
  }
  if (b==NULL) {
    b = new_border(&bt_firewall, r, r2);
    fd = (wall_data*)b->data.v;
    fd->force = (int)(force/2+0.5);
    fd->mage = mage;
    fd->active = false;
  } else {
    fd = (wall_data*)b->data.v;
    fd->force = (int)max(fd->force, force/2+0.5);
  }

  a = a_find(b->attribs, &at_countdown);
  if (a==NULL) {
    a = a_add(&b->attribs, a_new(&at_countdown));
    a->data.i = cast_level;
  } else {
    a->data.i = max(a->data.i, cast_level);
  }

  /* melden, 1x pro Partei */
  {
    message * seen = msg_message("firewall_effect", "mage region", mage, r);
    message * unseen = msg_message("firewall_effect", "mage region", NULL, r);
    report_effect(r, mage, seen, unseen);
    msg_release(seen);
    msg_release(unseen);
  }

  return cast_level;
}

/* ------------------------------------------------------------- */

static const char *
wisps_name(const border * b, const region * r, const faction * f, int gflags)
{
	unused(f);
	unused(r);
	unused(b);
	if (gflags & GF_ARTICLE)
		return "eine Gruppe von Irrlichtern";
	else
		return "Irrlichter";
}

static region *
wisps_move(const border * b, struct unit * u, struct region * from, struct region * next, boolean routing)
{
  direction_t reldir = reldirection(from, next);
  wall_data * wd = (wall_data*)b->data.v;
  assert(reldir!=D_SPECIAL);

  if (routing && wd->active) {
    /* pick left and right region: */
    region * rl = rconnect(from, (direction_t)((reldir+MAXDIRECTIONS-1)%MAXDIRECTIONS));
    region * rr = rconnect(from, (direction_t)((reldir+1)%MAXDIRECTIONS));
    int j = rand() % 3;
    if (j==1 && rl && landregion(rterrain(rl))==landregion(rterrain(next))) return rl;
    if (j==2 && rr && landregion(rterrain(rr))==landregion(rterrain(next))) return rr;
  }
  return next;
}

border_type bt_wisps = {
	"wisps", VAR_VOIDPTR,
	b_transparent, /* transparent */
	wall_init, /* init */
	wall_destroy, /* destroy */
	wall_read, /* read */
	wall_write, /* write */
	b_blocknone, /* block */
	wisps_name, /* name */
	b_rvisible, /* rvisible */
	b_fvisible, /* fvisible */
	b_uvisible, /* uvisible */
  NULL, /* visible */
  wisps_move
};

static int
sp_wisps(castorder *co)
{
	border * b;
	wall_data * fd;
	region * r2;
	direction_t dir;
	region *r = co->rt;
	unit *mage = (unit *)co->magician;
	int cast_level = co->level;
	double force = co->force;
	spellparameter *pa = co->par;

	dir = finddirection(pa->param[0]->data.s, mage->faction->locale);
	r2 = rconnect(r, dir);

	if (!r2) {
		report_failure(mage, co->order);
		return 0;
	}

	b = new_border(&bt_wisps, r, r2);
	fd = (wall_data*)b->data.v;
	fd->force = (int)(force/2+0.5);
	fd->mage = mage;
	fd->active = false;

	a_add(&b->attribs, a_new(&at_countdown))->data.i = cast_level;

	/* melden, 1x pro Partei */
        {
          message * seen = msg_message("wisps_effect", "mage region", mage, r);
          message * unseen = msg_message("wisps_effect", "mage region", NULL, r);
          report_effect(r, mage, seen, unseen);
          msg_release(seen);
          msg_release(unseen);
        }

        return cast_level;
}

/* ------------------------------------------------------------- */
/* Name:       Unheilige Kraft
 * Stufe:      10
 * Gebiet:     Draig
 * Kategorie:  Untote Einheit, positiv
 *
 * Wirkung:
 *  transformiert (Stufe)W10 Untote in ihre st�rkere Form
 *
 *
 * Flag:
 *	 (SPELLLEVEL | TESTCANSEE)
 */

static int
sp_unholypower(castorder *co)
{
	unit *mage = (unit *)co->magician;
	int cast_level = co->level;
	spellparameter *pa = co->par;
	int i;
	int n;
	int wounds;

	n = dice((int)co->force, 10);

	for (i = 0; i < pa->length && n > 0; i++) {
		const race * target_race;
		unit *u;

		if(pa->param[i]->flag == TARGET_RESISTS
				|| pa->param[i]->flag == TARGET_NOTFOUND)
			continue;

		u = pa->param[i]->data.u;

		switch (old_race(u->race)) {
		case RC_SKELETON:
			target_race = new_race[RC_SKELETON_LORD];
			break;
		case RC_ZOMBIE:
			target_race = new_race[RC_ZOMBIE_LORD];
			break;
		case RC_GHOUL:
			target_race = new_race[RC_GHOUL_LORD];
			break;
		default:
			cmistake(mage, co->order, 284, MSG_MAGIC);
			continue;
		}
		/* Untote heilen nicht, darum den neuen Untoten maximale hp geben
		 * und vorhandene Wunden abziehen */
		wounds = unit_max_hp(u)*u->number - u->hp;

		if(u->number <= n) {
			n -= u->number;
			u->race = u->irace = target_race;
			u->hp = unit_max_hp(u)*u->number - wounds;
			ADDMSG(&co->rt->msgs, msg_message("unholypower_effect",
				"mage target race", mage, u, target_race));
		} else {
			unit *un;

			/* Wird hoffentlich niemals vorkommen. Es gibt im Source
			 * vermutlich eine ganze Reihe von Stellen, wo das nicht
			 * korrekt abgefangen wird. Besser (aber nicht gerade einfach)
			 * w�re es, eine solche Konstruktion irgendwie zu kapseln. */
			if(fval(u, UFL_LOCKED) || fval(u, UFL_HUNGER)
					|| is_cursed(u->attribs, C_SLAVE, 0)) {
				cmistake(mage, co->order, 74, MSG_MAGIC);
				continue;
			}
			/* Verletzungsanteil der transferierten Personen berechnen */
			wounds = wounds*n/u->number;

			un = create_unit(co->rt, u->faction, 0, target_race, 0, NULL, u);
			transfermen(u, un, n);
			un->hp = unit_max_hp(un)*n - wounds;
			ADDMSG(&co->rt->msgs, msg_message("unholypower_limitedeffect",
				"mage target race amount",
				mage, u, target_race, n));
			n = 0;
		}
	}

	return cast_level;
}

/* ------------------------------------------------------------- */
/* Name:       Todeswolke
 * Stufe:      11
 * Gebiet:     Draig
 * Kategorie:  Region, negativ
 *
 * Wirkung:
 *   Personen in der Region verlieren stufe/2 Trefferpunkte pro Runde.
 *   Dauer force/2
 *   Wirkt gegen MR
 *   R�stung wirkt nicht
 * Patzer:
 *   Magier ger�t in den Staub und verliert zuf�llige Zahl von HP bis
 *   auf max(hp,2)
 * Besonderheiten:
 *   Nicht als curse implementiert, was schlecht ist - man kann dadurch
 *   kein dispell machen. Wegen fix unter Zeitdruck erstmal nicht zu
 *   �ndern...
 * Missbrauchsm�glichkeit:
 *   Hat der Magier mehr HP als Rasse des Feindes (extrem: D�mon/Goblin)
 *   so kann er per Farcasting durch mehrmaliges Zaubern eine
 *   Nachbarregion ausl�schen. Darum sollte dieser Spruch nur einmal auf
 *   eine Region gelegt werden k�nnen.
 *
 * Flag:
 *	 (FARCASTING | REGIONSPELL | TESTRESISTANCE)
 */

typedef struct dc_data {
  region * r;
  unit * mage;
  double strength;
  int countdown;
  boolean active;
} dc_data;

static void
dc_initialize(struct attrib *a)
{
  dc_data * data = (dc_data *)malloc(sizeof(dc_data));
  a->data.v = data;
  data->active = true;
}

static void
dc_finalize(struct attrib * a)
{
  free(a->data.v);
}

static int
dc_age(struct attrib * a)
/* age returns 0 if the attribute needs to be removed, !=0 otherwise */
{
  dc_data * data = (dc_data *)a->data.v;
  region * r = data->r;
  unit ** up = &r->units;
  unit * mage = data->mage;
  unit * u;

  if (mage==NULL || mage->number==0) {
    /* if the mage disappears, so does the spell. */
    return 0;
  }

  if (data->active) while (*up!=NULL) {
    unit * u = *up;
    double damage = data->strength * u->number;

    freset(u->faction, FL_DH);
    if (target_resists_magic(mage, u, TYP_UNIT, 0)){
      continue;
    }

    /* Reduziert durch Magieresistenz */
    damage *= (1.0 - magic_resistance(u));
    change_hitpoints(u, -(int)damage);

    if (*up==u) up=&u->next;
  }

  /* melden, 1x pro Partei */
  for (u = r->units; u; u = u->next ) {
    if (!fval(u->faction, FL_DH) ) {
      fset(u->faction, FL_DH);
      ADDMSG(&u->faction->msgs, msg_message("deathcloud_effect",
        "mage region", cansee(u->faction, r, mage, 0) ? mage : NULL, r));
    }
  }

  if (!fval(mage->faction, FL_DH)){
    ADDMSG(&mage->faction->msgs, msg_message("deathcloud_effect",
      "mage region", mage, r));
  }

  return --data->countdown;
}

static void
dc_write(const struct attrib * a, FILE* F)
{
  const dc_data * data = (const dc_data *)a->data.v;
  fprintf(F, "%d %lf ", data->countdown, data->strength);
  write_unit_reference(data->mage, F);
  write_region_reference(data->r, F);
}

static int
dc_read(struct attrib * a, FILE* F)
/* return AT_READ_OK on success, AT_READ_FAIL if attrib needs removal */
{
  dc_data * data = (dc_data *)a->data.v;
  fscanf(F, "%d %lf ", &data->countdown, &data->strength);
  read_unit_reference(&data->mage, F);
  return read_region_reference(&data->r, F);
}

attrib_type at_deathcloud = {
  "zauber_todeswolke", dc_initialize, dc_finalize, dc_age, dc_write, dc_read
};

static attrib *
mk_deathcloud(unit * mage, region * r, double strength, int duration)
{
  attrib * a = a_new(&at_deathcloud);
  dc_data * data = (dc_data *)a->data.v;

  data->countdown = duration;
  data->r = r;
  data->mage = mage;
  data->strength = strength;
  data->active = false;
  return a;
}

static int
sp_deathcloud(castorder *co)
{
  region *r = co->rt;
  unit *mage = (unit *)co->magician;

  attrib *a = a_find(r->attribs, &at_deathcloud);

  if (a!=NULL) {
    report_failure(mage, co->order);
    return 0;
  }

  a_add(&r->attribs, mk_deathcloud(mage, r, co->force/2, co->level));

  return co->level;
}

void
patzer_deathcloud(castorder *co)
{
	unit *mage = (unit *)co->magician;
	int hp = (mage->hp - 2);

	change_hitpoints(mage, -rand()%hp);

	ADDMSG(&mage->faction->msgs, msg_message(
		"magic_fumble", "unit region command",
		mage, mage->region, co->order));

	return;
}

/* ------------------------------------------------------------- */
/* Name:	   Trollg�rtel
 * Stufe:	  9
 * Gebiet:	 Draig
 * Kategorie:      Artefakt
 * Wirkung:
 *   Artefakt. *50 GE, +2 Schaden, +1 R�stung
*/

static int
sp_create_trollbelt(castorder *co)
{
	unit *mage = (unit *)co->magician;
	int cast_level = co->level;

	change_item(mage,I_TROLLBELT,1);

	creation_message(mage, I_TROLLBELT);
	return cast_level;
}

/* ------------------------------------------------------------- */
/* Name:       Erschaffe ein Flammenschwert
 * Stufe:      12
 * Gebiet:     Draig
 * Kategorie:      Artefakt
 * Wirkung:
 *   Artefakt.
 *   3d6+10 Schaden, +1 auf AT, +1 auf DF, schleudert Feuerball
 */

static int
sp_create_firesword(castorder *co)
{
	unit *mage = (unit *)co->magician;
	int cast_level = co->level;

	change_item(mage,I_FIRESWORD,1);
	creation_message(mage, I_FIRESWORD);
	return cast_level;
}

/* ------------------------------------------------------------- */
/* Name:      Pest
 * Stufe:     7
 * Gebiet:    Draig
 * Wirkung:
 *  ruft eine Pest in der Region hervor.
 * Flags:
 *  (FARCASTING | REGIONSPELL | TESTRESISTANCE)
 * Syntax: ZAUBER [REGION x y] "Pest"
 */
static int
sp_plague(castorder *co)
{
	region *r = co->rt;
	unit *mage = (unit *)co->magician;
	int cast_level = co->level;

	plagues(r, true);

  ADDMSG(&mage->faction->msgs, msg_message("plague_spell", 
    "region mage", r, mage));
	return cast_level;
}

/* ------------------------------------------------------------- */
/* Name:       Beschw�re Schattend�mon
 * Stufe:      8
 * Gebiet:     Draig
 * Kategorie:  Beschw�rung, positiv
 * Wirkung:
 *  Der Magier beschw�rt Stufe^2 Schattend�monen.
 *  Schattend�monen haben Tarnung = (Magie_Magier+ Tarnung_Magier)/2 und
 *  Wahrnehmung 1. Sie haben einen Attacke-Bonus von 8, einen
 *  Verteidigungsbonus von 11 und machen 2d3 Schaden.  Sie entziehen bei
 *  einem Treffer dem Getroffenen einen Attacke- oder
 *  Verteidigungspunkt.  (50% Chance.) Sie haben 25 Hitpoints und
 *  R�stungsschutz 3.
 * Flag:
 *  (SPELLLEVEL)
 */
static int
sp_summonshadow(castorder *co)
{
	region *r = co->rt;
	unit *mage = (unit *)co->magician;
	int cast_level = co->level;
	double force = co->force;
	unit *u;
	int val;

	u = create_unit(r, mage->faction, (int)(force*force), new_race[RC_SHADOW], 0, NULL, mage);

	/* Bekommen Tarnung = (Magie+Tarnung)/2 und Wahrnehmung 1. */
	val = get_level(mage, SK_MAGIC) + get_level(mage, SK_STEALTH);

	set_level(u, SK_STEALTH, val);
	set_level(u, SK_OBSERVATION, 1);

	sprintf(buf, "%s beschw�rt %d D�monen aus dem Reich der Schatten.",
		unitname(mage), (int)(force*force));
	addmessage(0, mage->faction, buf, MSG_MAGIC, ML_INFO);

	return cast_level;
}

/* ------------------------------------------------------------- */
/* Name:       Beschw�re Schattenmeister
 * Stufe:      12
 * Gebiet:     Draig
 * Kategorie:  Beschw�rung, positiv
 * Wirkung:
 *  Diese h�heren Schattend�monen sind erheblich gef�hrlicher als die
 *  einfachen Schattend�monen.  Sie haben Tarnung entsprechend dem
 *  Magietalent des Beschw�rer-1 und Wahrnehmung 5, 75 HP,
 *  R�stungsschutz 4, Attacke-Bonus 11 und Verteidigungsbonus 13, machen
 *  bei einem Treffer 2d4 Schaden, entziehen einen St�rkepunkt und
 *  entziehen 5 Talenttage in einem zuf�lligen Talent.
 *  Stufe^2 D�monen.
 *
 * Flag:
 *  (SPELLLEVEL)
 * */
static int
sp_summonshadowlords(castorder *co)
{
	unit *u;
	region *r = co->rt;
	unit *mage = (unit *)co->magician;
	int cast_level = co->level;
	double force = co->force;

	u = create_unit(r, mage->faction, (int)(force*force), new_race[RC_SHADOWLORD], 0, NULL, mage);

	/* Bekommen Tarnung = Magie und Wahrnehmung 5. */
	set_level(u, SK_STEALTH, get_level(mage, SK_MAGIC));
	set_level(u, SK_OBSERVATION, 5);
	sprintf(buf, "%s beschw�rt %d Schattenmeister.",
		unitname(mage), (int)(force*force));
	addmessage(0, mage->faction, buf, MSG_MAGIC, ML_INFO);
	return cast_level;
}

/* ------------------------------------------------------------- */
/* Name:       Chaossog
 * Stufe:      14
 * Gebiet:     Draig
 * Kategorie:  Teleport
 * Wirkung:
 *  Durch das Opfern von 200 Bauern kann der Chaosmagier ein Tor zur
 *  astralen Welt �ffnen. Das Tor kann im Folgemonat verwendet werden,
 *  es l�st sich am Ende des Folgemonats auf.
 *
 * Flag:  (0)
 */
static int
sp_chaossuction(castorder *co)
{
	unit *u;
	region *rt;
	faction *f;
	region *r = co->rt;
	unit *mage = (unit *)co->magician;
	int cast_level = co->level;

	if (getplane(r)!=get_normalplane()) {
		/* Der Zauber funktioniert nur in der materiellen Welt. */
		cmistake(mage, co->order, 190, MSG_MAGIC);
		return 0;
	}

	rt  = r_standard_to_astral(r);

	if (rt==NULL || is_cursed(rt->attribs, C_ASTRALBLOCK, 0)) {
		/* Hier gibt es keine Verbindung zur astralen Welt.*/
		cmistake(mage, co->order, 216, MSG_MAGIC);
		return 0;
	}

	create_special_direction(r, rt, 2,
			"Ein Wirbel aus reinem Chaos zieht �ber die Region",
			"Wirbel");
	create_special_direction(rt, r, 2,
			"Ein Wirbel aus reinem Chaos zieht �ber die Region",
			"Wirbel");
  new_border(&bt_chaosgate, r, rt);

	for (f = factions; f; f = f->next) freset(f, FL_DH);
	for (u = r->units; u; u = u->next) {
		if (!fval(u->faction, FL_DH)) {
			fset(u->faction, FL_DH);
			sprintf(buf, "%s �ffnete ein Chaostor.",
					cansee(u->faction, r, mage, 0)?unitname(mage):"Jemand");
			addmessage(r, u->faction, buf, MSG_EVENT, ML_INFO);
		}
	}
	for (u = rt->units; u; u = u->next) freset(u->faction, FL_DH);

	for (u = rt->units; u; u = u->next) {
		if (!fval(u->faction, FL_DH)) {
			fset(u->faction, FL_DH);
			addmessage(r, u->faction, "Ein Wirbel aus blendendem Licht erscheint.",
				MSG_EVENT, ML_INFO);
		}
	}
	return cast_level;
}

/* ------------------------------------------------------------- */
/* Name:       Magic Boost - Gabe des Chaos
 * Stufe:      3
 * Gebiet:     Draig
 * Kategorie:  Einheit, positiv
 *
 * Wirkung:
 *   Erh�ht die maximalen Magiepunkte und die monatliche Regeneration auf
 *   das doppelte. Dauer: 4 Wochen Danach sinkt beides auf die H�lfte des
 *   normalen ab.
 * Dauer: 6 Wochen
 * Patzer:
 *   permanenter Stufen- (Talenttage), Regenerations- oder maxMP-Verlust
 * Besonderheiten:
 *   Patzer k�nnen w�hrend der Zauberdauer h�ufiger auftreten derzeit
 *   +10%
 *
 * Flag:
 *  (ONSHIPCAST)
 */

static int
sp_magicboost(castorder *co)
{
	curse * c;
	unit *mage = (unit *)co->magician;
	int cast_level = co->level;
	double power = co->force;
  variant effect;
  trigger * tsummon;

	/* fehler, wenn schon ein boost */
	if(is_cursed(mage->attribs, C_MBOOST, 0) == true){
		report_failure(mage, co->order);
		return 0;
	}

  effect.i = 6;
	c = create_curse(mage, &mage->attribs, ct_find("magicboost"), power, 10, effect, 1);
	/* kann nicht durch Antimagie beeinflusst werden */
	curse_setflag(c, CURSE_IMMUNE);

  effect.i = 200;
	c = create_curse(mage, &mage->attribs, ct_find("aura"), power, 4, effect, 1);

	tsummon = trigger_createcurse(mage, mage, c->type, power, 6, 50, 1);
  add_trigger(&mage->attribs, "timer", trigger_timeout(5, tsummon));

	ADDMSG(&mage->faction->msgs, msg_message("magicboost_effect", 
    "unit region command", mage, mage->region, co->order));

	return cast_level;
}

/* ------------------------------------------------------------- */
/* Name:       kleines Blutopfer
 * Stufe:      4
 * Gebiet:     Draig
 * Kategorie:  Einheit, positiv
 *
 * Wirkung:
 *   Hitpoints to Aura:
 *   skill < 8  = 4:1
 *   skill < 12 = 3:1
 *   skill < 15 = 2:1
 *   skill < 18 = 1:2
 *   skill >    = 2:1
 * Patzer:
 *   permanenter HP verlust
 *
 * Flag:
 *  (ONSHIPCAST)
 */
static int
sp_bloodsacrifice(castorder *co)
{
	unit *mage = (unit *)co->magician;
	int cast_level = co->level;
	int aura;
	int skill = eff_skill(mage, SK_MAGIC, mage->region);
	int hp = (int)(co->force*8);

	if (hp <= 0) {
		report_failure(mage, co->order);
		return 0;
	}

	aura = lovar(hp);

	if (skill < 8) {
		aura /= 4;
	} else if (skill < 12){
		aura /=  3;
	} else if (skill < 15){
		aura /= 2;
	/* von 15 bis 17 ist hp = aura */
	} else if (skill > 17){
		aura *= 2;
	}

	if (aura <= 0){
		report_failure(mage, co->order);
		return 0;
	}

	/* sicherheitshalber gibs hier einen HP gratis. sonst schaffen es
	 * garantiert ne ganze reihe von leuten ihren Magier damit umzubringen */
	mage->hp++;
	change_spellpoints(mage, aura);
	ADDMSG(&mage->faction->msgs,
		msg_message("sp_bloodsacrifice_effect",
		"unit region command amount",
		mage, mage->region, co->order, aura));
	return cast_level;
}

/* ------------------------------------------------------------- */
/* Name:       Totenruf - M�chte des Todes
 * Stufe:      6
 * Gebiet:     Draig
 * Kategorie:  Beschw�rung, positiv
 * Flag:       FARCASTING
 * Wirkung:
 *   Untote aus deathcounther ziehen, bis Stufe*10 St�ck
 *
 * Patzer:
 *   Erzeugt Monsteruntote
 */
static int
sp_summonundead(castorder *co)
{
	int undead;
	unit *u;
	region *r = co->rt;
	unit *mage = (unit *)co->magician;
	int cast_level = co->level;
	int force = (int)(co->force*10);
	const race * race = new_race[RC_SKELETON];

	if (!r->land || deathcount(r) == 0) {
		sprintf(buf, "%s in %s: In %s sind keine Gr�ber.", unitname(mage),
				regionname(mage->region, mage->faction), regionname(r, mage->faction));
		addmessage(0, mage->faction, buf, MSG_MAGIC, ML_MISTAKE);
		return 0;
	}

	undead = min(deathcount(r), 2 + lovar(force));

	if(cast_level <= 8) {
		race = new_race[RC_SKELETON];
	} else if(cast_level <= 12) {
		race = new_race[RC_ZOMBIE];
	} else {
		race = new_race[RC_GHOUL];
	}

	u = create_unit(r, mage->faction, undead, race, 0, NULL, mage);
  make_undead_unit(u);

	sprintf(buf, "%s erweckt %d Untote aus ihren Gr�bern.",
			unitname(mage), undead);
	addmessage(0, mage->faction, buf, MSG_MAGIC, ML_INFO);

	/* melden, 1x pro Partei */
	for (u = r->units; u; u = u->next) freset(u->faction, FL_DH);

	for(u = r->units; u; u = u->next ) {
		if(!fval(u->faction, FL_DH) ) {
			fset(u->faction, FL_DH);
			sprintf(buf, "%s st�rt die Ruhe der Toten",
				cansee(u->faction, r, mage, 0) ? unitname(mage) : "Jemand");
			addmessage(r, u->faction, buf, MSG_EVENT, ML_INFO);
		}
	}
	return cast_level;
}

/* ------------------------------------------------------------- */
/* Name:       Astraler Sog
 * Stufe:      9
 * Gebiet:     Draig
 * Kategorie:  Region, negativ
 * Wirkung:
 *   Allen Magier in der betroffenen Region wird eine Teil ihrer
 *   Magischen Kraft in die Gefilde des Chaos entzogen Jeder Magier im
 *   Einflussbereich verliert Stufe(Zaubernden)*5% seiner Magiepunkte.
 *   Keine Regeneration in der Woche (fehlt noch)
 *
 * Flag:
 *   (REGIONSPELL | TESTRESISTANCE)
 */

static int
sp_auraleak(castorder *co)
{
	int lost_aura;
	double lost;
	unit *u;
	region *r = co->rt;
	unit *mage = (unit *)co->magician;
	int cast_level = co->level;

	lost = min(0.95, cast_level * 0.05);

	for(u = r->units; u; u = u->next) {
		if (is_mage(u)){
			/* Magieresistenz Einheit?  Bei gegenerischen Magiern nur sehr
			 * geringe Chance auf Erfolg wg erh�hter MR, w�rde Spruch sinnlos
			 * machen */
			lost_aura = (int)(get_spellpoints(u)*lost);
			change_spellpoints(u, -lost_aura);
		}
		freset(u->faction, FL_DH);
	}
	for (u = r->units; u; u = u->next) {
		if (!fval(u->faction, FL_DH)) {
			fset(u->faction, FL_DH);
			if (cansee(u->faction, r, mage, 0)) {
				sprintf(buf, "%s rief in %s einen Riss in dem Gef�ge der Magie "
						"hervor, der alle magische Kraft aus der Region riss.",
						unitname(mage), regionname(r, u->faction));
			} else {
				sprintf(buf, "In %s entstand ein Riss in dem Gef�ge der Magie, "
						"der alle magische Kraft aus der Region riss.",
						regionname(r, u->faction));
			}
			addmessage(r, u->faction, buf, MSG_EVENT, ML_WARN);
		}
	}
	return cast_level;
}

/* ------------------------------------------------------------- */
/* BARDE  - CERDDOR*/
/* ------------------------------------------------------------- */
/* Name:       Plappermaul
 * Stufe:      4
 * Gebiet:     Cerddor
 * Kategorie:  Einheit
 *
 * Wirkung:
 *  Einheit ausspionieren. Gibt auch Zauber und Kampfstatus aus.  Wirkt
 *  gegen Magieresistenz. Ist diese zu hoch, so wird der Zauber entdeckt
 *  (Meldung) und der Zauberer erh�lt nur die Talente, nicht die Werte
 *  der Einheit und auch keine Zauber.
 *
 * Flag:
 *  (UNITSPELL | ONETARGET | TESTCANSEE)
 */
static int
sp_babbler(castorder *co)
{
	unit *target;
	region *r = co->rt;
	unit *mage = (unit *)co->magician;
	int cast_level = co->level;
	spellparameter *pa = co->par;

	/* wenn kein Ziel gefunden, Zauber abbrechen */
	if(pa->param[0]->flag == TARGET_NOTFOUND) return 0;

	target = pa->param[0]->data.u;

	if (target->faction == mage->faction){
		/* Die Einheit ist eine der unsrigen */
		cmistake(mage, co->order, 45, MSG_MAGIC);
	}

	/* Magieresistenz Unit */
	if (target_resists_magic(mage, target, TYP_UNIT, 0)){
		spy_message(5, mage, target);
		sprintf(buf, "%s hat einen feuchtfr�hlichen Abend in der Taverne "
				"verbracht. Ausser einem f�rchterlichen Brummsch�del ist da auch "
				"noch das dumme Gef�hl %s seine ganze Lebensgeschichte "
				"erz�hlt zu haben.", unitname(target),
				cansee(target->faction, r, mage, 0)? "irgendjemanden":unitname(mage));
		addmessage(r, target->faction, buf, MSG_EVENT, ML_WARN);

	} else {
		spy_message(100, mage, target);
		sprintf(buf, "%s hat einen feuchtfr�hlichen Abend in der Taverne "
				"verbracht. Ausser einem f�rchterlichen Brummsch�del ist da auch "
				"noch das dumme Gef�hl die ganze Taverne mit seiner Lebensgeschichte "
				"unterhalten zu haben.", unitname(target));
		addmessage(r, target->faction, buf, MSG_EVENT, ML_WARN);
	}
	return cast_level;
}

/* ------------------------------------------------------------- */
/* Name:       Magie analysieren - Geb�ude, Schiffe, Region
 * Name:       Lied des Ortes analysieren
 * Stufe:      8
 * Gebiet:     Cerddor
 *
 * Wirkung:
 *  Zeigt die Verzauberungen eines Objekts an (curse->name,
 *  curse::info). Aus der Differenz Spruchst�rke und Curse->vigour
 *  ergibt sich die Chance den Spruch zu identifizieren ((force -
 *  c->vigour)*10 + 100 %).
 *
 * Flag:
 *  (SPELLLEVEL|ONSHIPCAST)
 */
static int
sp_analysesong_obj(castorder *co)
{
	int obj;
	region *r = co->rt;
	unit *mage = (unit *)co->magician;
	int cast_level = co->level;
	double force = co->force;
	spellparameter *pa = co->par;

	obj = pa->param[0]->typ;

	switch(obj) {
	case SPP_REGION:
		magicanalyse_region(r, mage, force);
		break;

	case SPP_BUILDING:
		{
			building *b = pa->param[0]->data.b;
			magicanalyse_building(b, mage, force);
			break;
		}
	case SPP_SHIP:
		{
			ship * sh = pa->param[0]->data.sh;
			magicanalyse_ship(sh, mage, force);
			break;
		}
	default:
		/* Syntax fehlerhaft */
		return 0;
	}

	return cast_level;
}

/* ------------------------------------------------------------- */
/* Name:	   Gesang des Lebens analysieren
 * Name:	   Magie analysieren - Unit
 * Stufe:	  5
 * Gebiet:	 Cerddor
 * Wirkung:
 *  Zeigt die Verzauberungen eines Objekts an (curse->name,
 *  curse::info). Aus der Differenz Spruchst�rke und Curse->vigour
 *  ergibt sich die Chance den Spruch zu identifizieren ((force -
 *  c->vigour)*10 + 100 %).
 *
 * Flag:
 *  (UNITSPELL|ONSHIPCAST|ONETARGET|TESTCANSEE)
 */
static int
sp_analysesong_unit(castorder *co)
{
	unit *u;
	unit *mage = (unit *)co->magician;
	int cast_level = co->level;
	double force = co->force;
	spellparameter *pa = co->par;

	/* wenn kein Ziel gefunden, Zauber abbrechen */
	if(pa->param[0]->flag == TARGET_NOTFOUND) return 0;

	/* wenn Ziel gefunden, dieses aber Magieresistent war, Zauber
	 * abbrechen aber kosten lassen */
	if(pa->param[0]->flag == TARGET_RESISTS) return cast_level;

	u = pa->param[0]->data.u;

	magicanalyse_unit(u, mage, force);

	return cast_level;
}
/* ------------------------------------------------------------- */
/* Name:	   Charming
 * Stufe:	  13
 * Gebiet:	 Cerddor
 * Flag:	   UNITSPELL
 * Wirkung:
 *   bezauberte Einheit wechselt 'virtuell' die Partei und f�hrt fremde
 *   Befehle aus.
 *   Dauer: 3 - force+2 Wochen
 *   Wirkt gegen Magieresistenz
 *
 *   wirkt auf eine Einheit mit maximal Talent Personen normal. F�r jede
 *   zus�tzliche Person gibt es einen Bonus auf Magieresistenz, also auf
 *   nichtgelingen, von 10%.
 *
 *   Das h�chste Talent der Einheit darf maximal so hoch sein wie das
 *   Magietalent des Magiers. F�r jeden Talentpunkt mehr gibt es einen
 *   Bonus auf Magieresistenz von 15%, was dazu f�hrt, das bei +2 Stufen
 *   die Magiersistenz bei 90% liegt.
 *
 *   Migrantenz�hlung muss Einheit �berspringen
 *
 *   Attackiere verbieten
 * Flags:
 *   (UNITSPELL | ONETARGET | TESTCANSEE)
 */
static int
sp_charmingsong(castorder *co)
{
	unit *target;
	int duration;
	skill_t i;
	unit *mage = (unit *)co->magician;
	int cast_level = co->level;
	double force = co->force;
	spellparameter *pa = co->par;
	int resist_bonus = 0;
	int tb = 0;

	/* wenn kein Ziel gefunden, Zauber abbrechen */
	if (pa->param[0]->flag == TARGET_NOTFOUND) return 0;

	target = pa->param[0]->data.u;

	/* Auf eigene Einheiten versucht zu zaubern? Garantiert Tippfehler */
	if (target->faction == mage->faction){
		/* Die Einheit ist eine der unsrigen */
		cmistake(mage, co->order, 45, MSG_MAGIC);
	}

	/* Magieresistensbonus f�r mehr als Stufe Personen */
	if (target->number > force) {
		resist_bonus += (int)((target->number - force) * 10);
	}
	/* Magieresistensbonus f�r h�here Talentwerte */
	for(i = 0; i < MAXSKILLS; i++){
		int sk = effskill(target, i);
		if (tb < sk) tb = sk;
	}
	tb -= effskill(mage, SK_MAGIC);
	if(tb > 0){
		resist_bonus += tb * 15;
	}
	/* Magieresistenz */
	if (target_resists_magic(mage, target, TYP_UNIT, resist_bonus)) {
		report_failure(mage, co->order);
		sprintf(buf, "%s f�hlt sich einen Moment lang benommen und desorientiert.",
				unitname(target));
		addmessage(target->region, target->faction, buf, MSG_EVENT, ML_WARN);
		return 0;
	}

	duration = 3 + rand()%(int)force;
	{
		trigger * trestore = trigger_changefaction(target, target->faction);
		/* l�uft die Dauer ab, setze Partei zur�ck */
		add_trigger(&target->attribs, "timer", trigger_timeout(duration, trestore));
		/* wird die alte Partei von Target aufgel�st, dann auch diese Einheit */
		add_trigger(&target->faction->attribs, "destroy", trigger_killunit(target));
		/* wird die neue Partei von Target aufgel�st, dann auch diese Einheit */
		add_trigger(&mage->faction->attribs, "destroy", trigger_killunit(target));
	}
	/* sperre ATTACKIERE, GIB PERSON und �berspringe Migranten */
	create_curse(mage, &target->attribs, ct_find("slavery"), force, duration, zero_effect, 0);

	/* setze Partei um und l�sche langen Befehl aus Sicherheitsgr�nden */
	u_setfaction(target,mage->faction);
	set_order(&target->thisorder, NULL);

	/* setze Parteitarnung, damit nicht sofort klar ist, wer dahinter
	 * steckt */
	fset(target, UFL_PARTEITARNUNG);

	sprintf(buf, "%s gelingt es %s zu verzaubern. %s wird f�r etwa %d "
			"Wochen unseren Befehlen gehorchen.", unitname(mage),
			unitname(target), unitname(target), duration);
	addmessage(0, mage->faction, buf, MSG_MAGIC, ML_INFO);

	return cast_level;
}

/* ------------------------------------------------------------- */
/* Name:	   Gesang des wachen Geistes
 * Stufe:	  10
 * Gebiet:	 Cerddor
 * Kosten:	 SPC_LEVEL
 * Wirkung:
 *  Bringt einmaligen Bonus von +15% auf Magieresistenz.  Wirkt auf alle
 *  Aliierten (HELFE BEWACHE) in der Region.
 *  Dauert Stufe Wochen an, ist nicht kumulativ.
 * Flag:
 *   (FARCASTING | SPELLLEVEL | REGIONSPELL | TESTRESISTANCE)
 */
static int
sp_song_resistmagic(castorder *co)
{
	variant mr_bonus;
	region *r = co->rt;
	unit *mage = (unit *)co->magician;
	int cast_level = co->level;
	double force = co->force;
	int duration = (int)force+1;

  mr_bonus.i = 15;
	create_curse(mage, &r->attribs, ct_find("goodmagicresistancezone"),
			force, duration, mr_bonus, 0);

	/* Erfolg melden */
	ADDMSG(&mage->faction->msgs, msg_message(
				"regionmagic_effect", "unit region command", mage,
				mage->region, co->order));

	return cast_level;
}
/* ------------------------------------------------------------- */
/* Name:	   Gesang des schwachen Geistes
 * Stufe:	  12
 * Gebiet:	 Cerddor
 * Wirkung:
 *  Bringt einmaligen Malus von -15% auf Magieresistenz.
 *  Wirkt auf alle Nicht-Aliierten (HELFE BEWACHE) in der Region.
 *  Dauert Stufe Wochen an, ist nicht kumulativ.
 * Flag:
 *   (FARCASTING | SPELLLEVEL | REGIONSPELL | TESTRESISTANCE)
 */
static int
sp_song_susceptmagic(castorder *co)
{
  variant mr_malus;
	region *r = co->rt;
	unit *mage = (unit *)co->magician;
	int cast_level = co->level;
	double force = co->force;
	int duration = (int)force+1;

  mr_malus.i = 15;
	create_curse(mage, &r->attribs, ct_find("badmagicresistancezone"),
			force, duration, mr_malus, 0);

	ADDMSG(&mage->faction->msgs, msg_message(
				"regionmagic_effect", "unit region command", mage,
				mage->region, co->order));

	return cast_level;
}

/* ------------------------------------------------------------- */
/* Name:	   Aufruhr beschwichtigen
 * Stufe:	  15
 * Gebiet:	 Cerddor
 * Flag:	   FARCASTING
 * Wirkung:
 *   zerstreut einen Monsterbauernmob, Antimagie zu 'Aufruhr
 *   verursachen'
 */

static int
sp_rallypeasantmob(castorder *co)
{
	unit *u, *un;
	int erfolg = 0;
	region *r = co->rt;
	unit *mage = (unit *)co->magician;
	int cast_level = co->level;

	/* TODO
	remove_allcurse(&r->attribs, C_RIOT, 0);
	*/

	for (u = r->units; u; u = un){
		un = u->next;
		if (u->faction->no == MONSTER_FACTION && u->race == new_race[RC_PEASANT]){
			rsetpeasants(r, rpeasants(r) + u->number);
			rsetmoney(r, rmoney(r) + get_money(u));
			set_money(u, 0);
			setguard(u, GUARD_NONE);
			set_number(u, 0);
			erfolg = cast_level;
		}
	}

	if (erfolg){
		for (u = r->units; u; u = u->next) freset(u->faction, FL_DH);
		for(u = r->units; u; u = u->next ) {
			if (!fval(u->faction, FL_DH) ) {
				fset(u->faction, FL_DH);
				sprintf(buf, "%s bes�nftigt den Bauernaufstand in %s.",
						cansee(u->faction, r, mage, 0) ? unitname(mage) : "Jemand",
						regionname(r, u->faction));
				addmessage(r, u->faction, buf, MSG_MAGIC, ML_INFO);
			}
		}
		if (!fval(mage->faction, FL_DH)){
			sprintf(buf, "%s bes�nftigt den Bauernaufstand in %s.",
					unitname(mage), regionname(r, u->faction));
			addmessage(r, u->faction, buf, MSG_MAGIC, ML_INFO);
		}
	} else {
		sprintf(buf, "Der Bauernaufstand in %s hatte sich bereits verlaufen.",
				regionname(r, u->faction));
		addmessage(r, u->faction, buf, MSG_MAGIC, ML_INFO);
	}
	return erfolg;
}

/* ------------------------------------------------------------- */
/* Name:	   Aufruhr verursachen
 * Stufe:	  16
 * Gebiet:	 Cerddor
 * Wirkung:
 *	Wiegelt 60% bis 90% der Bauern einer Region auf.  Bauern werden ein
 *	gro�er Mob, der zur Monsterpartei geh�rt und die Region bewacht.
 *	Regionssilber sollte auch nicht durch Unterhaltung gewonnen werden
 *	k�nnen.
 *
 *	 Fehlt: Triggeraktion: l�ste Bauernmob auf und gib alles an Region,
 *	 dann k�nnen die Bauernmobs ihr Silber mitnehmen und bleiben x
 *	 Wochen bestehen
 *
 *  alternativ: L�sen sich langsam wieder auf
 * Flag:
 *   (FARCASTING | REGIONSPELL | TESTRESISTANCE)
 */
static int
sp_raisepeasantmob(castorder *co)
{
	unit *u;
	attrib *a;
	int n;
	variant anteil;
	region *r = co->rt;
	unit *mage = (unit *)co->magician;
	int cast_level = co->level;
	double force = co->force;
	int duration = (int)force+1;

  anteil.i = 6 + (rand()%4);

	n = rpeasants(r) * anteil.i / 10;
	n = max(0, n);
	n = min(n, rpeasants(r));

	if(n <= 0){
		report_failure(mage, co->order);
		return 0;
	}

	rsetpeasants(r, rpeasants(r) - n);
	assert(rpeasants(r) >= 0);

	u = createunit(r, findfaction(MONSTER_FACTION), n, new_race[RC_PEASANT]);
  fset(u, UFL_ISNEW);
  set_string(&u->name, "Aufgebrachte Bauern");
	guard(u, GUARD_ALL);
	a = a_new(&at_unitdissolve);
	a->data.ca[0] = 1;	/* An rpeasants(r). */
	a->data.ca[1] = 15;	/* 15% */
	a_add(&u->attribs, a);

	create_curse(mage, &r->attribs, ct_find("riotzone"), cast_level, duration, anteil, 0);

	for (u = r->units; u; u = u->next) freset(u->faction, FL_DH);
	for (u = r->units; u; u = u->next ) {
		if (!fval(u->faction, FL_DH) ) {
			fset(u->faction, FL_DH);
			ADDMSG(&u->faction->msgs, msg_message(
				"sp_raisepeasantmob_effect", "mage region",
				cansee(u->faction, r, mage, 0) ? mage : NULL, r ));
		}
	}
	if (!fval(mage->faction, FL_DH)){
			ADDMSG(&mage->faction->msgs, msg_message(
				"sp_raisepeasantmob_effect", "mage region", mage, r));
	}

	return cast_level;
}

/* ------------------------------------------------------------- */
/* Name:	   Ritual der Aufnahme / Migrantenwerben
 * Stufe:	  9
 * Gebiet:	 Cerddor
 * Wirkung:
 *	Bis zu Stufe Personen fremder Rasse k�nnen angeworben werden. Die
 *	angeworbene Einheit muss kontaktieren. Keine teuren Talente
 *
 * Flag:
 *  (UNITSPELL | SPELLLEVEL | ONETARGET | TESTCANSEE)
 */
static int
sp_migranten(castorder *co)
{
  unit *target;
  order * ord;
  int kontaktiert = 0;
  region *r = co->rt;
  unit *mage = (unit *)co->magician;
  int cast_level = co->level;
  spellparameter *pa = co->par;
  spell *sp = co->sp;

  /* wenn kein Ziel gefunden, Zauber abbrechen */
  if(pa->param[0]->flag == TARGET_NOTFOUND) return 0;

  target = pa->param[0]->data.u; /* Zieleinheit */

  /* Personen unserer Rasse k�nnen problemlos normal �bergeben werden */
  if (target->race == mage->faction->race){
    /* u ist von unserer Art, das Ritual w�re verschwendete Aura. */
    ADDMSG(&mage->faction->msgs, msg_message(
      "sp_migranten_fail1", "unit region command target", mage,
      mage->region, co->order, target));
  }
  /* Auf eigene Einheiten versucht zu zaubern? Garantiert Tippfehler */
  if (target->faction == mage->faction){
    cmistake(mage, co->order, 45, MSG_MAGIC);
  }

  /* Keine Monstereinheiten */
  if (!playerrace(target->race)){
    sprintf(buf, "%s kann nicht auf Monster gezaubert werden.",
      spell_name(sp, mage->faction->locale));
    addmessage(0, mage->faction, buf, MSG_EVENT, ML_WARN);
    return 0;
  }
  /* niemand mit teurem Talent */
  if (teure_talente(target)) {
    sprintf(buf, "%s hat unaufk�ndbare Bindungen an seine alte Partei.",
      unitname(target));
    addmessage(0, mage->faction, buf, MSG_EVENT, ML_WARN);
    return 0;
  }
  /* maximal Stufe Personen */
  if (target->number > cast_level
    || target->number > max_spellpoints(r, mage))
  {
    sprintf(buf, "%s in %s: 'ZAUBER %s': So viele Personen �bersteigen "
      "meine Kr�fte.", unitname(mage), regionname(mage->region, mage->faction),
      spell_name(sp, mage->faction->locale));
    addmessage(0, mage->faction, buf, MSG_MAGIC, ML_WARN);
  }

  /* Kontakt pr�fen (aus alter Teleportroutine �bernommen) */
  {
    /* Nun kommt etwas reichlich krankes, um den
    * KONTAKTIERE-Befehl des Ziels zu �berpr�fen. */

    for (ord = target->orders; ord; ord = ord->next) {
      if (get_keyword(ord) == K_CONTACT) {
        const char *c;
        /* So weit, so gut. S->s ist also ein KONTAKTIERE. Nun gilt es,
        * herauszufinden, wer kontaktiert wird. Das ist nicht trivial.
        * Zuerst mu� der Parameter herausoperiert werden. */
        /* Leerzeichen finden */

        init_tokens(ord);
        skip_token();
        c = getstrtoken();

        /* Wenn ein Leerzeichen da ist, ist *c != 0 und zeigt auf das
        * Leerzeichen. */

        if (c!=NULL) {
          int kontakt = atoi36(c);

          if (kontakt == mage->no) {
            kontaktiert = 1;
            break;
          }
        }
      }
    }
  }

  if (kontaktiert == 0) {
    ADDMSG(&mage->faction->msgs, msg_message("spellfail::contact",
      "mage region command target", mage, mage->region, co->order,
      target));
    return 0;
  }
  u_setfaction(target,mage->faction);
  set_order(&target->thisorder, NULL);

  /* Erfolg melden */
  ADDMSG(&mage->faction->msgs, msg_message("sp_migranten", 
    "unit region command target", mage, mage->region, co->order, target));

  return target->number;
}

/* ------------------------------------------------------------- */
/* Name:	   Gesang der Friedfertigkeit
 * Stufe:	  12
 * Gebiet:	 Cerddor
 * Wirkung:
 *	 verhindert jede Attacke f�r lovar(Stufe/2) Runden
 */

static int
sp_song_of_peace(castorder *co)
{
	unit *u;
	region *r = co->rt;
	unit *mage = (unit *)co->magician;
	int cast_level = co->level;
	double force = co->force;
	int duration = 2 + lovar(force/2);
	
	create_curse(mage,&r->attribs, ct_find("peacezone"), force, duration, zero_effect, 0);

	for (u = r->units; u; u = u->next) freset(u->faction, FL_DH);
	for (u = r->units; u; u = u->next ) {
		if (!fval(u->faction, FL_DH) ) {
			fset(u->faction, FL_DH);
			if (cansee(u->faction, r, mage, 0)){
				sprintf(buf, "%s's Gesangskunst begeistert die Leute. Die "
						"friedfertige Stimmung des Lieds �bertr�gt sich auf alle "
						"Zuh�rer. Einige werfen ihre Waffen weg.", unitname(mage));
			}else{
				sprintf(buf, "In der Luft liegt ein wundersch�nes Lied, dessen "
						"friedfertiger Stimmung sich niemand entziehen kann. "
						"Einige Leute werfen sogar ihre Waffen weg.");
			}
			addmessage(r, u->faction, buf, MSG_EVENT, ML_INFO);
		}
	}
	return cast_level;

}

/* ------------------------------------------------------------- */
/* Name:	   Hohes Lied der Gaukelei
 * Stufe:	  2
 * Gebiet:	 Cerddor
 * Wirkung:
 *	 Das Unterhaltungsmaximum steigt von 20% auf 40% des
 *	 Regionsverm�gens. Der Spruch h�lt Stufe Wochen an
 */

static int
sp_generous(castorder *co)
{
	unit *u;
	region *r = co->rt;
	unit *mage = (unit *)co->magician;
	int cast_level = co->level;
	double force = co->force;
	int duration = (int)force+1;
  variant effect;

	if(is_cursed(r->attribs, C_DEPRESSION, 0)){
		sprintf(buf, "%s in %s: Die Stimmung in %s ist so schlecht, das "
				"niemand auf den Zauber reagiert.", unitname(mage),
				regionname(mage->region, mage->faction), regionname(r, mage->faction));
		addmessage(0, mage->faction, buf, MSG_MAGIC, ML_MISTAKE);
		return 0;
	}

  effect.i = 2;
	create_curse(mage,&r->attribs, ct_find("generous"), force, duration, effect, 0);

	for (u = r->units; u; u = u->next) freset(u->faction, FL_DH);
	for (u = r->units; u; u = u->next ) {
		if (!fval(u->faction, FL_DH) ) {
			fset(u->faction, FL_DH);
			if (cansee(u->faction, r, mage, 0)){
				sprintf(buf, "%s's Gesangskunst begeistert die Leute. Die "
						"fr�hliche und ausgelassene Stimmung der Lieder �bertr�gt "
						"sich auf alle Zuh�rer.", unitname(mage));
			}else{
				sprintf(buf, "Die Darbietungen eines fahrenden Gauklers begeistern "
						"die Leute. Die fr�hliche und ausgelassene Stimmung seiner "
						"Lieder �bertr�gt sich auf alle Zuh�rer.");
			}
			addmessage(r, u->faction, buf, MSG_EVENT, ML_INFO);
		}
	}
	return cast_level;
}

/* ------------------------------------------------------------- */
/* Name:	   Anwerbung
 * Stufe:	  4
 * Gebiet:	 Cerddor
 * Wirkung:
 *	 Bauern schliessen sich der eigenen Partei an
 *	 ist zus�tzlich zur Rekrutierungsmenge in der Region
 * */

static int
sp_recruit(castorder *co)
{
	unit *u;
	region *r = co->rt;
  int n, maxp = rpeasants(r);
	unit *mage = (unit *)co->magician;
	int cast_level = co->level;
	double force = co->force;
  faction *f = mage->faction;

	if (maxp == 0) {
		report_failure(mage, co->order);
		return 0;
	}
	/* Immer noch zuviel auf niedrigen Stufen. Deshalb die Rekrutierungskosten
	 * mit einfliessen lassen und daf�r den Exponenten etwas gr��er.
	 * Wenn die Rekrutierungskosten deutlich h�her sind als der Faktor,
	 * ist das Verh�ltniss von ausgegebene Aura pro Bauer bei Stufe 2
	 * ein mehrfaches von Stufe 1, denn in beiden F�llen gibt es nur 1
	 * Bauer, nur die Kosten steigen. */
	n = (int)((pow(force, 1.6) * 100)/f->race->recruitcost);
  if (f->race==new_race[RC_URUK]) {
    n = min(2*maxp, n);
    n = max(n, 1);
    rsetpeasants(r, maxp - (n+1) / 2);
  } else {
    n = min(maxp, n);
    n = max(n, 1);
    rsetpeasants(r, maxp - n);
  }

	u = create_unit(r, f, n, f->race, 0, (n == 1 ? "Bauer" : "Bauern"), mage);
	set_order(&u->thisorder, default_order(f->locale));

	sprintf(buf, "%s konnte %d %s anwerben", unitname(mage), n,
			n == 1 ? "Bauer" : "Bauern");
	addmessage(r, mage->faction, buf, MSG_MAGIC, ML_INFO);

  if (f->race==new_race[RC_URUK]) n = (n+1) / 2;
  
  return cast_level;
}

/* ------------------------------------------------------------- */
/* Name:    Wanderprediger - Gro�e Anwerbung
 * Stufe:   14
 * Gebiet:  Cerddor
 * Wirkung:
 *	 Bauern schliessen sich der eigenen Partei an
 *	 ist zus�tzlich zur Rekrutierungsmenge in der Region
 * */

static int
sp_bigrecruit(castorder *co)
{
	unit *u;
	region *r = co->rt;
  int n, maxp = rpeasants(r);
	unit *mage = (unit *)co->magician;
	int cast_level = co->level;
	double force = co->force;
  faction *f = mage->faction;

	if (maxp <= 0) {
		report_failure(mage, co->order);
		return 0;
	}
	/* F�r vergleichbare Erfolge bei unterschiedlichen Rassen die
	 * Rekrutierungskosten mit einfliessen lassen. */

  n = (int)force + lovar((force * force * 1000)/f->race->recruitcost);
  if (f->race==new_race[RC_URUK]) {
    n = min(2*maxp, n);
    n = max(n, 1);
    rsetpeasants(r, maxp - (n+1) / 2);
  } else {
    n = min(maxp, n);
    n = max(n, 1);
    rsetpeasants(r, maxp - n);
  }

	u = create_unit(r, f, n, f->race, 0, (n == 1 ? "Bauer" : "Bauern"), mage);
	set_order(&u->thisorder, default_order(f->locale));

	sprintf(buf, "%s konnte %d %s anwerben", unitname(mage), n,
			n == 1 ? "Bauer" : "Bauern");
	addmessage(r, mage->faction, buf, MSG_MAGIC, ML_INFO);
	return cast_level;
}

/* ------------------------------------------------------------- */
/* Name:	   Aushorchen
 * Stufe:	  7
 * Gebiet:	 Cerddor
 * Wirkung:
 *  Erliegt die Einheit dem Zauber, so wird sie dem Magier alles
 *  erz�hlen, was sie �ber die gefragte Region wei�. Ist in der Region
 *  niemand ihrer Partei, so wei� sie nichts zu berichten.  Auch kann
 *  sie nur das erz�hlen, was sie selber sehen k�nnte.
 * Flags:
 *   (UNITSPELL | ONETARGET | TESTCANSEE)
 */

/* restistenz der einheit pr�fen */
static int
sp_pump(castorder *co)
{
	unit *u, *target;
	region *rt;
	boolean see = false;
	region *r = co->rt;
	unit *mage = (unit *)co->magician;
	spellparameter *pa = co->par;
	int cast_level = co->level;
	spell *sp = co->sp;

	/* wenn kein Ziel gefunden, Zauber abbrechen */
	if(pa->param[0]->flag == TARGET_NOTFOUND) return 0;

	target = pa->param[0]->data.u; /* Zieleinheit */

	if (fval(target->race, RCF_UNDEAD)) {
		sprintf(buf, "%s kann nicht auf Untote gezaubert werden.",
			spell_name(sp, mage->faction->locale));
		addmessage(0, mage->faction, buf, MSG_EVENT, ML_WARN);
		return 0;
	}
	if (is_magic_resistant(mage, target, 0) || target->faction->no == MONSTER_FACTION) {
		report_failure(mage, co->order);
		return 0;
	}

	rt  = pa->param[1]->data.r;

	for (u = rt->units; u; u = u->next){
		if(u->faction == target->faction)
			see = true;
	}

	if (see == false){
		sprintf(buf, "%s horcht %s �ber %s aus, aber %s wusste nichts zu "
				"berichten.", unitname(mage), unitname(target), regionname(rt, mage->faction),
				unitname(target));
		addmessage(r, mage->faction, buf, MSG_MAGIC, ML_INFO);
		return cast_level/2;
	} else {
		sprintf(buf, "%s horcht %s �ber %s aus.", unitname(mage),
				unitname(target), regionname(rt, mage->faction));
		addmessage(r, mage->faction, buf, MSG_MAGIC, ML_INFO);
	}

	u = createunit(rt, mage->faction, RS_FARVISION, new_race[RC_SPELL]);
	set_string(&u->name, "Zauber: Aushorchen");
	u->age = 2;
	set_level(u, SK_OBSERVATION, eff_skill(target, SK_OBSERVATION, u->region));

	return cast_level;
}

/* ------------------------------------------------------------- */
/* Name:	   Verf�hrung
 * Stufe:	  6
 * Gebiet:	 Cerddor
 * Wirkung:
 *  Bet�rt eine Einheit, so das sie ihm den gr��ten Teil ihres Bargelds
 *  und 50% ihres Besitzes schenkt. Sie beh�lt jedoch immer soviel, wie
 *  sie zum �berleben braucht. Wirkt gegen Magieresistenz.
 *  min(Stufe*1000$, u->money - maintenace)
 *  Von jedem Item wird 50% abgerundet ermittelt und �bergeben. Dazu
 *  kommt Itemzahl%2 mit 50% chance
 *
 * Flags:
 * (UNITSPELL | ONETARGET | TESTRESISTANCE | TESTCANSEE)
 */
static int
sp_seduce(castorder *co)
{
	unit *target;
	int loot;
	item **itmp;
	region *r = co->rt;
	unit *mage = (unit *)co->magician;
	spellparameter *pa = co->par;
	int cast_level = co->level;
	spell *sp = co->sp;
	double force = co->force;

	/* wenn kein Ziel gefunden, Zauber abbrechen */
	if(pa->param[0]->flag == TARGET_NOTFOUND) return 0;

	target = pa->param[0]->data.u; /* Zieleinheit */

	if (fval(target->race, RCF_UNDEAD)) {
		sprintf(buf, "%s kann nicht auf Untote gezaubert werden.",
			spell_name(sp, mage->faction->locale));
		addmessage(0, mage->faction, buf, MSG_MAGIC, ML_WARN);
		return 0;
	}

	/* Erfolgsmeldung */
	sprintf(buf, "%s schenkt %s ", unitname(target), unitname(mage));

	loot = min(cast_level * 1000, get_money(target) - (MAINTENANCE*target->number));
	loot = max(loot, 0);
	change_money(mage, loot);
	change_money(target, -loot);

	if(loot > 0){
		icat(loot);
	} else {
		scat("kein");
	}
	scat(" Silber");
	itmp=&target->items;
	while(*itmp) {
		item * itm = *itmp;
		loot = itm->number/2;
		if (itm->number % 2) {
			loot += rand() % 2;
		}
		if (loot > 0) {
			loot = (int)min(loot, force * 5);
			scat(", ");
			icat(loot);
			scat(" ");
			scat(locale_string(mage->faction->locale, resourcename(itm->type->rtype, (loot==1)?0:GR_PLURAL)));
			i_change(&mage->items, itm->type, loot);
			if (loot!=itm->number) itm->number-=loot;
			else i_free(i_remove(itmp, itm));
		}
		if (*itmp==itm) itmp=&itm->next;
	}
	scat(".");
	addmessage(r, mage->faction, buf, MSG_MAGIC, ML_INFO);

	sprintf(buf, "%s verfiel dem Gl�cksspiel und hat fast sein ganzes Hab "
			"und Gut verspielt.", unitname(target));
	addmessage(r, target->faction, buf, MSG_EVENT, ML_WARN);

	return cast_level;
}

/* ------------------------------------------------------------- */
/* Name:	   Miriams flinke Finger
 * Stufe:	  11
 * Gebiet:	 Cerddor
 * Wirkung:
 *	 Erschafft Artefakt I_RING_OF_NIMBLEFINGER, Ring der flinken Finger. Der
 *	 Ring verzehnfacht die Produktion seines Tr�gers (wirkt nur auf
 *	 'Handwerker') und erh�ht Menge des geklauten Geldes auf 500 Silber
 *	 pro Talentpunkt Unterschied
 */

static int
sp_create_nimblefingerring(castorder *co)
{
	unit *mage = (unit *)co->magician;
	int cast_level = co->level;

	change_item(mage,I_RING_OF_NIMBLEFINGER,1);
	creation_message(mage, I_RING_OF_NIMBLEFINGER);
	return cast_level;
}

/* ------------------------------------------------------------- */
/* Name:	   Monster friedlich stimmen
 * Stufe:	  6
 * Gebiet:	 Cerddor
 * Wirkung:
 *  verhindert Angriffe des bezauberten Monsters auf die Partei des
 *  Barden f�r Stufe Wochen. Nicht �bertragbar, dh Verb�ndete werden vom
 *  Monster nat�rlich noch angegriffen. Wirkt nicht gegen Untote
 *  Jede Einheit kann maximal unter einem Beherrschungszauber dieser Art
 *  stehen, dh wird auf die selbe Einheit dieser Zauber von einem
 *  anderen Magier nochmal gezaubert, schl�gt der Zauber fehl.
 *
 * Flags:
 * (UNITSPELL | ONSHIPCAST | ONETARGET | TESTRESISTANCE | TESTCANSEE)
 */

static int
sp_calm_monster(castorder *co)
{
	curse * c;
	unit *target;
	region *r = co->rt;
	unit *mage = (unit *)co->magician;
	spellparameter *pa = co->par;
	int cast_level = co->level;
	double force = co->force;
	spell *sp = co->sp;
  variant effect;

	/* wenn kein Ziel gefunden, Zauber abbrechen */
	if(pa->param[0]->flag == TARGET_NOTFOUND) return 0;

	target = pa->param[0]->data.u; /* Zieleinheit */

	if (fval(target->race, RCF_UNDEAD)) {
		sprintf(buf, "%s kann nicht auf Untote gezaubert werden.",
			spell_name(sp, mage->faction->locale));
		addmessage(0, mage->faction, buf, MSG_MAGIC, ML_WARN);
		return 0;
	}

  effect.i = mage->faction->subscription;
	c = create_curse(mage, &target->attribs, ct_find("calmmonster"), force,
    (int)force, effect, 0);
	if (c==NULL) {
		report_failure(mage, co->order);
		return 0;
	}
	/* Nur ein Beherrschungszauber pro Unit */
	curse_setflag(c, CURSE_ONLYONE);

	sprintf(buf, "%s bes�nftigt %s.", unitname(mage), unitname(target));
	addmessage(r, mage->faction, buf, MSG_MAGIC, ML_INFO);
	return cast_level;
}

/* ------------------------------------------------------------- */
/* Name:	   schaler Wein
 * Stufe:	  7
 * Gebiet:	 Cerddor
 * Wirkung:
 *  wird gegen Magieresistenz gezaubert Das Opfer vergisst bis zu
 *  Talenttage seines h�chsten Talentes und tut die Woche nix.
 *  Nachfolgende Zauber sind erschwert.
 *  Wirkt auf bis zu 10 Personen in der Einheit
 *
 * Flags:
 * (UNITSPELL | ONETARGET | TESTRESISTANCE | TESTCANSEE)
 */

static int
sp_headache(castorder *co)
{
	skill * smax = NULL;
	int i;
	unit *target;
	region *r = co->rt;
	unit *mage = (unit *)co->magician;
	spellparameter *pa = co->par;
	int cast_level = co->level;

	/* Macht alle nachfolgenden Zauber doppelt so teuer */
	countspells(mage, 1);

	/* wenn kein Ziel gefunden, Zauber abbrechen */
	if(pa->param[0]->flag == TARGET_NOTFOUND) return 0;

	target = pa->param[0]->data.u; /* Zieleinheit */

	/* finde das gr��te Talent: */
	for (i=0;i!=target->skill_size;++i) {
		skill * sv = target->skills+i;
		if (smax==NULL || skill_compare(sv, smax)>0) {
			smax = sv;
		}
	}
	if (smax!=NULL) {
		/* wirkt auf maximal 10 Personen */
		int change = min(10, target->number) * (rand()%2+1) / target->number;
		reduce_skill(target, smax, change);
	}
	set_order(&target->thisorder, NULL);

	sprintf(buf, "%s verschafft %s einige feuchtfr�hliche Stunden mit heftigen "
		"Nachwirkungen.", unitname(mage), unitname(target));
	addmessage(mage->region, mage->faction, buf, MSG_MAGIC, ML_INFO);

	sprintf(buf, "%s hat h�llische Kopfschmerzen und kann sich an die "
		"vergangene Woche nicht mehr erinnern. Nur noch daran, wie alles mit "
		"einer fr�hlichen Feier in irgendeiner Taverne anfing...", unitname(target));
	addmessage(r, target->faction, buf, MSG_EVENT, ML_WARN);

	return cast_level;
}


/* ------------------------------------------------------------- */
/* Name:	   Mob
 * Stufe:	  10
 * Gebiet:	 Cerddor
 * Wirkung:
 *   Wiegelt Stufe*250 Bauern zu einem Mob auf, der sich der Partei des
 *   Magier anschliesst Pro Woche beruhigen sich etwa 15% wieder und
 *   kehren auf ihre Felder zur�ck
 *
 * Flags:
 * (SPELLLEVEL | REGIONSPELL | TESTRESISTANCE)
 */
static int
sp_raisepeasants(castorder *co)
{
	int bauern;
	unit *u, *u2;
	attrib *a;
	region *r = co->rt;
	unit *mage = (unit *)co->magician;
	int cast_level = co->level;
	double power = co->force;

	if(rpeasants(r) == 0) {
		addmessage(r, mage->faction, "Hier gibt es keine Bauern.",
				MSG_MAGIC, ML_MISTAKE);
		return 0;
	}
	bauern = (int)min(rpeasants(r), power*250);
	rsetpeasants(r, rpeasants(r) - bauern);

	u2 = create_unit(r,mage->faction, bauern, new_race[RC_PEASANT], 0,"Wilder Bauernmob",mage);
  set_string(&u2->name, "Erz�rnte Bauern");

	fset(u2, UFL_LOCKED);
	fset(u2, UFL_PARTEITARNUNG);

	a = a_new(&at_unitdissolve);
	a->data.ca[0] = 1;	/* An rpeasants(r). */
	a->data.ca[1] = 15;	/* 15% */
	a_add(&u2->attribs, a);

	for (u = r->units; u; u = u->next) freset(u->faction, FL_DH);
	for (u = r->units; u; u = u->next ) {
		if (!fval(u->faction, FL_DH) ) {
			fset(u->faction, FL_DH);
			sprintf(buf, "%s wiegelt %d Bauern auf.",
					cansee(u->faction, r, mage, 0) ? unitname(mage) : "Jemand",
					u2->number);
			addmessage(r, u->faction, buf, MSG_MAGIC, ML_INFO);
		}
	}
	return cast_level;
}

/* ------------------------------------------------------------- */
/* Name:       Tr�bsal
 * Stufe:      11
 * Kategorie:  Region, negativ
 * Wirkung:
 *  in der Region kann f�r einige Wochen durch Unterhaltung kein Geld
 *  mehr verdient werden
 *
 * Flag:
 * (FARCASTING | REGIONSPELL | TESTRESISTANCE)
 */
static int
sp_depression(castorder *co)
{
	unit *u;
	region *r = co->rt;
	unit *mage = (unit *)co->magician;
	int cast_level = co->level;
	double force = co->force;
	int duration = (int)force+1;

  create_curse(mage,&r->attribs, ct_find("depression"), force, duration, zero_effect, 0);

	for (u = r->units; u; u = u->next) freset(u->faction, FL_DH);
	for (u = r->units; u; u = u->next ) {
		if (!fval(u->faction, FL_DH) ) {
			fset(u->faction, FL_DH);
			sprintf(buf, "%s sorgt f�r Tr�bsal unter den Bauern.",
					cansee(u->faction, r, mage, 0) ? unitname(mage) : "Jemand");
			addmessage(r, u->faction, buf, MSG_MAGIC, ML_INFO);
		}
	}
	return cast_level;
}

#if 0
/* ------------------------------------------------------------- */
/* Name:       Hoher Gesang der Drachen
 * Stufe:      14
 * Gebiet:     Cerddor
 * Kategorie:  Monster, Beschw�rung, positiv
 *
 * Wirkung:
 *  Erh�ht HP-Regeneration in der Region und lockt drachenartige (Wyrm,
 *  Drache, Jungdrache, Seeschlange, ...) aus der Umgebung an
 *
 * Flag:
 *  (FARCASTING | REGIONSPELL | TESTRESISTANCE)
 */
/* TODO zur Aktivierung in Zauberliste aufnehmen*/
static int
sp_dragonsong(castorder *co)
{
	region *r = co->rt; /* Zauberregion */
	unit *mage = (unit *)co->magician;
	unit *u;
	int cast_level = co->level;
	double power = co->force;
	region_list *rl,*rl2;
	faction *f;

	/* TODO HP-Effekt */

	f = findfaction(MONSTER_FACTION);

	rl = all_in_range(r, (int)power);

	for(rl2 = rl; rl2; rl2 = rl2->next) {
		for(u = rl2->data->units; u; u = u->next) {
			if (u->race->flags & RCF_DRAGON) {
				attrib * a = a_find(u->attribs, &at_targetregion);
				if (!a) {
					a = a_add(&u->attribs, make_targetregion(r));
				} else {
					a->data.v = r;
				}
				sprintf(buf, "Kommt aus: %s, Will nach: %s", regionname(rl2->data, u->faction), regionname(r, u->faction));
				usetprivate(u, buf);
			}
		}
	}

	ADDMSG(&mage->faction->msgs, msg_message(
				"summondragon", "unit region command region",
				mage, mage->region, co->order, co->rt));

	free_regionlist(rl);
	return cast_level;
}

/* ------------------------------------------------------------- */
/* Name:       Hoher Gesang der Verlockung
 * Stufe:      17
 * Gebiet:     Cerddor
 * Kategorie:  Monster, Beschw�rung, positiv
 *
 * Wirkung:
 *  Lockt Bauern aus den umliegenden Regionen her
 *
 * Flag:
 *  (FARCASTING | REGIONSPELL | TESTRESISTANCE)
 */
/* TODO zur Aktivierung in Zauberliste aufnehmen*/

static int
sp_songofAttraction(castorder *co)
{
	region *r = co->rt; /* Zauberregion */
	unit *mage = (unit *)co->magician;
	int cast_level = co->level;
	/* double power = co->force; */

	/* TODO Wander Effekt */

	ADDMSG(&mage->faction->msgs, msg_message(
				"summon", "unit region command region",
				mage, mage->region, co->order, r));

	return cast_level;
}

#endif

/* ------------------------------------------------------------- */
/* TRAUM - Illaun */
/* ------------------------------------------------------------- */

/* Name:       Seelenfrieden
 * Stufe:      2
 * Kategorie:  Region, positiv
 * Gebiet:     Illaun
 * Wirkung:
 *	 Reduziert Untotencounter
 * Flag: (0)
 */

int
sp_puttorest(castorder *co)
{
	region *r = co->rt;
	unit *mage = (unit *)co->magician;
  int dead = deathcount(r);
  int laid_to_rest = dice((int)(co->force * 2), 100);
  message * seen = msg_message("puttorest", "mage", mage);
  message * unseen = msg_message("puttorest", "mage", NULL);

  laid_to_rest = max(laid_to_rest, dead);

	deathcounts(r, -laid_to_rest);

  report_effect(r, mage, seen, unseen);
  msg_release(seen);
  msg_release(unseen);
	return co->level;
}

/* Name:       Traumschl��chen
 * Stufe:      3
 * Kategorie:  Region, Geb�ude, positiv
 * Gebiet:     Illaun
 * Wirkung:
 *  Mit Hilfe dieses Zaubers kann der Traumweber die Illusion eines
 *  beliebigen Geb�udes erzeugen. Die Illusion kann betreten werden, ist
 *  aber ansonsten funktionslos und ben�tigt auch keinen Unterhalt
 * Flag: (0)
 */

int
sp_icastle(castorder *co)
{
	building *b;
	const building_type * type;
	attrib *a;
	region *r = co->rt;
	unit *mage = (unit *)co->magician;
	int cast_level = co->level;
	double power = co->force;
	spellparameter *pa = co->par;
	icastle_data * data;

	if((type=findbuildingtype(pa->param[0]->data.s, mage->faction->locale)) == NULL) {
		type = bt_find("castle");
	}

	b = new_building(bt_find("illusion"), r, mage->faction->locale);

	/* Gr��e festlegen. */
	if(type == bt_find("illusion")) {
		b->size = (rand()%(int)((power*power)+1)*10);
	} else if (b->type->maxsize == -1) {
		b->size = ((rand()%(int)(power))+1)*5;
	} else {
		b->size = b->type->maxsize;
	}
	sprintf(buf, "%s %s", LOC(mage->faction->locale, buildingtype(b, 0)), buildingid(b));
	set_string(&b->name, buf);

	/* TODO: Auf timeout und action_destroy umstellen */
	a = a_add(&b->attribs, a_new(&at_icastle));
	data = (icastle_data*)a->data.v;
	data->type = type;
	data->building = b;
	data->time = 2+(rand()%(int)(power)+1)*(rand()%(int)(power)+1);

	if(mage->region == r) {
		leave(r, mage);
		mage->building = b;
	}

	ADDMSG(&mage->faction->msgs, msg_message(
		"icastle_create", "unit region command", mage, mage->region,
		co->order));

	addmessage(r, 0,
		"Verwundert blicken die Bauern auf ein pl�tzlich erschienenes Geb�ude.",
		MSG_EVENT, ML_INFO);

	return cast_level;
}

/* ------------------------------------------------------------- */
/* Name:		Gestaltwandlung
 * Stufe:		3
 * Gebiet:  Illaun
 * Wirkung:
 *  Zieleinheit erscheint f�r (Stufe) Wochen als eine andere Gestalt
 *  (wie bei d�monischer Rassetarnung).
 * Syntax: ZAUBERE "Gestaltwandlung"  <einheit>     <rasse>
 * Flags:
 *  (UNITSPELL | SPELLLEVEL | ONETARGET)
 */

int
sp_illusionary_shapeshift(castorder *co)
{
	unit *u;
	const race * rc;
	region *r = co->rt;
	unit *mage = (unit *)co->magician;
	int cast_level = co->level;
	double power = co->force;
	spellparameter *pa = co->par;

	/* wenn kein Ziel gefunden, Zauber abbrechen */
	if(pa->param[0]->flag == TARGET_NOTFOUND) return 0;

	/* wenn Ziel gefunden, dieses aber Magieresistent war, Zauber
	 * abbrechen aber kosten lassen */
	if(pa->param[0]->flag == TARGET_RESISTS) return cast_level;

	u = pa->param[0]->data.u;

	rc = findrace(pa->param[1]->data.s, mage->faction->locale);
	if (rc == NULL) {
		cmistake(mage, co->order, 202, MSG_MAGIC);
		return 0;
	}

	/* �hnlich wie in laws.c:setealth() */
	if (!playerrace(rc)) {
		sprintf(buf, "%s %s keine %s-Gestalt annehmen.",
			unitname(u),
			u->number > 1 ? "k�nnen" : "kann",
			LOC(u->faction->locale, rc_name(rc, 2)));
		addmessage(r, mage->faction, buf, MSG_MAGIC, ML_MISTAKE);
		return 0;
	}
	{
		trigger * trestore = trigger_changerace(u, NULL, u->irace);
		add_trigger(&u->attribs, "timer", trigger_timeout((int)power+2, trestore));
	}
	u->irace = rc;

	sprintf(buf, "%s l��t %s als %s erscheinen.",
		unitname(mage), unitname(u), LOC(u->faction->locale, rc_name(rc, u->number != 1)));
	addmessage(r, mage->faction, buf, MSG_MAGIC, ML_INFO);

	return cast_level;
}

/* ------------------------------------------------------------- */
/* Name:	   Traumdeuten
 * Stufe:	  7
 * Kategorie:      Einheit
 *
 * Wirkung:
 *  Wirkt gegen Magieresistenz.  Spioniert die Einheit aus. Gibt alle
 *  Gegenst�nde, Talente mit Stufe, Zauber und Kampfstatus an.
 *
 *  Magieresistenz hier pr�fen, wegen Fehlermeldung
 *
 * Flag:
 * (UNITSPELL | ONETARGET)
 */
int
sp_readmind(castorder *co)
{
	unit *target;
	unit *mage = (unit *)co->magician;
	int cast_level = co->level;
	spellparameter *pa = co->par;

	/* wenn kein Ziel gefunden, Zauber abbrechen */
	if(pa->param[0]->flag == TARGET_NOTFOUND) return 0;

	target = pa->param[0]->data.u;

	if (target->faction == mage->faction){
		/* Die Einheit ist eine der unsrigen */
		cmistake(mage, co->order, 45, MSG_MAGIC);
	}

	/* Magieresistenz Unit */
	if (target_resists_magic(mage, target, TYP_UNIT, 0)){
		report_failure(mage, co->order);
		/* "F�hlt sich beobachtet"*/
	  ADDMSG(&target->faction->msgs, msg_message(
						    "stealdetect", "unit", target));
		return 0;
	}
	spy_message(2, mage, target);

	return cast_level;
}

/* ------------------------------------------------------------- */
/* Name:	   Regionstraum analysieren
 * Stufe:	  9
 * Aura:	   18
 * Kosten:	 SPC_FIX
 * Wirkung:
 *  Zeigt die Verzauberungen eines Objekts an (curse->name,
 *  curse::info). Aus der Differenz Spruchst�rke und Curse->vigour
 *  ergibt sich die Chance den Spruch zu identifizieren ((force -
 *  c->vigour)*10 + 100 %).
 */
int
sp_analyseregionsdream(castorder *co)
{
	region *r = co->rt;
	unit *mage = (unit *)co->magician;
	int cast_level = co->level;

	magicanalyse_region(r, mage, cast_level);

	return cast_level;
}


/* ------------------------------------------------------------- */
/* Name:	   Traumbilder erkennen
 * Stufe:	  5
 * Aura:	   12
 * Kosten:	 SPC_FIX
 * Wirkung:
 *  Zeigt die Verzauberungen eines Objekts an (curse->name,
 *  curse::info). Aus der Differenz Spruchst�rke und Curse->vigour
 *  ergibt sich die Chance den Spruch zu identifizieren ((force -
 *  c->vigour)*10 + 100 %).
 */
int
sp_analysedream(castorder *co)
{
	unit *u;
	unit *mage = (unit *)co->magician;
	int cast_level = co->level;
	spellparameter *pa = co->par;

	/* wenn kein Ziel gefunden, Zauber abbrechen */
	if(pa->param[0]->flag == TARGET_NOTFOUND) return 0;

	/* wenn Ziel gefunden, dieses aber Magieresistent war, Zauber
	 * abbrechen aber kosten lassen */
	if(pa->param[0]->flag == TARGET_RESISTS) return cast_level;

	u = pa->param[0]->data.u;
	magicanalyse_unit(u, mage, cast_level);

	return cast_level;
}

/* ------------------------------------------------------------- */
/* Name:       Schlechte Tr�ume
 * Stufe:      10
 * Kategorie:  Region, negativ
 * Wirkung:
 *   Dieser Zauber erm�glicht es dem Tr�umer, den Schlaf aller
 *   nichtaliierten Einheiten (HELFE BEWACHE) in der Region so starkzu
 *   st�ren, das sie 1 Talentstufe in allen Talenten
 *   vor�bergehend verlieren. Der Zauber wirkt erst im Folgemonat.
 *
 * Flags:
 * (FARCASTING | SPELLLEVEL | REGIONSPELL | TESTRESISTANCE)
 * */
int
sp_baddreams(castorder *co)
{
	int duration;
	unit *mage = (unit *)co->magician;
	int cast_level = co->level;
	double power = co->force;
	region *r = co->rt;
	curse * c;
  variant effect;

	/* wirkt erst in der Folgerunde, soll mindestens eine Runde wirken,
	 * also duration+2 */
	duration = (int)max(1, power/2); /* Stufe 1 macht sonst mist */
	duration = 2 + rand()%duration;

	/* Nichts machen als ein entsprechendes Attribut in die Region legen. */
  effect.i = -1;
	c = create_curse(mage, &r->attribs, ct_find("gbdream"), power, duration, effect, 0);
	curse_setflag(c, CURSE_ISNEW);

	/* Erfolg melden*/
	ADDMSG(&mage->faction->msgs, msg_message(
				"regionmagic_effect", "unit region command", mage,
				mage->region, co->order));

	return cast_level;
}

/* ------------------------------------------------------------- */
/* Name:       Sch�ne Tr�ume
 * Stufe:      8
 * Kategorie:
 * Wirkung:
 *   Dieser Zauber erm�glicht es dem Tr�umer, den Schlaf aller aliierten
 *   Einheiten in der Region so zu beeinflussen, da� sie f�r einige Zeit
 *   einen Bonus von 1 Talentstufe in allen Talenten
 *   bekommen. Der Zauber wirkt erst im Folgemonat.
 * Flags:
 * (FARCASTING | SPELLLEVEL | REGIONSPELL | TESTRESISTANCE)
 */
int
sp_gooddreams(castorder *co)
{
	int    duration;
	curse * c;
	region *r = co->rt;
	unit *mage = (unit *)co->magician;
	int cast_level = co->level;
	double power = co->force;
  variant effect;

	/* wirkt erst in der Folgerunde, soll mindestens eine Runde wirken,
	 * also duration+2 */
	duration = (int)max(1, power/2); /* Stufe 1 macht sonst mist */
	duration = 2 + rand()%duration;
  effect.i = 1;
	c = create_curse(mage, &r->attribs, ct_find("gbdream"), power, duration, effect, 0);
	curse_setflag(c, CURSE_ISNEW);

	/* Erfolg melden*/
	ADDMSG(&mage->faction->msgs, msg_message(
				"regionmagic_effect", "unit region command", mage,
				mage->region, co->order));

	return cast_level;
}

/* ------------------------------------------------------------- */
/* Name:
 * Stufe:      9
 * Kategorie:
 * Wirkung:
 *	 Es wird eine Kloneinheit erzeugt, die nichts kann. Stirbt der
 *	 Magier, wird er mit einer Wahrscheinlichkeit von 90% in den Klon
 *	 transferiert.
 * Flags:
 * (NOTFAMILARCAST)
 */
int
sp_clonecopy(castorder *co)
{
	unit *clone;
	region *r = co->rt;
	region *target_region = co->rt;
	unit *mage = (unit *)co->magician;
	int cast_level = co->level;

	if (get_clone(mage) != NULL ) {
		cmistake(mage, co->order, 298, MSG_MAGIC);
		return 0;
	}

  sprintf(buf, "Klon von %s", unitname(mage));
	clone = create_unit(target_region, mage->faction, 1, new_race[RC_CLONE], 0, buf, mage);
	clone->status = ST_FLEE;
	fset(clone, UFL_LOCKED);

	create_newclone(mage, clone);

	sprintf(buf, "%s erschafft einen Klon.", unitname(mage));
	addmessage(r, mage->faction, buf, MSG_MAGIC, ML_INFO);

	return cast_level;
}

/* ------------------------------------------------------------- */
int
sp_dreamreading(castorder *co)
{
	unit *u,*u2;
	region *r = co->rt;
	unit *mage = (unit *)co->magician;
	int cast_level = co->level;
	spellparameter *pa = co->par;
	double power = co->force;

	/* wenn kein Ziel gefunden, Zauber abbrechen */
	if(pa->param[0]->flag == TARGET_NOTFOUND) return 0;

	/* wenn Ziel gefunden, dieses aber Magieresistent war, Zauber
	 * abbrechen aber kosten lassen */
	if(pa->param[0]->flag == TARGET_RESISTS) return cast_level;

	u = pa->param[0]->data.u;

	/* Illusionen und Untote abfangen. */
	if (fval(u->race, RCF_UNDEAD|RCF_ILLUSIONARY)) {
		ADDMSG(&mage->faction->msgs, msg_message(
			"spellunitnotfound", "unit region command id",
			mage, mage->region, co->order, strdup(itoa36(u->no))));
		return 0;
	}

  /* Entfernung */
  if(distance(mage->region, u->region) > power) {
			addmessage(r, mage->faction, "Die Einheit ist zu weit "
					"entfernt.", MSG_MAGIC, ML_MISTAKE);
      return 0;
  }

  u2 = createunit(u->region,mage->faction, RS_FARVISION, new_race[RC_SPELL]);
  set_number(u2, 1);
  set_string(&u2->name, "sp_dreamreading");
  u2->age  = 2;   /* Nur f�r diese Runde. */
  set_level(u2, SK_OBSERVATION, eff_skill(u, SK_OBSERVATION, u2->region));

  sprintf(buf, "%s verliert sich in die Tr�ume von %s und erh�lt einen "
      "Eindruck von %s.", unitname(mage), unitname(u), regionname(u->region, mage->faction));
  addmessage(r, mage->faction, buf, MSG_EVENT, ML_INFO);
	return cast_level;
}

/* ------------------------------------------------------------- */
/* Wirkt power/2 Runden auf bis zu power^2 Personen
 * mit einer Chance von 5% vermehren sie sich  */
int
sp_sweetdreams(castorder *co)
{
	region *r = co->rt;
	unit *mage = (unit *)co->magician;
	int cast_level = co->level;
	double power = co->force;
	spellparameter *pa = co->par;
	int men, n;
	int duration = (int)(power/2)+1;
	int opfer = (int)(power*power);

	/* Schleife �ber alle angegebenen Einheiten */
	for (n = 0; n < pa->length; n++) {
		curse * c;
		unit *u;
    variant effect;
		/* sollte nie negativ werden */
		if (opfer < 1) break;

		if (pa->param[n]->flag == TARGET_RESISTS ||
			pa->param[n]->flag == TARGET_NOTFOUND)
			continue;

		/* Zieleinheit */
		u = pa->param[n]->data.u;

    if (!ucontact(u, mage)) {
			cmistake(mage, co->order, 40, MSG_EVENT);
      continue;
    }
		men = min(opfer, u->number);
		opfer -= men;

		/* Nichts machen als ein entsprechendes Attribut an die Einheit legen. */
    effect.i = 5;
		c = create_curse(mage,&u->attribs, ct_find("orcish"), power, duration, effect, men);
		curse_setflag(c, CURSE_ISNEW);

		sprintf(buf, "%s verschafft %s ein interessanteres Nachtleben.",
				unitname(mage), unitname(u));
		addmessage(r, mage->faction, buf, MSG_EVENT, ML_INFO);
	}
	return cast_level;
}

/* ------------------------------------------------------------- */
int
sp_create_tacticcrystal(castorder *co)
{
	unit *mage = (unit *)co->magician;
	int cast_level = co->level;

	change_item(mage,I_TACTICCRYSTAL,1);
	creation_message(mage, I_TACTICCRYSTAL);
	return cast_level;
}

/* ------------------------------------------------------------- */
int
sp_disturbingdreams(castorder *co)
{
	region *r = co->rt;
	unit *mage = (unit *)co->magician;
	int cast_level = co->level;
	double power = co->force;
	int duration = 1 + (int)(power/6);
  variant effect;
	curse * c;
  
  effect.i = 10;
  c = create_curse(mage, &r->attribs, ct_find("badlearn"), power, duration, effect, 0);
	curse_setflag(c, CURSE_ISNEW);

	sprintf(buf, "%s sorgt f�r schlechten Schlaf in %s.",
			unitname(mage), regionname(r, mage->faction));
	addmessage(0, mage->faction, buf, MSG_EVENT, ML_INFO);
	return cast_level;
}

/* ------------------------------------------------------------- */
int
sp_dream_of_confusion(castorder *co)
{
	unit *u;
	region_list *rl,*rl2;
	region *r = co->rt;
	unit *mage = (unit *)co->magician;
	int cast_level = co->level;
	double power = co->force;
	double range = (power-14)/2-1;
	int duration = (int)(power-14)+1;

	rl = all_in_range(r, (short)range, NULL);

	for(rl2 = rl; rl2; rl2 = rl2->next) {
    region * r2 = rl2->data;
    variant effect;
		curse * c;
		/* Magieresistenz jeder Region pr�fen */
		if (target_resists_magic(mage, r2, TYP_REGION, 0)){
			report_failure(mage, co->order);
			continue;
		}

    effect.i = cast_level*5;
		c = create_curse(mage, &r2->attribs,
			ct_find("disorientationzone"), power, duration, effect, 0);
		/* soll der Zauber schon in der Zauberrunde wirken? */
		curse_setflag(c, CURSE_ISNEW);

		for (u = r2->units; u; u = u->next) freset(u->faction, FL_DH);
		for (u = r2->units; u; u = u->next ) {
			if(!fval(u->faction, FL_DH) ) {
				fset(u->faction, FL_DH);
				sprintf(buf, "%s beschw�rt einen Schleier der Verwirrung.",
						cansee(u->faction, r, mage, 0) ? unitname(mage) : "Jemand");
				addmessage(r2, u->faction, buf, MSG_EVENT, ML_INFO);
			}
		}
		if(!fval(mage->faction, FL_DH)){
			sprintf(buf, "%s beschw�rt einen Schleier der Verwirrung.",
					unitname(mage));
		  addmessage(0, mage->faction, buf, MSG_MAGIC, ML_INFO);
		}
	}
	free_regionlist(rl);
	return cast_level;
}

/* ------------------------------------------------------------- */
/* ASTRAL / THEORIE / M_ASTRAL */
/* ------------------------------------------------------------- */
/* Name:	   Magie analysieren
 * Stufe:	  1
 * Aura:	   1
 * Kosten:	 SPC_LINEAR
 * Komponenten:
 *
 * Wirkung:
 *  Zeigt die Verzauberungen eines Objekts an (curse->name,
 *  curse::info). Aus der Differenz Spruchst�rke und Curse->vigour
 *  ergibt sich die Chance den Spruch zu identifizieren ((force -
 *  c->vigour)*10 + 100 %).
 *
 * Flags:
 *  UNITSPELL, SHIPSPELL, BUILDINGSPELL
 */

int
sp_analysemagic(castorder *co)
{
	int obj;
	unit *mage = (unit *)co->magician;
	int cast_level = co->level;
	spellparameter *pa = co->par;

	/* Objekt ermitteln */
	obj = pa->param[0]->typ;

	switch(obj) {
		case SPP_REGION:
		{
			region *tr = pa->param[0]->data.r;
			magicanalyse_region(tr, mage, cast_level);
			break;
		}
    case SPP_TEMP:
		case SPP_UNIT:
		{
			unit *u;
			u = pa->param[0]->data.u;
			magicanalyse_unit(u, mage, cast_level);
			break;
		}
		case SPP_BUILDING:
		{
			building *b;
			b = pa->param[0]->data.b;
			magicanalyse_building(b, mage, cast_level);
			break;
		}
		case SPP_SHIP:
		{
			ship *sh;
			sh = pa->param[0]->data.sh;
			magicanalyse_ship(sh, mage, cast_level);
			break;
		}
		default:
		/* Fehlerhafter Parameter */
			return 0;
	}

	return cast_level;
}

/* ------------------------------------------------------------- */

int
sp_itemcloak(castorder *co)
{
	unit *target;
	unit *mage = (unit *)co->magician;
	int cast_level = co->level;
	double power = co->force;
	int duration = (int)power+1;
	spellparameter *pa = co->par;

	/* wenn kein Ziel gefunden, Zauber abbrechen */
	if(pa->param[0]->flag == TARGET_NOTFOUND) return 0;

	/* Zieleinheit */
	target = pa->param[0]->data.u;

	create_curse(mage,&target->attribs, ct_find("itemcloak"), power, duration, zero_effect, 0);
	ADDMSG(&mage->faction->msgs, msg_message(
		"itemcloak", "mage target", mage, target));

	return cast_level;
}

/* ------------------------------------------------------------- */
/* Name:	   Magieresistenz erh�hen
 * Stufe:	  3
 * Aura:	   5 MP
 * Kosten:	 SPC_LEVEL
 * Komponenten:
 *
 * Wirkung:
 *	 erh�ht die Magierestistenz der Personen um 20 Punkte f�r 6 Wochen
 *	 Wirkt auf Stufe*5 Personen kann auf mehrere Einheiten gezaubert
 *	 werden, bis die Zahl der m�glichen Personen ersch�pft ist
 *
 * Flags:
 *	 UNITSPELL
 */
int
sp_resist_magic_bonus(castorder *co)
{
	unit *u;
	int n, m, opfer;
	variant resistbonus;
	int duration = 6;
	unit *mage = (unit *)co->magician;
	int cast_level = co->level;
	double power = co->force;
	spellparameter *pa = co->par;

	/* Pro Stufe k�nnen bis zu 5 Personen verzaubert werden */
	opfer = (int)(power * 5);

	/* Schleife �ber alle angegebenen Einheiten */
	for (n = 0; n < pa->length; n++) {
		/* sollte nie negativ werden */
		if (opfer < 1)
			break;

		if(pa->param[n]->flag == TARGET_RESISTS
				|| pa->param[n]->flag == TARGET_NOTFOUND)
			continue;

		u = pa->param[n]->data.u;

		/* Ist die Einheit schon verzaubert, wirkt sich dies nur auf die
		 * Menge der Verzauberten Personen aus.
		if(is_cursed(u->attribs, C_MAGICRESISTANCE, 0))
			continue;
		*/

		m = min(u->number,opfer);
		opfer -= m;

    resistbonus.i = 20;
		create_curse(mage, &u->attribs, ct_find("magicresistance"),
			power, duration, resistbonus, m);

		sprintf(buf, "%s wird kurz von einem magischen Licht umh�llt.",
				unitname(u));
		addmessage(0, u->faction, buf, MSG_EVENT, ML_IMPORTANT);

		/* und noch einmal dem Magier melden */
		if (u->faction != mage->faction)
			addmessage(mage->region, mage->faction, buf, MSG_MAGIC, ML_INFO);
	}
	/* pro 5 nicht verzauberte Personen kann der Level und damit die
	 * Kosten des Zaubers um 1 reduziert werden. (die Formel geht von
	 * immer abrunden da int aus) */
	cast_level -= opfer/5;

	return cast_level;
}

/* ------------------------------------------------------------- */
/* "ZAUBERE [STUFE n]  \"Astraler Weg\" <Einheit-Nr> [<Einheit-Nr> ...]",
 *
 * Parameter:
 * pa->param[0]->data.s
*/
int
sp_enterastral(castorder *co)
{
	region *rt, *ro;
	unit *u, *u2;
	int remaining_cap;
	int n, w;
	region *r = co->rt;
	unit *mage = (unit *)co->magician;
	int cast_level = co->level;
	double power = co->force;
	spellparameter *pa = co->par;
	spell *sp = co->sp;

	switch(getplaneid(r)) {
	case 0:
		rt = r_standard_to_astral(r);
		ro = r;
		break;
	default:
		sprintf(buf, "%s in %s: 'ZAUBER %s': Dieser Zauber funktioniert "
				"nur in der materiellen Welt.", unitname(mage),
				regionname(mage->region, mage->faction), spell_name(sp, mage->faction->locale));
		addmessage(r, mage->faction, buf, MSG_MAGIC, ML_MISTAKE);
		return 0;
	}

	if(!rt) {
		sprintf(buf, "%s in %s: 'ZAUBER %s': Es kann hier kein Kontakt zur "
				"Astralwelt hergestellt werden.", unitname(mage),
				regionname(mage->region, mage->faction), spell_name(sp, mage->faction->locale));
		addmessage(r, mage->faction, buf, MSG_MAGIC, ML_MISTAKE);
		return 0;
	}

	if(is_cursed(rt->attribs, C_ASTRALBLOCK, 0) ||
			is_cursed(ro->attribs, C_ASTRALBLOCK, 0)) {
		sprintf(buf, "%s in %s: 'ZAUBER %s': Es kann kein Kontakt zu "
				"dieser astralen Region hergestellt werden.", unitname(mage),
				regionname(mage->region, mage->faction), spell_name(sp, mage->faction->locale));
		addmessage(r, mage->faction, buf, MSG_MAGIC, ML_MISTAKE);
		return 0;
	}

	remaining_cap = (int)((power-3) * 1500);

	/* f�r jede Einheit in der Kommandozeile */
	for (n = 0; n < pa->length; n++) {
		if(pa->param[n]->flag == TARGET_NOTFOUND) continue;
		u = pa->param[n]->data.u;

		if (!ucontact(u, mage)) {
			if (power > 10 && !is_magic_resistant(mage, u, 0)
					&& can_survive(u, rt)) {
				sprintf(buf, "%s hat uns nicht kontaktiert, widersteht dem "
						"Zauber jedoch nicht.", unitname(u));
				addmessage(r, mage->faction, buf, MSG_MAGIC, ML_INFO);
				sprintf(buf, "%s wird von %s in eine andere Welt geschleudert.",
					unitname(u),
					cansee(u->faction, r, mage, 0)?unitname(mage):"jemandem");
				addmessage(r, u->faction, buf, MSG_MAGIC, ML_WARN);
			} else {
				sprintf(buf, "%s hat uns nicht kontaktiert und widersteht dem "
						"Zauber.", unitname(u));
				addmessage(r, mage->faction, buf, MSG_MAGIC, ML_MISTAKE);
				sprintf(buf, "%s versucht, %s in eine andere Welt zu schleudern.",
					cansee(u->faction, r, mage, 0)?unitname(mage):"Jemand",
					unitname(u));
				addmessage(r, u->faction, buf, MSG_EVENT, ML_WARN);
				continue;
			}
		}

		w = weight(u);
		if(!can_survive(u, rt)) {
			cmistake(mage, co->order, 231, MSG_MAGIC);
		} else if(remaining_cap - w < 0) {
			addmessage(r, mage->faction, "Die Einheit ist zu schwer.",
					MSG_MAGIC, ML_MISTAKE);
		} else {
			remaining_cap = remaining_cap - w;
			move_unit(u, rt, NULL);

			/* Meldungen in der Ausgangsregion */

			for (u2 = r->units; u2; u2 = u2->next) freset(u2->faction, FL_DH);
			for(u2 = r->units; u2; u2 = u2->next ) {
				if(!fval(u2->faction, FL_DH)) {
					fset(u2->faction, FL_DH);
					if(cansee(u2->faction, r, u, 0)) {
						sprintf(buf, "%s wird durchscheinend und verschwindet.",
							unitname(u));
						addmessage(r, u2->faction, buf, MSG_EVENT, ML_INFO);
					}
				}
			}

			/* Meldungen in der Zielregion */

			for (u2 = rt->units; u2; u2 = u2->next) freset(u2->faction, FL_DH);
			for (u2 = rt->units; u2; u2 = u2->next ) {
				if(!fval(u2->faction, FL_DH)) {
					fset(u2->faction, FL_DH);
					if(cansee(u2->faction, rt, u, 0)) {
						sprintf(buf, "%s erscheint pl�tzlich.", unitname(u));
						addmessage(rt, u2->faction, buf, MSG_EVENT, ML_INFO);
					}
				}
			}
		}
	}
	return cast_level;
}

int
sp_pullastral(castorder *co)
{
  region *rt, *ro;
  unit *u, *u2;
  region_list *rl, *rl2;
  int remaining_cap;
  int n, w;
  region *r = co->rt;
  unit *mage = (unit *)co->magician;
  int cast_level = co->level;
  double power = co->force;
  spellparameter *pa = co->par;
  spell *sp = co->sp;

  switch (getplaneid(r)) {
    case 1:
      rt = r;
      ro = pa->param[0]->data.r;
      rl = astralregions(r, NULL);
      rl2 = rl;
      while (rl2!=NULL) {
        region * r2 = rl2->data;
        if (r2->x == ro->x && r2->y == ro->y) {
          ro = r2;
          break;
        }
        rl2 = rl2->next;
      }
      if(!rl2) {
        sprintf(buf, "%s in %s: 'ZAUBER %s': Es kann kein Kontakt zu "
          "dieser Region hergestellt werden.", unitname(mage),
          regionname(mage->region, mage->faction), spell_name(sp, mage->faction->locale));
        addmessage(r, mage->faction, buf, MSG_MAGIC, ML_MISTAKE);
        free_regionlist(rl);
        return 0;
      }
      free_regionlist(rl);
      break;
    default:
      sprintf(buf, "%s in %s: 'ZAUBER %s': Dieser Zauber funktioniert "
        "nur in der astralen  Welt.", unitname(mage),
        regionname(mage->region, mage->faction), spell_name(sp, mage->faction->locale));
      addmessage(r, mage->faction, buf, MSG_MAGIC, ML_MISTAKE);
      return 0;
  }

  if(is_cursed(rt->attribs, C_ASTRALBLOCK, 0) ||
    is_cursed(ro->attribs, C_ASTRALBLOCK, 0)) {
      sprintf(buf, "%s in %s: 'ZAUBER %s': Es kann kein Kontakt zu "
        "dieser Region hergestellt werden.", unitname(mage),
        regionname(mage->region, mage->faction), spell_name(sp, mage->faction->locale));
      addmessage(r, mage->faction, buf, MSG_MAGIC, ML_MISTAKE);
      return 0;
    }

    remaining_cap = (int)((power-3) * 1500);

    /* f�r jede Einheit in der Kommandozeile */
    for (n = 1; n < pa->length; n++) {
      if(pa->param[n]->flag == TARGET_NOTFOUND) continue;

      u = pa->param[n]->data.u;

      if (!ucontact(u, mage)) {
        if(power > 12 && pa->param[n]->flag != TARGET_RESISTS && can_survive(u, rt)) {
          sprintf(buf, "%s hat uns nicht kontaktiert, widersteht dem "
            "Zauber jedoch nicht.", unitname(u));
          addmessage(r, mage->faction, buf, MSG_MAGIC, ML_INFO);
          sprintf(buf, "%s wird von %s in eine andere Welt geschleudert.",
            unitname(u),
            cansee(u->faction, r, mage, 0)?unitname(mage):"jemandem");
          addmessage(r, u->faction, buf, MSG_MAGIC, ML_WARN);
        } else {
          sprintf(buf, "%s hat uns nicht kontaktiert und widersteht dem "
            "Zauber.", unitname(u));
          addmessage(r, mage->faction, buf, MSG_MAGIC, ML_MISTAKE);
          sprintf(buf, "%s versucht, %s in eine andere Welt zu schleudern.",
            cansee(u->faction, r, mage, 0)?unitname(mage):"Jemand",
            unitname(u));
          addmessage(r, u->faction, buf, MSG_EVENT, ML_WARN);
          continue;
        }
      }

      w = weight(u);

      if(!can_survive(u, rt)) {
        cmistake(mage, co->order, 231, MSG_MAGIC);
      } else if(remaining_cap - w < 0) {
        addmessage(r, mage->faction, "Die Einheit ist zu schwer.",
          MSG_MAGIC, ML_MISTAKE);
      } else {
        remaining_cap = remaining_cap - w;
        move_unit(u, rt, NULL);

        /* Meldungen in der Ausgangsregion */

        for (u2 = r->units; u2; u2 = u2->next) freset(u2->faction, FL_DH);
        for(u2 = r->units; u2; u2 = u2->next ) {
          if(!fval(u2->faction, FL_DH)) {
            fset(u2->faction, FL_DH);
            if(cansee(u2->faction, r, u, 0)) {
              sprintf(buf, "%s wird durchscheinend und verschwindet.",
                unitname(u));
              addmessage(r, u2->faction, buf, MSG_EVENT, ML_INFO);
            }
          }
        }

        /* Meldungen in der Zielregion */

        for (u2 = rt->units; u2; u2 = u2->next) freset(u2->faction, FL_DH);
        for(u2 = rt->units; u2; u2 = u2->next ) {
          if(!fval(u2->faction, FL_DH)) {
            fset(u2->faction, FL_DH);
            if(cansee(u2->faction, rt, u, 0)) {
              sprintf(buf, "%s erscheint pl�tzlich.", unitname(u));
              addmessage(rt, u2->faction, buf, MSG_EVENT, ML_INFO);
            }
          }
        }
      }
    }
    return cast_level;
}

int
sp_leaveastral(castorder *co)
{
  region *rt, *ro;
  region_list *rl, *rl2;
  unit *u, *u2;
  int remaining_cap;
  int n, w;
  region *r = co->rt;
  unit *mage = (unit *)co->magician;
  int cast_level = co->level;
  double power = co->force;
  spellparameter *pa = co->par;

  switch(getplaneid(r)) {
  case 1:
    ro = r;
    rt = pa->param[0]->data.r;
    if(!rt) {
      addmessage(r, mage->faction, "Dorthin f�hrt kein Weg.",
        MSG_MAGIC, ML_MISTAKE);
      return 0;
    }
    rl  = astralregions(r, inhabitable);
    rl2 = rl;
    while (rl2!=NULL) {
      if (rl2->data == rt) break;
      rl2 = rl2->next;
    }
    if (rl2==NULL) {
      addmessage(r, mage->faction, "Dorthin f�hrt kein Weg.",
        MSG_MAGIC, ML_MISTAKE);
      free_regionlist(rl);
      return 0;
    }
    free_regionlist(rl);
    break;
  default:
    sprintf(buf, "Der Zauber funktioniert nur in der astralen Welt.");
    addmessage(r, mage->faction, buf, MSG_MAGIC, ML_MISTAKE);
    return 0;
  }

  if (ro==NULL || is_cursed(ro->attribs, C_ASTRALBLOCK, 0) || is_cursed(rt->attribs, C_ASTRALBLOCK, 0)) {
    sprintf(buf, "Die Wege aus dieser astralen Region sind blockiert.");
    addmessage(r, mage->faction, buf, MSG_MAGIC, ML_MISTAKE);
    return 0;
  }

  remaining_cap = (int)((power-3) * 1500);

  /* f�r jede Einheit in der Kommandozeile */
  for (n = 1; n < pa->length; n++) {
    if(pa->param[n]->flag == TARGET_NOTFOUND) continue;

    u = pa->param[n]->data.u;

    if (!ucontact(u, mage)) {
      if (power > 10 && !pa->param[n]->flag == TARGET_RESISTS && can_survive(u, rt)) {
        sprintf(buf, "%s hat uns nicht kontaktiert, widersteht dem "
          "Zauber jedoch nicht.", unitname(u));
        addmessage(r, mage->faction, buf, MSG_MAGIC, ML_INFO);
        sprintf(buf, "%s wird von %s in eine andere Welt geschleudert.",
          unitname(u),
          cansee(u->faction, r, mage, 0)?unitname(mage):"jemandem");
        addmessage(r, u->faction, buf, MSG_EVENT, ML_WARN);
      } else {
        sprintf(buf, "%s hat uns nicht kontaktiert und widersteht dem "
          "Zauber.", unitname(u));
        addmessage(r, mage->faction, buf, MSG_MAGIC, ML_WARN);
        sprintf(buf, "%s versucht, %s in eine andere Welt zu schleudern.",
          cansee(u->faction, r, mage, 0)?unitname(mage):"Jemand",
          unitname(u));
        addmessage(r, u->faction, buf, MSG_EVENT, ML_WARN);
        continue;
      }
    }

    w = weight(u);

    if(!can_survive(u, rt)) {
      cmistake(mage, co->order, 231, MSG_MAGIC);
    } else if(remaining_cap - w < 0) {
      addmessage(r, mage->faction, "Die Einheit ist zu schwer.",
        MSG_MAGIC, ML_MISTAKE);
    } else {
      remaining_cap = remaining_cap - w;
      move_unit(u, rt, NULL);

      /* Meldungen in der Ausgangsregion */

      for (u2 = r->units; u2; u2 = u2->next) freset(u2->faction, FL_DH);
      for(u2 = r->units; u2; u2 = u2->next ) {
        if(!fval(u2->faction, FL_DH)) {
          fset(u2->faction, FL_DH);
          if(cansee(u2->faction, r, u, 0)) {
            sprintf(buf, "%s wird durchscheinend und verschwindet.",
              unitname(u));
            addmessage(r, u2->faction, buf, MSG_EVENT, ML_INFO);
          }
        }
      }

      /* Meldungen in der Zielregion */

      for (u2 = rt->units; u2; u2 = u2->next) freset(u2->faction, FL_DH);
      for (u2 = rt->units; u2; u2 = u2->next ) {
        if(!fval(u2->faction, FL_DH)) {
          fset(u2->faction, FL_DH);
          if(cansee(u2->faction, rt, u, 0)) {
            sprintf(buf, "%s erscheint pl�tzlich.", unitname(u));
            addmessage(rt, u2->faction, buf, MSG_EVENT, ML_INFO);
          }
        }
      }
    }
  }
  return cast_level;
}

int
sp_fetchastral(castorder *co)
{
  int n;
  unit *mage = (unit *)co->magician;
  int cast_level = co->level;
  spellparameter *pa = co->par;
  double power = co->force;
  int remaining_cap = (int)((power-3) * 1500);
  region_list * rtl = NULL;
  region * rt = co->rt; /* region to which we are fetching */
  region * ro = NULL; /* region in which the target is */

  if (rplane(rt)!=get_normalplane()) {
    ADDMSG(&mage->faction->msgs, msg_message("error190", 
      "command region unit", co->order, rt, mage));
    return 0;
  }

  if (is_cursed(rt->attribs, C_ASTRALBLOCK, 0)) {
    ADDMSG(&mage->faction->msgs, msg_message("spellfail_distance", 
      "command region unit", co->order, rt, mage));
    return 0;
  }

  /* f�r jede Einheit in der Kommandozeile */
  for (n=0; n!=pa->length; ++n) {
    unit * u = pa->param[n]->data.u;
    int w;

    if (pa->param[n]->flag & TARGET_NOTFOUND) continue;

    if (u->region!=ro) {
      /* this can happen several times if the units are from different astral
       * regions. Only possible on the intersections of schemes */
      region_list * rfind;
      if (getplane(u->region) != get_astralplane()) {
        cmistake(mage, co->order, 193, MSG_MAGIC);
        continue;
      }
      if (rtl!=NULL) free_regionlist(rtl);
      rtl = astralregions(u->region, NULL);
      for (rfind=rtl;rfind!=NULL;rfind=rfind->next) {
        if (rfind->data==mage->region) break;
      }
      if (rfind==NULL) {
        /* the region r is not in the schemes of rt */
        ADDMSG(&mage->faction->msgs, msg_message("spellfail_distance", 
          "command region unit target", co->order, mage->region, mage, u));
        continue;
      }
      ro = u->region;
    }

    if (is_cursed(rt->attribs, C_ASTRALBLOCK, 0)) {
      ADDMSG(&mage->faction->msgs, msg_message("spellfail_distance", 
        "command region unit", co->order, mage->region, mage));
      continue;
    }

    if (!can_survive(u, rt)) {
      cmistake(mage, co->order, 231, MSG_MAGIC);
      continue;
    } 

    w = weight(u);
    if (remaining_cap - w < 0) {
      ADDMSG(&mage->faction->msgs, msg_message("fail_tooheavy", 
        "command region unit target", co->order, mage->region, mage, u));
      continue;
    }

    if (!ucontact(u, mage)) {
      if (power>12 && !(pa->param[n]->flag & TARGET_RESISTS)) {
        sprintf(buf, "%s hat uns nicht kontaktiert, widersteht dem "
          "Zauber jedoch nicht.", unitname(u));
        addmessage(rt, mage->faction, buf, MSG_MAGIC, ML_INFO);
        sprintf(buf, "%s wird von %s in eine andere Welt geschleudert.",
          unitname(u), unitname(mage));
        addmessage(ro, u->faction, buf, MSG_EVENT, ML_WARN);
      } else {
        sprintf(buf, "%s hat uns nicht kontaktiert und widersteht dem "
          "Zauber.", unitname(u));
        addmessage(rt, mage->faction, buf, MSG_MAGIC, ML_WARN);
        sprintf(buf, "%s versucht, %s in eine andere Welt zu schleudern.",
          unitname(mage), unitname(u));
        addmessage(ro, u->faction, buf, MSG_EVENT, ML_WARN);
        continue;
      }
    }

    remaining_cap -= w;
    move_unit(u, rt, NULL);

    /* Meldungen in der Ausgangsregion */
    ADDMSG(&ro->msgs, msg_message("astral_disappear", "unit", u));

    /* Meldungen in der Zielregion */
    ADDMSG(&rt->msgs, msg_message("astral_appear", "unit", u));
  }
  if (rtl!=NULL) free_regionlist(rtl);
  return cast_level;
}

#ifdef SHOWASTRAL_NOT_BORKED
int
sp_showastral(castorder *co)
{
	unit *u;
	region *rt;
	int n = 0;
	int c = 0;
	region_list *rl, *rl2;
	region *r = co->rt;
	unit *mage = (unit *)co->magician;
	int cast_level = co->level;
	double power = co->force;

	switch(getplaneid(r)) {
	case 0:
		rt = r_standard_to_astral(r);
		if(!rt) {
			/* Hier gibt es keine Verbindung zur astralen Welt */
			cmistake(mage, co->order, 216, MSG_MAGIC);
			return 0;
		}
		break;
	case 1:
		rt = r;
		break;
	default:
		/* Hier gibt es keine Verbindung zur astralen Welt */
		cmistake(mage, co->order, 216, MSG_MAGIC);
		return 0;
	}

	rl = all_in_range(rt,power/5);

	/* Erst Einheiten z�hlen, f�r die Grammatik. */

	for(rl2=rl; rl2; rl2=rl2->next) {
		if(!is_cursed(rl2->data->attribs, C_ASTRALBLOCK, 0)) {
			for(u = rl2->data->units; u; u=u->next) {
				if (u->race != new_race[RC_SPECIAL] && u->race != new_race[RC_SPELL]) n++;
			}
		}
	}

	if(n == 0) {
		/* sprintf(buf, "%s kann niemanden im astralen Nebel entdecken.",
			unitname(mage)); */
		cmistake(mage, co->order, 220, MSG_MAGIC);
	} else {

		/* Ausgeben */

		sprintf(buf, "%s hat eine Vision der astralen Ebene. Im astralen "
			"Nebel zu erkennen sind ", unitname(mage));

		for(rl2=rl; rl2; rl2=rl2->next) {
			if(!is_cursed(rl2->data->attribs, C_ASTRALBLOCK, 0)) {
				for(u = rl2->data->units; u; u=u->next) {
					if(u->race != new_race[RC_SPECIAL] && u->race != new_race[RC_SPELL]) {
						c++;
						scat(unitname(u));
						scat(" (");
						if(!fval(u, UFL_PARTEITARNUNG)) {
							scat(factionname(u->faction));
							scat(", ");
						}
						icat(u->number);
						scat(" ");
						scat(LOC(mage->faction->locale, rc_name(u->race, u->number!=1)));
						scat(", Entfernung ");
						icat(distance(rl2->data, rt));
						scat(")");
						if(c == n-1) {
							scat(" und ");
						} else if(c < n-1) {
							scat(", ");
						}
					}
				}
			}
		}
		scat(".");
		addmessage(r, mage->faction, buf, MSG_MAGIC, ML_INFO);
	}

	free_regionlist(rl);
	return cast_level;
	unused(co);
	return 0;
}
#endif

/* ------------------------------------------------------------- */
int
sp_viewreality(castorder *co)
{
  region_list *rl, *rl2;
  unit *u;
  region *r = co->rt;
  unit *mage = (unit *)co->magician;
  int cast_level = co->level;

  if(getplaneid(r) != 1) {
    /* sprintf(buf, "Dieser Zauber kann nur im Astralraum gezaubert werden."); */
    cmistake(mage, co->order, 217, MSG_MAGIC);
    return 0;
  }

  if(is_cursed(r->attribs, C_ASTRALBLOCK, 0)) {
    /* sprintf(buf, "Die materielle Welt ist hier nicht sichtbar.");*/
    cmistake(mage, co->order, 218, MSG_MAGIC);
    return 0;
  }

  rl = astralregions(r, NULL);

  /* Irgendwann mal auf Curses u/o Attribut umstellen. */
  for (rl2=rl; rl2; rl2=rl2->next) {
    u = createunit(rl2->data, mage->faction, RS_FARVISION, new_race[RC_SPELL]);
    set_level(u, SK_OBSERVATION, co->level/2);
    set_string(&u->name, "Zauber: Blick in die Realit�t");
    u->age = 2;
  }

  free_regionlist(rl);

  sprintf(buf, "%s gelingt es, durch die Nebel auf die Realit�t zu blicken.",
    unitname(mage));
  addmessage(r, mage->faction, buf, MSG_MAGIC, ML_INFO);
  return cast_level;
}

/* ------------------------------------------------------------- */
int
sp_disruptastral(castorder *co)
{
	region_list *rl, *rl2;
	region *rt;
	unit *u;
	region *r = co->rt;
	unit *mage = (unit *)co->magician;
	int cast_level = co->level;
	double power = co->force;
	int duration = (int)(power/3)+1;

	switch(getplaneid(r)) {
	case 0:
		rt = r_standard_to_astral(r);
		if(!rt) {
			/* "Hier gibt es keine Verbindung zur astralen Welt." */
			cmistake(mage, co->order, 216, MSG_MAGIC);
			return 0;
		}
		break;
	case 1:
		rt = r;
		break;
	default:
		/* "Von hier aus kann man die astrale Ebene nicht erreichen." */
		cmistake(mage, co->order, 215, MSG_MAGIC);
		return 0;
	}

	rl = all_in_range(rt, (short)(power/5), NULL);

	for (rl2=rl; rl2!=NULL; rl2=rl2->next) {
		attrib *a, *a2;
    variant effect;
    region * r2 = rl2->data;
		spec_direction *sd;
    int inhab_regions = 0;
    region_list * trl = NULL;

		if (is_cursed(r2->attribs, C_ASTRALBLOCK, 0)) continue;

    if (r2->units!=NULL) {
      region_list * trl2;

      trl = astralregions(rl2->data, inhabitable);
      for (trl2 = trl; trl2; trl2 = trl2->next) ++inhab_regions;
    }

		/* Nicht-Permanente Tore zerst�ren */
		a = a_find(r->attribs, &at_direction);

		while (a!=NULL) {
			a2 = a->nexttype;
			sd = (spec_direction *)(a->data.v);
			if (sd->duration != -1) a_remove(&r->attribs, a);
			a = a2;
		}

		/* Einheiten auswerfen */

    if (trl!=NULL) {
      for (u=r2->units;u;u=u->next) {
			  if (u->race != new_race[RC_SPELL]) {
          region_list *trl2 = trl;
				  region *tr;
          int c = rand() % inhab_regions;

				  /* Zuf�llige Zielregion suchen */
          while (c--!=0) trl2 = trl2->next;
				  tr = trl2->data;

				  if(!is_magic_resistant(mage, u, 0) && can_survive(u, tr)) {
					  move_unit(u, tr, NULL);
					  sprintf(buf, "%s wird aus der astralen Ebene nach %s geschleudert.",
						  unitname(u), regionname(tr, u->faction));
					  addmessage(0, u->faction, buf, MSG_MAGIC, ML_INFO);
				  }
			  }
		  }
      free_regionlist(trl);
    }

		/* Kontakt unterbinden */
    effect.i = 100;
		create_curse(mage, &rl2->data->attribs, ct_find("astralblock"),
			power, duration, effect, 0);
		addmessage(r2, 0, "M�chtige Magie verhindert den Kontakt zur Realit�t.",
				MSG_COMMENT, ML_IMPORTANT);
	}

	free_regionlist(rl);
	return cast_level;
}

/* ------------------------------------------------------------- */
/* Name:	   Erschaffe einen Beutel des Negativen Gewichts
 * Stufe:	  10
 * Gebiet:	 Tybied
 * Kategorie:      Artefakt
 * Wirkung:	Von transportierten Items werden bis zu 200 GE nicht auf
 *		 das Traggewicht angerechnet.
*/
int
sp_create_bag_of_holding(castorder *co)
{
	unit *mage = (unit *)co->magician;
	int cast_level = co->level;

	change_item(mage,I_BAG_OF_HOLDING,1);

	creation_message(mage, I_BAG_OF_HOLDING);
	return cast_level;
}

/* ------------------------------------------------------------- */
/* Name:		   Mauern der Ewigkeit
 * Stufe:		   7
 * Kategorie:  Artefakt
 * Gebiet:     Tybied
 * Wirkung:
 * Das Geb�ude kostet keinen Unterhalt mehr
 *
 * ZAUBER "Mauern der Ewigkeit" <geb�ude-nummer>
 * Flags: (0)
 */
static int
sp_eternizewall(castorder *co)
{
	unit *u;
	curse * c;
	building *b;
	region *r = co->rt;
	unit *mage = (unit *)co->magician;
	int cast_level = co->level;
	double power = co->force;
	spellparameter *pa = co->par;

	/* wenn kein Ziel gefunden, Zauber abbrechen */
	if(pa->param[0]->flag == TARGET_NOTFOUND) return 0;

	b = pa->param[0]->data.b;
	c = create_curse(mage, &b->attribs, ct_find("nocost"),
		power*power, 1, zero_effect, 0);

	if(c==NULL) {	/* ist bereits verzaubert */
		cmistake(mage, co->order, 206, MSG_MAGIC);
		return 0;
	}

	curse_setflag(c, CURSE_NOAGE|CURSE_ONLYONE);

	/* melden, 1x pro Partei in der Burg */
	for (u = r->units; u; u = u->next) freset(u->faction, FL_DH);
	for (u = r->units; u; u = u->next) {
		if (!fval(u->faction, FL_DH)) {
			fset(u->faction, FL_DH);
			if (u->building ==  b) {
				sprintf(buf, "Mit einem Ritual bindet %s die magischen Kr�fte "
						"der Erde in die Mauern von %s", unitname(mage),
						buildingname(b));
				addmessage(r, u->faction, buf, MSG_EVENT, ML_INFO);
			}
		}
	}

	return cast_level;
}


/* ------------------------------------------------------------- */
/* Name:       Opfere Kraft
 * Stufe:      15
 * Gebiet:     Tybied
 * Kategorie:  Einheit, positiv
 * Wirkung:
 *   Mit Hilfe dieses Zaubers kann der Magier einen Teil seiner
 *   magischen Kraft permanent auf einen anderen Magier �bertragen. Auf
 *   einen Tybied-Magier kann er die H�lfte der eingesetzten Kraft
 *   �bertragen, auf einen Magier eines anderen Gebietes ein Drittel.
 *
 * Flags:
 * (UNITSPELL|ONETARGET)
 *
 * Syntax:
 * ZAUBERE \"Opfere Kraft\" <Einheit-Nr> <Aura>
 * "ui"
 */
int
sp_permtransfer(castorder *co)
{
	int aura;
	unit *tu;
	region *r = co->rt;
	unit *mage = (unit *)co->magician;
	int cast_level = co->level;
	spellparameter *pa = co->par;
	spell *sp = co->sp;

	/* wenn kein Ziel gefunden, Zauber abbrechen */
	if(pa->param[0]->flag == TARGET_NOTFOUND) return 0;

	/* wenn Ziel gefunden, dieses aber Magieresistent war, Zauber
	 * abbrechen aber kosten lassen */
	if(pa->param[0]->flag == TARGET_RESISTS) return cast_level;

	tu = pa->param[0]->data.u;
	aura = pa->param[1]->data.i;

	if(!is_mage(tu)) {
/*		sprintf(buf, "%s in %s: 'ZAUBER %s': Einheit ist kein Magier."
			, unitname(mage), regionname(mage->region, mage->faction),sa->strings[0]); */
		cmistake(mage, co->order, 214, MSG_MAGIC);
		return 0;
	}

  aura = min(get_spellpoints(mage)-spellcost(mage, sp), aura);

	change_maxspellpoints(mage,-aura);
	change_spellpoints(mage,-aura);

	if(get_mage(tu)->magietyp == get_mage(mage)->magietyp) {
		change_maxspellpoints(tu, aura/2);
	} else {
		change_maxspellpoints(tu, aura/3);
	}

	sprintf(buf, "%s opfert %s %d Aura.", unitname(mage), unitname(tu), aura);
	addmessage(r, mage->faction, buf, MSG_MAGIC, ML_INFO);

	return cast_level;
}

/* ------------------------------------------------------------- */
/* TODO: specialdirections? */

int
sp_movecastle(castorder *co)
{
	building *b;
	direction_t dir;
	region *target_region;
	unit *u, *unext;
	region *r = co->rt;
	unit *mage = (unit *)co->magician;
	int cast_level = co->level;
	spellparameter *pa = co->par;
	spell *sp = co->sp;

	/* wenn kein Ziel gefunden, Zauber abbrechen */
	if(pa->param[0]->flag == TARGET_NOTFOUND) return 0;

	b = pa->param[0]->data.b;
	dir = finddirection(pa->param[1]->data.s, mage->faction->locale);

	if(dir == NODIRECTION) {
		sprintf(buf, "%s in %s: 'ZAUBER %s': Ung�ltige Richtung %s.",
			unitname(mage), regionname(mage->region, mage->faction),
			spell_name(sp, mage->faction->locale),
			pa->param[1]->data.s);
		addmessage(0, mage->faction, buf, MSG_MAGIC, ML_MISTAKE);
		return 0;
	}

	if(b->size > (cast_level-12) * 250) {
		sprintf(buf, "%s in %s: 'ZAUBER %s': Der Elementar ist "
			"zu klein, um das Geb�ude zu tragen.", unitname(mage),
			regionname(mage->region, mage->faction), spell_name(sp, mage->faction->locale));
		addmessage(0, mage->faction, buf, MSG_MAGIC, ML_MISTAKE);
		return cast_level;
	}

	target_region = rconnect(r,dir);

	if(!(terrain[target_region->terrain].flags & LAND_REGION)) {
		sprintf(buf, "%s in %s: 'ZAUBER %s': Der Erdelementar "
				"weigert sich, nach %s zu gehen.",
			unitname(mage), regionname(mage->region, mage->faction),
			spell_name(sp, mage->faction->locale),
			locale_string(mage->faction->locale, directions[dir]));
		addmessage(0, mage->faction, buf, MSG_MAGIC, ML_MISTAKE);
		return cast_level;
	}

	bunhash(b);
	translist(&r->buildings, &target_region->buildings, b);
	b->region = target_region;
	b->size -= b->size/(10-rand()%6);
	bhash(b);

	for(u=r->units;u;) {
		unext = u->next;
		if(u->building == b) {
			uunhash(u);
			translist(&r->units, &target_region->units, u);
			uhash(u);
		}
		u = unext;
	}

	sprintf(buf, "Ein Beben ersch�ttert %s. Viele kleine Pseudopodien "
		"erheben das Geb�ude und tragen es in Richtung %s.",
		buildingname(b), locale_string(mage->faction->locale, directions[dir]));

	if((b->type==bt_find("caravan") || b->type==bt_find("dam") || b->type==bt_find("tunnel"))) {
		boolean damage = false;
		direction_t d;
		for (d=0;d!=MAXDIRECTIONS;++d) {
			if (rroad(r, d)) {
				rsetroad(r, d, rroad(r, d)/2);
				damage = true;
			}
		}
		if (damage) strcat(buf, " Die Stra�en der Region wurden besch�digt.");
	}
	addmessage(r, 0, buf, MSG_MAGIC, ML_INFO);

	return cast_level;
}

/* ------------------------------------------------------------- */
/* Name:       Luftschiff
 * Stufe:      6
 *
 * Wirkung:
 * L��t ein Schiff eine Runde lang fliegen.  Wirkt nur auf Boote und
 * Langboote.
 * Kombinierbar mit "G�nstige Winde", aber nicht mit "Sturmwind".
 *
 * Flag:
 *  (ONSHIPCAST | SHIPSPELL | ONETARGET | TESTRESISTANCE)
 */
int
sp_flying_ship(castorder *co)
{
	ship *sh;
	unit *u;
	region *r = co->rt;
	unit *mage = (unit *)co->magician;
	int cast_level = co->level;
	double power = co->force;
	spellparameter *pa = co->par;

	/* wenn kein Ziel gefunden, Zauber abbrechen */
	if(pa->param[0]->flag == TARGET_NOTFOUND) return 0;

	sh = pa->param[0]->data.sh;

	if(is_cursed(sh->attribs, C_SHIP_FLYING, 0) ) {
/*		sprintf(buf, "Auf dem Schiff befindet liegt bereits so ein Zauber."); */
		cmistake(mage, co->order, 211, MSG_MAGIC);
		return 0;
	}
	if(is_cursed(sh->attribs, C_SHIP_SPEEDUP, 0) ) {
/*		sprintf(buf, "Es ist zu gef�hrlich, ein sturmgepeitschtes Schiff "
				"fliegen zu lassen."); */
		cmistake(mage, co->order, 210, MSG_MAGIC);
		return 0;
	}
	/* mit C_SHIP_NODRIFT haben wir kein Problem */

	/* Duration = 1, nur diese Runde */
	create_curse(mage, &sh->attribs, ct_find("flyingship"), power, 1, zero_effect, 0);
	/* Da der Spruch nur diese Runde wirkt, brauchen wir kein
	 * set_cursedisplay() zu benutzten - es sieht eh niemand...
	 */
	sh->coast = NODIRECTION;

	/* melden, 1x pro Partei */
	for (u = r->units; u; u = u->next) freset(u->faction, FL_DH);
	for(u = r->units; u; u = u->next ) {
		/* das sehen nat�rlich auch die Leute an Land */
		if(!fval(u->faction, FL_DH) ) {
			fset(u->faction, FL_DH);
			sprintf(buf, "%s beschw�rt einen Luftgeist, der die %s in "
					"die Wolken hebt.",
				cansee(u->faction, r, mage, 0) ? unitname(mage) : "Jemand",
				shipname(sh));
			addmessage(r, u->faction, buf, MSG_EVENT, ML_INFO);
		}
	}
	return cast_level;
}


/* ------------------------------------------------------------- */
/* Name:       Stehle Aura
 * Stufe:      6
 * Kategorie:  Einheit, negativ
 * Wirkung:
 *     Mit Hilfe dieses Zaubers kann der Magier einem anderen Magier
 *     seine Aura gegen dessen Willen entziehen und sich selber
 *     zuf�hren.
 *
 * Flags:
 * (FARCASTING | SPELLLEVEL | UNITSPELL | ONETARGET | TESTRESISTANCE |
 * TESTCANSEE)
 * */
int
sp_stealaura(castorder *co)
{
	int taura;
	unit *u;
	unit *mage = (unit *)co->magician;
	int cast_level = co->level;
	double power = co->force;
	spellparameter *pa = co->par;

	/* wenn kein Ziel gefunden, Zauber abbrechen */
	if(pa->param[0]->flag == TARGET_NOTFOUND) return 0;

	/* Zieleinheit */
	u  = pa->param[0]->data.u;

	if(!get_mage(u)) {
		ADDMSG(&mage->faction->msgs, msg_message(
			"stealaura_fail", "unit target", mage, u));
		ADDMSG(&u->faction->msgs, msg_message(
			"stealaura_fail_detect", "unit", u));
		return 0;
	}

	taura = (get_mage(u)->spellpoints*(rand()%(int)(3*power)+1))/100;

	if(taura > 0) {
		get_mage(u)->spellpoints -= taura;
		get_mage(mage)->spellpoints += taura;
/*		sprintf(buf, "%s entzieht %s %d Aura.", unitname(mage), unitname(u),
			taura); */
		ADDMSG(&mage->faction->msgs, msg_message(
			"stealaura_success", "mage target aura", mage, u, taura));
/*		sprintf(buf, "%s f�hlt seine magischen Kr�fte schwinden und verliert %d "
			"Aura.", unitname(u), taura); */
		ADDMSG(&u->faction->msgs, msg_message(
			"stealaura_detect", "unit aura", u, taura));
	} else {
		ADDMSG(&mage->faction->msgs, msg_message(
			"stealaura_fail", "unit target", mage, u));
		ADDMSG(&u->faction->msgs, msg_message(
			"stealaura_fail_detect", "unit", u));
	}
	return cast_level;
}

/* ------------------------------------------------------------- */
/* Name:       Erschaffe Antimagiekristall
 * Stufe:      7
 * Kategorie:  Artefakt
 * Wirkung:
 *	  Erzeugt Antimagiekristall
 */
int
sp_create_antimagiccrystal(castorder *co)
{
	unit *mage = (unit *)co->magician;
	int cast_level = co->level;

	change_item(mage,I_ANTIMAGICCRYSTAL,1);
	creation_message(mage, I_ANTIMAGICCRYSTAL);
	return cast_level;
}

/* ------------------------------------------------------------- */
/* Name:       Astrale Schw�chezone
 * Stufe:      5
 * Kategorie:
 * Wirkung:
 *    Reduziert die St�rke jedes Spruch in der Region um Level H�lt
 *    Spr�che bis zu einem Gesammtlevel von St�rke*10 aus, danach ist
 *    sie verbraucht.
 *    leibt bis zu St�rke Wochen aktiv.
 *    Ein Ring der Macht erh�ht die St�rke um 1, in einem Magierturm
 *    gezaubert gibt nochmal +1 auf St�rke. (force)
 *
 *    Beispiel:
 *    Eine Antimagiezone Stufe 7 h�lt bis zu 7 Wochen an oder Spr�che mit
 *    einem Gesammtlevel bis zu 70 auf. Also zB 7 Stufe 10 Spr�che, 10
 *    Stufe 7 Spr�che oder 35 Stufe 2 Spr�che.  Sie reduziert die St�rke
 *    (level+boni) jedes Spruchs, der in der Region gezaubert wird, um
 *    7. Alle Spr�che mit einer St�rke kleiner als 7 schlagen fehl
 *    (power = 0).
 *
 * Flags:
 * (FARCASTING | SPELLLEVEL | REGIONSPELL | TESTRESISTANCE)
 *
 */
int
sp_antimagiczone(castorder *co)
{
	double power;
	variant effect;
	region *r = co->rt;
	unit *mage = (unit *)co->magician;
	int cast_level = co->level;
	double force = co->force;
	int duration = (int)force+1;

	/* H�lt Spr�che bis zu einem summierten Gesamtlevel von power aus.
	 * Jeder Zauber reduziert die 'Lebenskraft' (vigour) der Antimagiezone
	 * um seine Stufe */
	power = force * 10;

	/* Reduziert die St�rke jedes Spruchs um effect */
	effect.i = cast_level;

	create_curse(mage, &r->attribs, ct_find("antimagiczone"), power, duration,
			effect, 0);

	/* Erfolg melden*/
	ADDMSG(&mage->faction->msgs, msg_message(
				"regionmagic_effect", "unit region command", mage,
				mage->region, co->order));

	return cast_level;
}

/* ------------------------------------------------------------- */
/* Name:	   Schutzrunen
 * Stufe:	  8
 * Kosten:	 SPC_FIX
 *
 * Wirkung:
 *   Gibt Geb�uden einen Bonus auf Magieresistenz von +20%. Der Zauber
 *   dauert 3+rand()%Level Wochen an, also im Extremfall bis zu 2 Jahre
 *   bei Stufe 20
 *
 *   Es k�nnen mehrere Zauber �bereinander gelegt werden, der Effekt
 *   summiert sich, jedoch wird die Dauer dadurch nicht verl�ngert.
 *
 * oder:
 *
 *   Gibt Schiffen einen Bonus auf Magieresistenz von +20%. Der Zauber
 *   dauert 3+rand()%Level Wochen an, also im Extremfall bis zu 2 Jahre
 *   bei Stufe 20
 *
 *   Es k�nnen mehrere Zauber �bereinander gelegt werden, der Effekt
 *   summiert sich, jedoch wird die Dauer dadurch nicht verl�ngert.
 *
 * Flags:
 * (ONSHIPCAST | TESTRESISTANCE)
 *
 * Syntax:
 *  ZAUBERE \"Runen des Schutzes\" GEB�UDE <Geb�ude-Nr>
 *  ZAUBERE \"Runen des Schutzes\" SCHIFF <Schiff-Nr>
 * "kc"
 */

static int
sp_magicrunes(castorder *co)
{
	int duration;
	unit *mage = (unit *)co->magician;
	int cast_level = co->level;
	double force = co->force;
	spellparameter *pa = co->par;
  variant effect;

	duration = 3 + rand()%cast_level;
  effect.i = 20;

	switch(pa->param[0]->typ){
		case SPP_BUILDING:
		{
			building *b;
			b = pa->param[0]->data.b;

			/* Magieresistenz der Burg erh�ht sich um 20% */
			create_curse(mage, &b->attribs, ct_find("magicrunes"), force,
					duration, effect, 0);

			/* Erfolg melden */
			ADDMSG(&mage->faction->msgs, msg_message(
					"objmagic_effect", "unit region command target", mage,
					mage->region, co->order, buildingname(b)));
			break;
		}
		case SPP_SHIP:
		{
			ship *sh;
			sh = pa->param[0]->data.sh;
			/* Magieresistenz des Schiffs erh�ht sich um 20% */
			create_curse(mage, &sh->attribs, ct_find("magicrunes"), force,
					duration, effect, 0);

			/* Erfolg melden */
			ADDMSG(&mage->faction->msgs, msg_message(
					"objmagic_effect", "unit region command target", mage,
					mage->region, co->order, shipname(sh)));
			break;
		}
		default:
		/* fehlerhafter Parameter */
		return 0;
	}

	return cast_level;
}

/* ------------------------------------------------------------- */
/* Name:   Zeitdehnung
 *
 * Flags:
 * (UNITSPELL | SPELLLEVEL | ONSHIPCAST | TESTCANSEE)
 * Syntax:
 *  "u+"
 */

int
sp_speed2(castorder *co)
{
	int n, maxmen, used = 0, dur, men;
	unit *u;
	unit *mage = (unit *)co->magician;
	int cast_level = co->level;
	double force = co->force;
	spellparameter *pa = co->par;

	maxmen = 2 * cast_level * cast_level;
	dur = max(1, cast_level/2);

	for (n = 0; n < pa->length; n++) {
    variant effect;
		/* sollte nie negativ werden */
		if (maxmen < 1)
			break;

		if(pa->param[n]->flag == TARGET_RESISTS
				|| pa->param[n]->flag == TARGET_NOTFOUND)
			continue;

		u = pa->param[n]->data.u;

		men = min(maxmen,u->number);
    effect.i = 2;
		create_curse(mage, &u->attribs, ct_find("speed"), force, dur, effect, men);
		maxmen -= men;
		used += men;
	}

	/* TODO: Erfolg melden*/
	/* Effektiv ben�tigten cast_level (mindestens 1) zur�ckgeben */
	used = (int)sqrt(used/2);
	return max(1, used);
}

/* ------------------------------------------------------------- */
/* Name:	   Magiefresser
 * Stufe:	  7
 * Kosten:	 SPC_LEVEL
 *
 * Wirkung:
 *   Kann eine bestimmte Verzauberung angreifen und aufl�sen. Die St�rke
 *   des Zaubers muss st�rker sein als die der Verzauberung.
 * Syntax:
 *  ZAUBERE \"Magiefresser\" REGION
 *  ZAUBERE \"Magiefresser\" EINHEIT <Einheit-Nr>
 *  ZAUBERE \"Magiefresser\" GEB�UDE <Geb�ude-Nr>
 *  ZAUBERE \"Magiefresser\" SCHIFF <Schiff-Nr>
 *
 *  "kc?c"
 * Flags:
 *   (FARCASTING | SPELLLEVEL | ONSHIPCAST | TESTCANSEE)
 */
/* Jeder gebrochene Zauber verbraucht c->vigour an Zauberkraft
 * (force) */
int
sp_q_antimagie(castorder *co)
{
	attrib **ap;
	int obj;
	curse * c = NULL;
	int succ;
	region *r = co->rt;
	unit *mage = (unit *)co->magician;
	int cast_level = co->level;
	double force = co->force;
	spellparameter *pa = co->par;
	char *ts;

	obj = pa->param[0]->typ;

	switch(obj){
		case SPP_REGION:
			ap = &r->attribs;
			set_string(&ts, regionname(r, mage->faction));
			break;

    case SPP_TEMP:
		case SPP_UNIT:
		{
			unit *u = pa->param[0]->data.u;
			ap = &u->attribs;
			set_string(&ts, unitid(u));
			break;
		}
		case SPP_BUILDING:
		{
			building *b = pa->param[0]->data.b;
			ap = &b->attribs;
			set_string(&ts, buildingid(b));
			break;
		}
		case SPP_SHIP:
		{
			ship *sh = pa->param[0]->data.sh;
			ap = &sh->attribs;
			set_string(&ts, shipid(sh));
			break;
		}
		default:
			/* Das Zielobjekt wurde vergessen */
			cmistake(mage, co->order, 203, MSG_MAGIC);
		return 0;
	}

	succ = destroy_curse(ap, cast_level, force, c);

	if (succ) {
		ADDMSG(&mage->faction->msgs, msg_message(
			"destroy_magic_effect", "unit region command succ target",
			mage, mage->region, co->order, succ, strdup(ts)));
	} else {
		ADDMSG(&mage->faction->msgs, msg_message(
			"destroy_magic_noeffect", "unit region command",
			mage, mage->region, co->order));
	}

	return max(succ, 1);
}

/* ------------------------------------------------------------- */
/* Name:	   Fluch brechen
 * Stufe:	  7
 * Kosten:	 SPC_LEVEL
 *
 * Wirkung:
 *   Kann eine bestimmte Verzauberung angreifen und aufl�sen. Die St�rke
 *   des Zaubers muss st�rker sein als die der Verzauberung.
 * Syntax:
 *  ZAUBERE \"Fluch brechen\" REGION <Zauber-id>
 *  ZAUBERE \"Fluch brechen\" EINHEIT <Einheit-Nr> <Zauber-id>
 *  ZAUBERE \"Fluch brechen\" GEB�UDE <Geb�ude-Nr> <Zauber-id>
 *  ZAUBERE \"Fluch brechen\" SCHIFF <Schiff-Nr> <Zauber-id>
 *
 *  "kcc"
 * Flags:
 *   (FARCASTING | SPELLLEVEL | ONSHIPCAST | TESTCANSEE)
 */
int
sp_destroy_curse(castorder *co)
{
	attrib **ap;
	int obj;
	curse * c;
	region *r = co->rt;
	unit *mage = (unit *)co->magician;
	int cast_level = co->level;
	double force = co->force;
	spellparameter *pa = co->par;
	char *ts = NULL;

	if(pa->length < 2){
		/* Das Zielobjekt wurde vergessen */
		cmistake(mage, co->order, 203, MSG_MAGIC);
	}

	obj = pa->param[0]->typ;

	c = findcurse(atoi36(pa->param[1]->data.s));
	if (!c){
		/* Es wurde kein Ziel gefunden */
		ADDMSG(&mage->faction->msgs, msg_message(
					"spelltargetnotfound", "unit region command",
					mage, mage->region, co->order));
	} else {
		switch(obj){
		case SPP_REGION:
			ap = &r->attribs;
			set_string(&ts, regionname(r, mage->faction));
			break;

    case SPP_TEMP:
		case SPP_UNIT:
		{
			unit *u = pa->param[0]->data.u;
			ap = &u->attribs;
			set_string(&ts, unitid(u));
			break;
		}
		case SPP_BUILDING:
		{
			building *b = pa->param[0]->data.b;
			ap = &b->attribs;
			set_string(&ts, buildingid(b));
			break;
		}
		case SPP_SHIP:
		{
			ship *sh = pa->param[0]->data.sh;
			ap = &sh->attribs;
			set_string(&ts, shipid(sh));
			break;
		}
		default:
			/* Das Zielobjekt wurde vergessen */
			cmistake(mage, co->order, 203, MSG_MAGIC);
			return 0;
		}

		/* �berpr�fung, ob curse zu diesem objekt geh�rt */
		if (!is_cursed_with(*ap, c)){
			/* Es wurde kein Ziel gefunden */
			ADDMSG(&mage->faction->msgs,
						msg_message(
									"spelltargetnotfound", "unit region command",
									mage, mage->region, co->order));
		}

		/* curse aufl�sen, wenn zauber st�rker (force > vigour)*/
		c->vigour -= force;

		if (c->vigour <= 0.0) {
			remove_curse(ap, c);

			ADDMSG(&mage->faction->msgs, msg_message(
				"destroy_magic_effect", "unit region command id target",
				mage, mage->region, co->order, strdup(pa->param[1]->data.s),
				strdup(ts)));
		} else {
			ADDMSG(&mage->faction->msgs, msg_message(
				"destroy_magic_noeffect", "unit region command",
				mage, mage->region, co->order));
		}
	}
	if (ts != NULL) free(ts);

	return cast_level;
}


/* ------------------------------------------------------------- */
int
sp_becomewyrm(castorder *co)
{
  unit *u = (unit *)co->magician;
  int wyrms_already_created = 0;
  int wyrms_allowed;
  attrib *a;

  wyrms_allowed = fspecial(u->faction, FS_WYRM);
  a = a_find(u->faction->attribs, &at_wyrm);
  if(a) wyrms_already_created = a->data.i;

  if(wyrms_already_created >= wyrms_allowed) {
    cmistake(u, co->order, 262, MSG_MAGIC);
    return 0;
  }

  if(!a) {
    a_add(&u->faction->attribs, a_new(&at_wyrm));
    a->data.i = 1;
  } else {
    a->data.i++;
  }

  u->race = new_race[RC_WYRM];
  add_spell(get_mage(u), SPL_WYRMODEM);

  ADDMSG(&u->faction->msgs, msg_message("becomewyrm", "u", u));

  return co->level;
}

#ifdef WDW_PYRAMIDSPELL
/* ------------------------------------------------------------- */
/* Name:       WDW-Pyramidenfindezauber
 * Stufe:      unterschiedlich
 * Gebiet:     alle
 * Wirkung:
 *             gibt die ungefaehre Entfernung zur naechstgelegenen Pyramiden-
 *             region an.
 *
 * Flags:
 */
static int
sp_wdwpyramid(castorder *co)
{
	region  *r          = co->rt;
	unit    *mage       = (unit *)co->magician;
	int     cast_level  = co->level;

	if(a_find(r->attribs, &at_wdwpyramid) != NULL) {
		ADDMSG(&mage->faction->msgs, msg_message("wdw_pyramidspell_found",
			"unit region command", mage, r, co->order));
	} else {
		region *r2;
		int    mindist = INT_MAX;
		int    minshowdist;
		int    maxshowdist;

		for(r2 = regions; r2; r2 = r2->next) {
			if(a_find(r2->attribs, &at_wdwpyramid) != NULL) {
				int dist = distance(mage->region, r2);
				if (dist < mindist) {
					mindist = dist;
				}
			}
		}

		assert(mindist >= 1);

		minshowdist = mindist - rand()%5;
		maxshowdist = minshowdist + 4;

		ADDMSG(&mage->faction->msgs, msg_message("wdw_pyramidspell_notfound",
			"unit region command mindist maxdist", mage, r, co->order,
			max(1, minshowdist), maxshowdist));
	}

	return cast_level;
}
#endif

/* ------------------------------------------------------------- */
/* Name:       Alltagszauber, hat je nach Gebiet anderen Namen
 * Stufe:      1
 * Gebiet:     alle
 * Wirkung:		 der Magier verdient $50 pro Spruchstufe
 * Kosten:		 1 SP pro Stufe
 */
#include "../gamecode/economy.h"
/* TODO: das ist scheisse, aber spells geh�ren eh nicht in den kernel */
int
sp_earn_silver(castorder *co)
{
	unit *mage = (unit *)co->magician;
	double force = co->force;
	region *r = co->rt;
	int wanted = (int)(force * 50);
	int earned = min(rmoney(r), wanted);

  rsetmoney(r, rmoney(r) - earned);
	change_money(mage, earned);
	/* TODO kl�ren: ist das Silber damit schon reserviert? */

	add_income(mage, IC_MAGIC, wanted, earned);
	return co->level;
}


/* ------------------------------------------------------------- */
/* Dummy-Zauberpatzer, Platzhalter f�r speziel auf die Spr�che
 * zugeschnittene Patzer */
void
patzer(castorder *co)
{
	unit *mage = (unit *)co->magician;

	report_failure(mage, co->order);
	return;
}

/* ------------------------------------------------------------- */
/* allgemeine Artefakterschaffungszauber (Gebietsunspezifisch)   */
/* ------------------------------------------------------------- */
/* Amulett des wahren Sehens */
int
sp_createitem_trueseeing(castorder *co)
{
	unit *mage = (unit *)co->magician;
	int cast_level = co->level;
	unit *familiar = (unit *)co->familiar;

	if (familiar){
		mage = familiar;
	}

	change_item(mage,I_AMULET_OF_TRUE_SEEING,1);
	creation_message(mage, I_AMULET_OF_TRUE_SEEING);

	return cast_level;
}

/* ------------------------------------------------------------- */
/* Ring der Unsichtbarkeit */
int
sp_createitem_invisibility(castorder *co)
{
	unit *mage = (unit *)co->magician;
	int cast_level = co->level;
	unit *familiar = (unit *)co->familiar;

	if (familiar){
		mage = familiar;
	}

	change_item(mage,I_RING_OF_INVISIBILITY,1);
	creation_message(mage, I_RING_OF_INVISIBILITY);

	return cast_level;
}

/* ------------------------------------------------------------- */
/* Sph�re der Unsichtbarkeit */
int
sp_createitem_invisibility2(castorder *co)
{
	unit *mage = (unit *)co->magician;
	int cast_level = co->level;
	unit *familiar = (unit *)co->familiar;

	if (familiar){
		mage = familiar;
	}

	change_item(mage,I_SPHERE_OF_INVISIBILITY,1);
	creation_message(mage, I_SPHERE_OF_INVISIBILITY);

	return cast_level;
}

/* ------------------------------------------------------------- */
/* Keuschheitsg�rtel der Orks */
int
sp_createitem_chastitybelt(castorder *co)
{
	unit *mage = (unit *)co->magician;
	int cast_level = co->level;
	unit *familiar = (unit *)co->familiar;

	if (familiar){
		mage = familiar;
	}

	change_item(mage,I_CHASTITY_BELT,1);
	creation_message(mage, I_CHASTITY_BELT);

	return cast_level;
}
/* ------------------------------------------------------------- */
/* Ring der Macht
 * erh�ht effektive Stufe +1 */
int
sp_createitem_power(castorder *co)
{
	unit *mage = (unit *)co->magician;
	int cast_level = co->level;
	unit *familiar = (unit *)co->familiar;

	if (familiar){
		mage = familiar;
	}

	change_item(mage,I_RING_OF_POWER,1);
	creation_message(mage, I_RING_OF_POWER);

	return cast_level;
}
/* ------------------------------------------------------------- */
/* Runenschwert */
int
sp_createitem_runesword(castorder *co)
{
	unit *mage = (unit *)co->magician;
	int cast_level = co->level;
	unit *familiar = (unit *)co->familiar;

	if (familiar){
		mage = familiar;
	}

	change_item(mage,I_RUNESWORD,1);
	creation_message(mage, I_RUNESWORD);

	return cast_level;
}
/* ------------------------------------------------------------- */
/* Artefakt der St�rke
 * Erm�glicht dem Magier mehr Magiepunkte zu 'speichern'
 */
int
sp_createitem_aura(castorder *co)
{
	unit *mage = (unit *)co->magician;
	int cast_level = co->level;
	unit *familiar = (unit *)co->familiar;

	if (familiar){
		mage = familiar;
	}

	change_item(mage,I_AURAKULUM,1);
	creation_message(mage, I_AURAKULUM);

	return cast_level;
}

/* ------------------------------------------------------------- */
/* Artefakt der Regeneration
 * Heilt pro Runde HP
 * noch nicht implementiert
 */
int
sp_createitem_regeneration(castorder *co)
{
	unit *mage = (unit *)co->magician;
	int cast_level = co->level;
	unit *familiar = (unit *)co->familiar;

	if (familiar){
		mage = familiar;
	}

	change_item(mage,I_RING_OF_REGENERATION,1);
	creation_message(mage, I_RING_OF_REGENERATION);

	return cast_level;
}

/* ------------------------------------------------------------- */
/* Dummy-Zauberpatzer, Platzhalter f�r speziel auf die Spr�che
 * zugeschnittene Patzer */
void
patzer_createitem(castorder *co)
{
	unit *mage = (unit *)co->magician;

	report_failure(mage, co->order);
	return;
}

/* ------------------------------------------------------------- */
/* Erl�uterungen zu den Spruchdefinitionen
 *
 * Spruchstukturdefinition:
 * spell{
 *  id, name,
 *  beschreibung,
 *  syntax,
 *  parameter,
 *  magietyp,
 *  sptyp,
 *  rank,level,
 *  costtyp, aura,
 *  komponenten[5][2][faktorart],
 *  &funktion, patzer}
 *
 * id:
 * SPL_NOSPELL muss der letzte Spruch in der Liste spelldaten sein,
 * denn nicht auf die Reihenfolge in der Liste sondern auf die id wird
 * gepr�ft
 *
 * sptyp:
 * besondere Spruchtypen und Flags
 *    (Regionszauber, Kampfzauber, Farcastbar, Stufe variable, ..)
 *
 * rank:
 * gibt die Priorit�t und damit die Reihenfolge an, in der der Spruch
 * gezaubert wird.
 * 1: Aura �bertragen
 * 2: Antimagie
 * 3: Magierver�ndernde Spr�che (Magic Boost, ..)
 * 4: Monster erschaffen
 * 5: Standartlevel
 * 7: Teleport
 *
 * Komponenten[Anzahl m�gl. Items][Art:Anzahl:Kostentyp]
 *
 * R_AURA:
 * Grundkosten f�r einen Zauber. Soviel Mp m�ssen mindestens investiert
 * werden, um den Spruch zu wirken. Zus�tzliche Mp k�nnen unterschiedliche
 * Auswirkungen haben, die in der Spruchfunktionsroutine definiert werden.
 *
 * R_PERMAURA:
 * Kosten an permantenter Aura
 *
 * Komponenten Kostentyp:
 * SPC_LEVEL == Spruch mit Levelabh�ngigen Magiekosten. Die angegeben
 * Kosten m�ssen f�r Stufe 1 berechnet sein.
 * SPC_FIX   == Feste Kosten
 *
 * Wenn keine spezielle Syntax angegeben ist, wird die
 * Syntaxbeschreibung aus sptyp  generiert:
 * FARCASTING: ZAUBER [REGION x y]
 * SPELLLEVEL: ZAUBER [STUFE n]
 * UNITSPELL : ZAUBER <spruchname> <Einheit-Nr> [<Einheit-Nr> ..]
 * SHIPSPELL : ZAUBER <spruchname> <Schiff-Nr> [<Schiff-Nr> ..]
 * BUILDINGSPELL: ZAUBER <spruchname> <Geb�ude-Nr> [<Geb�ude-Nr> ..]
 * ONETARGET : ZAUBER <spruchname> <target-nr>
 * PRECOMBATSPELL : KAMPFZAUBER [STUFE n] <spruchname>
 * COMBATSPELL    : KAMPFZAUBER [STUFE n] <spruchname>
 * POSTCOMBATSPELL: KAMPFZAUBER [STUFE n] <spruchname>
 *
 * Das Parsing
 *
 * Der String spell->parameter gibt die Syntax an, nach der die
 * Parameter des Spruches in add_spellparameter() geparst werden sollen.
 *
 * u : eine Einheitennummer
 * r : hier kommen zwei Regionskoordinaten x y
 * b : Geb�ude- oder Burgnummer
 * s : Schiffsnummer
 * c : String, wird ohne Weiterverarbeitung �bergeben
 * i : Zahl (int), wird ohne Weiterverarbeitung �bergeben
 * k : Keywort - dieser String gibt den Paramter an, der folgt. Der
 *     Parameter wird mit findparam() identifiziert.
 *     k muss immer von einem c als Platzhalter f�r das Objekt gefolgt
 *     werden.
 *     Ein gutes Beispiel sind hierf�r die Spr�che zur Magieanalyse.
 * + : gibt an, das der vorherige Parameter mehrfach vorkommen kann. Da
 *     ein Ende nicht definiert werden kann, muss dies immer am Schluss
 *     kommen.
 *
 * Flags f�r das Parsing:
 * TESTRESISTANCE : alle Zielobjekte, also alle Parameter vom Typ Unit,
 *		  Burg, Schiff oder Region, werden auf ihre
 *		  Magieresistenz �berpr�ft
 * TESTCANSEE     : jedes Objekt vom Typ Einheit wird auf seine
 *		  Sichtbarkeit �berpr�ft
 * SEARCHGLOBAL   : die Zielobjekte werden global anstelle von regional
 *		  gesucht
 * REGIONSPELL    : Ziel ist die Region, auch wenn kein Zielobjekt
 *		  angegeben wird. Ist TESTRESISTANCE gesetzt, so wird
 *		  die Magieresistenz der Region �berpr�ft
 *
 * Bei fehlendem Ziel oder wenn dieses dem Zauber widersteht, wird die
 * Spruchfunktion nicht aufgerufen.
 * Sind zu wenig Parameter vorhanden, wird der Zauber ebenfalls nicht
 * ausgef�hrt.
 * Ist eins von mehreren Zielobjekten resistent, so wird das Flag
 * pa->param[n]->flag == TARGET_RESISTS
 * Ist eins von mehreren Zielobjekten nicht gefunden worden, so ist
 * pa->param[n]->flag == TARGET_NOTFOUND
 *
 */
/* ------------------------------------------------------------- */

 /* Bitte die Spr�che nach Gebieten und Stufe ordnen, denn in derselben
	* Reihenfolge wie in Spelldaten tauchen sie auch im Report auf
	*/

spell_list * spells = NULL;

void
register_spell(spell * sp)
{
  spell_list * slist = malloc(sizeof(spell_list));
  slist->next = spells;
  slist->data = sp;
  spells = slist;
}

/* ------------------------------------------------------------- */
/* Spruch identifizieren */

typedef struct spell_names {
  struct spell_names * next;
  const struct locale * lang;
  magic_t mtype;
  struct tnode names;
} spell_names;

static spell_names * spellnames;

static spell_names *
init_spellnames(const struct locale * lang, magic_t mtype)
{
  spell_list * slist;
  spell_names * sn = calloc(sizeof(spell_names), 1);
  sn->next = spellnames;
  sn->lang = lang;
  sn->mtype = mtype;
  for (slist=spells;slist!=NULL;slist=slist->next) {
    spell * sp = slist->data;
    const char * n = sp->sname;
    variant token;
    if (sp->magietyp!=mtype) continue;
    if (sp->info==NULL) n = locale_string(lang, mkname("spell", n));
    token.v = sp;
    addtoken(&sn->names, n, token);
  }
  return spellnames = sn;
}

static spell_names *
get_spellnames(const struct locale * lang, magic_t mtype)
{
  spell_names * sn = spellnames;
  while (sn) {
    if (sn->mtype==mtype && sn->lang==lang) break;
    sn=sn->next;
  }
  if (!sn) return init_spellnames(lang, mtype);
  return sn;
}

static spell *
find_spellbyname_i(const char *name, const struct locale * lang, magic_t mtype)
{
  variant token = { 0 };
  spell_names * sn;

  sn = get_spellnames(lang, mtype);
  if (findtoken(&sn->names, name, &token)==E_TOK_NOMATCH) {
    magic_t mt;
    /* if we could not find it in the main magic type, we look through all the others */
    for (mt=0;mt!=MAXMAGIETYP;++mt) {
      sn = get_spellnames(lang, mt);
      if (findtoken(&sn->names, name, &token)!=E_TOK_NOMATCH) break;
    }
  }

  if (token.v!=NULL) return (spell*)token.v;
  if (lang==default_locale) return NULL;
  return find_spellbyname_i(name, default_locale, mtype);
}

spell *
find_spellbyname(unit *u, const char *name, const struct locale * lang)
{
  sc_mage * m = get_mage(u);
  spell * sp;

  if (m==NULL) return NULL;
  sp = find_spellbyname_i(name, lang, m->magietyp);
  if (sp!=NULL) {
    spell_ptr *spt;

    for (spt = m->spellptr; spt; spt = spt->next) {
      if (sp->id==spt->spellid) return sp;
    }
  }
  return NULL;
}

spell *
find_spellbyid(spellid_t id)
{
  spell_list * slist;

  assert(id>=0);
#ifndef SHOWASTRAL_NOT_BORKED
  /* disabled spells */
  if (id==SPL_SHOWASTRAL) return NULL;
#endif
  if (id==SPL_NOSPELL) return NULL;
  for (slist=spells;slist!=NULL;slist=slist->next) {
    spell* sp = slist->data;
    if (sp->id == id) return sp;
  }
  log_error(("cannot find spell by id: %u\n", id));
  return NULL;
}

static spell spelldaten[] =
{
  /* M_DRUIDE */
  {
    SPL_BLESSEDHARVEST, "blessedharvest", NULL, NULL, NULL,
    M_DRUIDE,
    (FARCASTING | SPELLLEVEL | ONSHIPCAST | REGIONSPELL),
    5, 1,
    {
      { R_AURA, 1, SPC_LEVEL },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_blessedharvest,
    patzer
  },
  {
    SPL_GWYRRD_EARN_SILVER, "gwyrrdearnsilver", NULL,
    NULL, NULL,
    M_DRUIDE,
    (SPELLLEVEL|ONSHIPCAST),
    5, 1,
    {
      { R_AURA, 1, SPC_LEVEL },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_earn_silver,
    patzer
  },
  {
    SPL_STONEGOLEM, "stonegolem", NULL, NULL, NULL,
    M_DRUIDE, (SPELLLEVEL), 4, 1,
    {
      { R_AURA, 2, SPC_LEVEL },
      { R_STONE, 1, SPC_LEVEL },
      { R_TREES, 1, SPC_FIX },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_create_stonegolem, patzer
  },
  {
    SPL_IRONGOLEM, "irongolem", NULL, NULL, NULL,
    M_DRUIDE, (SPELLLEVEL), 4, 2,
    {
      { R_AURA, 2, SPC_LEVEL },
      { R_IRON, 1, SPC_LEVEL },
      { R_TREES, 1, SPC_FIX },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_create_irongolem, patzer
  },
  {
    SPL_TREEGROW, "treegrow", NULL, NULL, NULL,
    M_DRUIDE,
    (FARCASTING | SPELLLEVEL | REGIONSPELL | TESTRESISTANCE),
    5, 2,
    {
      { R_AURA, 4, SPC_LEVEL },
      { R_WOOD, 1, SPC_LEVEL },
      { R_TREES, 1, SPC_FIX },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_hain, patzer_ents
  },
  {
    SPL_RUSTWEAPON, "rustweapon", NULL, NULL,
    "u+",
    M_DRUIDE,
    (FARCASTING | SPELLLEVEL | UNITSPELL | TESTCANSEE | TESTRESISTANCE),
    5, 3,
    {
      { R_AURA, 2, SPC_LEVEL },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_rosthauch, patzer
  },
  {
    SPL_KAELTESCHUTZ, "kaelteschutz", NULL, NULL,
    "u+",
    M_DRUIDE,
    (UNITSPELL | SPELLLEVEL | TESTCANSEE | ONSHIPCAST),
    5, 3,
    {
      { R_AURA, 2, SPC_LEVEL },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_kaelteschutz, patzer
  },
  {
    SPL_HAGEL, "hagel", NULL, NULL, NULL,
    M_DRUIDE, (COMBATSPELL|SPELLLEVEL), 5, 3,
    {
      { R_AURA, 1, SPC_LEVEL },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_kampfzauber, patzer
  },
  {
    SPL_IRONKEEPER, "ironkeeper", NULL, NULL, NULL,
    M_DRUIDE,
    (FARCASTING | SPELLLEVEL | REGIONSPELL | TESTRESISTANCE),
    5, 3,
    {
      { R_AURA, 3, SPC_LEVEL },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_ironkeeper, patzer
  },
  {
    SPL_MAGICSTREET, "magicstreet", NULL, NULL, NULL,
    M_DRUIDE,
    (FARCASTING | SPELLLEVEL | REGIONSPELL | ONSHIPCAST | TESTRESISTANCE),
    5, 4,
    {
      { R_AURA, 1, SPC_LEVEL },
      { R_STONE, 1, SPC_FIX },
      { R_WOOD, 1, SPC_FIX },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_magicstreet, patzer
  },
  {
    SPL_WINDSHIELD, "windshield", NULL, NULL, NULL,
    M_DRUIDE, (PRECOMBATSPELL | SPELLLEVEL), 5, 4,
    {
      { R_AURA, 2, SPC_LEVEL },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_windshield, patzer
  },
  {
    SPL_MALLORNTREEGROW, "mallorntreegrow", NULL, NULL, NULL,
    M_DRUIDE,
    (FARCASTING | SPELLLEVEL | REGIONSPELL | TESTRESISTANCE),
    5, 4,
    {
      { R_AURA, 6, SPC_LEVEL },
      { R_MALLORN, 1, SPC_LEVEL },
      { R_TREES, 1, SPC_FIX },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_mallornhain, patzer_ents
  },
  { SPL_GOODWINDS, "goodwinds", NULL, NULL,
    "s",
    M_DRUIDE,
    (SHIPSPELL|ONSHIPCAST|SPELLLEVEL|ONETARGET|TESTRESISTANCE),
    5, 4,
    {
      { R_AURA, 1, SPC_LEVEL },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_goodwinds, patzer
  },
  {
    SPL_HEALING, "healing", NULL, NULL, NULL,
    M_DRUIDE, (POSTCOMBATSPELL | SPELLLEVEL), 5, 5,
    {
      { R_AURA, 1, SPC_LEVEL },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_healing, patzer
  },
  {
    SPL_REELING_ARROWS, "reelingarrows", NULL, NULL, NULL,
    M_DRUIDE, (PRECOMBATSPELL | SPELLLEVEL), 5, 5,
    {
      { R_AURA, 15, SPC_FIX },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_reeling_arrows, patzer
  },
  {
    SPL_GWYRRD_FUMBLESHIELD, "gwyrrdfumbleshield", NULL, NULL, NULL,
    M_DRUIDE, (PRECOMBATSPELL | SPELLLEVEL), 2, 5,
    {
      { R_AURA, 5, SPC_LEVEL },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_fumbleshield, patzer
  },
  {
    SPL_TRANSFERAURA_DRUIDE, "transferauradruide", NULL,
    "ZAUBERE \'Meditation\' <Einheit-Nr> <investierte Aura>",
    "ui",
    M_DRUIDE, (UNITSPELL|ONSHIPCAST|ONETARGET), 1, 6,
    {
      { R_AURA, 2, SPC_FIX },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_transferaura, patzer
  },
  {
    SPL_EARTHQUAKE, "earthquake", NULL, NULL, NULL,
    M_DRUIDE, (FARCASTING|REGIONSPELL|TESTRESISTANCE), 5, 6,
    {
      { R_AURA, 25, SPC_FIX },
      { R_EOG, 2, SPC_FIX },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_earthquake, patzer
  },
  {
    SPL_STORMWINDS, "stormwinds", NULL, NULL,
    "s+",
    M_DRUIDE,
    (SHIPSPELL | ONSHIPCAST | OCEANCASTABLE | TESTRESISTANCE | SPELLLEVEL),
    5, 6,
    {
      { R_AURA, 6, SPC_LEVEL },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_stormwinds, patzer
  },
  {
    SPL_TRUESEEING_GWYRRD, "trueseeinggwyrrd", NULL, NULL, NULL,
    M_DRUIDE, (ONSHIPCAST), 5, 6,
    {
      { R_AURA, 50, SPC_FIX },
      { R_SILVER, 3000, SPC_FIX },
      { R_PERMAURA, 1, SPC_FIX },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_createitem_trueseeing, patzer_createitem
  },
  {
    SPL_INVISIBILITY_GWYRRD, "invisibilitygwyrrd", NULL, NULL, NULL,
    M_DRUIDE, (ONSHIPCAST), 5, 6,
    {
      { R_AURA, 50, SPC_FIX },
      { R_SILVER, 3000, SPC_FIX },
      { R_PERMAURA, 1, SPC_FIX },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_createitem_invisibility, patzer_createitem
  },
  {
    SPL_HOMESTONE, "homestone", NULL, NULL, NULL,
    M_DRUIDE, (0), 5, 7,
    {
      { R_AURA, 50, SPC_FIX },
      { R_PERMAURA, 1, SPC_FIX },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_homestone, patzer
  },
  {
    SPL_WOLFHOWL, "wolfhowl", NULL, NULL, NULL,
    M_DRUIDE, (PRECOMBATSPELL | SPELLLEVEL ), 5, 7,
    {
      { R_AURA, 2, SPC_LEVEL },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_wolfhowl, patzer
  },
  {
    SPL_VERSTEINERN, "versteinern", NULL, NULL, NULL,
    M_DRUIDE, (COMBATSPELL | SPELLLEVEL), 5, 8,
    {
      { R_AURA, 1, SPC_LEVEL },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_versteinern, patzer
  },
  {
    SPL_STRONG_WALL, "strongwall", NULL, NULL, NULL,
    M_DRUIDE, (PRECOMBATSPELL | SPELLLEVEL), 5, 8,
    {
      { R_AURA, 2, SPC_LEVEL },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_strong_wall, patzer
  },
  {
    SPL_GWYRRD_DESTROY_MAGIC, "gwyrrddestroymagic", NULL,
    "ZAUBERE [REGION x y] [STUFE n] \'Geister bannen\' REGION\n"
    "ZAUBERE [REGION x y] [STUFE n] \'Geister bannen\' EINHEIT <Einheit-Nr>\n"
    "ZAUBERE [REGION x y] [STUFE n] \'Geister bannen\' GEB�UDE <Geb�ude-Nr>\n"
    "ZAUBERE [REGION x y] [STUFE n] \'Geister bannen\' SCHIFF <Schiff-Nr>",
    "kc?",
    M_DRUIDE,
    (FARCASTING | SPELLLEVEL | ONSHIPCAST | ONETARGET | TESTCANSEE),
    2, 8,
    {
      { R_AURA, 6, SPC_LEVEL },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_destroy_magic, patzer
  },
  {
    SPL_TREEWALKENTER, "treewalkenter", NULL, NULL,
    "u+",
    M_DRUIDE, (UNITSPELL | SPELLLEVEL | TESTCANSEE), 7, 9,
    {
      { R_AURA, 3, SPC_LEVEL },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_treewalkenter, patzer
  },
  {
    SPL_TREEWALKEXIT, "treewalkexit", NULL,
    "ZAUBERE \'Sog des Lebens\' <Ziel-X> <Ziel-Y> <Einheit> [<Einheit> ..]",
    "ru+",
    M_DRUIDE, (UNITSPELL | SPELLLEVEL | TESTCANSEE), 7, 9,
    {
      { R_AURA, 2, SPC_LEVEL },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_treewalkexit, patzer
  },
  {
    SPL_HOLYGROUND, "holyground", NULL, NULL, NULL,
    M_DRUIDE, (0), 5, 9,
    {
      { R_AURA, 80, SPC_FIX },
      { R_PERMAURA, 3, SPC_FIX },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_holyground, patzer
  },
  {
    SPL_ARTEFAKT_SACK_OF_CONSERVATION, "artefaktsackofconservation", NULL, NULL, NULL,
    M_DRUIDE, (ONSHIPCAST), 5, 5,
    {
      { R_AURA, 30, SPC_FIX },
      { R_PERMAURA, 1, SPC_FIX },
      { R_TREES, 1, SPC_FIX },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_create_sack_of_conservation, patzer
  },
  {
    SPL_SUMMONENT, "summonent", NULL, NULL, NULL,
    M_DRUIDE, (SPELLLEVEL), 5, 10,
    {
      { R_AURA, 6, SPC_LEVEL },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_summonent, patzer
  },
  {
    SPL_GWYRRD_FAMILIAR, "gwyrrdfamiliar", NULL, NULL, NULL,
    M_DRUIDE, (NOTFAMILIARCAST), 5, 10,
    {
      { R_AURA, 100, SPC_FIX },
      { R_PERMAURA, 5, SPC_FIX },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_summon_familiar, patzer
  },
  {
    SPL_BLESSSTONECIRCLE, "blessstonecircle", NULL, NULL,
    "b",
    M_DRUIDE, (BUILDINGSPELL | ONETARGET), 5, 11,
    {
      { R_AURA, 350, SPC_FIX },
      { R_PERMAURA, 5, SPC_FIX },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_blessstonecircle, patzer
  },
  {
    SPL_GWYRRD_ARMORSHIELD, "Rindenhaut",
    "Dieses vor dem Kampf zu zaubernde Ritual gibt den eigenen Truppen "
    "einen zus�tzlichen Bonus auf ihre R�stung. Jeder Treffer "
    "reduziert die Kraft des Zaubers, so dass der Schild sich irgendwann "
    "im Kampf aufl�sen wird.", NULL, NULL,
    M_DRUIDE, (PRECOMBATSPELL | SPELLLEVEL), 2, 12,
    {
      { R_AURA, 4, SPC_LEVEL },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_armorshield, patzer
  },
  {
    SPL_DROUGHT, "Beschw�rung eines Hitzeelementar",
    "Dieses Ritual beschw�rt w�tende Elementargeister der Hitze. "
    "Eine D�rre sucht das Land heim. B�ume verdorren, Tiere verenden, "
    "und die Ernte f�llt aus. F�r Tagel�hner gibt es kaum noch Arbeit "
    "in der Landwirtschaft zu finden.", NULL, NULL,
    M_DRUIDE, (FARCASTING|REGIONSPELL|TESTRESISTANCE), 5, 13,
    {
      { R_AURA, 600, SPC_FIX },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_drought, patzer
  },
  {
    SPL_FOG_OF_CONFUSION, "Nebel der Verwirrung",
    "Der Druide beschw�rt die Elementargeister des Nebels. Sie werden sich "
    "f�r einige Zeit in der Umgebung festsetzen und sie mit dichtem Nebel "
    "�berziehen. Personen innerhalb des magischen Nebels verlieren die "
    "Orientierung und haben gro�e Schwierigkeiten, sich in eine bestimmte "
    "Richtung zu bewegen.", NULL, NULL,
    M_DRUIDE,
    (FARCASTING|SPELLLEVEL),
    5, 14,
    {
      { R_AURA, 8, SPC_LEVEL },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_fog_of_confusion, patzer
  },
  {
    SPL_MAELSTROM, "Mahlstrom",
    "Dieses Ritual besch�rt einen gro�en Wasserelementar aus den "
    "Tiefen des Ozeans. Der Elementar erzeugt einen gewaltigen "
    "Strudel, einen Mahlstrom, welcher alle Schiffe, die ihn passieren, "
    "schwer besch�digen kann.", NULL, NULL,
    M_DRUIDE,
    (OCEANCASTABLE | ONSHIPCAST | REGIONSPELL | TESTRESISTANCE),
    5, 15,
    {
      { R_AURA, 200, SPC_FIX },
      { R_SEASERPENTHEAD, 1, SPC_FIX },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_maelstrom, patzer
  },
  {
    SPL_MALLORN, "Wurzeln der Magie",
    "Mit Hilfe dieses aufw�ndigen Rituals l��t der Druide einen Teil seiner "
    "dauerhaft in den Boden und die W�lder der Region fliessen. Dadurch wird "
    "das Gleichgewicht der Natur in der Region f�r immer ver�ndert, und in "
    "Zukunft werden nur noch die anspruchsvollen, aber kr�ftigen "
    "Mallorngew�chse in der Region gedeihen.", NULL, NULL,
    M_DRUIDE,
    (FARCASTING | REGIONSPELL | TESTRESISTANCE),
    5, 16,
    {
      { R_AURA, 250, SPC_FIX },
      { R_PERMAURA, 10, SPC_FIX },
      { R_TOADSLIME, 1, SPC_FIX },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_mallorn, patzer
  },
  {
    SPL_GREAT_DROUGHT, "Tor in die Ebene der Hitze",
    "Dieses m�chtige Ritual �ffnet ein Tor in die Elementarebene der "
    "Hitze. Eine grosse D�rre kommt �ber das Land. Bauern, Tiere und "
    "Pflanzen der Region k�mpfen um das nackte �berleben, aber eine "
    "solche D�rre �berlebt wohl nur die H�lfte aller Lebewesen. "
    "Der Landstrich kann �ber Jahre hinaus von den Folgen einer "
    "solchen D�rre betroffen sein.", NULL, NULL,
    M_DRUIDE,
    (FARCASTING | REGIONSPELL | TESTRESISTANCE),
    5, 17,
    {
      { R_AURA, 800, SPC_FIX },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_great_drought, patzer
  },
  /* M_CHAOS */
  {
    SPL_SPARKLE_CHAOS, "sparklechaos", NULL, NULL,
    "u",
    M_CHAOS, (UNITSPELL | TESTCANSEE | SPELLLEVEL | ONETARGET), 5, 1,
    {
      { R_AURA, 1, SPC_LEVEL },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_sparkle, patzer
  },
  {
    SPL_DRAIG_EARN_SILVER, "draigearnsilver", NULL,
    NULL,
    NULL,
    M_CHAOS, (SPELLLEVEL|ONSHIPCAST), 5, 1,
    {
      { R_AURA, 1, SPC_LEVEL },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_earn_silver, patzer
  },
  {
    SPL_FIREBALL, "fireball", NULL, NULL, NULL,
    M_CHAOS, (COMBATSPELL | SPELLLEVEL), 5, 2,
    {
      { R_AURA, 1, SPC_LEVEL },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_kampfzauber, patzer
  },
  {
    SPL_MAGICBOOST, "magicboost", NULL, NULL, NULL,
    M_CHAOS, (ONSHIPCAST), 3, 3,
    {
      { R_AURA, 2, SPC_LINEAR },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_magicboost, patzer
  },
  {
    SPL_BLOODSACRIFICE, "bloodsacrifice", NULL, NULL, NULL,
    M_CHAOS, (ONSHIPCAST), 1, 4,
    {
      { R_HITPOINTS, 4, SPC_LEVEL },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_bloodsacrifice, patzer
  },
  {
    SPL_BERSERK, "berserk", NULL, NULL, NULL,
    M_CHAOS, (PRECOMBATSPELL | SPELLLEVEL), 4, 5,
    {
      { R_AURA, 5, SPC_LEVEL },
      { R_PEASANTS, 1, SPC_FIX },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_berserk, patzer
  },
  {
    SPL_FUMBLECURSE, "fumblecurse", NULL, NULL,
    "u",
    M_CHAOS,
    (UNITSPELL | SPELLLEVEL | ONETARGET | TESTCANSEE | TESTRESISTANCE),
    4, 5,
    {
      { R_AURA, 4, SPC_LEVEL },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_fumblecurse, patzer_fumblecurse
  },
  {
    SPL_SUMMONUNDEAD, "summonundead", NULL, NULL, NULL,
    M_CHAOS, (SPELLLEVEL | FARCASTING | ONSHIPCAST),
    5, 6,
    {
      { R_AURA, 5, SPC_LEVEL },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_summonundead, patzer_peasantmob
  },
  {
    SPL_COMBATRUST, "combatrust", NULL, NULL, NULL,
    M_CHAOS, (COMBATSPELL | SPELLLEVEL), 5, 6,
    {
      { R_AURA, 2, SPC_LEVEL },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_combatrosthauch, patzer
  },
  {
    SPL_TRUESEEING_DRAIG, "trueseeingdraig", NULL, NULL, NULL,
    M_CHAOS, (ONSHIPCAST), 5, 6,
    {
      { R_AURA, 50, SPC_FIX },
      { R_SILVER, 3000, SPC_FIX },
      { R_PERMAURA, 1, SPC_FIX },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_createitem_trueseeing, patzer_createitem
  },
  {
    SPL_INVISIBILITY_DRAIG, "invisibilitydraig", NULL, NULL, NULL,
    M_CHAOS, (ONSHIPCAST), 5, 6,
    {
      { R_AURA, 50, SPC_FIX },
      { R_SILVER, 3000, SPC_FIX },
      { R_PERMAURA, 1, SPC_FIX },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_createitem_invisibility, patzer_createitem
  },
  {
    SPL_TRANSFERAURA_CHAOS, "tranferaurachaos", NULL,
    "ZAUBERE \'Macht�bertragung\' <Einheit-Nr> <investierte Aura>",
    "ui",
    M_CHAOS, (UNITSPELL|ONSHIPCAST|ONETARGET), 1, 7,
    {
      { R_AURA, 2, SPC_FIX },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_transferaura, patzer
  },
  {
    SPL_FIREWALL, "firewall", NULL,
    "ZAUBERE \'Feuerwand\' <Richtung>",
    "c",
    M_CHAOS, (SPELLLEVEL | REGIONSPELL | TESTRESISTANCE), 4, 7,
    {
      { R_AURA, 6, SPC_LEVEL },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_firewall, patzer_peasantmob
  },
  {
    SPL_PLAGUE, "plague", NULL, NULL, NULL,
    M_CHAOS,
    (FARCASTING | REGIONSPELL | TESTRESISTANCE),
    5, 7,
    {
      { R_AURA, 30, SPC_FIX },
      { R_PEASANTS, 50, SPC_FIX },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_plague, patzer_peasantmob
  },
  {
    SPL_CHAOSROW, "chaosrow", NULL, NULL, NULL,
    M_CHAOS, (PRECOMBATSPELL | SPELLLEVEL), 5, 8,
    {
      { R_AURA, 3, SPC_LEVEL },
      { R_PEASANTS, 10, SPC_FIX },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_chaosrow, patzer
  },
  {
    SPL_SUMMONSHADOW, "summonshadow", NULL, NULL, NULL,
    M_CHAOS, (SPELLLEVEL), 5, 8,
    {
      { R_AURA, 3, SPC_LEVEL },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_summonshadow, patzer_peasantmob
  },
  {
    SPL_UNDEADHERO, "undeadhero", NULL, NULL, NULL,
    M_CHAOS, (POSTCOMBATSPELL | SPELLLEVEL), 5, 9,
    {
      { R_AURA, 1, SPC_LEVEL },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_undeadhero, patzer
  },
  {
    SPL_STRENGTH, "strength", NULL, NULL, NULL,
    M_CHAOS, (ONSHIPCAST), 5, 9,
    {
      { R_AURA, 20, SPC_FIX },
      { R_PERMAURA, 1, SPC_FIX },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_create_trollbelt, patzer
  },
  {
    SPL_AURALEAK, "auraleak", NULL, NULL, NULL,
    M_CHAOS, (REGIONSPELL | TESTRESISTANCE), 3, 9,
    {
      { R_AURA, 35, SPC_FIX },
      { R_DRACHENBLUT, 1, SPC_FIX },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_auraleak, patzer
  },
  {
    SPL_DRAIG_FUMBLESHIELD, "draigfumbleshield", NULL, NULL, NULL,
    M_CHAOS, (PRECOMBATSPELL | SPELLLEVEL), 2, 9,
    {
      { R_AURA, 6, SPC_LEVEL },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_fumbleshield, patzer
  },
  {
    SPL_FOREST_FIRE, "forestfire", NULL, NULL, NULL,
    M_CHAOS, (FARCASTING | REGIONSPELL | TESTRESISTANCE), 5, 10,
    {
      { R_AURA, 50, SPC_FIX },
      { R_OIL, 1, SPC_FIX },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_forest_fire, patzer_peasantmob
  },
  {
    SPL_DRAIG_DESTROY_MAGIC, "draigdestroymagic", NULL,
    "ZAUBERE [REGION x y] [STUFE n] \'Pentagramm\' REGION\n"
    "ZAUBERE [REGION x y] [STUFE n] \'Pentagramm\' EINHEIT <Einheit-Nr>\n"
    "ZAUBERE [REGION x y] [STUFE n] \'Pentagramm\' GEB�UDE <Geb�ude-Nr>\n"
    "ZAUBERE [REGION x y] [STUFE n] \'Pentagramm\' SCHIFF <Schiff-Nr>",
    "kc?",
    M_CHAOS,
    (FARCASTING | SPELLLEVEL | ONSHIPCAST | ONETARGET | TESTCANSEE),
    2, 10,
    {
      { R_AURA, 10, SPC_LEVEL },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_destroy_magic, patzer
  },
  {
    SPL_UNHOLYPOWER, "unholypower", NULL, NULL,
    "u+",
    M_CHAOS, (UNITSPELL | SPELLLEVEL | TESTCANSEE), 5, 14,
    {
      { R_AURA, 10, SPC_LEVEL },
      { R_PEASANTS, 5, SPC_LEVEL },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_unholypower, patzer
  },
  {
    SPL_DEATHCLOUD, "deathcloud", NULL, NULL, NULL,
    M_CHAOS, (FARCASTING | REGIONSPELL | TESTRESISTANCE), 5, 11,
    {
      { R_AURA, 40, SPC_FIX },
      { R_HITPOINTS, 15, SPC_FIX },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_deathcloud, patzer_peasantmob
  },
  {
    SPL_SUMMONDRAGON, "summondragon", NULL, NULL, NULL,
    M_CHAOS, (FARCASTING | REGIONSPELL | TESTRESISTANCE), 5, 11,
    {
      { R_AURA, 80, SPC_FIX },
      { R_DRAGONHEAD, 1, SPC_FIX },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_summondragon, patzer_peasantmob
  },
  {
    SPL_SUMMONSHADOWLORDS, "summonshadowlords", NULL, NULL, NULL,
    M_CHAOS, (SPELLLEVEL), 5, 12,
    {
      { R_AURA, 7, SPC_LEVEL },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_summonshadowlords, patzer_peasantmob
  },
  {
    SPL_FIRESWORD, "firesword", NULL, NULL, NULL,
    M_CHAOS, (ONSHIPCAST), 5, 12,
    {
      { R_AURA, 100, SPC_FIX },
      { R_BERSERK, 1, SPC_FIX },
      { R_SWORD, 1, SPC_FIX },
      { R_PERMAURA, 1, SPC_FIX },
      { 0, 0, 0 }
    },
    (spell_f)sp_create_firesword, patzer
  },
  {
    SPL_DRAIG_FAMILIAR, "draigfamiliar", NULL, NULL, NULL,
    M_CHAOS, (NOTFAMILIARCAST), 5, 13,
    {
      { R_AURA, 100, SPC_FIX },
      { R_PERMAURA, 5, SPC_FIX },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_summon_familiar, patzer
  },
  {
    SPL_CHAOSSUCTION, "chaossuction", NULL, NULL, NULL,
    M_CHAOS, (0), 5, 14,
    {
      { R_AURA, 150, SPC_FIX },
      { R_PEASANTS, 200, SPC_FIX },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_chaossuction, patzer_peasantmob
  },
  /* M_TRAUM */
  {
    SPL_SPARKLE_DREAM, "sparkledream", NULL, NULL,
    "u",
    M_TRAUM,
    (UNITSPELL | TESTCANSEE | SPELLLEVEL | ONETARGET | ONSHIPCAST),
    5, 1,
    {
      { R_AURA, 1, SPC_LEVEL },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_sparkle, patzer
  },
  {
    SPL_ILLAUN_EARN_SILVER, "illaunearnsilver", NULL,
    NULL,
    NULL,
    M_TRAUM, (SPELLLEVEL|ONSHIPCAST), 5, 1,
    {
      { R_AURA, 1, SPC_LEVEL },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_earn_silver, patzer
  },
  {
    SPL_SHADOWKNIGHTS, "shadowknights", NULL, NULL, NULL,
    M_TRAUM, (PRECOMBATSPELL | SPELLLEVEL), 4, 1,
    {
      { R_AURA, 1, SPC_LEVEL },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_shadowknights, patzer
  },
  {
    SPL_FLEE, "flee", NULL, NULL, NULL,
    M_TRAUM, (PRECOMBATSPELL | SPELLLEVEL), 5, 2,
    {
      { R_AURA, 1, SPC_LEVEL },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_flee, patzer
  },
  {
    SPL_PUTTOREST, "puttorest", NULL, NULL, NULL,
    M_TRAUM, (SPELLLEVEL), 5, 2,
    {
      { R_AURA, 3, SPC_LEVEL },
      { R_TREES, 1, SPC_FIX },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_puttorest, patzer
  },
  {
    SPL_ICASTLE, "icastle", NULL,
    "ZAUBERE \"Traumschl��chen\" <Geb�ude-Typ>",
    "c",
    M_TRAUM, (0), 5, 3,
    {
      { R_AURA, 1, SPC_LEVEL },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_icastle, patzer
  },
  {
    SPL_TRANSFERAURA_TRAUM, "transferauratraum", NULL,
    "ZAUBERE \'Traum der Magie\' <Einheit-Nr> <investierte Aura>",
    "ui",
    M_TRAUM, (UNITSPELL|ONSHIPCAST|ONETARGET), 1, 3,
    {
      { R_AURA, 2, SPC_FIX },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_transferaura, patzer
  },
  {
    SPL_ILL_SHAPESHIFT, "shapeshift", NULL,
    "ZAUBERE [STUFE n] \'Gestaltwandlung\' <Einheit-nr> <Rasse>",
    "uc",
    M_TRAUM, (UNITSPELL|SPELLLEVEL|ONETARGET), 5, 3,
    {
      { R_AURA, 1, SPC_LEVEL },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_illusionary_shapeshift, patzer
  },
  {
    SPL_DREAMREADING, "dreamreading", NULL, NULL,
    "u",
    M_TRAUM, (FARCASTING | UNITSPELL | ONETARGET | TESTRESISTANCE), 5, 4,
    {
      { R_AURA, 8, SPC_FIX },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_dreamreading, patzer
  },
  {
    SPL_TIREDSOLDIERS, "tiredsoldiers", NULL, NULL, NULL,
    M_TRAUM, (PRECOMBATSPELL | SPELLLEVEL), 5, 4,
    {
      { R_AURA, 4, SPC_LEVEL },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_tiredsoldiers, patzer
  },
  {
    SPL_REANIMATE, "reanimate", NULL, NULL, NULL,
    M_TRAUM, (POSTCOMBATSPELL | SPELLLEVEL), 4, 5,
    {
      { R_AURA, 1, SPC_LEVEL },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_reanimate, patzer
  },
  {
    SPL_ANALYSEDREAM, "analysedream", NULL, NULL,
    "u",
    M_TRAUM, (UNITSPELL | ONSHIPCAST | ONETARGET | TESTCANSEE), 5, 5,
    {
      { R_AURA, 5, SPC_LEVEL },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_analysedream, patzer
  },
  {
    SPL_DISTURBINGDREAMS, "disturbingdreams", NULL, NULL, NULL,
    M_TRAUM, (FARCASTING | REGIONSPELL | TESTRESISTANCE), 5, 6,
    {
      { R_AURA, 18, SPC_FIX },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_disturbingdreams, patzer
  },
  {
    SPL_TRUESEEING_ILLAUN, "trueseeingillaun", NULL, NULL, NULL,
    M_TRAUM, (ONSHIPCAST), 5, 6,
    {
      { R_AURA, 50, SPC_FIX },
      { R_SILVER, 3000, SPC_FIX },
      { R_PERMAURA, 1, SPC_FIX },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_createitem_trueseeing, patzer_createitem
  },
  {
    SPL_INVISIBILITY_ILLAUN, "invisibilityillaun", NULL, NULL, NULL,
    M_TRAUM, (ONSHIPCAST), 5, 6,
    {
      { R_AURA, 50, SPC_FIX },
      { R_SILVER, 3000, SPC_FIX },
      { R_PERMAURA, 1, SPC_FIX },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_createitem_invisibility, patzer_createitem
  },
  {
    SPL_SLEEP, "sleep", NULL, NULL, NULL,
    M_TRAUM, (COMBATSPELL | SPELLLEVEL ), 5, 7,
    {
      { R_AURA, 1, SPC_LEVEL },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_sleep, patzer
  },
  {
    SPL_WISPS, "Irrlichter",
    "Der Zauberer spricht eine Beschw�rung �ber einen Teil der Region, "
    "und in der Folgewoche entstehen dort Irrlichter. "
    "Wer durch diese Nebel wandert, wird von Visionen geplagt und "
    "in die Irre geleitet.",
    "ZAUBERE [REGION x y] [STUFE n] \'Irrlichter\' <Richtung>",
    "c",
    M_TRAUM, (SPELLLEVEL | FARCASTING), 5, 7,
    {
      { R_AURA, 2, SPC_LEVEL },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_wisps, patzer
  },
  {
    SPL_READMIND, "readmind", NULL, NULL,
    "u",
    M_TRAUM, (UNITSPELL | ONETARGET), 5, 7,
    {
      { R_AURA, 20, SPC_FIX },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_readmind, patzer
  },
  {
    SPL_GOODDREAMS, "gooddreams", NULL, NULL, NULL,
    M_TRAUM,
    (FARCASTING | REGIONSPELL | TESTRESISTANCE),
    5, 8,
    {
      { R_AURA, 80, SPC_FIX },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_gooddreams, patzer
  },
  {
    SPL_ILLAUN_DESTROY_MAGIC, "illaundestroymagic", NULL,
    "ZAUBERE [REGION x y] [STUFE n] \'Traumbilder entwirren\' REGION\n"
    "ZAUBERE [REGION x y] [STUFE n] \'Traumbilder entwirren\' EINHEIT <Einheit-Nr>\n"
    "ZAUBERE [REGION x y] [STUFE n] \'Traumbilder entwirren\' GEB�UDE <Geb�ude-Nr>\n"
    "ZAUBERE [REGION x y] [STUFE n] \'Traumbilder entwirren\' SCHIFF <Schiff-Nr>",
    "kc?",
    M_TRAUM,
    (FARCASTING | SPELLLEVEL | ONSHIPCAST | ONETARGET | TESTCANSEE),
    2, 8,
    {
      { R_AURA, 6, SPC_LEVEL },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_destroy_magic, patzer
  },
  {
    SPL_ILLAUN_FAMILIAR, "illaunfamiliar", NULL, NULL, NULL,
    M_TRAUM, (NOTFAMILIARCAST), 5, 9,
    {
      { R_AURA, 100, SPC_FIX },
      { R_PERMAURA, 5, SPC_FIX },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_summon_familiar, patzer
  },
  {
    SPL_CLONECOPY, "Seelenkopie",
    "Dieser m�chtige Zauber kann einen Magier vor dem sicheren Tod "
    "bewahren. Der Magier erschafft anhand einer kleinen Blutprobe einen "
    "Klon von sich, und legt diesen in ein Bad aus Drachenblut und verd�nntem "
    "Wasser des Lebens. "
    "Anschlie�end transferiert er in einem aufw�ndigen Ritual einen Teil "
    "seiner Seele in den Klon. Stirbt der Magier, reist seine Seele in den "
    "Klon und der erschaffene K�rper dient nun dem Magier als neues Gef��. "
    "Es besteht allerdings eine geringer Wahrscheinlichkeit, dass die Seele "
    "nach dem Tod zu schwach ist, das neue Gef�� zu erreichen.", NULL, NULL,
    M_TRAUM, (NOTFAMILIARCAST), 5, 9,
    {
      { R_AURA, 100, SPC_FIX },
      { R_PERMAURA, 20, SPC_FIX },
      { R_DRACHENBLUT, 5, SPC_FIX },
      { R_TREES, 5, SPC_FIX },
      { 0, 0, 0 }
    },
    (spell_f)sp_clonecopy, patzer
  },
  {
    SPL_BADDREAMS, "Schlechte Tr�ume",
    "Dieser Zauber erm�glicht es dem Tr�umer, den Schlaf aller nichtaliierten "
    "Einheiten (HELFE BEWACHE) in der Region so stark zu st�ren, das sie "
    "vor�bergehend einen Teil ihrer Erinnerungen verlieren.", NULL, NULL,
    M_TRAUM,
    (FARCASTING | REGIONSPELL | TESTRESISTANCE), 5, 10,
    {
      { R_AURA, 90, SPC_FIX },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_baddreams, patzer
  },
  {
    SPL_MINDBLAST, "mindblast", NULL, NULL, NULL,
    M_TRAUM, (COMBATSPELL | SPELLLEVEL), 5, 11,
    {
      { R_AURA, 2, SPC_LEVEL },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_mindblast, patzer
  },
  {
    SPL_ORKDREAM, "orkdream", NULL, NULL,
    "u+",
    M_TRAUM,
    (UNITSPELL | TESTRESISTANCE | TESTCANSEE | SPELLLEVEL), 5, 12,
    {
      { R_AURA, 5, SPC_LEVEL },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_sweetdreams, patzer
  },
  { SPL_INVISIBILITY2_ILLAUN, "create_invisibility_sphere", NULL, NULL, NULL,
    M_TRAUM, (ONSHIPCAST), 5, 13,
    {
      { R_AURA, 150, SPC_FIX },
      { R_SILVER, 30000, SPC_FIX },
      { R_PERMAURA, 3, SPC_FIX },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_createitem_invisibility2, patzer_createitem
  },
  {
    SPL_CREATE_TACTICCRYSTAL, "create_tacticcrystal", NULL, NULL, NULL,
    M_TRAUM, (ONSHIPCAST), 5, 14,
    {
      { R_PERMAURA, 5, SPC_FIX },
      { R_DRAGONHEAD, 1, SPC_FIX },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_create_tacticcrystal, patzer_createitem
  },
  {
    SPL_SUMMON_ALP, "summon_alp", NULL, NULL, "u",
    M_TRAUM,
    (UNITSPELL | ONETARGET | SEARCHGLOBAL | TESTRESISTANCE),
    5, 15,
    {
      { R_AURA, 350, SPC_FIX },
      { R_PERMAURA, 5, SPC_FIX },
      { R_SWAMP_3, 1, SPC_FIX },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_summon_alp, patzer
  },
  {
    SPL_DREAM_OF_CONFUSION, "dream_of_confusion", NULL, NULL, NULL,
    M_TRAUM,
    (FARCASTING | SPELLLEVEL),
    5, 16,
    {
      { R_AURA, 7, SPC_LEVEL },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_dream_of_confusion, patzer
  },
  /* M_BARDE */
  {
    SPL_DENYATTACK, "appeasement", NULL, NULL, NULL,
    M_BARDE, (PRECOMBATSPELL | SPELLLEVEL ), 5, 1,
    {
      { R_AURA, 2, SPC_FIX },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_denyattack, patzer
  },
  {
    SPL_CERDDOR_EARN_SILVER, "jugglery", NULL, NULL, NULL,
    M_BARDE, (SPELLLEVEL|ONSHIPCAST), 5, 1,
    {
      { R_AURA, 1, SPC_LEVEL },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_earn_silver, patzer
  },
  {
    SPL_HEALINGSONG, "song_of_healing", NULL, NULL, NULL,
    M_BARDE, (POSTCOMBATSPELL | SPELLLEVEL), 5, 2,
    {
      { R_AURA, 1, SPC_LEVEL },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_healing, patzer
  },
  {
    SPL_GENEROUS, "generous", NULL, NULL, NULL,
    M_BARDE,
    (FARCASTING | SPELLLEVEL | ONSHIPCAST | REGIONSPELL | TESTRESISTANCE),
    5, 2,
    {
      { R_AURA, 2, SPC_LEVEL },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_generous, patzer
  },
  {
    SPL_RAINDANCE, "raindance", NULL, NULL, NULL,
    M_BARDE,
    (FARCASTING | SPELLLEVEL | ONSHIPCAST | REGIONSPELL),
    5, 3,
    {
      { R_AURA, 1, SPC_LEVEL },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_blessedharvest, patzer
  },
  {
    SPL_SONG_OF_FEAR, "song_of_fear", NULL, NULL, NULL,
    M_BARDE, (COMBATSPELL | SPELLLEVEL), 5, 3,
    {
      { R_AURA, 1, SPC_LEVEL },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_flee, patzer
  },
  {
    SPL_RECRUIT, "courting", NULL, NULL, NULL,
    M_BARDE, (SPELLLEVEL), 5, 4,
    {
      { R_AURA, 2, SPC_LEVEL },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_recruit, patzer
  },
  {
    SPL_SONG_OF_CONFUSION, "song_of_confusion", NULL, NULL, NULL,
    M_BARDE, (PRECOMBATSPELL | SPELLLEVEL), 5, 4,
    {
      { R_AURA, 2, SPC_LEVEL },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_chaosrow, patzer
  },
  {
    SPL_BABBLER, "blabbermouth", NULL, NULL, "u",
    M_BARDE, (UNITSPELL | ONETARGET | TESTCANSEE), 5, 4,
    {
      { R_AURA, 10, SPC_FIX },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_babbler, patzer
  },
  {
    SPL_HERO, "heroic_song", NULL, NULL, NULL,
    M_BARDE, (PRECOMBATSPELL | SPELLLEVEL), 4, 5,
    {
      { R_AURA, 2, SPC_LEVEL },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_hero, patzer
  },
  {
    SPL_TRANSFERAURA_BARDE, "transfer_aura_song", NULL, NULL,
    "ui",
    M_BARDE, (UNITSPELL|ONSHIPCAST|ONETARGET), 1, 5,
    {
      { R_AURA, 2, SPC_FIX },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_transferaura, patzer
  },
  {
    SPL_UNIT_ANALYSESONG, "analysesong_unit", NULL, NULL,
    "u",
    M_BARDE,
    (UNITSPELL | ONSHIPCAST | ONETARGET | TESTCANSEE),
    5, 5,
    {
      { R_AURA, 10, SPC_FIX },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_analysesong_unit, patzer
  },
  {
    SPL_CERRDOR_FUMBLESHIELD, "fumbleshield", NULL, NULL, NULL,
    M_BARDE, (PRECOMBATSPELL | SPELLLEVEL), 2, 5,
    {
      { R_AURA, 5, SPC_LEVEL },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_fumbleshield, patzer
  },
  { SPL_CALM_MONSTER, "Monster friedlich stimmen",
    "Dieser einschmeichelnde Gesang kann fast jedes intelligente Monster "
    "z�hmen. Es wird von Angriffen auf den Magier absehen und auch seine "
    "Begleiter nicht anr�hren. Doch sollte man sich nicht t�uschen, es "
    "wird dennoch ein unberechenbares Wesen bleiben.", NULL,
    "u",
    M_BARDE,
    (UNITSPELL | ONSHIPCAST | ONETARGET | TESTRESISTANCE | TESTCANSEE),
    5, 6,
    {
      { R_AURA, 15, SPC_FIX },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_calm_monster, patzer
  },
  { SPL_SEDUCE, "Lied der Verf�hrung",
    "Mit diesem Lied kann eine Einheit derartig bet�rt werden, so dass "
    "sie dem Barden den gr��ten Teil ihres Bargelds und ihres Besitzes "
    "schenkt. Sie beh�lt jedoch immer soviel, wie sie zum �berleben "
    "braucht.", NULL,
    "u",
    M_BARDE,
    (UNITSPELL | ONETARGET | TESTRESISTANCE | TESTCANSEE),
    5, 6,
    {
      { R_AURA, 12, SPC_FIX },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_seduce, patzer
  },
  {
    SPL_TRUESEEING_CERDDOR, "Erschaffe ein Amulett des wahren Sehens",
    "Der Spruch erm�glicht es einem Magier, ein Amulett des Wahren Sehens "
    "zu erschaffen. Das Amulett erlaubt es dem Tr�ger, alle Einheiten, die "
    "durch einen Ring der Unsichtbarkeit gesch�tzt sind, zu sehen. Einheiten "
    "allerdings, die sich mit ihrem Tarnungs-Talent verstecken, bleiben "
    "weiterhin unentdeckt.", NULL, NULL,
    M_BARDE, (ONSHIPCAST), 5, 6,
    {
      { R_AURA, 50, SPC_FIX },
      { R_SILVER, 3000, SPC_FIX },
      { R_PERMAURA, 1, SPC_FIX },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_createitem_trueseeing, patzer_createitem
  },
  {
    SPL_INVISIBILITY_CERDDOR, "Erschaffe einen Ring der Unsichtbarkeit",
    "Mit diesem Spruch kann der Zauberer einen Ring der Unsichtbarkeit "
    "erschaffen. Der Tr�ger des Ringes wird f�r alle Einheiten anderer "
    "Parteien unsichtbar, egal wie gut ihre Wahrnehmung auch sein mag. In "
    "einer unsichtbaren Einheit muss jede Person einen Ring tragen.", NULL, NULL,
    M_BARDE, (ONSHIPCAST), 5, 6,
    {
      { R_AURA, 50, SPC_FIX },
      { R_SILVER, 3000, SPC_FIX },
      { R_PERMAURA, 1, SPC_FIX },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_createitem_invisibility, patzer_createitem
  },
  {
    SPL_HEADACHE, "Schaler Wein",
    "Aufzeichung des Vortrags von Selen Ard'Ragorn in Bar'Glingal: "
    "'Es heiss, dieser Spruch w�re wohl in den Spelunken der Westgassen "
    "entstanden, doch es kann genausogut in jedem andern verrufenen "
    "Viertel gewesen sein. Seine wichtigste Zutat ist etwa ein Fass "
    "schlechtesten Weines, je billiger und ungesunder, desto "
    "wirkungsvoller wird die Essenz. Die Kunst, diesen Wein in pure "
    "Essenz zu destillieren, die weitaus anspruchsvoller als das einfache "
    "Rezeptmischen eines Alchemisten ist, und diese dergestalt zu binden "
    "und konservieren, das sie sich nicht gleich wieder verfl�chtigt, wie "
    "es ihre Natur w�re, ja, dies ist etwas, das nur ein Meister des "
    "Cerddor vollbringen kann. Nun besitzt Ihr eine kleine Phiola mit "
    "einer rubinrotschimmernden - nun, nicht fl�ssig, doch auch nicht "
    "ganz Dunst - nennen wir es einfach nur Elixier. Doch nicht dies ist "
    "die wahre Herausforderung, sodann muss, da sich ihre Wirkung leicht "
    "verfl�chtigt, diese innerhalb weniger Tage unbemerkt in das Getr�nkt "
    "des Opfers getr�ufelt werden. Ihr Meister der Bet�hrung und "
    "Verf�hrung, hier nun k�nnt Ihr Eure ganze Kunst unter Beweis "
    "stellen. Doch gebt Acht, nicht unbedacht selbst von dem Elixier zu "
    "kosten, denn wer einmal gekostet hat, der kann vom Weine nicht mehr "
    "lassen, und er s�uft sicherlich eine volle Woche lang. Jedoch nicht "
    "die Verf�hrung zum Trunke ist die wahre Gefahr, die dem Elixier "
    "innewohnt, sondern das der Trunkenheit so sicher ein gar "
    "f�rchterliches Leid des Kopfes folgen wird, wie der Tag auf die "
    "Nacht folgt. Und er wird gar sicherlich von seiner besten F�higkeit "
    "einige Tage bis hin zu den Studien zweier Wochen vergessen haben. "
    "Noch ein Wort der Warnung: Dieses ist sehr aufwendig, und so Ihr "
    "noch weitere Zauber in der selben Woche wirken wollt, so werden sie Euch "
    "schwerer fallen.'", NULL,
    "u",
    M_BARDE,
    (UNITSPELL | ONETARGET | TESTRESISTANCE | TESTCANSEE),
    5, 7,
    {
      { R_AURA, 4, SPC_LINEAR },
      { R_SWAMP_2, 3, SPC_FIX },
      { R_SILVER, 50, SPC_FIX },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_headache, patzer
  },
  { SPL_PUMP, "Aushorchen",
    "Erliegt die Einheit dem Zauber, so wird sie dem Magier alles erz�hlen, "
    "was sie �ber die gefragte Region wei�. Ist in der Region niemand "
    "ihrer Partei, so wei� sie nichts zu berichten. Auch kann sie nur das "
    "erz�hlen, was sie selber sehen k�nnte.",
    "ZAUBERE \'Aushorchen\' <Einheit-Nr> <Zielregion-X> <Zielregion-Y>",
    "ur",
    M_BARDE, (UNITSPELL | ONETARGET | TESTCANSEE), 5, 7,
    {
      { R_AURA, 4, SPC_FIX },
      { R_SILVER, 100, SPC_FIX },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_pump, patzer
  },
  {
    SPL_BLOODTHIRST, "Kriegsgesang",
    "Wie viele magischen Ges�nge, so entstammt auch dieser den altem "
    "Wissen der Katzen, die schon immer um die machtvolle Wirkung der "
    "Stimme wussten. Mit diesem Lied wird die Stimmung der Krieger "
    "aufgepeitscht, sie gar in wilde Raserrei und Blutrausch versetzt. "
    "Ungeachtet eigener Schmerzen werden sie k�mpfen bis zum "
    "Tode und niemals fliehen. W�hrend ihre Attacke verst�rkt ist "
    "achten sie kaum auf sich selbst.", NULL, NULL,
    M_BARDE, (PRECOMBATSPELL | SPELLLEVEL), 4, 7,
    {
      { R_AURA, 5, SPC_LEVEL },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_berserk, patzer
  },
  {
    SPL_FRIGHTEN, "Gesang der Angst",
    "Dieser Kriegsgesang s�t Panik in der Front der Gegner und schw�cht "
    "so ihre Kampfkraft erheblich. Angst wird ihren Schwertarm schw�chen "
    "und Furcht ihren Schildarm l�hmen.", NULL, NULL,
    M_BARDE, (PRECOMBATSPELL | SPELLLEVEL), 5, 8,
    {
      { R_AURA, 5, SPC_LEVEL },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_frighten, patzer
  },
  {
    SPL_OBJ_ANALYSESONG, "Lied des Ortes analysieren",
    "Wie Lebewesen, so haben auch Schiffe und Geb�ude und sogar Regionen "
    "ihr eigenes Lied, wenn auch viel schw�cher und schwerer zu h�ren. "
    "Und so, wie wie aus dem Lebenslied einer Person erkannt werden kann, "
    "ob diese unter einem Zauber steht, so ist dies auch bei Burgen, "
    "Schiffen oder Regionen m�glich.",
    "ZAUBERE [STUFE n] \'Lied des Ortes analysieren\' REGION\n"
    "ZAUBERE [STUFE n] \'Lied des Ortes analysieren\' GEB�UDE <Geb�ude-nr>\n"
    "ZAUBERE [STUFE n] \'Lied des Ortes analysieren\' SCHIFF <Schiff-nr>",
    "kc?",
    M_BARDE, (SPELLLEVEL|ONSHIPCAST), 5, 8,
    {
      { R_AURA, 3, SPC_LEVEL },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_analysesong_obj, patzer
  },
  {
    SPL_CERDDOR_DESTROY_MAGIC, "Lebenslied festigen",
    "Jede Verzauberung beeinflu�t das Lebenslied, schw�cht und verzerrt es. "
    "Der kundige Barde kann versuchen, das Lebenslied aufzufangen und zu "
    "verst�rken und die Ver�nderungen aus dem Lied zu tilgen.",
    "ZAUBERE [REGION x y] [STUFE n] \'Lebenslied festigen\' REGION\n"
    "ZAUBERE [REGION x y] [STUFE n] \'Lebenslied festigen\' EINHEIT <Einheit-Nr>\n"
    "ZAUBERE [REGION x y] [STUFE n] \'Lebenslied festigen\' GEB�UDE <Geb�ude-Nr>\n"
    "ZAUBERE [REGION x y] [STUFE n] \'Lebenslied festigen\' SCHIFF <Schiff-Nr>",
    "kc?",
    M_BARDE,
    (FARCASTING | SPELLLEVEL | ONSHIPCAST | ONETARGET | TESTCANSEE),
    2, 8,
    {
      { R_AURA, 5, SPC_LEVEL },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_destroy_magic, patzer
  },
  {
    SPL_MIGRANT, "Ritual der Aufnahme",
    "Dieses Ritual erm�glicht es, eine Einheit, egal welcher Art, in die "
    "eigene Partei aufzunehmen. Der um Aufnahme Bittende muss dazu willig "
    "und bereit sein, seiner alten Partei abzuschw�ren. Dies bezeugt er "
    "durch KONTAKTIEREn des Magiers. Auch wird er die Woche �ber "
    "ausschliesslich mit Vorbereitungen auf das Ritual besch�ftigt sein. "
    "Das Ritual wird fehlschlagen, wenn er zu stark an seine alte Partei "
    "gebunden ist, dieser etwa Dienst f�r seine teuere Ausbildung "
    "schuldet. Der das Ritual leitende Magier muss f�r die permanente "
    "Bindung des Aufnahmewilligen an seine Partei naturgem�� auch "
    "permanente Aura aufwenden. Pro Stufe und pro 1 permanente Aura kann "
    "er eine Person aufnehmen.", NULL,
    "u",
    M_BARDE, (UNITSPELL | SPELLLEVEL | ONETARGET | TESTCANSEE), 5, 9,
    {
      { R_AURA, 3, SPC_LEVEL },
      { R_PERMAURA, 1, SPC_LEVEL },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_migranten, patzer
  },
  {
    SPL_CERDDOR_FAMILIAR, "Vertrauten rufen",
    "Einem erfahrenen Magier wird irgendwann auf seinen Wanderungen ein "
    "ungew�hnliches Exemplar einer Gattung begegnen, welches sich dem "
    "Magier anschlie�en wird.", NULL, NULL,
    M_BARDE, (NOTFAMILIARCAST), 5, 9,
    {
      { R_AURA, 100, SPC_FIX },
      { R_PERMAURA, 5, SPC_FIX },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_summon_familiar, patzer
  },
  {
    SPL_RAISEPEASANTS, "Mob aufwiegeln",
    "Mit Hilfe dieses magischen Gesangs �berzeugt der Magier die Bauern "
    "der Region, sich ihm anzuschlie�en. Die Bauern werden ihre Heimat jedoch "
    "nicht verlassen, und keine ihrer Besitzt�mer fortgeben. Jede Woche "
    "werden zudem einige der Bauern den Bann abwerfen und auf ihre Felder "
    "zur�ckkehren. Wie viele Bauern sich dem Magier anschlie�en h�ngt von der "
    "Kraft seines Gesangs ab.", NULL, NULL,
    M_BARDE, (SPELLLEVEL | REGIONSPELL | TESTRESISTANCE), 5, 10,
    {
      { R_AURA, 4, SPC_LEVEL },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_raisepeasants, patzer
  },
  {
    SPL_SONG_RESISTMAGIC, "Gesang des wachen Geistes",
    "Dieses magische Lied wird, einmal mit Inbrunst gesungen, sich in der "
    "Region fortpflanzen, von Mund zu Mund springen und eine Zeitlang "
    "�berall zu vernehmen sein. Nach wie vielen Wochen der Gesang aus dem "
    "Ged�chnis der Region entschwunden ist, ist von dem Geschick des Barden "
    "abh�ngig. Bis das Lied ganz verklungen ist, wird seine Magie allen "
    "Verb�ndeten des Barden (HELFE BEWACHE), und nat�rlich auch seinen "
    "eigenem Volk, einen einmaligen Bonus von 15% "
    "auf die nat�rliche Widerstandskraft gegen eine Verzauberung "
    "verleihen.", NULL, NULL,
    M_BARDE,
    (FARCASTING | SPELLLEVEL | REGIONSPELL | TESTRESISTANCE),
    2, 10,
    {
      { R_AURA, 2, SPC_LEVEL },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_song_resistmagic, patzer
  },
  {
    SPL_DEPRESSION, "Gesang der Melancholie",
    "Mit diesem Gesang verbreitet der Barde eine melancholische, traurige "
    "Stimmung unter den Bauern. Einige Wochen lang werden sie sich in ihre "
    "H�tten zur�ckziehen und kein Silber in den Theatern und Tavernen lassen.", NULL, NULL,
    M_BARDE, (FARCASTING | REGIONSPELL | TESTRESISTANCE), 5, 11,
    {
      { R_AURA, 40, SPC_FIX },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_depression, patzer
  },
  {
    SPL_ARTEFAKT_NIMBLEFINGERRING, "Miriams flinke Finger",
    "Die ber�hmte Bardin Miriam bhean'Meddaf war bekannt f�r ihr "
    "au�ergew�hnliches Geschick mit der Harfe. Ihre Finger sollen sich "
    "so schnell �ber die Saiten bewegt haben, das sie nicht mehr erkennbar "
    "waren. Dieser Zauber, der recht einfach in einen Silberring zu bannen "
    "ist, bewirkt eine um das zehnfache verbesserte Geschicklichkeit und "
    "Gewandheit der Finger. (Das soll sie auch an anderer Stelle ausgenutzt "
    "haben, ihr Ruf als Falschspielerin war ber�chtigt.) Handwerker k�nnen "
    "somit das zehnfache produzieren, und bei einigen anderen T�tigkeiten "
    "k�nnte dies ebenfalls von Nutzen sein.", NULL, NULL,
    M_BARDE, (ONSHIPCAST), 5, 11,
    {
      { R_AURA, 20, SPC_FIX },
      { R_PERMAURA, 1, SPC_FIX },
      { R_SILVER, 1000, SPC_FIX },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_create_nimblefingerring, patzer
  },
  {
    SPL_SONG_SUSCEPTMAGIC, "Gesang des schwachen Geistes",
    "Dieses Lied, das in die magische Essenz der Region gewoben wird, "
    "schw�cht die nat�rliche Widerstandskraft gegen eine "
    "Verzauberung einmalig um 15%. Nur die Verb�ndeten des Barden "
    "(HELFE BEWACHE) sind gegen die Wirkung des Gesangs gefeit.", NULL, NULL,
    M_BARDE,
    (FARCASTING | SPELLLEVEL | REGIONSPELL | TESTRESISTANCE),
    2, 12,
    {
      { R_AURA, 2, SPC_LEVEL },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_song_susceptmagic, patzer
  },
  {
    SPL_SONG_OF_PEACE, "Gesang der Friedfertigkeit",
    "Dieser m�chtige Bann verhindert jegliche Attacken. Niemand in der "
    "ganzen Region ist f�hig seine Waffe gegen irgendjemanden zu erheben. "
    "Die Wirkung kann etliche Wochen andauern", NULL, NULL,
    M_BARDE, (SPELLLEVEL | REGIONSPELL | TESTRESISTANCE), 5, 12,
    {
      { R_AURA, 20, SPC_LEVEL },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_song_of_peace, patzer
  },
  {
    SPL_SONG_OF_ENSLAVE, "Gesang der Versklavung",
    "Dieser m�chtige Bann raubt dem Opfer seinen freien Willen und "
    "unterwirft sie den Befehlen des Barden. F�r einige Zeit wird das Opfer "
    "sich v�llig von seinen eigenen Leuten abwenden und der Partei des Barden "
    "zugeh�rig f�hlen.", NULL,
    "u",
    M_BARDE, (UNITSPELL | ONETARGET | TESTCANSEE), 5, 13,
    {
      { R_AURA, 40, SPC_FIX },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_charmingsong, patzer
  },
  {
    SPL_BIGRECRUIT, "Hohe Kunst der �berzeugung",
    "Aus 'Wanderungen' von Firudin dem Weisen: "
    "'In Weilersweide, nahe dem Wytharhafen, liegt ein kleiner Gasthof, der "
    "nur wenig besucht ist. Niemanden bekannt ist, das dieser Hof "
    "bis vor einigen Jahren die Bleibe des verbannten Wanderpredigers Grauwolf "
    "war. Nachdem er bei einer seiner ber�chtigten flammenden Reden fast die "
    "gesammte Bauernschaft angeworben hatte, wurde er wegen Aufruhr verurteilt "
    "und verbannt. Nur z�gerlich war er bereit mir das Geheimniss seiner "
    "�berzeugungskraft zu lehren.'", NULL, NULL,
    M_BARDE, (SPELLLEVEL), 5, 14,
    {
      { R_AURA, 20, SPC_LEVEL },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_bigrecruit, patzer
  },
  {
    SPL_RALLYPEASANTMOB, "Aufruhr beschwichtigen",
    "Mit Hilfe dieses magischen Gesangs kann der Magier eine Region in "
    "Aufruhr wieder beruhigen. Die Bauernhorden werden sich verlaufen "
    "und wieder auf ihre Felder zur�ckkehren.", NULL, NULL,
    M_BARDE, (FARCASTING), 5, 15,
    {
      { R_AURA, 30, SPC_FIX },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_rallypeasantmob, patzer
  },
  {
    SPL_RAISEPEASANTMOB, "Aufruhr verursachen",
    "Mit Hilfe dieses magischen Gesangs versetzt der Magier eine ganze "
    "Region in Aufruhr. Rebellierende Bauernhorden machen jedes Besteuern "
    "unm�glich, kaum jemand wird mehr f�r Gaukeleien Geld spenden und "
    "es k�nnen keine neuen Leute angeworben werden. Nach einigen Wochen "
    "beruhigt sich der Mob wieder.", NULL, NULL,
    M_BARDE, (FARCASTING | REGIONSPELL | TESTRESISTANCE), 5, 16,
    {
      { R_AURA, 40, SPC_FIX },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_raisepeasantmob, patzer
  },
  /* M_ASTRAL */
  {
    SPL_ANALYSEMAGIC, "analyze_magic", NULL,
    "ZAUBERE [STUFE n] \'Magie analysieren\' REGION\n"
    "ZAUBERE [STUFE n] \'Magie analysieren\' EINHEIT <Einheit-Nr>\n"
    "ZAUBERE [STUFE n] \'Magie analysieren\' GEB�UDE <Geb�ude-Nr>\n"
    "ZAUBERE [STUFE n] \'Magie analysieren\' SCHIFF <Schiff-Nr>",
    "kc?",
    M_ASTRAL, (SPELLLEVEL | UNITSPELL | ONSHIPCAST | TESTCANSEE), 5, 1,
    {
      { R_AURA, 1, SPC_LEVEL },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_analysemagic, patzer
  },
  {
    SPL_ITEMCLOAK, "concealing_aura", NULL, NULL,
    "u",
    M_ASTRAL, (SPELLLEVEL | UNITSPELL | ONSHIPCAST | ONETARGET), 5, 1,
    {
      { R_AURA, 1, SPC_LEVEL },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_itemcloak, patzer
  },
  {
    SPL_TYBIED_EARN_SILVER, "miracle_doctor", NULL, NULL, NULL,
    M_ASTRAL, (SPELLLEVEL|ONSHIPCAST), 5, 1,
    {
      { R_AURA, 1, SPC_LEVEL },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_earn_silver, patzer
  },
  {
    SPL_TYBIED_FUMBLESHIELD, "Schutz vor Magie",
    "Dieser Zauber legt ein antimagisches Feld um die Magier der "
    "Feinde und behindert ihre Zauber erheblich. Nur wenige werden "
    "die Kraft besitzen, das Feld zu durchdringen und ihren Truppen "
    "in der Schlacht zu helfen.", NULL, NULL,
    M_ASTRAL, (PRECOMBATSPELL | SPELLLEVEL), 2, 2,
    {
      { R_AURA, 3, SPC_LEVEL },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_fumbleshield, patzer
  },
#ifdef SHOWASTRAL_NOT_BORKED
  {
    SPL_SHOWASTRAL, "Astraler Blick",
    "Der Magier kann kurzzeitig in die Astralebene blicken und erf�hrt "
    "so alle Einheiten innerhalb eines astralen Radius von Stufe/5 Regionen.", NULL, NULL,
    M_ASTRAL, (SPELLLEVEL), 5, 2,
    {
      { R_AURA, 1, SPC_LEVEL },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_showastral, patzer
  },
#endif
  {
    SPL_RESISTMAGICBONUS, "Schutzzauber",
    "Dieser Zauber verst�rkt die nat�rliche Widerstandskraft gegen Magie. "
    "Eine so gesch�tzte Einheit ist auch gegen Kampfmagie weniger "
    "empfindlich. Pro Stufe reicht die Kraft des Magiers aus, um 5 Personen "
    "zu sch�tzen.", NULL,
    "u+",
    M_ASTRAL,
    (UNITSPELL | SPELLLEVEL | ONSHIPCAST | TESTCANSEE),
    2, 3,
    {
      { R_AURA, 5, SPC_LEVEL },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_resist_magic_bonus, patzer
  },
  {
    SPL_KEEPLOOT, "Beute bewahren",
    "Dieser Zauber verhindert, dass ein Teil der sonst im Kampf zerst�rten "
    "Gegenst�nde besch�digt wird. Die Verluste reduzieren sich um 5% pro "
    "Stufe des Zaubers bis zu einem Minimum von 25%.", NULL, NULL,
    M_ASTRAL, ( POSTCOMBATSPELL | SPELLLEVEL ), 5, 3,
    {
      { R_AURA, 1, SPC_LEVEL },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_keeploot, patzer
  },
  {
    SPL_ENTERASTRAL, "Astraler Weg",
    "Alte arkane Formeln erm�glichen es dem Magier, sich und andere in die "
    "astrale Ebene zu schicken. Der Magier kann (Stufe-3)*15 GE durch das "
    "kurzzeitig entstehende Tor schicken. Ist der Magier erfahren genug, "
    "den Zauber auf Stufen von 11 oder mehr zu zaubern, kann er andere "
    "Einheiten auch gegen ihren Willen auf die andere Ebene zwingen.", NULL,
    "u+",
    M_ASTRAL, (UNITSPELL|SPELLLEVEL), 7, 4,
    {
      { R_AURA, 2, SPC_LEVEL },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_enterastral, patzer
  },
  {
    SPL_LEAVEASTRAL, "Astraler Ausgang",
    "Der Magier konzentriert sich auf die Struktur der Realit�t und kann "
    "so die astrale Ebene verlassen. Er kann insgesamt (Stufe-3)*15 GE durch "
    "das kurzzeitig entstehende Tor schicken. Ist der Magier erfahren genug, "
    "den Zauber auf Stufen von 11 oder mehr zu zaubern, kann er andere "
    "Einheiten auch gegen ihren Willen auf die andere Ebene zwingen.",
    "ZAUBER [STUFE n] \'Astraler Ausgang\' <Ziel-X> <Ziel-Y> <Einheit-Nr> "
    "[<Einheit-Nr> ...]",
    "ru+",
    M_ASTRAL, (UNITSPELL |SPELLLEVEL), 7, 4,
    {
      { R_AURA, 2, SPC_LEVEL },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_leaveastral, patzer
  },
  {
    SPL_TRANSFERAURA_ASTRAL, "Auratransfer",
    "Mit Hilfe dieses Zauber kann der Magier eigene Aura im Verh�ltnis "
    "2:1 auf einen anderen Magier des gleichen Magiegebietes oder im "
    "Verh�ltnis 3:1 auf einen Magier eines anderen Magiegebietes "
    "�bertragen.",
    "ZAUBERE \'Auratransfer\' <Einheit-Nr> <investierte Aura>",
    "ui",
    M_ASTRAL, (UNITSPELL|ONSHIPCAST|ONETARGET), 1, 5,
    {
      { R_AURA, 1, SPC_FIX },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_transferaura, patzer
  },
  {
    SPL_SHOCKWAVE, "Schockwelle",
    "Dieser Zauber l��t eine Welle aus purer Kraft �ber die "
    "gegnerischen Reihen hinwegfegen.  Viele K�mpfer wird der Schock "
    "so benommen machen, da� sie f�r einen kurzen Moment nicht angreifen "
    "k�nnen.", NULL, NULL,
    M_ASTRAL, (COMBATSPELL|SPELLLEVEL), 5, 5,
    {
      { R_AURA, 1, SPC_LEVEL },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_stun, patzer
  },
  {
    SPL_ANTIMAGICZONE, "Astrale Schw�chezone",
    "Mit diesem Zauber kann der Magier eine Zone der astralen Schw�chung "
    "erzeugen, ein lokales Ungleichgewicht im Astralen Feld. Dieses "
    "Zone wird bestrebt sein, wieder in den Gleichgewichtszustand "
    "zu gelangen. Dazu wird sie jedem in dieser Region gesprochenen "
    "Zauber einen Teil seiner St�rke entziehen, die schw�cheren gar "
    "ganz absorbieren.", NULL, NULL,
    M_ASTRAL, (FARCASTING | SPELLLEVEL | REGIONSPELL | TESTRESISTANCE),
    2, 5,
    {
      { R_AURA, 3, SPC_LEVEL },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_antimagiczone, patzer
  },
  {
    SPL_TRUESEEING_TYBIED, "Erschaffe ein Amulett des wahren Sehens",
    "Der Spruch erm�glicht es einem Magier, ein Amulett des Wahren Sehens "
    "zu erschaffen. Das Amulett erlaubt es dem Tr�ger, alle Einheiten, die "
    "durch einen Ring der Unsichtbarkeit gesch�tzt sind, zu sehen. Einheiten "
    "allerdings, die sich mit ihrem Tarnungs-Talent verstecken, bleiben "
    "weiterhin unentdeckt.", NULL, NULL,
    M_ASTRAL, (ONSHIPCAST), 5, 5,
    {
      { R_AURA, 50, SPC_FIX },
      { R_SILVER, 3000, SPC_FIX },
      { R_PERMAURA, 1, SPC_FIX },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_createitem_trueseeing, patzer_createitem
  },
  {
    SPL_TYBIED_DESTROY_MAGIC, "Magiefresser",
    "Dieser Zauber erm�glicht dem Magier, Verzauberungen einer Einheit, "
    "eines Schiffes, Geb�udes oder auch der Region aufzul�sen.",
    "ZAUBERE [REGION x y] [STUFE n] \'Magiefresser\' REGION\n"
    "ZAUBERE [REGION x y] [STUFE n] \'Magiefresser\' EINHEIT <Einheit-Nr>\n"
    "ZAUBERE [REGION x y] [STUFE n] \'Magiefresser\' GEB�UDE <Geb�ude-Nr>\n"
    "ZAUBERE [REGION x y] [STUFE n] \'Magiefresser\' SCHIFF <Schiff-Nr>",
    "kc?",
    M_ASTRAL,
    (FARCASTING | SPELLLEVEL | ONSHIPCAST | ONETARGET | TESTCANSEE),
    2, 5,
    {
      { R_AURA, 4, SPC_LEVEL },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_destroy_magic, patzer
  },
  {
    SPL_PULLASTRAL, "Astraler Ruf",
    "Ein Magier, der sich in der astralen Ebene befindet, kann mit Hilfe "
    "dieses Zaubers andere Einheiten zu sich holen. Der Magier kann "
    "(Stufe-3)*15 GE durch das kurzzeitig entstehende Tor schicken. Ist der "
    "Magier erfahren genug, den Zauber auf Stufen von 13 oder mehr zu zaubern, "
    "kann er andere Einheiten auch gegen ihren Willen auf die andere Ebene "
    "zwingen.",
    "ZAUBER [STUFE n] \'Astraler Ruf\' <Ziel-X> <Ziel-Y> <Einheit-Nr> "
    "[<Einheit-Nr> ...]",
    "ru+",
    M_ASTRAL, (UNITSPELL | SEARCHGLOBAL | SPELLLEVEL), 7, 6,
    {
      { R_AURA, 2, SPC_LEVEL },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_pullastral, patzer
  },

  {
    SPL_FETCHASTRAL, "Ruf der Realit�t",
    "Ein Magier, welcher sich in der materiellen Welt befindet, kann er mit "
    "Hilfe dieses Zaubers Einheiten aus der angrenzenden Astralwelt herbeiholen. "
    "Ist der Magier erfahren genug, den Zauber auf Stufen von 13 oder mehr zu "
    "zaubern, kann er andere Einheiten auch gegen ihren Willen in die materielle "
    "Welt zwingen.", NULL,
    "u+",
    M_ASTRAL, (UNITSPELL | SEARCHGLOBAL | SPELLLEVEL), 7, 6,
    {
      { R_AURA, 2, SPC_LEVEL },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_fetchastral, patzer
  },
  {
    SPL_STEALAURA, "Stehle Aura",
    "Mit Hilfe dieses Zaubers kann der Magier einem anderen Magier seine "
    "Aura gegen dessen Willen entziehen und sich selber zuf�hren.", NULL,
    "u",
    M_ASTRAL,
    (FARCASTING | SPELLLEVEL | UNITSPELL | ONETARGET | TESTRESISTANCE | TESTCANSEE),
    3, 6,
    {
      { R_AURA, 2, SPC_LEVEL },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_stealaura, patzer
  },
  {
    SPL_FLYING_SHIP, "Luftschiff",
    "Diese magischen Runen bringen ein Boot oder Langboot f�r eine Woche "
    "zum fliegen. Damit kann dann auch Land �berquert werden. Die Zuladung "
    "von Langbooten ist unter der Einwirkung dieses Zaubers auf 100 "
    "Gewichtseinheiten begrenzt. F�r die Farbe der Runen muss eine spezielle "
    "Tinte aus einem Windbeutel und einem Schneekristall anger�hrt werden.", NULL,
    "s",
    M_ASTRAL, (ONSHIPCAST | SHIPSPELL | ONETARGET | TESTRESISTANCE), 5, 6,
    {
      { R_AURA, 10, SPC_FIX },
      { R_HIGHLAND_1, 1, SPC_FIX },
      { R_GLACIER_3, 1, SPC_FIX },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_flying_ship, patzer
  },
  {
    SPL_INVISIBILITY_TYBIED, "Erschaffe einen Ring der Unsichtbarkeit",
    "Mit diesem Spruch kann der Zauberer einen Ring der Unsichtbarkeit "
    "erschaffen. Der Tr�ger des Ringes wird f�r alle Einheiten anderer "
    "Parteien unsichtbar, egal wie gut ihre Wahrnehmung auch sein mag. In "
    "einer unsichtbaren Einheit muss jede Person einen Ring tragen.", NULL, NULL,
    M_ASTRAL, (ONSHIPCAST), 5, 6,
    {
      { R_AURA, 50, SPC_FIX },
      { R_SILVER, 3000, SPC_FIX },
      { R_PERMAURA, 1, SPC_FIX },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_createitem_invisibility, patzer_createitem
  },
  {
    SPL_CREATE_ANTIMAGICCRYSTAL, "Erschaffe Antimagiekristall",
    "Mit Hilfe dieses Zauber entzieht der Magier einem Quarzkristall "
    "all seine magischen Energien. Der Kristall wird dann, wenn er zu "
    "feinem Staub zermahlen und verteilt wird, die beim Zaubern "
    "freigesetzten magischen Energien aufsaugen und alle Zauber, "
    "welche in der betreffenden Woche in der Region gezaubert werden "
    "fehlschlagen lassen.", NULL, NULL,
    M_ASTRAL, (ONSHIPCAST), 5, 7,
    {
      { R_AURA, 50, SPC_FIX },
      { R_SILVER, 3000, SPC_FIX },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_create_antimagiccrystal, patzer_createitem
  },
  {
    SPL_DESTROY_MAGIC, "Fluch brechen",
    "Dieser Zauber erm�glicht dem Magier, gezielt eine bestimmte "
    "Verzauberung einer Einheit, eines Schiffes, Geb�udes oder auch "
    "der Region aufzul�sen.",
    "ZAUBERE [REGION x y] [STUFE n] \'Fluch brechen\' REGION <Zauber-Nr>\n"
    "ZAUBERE [REGION x y] [STUFE n] \'Fluch brechen\' EINHEIT <Einheit-Nr> <Zauber-Nr>\n"
    "ZAUBERE [REGION x y] [STUFE n] \'Fluch brechen\' GEB�UDE <Geb�ude-Nr> <Zauber-Nr>\n"
    "ZAUBERE [REGION x y] [STUFE n] \'Fluch brechen\' SCHIFF <Schiff-Nr> <Zauber-Nr>",
    "kcc",
    M_ASTRAL, (FARCASTING | SPELLLEVEL | ONSHIPCAST | TESTCANSEE), 3, 7,
    {
      { R_AURA, 3, SPC_LEVEL },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_destroy_curse, patzer
  },
  {
    SPL_ETERNIZEWALL, "Mauern der Ewigkeit",
    "Mit dieser Formel bindet der Magier auf ewig die Kr�fte der Erde in "
    "die Mauern des Geb�udes. Ein solcherma�en verzaubertes Geb�ude ist "
    "gegen den Zahn der Zeit gesch�tzt und ben�tigt keinen "
    "Unterhalt mehr.",
    "ZAUBERE \'Mauern der Ewigkeit\' <Geb�ude-Nr>",
    "b",
    M_ASTRAL,
    (SPELLLEVEL | BUILDINGSPELL | ONETARGET | TESTRESISTANCE | ONSHIPCAST),
    5, 7,
    {
      { R_AURA, 50, SPC_FIX },
      { R_PERMAURA, 1, SPC_FIX },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_eternizewall, patzer
  },
  {
    SPL_SCHILDRUNEN, "Runen des Schutzes",
    "Zeichnet man diese Runen auf die W�nde eines Geb�udes oder auf die "
    "Planken eines Schiffes, so wird es schwerer durch Zauber zu "
    "beeinflussen sein. Jedes Ritual erh�ht die Widerstandskraft des "
    "Geb�udes oder Schiffes gegen Verzauberung um 20%. "
    "Werden mehrere Schutzzauber �bereinander gelegt, so addiert "
    "sich ihre Wirkung, doch ein hundertprozentiger Schutz l��t sich so "
    "nicht erreichen. Der Zauber h�lt mindestens drei Wochen an, je nach "
    "Talent des Magiers aber auch viel l�nger.",
    "ZAUBERE \'Runen des Schutzes\' GEB�UDE <Geb�ude-Nr> | "
    "SCHIFF <Schiff-Nr>]",
    "kc",
    M_ASTRAL, (ONSHIPCAST | TESTRESISTANCE), 2, 8,
    {
      { R_AURA, 20, SPC_FIX },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_magicrunes, patzer
  },

  {
    SPL_REDUCESHIELD, "Schild des Fisches",
    "Dieser Zauber vermag dem Gegner ein geringf�gig versetztes Bild der "
    "eigenen Truppen vorzuspiegeln, so wie der Fisch im Wasser auch nicht "
    "dort ist wo er zu sein scheint. Von jedem Treffer kann so die H�lfte "
    "des Schadens unsch�dlich abgeleitet werden. Doch h�lt der Schild nur "
    "einige Hundert Schwerthiebe aus, danach wird er sich aufl�sen. "
    "Je st�rker der Magier, desto mehr Schaden h�lt der Schild aus.", NULL, NULL,
    M_ASTRAL, (PRECOMBATSPELL | SPELLLEVEL), 2, 8,
    {
      { R_AURA, 4, SPC_LEVEL },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_reduceshield, patzer
  },
  {
    SPL_SPEED, "Beschleunigung",
    "Dieser Zauber beschleunigt einige K�mpfer auf der eigenen Seite "
    "so, dass sie w�hrend des gesamten Kampfes in einer Kampfrunde zweimal "
    "angreifen k�nnen.", NULL, NULL,
    M_ASTRAL, (PRECOMBATSPELL | SPELLLEVEL), 5, 9,
    {
      { R_AURA, 5, SPC_LEVEL },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_speed, patzer
  },
  {
    SPL_ARTEFAKT_OF_POWER, "Erschaffe einen Ring der Macht",
    "Dieses m�chtige Ritual erschafft einen Ring der Macht. Ein Ring "
    "der Macht erh�ht die St�rke jedes Zaubers, den sein Tr�ger zaubert, "
    "als w�re der Magier eine Stufe besser.", NULL, NULL,
    M_ASTRAL, (ONSHIPCAST), 5, 9,
    {
      { R_AURA, 100, SPC_FIX },
      { R_SILVER, 4000, SPC_FIX },
      { R_PERMAURA, 1, SPC_FIX },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_createitem_power, patzer_createitem
  },
  {
    SPL_VIEWREALITY, "Blick in die Realit�t",
    "Der Magier kann mit Hilfe dieses Zaubers aus der Astral- in die "
    "materielle Ebene blicken und die Regionen und Einheiten genau "
    "erkennen.", NULL, NULL,
    M_ASTRAL, (0), 5, 10,
    {
      { R_AURA, 40, SPC_FIX },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_viewreality, patzer
  },
  {
    SPL_BAG_OF_HOLDING, "Erschaffe einen Beutel des Negativen Gewichts",
    "Dieser Beutel umschlie�t eine kleine Dimensionsfalte, in der bis "
    "zu 200 Gewichtseinheiten transportiert werden k�nnen, ohne dass "
    "sie auf das Traggewicht angerechnet werden.  Pferde und andere "
    "Lebewesen sowie besonders sperrige Dinge (Wagen und Katapulte) k�nnen "
    "nicht in dem Beutel transportiert werden.  Auch ist es nicht m�glich, "
    "einen Zauberbeutel in einem anderen zu transportieren.  Der Beutel "
    "selber wiegt 1 GE.", NULL, NULL,
    M_ASTRAL, (ONSHIPCAST), 5, 10,
    {
      { R_AURA, 30, SPC_FIX },
      { R_PERMAURA, 1, SPC_FIX },
      { R_SILVER, 5000, SPC_FIX },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_create_bag_of_holding, patzer
  },
  {
    SPL_SPEED2, "Zeitdehnung",
    "Diese praktische Anwendung des theoretischen Wissens um Raum und Zeit "
    "erm�glicht es, den Zeitflu� f�r einige Personen zu ver�ndern. Auf "
    "diese Weise ver�nderte Personen bekommen f�r einige Wochen doppelt "
    "soviele Bewegungspunkte und doppelt soviele Angriffe pro Runde.", NULL,
    "u+",
    M_ASTRAL, (UNITSPELL | SPELLLEVEL | ONSHIPCAST | TESTCANSEE), 5, 11,
    {
      { R_AURA, 5, SPC_LEVEL },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_speed2, patzer
  },
  {
    SPL_ARMORSHIELD, "R�stschild",
    "Diese vor dem Kampf zu zaubernde Ritual gibt den eigenen Truppen "
    "einen zus�tzlichen Bonus auf ihre R�stung. Jeder Treffer "
    "reduziert die Kraft des Zaubers, so dass der Schild sich irgendwann "
    "im Kampf aufl�sen wird.", NULL, NULL,
    M_ASTRAL, (PRECOMBATSPELL | SPELLLEVEL), 2, 12,
    {
      { R_AURA, 4, SPC_LEVEL },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_armorshield, patzer
  },
  {
    SPL_TYBIED_FAMILIAR, "Vertrauten rufen",
    "Einem erfahrenen Magier wird irgendwann auf seinen Wanderungen ein "
    "ungew�hnliches Exemplar einer Gattung begegnen, welches sich dem "
    "Magier anschlie�en wird.", NULL, NULL,
    M_ASTRAL, (NOTFAMILIARCAST), 5, 12,
    {
      { R_AURA, 100, SPC_FIX },
      { R_PERMAURA, 5, SPC_FIX },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_summon_familiar, patzer
  },
  {
    SPL_MOVECASTLE, "Belebtes Gestein",
    "Dieses kr�ftezehrende Ritual beschw�rt mit Hilfe einer Kugel aus "
    "konzentriertem Laen einen gewaltigen Erdelementar und bannt ihn "
    "in ein Geb�ude. Dem Elementar kann dann befohlen werden, das "
    "Geb�ude mitsamt aller Bewohner in eine Nachbarregion zu tragen. "
    "Die St�rke des beschworenen Elementars h�ngt vom Talent des "
    "Magiers ab: Der Elementar kann maximal [Stufe-12]*250 Gr��eneinheiten "
    "gro�e Geb�ude versetzen. Das Geb�ude wird diese Prozedur nicht "
    "unbesch�digt �berstehen.",
    "ZAUBER [STUFE n] \'Belebtes Gestein\' <Burg-Nr> <Richtung>",
    "bc",
    M_ASTRAL,
    (SPELLLEVEL | BUILDINGSPELL | ONETARGET | TESTRESISTANCE),
    5, 13,
    {
      { R_AURA, 10, SPC_LEVEL },
      { R_PERMAURA, 1, SPC_FIX },
      { R_EOG, 5, SPC_FIX },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_movecastle, patzer
  },
  {
    SPL_DISRUPTASTRAL, "St�re Astrale Integrit�t",
    "Dieser Zauber bewirkt eine schwere St�rung des Astralraums. Innerhalb "
    "eines astralen Radius von Stufe/5 Regionen werden alle Astralwesen, "
    "die dem Zauber nicht wiederstehen k�nnen, aus der astralen Ebene "
    "geschleudert. Der astrale Kontakt mit allen betroffenen Regionen ist "
    "f�r Stufe/3 Wochen gest�rt.", NULL, NULL,
    M_ASTRAL, (REGIONSPELL), 4, 14,
    {
      { R_AURA, 140, SPC_FIX },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_disruptastral, patzer
  },
  {
    SPL_PERMTRANSFER, "Opfere Kraft",
    "Mit Hilfe dieses Zaubers kann der Magier einen Teil seiner magischen "
    "Kraft permanent auf einen anderen Magier �bertragen. Auf einen Tybied-"
    "Magier kann er die H�lfte der eingesetzten Kraft �bertragen, auf einen "
    "Magier eines anderen Gebietes ein Drittel.",
    "ZAUBERE \'Opfere Kraft\' <Einheit-Nr> <Aura>",
    "ui",
    M_ASTRAL, (UNITSPELL|ONETARGET), 1, 15,
    {
      { R_AURA, 100, SPC_FIX },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_permtransfer, patzer
  },
  /* M_GRAU */
  /*  Definitionen von Create_Artefaktspr�chen    */
  {
    SPL_ARTEFAKT_OF_AURAPOWER, "Erschaffe einen Fokus",
    "Der auf diesem Gegenstand liegende Zauber erleichtert es dem "
    "Zauberers enorm gr��ere Mengen an Aura zu beherrschen.", NULL, NULL,
    M_GRAU, (ONSHIPCAST), 5, 9,
    {
      { R_AURA, 100, SPC_FIX },
      { R_PERMAURA, 1, SPC_FIX },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_createitem_aura, patzer_createitem
  },
  {
    SPL_ARTEFAKT_OF_REGENERATION, "Regeneration",
    "Der auf diesem Gegenstand liegende Zauber saugt die diffusen "
    "magischen Energien des Lebens aus der Umgebung auf und l��t sie "
    "seinem Tr�ger zukommen.", NULL, NULL,
    M_GRAU, (ONSHIPCAST), 5, 9,
    {
      { R_AURA, 100, SPC_FIX },
      { R_PERMAURA, 1, SPC_FIX },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_createitem_regeneration, patzer_createitem
  },
  {
    SPL_ARTEFAKT_CHASTITYBELT, "Erschaffe ein Amulett der Keuschheit",
    "Dieses Amulett in Gestalt einer orkischen Matrone unterdr�ckt den "
    "Fortpflanzungstrieb eines einzelnen Orks sehr zuverl�ssig. Ein Ork "
    "mit Amulett der Keuschheit wird sich nicht mehr vermehren.", NULL, NULL,
    M_GRAU, (ONSHIPCAST), 5, 7,
    {
      { R_AURA, 50, SPC_FIX },
      { R_SILVER, 3000, SPC_FIX },
      { R_PERMAURA, 1, SPC_FIX },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_createitem_chastitybelt, patzer_createitem
  },
  {
    SPL_METEORRAIN, "Meteorregen",
    "Ein Schauer von Meteoren regnet �ber das Schlachtfeld.", NULL, NULL,
    M_GRAU, (COMBATSPELL | SPELLLEVEL), 5, 3,
    {
      { R_AURA, 1, SPC_LEVEL },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_kampfzauber, patzer
  },
  {
    SPL_ARTEFAKT_RUNESWORD, "Erschaffe ein Runenschwert",
    "Mit diesem Spruch erzeugt man ein Runenschwert. Die Klinge des "
    "schwarzen "
    "Schwertes ist mit alten, magischen Runen verziert, und ein seltsames "
    "Eigenleben erf�llt die warme Klinge. Um es zu benutzen, muss man "
    "ein Schwertk�mpfer von beachtlichem Talent (7) sein. "
    "Der Tr�ger des Runenschwertes erh�lt einen Talentbonus von +4 im Kampf "
    "und wird so gut wie immun gegen alle Formen von Magie.", NULL, NULL,
    M_GRAU, (ONSHIPCAST), 5, 6,
    {
      { R_AURA, 100, SPC_FIX },
      { R_PERMAURA, 1, SPC_FIX },
      { R_SILVER, 1000, SPC_FIX },
      { R_EOGSWORD, 1, SPC_FIX },
      { 0, 0, 0 }
    },
    (spell_f)sp_createitem_runesword, patzer_createitem
  },
  {
    SPL_BECOMEWYRM, "Wyrmtransformation",
    "Mit Hilfe dieses Zaubers kann sich der Magier permanent in einen "
    "m�chtigen Wyrm verwandeln. Der Magier beh�lt seine Talente und "
    "M�glichkeiten, bekommt jedoch die Kampf- und Bewegungseigenschaften "
    "eines Wyrms. Der Odem des Wyrms wird sich mit steigendem Magie-Talent "
    "verbessern. Der Zauber ist sehr kraftraubend und der Wyrm wird einige "
    "Zeit brauchen, um sich zu erholen.", NULL, NULL,
    M_GRAU, 0, 5, 1,
    {
      { R_AURA, 1, SPC_FIX },
      { R_PERMAURA, 1, SPC_FIX },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_becomewyrm, patzer
  },
  /* Monsterspr�che */
  { SPL_FIREDRAGONODEM, "Feuriger Drachenodem",
    "Verbrennt die Feinde", NULL, NULL,
    M_GRAU, (COMBATSPELL), 5, 3,
    {
      { R_AURA, 1, SPC_FIX },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_dragonodem, patzer
  },
  { SPL_DRAGONODEM, "Eisiger Drachenodem",
    "T�tet die Feinde", NULL, NULL,
    M_GRAU, (COMBATSPELL), 5, 6,
    {
      { R_AURA, 2, SPC_FIX },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_dragonodem, patzer
  },
  { SPL_WYRMODEM, "Gro�er Drachenodem",
    "Verbrennt die Feinde", NULL, NULL,
    M_GRAU, (COMBATSPELL), 5, 12,
    {
      { R_AURA, 3, SPC_FIX },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_dragonodem, patzer
  },
  { SPL_DRAINODEM, "Schattenodem",
    "Entzieht Talentstufen und macht Schaden wie Gro�er Odem", NULL, NULL,
    M_GRAU, (COMBATSPELL), 5, 12,
    {
      { R_AURA, 4, SPC_FIX },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_dragonodem, patzer
  },
  {
    SPL_AURA_OF_FEAR, "Furchteinfl��ende Aura",
    "Panik", NULL, NULL,
    M_GRAU, (COMBATSPELL), 5, 12,
    {
      { R_AURA, 1, SPC_LEVEL },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_flee, patzer
  },
  {
    SPL_SHADOWCALL, "Schattenruf",
    "Ruft Schattenwesen.", NULL, NULL,
    M_GRAU, (PRECOMBATSPELL), 5, 12,
    {
      { R_AURA, 2, SPC_LEVEL },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_shadowcall, patzer
  },
  {
    SPL_IMMOLATION, "Feuersturm",
    "Verletzt alle Gegner.", NULL, NULL,
    M_GRAU, (COMBATSPELL), 5, 12,
    {
      { R_AURA, 2, SPC_LEVEL },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_immolation, patzer
  },
  { SPL_FIREODEM, "Feuerwalze",
    "T�tet die Feinde", NULL, NULL,
    M_GRAU, (COMBATSPELL), 5, 8,
    {
      { R_AURA, 2, SPC_FIX },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_immolation, patzer
  },
  { SPL_ICEODEM, "Eisnebel",
    "T�tet die Feinde", NULL, NULL,
    M_GRAU, (COMBATSPELL), 5, 8,
    {
      { R_AURA, 2, SPC_FIX },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_immolation, patzer
  },
  { SPL_ACIDODEM, "S�urenebel",
    "T�tet die Feinde", NULL, NULL,
    M_GRAU, (COMBATSPELL), 5, 8,
    {
      { R_AURA, 2, SPC_FIX },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_immolation, patzer
  },
  #ifdef WDW_PYRAMIDSPELL
  {
    SPL_WDWPYRAMID_TRAUM, "Traum von den G�ttern",
    "Mit Hilfe dieses Zaubers kann der Magier erkennen, ob eine "
    "Region f�r den Pyramidenbau geeignet ist.", NULL, NULL,
    M_TRAUM, (0), 5, 4,
    {
      { R_AURA, 2, SPC_FIX },
      { R_PLAIN_3, 1, SPC_FIX },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_wdwpyramid, patzer
  },
  {
    SPL_WDWPYRAMID_ASTRAL, "G�ttliches Netz",
    "Mit Hilfe dieses Zaubers kann der Magier erkennen, ob eine "
    "Region f�r den Pyramidenbau geeignet ist.", NULL, NULL,
    M_ASTRAL, (0), 5, 3,
    {
      { R_AURA, 4, SPC_FIX },
      { R_WISE, 1, SPC_FIX },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_wdwpyramid, patzer
  },

  {
    SPL_WDWPYRAMID_DRUIDE, "Kraft der Natur",
    "Mit Hilfe dieses Zaubers kann der Magier erkennen, ob eine "
    "Region f�r den Pyramidenbau geeignet ist.", NULL, NULL,
    M_DRUIDE, (0), 5, 5,
    {
      { R_AURA, 3, SPC_FIX },
      { R_MALLORN, 5, SPC_FIX },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_wdwpyramid, patzer
  },
  {
    SPL_WDWPYRAMID_BARDE, "Gesang der G�tter",
    "Mit Hilfe dieses Zaubers kann der Magier erkennen, ob eine "
    "Region f�r den Pyramidenbau geeignet ist.", NULL, NULL,
    M_BARDE, (0), 5, 4,
    {
      { R_AURA, 2, SPC_FIX },
      { R_HIGHLAND_3, 1, SPC_FIX },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_wdwpyramid, patzer
  },
  {
    SPL_WDWPYRAMID_CHAOS, "G�ttliche Macht",
    "Mit Hilfe dieses Zaubers kann der Magier erkennen, ob eine "
    "Region f�r den Pyramidenbau geeignet ist.", NULL, NULL,
    M_CHAOS, (0), 5, 5,
    {
      { R_AURA, 1, SPC_FIX },
      { R_PERMAURA, 1, SPC_FIX },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    (spell_f)sp_wdwpyramid, patzer
  },
  #endif
  /* SPL_NOSPELL  MUSS der letzte Spruch der Liste sein*/
  {
    SPL_NOSPELL, "Keiner", NULL, NULL, NULL, 0, 0, 0, 0,
    {
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 }
    },
    NULL, NULL
  }
};

void
init_spells(void)
{
  int i;

  /* register all the old spells in the spelldata array */
  for (i=0;spelldaten[i].id!=SPL_NOSPELL;++i) {
    register_spell(spelldaten+i);
  }
}

static boolean 
chaosgate_valid(const border * b)
{
  const attrib * a = a_findc(b->from->attribs, &at_direction);
  if (!a) a = a_findc(b->to->attribs, &at_direction);
  if (!a) return false;
  return true;
}

struct region * 
chaosgate_move(const border * b, struct unit * u, struct region * from, struct region * to, boolean routing)
{
  if (!routing) {
    int maxhp = u->hp / 4;
    if (maxhp<u->number) maxhp = u->number;
    u->hp = maxhp;
  }
  return to;
}

border_type bt_chaosgate = {
  "chaosgate", VAR_NONE,
  b_transparent, /* transparent */
  NULL, /* init */
  NULL, /* destroy */
  NULL, /* read */
  NULL, /* write */
  b_blocknone, /* block */
  NULL, /* name */
  b_rinvisible, /* rvisible */
  b_finvisible, /* fvisible */
  b_uinvisible, /* uvisible */
  chaosgate_valid,
  chaosgate_move
};

