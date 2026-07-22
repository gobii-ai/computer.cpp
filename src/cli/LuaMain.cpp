#include <cstdio>

extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

namespace {

void PrintLuaError(lua_State* state) {
    const char* message = lua_tostring(state, -1);
    std::fprintf(stderr, "%s\n", message ? message : "unknown Lua error");
}

void SetArgTable(lua_State* state, int argc, char** argv) {
    lua_createtable(state, argc > 2 ? argc - 2 : 0, 2);
    for (int index = 0; index < argc; ++index) {
        lua_pushstring(state, argv[index]);
        // Match the standalone interpreter: arg[0] is the script and arg[1]
        // is its first argument. arg[-1] remains the interpreter path.
        lua_rawseti(state, -2, index - 1);
    }
    lua_setglobal(state, "arg");
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: lua <script> [args...]\n");
        return 2;
    }

    lua_State* state = luaL_newstate();
    if (!state) {
        std::fprintf(stderr, "failed to create Lua state\n");
        return 1;
    }
    luaL_openlibs(state);
    SetArgTable(state, argc, argv);

    if (luaL_loadfile(state, argv[1]) != LUA_OK) {
        PrintLuaError(state);
        lua_close(state);
        return 1;
    }
    if (lua_pcall(state, 0, LUA_MULTRET, 0) != LUA_OK) {
        PrintLuaError(state);
        lua_close(state);
        return 1;
    }

    lua_close(state);
    return 0;
}
