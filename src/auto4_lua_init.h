// Copyright (c) 2026, Aegisub Project. Distributed under the ISC license.
#pragma once

#include <atomic>
#include <lua.hpp>

namespace Automation4 {
// LuaJIT aborts trace recording when it calls a line hook. Install this before
// any Lua modules run in the fresh state, so no compiled traces can bypass the
// cancellation count hook. This preserves scripts' explicit jit.on/off choices.
// See LuaJIT/src/lj_dispatch.c: callhook() and lj_dispatch_ins().
inline void InitCancellationHook(lua_State *state, lua_Debug *event) {
	if (event->event != LUA_HOOKCOUNT) return;
	lua_getfield(state, LUA_REGISTRYINDEX, "aegisub_init_cancelled");
	auto cancel = static_cast<std::atomic<bool> *>(lua_touserdata(state, -1));
	lua_pop(state, 1);
	if (cancel && cancel->load()) luaL_error(state, "Automation loading cancelled");
}

inline void SetInitCancellation(lua_State *L, std::atomic<bool> *cancelled) {
	lua_pushlightuserdata(L, cancelled);
	lua_setfield(L, LUA_REGISTRYINDEX, "aegisub_init_cancelled");
	lua_sethook(L, InitCancellationHook, LUA_MASKLINE | LUA_MASKCOUNT, 10000);
}

inline void ClearInitCancellation(lua_State *L) {
	if (lua_gethook(L) == InitCancellationHook) lua_sethook(L, nullptr, 0, 0);
	lua_pushnil(L);
	lua_setfield(L, LUA_REGISTRYINDEX, "aegisub_init_cancelled");
}
}
