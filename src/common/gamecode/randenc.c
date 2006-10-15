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
#include "randenc.h"

#include "economy.h"
#include "luck.h"
#include "monster.h"

/* kernel includes */
#include <kernel/alchemy.h>
#include <kernel/battle.h>
#include <kernel/building.h>
#include <kernel/curse.h>
#include <kernel/equipment.h>
#include <kernel/faction.h>
#include <kernel/item.h>
#include <kernel/karma.h>
#include <kernel/magic.h>
#include <kernel/message.h>
#include <kernel/movement.h>
#include <kernel/names.h>
#include <kernel/order.h>
#include <kernel/plane.h>
#include <kernel/pool.h>
#include <kernel/race.h>
#include <kernel/region.h>
#include <kernel/ship.h>
#include <kernel/skill.h>
#include <kernel/terrain.h>
#include <kernel/terrainid.h>
#include <kernel/unit.h>

/* attributes includes */
#include <attributes/racename.h>

/* util includes */
#include <util/rand.h>
#include <util/message.h>
#include <util/rng.h>

/* libc includes */
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <math.h>
#include <stdlib.h>

#include <attributes/iceberg.h>
extern attrib_type at_unitdissolve;
extern attrib_type at_orcification;

/* In a->data.ca[1] steht der Prozentsatz mit dem sich die Einheit
 * aufl�st, in a->data.ca[0] kann angegeben werden, wohin die Personen
 * verschwinden. Passiert bereits in der ersten Runde! */
static void
dissolve_units(void)
{
	region *r;
	unit *u;
	int n;
	int i;

	for (r=regions;r;r=r->next) {
		for (u=r->units;u;u=u->next) {
			attrib * a = a_find(u->attribs, &at_unitdissolve);
			if (a) {
        const char * str = NULL;
				if (u->age == 0 && a->data.ca[1] < 100) continue;

				/* TODO: Durch einzelne Berechnung ersetzen */
				if (a->data.ca[1] == 100) {
					n = u->number;
				} else {
					n = 0;
					for (i=0;i<u->number;i++) {
						if (rng_int()%100 < a->data.ca[1]) n++;
					}
				}

				/* wenn keiner verschwindet, auch keine Meldung */
				if (n == 0) {
					continue;
				}

				scale_number(u, u->number - n);

				switch(a->data.ca[0]) {
				case 1:
					rsetpeasants(r, rpeasants(r) + n);
					if (n == 1) {
						str = "kehrte auf sein Feld zur�ck.";
					} else {
						str = "kehrten auf ihre Felder zur�ck.";
					}
					break;
				case 2:
					if (r->land && !fval(r, RF_MALLORN)) {
  					rsettrees(r, 2, rtrees(r,2) + n);
						if (n == 1) {
							str = "wurde zum Baum.";
						} else {
							str = "wurden zu B�umen.";
						}
					} else {
						if(n == 1) {
							str = "verfaulte.";
						} else {
							str = "verfaulten.";
						}
					}
					break;
				default:
					if (u->race == new_race[RC_STONEGOLEM] || u->race == new_race[RC_IRONGOLEM]) {
						if (n == 1) {
							str = "zerfiel zu Staub.";
						} else {
							str = "zerfielen zu Staub.";
						}
					}else{
						if (n == 1) {
							str = "verschwand �ber Nacht.";
						}else{
							str = "verschwanden �ber Nacht.";
						}
					}
					break;
				}
        ADDMSG(&u->faction->msgs, msg_message("dissolve_units",
          "unit region number race action", u, r, n, u->race, str));
			}
		}
	}

	remove_empty_units();
}

static int
improve_all(faction * f, skill_t sk, int by_weeks)
{
  unit *u;
  boolean ret = by_weeks;
  
  for (u = f->units; u; u = u->nextF) {
    if (has_skill(u, sk)) {
      int weeks = 0;
      for (;weeks!=by_weeks;++weeks) {
        learn_skill(u, sk, 1.0);
        ret = 0;
      }
    }
  }
  
  return ret;
}

