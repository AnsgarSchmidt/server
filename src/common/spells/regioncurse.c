/* vi: set ts=2:
 *
 * Eressea PB(E)M host Copyright (C) 1998-2003
 *      Christian Schlittchen (corwin@amber.kn-bremen.de)
 *      Katja Zedel (katze@felidae.kn-bremen.de)
 *      Henning Peters (faroul@beyond.kn-bremen.de)
 *      Enno Rehling (enno@eressea-pbem.de)
 *      Ingo Wilken (Ingo.Wilken@informatik.uni-oldenburg.de)
 *
 * This program may not be used, modified or distributed without
 * prior permission by the authors of Eressea.
 */

#include <config.h>
#include "eressea.h"
#include "regioncurse.h"

/* kernel includes */
#include <kernel/curse.h>
#include <kernel/magic.h>
#include <kernel/message.h>
#include <kernel/objtypes.h>
#include <kernel/region.h>
#include <kernel/terrain.h>
#include <kernel/unit.h>

/* util includes */
#include <util/nrmessage.h>
#include <util/message.h>
#include <util/functions.h>

/* libc includes */
#include <string.h>
#include <stdlib.h>
#include <assert.h>

/* --------------------------------------------------------------------- */
/* CurseInfo mit Spezialabfragen 
 */

/*
 * godcursezone
 */
static message *
cinfo_cursed_by_the_gods(const void * obj, typ_t typ, const curse *c, int self)
{
  region *r = (region *)obj;;

  unused(typ);
  unused(self);
  assert(typ == TYP_REGION);
  
  if (fval(r->terrain, SEA_REGION)) {
    return msg_message("curseinfo::godcurseocean", "id", c->no);
  }
  return msg_message("curseinfo::godcurse", "id", c->no);
}

static struct curse_type ct_godcursezone = {
  "godcursezone",
  CURSETYP_NORM, 0, (NO_MERGE),
  "Diese Region wurde von den G�ttern verflucht. Stinkende Nebel ziehen "
  "�ber die tote Erde, furchbare Kreaturen ziehen �ber das Land. Die Brunnen "
  "sind vergiftet, und die wenigen essbaren Fr�chte sind von einem rosa Pilz "
  "�berzogen. Niemand kann hier lange �berleben.",
  cinfo_cursed_by_the_gods,
};


/* --------------------------------------------------------------------- */
/*
 * C_GBDREAM
 */
static message *
cinfo_dreamcurse(const void * obj, typ_t typ, const curse *c, int self)
{
  unused(self);
  unused(typ);
  unused(obj);
  assert(typ == TYP_REGION);

  if (curse_geteffect(c) > 0) {
    return msg_message("curseinfo::gooddream", "id", c->no);
  }
  return msg_message("curseinfo::baddream", "id", c->no);
}

static struct curse_type ct_gbdream = { 
  "gbdream",
  CURSETYP_NORM, 0, (NO_MERGE),
  "",
  cinfo_dreamcurse
};

/* --------------------------------------------------------------------- */
/*
 * C_MAGICSTREET
 *  erzeugt Stra�ennetz
 */
static message *
cinfo_magicstreet(const void * obj, typ_t typ, const curse *c, int self)
{
  unused(typ);
  unused(self);
  unused(obj);
  assert(typ == TYP_REGION);

  /* Warnung vor Aufl�sung */
  if (c->duration <= 2) {
    return msg_message("curseinfo::magicstreet", "id", c->no);
  }
  return msg_message("curseinfo::magicstreetwarn", "id", c->no);
}

static struct curse_type ct_magicstreet = {
  "magicstreet",
  CURSETYP_NORM, 0, (M_DURATION | M_VIGOUR),
  "Es scheint sich um einen elementarmagischen Zauber zu handeln, der alle "
  "Pfade und Wege so gut festigt, als w�ren sie gepflastert. Wie auf einer "
  "Stra�e kommt man so viel besser und schneller vorw�rts.",
  cinfo_magicstreet
};

/* --------------------------------------------------------------------- */

