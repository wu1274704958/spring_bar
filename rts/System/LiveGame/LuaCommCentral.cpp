#include "LuaCommCentral.h"
#include "Lua/LuaUtils.h"
#include "System/Log/ILog.h"
#include <json/writer.h>
#include <json/json.h>
#include <System/EventHandler.h>

CCommCentral commCentral;

bool LuaCommCentral::PushEntries(lua_State* L)
{
    RECOIL_DETAILED_TRACY_ZONE;
    REGISTER_LUA_CFUNC(InitLMCommCentral);
    REGISTER_LUA_CFUNC(ReleaseLMCommCentral);
    REGISTER_LUA_CFUNC(SendLocalMemMsg);
    REGISTER_LUA_CFUNC(TickLMCommCentral);
    return true;
}

int LuaCommCentral::InitLMCommCentral(lua_State* L)
{
	auto memKey = luaL_checkstring(L, 1);
	auto size = luaL_checkinteger(L, 2);

	lua_pushboolean(L,commCentral.Init(memKey, size));

	return 1;
}

int LuaCommCentral::ReleaseLMCommCentral(lua_State* L)
{
	commCentral.Destroy(luaL_optboolean(L,1,true));
	return 0;
}

int LuaCommCentral::SendLocalMemMsg(lua_State* L)
{
    if(!commCentral.IsInit())
        return 0;
	if (lua_istable(L, -1)) {
        commCentral.SendMsg(LuaTable2JsonStr(L,1));
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
            if (msg.has_value())
            {
                eventHandler.OnRecvLocalMsg(std::move(*msg));
            }
        }
    }
}

int LuaCommCentral::TickLMCommCentral(lua_State* L)
{
    Tick();
    return 0;
}

Json::Value LuaValueToJson(lua_State* L, int index) {
    switch (lua_type(L, index)) {
    case LUA_TNUMBER:
    {
        auto v = lua_tonumber(L, index);
        if(std::floor(v) == v)
            return Json::Value((int)v);
        return Json::Value(v);
    }
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

bool LuaCommCentral::Str2LuaTableAndPush(lua_State* L, const std::string& msg)
{
    Json::CharReaderBuilder readerBuilder;
    Json::Value root;
    std::string errors;

    std::unique_ptr<Json::CharReader> reader(readerBuilder.newCharReader());
    if (!reader->parse(msg.c_str(), msg.c_str() + msg.size(), &root, &errors)) {
        LOG_L(L_WARNING, "Parse local comm recv msg. Error msg = %s", errors.c_str());
        return false;
    }
    JsonToLuaTable(L, root);
    return true;
}

void LuaCommCentral::JsonToLuaTable(lua_State* L, const Json::Value& value)
{
    
    if (value.isObject()) {
        lua_newtable(L);
        for (const std::string& key : value.getMemberNames()) {
            lua_pushstring(L, key.c_str());  
            JsonToLuaTable(L, value[key]);   
            lua_settable(L, -3);             
        }
    }
    else if (value.isArray()) {
        lua_newtable(L);
        for (Json::ArrayIndex i = 0; i < value.size(); ++i) {
            lua_pushnumber(L, i + 1);       
            JsonToLuaTable(L, value[i]);    
            lua_settable(L, -3);            
        }
    }
    
    else if (value.isString()) {
        lua_pushstring(L, value.asCString());
    }
    else if (value.isBool()) {
        lua_pushboolean(L, value.asBool());
    }
    else if (value.isInt()) {
        lua_pushinteger(L, value.asInt());
    }
    else if (value.isUInt()) {
        lua_pushinteger(L, value.asUInt());
    }
    else if (value.isDouble()) {
        lua_pushnumber(L, value.asDouble());
    }
    else {
        lua_pushnil(L);  
    }
}
