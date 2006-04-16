#ifndef LUA_BINDINGS_H
#define LUA_BINDINGS_H

extern void bind_region(struct lua_State * L);
extern void bind_unit(struct lua_State * L);
extern void bind_ship(struct lua_State * L);
extern void bind_building(struct lua_State * L);
extern void bind_faction(struct lua_State * L);
extern void bind_alliance(struct lua_State * L);
extern void bind_eressea(struct lua_State * L);
extern void bind_spell(struct lua_State * L) ;
extern void bind_item(struct lua_State * L);
extern void bind_event(struct lua_State * L);
extern void bind_message(struct lua_State * L);
extern void bind_objects(struct lua_State * L);
extern void bind_script(struct lua_State * L);
extern void bind_gamecode(struct lua_State * L);

extern bool is_function(struct lua_State * L, const char * fname);

#endif