static message *
cinfo_antimagiczone(const void * obj, typ_t typ, const curse *c, int self)
{
  unused(typ);
  unused(self);
  unused(obj);
  assert(typ == TYP_REGION);

  /* Magier sp�ren eine Antimagiezone */
  if (self != 0) {
    return msg_message("curseinfo::antimagiczone", "id", c->no);
  }

  return NULL;
}

/* alle Magier k�nnen eine Antimagiezone wahrnehmen */
static int
cansee_antimagiczone(const struct faction *viewer, const void * obj, typ_t typ, const curse *c, int self)
{
  region *r;
  unit *u = NULL;
  unit *mage = c->magician;

  unused(typ);

  assert(typ == TYP_REGION);
  r = (region *)obj;
  for (u = r->units; u; u = u->next) {
    if (u->faction==viewer) {
      if (u==mage) {
        self = 2;
        break;
      }
      if (is_mage(u)) {
        self = 1;
      }
    } 
  }
  return self;
}
static struct curse_type ct_antimagiczone = { 
  "antimagiczone",
  CURSETYP_NORM, 0, (M_DURATION | M_VIGOUR),
  "Dieser Zauber scheint magische Energien irgendwie abzuleiten und "
  "so alle in der Region gezauberten Spr�che in ihrer Wirkung zu "
  "schw�chen oder ganz zu verhindern.",
  cinfo_antimagiczone, NULL, NULL, NULL, cansee_antimagiczone
};

/* --------------------------------------------------------------------- */
static message *
cinfo_farvision(const void * obj, typ_t typ, const curse *c, int self)
{
  unused(typ);
  unused(obj);

  assert(typ == TYP_REGION);

  /* Magier sp�ren eine farvision */
  if (self != 0) {
    return msg_message("curseinfo::farvision", "id", c->no);
  }

  return 0;
}

static struct curse_type ct_farvision = { 
  "farvision",
  CURSETYP_NORM, 0, (NO_MERGE),
  "",
  cinfo_farvision
};


/* --------------------------------------------------------------------- */

static struct curse_type ct_fogtrap = {
  "fogtrap",
  CURSETYP_NORM, 0, (M_DURATION | M_VIGOUR),
  "",
  cinfo_simple
};
static struct curse_type ct_maelstrom = {
  "maelstrom",
  CURSETYP_NORM, 0, (M_DURATION | M_VIGOUR),
  "Dieser Zauber verursacht einen gigantischen magischen Strudel. Der "
  "Mahlstrom wird alle Schiffe, die in seinen Sog geraten, schwer "
  "besch�digen.",
  NULL
};
static struct curse_type ct_blessedharvest = {
  "blessedharvest",
  CURSETYP_NORM, 0, ( M_DURATION | M_VIGOUR ),
  "Dieser Fruchtbarkeitszauber erh�ht die Ertr�ge der Felder.",
  cinfo_simple
};
static struct curse_type ct_drought = {
  "drought",
  CURSETYP_NORM, 0, ( M_DURATION | M_VIGOUR ),
  "Dieser Zauber strahlt starke negative Energien aus. Warscheinlich "
  "ist er die Ursache der D�rre."	,
  cinfo_simple
};
static struct curse_type ct_badlearn = {
  "badlearn",
  CURSETYP_NORM, 0, ( M_DURATION | M_VIGOUR ),
  "Dieser Zauber scheint die Ursache f�r die Schlaflosigkeit und "
  "Mattigkeit zu sein, unter der die meisten Leute hier leiden und "
  "die dazu f�hrt, das Lernen weniger Erfolg bringt. ",
  cinfo_simple
};
/*  Tr�bsal-Zauber */
static struct curse_type ct_depression = {
  "depression",
  CURSETYP_NORM, 0, ( M_DURATION | M_VIGOUR ),
  "Wie schon zu vermuten war, sind der ewig graue Himmel und die "
  "depressive Stimmung in der Region nicht nat�rlich. Dieser Fluch "
  "hat sich wie ein bleiernes Tuch auf die Gem�ter der Bev�lkerung "
  "gelegt und eh er nicht gebrochen oder verklungen ist, wird keiner "
  "sich an Gaukelleien erfreuen k�nnen.",
  cinfo_simple
};

