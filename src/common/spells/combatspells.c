/* vi: set ts=2:
 +-------------------+  Christian Schlittchen <corwin@amber.kn-bremen.de>
 |                   |  Enno Rehling <enno@eressea-pbem.de>
 | Eressea PBEM host |  Katja Zedel <katze@felidae.kn-bremen.de>
 | (c) 1998 - 2003   |  Henning Peters <faroul@beyond.kn-bremen.de>
 |                   |  Ingo Wilken <Ingo.Wilken@informatik.uni-oldenburg.de>
 +-------------------+  Stefan Reich <reich@halbling.de>

 This program may not be used, modified or distributed
 without prior permission by the authors of Eressea.
*/
#include <config.h>
#include "eressea.h"
#include "combatspells.h"

/* kernel includes */
#include <kernel/battle.h>
#include <kernel/build.h>
#include <kernel/building.h>
#include <kernel/faction.h>
#include <kernel/item.h>
#include <kernel/magic.h>
#include <kernel/message.h>
#include <kernel/order.h>
#include <kernel/region.h>
#include <kernel/unit.h>
#include <kernel/movement.h>
#include <kernel/spell.h>
#include <kernel/spellid.h>
#include <kernel/race.h>
#include <kernel/skill.h>

/* util includes */
#include <util/base36.h>
#include <util/rand.h>
#include <util/rng.h>

/* libc includes */
#include <assert.h>

#define EFFECT_HEALING_SPELL     5

/* ------------------------------------------------------------------ */
/* Kampfzauberfunktionen */

/* COMBAT */

static const char *
spell_damage(int sp)
{
  switch (sp) {
    case 0:
      /* meist t�dlich 20-65 HP */
      return "5d10+15";
    case 1:
      /* sehr variabel 4-48 HP */
      return "4d12";
    case 2:
      /* leicht verwundet 4-18 HP */
      return "2d8+2";
    case 3:
      /* fast immer t�dlich 30-50 HP */
      return "5d5+25";
    case 4:
      /* verwundet 11-26 HP */
      return "3d6+8";
    case 5:
      /* leichter Schaden */
      return "2d4";
    default:
      /* schwer verwundet 14-34 HP */
      return "4d6+10";
  }
}

static double
get_force(double power, int formel)
{
  switch (formel) {
    case 0:
      /* (4,8,12,16,20,24,28,32,36,40,44,..)*/
      return (power * 4);
    case 1:
      /* (15,30,45,60,75,90,105,120,135,150,165,..) */
      return (power*15);
    case 2:
      /* (40,80,120,160,200,240,280,320,360,400,440,..)*/
      return (power*40);
    case 3:
      /* (2,8,18,32,50,72,98,128,162,200,242,..)*/
      return (power*power*2);
    case 4:
      /* (4,16,36,64,100,144,196,256,324,400,484,..)*/
      return (power*power*4);
    case 5:
      /* (10,40,90,160,250,360,490,640,810,1000,1210,1440,..)*/
      return (power*power*10);
    case 6:
      /* (6,24,54,96,150,216,294,384,486,600,726,864)*/
      return (power*power*6);
    default:
      return power;
  }
}

/* Generischer Kampfzauber */
int
sp_kampfzauber(fighter * fi, int level, double power, spell * sp)
{
  battle *b = fi->side->battle;
  troop at, dt;
  message * m;
  /* Immer aus der ersten Reihe nehmen */
  int force, enemies;
  int killed = 0;
  const char *damage;

  if (power <= 0) return 0;
  at.fighter = fi;
  at.index   = 0;

  switch(sp->id) {
    /* lovar halbiert im Schnitt! */
    case SPL_FIREBALL:
      damage = spell_damage(0);
      force = lovar(get_force(power,0));
      break;
    case SPL_HAGEL:
      damage = spell_damage(2);
      force = lovar(get_force(power,4));
      break;
    case SPL_METEORRAIN:
      damage = spell_damage(1);
      force = lovar(get_force(power,1));
      break;
    default:
      damage = spell_damage(10);
      force = lovar(get_force(power,10));
  }

  sprintf(buf, "%s zaubert %s", unitname(fi->unit),
    spell_name(sp, default_locale));

  enemies = count_enemies(b, fi, FIGHT_ROW, BEHIND_ROW-1, SELECT_ADVANCE);
  if (enemies==0) {
    m = msg_message("battle::out_of_range", "mage spell", fi->unit, sp);
    message_all(b, m);
    msg_release(m);
    return 0;
  }

  while (force>0 && killed < enemies) {
    dt = select_enemy(fi, FIGHT_ROW, BEHIND_ROW-1, SELECT_ADVANCE);
    assert(dt.fighter);
    --force;
    killed += terminate(dt, at, AT_COMBATSPELL, damage, false);
  }

  m = msg_message("battle::combatspell", "mage spell dead",
    fi->unit, sp, killed);
  message_all(b, m);
  msg_release(m);

  return level;
}

/* Versteinern */
int
sp_versteinern(fighter * fi, int level, double power, spell * sp)
{
  battle *b = fi->side->battle;
  unit *mage = fi->unit;
  /* Wirkt auf erste und zweite Reihe */
  int force, enemies;
  int stoned = 0;

  sprintf(buf, "%s zaubert %s", unitname(fi->unit),
    spell_name(sp, default_locale));

  force = lovar(get_force(power, 0));

  enemies = count_enemies(b, fi, FIGHT_ROW, BEHIND_ROW, SELECT_ADVANCE);
  if (!enemies) {
    scat(", aber niemand war in Reichweite.");
    battlerecord(b, buf);
    return 0;
  }
  scat(":");
  battlerecord(b, buf);

  while (force && stoned < enemies) {
    troop dt = select_enemy(fi, FIGHT_ROW, BEHIND_ROW, SELECT_ADVANCE);
    fighter * df = dt.fighter;
    unit * du = df->unit;
    if (is_magic_resistant(mage, du, 0) == false) {
      /* person ans ende hinter die lebenden schieben */
      struct person p = dt.fighter->person[dt.index];
      ++dt.fighter->removed;
      ++dt.fighter->side->removed;
      ++stoned;
      dt.fighter->person[dt.index] = dt.fighter->person[df->alive-df->removed];
      dt.fighter->person[(df->alive - df->removed)] = p;
    }
    --force;
  }

  sprintf(buf, "%d Personen %s versteinert.",
      stoned, stoned == 1 ? "wurde" : "wurden");
  battlerecord(b, buf);
  return level;
}

