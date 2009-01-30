/* vi: set ts=2:
*
*	
*	Eressea PB(E)M host Copyright (C) 1998-2003
*      Christian Schlittchen (corwin@amber.kn-bremen.de)
*      Katja Zedel (katze@felidae.kn-bremen.de)
*      Henning Peters (faroul@beyond.kn-bremen.de)
*      Enno Rehling (enno@eressea.de)
*      Ingo Wilken (Ingo.Wilken@informatik.uni-oldenburg.de)
*
* This program may not be used, modified or distributed without
* prior permission by the authors of Eressea.
*/

#ifndef H_KRNL_SPELL
#define H_KRNL_SPELL
#ifdef __cplusplus
extern "C" {
#endif

  struct fighter;
  struct spell;
  struct border_type;
  struct attrib_type;
  struct curse_type;
  struct castorder;
  struct curse;

  /* Prototypen */

  int use_item_power(struct region * r, struct unit * u);
  int use_item_regeneration(struct region * r, struct unit * u);
  void showspells(struct region *r, struct unit *u);
  int sp_antimagiczone(struct castorder *co);
  extern double destr_curse(struct curse* c, int cast_level, double force);

  /* ------------------------------------------------------------- */

  extern struct attrib_type at_unitdissolve;
  extern struct attrib_type at_wdwpyramid;

  extern struct spell_list * spells;
  extern void register_spell(struct spell * sp);
  extern struct spell * find_spell(magic_t mtype, const char * name);
  extern struct spell * find_spellbyid(magic_t mtype, spellid_t i);
  extern struct spell *get_spellfromtoken(struct unit *u, const char *s, const struct locale * lang);

#ifdef __cplusplus
}
#endif
#endif

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
 * BUILDINGSPELL: ZAUBER <spruchname> <Gebaeude-Nr> [<Gebaeude-Nr> ..]
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

