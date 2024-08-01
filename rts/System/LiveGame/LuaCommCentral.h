#include "CommCentral.h"
#include <optional>
#include <json/json.h>

struct lua_State;

extern CCommCentral commCentral;

class LuaCommCentral {
public:
	static int Init(lua_State* L);
	static int Release(lua_State* L);
	static int Send(lua_State* L);

	static void Tick();

	static Json::Value LuaTable2JsonObj(lua_State* L, int index);
	static std::string LuaTable2JsonStr(lua_State* L, int index);
};