/* Benommenheit: eine Runde kein Angriff */
int
sp_stun(fighter * fi, int level, double power, spell * sp)
{
  battle *b = fi->side->battle;
  unit *mage = fi->unit;
  troop at;
  /* Aus beiden Reihen nehmen */
  int force=0, enemies;
  int stunned;
  at.fighter = fi;
  at.index   = 0;

  if (power <= 0) return 0;

  sprintf(buf, "%s zaubert %s", unitname(fi->unit),
    spell_name(sp, default_locale));

  switch(sp->id) {
    case SPL_SHOCKWAVE:
      force = lovar(get_force(power,1));
      break;
    default:
      assert(0);
  }

  enemies = count_enemies(b, fi, FIGHT_ROW, BEHIND_ROW, SELECT_ADVANCE);
  if (!enemies) {
    scat(", aber niemand war in Reichweite.");
    battlerecord(b, buf);
    return 0;
  }
  scat(":");
  battlerecord(b, buf);

  stunned = 0;
  while (force && stunned < enemies) {
    troop dt = select_enemy(fi, FIGHT_ROW, BEHIND_ROW, SELECT_ADVANCE);
    fighter * df = dt.fighter;
    unit * du = df->unit;

    --force;
    if (is_magic_resistant(mage, du, 0) == false) {
        df->person[dt.index].flags |= FL_STUNNED;
        ++stunned;
    }
  }

  sprintf(buf, "%d Krieger %s f�r einen Moment benommen.",
      stunned, stunned == 1 ? "ist" : "sind");

  scat(".");
  battlerecord(b, buf);
  return level;
}

/* ------------------------------------------------------------- */
/* F�r Spr�che 'get_scrambled_list_of_enemys_in_row', so da� man diese
 * Liste nur noch einmal durchlaufen muss, um Fl�chenzauberwirkungen
 * abzuarbeiten */

/* Rosthauch */
int
sp_combatrosthauch(fighter * fi, int level, double power, spell * sp)
{
  battle *b = fi->side->battle;
  cvector *fgs;
  void **fig;
  int force;
  int k = 0;
  /* Immer aus der ersten Reihe nehmen */
  static const char * msgt[] = {
    "ruft ein f�rchterliches Unwetter �ber seine Feinde, doch es gab niemanden mehr, den dies treffen konnte.",
    "ruft ein f�rchterliches Unwetter �ber seine Feinde, doch der magische Regen zeigt keinen Effekt.",
    "ruft ein f�rchterliches Unwetter �ber seine Feinde, Der magischen Regen l�sst alles Eisen rosten."
  };
  unused(sp);

  force = lovar(power * 15);

  if (!count_enemies(b, fi, FIGHT_ROW, BEHIND_ROW-1, SELECT_ADVANCE|SELECT_FIND)) {
    battlemsg(b, fi->unit, msgt[0]);
    return 0;
  }

  fgs = fighters(b, fi->side, FIGHT_ROW, BEHIND_ROW-1, FS_ENEMY);
  v_scramble(fgs->begin, fgs->end);

  for (fig = fgs->begin; fig != fgs->end; ++fig) {
    fighter *df = *fig;

                if (df->alive==0) continue;
                if (force<=0) break;

    /* da n min(force, x), sollte force maximal auf 0 sinken */
    assert(force >= 0);

    if (df->weapons) {
      int w;
      for (w=0;df->weapons[w].type!=NULL;++w) {
        weapon * wp = df->weapons;
        int n = min(force, wp->used);
        if (n) {
          requirement * mat = wp->type->itype->construction->materials;
          boolean iron = false;
          while (mat && mat->number>0) {
            if (mat->rtype==oldresourcetype[R_IRON]) {
              iron = true;
              break;
            }
            mat++;
          }
          if (iron) {
            int p;
            force -=n;
            wp->used -= n;
            k +=n;
            i_change(&df->unit->items, wp->type->itype, -n);
            for (p=0;n && p!=df->unit->number;++p) {
              if (df->person[p].missile==wp) {
                df->person[p].missile = NULL;
                --n;
              }
            }
            for (p=0;n && p!=df->unit->number;++p) {
              if (df->person[p].melee==wp) {
                df->person[p].melee = NULL;
                --n;
              }
            }
          }
        }
      }
    }
  }
  cv_kill(fgs);
  free(fgs);

  if (k == 0) {
    /* keine Waffen mehr da, die zerst�rt werden k�nnten */
    battlemsg(b, fi->unit, msgt[1]);
    fi->magic = 0; /* k�mpft nichtmagisch weiter */
    level = 0;
  } else {
    battlemsg(b, fi->unit, msgt[2]);
  }
  return level;
}

int
sp_sleep(fighter * fi, int level, double power, spell * sp)
{
  battle *b = fi->side->battle;
  unit *mage = fi->unit;
  unit *du;
  troop dt;
  int force, enemies;
  int k = 0;
  /* Immer aus der ersten Reihe nehmen */

  sprintf(buf, "%s zaubert %s", unitname(mage),
    spell_name(sp, default_locale));
  force = lovar(power * 25);
  enemies = count_enemies(b, fi, FIGHT_ROW, BEHIND_ROW, SELECT_ADVANCE);

  if (!enemies) {
    scat(", aber niemand war in Reichweite.");
    battlerecord(b, buf);
    return 0;
  }
  scat(":");
  battlerecord(b, buf);

  while (force && enemies) {
    dt = select_enemy(fi, FIGHT_ROW, BEHIND_ROW, SELECT_ADVANCE);
    assert(dt.fighter);
    du = dt.fighter->unit;
    if (is_magic_resistant(mage, du, 0) == false) {
      dt.fighter->person[dt.index].flags |= FL_SLEEPING;
      ++k;
      --enemies;
    }
    --force;
  }

  sprintf(buf, "%d Krieger %s in Schlaf versetzt.",
      k, k == 1 ? "wurde" : "wurden");
  battlerecord(b, buf);
  return level;
}


static troop
select_ally(fighter * af, int minrow, int maxrow)
{
  side *as = af->side;
  battle *b = as->battle;
  side * ds;
  int allies = count_allies(as, minrow, maxrow, SELECT_ADVANCE);

  if (!allies) {
    return no_troop;
  }
  allies = rng_int() % allies;

  for (ds=b->sides; ds; ds=ds->next) {
    if (helping(as, ds)) {
      fighter * df;
      for (df=ds->fighters; df; df=df->next) {
        int dr = get_unitrow(df, NULL);
        if (dr >= minrow && dr <= maxrow) {
          if (df->alive - df->removed > allies) {
            troop dt;
            assert(allies>=0);
            dt.index = allies;
            dt.fighter = df;
            return dt;
          }
          allies -= df->alive;
        }
      }
    }
  }
  assert(!"we should never have gotten here");
  return no_troop;
}