void
find_manual(region * r, unit * u)
{
  skill_t skill = NOSKILL;
  sprintf(buf, "%s stolper%c bei der Erforschung der Region �ber ",
          unitname(u), "nt"[u->number == 1]);
  
  switch (rng_int() % 4) {
  case 0:
    scat("die Ruine eines alten Tempels");
    break;
  case 1:
    scat("eine alte Burgruine");
    break;
  case 2:
    scat("ein zerfallenes Bauernhaus");
    break;
  case 3:
    scat("eine Leiche am Wegesrand");
    break;
  }
  
  scat(". Bei der Durchsuchung ");
  if (u->number == 1) {
    scat("st��t");
  } else {
    scat("sto�en");
  }
  scat(" sie auf das zerfledderte Exemplar eines alten Buches, betitelt ");
  
  switch (rng_int() % 36) {
  case 0:
    scat("\'Magie der Elemente\'");
    skill = SK_MAGIC;
    break;
  case 1:
  case 2:
  case 3:
  case 4:
    scat("\'Schwerter, Armbr�ste, Langb�gen\'");
    skill = SK_WEAPONSMITH;
    break;
  case 5:
  case 6:
    scat("\'Gorms Almanach der Rationellen Kriegsf�hrung\'");
    skill = SK_TACTICS;
    break;
  case 7:
  case 8:
  case 9:
  case 10:
    scat("\'Katamarane, Koggen, Karavellen\'");
    skill = SK_SHIPBUILDING;
    break;
  case 11:
  case 12:
  case 13:
  case 14:
    scat("\'Wege der Sterne\'");
    skill = SK_SAILING;
    break;
  case 15:
  case 16:
  case 17:
    scat("\'Nadishahs Kleine Gift- und Kr�uterkunde\'");
    skill = SK_HERBALISM;
    break;
  case 18:
  case 19:
    scat("\'Mandricks Kompendium der Alchemie\'");
    skill = SK_ALCHEMY;
    break;
  case 20:
  case 21:
  case 22:
  case 23:
    scat("\'Die Konstruktion der Burgen und Schl�sser von Zentralandune\'");
    skill = SK_BUILDING;
    break;
  case 24:
  case 25:
  case 26:
  case 27:
    scat("\'Die Esse\'");
    skill = SK_ARMORER;
    break;
  case 28:
  case 29:
  case 30:
  case 31:
    scat("\'�ber die Gewinnung von Erzen\'");
    skill = SK_MINING;
    break;
  case 32:
  case 33:
  case 34:
  case 35:
    scat("\'Barinions Lieder, eine Einf�hrung f�r Unbedarfte\'");
    skill = SK_ENTERTAINMENT;
    break;
  }
  
  scat(". Der Wissensschub ist enorm.");
  addmessage(r, u->faction, buf, MSG_EVENT, ML_IMPORTANT);

  if (improve_all(u->faction, skill, 3) == 3) {
    int i;
    for (i=0;i!=9;++i) learn_skill(u, skill, 1.0);
  }
}

static void
get_unit(region * r, unit * u)
{
	unit *newunit;
	sprintf(buf, "%s entdeck%s ein kleines Dorf. Die meisten H�user "
			"wurden durch einen �ber die Ufer getretenen Flu� zerst�rt. Eine "
			"Gruppe der verzweifelten Menschen schlie�t sich deiner Partei an.",
			unitname(u), "en\0t" + (3 - 3 * (u->number == 1)));

	addmessage(r, u->faction, buf, MSG_EVENT, ML_IMPORTANT);

	newunit = createunit(r, u->faction, rng_int() % 20 + 3, u->faction->race);
  fset(newunit, UFL_ISNEW|UFL_MOVED);
	set_string(&newunit->name, "Dorfbewohner");
  if (fval(u, UFL_PARTEITARNUNG)) {
    fset(newunit, UFL_PARTEITARNUNG);
  }
  equip_unit(newunit, get_equipment("random_villagers"));
}

static void
get_allies(region * r, unit * u)
{
	unit *newunit = NULL;
  assert(u->number);

	switch (rterrain(r)) {

	case T_PLAIN:
		if (!r_isforest(r)) {
			if (get_money(u) / u->number < 100 + rng_int() % 200)
				return;
			newunit = createunit(r, u->faction, rng_int() % 8 + 2, u->faction->race);
			set_string(&newunit->name, "S�ldner");
      equip_unit(newunit, get_equipment("random_plain"));
			break;
		} else {
			if (eff_skill(u, SK_LONGBOW, r) < 3
				&& eff_skill(u, SK_HERBALISM, r) < 2
				&& eff_skill(u, SK_MAGIC, r) < 2) {
				return;
			}
			newunit = createunit(r, u->faction, rng_int() % 6 + 2, u->faction->race);
			set_string(&newunit->name, "Waldbewohner");
      equip_unit(newunit, get_equipment("random_forest"));
		}
		break;

	case T_SWAMP:
		if (eff_skill(u, SK_OBSERVATION, r) <= 3) {
			return;
		}
		newunit = createunit(r, u->faction, rng_int() % 6 + 2, u->faction->race);
		set_string(&newunit->name, "Sumpfbewohner");
    equip_unit(newunit, get_equipment("random_swamp"));
		break;

	case T_DESERT:
		if (eff_skill(u, SK_RIDING, r) <= 2) {
			return;
		}
		newunit = createunit(r, u->faction, rng_int() % 12 + 2, u->faction->race);
		set_string(&newunit->name, "Berber");
    equip_unit(newunit, get_equipment("random_desert"));
		break;

	case T_HIGHLAND:
		if (eff_skill(u, SK_MELEE, r) <= 1) {
			return;
		}
		newunit = createunit(r, u->faction, rng_int() % 8 + 2, u->faction->race);
		set_string(&newunit->name, "Hochlandbarbaren");
    equip_unit(newunit, get_equipment("random_highland"));
		break;

	case T_MOUNTAIN:
		if (eff_skill(u, SK_MELEE, r) <= 1
			|| eff_skill(u, SK_TRADE, r) <= 2) {
			return;
		}
		newunit = createunit(r, u->faction, rng_int() % 6 + 2, u->faction->race);
		set_string(&newunit->name, "Bergbewohner");
    equip_unit(newunit, get_equipment("random_mountain"));

		break;

	case T_GLACIER:
		if (eff_skill(u, SK_MELEE, r) <= 1
			|| eff_skill(u, SK_TRADE, r) <= 1) {
			return;
		}
		newunit = createunit(r, u->faction, rng_int() % 4 + 2, u->faction->race);
		set_string(&newunit->name, "Eisleute");
    equip_unit(newunit, get_equipment("random_glacier"));

		break;
	}

  if (newunit!=NULL) {
	  u_setfaction(newunit, u->faction);
	  set_racename(&newunit->attribs, get_racename(u->attribs));
	  if(u->race->flags & RCF_SHAPESHIFT) {
		  newunit->irace = u->irace;
	  }
	  if (fval(u, UFL_PARTEITARNUNG)) fset(newunit, UFL_PARTEITARNUNG);
	  fset(newunit, UFL_ISNEW);

	  sprintf(buf, "Pl�tzlich stolper%c %s �ber einige %s. Nach kurzem "
		  "Z�gern entschlie�en sich die %s, sich Deiner Partei anzuschlie�en.",
	  u->number == 1 ? 't' : 'n', unitname(u), newunit->name, newunit->name);

	  addmessage(r, u->faction, buf, MSG_EVENT, ML_IMPORTANT);
  }
}