/* Astralblock, auf Astralregion */
static struct curse_type ct_astralblock = {
  "astralblock",
  CURSETYP_NORM, 0, NO_MERGE,
  "",
  cinfo_simple
};
/* Unterhaltungsanteil vermehren */
static struct curse_type ct_generous = {
  "generous",
  CURSETYP_NORM, 0, ( M_DURATION | M_VIGOUR | M_MAXEFFECT ),
  "Dieser Zauber beeinflusst die allgemeine Stimmung in der Region positiv. "
  "Die gute Laune macht die Leute freigiebiger.",
  cinfo_simple
};
/* verhindert Attackiere regional */
static struct curse_type ct_peacezone = {
  "peacezone",
  CURSETYP_NORM, 0, NO_MERGE,
  "Dieser machtvoller Beeinflussungszauber erstickt jeden Streit schon im "
  "Keim.",
  cinfo_simple
};
/* erschwert geordnete Bewegungen */
static struct curse_type ct_disorientationzone = {
  "disorientationzone",
  CURSETYP_NORM, 0, NO_MERGE,
  "",
  cinfo_simple
};
/*  erniedigt Magieresistenz von nicht-aliierten Einheiten, wirkt nur 1x
*  pro Einheit */
static struct curse_type ct_badmagicresistancezone = {
  "badmagicresistancezone",
  CURSETYP_NORM, 0, NO_MERGE,
  "Dieses Lied, das irgendwie in die magische Essenz der Region gewoben "
  "ist, schw�cht die nat�rliche Widerstandskraft gegen eine "
  "Verzauberung. Es scheint jedoch nur auf bestimmte Einheiten zu wirken.",
  cinfo_simple
};
/* erh�ht Magieresistenz von aliierten Einheiten, wirkt nur 1x pro
* Einheit */
static struct curse_type ct_goodmagicresistancezone = {
  "goodmagicresistancezone",
  CURSETYP_NORM, 0, NO_MERGE,
  "Dieses Lied, das irgendwie in die magische Essenz der Region gewoben "
  "ist, verst�rkt die nat�rliche Widerstandskraft gegen eine "
  "Verzauberung. Es scheint jedoch nur auf bestimmte Einheiten zu wirken.",
  cinfo_simple
};
static struct curse_type ct_riotzone = {
  "riotzone",
  CURSETYP_NORM, 0, (M_DURATION),
  NULL,
  cinfo_simple
};
static struct curse_type ct_holyground = {
  "holyground",
  CURSETYP_NORM, 0, (M_VIGOUR_ADD),
  "Verschiedene Naturgeistern sind im Boden der Region gebunden und "
  "besch�tzen diese vor dem der dunklen Magie des lebenden Todes.",
  cinfo_simple
};
static struct curse_type ct_healing = {
  "healingzone",
  CURSETYP_NORM, 0, (M_VIGOUR | M_DURATION),
  "Heilung ist in dieser Region magisch beeinflusst.",
  cinfo_simple
};


void 
register_regioncurse(void)
{
  ct_register(&ct_fogtrap);
  ct_register(&ct_antimagiczone);
  ct_register(&ct_farvision);
  ct_register(&ct_gbdream);
  ct_register(&ct_maelstrom);
  ct_register(&ct_blessedharvest);
  ct_register(&ct_drought);
  ct_register(&ct_badlearn);
  ct_register(&ct_depression);
  ct_register(&ct_astralblock);
  ct_register(&ct_generous);
  ct_register(&ct_peacezone);
  ct_register(&ct_disorientationzone);
  ct_register(&ct_magicstreet);
  ct_register(&ct_badmagicresistancezone);
  ct_register(&ct_goodmagicresistancezone);
  ct_register(&ct_riotzone);
  ct_register(&ct_godcursezone);
  ct_register(&ct_holyground);
  ct_register(&ct_healing);
}


