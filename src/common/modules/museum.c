/* vi: set ts=2:
 *
 *	
 *	Eressea PB(E)M host Copyright (C) 1998-2000
 *      Christian Schlittchen (corwin@amber.kn-bremen.de)
 *      Katja Zedel (katze@felidae.kn-bremen.de)
 *      Henning Peters (faroul@beyond.kn-bremen.de)
 *      Enno Rehling (enno@eressea-pbem.de)
 *      Ingo Wilken (Ingo.Wilken@informatik.uni-oldenburg.de)
 *
 * This program may not be used, modified or distributed without
 * prior permission by the authors of Eressea.
 */

/* TODO:
 * - Zweite Ausf�hrung des Benutzes in Zielregion verhindern (Befehl
 *   l�schen)
 * - Meldungen
 */

#include <config.h>

#include <eressea.h>
#include "museum.h"

/* kernel includes */
#include "unit.h"
#include "region.h"
#include "plane.h"
#include "item.h"
#include "terrain.h"
#include "movement.h"
#include "building.h"
#include "save.h"

/* util includes */
#include <attrib.h>
#include <functions.h>
#include <goodies.h>

/* libc includes */
#include <limits.h>
#include <stdlib.h>

resource_type rt_museumticket = {
	{ "museumticket", "museumticket_p"},
	{ "museumticket", "museumticket_p"},
	RTF_ITEM,
	&res_changeitem
};

item_type it_museumticket = {
	&rt_museumticket,
	ITF_NONE, 0, 0,
	NULL,
	&use_museumticket
};

resource_type rt_museumexitticket = {
	{ "museumexitticket", "museumexitticket_p"},
	{ "museumexitticket", "museumexitticket_p"},
	RTF_ITEM,
	&res_changeitem
};

item_type it_museumexitticket = {
	&rt_museumexitticket,
	ITF_CURSED, 0, 0,
	NULL,
	&use_museumexitticket
};

attrib_type at_museumexit = {
	"museumexit", NULL, NULL, NULL, a_writedefault, a_readdefault
};

static void
a_initmuseumgivebackcookie(attrib *a)
{
	a->data.v = calloc(1,sizeof(museumgivebackcookie));
}

static void
a_finalizemuseumgivebackcookie(attrib *a)
{
	free(a->data.v);
}

static void
a_writemuseumgivebackcookie(const attrib *a, FILE *f)
{
	museumgivebackcookie *gbc = (museumgivebackcookie *)a->data.v;
	fprintf(f,"%d %d ", gbc->warden_no, gbc->cookie);
}

static int
a_readmuseumgivebackcookie(attrib *a, FILE *f)
{
	museumgivebackcookie *gbc = (museumgivebackcookie *)a->data.v;
	fscanf(f, "%d %d", &gbc->warden_no, &gbc->cookie);
	return 1;
}

attrib_type at_museumgivebackcookie = {
	"museumgivebackcookie",
	a_initmuseumgivebackcookie,
	a_finalizemuseumgivebackcookie,
	NULL,
	a_writemuseumgivebackcookie,
	a_readmuseumgivebackcookie
};

attrib_type at_warden = {
	"itemwarden", NULL, NULL, NULL, a_writedefault, a_readdefault
};

static void
a_initmuseumgiveback(attrib *a)
{
	a->data.v = calloc(1, sizeof(museumgiveback));
}

static void
a_finalizemuseumgiveback(attrib *a)
{
	i_freeall(((museumgiveback *)(a->data.v))->items);
	free(a->data.v);
}

static void
a_writemuseumgiveback(const attrib *a, FILE *f)
{
	museumgiveback *gb = (museumgiveback *)a->data.v;
	fprintf(f,"%d ", gb->cookie);
	write_items(f, gb->items);
}

static int
a_readmuseumgiveback(attrib *a, FILE *f)
{
	museumgiveback *gb = (museumgiveback *)a->data.v;
	fscanf(f, "%d", &gb->cookie);
	read_items(f, &gb->items);
	return 1;
}