static void
encounter(region * r, unit * u)
{
	if (!fval(r, RF_ENCOUNTER)) return;
	freset(r, RF_ENCOUNTER);
	if (rng_int() % 100>=ENCCHANCE) return;
	switch (rng_int() % 3) {
	case 0:
		find_manual(r, u);
		break;
	case 1:
		get_unit(r, u);
		break;
	case 2:
		get_allies(r, u);
		break;
	}
}

void
encounters(void)
{
	region *r;

	for (r = regions; r; r = r->next) {
		if (!fval(r->terrain, SEA_REGION) && fval(r, RF_ENCOUNTER)) {
			int c = 0;
      unit * u;
			for (u = r->units; u; u = u->next) {
				c += u->number;
			}

			if (c > 0) {
        int i = 0;
				int n = rng_int() % c;

        for (u = r->units; u; u = u->next) {
					if (i+u->number>c) break;
				}
        assert(u && u->number);
        encounter(r, u);
			}
		}
	}
}

static const terrain_type *
chaosterrain(void) 
{
  static const terrain_type ** types;
  static int numtypes;

  if (numtypes==0) {
    const terrain_type * terrain;
    for (terrain=terrains();terrain!=NULL;terrain=terrain->next) {
      if (fval(terrain, LAND_REGION) && terrain->herbs) {
        ++numtypes;
      }
    }
    types = malloc(sizeof(terrain_type)*numtypes);
    numtypes = 0;
    for (terrain=terrains();terrain!=NULL;terrain=terrain->next) {
      if (fval(terrain, LAND_REGION) && terrain->herbs) {
        types[numtypes++] = terrain;
      }
    }
  }
  return types[rng_int() % numtypes];
}