int
sp_speed(fighter * fi, int level, double power, spell * sp)
{
  battle *b = fi->side->battle;
  int force;
  int allies;
  int targets = 0;

  sprintf(buf, "%s zaubert %s", unitname(fi->unit),
    spell_name(sp, default_locale));
  scat(":");
  battlerecord(b, buf);

  force = lovar(power * power * 5);

  allies = count_allies(fi->side, FIGHT_ROW, BEHIND_ROW, SELECT_ADVANCE);
  /* maximal 2*allies Versuche ein Opfer zu finden, ansonsten best�nde
   * die Gefahr eine Endlosschleife*/
  allies *= 2;

  while (force && allies) {
    troop dt = select_ally(fi, FIGHT_ROW, BEHIND_ROW);
    fighter *df = dt.fighter;
    --allies;

    if (df) {
      if (df->person[dt.index].speed == 1) {
        df->person[dt.index].speed++;
        targets++;
        --force;
      }
    }
  }

  sprintf(buf, "%d Krieger %s magisch beschleunigt.",
      targets, targets == 1 ? "wurde" : "wurden");
  battlerecord(b, buf);
  return 1;
}

static skill_t
random_skill(unit *u)
{
  int n = 0;
  skill * sv;

  for (sv = u->skills; sv != u->skills + u->skill_size; ++sv) {
    if (sv->level>0) ++n;
  }

  if(n == 0)
    return NOSKILL;

  n = rng_int()%n;

  for (sv = u->skills; sv != u->skills + u->skill_size; ++sv) {
    if (sv->level>0) {
      if (n-- == 0) return sv->id;
    }
  }

  assert(0==1); /* Hier sollte er niemals ankommen. */
  return NOSKILL;
}

int
sp_mindblast(fighter * fi, int level, double power, spell * sp)
{
  battle *b = fi->side->battle;
  unit *mage = fi->unit;
  troop dt;
  unit *du;
  skill_t sk;
  int killed = 0;
  int force, enemies;
  int k = 0;

  sprintf(buf, "%s zaubert %s", unitname(mage),
    spell_name(sp, default_locale));
  force = lovar(power * 25);

  enemies = count_enemies(b, fi, FIGHT_ROW, BEHIND_ROW, SELECT_ADVANCE);
  if (!enemies) {
    scat(", aber niemand war in Reichweite.");
    battlerecord(b, buf);
    return 0;
  }
  scat(":");
  battlerecord(b, buf);

  while (force && enemies) {
    dt = select_enemy(fi, FIGHT_ROW, BEHIND_ROW, SELECT_ADVANCE);
    assert(dt.fighter);
    du = dt.fighter->unit;
    if (humanoidrace(du->race) && !is_magic_resistant(mage, du, 0)) {
      sk = random_skill(du);
      if (sk != NOSKILL) {
        skill * sv = get_skill(du, sk);
        int n = 1 + rng_int() % 3;
        /* Skill abziehen */
        reduce_skill(du, sv, n);
      } else {
        /* Keine Skills mehr, Einheit t�ten */
        rmtroop(dt);
        ++killed;
      }
      --enemies;
      ++k;
    }
    --force;
  }

  sprintf(buf, "%d Krieger %s Erinnerungen", k, k == 1 ? "verliert" : "verlieren");

  if (killed > 0) {
    scat(", ");
    icat(killed);
    scat(" Krieger ");
    if (killed == 1) {
      scat("wurde");
    } else {
      scat("wurden");
    }
    scat(" get�tet");
  }
  scat(".");

  battlerecord(b, buf);
  return level;
}

int
sp_dragonodem(fighter * fi, int level, double power, spell * sp)
{
  battle *b = fi->side->battle;
  troop dt;
  troop at;
  int force, enemies;
  int killed = 0;
  const char *damage;

  sprintf(buf, "%s zaubert %s", unitname(fi->unit),
    spell_name(sp, default_locale));
  /* 11-26 HP */
  damage = spell_damage(4);
  /* Jungdrache 3->54, Drache 6->216, Wyrm 12->864 Treffer */
  force = lovar(get_force(level,6));

  enemies = count_enemies(b, fi, FIGHT_ROW, BEHIND_ROW-1, SELECT_ADVANCE);

  if (!enemies) {
    scat(", aber niemand war in Reichweite.");
    battlerecord(b, buf);
    return 0;
  }
  scat(":");
  battlerecord(b, buf);

  at.fighter = fi;
  at.index = 0;

  while (force && killed < enemies) {
    dt = select_enemy(fi, FIGHT_ROW, BEHIND_ROW-1, SELECT_ADVANCE);
    assert(dt.fighter);
    --force;
    killed += terminate(dt, at, AT_COMBATSPELL, damage, false);
  }

  sprintf(buf, "%d Personen %s get�tet",
      killed, killed == 1 ? "wurde" : "wurden");

  scat(".");
  battlerecord(b, buf);
  return level;
}

/* Feuersturm: Betrifft sehr viele Gegner (in der Regel alle),
 * macht nur vergleichsweise geringen Schaden */
int
sp_immolation(fighter * fi, int level, double power, spell * sp)
{
  battle *b = fi->side->battle;
  troop at;
  int force;
  int killed = 0;
  const char *damage;
  cvector *fgs;
  void **fig;

  sprintf(buf, "%s zaubert %s", unitname(fi->unit),
    spell_name(sp, default_locale));
  /* 2d4 HP */
  damage = spell_damage(5);
  /* Betrifft alle Gegner */
  force = 99999;

  if (!count_enemies(b, fi, FIGHT_ROW, AVOID_ROW, SELECT_ADVANCE|SELECT_FIND)) {
    scat(", aber niemand war in Reichweite.");
    battlerecord(b, buf);
    return 0;
  }
  scat(":");
  battlerecord(b, buf);

  at.fighter = fi;
  at.index = 0;

  fgs = fighters(b, fi->side, FIGHT_ROW, AVOID_ROW, FS_ENEMY);
  for (fig = fgs->begin; fig != fgs->end; ++fig) {
    fighter *df = *fig;
    int n = df->alive-df->removed;
    troop dt;

    dt.fighter = df;
    while (n!=0) {
      dt.index = --n;
      killed += terminate(dt, at, AT_COMBATSPELL, damage, false);
      if (--force==0) break;
    }
    if (force==0) break;
  }
  cv_kill(fgs);
  free(fgs);

  sprintf(buf, "%d Personen %s get�tet",
      killed, killed == 1 ? "wurde" : "wurden");

  scat(".");
  battlerecord(b, buf);
  return level;
}

