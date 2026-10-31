// Copyright (c) 2026, Aegisub Project. Distributed under the ISC license.
#pragma once

#include <atomic>
#include <lua.hpp>
#include <luajit.h>

namespace Automation4 {
// LuaJIT traces can bypass instruction hooks. Disable tracing only during
// initialization, then remove the hook before handing the state to the UI.
inline void SetInitCancellation(lua_State *L, std::atomic<bool> *cancelled) {
	lua_pushlightuserdata(L, cancelled);
	lua_setfield(L, LUA_REGISTRYINDEX, "aegisub_init_cancelled");
	luaJIT_setmode(L, 0, LUAJIT_MODE_ENGINE | LUAJIT_MODE_OFF);
	lua_sethook(L, [](lua_State *state, lua_Debug *) {
		lua_getfield(state, LUA_REGISTRYINDEX, "aegisub_init_cancelled");
		auto cancel = static_cast<std::atomic<bool> *>(lua_touserdata(state, -1));
		lua_pop(state, 1);
		if (cancel && cancel->load()) luaL_error(state, "Automation loading cancelled");
	}, LUA_MASKCOUNT, 10000);
}

inline void ClearInitCancellation(lua_State *L) {
	lua_sethook(L, nullptr, 0, 0);
	lua_pushnil(L);
	lua_setfield(L, LUA_REGISTRYINDEX, "aegisub_init_cancelled");
	luaJIT_setmode(L, 0, LUAJIT_MODE_ENGINE | LUAJIT_MODE_ON);
}
}