void
chaos(region * r)
{
	unit *u = NULL, *u2;
	building *b, *b2;

	if (rng_int() % 100 < 8) {
		switch (rng_int() % 3) {
		case 0:				/* Untote */
			if (!fval(r->terrain, SEA_REGION)) {
				u = random_unit(r);
				if (u && playerrace(u->race)) {
					sprintf(buf, "%s scheint von einer seltsamen Krankheit befallen.",
							unitname(u));
					addmessage(0, u->faction, buf, MSG_EVENT, ML_IMPORTANT);
					u_setfaction(u, findfaction(MONSTER_FACTION));
					u->race = new_race[RC_GHOUL];
				}
			}
			break;
		case 1:				/* Drachen */
			if (random_unit(r)) {
				int mfac = 0;
				switch (rng_int() % 3) {
				case 0:
					mfac = 100;
					u = createunit(r, findfaction(MONSTER_FACTION), rng_int() % 8 + 1, new_race[RC_FIREDRAGON]);
					if (u->number == 1) {
						set_string(&u->name, "Feuerdrache");
					} else {
						set_string(&u->name, "Feuerdrachen");
					}
					break;
				case 1:
					mfac = 500;
					u = createunit(r, findfaction(MONSTER_FACTION), rng_int() % 4 + 1, new_race[RC_DRAGON]);
					if (u->number == 1) {
						set_string(&u->name, "Drache");
					} else {
						set_string(&u->name, "Drachen");
					}
					break;
				case 2:
					mfac = 1000;
					u = createunit(r, findfaction(MONSTER_FACTION), rng_int() % 2 + 1, new_race[RC_WYRM]);
					if (u->number == 1) {
						set_string(&u->name, "Wyrm");
					} else {
						set_string(&u->name, "Wyrme");
					}
					break;
				}
				if (mfac) set_money(u, u->number * (rng_int() % mfac));
        fset(u, UFL_ISNEW|UFL_MOVED);
			}
		case 2:	/* Terrainver�nderung */
			if (!fval(r->terrain, FORBIDDEN_REGION)) {
				if (!fval(r->terrain, SEA_REGION)) {
					direction_t dir;
					for (dir=0;dir!=MAXDIRECTIONS;++dir) {
            region * rn = rconnect(r, dir);
						if (rn && fval(rn->terrain, SEA_REGION)) break;
					}
					if (dir!=MAXDIRECTIONS) {
						ship * sh = r->ships;
						while (sh) {
							ship * nsh = sh->next;
							damage_ship(sh, 0.50);
							if (sh->damage >= sh->size * DAMAGE_SCALE) destroy_ship(sh);
							sh = nsh;
						}

						for (u = r->units; u;) {
							u2 = u->next;
							if (u->race != new_race[RC_SPELL] && u->ship == 0) {
								set_number(u, 0);
							}
							u = u2;
						}
						ADDMSG(&r->msgs, msg_message("tidalwave", "region", r));

						for (b = rbuildings(r); b;) {
							b2 = b->next;
							destroy_building(b);
							b = b2;
						}
						terraform(r, T_OCEAN);
					}
				} else {
					direction_t dir;
					for (dir=0;dir!=MAXDIRECTIONS;++dir) {
            region * rn = rconnect(r, dir);
            if (rn && fval(rn->terrain, SEA_REGION)) break;
					}
					if (dir!=MAXDIRECTIONS) {
						terraform_region(r, chaosterrain());
					}
				}
			}
		}
	}
}


static int
nb_armor(const unit *u, int index)
{
  const item * itm;
  int av = 0;
  int s = 0, a = 0;

  if (!(u->race->battle_flags & BF_EQUIPMENT)) return 0;

  /* Normale R�stung */

  for (itm=u->items;itm;itm=itm->next) {
    const armor_type * atype = itm->type->rtype->atype;
    if (atype!=NULL) {
      int * schutz = &a;
      if (atype->flags & ATF_SHIELD) schutz = &s;
      if (*schutz <= index) {
        *schutz += itm->number;
        if (*schutz > index) {
          av += atype->prot;
        }
      }
    }
  }
  return av;
}

static int
damage_unit(unit *u, const char *dam, boolean physical, boolean magic)
{
  int *hp = malloc(u->number * sizeof(int));
  int   h;
  int   i, dead = 0, hp_rem = 0, heiltrank;
  double magres = magic_resistance(u);

  assert(u->number);
  if (fval(u->race, RCF_ILLUSIONARY) || u->race == new_race[RC_SPELL]) {
    return 0;
  }

  h = u->hp/u->number;
  /* HP verteilen */
  for (i=0; i<u->number; i++) hp[i] = h;
  h = u->hp - (u->number * h);
  for (i=0; i<h; i++) hp[i]++;

  /* Schaden */
  for (i=0; i<u->number; i++) {
    int damage = dice_rand(dam);
    if (magic) damage = (int)(damage * (1.0 - magres));
    if (physical) damage -= nb_armor(u, i);
    hp[i] -= damage;
  }

  /* Auswirkungen */
  for (i=0; i<u->number; i++) {
    if (hp[i] <= 0){
      heiltrank = 0;

      /* Sieben Leben */
      if (old_race(u->race) == RC_CAT && (chance(1.0 / 7))) {
        hp[i] = u->hp/u->number;
        hp_rem += hp[i];
        continue;
      }

      /* Heiltrank */
      if (get_effect(u, oldpotiontype[P_HEAL]) > 0) {
        change_effect(u, oldpotiontype[P_HEAL], -1);
        heiltrank = 1;
      } else if (i_get(u->items, oldpotiontype[P_HEAL]->itype) > 0) {
        i_change(&u->items, oldpotiontype[P_HEAL]->itype, -1);
        change_effect(u, oldpotiontype[P_HEAL], 3);
        heiltrank = 1;
      }
      if (heiltrank && (chance(0.50))) {
        hp[i] = u->hp/u->number;
        hp_rem += hp[i];
        continue;
      }

      dead++;
    }	else {
      hp_rem += hp[i];
    }
  }

  scale_number(u, u->number - dead);
  u->hp = hp_rem;

  free(hp);

  return dead;
}

