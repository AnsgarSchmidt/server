#include <config.h>
#include <eressea.h>
#include "list.h"

// kernel includes
#include <kernel/alliance.h>
#include <kernel/faction.h>
#include <kernel/item.h>
#include <kernel/message.h>
#include <kernel/race.h>
#include <kernel/unit.h>

// util includes
#include <util/base36.h>
#include <util/attrib.h>

// attrib includes
#include <attributes/attributes.h>
#include <attributes/variable.h>

// lua includes
#include <lua.hpp>
#include <luabind/luabind.hpp>
#include <luabind/iterator_policy.hpp>
#ifdef HAVE_LUABIND_B7
# include <luabind/operator.hpp>
#endif

#include <ostream>
using namespace luabind;

static faction *
add_faction(const char * email, const char * racename, const char * lang)
{
  const race * frace = findrace(racename, default_locale);
  if (frace==NULL) frace = findrace(racename, find_locale("de"));
  if (frace==NULL) frace = findrace(racename, find_locale("en"));
  if (frace==NULL) return NULL;
  locale * loc = find_locale(lang);
  faction * f = addfaction(email, NULL, frace, loc, 0);
  return f;
}

static eressea::list<faction *>
get_factions(void) {
  return eressea::list<faction *>(factions);
}

class factionunit {
public:
  static unit * next(unit * node) { return node->nextF; }
  static unit * value(unit * node) { return node; }
};

static eressea::list<unit *, unit *, factionunit>
faction_units(const faction& f)
{
  return eressea::list<unit *, unit *, factionunit>(f.units);
}

static void
faction_setalliance(faction& f, alliance * team)
{
  if (f.alliance==0) setalliance(&f, team);
}

static alliance *
faction_getalliance(const faction& f)
{
  return f.alliance;
}

const char *
faction_getlocale(const faction& f)
{
  return locale_name(f.locale);
}

void
faction_setlocale(faction& f, const char * name)
{
	f.locale = find_locale(name);
}

static std::ostream& 
operator<<(std::ostream& stream, const faction& f)
{
  stream << factionname(&f);
  return stream;
}

static bool 
operator==(const faction& a, const faction&b)
{
  return a.no==b.no;
}

static struct helpmode {
  const char * name;
  int status;
} helpmodes[] = {
  { "money", HELP_MONEY },
  { "fight", HELP_FIGHT },
  { "observe", HELP_OBSERVE },
  { "give", HELP_GIVE },
  { "guard", HELP_GUARD },
  { "stealth", HELP_FSTEALTH },
  { "travel", HELP_TRAVEL },
  { "all", HELP_ALL },
  { NULL, 0 }
};

static int
faction_getpolicy(const faction& a, const faction& b, const char * flag)
{
  int mode;

  for (mode=0;helpmodes[mode].name!=NULL;++mode) {
    if (strcmp(flag, helpmodes[mode].name)==0) {
      return get_alliance(&a, &b) & mode;
    }
  }
  return 0;
}

static void
faction_setpolicy(faction& a, faction& b, const char * flag, bool value)
{
  int mode;

  for (mode=0;helpmodes[mode].name!=NULL;++mode) {
    if (strcmp(flag, helpmodes[mode].name)==0) {
      if (value) set_alliance(&a, &b, get_alliance(&a, &b) | helpmodes[mode].status);
      else set_alliance(&a, &b, get_alliance(&a, &b) & ~helpmodes[mode].status);
      break;
    }
  }
}

static const char *
faction_get_variable(faction& f, const char *key)
{
	return get_variable((&f)->attribs, key);
}

static void
faction_set_variable(faction& f, const char *key, const char *value)
{
	set_variable(&((&f)->attribs), key, value);
}

static void
faction_delete_variable(faction& f, const char *key)
{
	return delete_variable(&((&f)->attribs), key);
}

static int
faction_additem(faction& f, const char * iname, int number)
{
  const item_type * itype = it_find(iname);
  if (itype!=NULL) {
    item * i = i_change(&f.items, itype, number);
    return i?i->number:0;
  } // if (itype!=NULL)
  return -1;
}

static void
faction_addnotice(faction& f, const char * str)
{
  str = LOC(f.locale, str);
  ADDMSG(&f.msgs, msg_message("msg_event", "string", str));
}

static const char *
faction_getrace(const faction& f)
{
  return f.race->_name[0];
}

static void
faction_setrace(faction& f, const char * rcname)
{
  race * rc = rc_find(rcname);
  if (rc!=NULL) {
    f.race = rc;
  }
}


void
bind_faction(lua_State * L) 
{
  module(L)[
    def("factions", &get_factions, return_stl_iterator),
    def("get_faction", &findfaction),
    def("add_faction", &add_faction),

    class_<struct faction>("faction")
    .def(tostring(self))
    .def(self == faction())
    .def("set_policy", &faction_setpolicy)
    .def("get_policy", &faction_getpolicy)
    .def("set_variable", &faction_set_variable)
    .def("get_variable", &faction_get_variable)
    .def("delete_variable", &faction_delete_variable)
    .def_readonly("info", &faction::banner)
    .def_readonly("name", &faction::name)
    .def_readonly("password", &faction::passw)
    .def_readonly("email", &faction::email)
    .def_readonly("id", &faction::no)
    .def_readwrite("age", &faction::age)
    .def_readwrite("subscription", &faction::subscription)
    .def_readwrite("lastturn", &faction::lastorders)
    .def("add_item", &faction_additem)
    .def("add_notice", &faction_addnotice)
    .property("locale", &faction_getlocale, &faction_setlocale)
    .property("units", &faction_units, return_stl_iterator)
    .property("alliance", &faction_getalliance, &faction_setalliance)
    .property("race", &faction_getrace, &faction_setrace)
  ];
}
