#pragma once

struct lua_State;
class SISClient;

void sisluafunc_register(lua_State *L, SISClient& cl);

// copies var to Lua and leaves the result on top of the stack
// None -> nil
// primitives stay as such
// userdata -> light user ptr
// arrays, maps become tables, recursively
// ranges are ignored
void luaImportVar(lua_State *L, VarCRef ref);