void
drown(region *r)
{
  if (fval(r->terrain, SEA_REGION)) {
    unit ** up = up=&r->units;
    while (*up) {
      unit *u = *up;
#ifdef KARMA_MODULE
      int amphibian_level = fspecial(u->faction, FS_AMPHIBIAN);
#else
      int amphibian_level = 0;
#endif
      if (u->ship || u->race == new_race[RC_SPELL] || u->number==0) {
        up=&u->next;
        continue;
      }

      if (amphibian_level) {
        int dead = damage_unit(u, "5d1", false, false);
        if (dead) {
          ADDMSG(&u->faction->msgs, msg_message("drown_amphibian_dead", 
            "amount unit region", dead, u, r));
        } else {
          ADDMSG(&u->faction->msgs, msg_message("drown_amphibian_nodead", 
            "unit region",u, r));
        }
      } else if (!(canswim(u) || canfly(u))) {
        scale_number(u, 0);
        ADDMSG(&u->faction->msgs, msg_message("drown", "unit region", u, r));
      }
      if (*up==u) up=&u->next;
    }
    remove_empty_units_in_region(r);
  }
}

region *
rrandneighbour(region *r)
{
	direction_t i;
	region *rc = NULL;
	int rr, c = 0;

	/* Nachsehen, wieviele Regionen in Frage kommen */

	for (i = 0; i != MAXDIRECTIONS; i++) {
		c++;
	}
	/* Zuf�llig eine ausw�hlen */

	rr = rng_int() % c;

	/* Durchz�hlen */

	c = -1;
	for (i = 0; i != MAXDIRECTIONS; i++) {
		rc = rconnect(r, i);
		c++;
		if (c == rr) break;
	}
	assert(i!=MAXDIRECTIONS);
	return rc;
}

void
volcano_outbreak(region *r)
{
  attrib *a;
  region *rn;
  unit *u, **up;
  faction *f;

  for (u=r->units; u; u=u->next) {
    f = u->faction;
    freset(f, FL_DH);
  }
  rn = rrandneighbour(r);

  /* Vulkan-Region verw�sten */

  rsettrees(r, 2, 0);
  rsettrees(r, 1, 0);
  rsettrees(r, 0, 0);

  a = a_find(r->attribs, &at_reduceproduction);
  if (!a) a = a_add(&r->attribs, a_new(&at_reduceproduction));

  /* Produktion vierteln ... */
  a->data.sa[0] = 25;
  /* F�r 6-17 Runden */
  a->data.sa[1] = (short)(a->data.sa[1] + 6 + rng_int()%12);

  /* Personen bekommen 4W10 Punkte Schaden. */

  for (up=&r->units; *up;) {
    unit * u = *up;
    if (u->number) {
      int dead = damage_unit(u, "4d10", true, false);
      if (dead) {
        ADDMSG(&u->faction->msgs, msg_message("volcano_dead", 
          "unit region dead", u, r, dead));
      }
      if (!fval(u->faction, FL_DH)) {
        fset(u->faction, FL_DH);
        if (rn) {
          ADDMSG(&u->faction->msgs, msg_message("volcanooutbreak", 
            "regionv regionn", r, rn));
        } else {
          ADDMSG(&u->faction->msgs, msg_message("volcanooutbreaknn", 
            "region", r));
        }
      }
    }
    if (u==*up) up=&u->next;
  }

  remove_empty_units_in_region(r);

  /* Zuf�llige Nachbarregion verw�sten */

  if (rn) {

    rsettrees(r, 2, 0);
    rsettrees(r, 1, 0);
    rsettrees(r, 0, 0);

    a = a_find(rn->attribs, &at_reduceproduction);
    if (!a) a = a_add(&rn->attribs, a_new(&at_reduceproduction));

    /* Produktion vierteln ... */
    a->data.sa[0] = 25;
    /* F�r 6-17 Runden */
    a->data.sa[1] = (short)(a->data.sa[1] + 6 + rng_int()%12);

    /* Personen bekommen 3W10 Punkte Schaden. */
    for (up=&rn->units; *up;) {
      unit * u = *up;
      if (u->number) {
        int dead = damage_unit(u, "3d10", true, false);
        if (dead) {
          ADDMSG(&u->faction->msgs, msg_message("volcano_dead", 
            "unit region dead", u, r, dead));
        }
      }
      if (u==*up) up=&u->next;
    }
    remove_empty_units_in_region(rn);
  }
}

static void
melt_iceberg(region *r)
{
	attrib *a;
	unit *u;
	building *b, *b2;

	for (u=r->units; u; u=u->next) freset(u->faction, FL_DH);
	for (u=r->units; u; u=u->next) if (!fval(u->faction, FL_DH)) {
		fset(u->faction, FL_DH);
		ADDMSG(&u->faction->msgs, msg_message("iceberg_melt", "region", r));
	}

	/* driftrichtung l�schen */
	a = a_find(r->attribs, &at_iceberg);
	if (a) a_remove(&r->attribs, a);

	/* Geb�ude l�schen */
	for (b = rbuildings(r); b; b = b2) {
		b2 = b->next;
		destroy_building(b);
	}

	/* in Ozean wandeln */
	terraform(r, T_OCEAN);

	/* Einheiten, die nicht schwimmen k�nnen oder in Schiffen sind,
	 * ertrinken */
	drown(r);
}