int
sp_drainodem(fighter * fi, int level, double power, spell * sp)
{
  battle *b = fi->side->battle;
  troop dt;
  troop at;
  int force, enemies;
  int drained = 0;
  int killed = 0;
  const char *damage;

  sprintf(buf, "%s zaubert %s", unitname(fi->unit),
    spell_name(sp, default_locale));
  /* 11-26 HP */
  damage = spell_damage(4);
  /* Jungdrache 3->54, Drache 6->216, Wyrm 12->864 Treffer */
  force = lovar(get_force(level,6));

  enemies = count_enemies(b, fi, FIGHT_ROW, BEHIND_ROW-1, SELECT_ADVANCE);

  if (!enemies) {
    scat(", aber niemand war in Reichweite.");
    battlerecord(b, buf);
    return 0;
  }
  scat(":");
  battlerecord(b, buf);

  at.fighter = fi;
  at.index = 0;

  while (force && drained < enemies) {
    dt = select_enemy(fi, FIGHT_ROW, BEHIND_ROW-1, SELECT_ADVANCE);
    assert(dt.fighter);
    if (hits(at, dt, NULL)) {
      drain_exp(dt.fighter->unit, 90);
      ++drained;
      killed += terminate(dt, at, AT_COMBATSPELL, damage, false);
    }
    --force;
  }

  sprintf(buf, "%d Person%s wurde ihre Lebenskraft entzogen",
    drained, drained == 1 ? " wurde" : "en wurden");

  scat(".");
  battlerecord(b, buf);
  return level;
}

/* ------------------------------------------------------------- */
/* PRECOMBAT */

int
sp_shadowcall(fighter * fi, int level, double power, spell * sp)
{
  battle *b = fi->side->battle;
  region *r = b->region;
  unit *mage = fi->unit;
  attrib *a;
  int force = (int)(get_force(power, 3)/2);
  unit *u;
  const char * races[3] = { "shadowbat", "nightmare", "vampunicorn" };
  const race *rc = rc_find(races[rng_int()%3]);

  unused(sp);

  u = create_unit(r, mage->faction, force, rc, 0, NULL, mage);
  u->status = ST_FIGHT;

  set_string(&u->name, racename(mage->faction->locale, u, u->race));
  set_level(u, SK_WEAPONLESS, (int)(power/2));
  set_level(u, SK_AUSDAUER, (int)(power/2));
  u->hp = u->number * unit_max_hp(u);

  a = a_new(&at_unitdissolve);
  a->data.ca[0] = 0;
  a->data.ca[1] = 100;
  a_add(&u->attribs, a);

  make_fighter(b, u, fi->side, is_attacker(fi));
  sprintf(buf, "%s ruft %d %s zu Hilfe", unitname(mage), force,
    racename(default_locale, u, u->race));
  battlerecord(b, buf);
  return level;
}

int
sp_wolfhowl(fighter * fi, int level, double power, spell * sp)
{
  battle *b = fi->side->battle;
  region *r = b->region;
  unit *mage = fi->unit;
  attrib *a;
  int force = (int)(get_force(power, 3)/2);
  unit *u = create_unit(r, mage->faction, force, new_race[RC_WOLF], 0, NULL, mage);
  unused(sp);

  u->status = ST_FIGHT;

  set_string(&u->name, racename(mage->faction->locale, u, u->race));
  set_level(u, SK_WEAPONLESS, (int)(power/3));
  set_level(u, SK_AUSDAUER, (int)(power/3));
  u->hp = u->number * unit_max_hp(u);

  if (fval(mage, UFL_PARTEITARNUNG))
    fset(u, UFL_PARTEITARNUNG);

  a = a_new(&at_unitdissolve);
  a->data.ca[0] = 0;
  a->data.ca[1] = 100;
  a_add(&u->attribs, a);

  make_fighter(b, u, fi->side, is_attacker(fi));
  sprintf(buf, "%s ruft %d %s zu Hilfe", unitname(mage), force,
    racename(default_locale, u, u->race));
  battlerecord(b, buf);
  return level;
}

int
sp_shadowknights(fighter * fi, int level, double power, spell * sp)
{
  unit *u;
  battle *b = fi->side->battle;
  region *r = b->region;
  unit *mage = fi->unit;
  attrib *a;
  int force = max(1, (int)get_force(power, 3));

  unused(sp);

  u = create_unit(r, mage->faction, force, new_race[RC_SHADOWKNIGHT], 0, NULL, mage);
  u->status = ST_FIGHT;

  set_string(&u->name, "Schattenritter");
  u->hp = u->number * unit_max_hp(u);

  if (fval(mage, UFL_PARTEITARNUNG))
    fset(u, UFL_PARTEITARNUNG);

  a = a_new(&at_unitdissolve);
  a->data.ca[0] = 0;
  a->data.ca[1] = 100;
  a_add(&u->attribs, a);

  make_fighter(b, u, fi->side, is_attacker(fi));

  sprintf(buf, "%s beschw�rt Trugbilder herauf", unitname(mage));
  battlerecord(b, buf);
  return level;
}

int
sp_strong_wall(fighter * fi, int level, double power, spell * sp)
{
  battle *b = fi->side->battle;
  unit *mage = fi->unit;
  building *burg;
  variant effect;
  static boolean init = false;
  static const curse_type * strongwall_ct;
  if (!init) { init = true; strongwall_ct = ct_find("strongwall"); }

  unused(sp);

  if (!mage->building) {
    sprintf(buf, "%s zaubert nicht, denn dieser Zauber h�tte hier keinen "
        "Sinn.", unitname(mage));
    battlerecord(b, buf);
    return 0;
  }
  burg = mage->building;

  effect.i = (int)(power/4);
  if (chance(power-effect.i)) ++effect.i;

  create_curse(mage, &burg->attribs, strongwall_ct, power, 1, effect, 0);

  sprintf(buf, "%s Mauern ergl�hen in einem unheimlichen magischen Licht.",
      buildingname(burg));
  battlerecord(b, buf);
  return level;
}

