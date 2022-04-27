#pragma once

#include <lua/lua.hpp>
#include "variant.h"

void luafunc_register(lua_State *L);

// copies var to Lua and leaves the result on top of the stack
// None -> nil
// primitives stay as such
// userdata -> light user ptr
// arrays, maps become tables, recursively
// ranges are ignored
void luaImportVar(lua_State* L, VarCRef ref);


// shortcuts
const char* str(lua_State* L, int idx = 1);
PoolStr strL(lua_State* L, int idx = 1);
u64 checkU64(lua_State* L, int idx = 1);
bool getBool(lua_State* L, int idx = 1);
size_t Sz(lua_State* L, int idx = 1);