static void
move_iceberg(region *r)
{
	attrib *a;
	direction_t dir;
	region *rc;

	a = a_find(r->attribs, &at_iceberg);
	if (!a) {
		dir = (direction_t)(rng_int()%MAXDIRECTIONS);
		a = a_add(&r->attribs, make_iceberg(dir));
	} else {
		if (rng_int()%100 < 20) {
			dir = (direction_t)(rng_int()%MAXDIRECTIONS);
			a->data.i = dir;
		} else {
			dir = (direction_t)a->data.i;
		}
	}

	rc = rconnect(r, dir);

	if (rc && !fval(rc->terrain, ARCTIC_REGION)) {
		if (fval(rc->terrain, SEA_REGION)) {	/* Eisberg treibt */
			ship *sh, *shn;
			unit *u;
			short x, y;


			for (u=r->units; u; u=u->next) freset(u->faction, FL_DH);
			for (u=r->units; u; u=u->next) if (!fval(u->faction, FL_DH)) {
				fset(u->faction, FL_DH);
				ADDMSG(&u->faction->msgs, msg_message("iceberg_drift", 
          "region dir", r, dir));
			}

			x = r->x;
			y = r->y;

			runhash(r);
			runhash(rc);
			r->x = rc->x;
			r->y = rc->y;
			rc->x = x;
			rc->y = y;
			rhash(rc);
			rhash(r);

			/* rc ist der Ozean (Ex-Eisberg), r der Eisberg (Ex-Ozean) */

			/* Schiffe aus dem Zielozean werden in den Eisberg transferiert
			 * und nehmen Schaden. */

			for (sh = r->ships; sh; sh=sh->next) freset(sh, FL_DH);

			for (sh = r->ships; sh; sh = sh->next) {
				/* Meldung an Kapit�n */
				damage_ship(sh, 0.10);
				fset(sh, FL_DH);
			}

			/* Personen, Schiffe und Geb�ude verschieben */
			while (rc->buildings) {
				rc->buildings->region = r;
				translist(&rc->buildings, &r->buildings, rc->buildings);
			}
			while (rc->ships) {
				fset(rc->ships, FL_DH);
				damage_ship(rc->ships, 0.10);
				move_ship(rc->ships, rc, r, NULL);
			}
			while (rc->units) {
				building * b = rc->units->building;
				u = rc->units;
				move_unit(rc->units, r, NULL);
				u->building = b; /* move_unit macht ein leave() */
			}

			/* Besch�digte Schiffe k�nnen sinken */

			for (sh = r->ships; sh;) {
				shn = sh->next;
				if (fval(sh, FL_DH)) {
					u = captain(sh, r);
					if (sh->damage>=sh->size * DAMAGE_SCALE) {
            if (u!=NULL) {
              ADDMSG(&u->faction->msgs, msg_message("overrun_by_iceberg_des", 
                "ship", sh));
            }
						destroy_ship(sh);
          } else if (u!=NULL) {
            ADDMSG(&u->faction->msgs, msg_message("overrun_by_iceberg", 
              "ship", sh));
					}
				}
				sh = shn;
			}

		} else if (rng_int()%100 < 20) {	/* Eisberg bleibt als Gletscher liegen */
			unit *u;

			rsetterrain(r, T_GLACIER);
			a_remove(&r->attribs, a);

			for (u=r->units; u; u=u->next) freset(u->faction, FL_DH);
			for (u=r->units; u; u=u->next) if (!fval(u->faction, FL_DH)) {
				fset(u->faction, FL_DH);
        ADDMSG(&u->faction->msgs, msg_message("iceberg_land", "region", r));
			}
		}
	}
}

static void
move_icebergs(void)
{
	region *r;

  for (r=regions; r; r=r->next) {
    if (rterrain(r) == T_ICEBERG && !fval(r, RF_DH)) {
      int select = rng_int() % 10;
      if (select < 4) {
        /* 4% chance */
  			fset(r, RF_DH);
	  		melt_iceberg(r);
      } else if (select<64) {
        /* 60% chance */
        fset(r, RF_DH);
        move_iceberg(r);
      }
		}
	}
}

void
create_icebergs(void)
{
	region *r;

  for (r=regions; r; r=r->next) {
    if (rterrain(r) == T_ICEBERG_SLEEP && chance(0.05)) {
      boolean has_ocean_neighbour = false;
      direction_t dir;
      region *rc;
      unit *u;

      freset(r, RF_DH);
      for (dir=0; dir < MAXDIRECTIONS; dir++) {
        rc = rconnect(r, dir);
        if (rc && fval(rc->terrain, SEA_REGION)) {
          has_ocean_neighbour = true;
          break;
        }
      }
      if (!has_ocean_neighbour) continue;

      rsetterrain(r, T_ICEBERG);

      fset(r, RF_DH);
      move_iceberg(r);

      for (u=r->units; u; u=u->next) {
        freset(u->faction, FL_DH);
      }
      for (u=r->units; u; u=u->next) {
        if (!fval(u->faction, FL_DH)) {
          fset(u->faction, FL_DH);
          ADDMSG(&u->faction->msgs, msg_message("iceberg_create", "region", r));
        }
      }
    }
	}
}