attrib_type at_museumgiveback = {
	"museumgiveback",
	a_initmuseumgiveback,
	a_finalizemuseumgiveback,
	NULL,
	a_writemuseumgiveback,
	a_readmuseumgiveback
};

void
warden_add_give(unit *src, unit *u, const item_type *itype, int n)
{
	attrib *aw = a_find(u->attribs, &at_warden);
	museumgiveback *gb = NULL;
	museumgivebackcookie *gbc;
	attrib *a;

	/* has the giver a cookie corresponding to the warden */
	for(a = a_find(src->attribs, &at_museumgivebackcookie); a; a=a->nexttype) {
		if(((museumgivebackcookie *)(a->data.v))->warden_no == u->no) break;
	}

	/* if not give it one */
	if(!a) {
		a = a_add(&src->attribs, a_new(&at_museumgivebackcookie));
		gbc = (museumgivebackcookie *)a->data.v;
		gbc->warden_no = u->no;
		gbc->cookie = aw->data.i;
		assert(aw->data.i < INT_MAX);
		aw->data.i++;
	} else {
		gbc = (museumgivebackcookie *)(a->data.v);
	}

	/* now we search for the warden's corresponding item list */
	for(a = a_find(u->attribs, &at_museumgiveback); a; a=a->nexttype) {
		gb = (museumgiveback *)a->data.v;
		if(gb->cookie == gbc->cookie) {
			break;
		}
	}

	/* if there's none, give it one */
	if(!gb) {
		a = a_add(&u->attribs, a_new(&at_museumgiveback));
		gb = (museumgiveback *)a->data.v;
		gb->cookie = gbc->cookie;
	}

	/* now register the items */
	i_change(&gb->items, itype, n);

	/* done */

	/* this has a caveat: If the src-unit is destroyed while inside
	 * the museum, the corresponding itemlist of the warden will never
	 * be removed. to circumvent that in a generic way will be extremly
	 * difficult. */
}

void
init_museum(void)
{
	at_register(&at_warden);
	at_register(&at_museumexit);
	at_register(&at_museumgivebackcookie);
	at_register(&at_museumgiveback);

	rt_register(&rt_museumticket);
	it_register(&it_museumticket);

	rt_register(&rt_museumexitticket);
	it_register(&it_museumexitticket);

	register_function((pf_generic)use_museumticket, "usemuseumticket");
	register_function((pf_generic)use_museumexitticket, "usemuseumexitticket");
}

