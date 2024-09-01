#include "CommCentral.h"
#include <optional>
#include <json/json.h>

struct lua_State;

extern CCommCentral commCentral;

class LuaCommCentral {
public:
	static bool PushEntries(lua_State* L);

	static int InitLMCommCentral(lua_State* L);
	static int ReleaseLMCommCentral(lua_State* L);
	static int SendLocalMemMsg(lua_State* L);

	static inline void Tick();
	static int TickLMCommCentral(lua_State* L);

	static Json::Value LuaTable2JsonObj(lua_State* L, int index);
	static std::string LuaTable2JsonStr(lua_State* L, int index);

	static bool Str2LuaTableAndPush(lua_State* L, const std::string& msg);
	static void JsonToLuaTable(lua_State* L, const Json::Value& value);
};