static void
godcurse(void)
{
  region *r;

  for (r=regions; r; r=r->next) {
    if (is_cursed(r->attribs, C_CURSED_BY_THE_GODS, 0)) {
      unit * u;
      for(u=r->units; u; u=u->next) {
        skill * sv = u->skills;
        while (sv!=u->skills+u->skill_size) {
          int weeks = 1+rng_int()%3;
          reduce_skill(u, sv, weeks);
          ++sv;
        }
      }
      if (fval(r->terrain, SEA_REGION)) {
        ship *sh;
        for (sh = r->ships; sh;) {
          ship *shn = sh->next;
          damage_ship(sh, 0.10);
          if (sh->damage>=sh->size * DAMAGE_SCALE) {
            unit * u = shipowner(sh);
            if (u) ADDMSG(&u->faction->msgs,
              msg_message("godcurse_destroy_ship", "ship", sh));
            destroy_ship(sh);
          }
          sh = shn;
        }
      }
    }
  }

}

static unit *
split_unit(region * r, unit *u)
{
  unit *u2 = createunit(r, u->faction, 0, u->race);
  int newsize = u->number/2;

  set_string(&u2->name, u->name);
  set_string(&u2->display, u->display);
  transfermen(u, u2, newsize);
  return u2;
}

static void
check_split(void)
{
  region *r;
  unit *u;

  for(r=regions;r;r=r->next) {
    for(u=r->units;u;u=u->next) {
      if(u->faction->no == MONSTER_FACTION) {
        if(u->number > u->race->splitsize) {
          unit * u2 = split_unit(r, u);
          fset(u2, UFL_ISNEW|UFL_MOVED);
        }
      }
    }
  }
}

/** handles the "orcish" curse that makes units grow like old orks
 * This would probably be better handled in an age-function for the curse,
 * but it's now being called by randomevents()
 */
static void
orc_growth(void)
{
  region * r;
  for (r = regions; r; r = r->next) {
    unit *u;
    for (u = r->units; u; u = u->next) {
      static boolean init = false;
      static const curse_type *ct_orcish = 0;
      curse *c = 0;
      if (!init) {
        init = true;
        ct_orcish = ct_find("orcish");
      }
      if (ct_orcish) c = get_curse(u->attribs, ct_orcish);

      if (c && !has_skill(u, SK_MAGIC) && !has_skill(u, SK_ALCHEMY) && !fval(u, UFL_HERO)) {
        int n;
        int increase = 0;
        int num  = get_cursedmen(u, c);
        int prob = curse_geteffect(c);

        for (n = (num - get_item(u, I_CHASTITY_BELT)); n > 0; n--) {
          if (rng_int() % 100 < prob) {
            ++increase;
          }
        }
        if (increase) {
          unit * u2 = create_unit(r, u->faction, increase, u->race, 0, NULL, u);
          transfermen(u2, u, u2->number);

          ADDMSG(&u->faction->msgs, msg_message("orcgrowth",
            "unit amount race", u, increase, u->race));
        }
      }
    }
  }
}

/** Talente von D�monen verschieben sich.
 */
static void
demon_skillchanges(void)
{
  region * r;

  for (r = regions; r; r = r->next) {
    unit * u;
    for (u = r->units; u; u = u->next) {
      if (u->race == new_race[RC_DAEMON]) {
        skill * sv = u->skills;
        while (sv!=u->skills+u->skill_size) {
          if (sv->level>0 && rng_int() % 100 < 25) {
            int weeks = 1+rng_int()%3;
            if (rng_int() % 100 < 40) {
              reduce_skill(u, sv, weeks);
            } else {
              while (weeks--) learn_skill(u, sv->id, 1.0);
            }
            if (sv->old>sv->level) {
              log_printf("%s dropped from %u to %u:%u in %s\n",
                unitname(u), sv->old, sv->level,
                sv->weeks, skillname(sv->id, NULL));
            }
          }
          ++sv;
        }
      }
    }
  }
}

/** Eisberge entstehen und bewegen sich.
 * Einheiten die im Wasser landen, ertrinken.
 */
static void 
icebergs(void)
{
  region * r;
  create_icebergs();
  move_icebergs();
  for (r=regions; r; r=r->next) {
    drown(r);
  }
}