int
sp_chaosrow(fighter * fi, int level, double power, spell * sp)
{
  battle *b = fi->side->battle;
  unit *mage = fi->unit;
  cvector *fgs;
  void **fig;
  int k = 0;

  switch (sp->id) {
    case SPL_CHAOSROW:
      sprintf(buf, "%s murmelt eine d�ster klingende Formel", unitname(mage));
      power *= 40;
      break;

    case SPL_SONG_OF_CONFUSION:
      sprintf(buf, "%s stimmt einen seltsamen Gesang an", unitname(mage));
      power = get_force(power, 5);
      break;
  }

  if (!count_enemies(b, fi, FIGHT_ROW, NUMROWS, SELECT_ADVANCE|SELECT_FIND)) {
    scat(", aber niemand war in Reichweite.");
    battlerecord(b, buf);
    return 0;
  }
  scat(". ");

  fgs = fighters(b, fi->side, FIGHT_ROW, NUMROWS, FS_ENEMY);
  v_scramble(fgs->begin, fgs->end);

  for (fig = fgs->begin; fig != fgs->end; ++fig) {
    fighter *df = *fig;
    int n = df->unit->number;

    if (df->alive==0) continue;
    if (power<=0.0) break;
    /* force sollte wegen des max(0,x) nicht unter 0 fallen k�nnen */

    if (is_magic_resistant(mage, df->unit, 0)) continue;

    if (chance(power/n)) {
      int row = statusrow(df->status);
      df->side->size[row] -= df->alive;
      if (df->unit->race->battle_flags & BF_NOBLOCK) {
        df->side->nonblockers[row] -= df->alive;
      }
      row = FIRST_ROW + (rng_int()%(LAST_ROW-FIRST_ROW));
      switch (row) {
        case FIGHT_ROW:
          df->status = ST_FIGHT;
          break;
        case BEHIND_ROW:
          df->status = ST_CHICKEN;
          break;
        case AVOID_ROW:
          df->status = ST_AVOID;
          break;
        case FLEE_ROW:
          df->status = ST_FLEE;
          break;
        default:
          assert(!"unknown combatrow");
      }
      assert(statusrow(df->status)==row);
      df->side->size[row] += df->alive;
      if (df->unit->race->battle_flags & BF_NOBLOCK) {
        df->side->nonblockers[row] += df->alive;
      }
      k+=df->alive;
    }
    power = max(0, power-n);
  }
  cv_kill(fgs);
  free(fgs);

  scat("Ein pl�tzlicher Tumult entsteht");
  if (k > 0) {
    scat(" und bringt die Kampfaufstellung durcheinander.");
  } else {
    scat(", der sich jedoch schnell wieder legt.");
  }
  battlerecord(b, buf);
  return level;
}

/* Gesang der Furcht (Kampfzauber) */
/* Panik (Pr�kampfzauber) */

int
sp_flee(fighter * fi, int level, double power, spell * sp)
{
  battle *b = fi->side->battle;
  unit *mage = fi->unit;
  cvector *fgs;
  void **fig;
  int force, n;
  int panik = 0;

  switch(sp->id) {
    case SPL_FLEE:
      sprintf(buf, "%s zaubert %s", unitname(mage),
        spell_name(sp, default_locale));
      force = (int)get_force(power,4);
      break;
    case SPL_SONG_OF_FEAR:
      sprintf(buf, "%s stimmt einen d�steren Gesang an", unitname(mage));
      force = (int)get_force(power,3);
      break;
    case SPL_AURA_OF_FEAR:
      sprintf(buf, "%s ist von dunklen Schatten umgeben", unitname(mage));
      force = (int)get_force(power,5);
      break;
    default:
      force = (int)get_force(power,10);
  }

  if (!count_enemies(b, fi, FIGHT_ROW, AVOID_ROW, SELECT_ADVANCE|SELECT_FIND)) {
    scat(", aber es gab niemanden mehr, der beeinflusst werden konnte.");
    battlerecord(b, buf);
    return 0;
  }
  scat(":");
  battlerecord(b, buf);

  fgs = fighters(b, fi->side, FIGHT_ROW, AVOID_ROW, FS_ENEMY);
  v_scramble(fgs->begin, fgs->end);

  for (fig = fgs->begin; fig != fgs->end; ++fig) {
    fighter *df = *fig;
    for (n=0; n!=df->alive; ++n) {
      if (force < 0)
        break;

      if (df->person[n].flags & FL_PANICED) { /* bei SPL_SONG_OF_FEAR m�glich */
        df->person[n].attack -= 1;
        --force;
        ++panik;
      } else if (!(df->person[n].flags & FL_COURAGE)
          || !fval(df->unit->race, RCF_UNDEAD))
      {
        if (is_magic_resistant(mage, df->unit, 0) == false) {
          df->person[n].flags |= FL_PANICED;
          ++panik;
        }
        --force;
      }
    }
  }
  cv_kill(fgs);
  free(fgs);

  sprintf(buf, "%d Krieger %s von Furcht gepackt.", panik,
      panik == 1 ? "wurde" : "wurden");
  battlerecord(b, buf);

  return level;
}

/* Heldenmut */
int
sp_hero(fighter * fi, int level, double power, spell * sp)
{
	battle *b = fi->side->battle;
	unit *mage = fi->unit;
	int df_bonus = 0;
	int force = 0;
	int allies;
	int targets = 0;

  sprintf(buf, "%s zaubert %s", unitname(mage),
    spell_name(sp, default_locale));
  switch(sp->id) {
    case SPL_HERO:
      df_bonus = (int)(power/5);
      force = max(1, lovar(get_force(power, 4)));
      break;

    default:
      df_bonus = 1;
      force = max(1, (int)power);
  }
  scat(":");
  battlerecord(b, buf);

  allies = count_allies(fi->side, FIGHT_ROW, BEHIND_ROW, SELECT_ADVANCE);
  /* maximal 2*allies Versuche ein Opfer zu finden, ansonsten best�nde
   * die Gefahr eine Endlosschleife*/
  allies *= 2;

  while (force && allies) {
    troop dt = select_ally(fi, FIGHT_ROW, BEHIND_ROW);
    fighter *df = dt.fighter;
    --allies;

    if (df) {
      if (!(df->person[dt.index].flags & FL_COURAGE)) {
        df->person[dt.index].defence += df_bonus;
        df->person[dt.index].flags = df->person[dt.index].flags | FL_COURAGE;
        targets++;
        --force;
      }
    }
  }

  sprintf(buf, "%d Krieger %s moralisch gest�rkt",
      targets, targets == 1 ? "wurde" : "wurden");

  scat(".");
  battlerecord(b, buf);
  return level;
}