void
create_museum(void)
{
	unsigned int museum_id = hashstring("museum");
	plane *museum = getplanebyid(museum_id);
	region *r;
	building *b;

	if (!museum) {
		museum = create_new_plane(museum_id, "Museum", 9500, 9550,
			9500, 9550, PFL_MUSEUM);
	}

	if(findregion(9525, 9525) == NULL) {
		/* Eingangshalle */
		r = new_region(9525, 9525);
		terraform(r, T_HALL1);
		r->planep  = museum;
		rsetname(r, "Eingangshalle");
		rsethorses(r, 0);
		rsetmoney(r, 0);
		rsetpeasants(r, 0);
		set_string(&r->display, "Die Eingangshalle des Gro�en Museum der 1. Welt ist bereits jetzt ein beeindruckender Anblick. Obwohl das Museum noch nicht er�ffnet ist, vermittelt sie bereits einen Flair exotischer Welten. In den Boden ist ein gro�er Kompass eingelassen, der den Besuchern bei Orientierung helfen soll.");
	}

	r = findregion(9526, 9525);
	if(!r) {
		/* Lounge */
		r = new_region(9526, 9525);
		terraform(r, T_HALL1);
		r->planep  = museum;
		rsetname(r, "Lounge");
		rsethorses(r, 0);
		rsetmoney(r, 0);
		rsetpeasants(r, 0);
		set_string(&r->display, "Die Lounge des gro�en Museums ist ein Platz, in dem sich die Besucher treffen, um die Eindr�cke, die sie gewonnen haben, zu verarbeiten. Gem�tliche Sitzgruppen laden zum Verweilen ein.");
	}

	r = findregion(9526, 9525);
	if(!r->buildings) {
		const building_type * bt_generic = bt_find("generic");
		b = new_building(bt_generic, r, NULL);
		set_string(&b->name, "S�par�e im d�monischen Stil");
		set_string(&b->display, "Diese ganz im d�monischen Stil gehaltene Sitzgruppe ist ganz in dunklen Schwarzt�nen gehalten. Muster fremdartiger Runen bedecken das merkw�rdig geformte Mobiliar, das unangenehm lebendig wirkt.");

		b = new_building(bt_generic, r, NULL);
		set_string(&b->name, "S�par�e im elfischen Stil");
		set_string(&b->display, "Ganz in Gr�n- und Braunt�nen gehalten wirkt die Sitzgruppe fast lebendig. Bei n�herer Betrachtung erschlie�t sich dem Betrachter, da� sie tats�chlich aus lebenden Pflanzen erstellt ist. So ist der Tisch aus einem eizigen Baum gewachsen, und die Polster bestehen aus weichen Grassoden. Ein wundersch�n gemusterter Webteppich mit tausenden naturgetreu eingestickter Blumensarten bedeckt den Boden.");

		b = new_building(bt_generic, r, NULL);
		set_string(&b->name, "S�par�e im halblingschen Stil");
		set_string(&b->display, "Dieses rustikale Mobiliar ist aus einem einzigen, gewaltigen Baum hergestellt worden. Den Stamm haben flei�ige Halblinge der L�nge nach gevierteilt und aus den vier langen Viertelst�mmen die Sitzb�nke geschnitzt, w�hrend der verbleibende Stumpf als Tisch dient. Schon von weitem steigen dem Besucher die Ger�che der K�stlichkeiten entgegen, die auf dem Tisch stapeln.");

		b = new_building(bt_generic, r, NULL);
		set_string(&b->name, "S�par�e im orkischen Stil");
		set_string(&b->display, "Grobgeschreinerte, elfenhautbespannte St�hle und ein Tisch aus Knochen, �ber deren Herkunft man sich lieber keine Gedanken macht, bilden die Sitzgruppe im orkischen Stil. �berall haben Orks ihre Namen, und anderes wenig zitierenswertes in das Holz und Gebein geritzt.");

		b = new_building(bt_generic, r, NULL);
		set_string(&b->name, "S�par�e im Meermenschenstil");
		set_string(&b->display, "Ganz in Blau- und Gr�nt�nen gehalten, mit Algen und Muscheln verziert wirken die aus altem Meerholz geschnitzten St�hle immer ein wenig feucht. Seltsammerweise hat der schwere aus alten Planken gezimmerte Tisch einen Mast mit kompletten Segel in der Mitte.");

		b = new_building(bt_generic, r, NULL);
		set_string(&b->name, "S�par�e im Katzenstil");
		set_string(&b->display, "Die W�nde dieses S�par�e sind aus dunklem Holz. Was aus der Ferne wie ein chaotisch durchbrochenes Flechtwerk wirkt, entpuppt sich bei n�herer Betrachtung als eine bis in winzige Details gestaltete dschungelartige Landschaft, in die eine Vielzahl von kleinen Bildergeschichten eingewoben sind. Wie es scheint hat sich der K�nstler M�he gegeben wirklich jedes Katzenvolk Eresseas zu portr�tieren. Das schummrige Innere wird von einem Kamin dominiert, vor dem einige Sessel und weiche Kissen zu einem gem�tlichen Nickerchen einladen. Feiner Anduner Sisal bezieht die Lehnen der Sessel und verlockt dazu, seine Krallen hinein zu versenken. Auf einem kleinen Ecktisch steht ein gro�er Korb mit roten Wollkn�ulen und grauen und braunen Spielm�usen.");
	} else {
		for(b=r->buildings; b; b=b->next) {
			b->size = b->type->maxsize;
		}
	}

	r = findregion(9524, 9526);
	if(!r) {
		r = new_region(9524, 9526);
		terraform(r, T_CORRIDOR1);
		r->planep  = museum;
		rsetname(r, "N�rdliche Promenade");
		rsethorses(r, 0);
		rsetmoney(r, 0);
		rsetpeasants(r, 0);
		set_string(&r->display, "Die N�rdliche Promenade f�hrt direkt in den naturgeschichtlichen Teil des Museums.");
	}
	r = findregion(9525, 9524);
	if(!r) {
		r = new_region(9525, 9524);
		terraform(r, T_CORRIDOR1);
		r->planep  = museum;
		rsetname(r, "S�dliche Promenade");
		rsethorses(r, 0);
		rsetmoney(r, 0);
		rsetpeasants(r, 0);
		set_string(&r->display, "Die S�dliche Promenade f�hrt den Besucher in den kulturgeschichtlichen Teil des Museums.");
	}
}