void
randomevents(void)
{
#ifdef KARMA_MODULE
	faction *f;
#endif /* KARMA_MODULE */
	region *r;
	building *b, *b2;
	unit *u;

  icebergs();
  godcurse();
  orc_growth();
  demon_skillchanges();

  /* Orkifizierte Regionen mutieren und mutieren zur�ck */

  for (r = regions; r; r = r->next) {
    if (fval(r, RF_ORCIFIED)) {
      direction_t dir;
      double probability = 0.0;
      for (dir = 0; dir < MAXDIRECTIONS; dir++) {
        region *rc = rconnect(r, dir);
        if (rc && rpeasants(rc) > 0 && !fval(rc, RF_ORCIFIED)) probability += 0.02;
      }
      if (chance(probability)) {
        ADDMSG(&r->msgs, msg_message("deorcified", "region", r));
        freset(r, RF_ORCIFIED);
      }
    } else {
      attrib *a = a_find(r->attribs, &at_orcification);
      if (a!=NULL) {
        double probability = 0.0;
        if (rpeasants(r) <= 0) continue;
        probability = a->data.i/(double)rpeasants(r);
        if (chance(probability)) {
          fset(r, RF_ORCIFIED);
          a_remove(&r->attribs, a);
          ADDMSG(&r->msgs, msg_message("orcified", "region", r));
        } else {
          a->data.i -= max(10,a->data.i/10);
          if (a->data.i <= 0) a_remove(&r->attribs, a);
        }
      }
    }
  }

  /* Vulkane qualmen, brechen aus ... */
  for (r = regions; r; r = r->next) {
    if (rterrain(r)==T_VOLCANO_SMOKING && a_find(r->attribs, &at_reduceproduction)) {
      ADDMSG(&r->msgs, msg_message("volcanostopsmoke", "region", r));
      rsetterrain(r, T_VOLCANO);
    } else switch(rterrain(r)) {
    case T_VOLCANO:
      if (rng_int()%100 < 4) {
        ADDMSG(&r->msgs, msg_message("volcanostartsmoke", "region", r));
        rsetterrain(r, T_VOLCANO_SMOKING);
      }
      break;
    case T_VOLCANO_SMOKING:
      if (rng_int()%100 < 12) {
        ADDMSG(&r->msgs, msg_message("volcanostopsmoke", "region", r));
        rsetterrain(r, T_VOLCANO);
      } else if (rng_int()%100 < 8) {
        volcano_outbreak(r);
      }
      break;
    }
  }

  /* Monumente zerfallen, Schiffe verfaulen */

  for (r = regions; r; r = r->next) {
    for (b = rbuildings(r); b; b = b2) {
      b2 = b->next;
      if (fval(b->type, BTF_DECAY) && !buildingowner(r, b)) {
        b->size -= max(1, (b->size * 20) / 100);
        if (b->size == 0) {
          destroy_building(b);
        }
      }
    }
  }

  /* monster-einheiten desertieren */
  for (r = regions; r; r=r->next) {
    for (u=r->units; u; u=u->next) {
      if (u->faction && u->faction->no != MONSTER_FACTION
        && (u->race->flags & RCF_DESERT)) {
          if (fval(u, UFL_ISNEW)) continue;
          if (rng_int()%100 < 5) {
            ADDMSG(&u->faction->msgs, msg_message("desertion",
              "unit region", u, r));
            u_setfaction(u, findfaction(MONSTER_FACTION));
          }
        }
    }
  }

#ifdef KARMA_MODULE
  /* lycanthropen werden werw�lfe */
  for (f = factions; f; f=f->next) {
    if (f->alive) {
      int level = fspecial(f, FS_LYCANTROPE);
      if (level > 0) {
        for(u = f->units; u; u=u->nextF) {
          if (rng_int()%100 < 2*level) {
            ADDMSG(&u->faction->msgs,
                   msg_message("becomewere", "unit region", u, u->region));
            fset(u, UFL_WERE);
          }
        }
      }
    }
  }
#endif

  /* Chaos */
  for (r = regions; r; r = r->next) {
    int i;
    if (fval(r, RF_CHAOTIC)) {
      chaos(r);
    }
    i = chaoscount(r);
    if (!i) continue;
    chaoscounts(r, -(int) (i * ((double) (rng_int() % 10)) / 100.0));
  }
  
#ifdef HERBS_ROT
  /* Kr�uter verrotten */
  for (r = regions; r; r = r->next) {
    for (u = r->units; u; u=u->next) {
      item **itmp = &u->items, *hbag = *i_find(&u->items, olditemtype[I_SACK_OF_CONSERVATION]);
      int rot_chance = HERBROTCHANCE;
      
      if (hbag) rot_chance = (HERBROTCHANCE*2)/5;
      
      while (*itmp) {
        item * itm = *itmp;
        int n = itm->number;
        double k = n*rot_chance/100.0;
        if (fval(itm->type, ITF_HERB)) {
          double nv = normalvariate(k, k/4);
          int inv = (int)nv;
          int delta = min(n, inv);
          i_change(itmp, itm->type, -delta);
        }
        if (itm==*itmp) itmp=&itm->next;
      }
    }
  }
#endif
  
  dissolve_units();
  check_split();
#ifdef KARMA_MODULE
  check_luck();
#endif /* KARMA_MODULE */
}