int
sp_berserk(fighter * fi, int level, double power, spell * sp)
{
  battle *b = fi->side->battle;
  unit *mage = fi->unit;
  int at_bonus = 0;
  int df_malus = 0;
  int force = 0;
  int allies = 0;
  int targets = 0;

  sprintf(buf, "%s zaubert %s", unitname(mage),
    spell_name(sp, default_locale));
  switch(sp->id) {
    case SPL_BERSERK:
    case SPL_BLOODTHIRST:
      at_bonus = max(1,level/3);
      df_malus = 2;
      force = (int)get_force(power,2);
      break;

    default:
      at_bonus = 1;
      df_malus = 0;
      force = (int)power;
  }
  scat(":");
  battlerecord(b, buf);

  allies = count_allies(fi->side, FIGHT_ROW, BEHIND_ROW-1, SELECT_ADVANCE);
  /* maximal 2*allies Versuche ein Opfer zu finden, ansonsten best�nde
   * die Gefahr eine Endlosschleife*/
  allies *= 2;

  while (force && allies) {
    troop dt = select_ally(fi, FIGHT_ROW, BEHIND_ROW-1);
    fighter *df = dt.fighter;
    --allies;

    if (df) {
      if (!(df->person[dt.index].flags & FL_COURAGE)) {
        df->person[dt.index].attack += at_bonus;
        df->person[dt.index].defence -= df_malus;
        df->person[dt.index].flags = df->person[dt.index].flags | FL_COURAGE;
        targets++;
        --force;
      }
    }
  }

  sprintf(buf, "%d Krieger %s in Blutrausch versetzt",
      targets, targets == 1 ? "wurde" : "wurden");

  scat(".");
  battlerecord(b, buf);
  return level;
}

int
sp_frighten(fighter * fi, int level, double power, spell * sp)
{
  battle *b = fi->side->battle;
  unit *mage = fi->unit;
  int at_malus = 0;
  int df_malus = 0;
  int force = 0;
  int enemies = 0;
  int targets = 0;

  at_malus = max(1,level - 4);
  df_malus = 2;
  force = (int)get_force(power, 2);

  sprintf(buf, "%s zaubert %s", unitname(mage),
    spell_name(sp, default_locale));
  enemies = count_enemies(b, fi, FIGHT_ROW, BEHIND_ROW-1, SELECT_ADVANCE);
  if (!enemies) {
    scat(", aber niemand war in Reichweite.");
    battlerecord(b, buf);
    return 0;
  }
  scat(":");
  battlerecord(b, buf);

  while (force && enemies) {
    troop dt = select_enemy(fi, FIGHT_ROW, BEHIND_ROW-1, SELECT_ADVANCE);
    fighter *df = dt.fighter;
    --enemies;

    if (!df)
      break;

    assert(!helping(fi->side, df->side));

    if (df->person[dt.index].flags & FL_COURAGE) {
      df->person[dt.index].flags &= ~(FL_COURAGE);
    }
    if (is_magic_resistant(mage, df->unit, 0) == false) {
      df->person[dt.index].attack -= at_malus;
      df->person[dt.index].defence -= df_malus;
      targets++;
    }
    --force;
  }

  sprintf(buf, "%d Krieger %s eingesch�chtert",
      targets, targets == 1 ? "wurde" : "wurden");

  scat(".");
  battlerecord(b, buf);
  return level;
}


int
sp_tiredsoldiers(fighter * fi, int level, double power, spell * sp)
{
  battle *b = fi->side->battle;
  unit *mage = fi->unit;
  int n = 0;
  int force = (int)(power * power * 4);

  sprintf(buf, "%s zaubert %s", unitname(mage),
    spell_name(sp, default_locale));
  if (!count_enemies(b, fi, FIGHT_ROW, BEHIND_ROW, SELECT_ADVANCE|SELECT_FIND)) {
    scat(", aber niemand war in Reichweite.");
    battlerecord(b, buf);
    return 0;
  }

  while (force) {
    troop t = select_enemy(fi, FIGHT_ROW, BEHIND_ROW, SELECT_ADVANCE);
    fighter *df = t.fighter;

    if (!df)
      break;

    assert(!helping(fi->side, df->side));
    if (!(df->person[t.index].flags & FL_TIRED)) {
      if (is_magic_resistant(mage, df->unit, 0) == false) {
        df->person[t.index].flags = df->person[t.index].flags | FL_TIRED;
        df->person[t.index].defence -= 2;
        ++n;
      }
    }
    --force;
  }

  scat(": ");
  if (n == 0) {
    scat("Der Zauber konnte keinen Krieger erm�den.");
  } else if (n == 1) {
    scat("Ein Krieger schleppt sich m�de in den Kampf.");
  } else {
    icat(n);
    scat(" Krieger schleppen sich m�de in den Kampf.");
  }
  battlerecord(b, buf);
  return level;
}

int
sp_windshield(fighter * fi, int level, double power, spell * sp)
{
  battle *b = fi->side->battle;
  unit *mage = fi->unit;
  int force, at_malus;
  int enemies;

  sprintf(buf, "%s zaubert %s", unitname(mage),
    spell_name(sp, default_locale));
  switch(sp->id) {
    case SPL_WINDSHIELD:
      force = (int)get_force(power,4);
      at_malus = level/4;
      break;

    default:
      force = (int)power;
      at_malus = 2;
  }
  enemies = count_enemies(b, fi, BEHIND_ROW, BEHIND_ROW, SELECT_ADVANCE);
  if (!enemies) {
    scat(", aber niemand war in Reichweite.");
    battlerecord(b, buf);
    return 0;
  }

  while (force && enemies) {
    troop dt = select_enemy(fi, BEHIND_ROW, BEHIND_ROW, SELECT_ADVANCE);
    fighter *df = dt.fighter;
    --enemies;

    if (!df)
      break;
    assert(!helping(fi->side, df->side));

    if (df->person[dt.index].missile) {
      /* this suxx... affects your melee weapon as well. */
      df->person[dt.index].attack -= at_malus;
      --force;
    }
  }

  scat(": ");
  scat("Ein Sturm kommt auf und die Sch�tzen k�nnen kaum noch zielen.");
  battlerecord(b, buf);
  return level;
}

int
sp_reeling_arrows(fighter * fi, int level, double power, spell * sp)
{
  battle *b = fi->side->battle;
  unit *mage = fi->unit;

  unused(power);

  b->reelarrow = true;
  sprintf(buf, "%s zaubert %s", unitname(mage),
    spell_name(sp, default_locale));
  scat(": ");
  scat("Ein Sturm kommt auf und die Sch�tzen k�nnen kaum noch zielen.");
  battlerecord(b, buf);
  return level;
}

