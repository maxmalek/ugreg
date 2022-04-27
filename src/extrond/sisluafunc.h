#pragma once

struct lua_State;
class SISClient;

void sisluafunc_register(lua_State *L, SISClient& cl);