int
use_museumticket(unit *u, const struct item_type *itype, int amount, const char * cmd)
{
	attrib *a;
	region *r = u->region;

	unused(amount);

	/* Pr�fen ob in normaler Plane und nur eine Person */
	if(r->planep != NULL) {
		cmistake(u, cmd, 265, MSG_MAGIC);
		return 0;
	}
	if(u->number != 1) {
		cmistake(u, cmd, 267, MSG_MAGIC);
		return 0;
	}
	if(get_item(u, I_HORSE)) {
		cmistake(u, cmd, 272, MSG_MAGIC);
		return 0;
	}

	/* In diesem Attribut merken wir uns, wohin die Einheit zur�ckgesetzt
	 * wird, wenn sie das Museum verl��t. */

	a = a_add(&u->attribs, a_new(&at_museumexit));
	a->data.sa[0] = (short)r->x;
	a->data.sa[1] = (short)r->y;

	/* Benutzer in die Halle teleportieren */
	move_unit(u, findregion(9525, 9525), NULL);

	/* Ticket abziehen */
	i_change(&u->items, &it_museumticket, -1);

	/* Benutzer ein Exitticket geben */
	i_change(&u->items, &it_museumexitticket, 1);

	return 1;
}

int
use_museumexitticket(unit *u, const struct item_type *itype, int amount, const char * cmd)
{
	attrib *a;
	region *r;
	unit   *warden = findunit(atoi36("mwar"));
	int    unit_cookie;

	unused(amount);

	/* Pr�fen ob in Eingangshalle */
	if(u->region->x != 9525 || u->region->y != 9525) {
		cmistake(u, cmd, 266, MSG_MAGIC);
		return 0;
	}

	a = a_find(u->attribs, &at_museumexit); assert(a);
	r = findregion(a->data.sa[0], a->data.sa[1]); assert(r);
	a_remove(&u->attribs, a);

	/* �bergebene Gegenst�nde zur�ckgeben */

	a = a_find(u->attribs, &at_museumgivebackcookie);
	unit_cookie = a->data.i;
	a_remove(&u->attribs, a);

	if(a) {
		for(a = a_find(warden->attribs, &at_museumgiveback); a; a = a->nexttype) {
			if(((museumgiveback *)(a->data.v))->cookie == unit_cookie) break;
		}
		if(a) {
			museumgiveback *gb = (museumgiveback *)(a->data.v);
			item *it;

			for(it = gb->items; it; it = it->next) {
				i_change(&u->items, it->type, it->number);
			}
			sprintf(buf, "von %s: 'Hier habt ihr eure Sachen zur�ck.'", unitname(warden));
			addstrlist(&u->botschaften, buf);
			a_remove(&warden->attribs, a);
		}
	}

	/* Benutzer zur�ck teleportieren */
	move_unit(u, r, NULL);

	/* Exitticket abziehen */
	i_change(&u->items, &it_museumexitticket, -1);

	return 1;
}