int
sp_denyattack(fighter * fi, int level, double power, spell * sp)
{
/* Magier weicht dem Kampf aus. Wenn er sich bewegen kann, zieht er in
 * eine Nachbarregion, wobei ein NACH ber�cksichtigt wird. Ansonsten
 * bleibt er stehen und nimmt nicht weiter am Kampf teil. */
  battle *b = fi->side->battle;
  unit *mage = fi->unit;
  region *r = b->region;
  unused(power);

  sprintf(buf, "%s zaubert %s", unitname(mage),
    spell_name(sp, default_locale));
  scat(": ");

  /* Fliehende Einheiten verlassen auf jeden Fall Geb�ude und Schiffe. */
  leave(r, mage);
  /* und bewachen nicht */
  setguard(mage, GUARD_NONE);
  /* irgendwie den langen befehl sperren */
  /* fset(fi, FIG_ATTACKED); */

  /* Hat der Magier ein NACH, wird die angegebene Richtung bevorzugt */
  switch (get_keyword(mage->thisorder)) {
  case K_MOVE:
  case K_ROUTE:
    init_tokens(mage->thisorder);
    skip_token();
    if (movewhere(mage, getstrtoken(), mage->region, &fi->run.region)!=E_MOVE_OK) {
      fi->run.region = fleeregion(mage);
    }
    break;
  default:
    fi->run.region = fleeregion(mage);
  }
  /* bewegung erst am Ende des Kampfes, zusammen mit den normalen
  * Fl�chtlingen */

  if (fi->run.region) {
    /* wir tun so, als w�re die Person geflohen */
    fset(fi, FIG_NOLOOT);
    fi->run.hp = mage->hp;
    fi->run.number = mage->number;
    /* fighter leeren */
    rmfighter(fi, mage->number);

    scat("Das Kampfget�mmel erstirbt und er kann unbehelligt "
      "seines Weges ziehen.");
    battlerecord(b, buf);
  }
  return level;
}

static void
do_meffect(fighter * af, int typ, int effect, int duration)
{
  battle *b = af->side->battle;
  meffect *meffect = calloc(1, sizeof(struct meffect));
  cv_pushback(&b->meffects, meffect);
  meffect->magician = af;
  meffect->typ = typ;
  meffect->effect = effect;
  meffect->duration = duration;
}

int
sp_armorshield(fighter * fi, int level, double power, spell * sp)
{
  int effect;
  int duration;
  unit *mage = fi->unit;
  battle *b = fi->side->battle;

  sprintf(buf, "%s zaubert %s", unitname(mage),
    spell_name(sp, default_locale));
  battlerecord(b, buf);

  /* gibt R�stung +effect f�r duration Treffer */

  switch(sp->id) {
    case SPL_ARMORSHIELD:
      effect = level/3;
      duration = (int)(20*power*power);
      break;

    default:
      effect = level/4;
      duration = (int)(power*power);
  }
  do_meffect(fi, SHIELD_ARMOR, effect, duration);
  return level;
}

int
sp_reduceshield(fighter * fi, int level, double power, spell * sp)
{
  int effect;
  int duration;
  unit *mage = fi->unit;
  battle *b = fi->side->battle;

  sprintf(buf, "%s zaubert %s", unitname(mage),
    spell_name(sp, default_locale));
  battlerecord(b, buf);

  /* jeder Schaden wird um effect% reduziert bis der Schild duration
   * Trefferpunkte aufgefangen hat */

  switch(sp->id) {
    case SPL_REDUCESHIELD:
      effect = 50;
      duration = (int)(50*power*power);
      break;

    default:
      effect = level*3;
      duration = (int)get_force(power,5);
  }
  do_meffect(fi, SHIELD_REDUCE, effect, duration);
  return level;
}

int
sp_fumbleshield(fighter * fi, int level, double power, spell * sp)
{
  int effect;
  int duration;
  unit *mage = fi->unit;
  battle *b = fi->side->battle;

  sprintf(buf, "%s zaubert %s", unitname(mage),
    spell_name(sp, default_locale));
  battlerecord(b, buf);

  /* der erste Zauber schl�gt mit 100% fehl  */

  switch(sp->id) {
    case SPL_DRAIG_FUMBLESHIELD:
    case SPL_GWYRRD_FUMBLESHIELD:
    case SPL_CERRDOR_FUMBLESHIELD:
    case SPL_TYBIED_FUMBLESHIELD:
      duration = 100;
      effect = max(1, 25-level);
      break;

    default:
      duration = 100;
      effect = 10;
  }
  do_meffect(fi, SHIELD_BLOCK, effect, duration);
  return level;
}

/* ------------------------------------------------------------- */
/* POSTCOMBAT */

static int
count_healable(battle *b, fighter *df)
{
  side *s;
  int  healable = 0;

  for (s=b->sides; s; s=s->next) {
    if (helping(df->side, s)) {
      healable += s->casualties;
    }
  }
  return healable;
}

/* wiederbeleben */
int
sp_reanimate(fighter * fi, int level, double power, spell * sp)
{
  battle *b = fi->side->battle;
  unit *mage = fi->unit;
  int healable, j=0;
  double c = 0.50;
  double k = EFFECT_HEALING_SPELL * power;

  switch(sp->id) {
    case SPL_REANIMATE:
      sprintf(buf, "%s beginnt ein Ritual der Wiederbelebung",
          unitname(mage));
      c += 0.02 * power;
      break;

    default:
      sprintf(buf, "%s zaubert %s",
          unitname(mage),
          spell_name(sp, default_locale));
  }
  if (get_item(mage, I_AMULET_OF_HEALING) > 0) {
    scat(" und benutzt das ");
    scat(locale_string(default_locale, resourcename(oldresourcetype[R_AMULET_OF_HEALING], 0)));
    scat(", um den Zauber zu verst�rken");
    k *= 2;
    c += 0.10;
  }

  healable = count_healable(b, fi);
  healable = (int)min(k, healable);
  while (healable--) {
    fighter * tf = select_corpse(b, fi);
    if (tf!=NULL && tf->side->casualties > 0
                  && tf->unit->race != new_race[RC_DAEMON]
                  && (chance(c)))
    {
      assert(tf->alive < tf->unit->number);
      /* t.fighter->person[].hp beginnt mit t.index = 0 zu z�hlen,
       * t.fighter->alive ist jedoch die Anzahl lebender in der Einheit,
       * also sind die hp von t.fighter->alive
       * t.fighter->hitpoints[t.fighter->alive-1] und der erste Tote
       * oder weggelaufene ist t.fighter->hitpoints[tf->alive] */
      tf->person[tf->alive].hp = 2;
      ++tf->alive;
      ++tf->side->size[SUM_ROW];
      ++tf->side->size[tf->unit->status + 1];
      ++tf->side->healed;
      --tf->side->casualties;
      assert(tf->side->casualties>=0);
      --tf->side->dead;
      assert(tf->side->dead>=0);
      ++j;
    }
  }
  if (j == 0) {
    scat(", kann aber niemanden wiederbeleben.");
    level = 0;
  } else if (j == 1) {
    scat(" und belebt einen Toten wieder.");
    level = 1;
  } else {
    scat(" und belebt ");
    icat(j);
    scat(" Tote wieder.");
  }
  battlerecord(b, buf);

  return level;
}

