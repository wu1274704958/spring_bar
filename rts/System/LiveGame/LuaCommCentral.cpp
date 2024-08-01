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
    if(!commCentral.IsInit())
        return;
	if (lua_istable(L, -1)) {
        commCentral.SendMsg(LuaTable2JsonStr(L,-1));
	}
	else {
		LOG_L(L_ERROR,"Error: LuaCommCentral::Send Expected a table on the Lua stack.");
	}
	return 0;
}

void LuaCommCentral::Tick()
{
    if (commCentral.IsInit())
    {
        if (commCentral.tick())
        {
            auto msg = commCentral.PopMsg();
            if (msg)
            {
                
            }
        }
    }
}

Json::Value LuaValueToJson(lua_State* L, int index) {
    switch (lua_type(L, index)) {
    case LUA_TNUMBER:
        return Json::Value(lua_tonumber(L, index));
    case LUA_TSTRING:
        return Json::Value(lua_tostring(L, index));
    case LUA_TBOOLEAN:
        return Json::Value(lua_toboolean(L, index));
    case LUA_TTABLE:
        return LuaCommCentral::LuaTable2JsonObj(L, index);
    default:
        return Json::Value();
    }
}

Json::Value LuaCommCentral::LuaTable2JsonObj(lua_State* L, int index)
{
    if (lua_type(L, index) != LUA_TTABLE) {
        return Json::Value();
    }

    Json::Value jsonValue;
    lua_pushstring(L, "IsArray");
    lua_gettable(L, index);

    bool isArray = lua_toboolean(L, -1);
    lua_pop(L, 1);

    if (isArray) {
        int arrayIndex = 1;
        lua_pushnil(L);
        while (lua_next(L, index) != 0) {
            if (lua_type(L, -2) == LUA_TNUMBER) {
                jsonValue[arrayIndex - 1] = LuaValueToJson(L, lua_gettop(L));
                arrayIndex++;
            }
            lua_pop(L, 1);
        }
    }
    else {
        lua_pushnil(L);
        while (lua_next(L, index) != 0) {
            if (lua_type(L, -2) == LUA_TSTRING) {
                const char* key = lua_tostring(L, -2);
                jsonValue[key] = LuaValueToJson(L, lua_gettop(L));
            }
            lua_pop(L, 1);
        }
    }

    return jsonValue;
}

std::string LuaCommCentral::LuaTable2JsonStr(lua_State* L, int index)
{
    auto obj = LuaTable2JsonObj(L,index);
    Json::FastWriter write;
	return write.write(obj);
}
