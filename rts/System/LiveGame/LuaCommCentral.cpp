#include "LuaCommCentral.h"
#include "Lua/LuaUtils.h"
#include "System/Log/ILog.h"
#include <json/writer.h>
#include <json/json.h>

CCommCentral commCentral;

int LuaCommCentral::Init(lua_State* L)
{
	auto memKey = luaL_checkstring(L, 1);
	auto size = luaL_checkinteger(L, 2);

	lua_pushboolean(L,commCentral.Init(memKey, size));

	return 1;
}

int LuaCommCentral::Release(lua_State* L)
{
	commCentral.Destroy();
	return 0;
}

int LuaCommCentral::Send(lua_State* L)
{
	if (lua_istable(L, -1)) {
		
	}
	else {
		LOG_L(L_ERROR,"Error: LuaCommCentral::Send Expected a table on the Lua stack.");
	}
	return 0;
}

Json::Value LuaCommCentral::LuaTable2JsonObj(lua_State* L, int index)
{

	return Json::Value();
}

std::string LuaCommCentral::LuaTable2JsonStr(lua_State* L, int index)
{
	return std::string();
}