int
sp_keeploot(fighter * fi, int level, double power, spell * sp)
{
  battle *b = fi->side->battle;

  sprintf(buf, "%s zaubert %s.", unitname(fi->unit),
    spell_name(sp, default_locale));
  battlerecord(b, buf);

  b->keeploot = (int)max(25, b->keeploot + 5*power);

  return level;
}

static int
heal_fighters(cvector *fgs, int * power, boolean heal_monsters)
{
  int healhp = *power;
  int healed = 0;
  void **fig;

  for (fig = fgs->begin; fig != fgs->end; ++fig) {
    fighter *df = *fig;

    if (healhp<=0) break;

    /* Untote kann man nicht heilen */
    if (df->unit->number==0 || fval(df->unit->race, RCF_NOHEAL)) continue;

    /* wir heilen erstmal keine Monster */
    if (heal_monsters || playerrace(df->unit->race)) {
      int n, hp = df->unit->hp / df->unit->number;
      int rest = df->unit->hp % df->unit->number;

      for (n = 0; n < df->unit->number; n++) {
        int wound = hp - df->person[n].hp;
        if (rest>n) ++wound;

        if (wound > 0 && wound < hp) {
          int heal = min(healhp, wound);
          assert(heal>=0);
          df->person[n].hp += heal;
          healhp = max(0, healhp - heal);
          ++healed;
          if (healhp<=0) break;
        }
      }
    }
  }

  *power = healhp;
  return healed;
}

int
sp_healing(fighter * fi, int level, double power, spell * sp)
{
  battle *b = fi->side->battle;
  unit *mage = fi->unit;
  int j = 0;
  int healhp = (int)power;
  cvector *fgs;

  sprintf(buf, "%s k�mmert sich um die Verletzten", unitname(mage));

  /* bis zu 11 Personen pro Stufe (einen HP m�ssen sie ja noch
  * haben, sonst w�ren sie tot) k�nnen geheilt werden */
  healhp *= 200;

  if (get_item(mage, I_AMULET_OF_HEALING) > 0) {
    scat(" und benutzt das ");
    scat(locale_string(default_locale, resourcename(oldresourcetype[R_AMULET_OF_HEALING], 0)));
    scat(", um die Heilzauber zu verst�rken");
    healhp *= 2;
  }

  /* gehe alle denen wir helfen der reihe nach durch, heile verwundete,
  * bis zu verteilende HP aufgebraucht sind */

  fgs = fighters(b, fi->side, FIGHT_ROW, AVOID_ROW, FS_HELP);
  v_scramble(fgs->begin, fgs->end);
  j += heal_fighters(fgs, &healhp, false);
  j += heal_fighters(fgs, &healhp, true);
  cv_kill(fgs);
  free(fgs);

  if (j == 0) {
    scat(", doch niemand mu�te magisch geheilt werden.");
    level = 0;
  } else if (j == 1) {
    scat(" und heilt einen Verwundeten.");
    level = 1;
  } else {
    scat(" und heilt ");
    icat(j);
    scat(" Verwundete.");
  }
  battlerecord(b, buf);

  return level;
}

int
sp_undeadhero(fighter * fi, int level, double power, spell * sp)
{
  battle *b = fi->side->battle;
  unit *mage = fi->unit;
  region *r = b->region;
  cvector *fgs;
  void **fig;
  int n, undead = 0;
  int force = (int)get_force(power,0);
  double c = 0.50 + 0.02 * power;

  /* Liste aus allen K�mpfern */
  fgs = fighters(b, fi->side, FIGHT_ROW, AVOID_ROW, FS_ENEMY | FS_HELP );
  v_scramble(fgs->begin, fgs->end);

  for (fig = fgs->begin; fig != fgs->end; ++fig) {
    fighter *df = *fig;
    unit *du = df->unit;

    if (force<=0) break;

    /* keine Monster */
    if (!playerrace(du->race)) continue;

    if (df->alive + df->run.number < du->number) {
      int j = 0;

      /* Wieviele Untote k�nnen wir aus dieser Einheit wecken? */
      for (n = df->alive + df->run.number; n != du->number; n++) {
        if (chance(c)) {
          ++j;
          if (--force<=0) break;
        }
      }

      if (j > 0) {
        unit * u = create_unit(r, mage->faction, 0, new_race[RC_UNDEAD], 0, NULL, mage);

        /* new units gets some stats from old unit */
        set_string(&u->name, du->name);
        set_string(&u->display, du->display);
        u->status = du->status;
        setguard(u, GUARD_NONE);

        /* inherit stealth from magician */
        if (fval(mage, UFL_PARTEITARNUNG)) {
          fset(u, UFL_PARTEITARNUNG);
        }

        /* transfer dead people to new unit, set hitpoints to those of old unit */
        transfermen(du, u, j);
        u->hp = u->number * unit_max_hp(du);
        assert(j<=df->side->casualties);
        df->side->casualties -= j;
        df->side->dead -= j;

        /* counting total number of undead */
        undead += j;
      }
    }
  }
  cv_kill(fgs);
  free(fgs);

  if (undead == 0) {
    sprintf(buf, "%s kann keine Untoten rufen.", unitname(mage));
    level = 0;
  } else if (undead == 1) {
    sprintf(buf, "%s erweckt einen Untoten.", unitname(mage));
    level = 1;
  } else {
    sprintf(buf, "%s erweckt %d Untote.", unitname(mage), undead);
  }

  battlerecord(b, buf);
  return level;
}


/* ------------------------------------------------------------------ */
