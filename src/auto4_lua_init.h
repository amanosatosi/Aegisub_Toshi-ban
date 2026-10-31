// Copyright (c) 2026, Aegisub Project. Distributed under the ISC license.
#pragma once

#include <atomic>
#include <initializer_list>
#include <lua.hpp>
#include <luajit.h>

namespace Automation4 {
inline bool InitCancellationActive(lua_State *L) {
	lua_getfield(L, LUA_REGISTRYINDEX, "aegisub_init_cancelled");
	bool active = !lua_isnil(L, -1);
	lua_pop(L, 1);
	return active;
}

// Remember global jit.on/off requests while initialization is interpreted.
// Saved aliases delegate to their original functions once initialization ends.
inline int InitJitMode(lua_State *L) {
	int args = lua_gettop(L);
	if (InitCancellationActive(L) && (!args || lua_isnil(L, 1))) {
		lua_pushvalue(L, lua_upvalueindex(2));
		lua_setfield(L, LUA_REGISTRYINDEX, "aegisub_init_jit_enabled");
		return 0;
	}
	lua_pushvalue(L, lua_upvalueindex(1));
	lua_insert(L, 1);
	lua_call(L, args, LUA_MULTRET);
	return lua_gettop(L);
}

inline int InitJitStatus(lua_State *L) {
	int args = lua_gettop(L);
	lua_pushvalue(L, lua_upvalueindex(1));
	lua_insert(L, 1);
	lua_call(L, args, LUA_MULTRET);
	if (InitCancellationActive(L)) {
		lua_getfield(L, LUA_REGISTRYINDEX, "aegisub_init_jit_enabled");
		lua_replace(L, 1);
	}
	return lua_gettop(L);
}

inline void InitCancellationHook(lua_State *state, lua_Debug *) {
	lua_getfield(state, LUA_REGISTRYINDEX, "aegisub_init_cancelled");
	auto cancel = static_cast<std::atomic<bool> *>(lua_touserdata(state, -1));
	lua_pop(state, 1);
	if (cancel && cancel->load()) luaL_error(state, "Automation loading cancelled");
}

// Compiled traces ignore hooks (https://luajit.org/faq.html). Install this after
// opening standard libraries, before executing any modules in the fresh state.
inline void SetInitCancellation(lua_State *L, std::atomic<bool> *cancelled) {
	lua_pushlightuserdata(L, cancelled);
	lua_setfield(L, LUA_REGISTRYINDEX, "aegisub_init_cancelled");
	lua_getglobal(L, "jit");
	lua_getfield(L, -1, "status");
	lua_call(L, 0, 1);
	lua_setfield(L, LUA_REGISTRYINDEX, "aegisub_init_jit_enabled");
	lua_newtable(L);
	lua_pushvalue(L, -2);
	lua_setfield(L, -2, "table");
	for (auto name : {"on", "off", "status"}) {
		lua_getfield(L, -2, name);
		lua_pushvalue(L, -1);
		lua_setfield(L, -3, name);
		if (name[0] == 's') lua_pushcclosure(L, InitJitStatus, 1);
		else {
			lua_pushboolean(L, name[1] == 'n');
			lua_pushcclosure(L, InitJitMode, 2);
		}
		lua_setfield(L, -3, name);
	}
	lua_setfield(L, LUA_REGISTRYINDEX, "aegisub_init_jit_original");
	lua_pop(L, 1);
	luaJIT_setmode(L, 0, LUAJIT_MODE_ENGINE | LUAJIT_MODE_OFF);
	lua_sethook(L, InitCancellationHook, LUA_MASKCOUNT, 10000);
}

inline void ClearInitCancellation(lua_State *L) {
	if (lua_gethook(L) == InitCancellationHook) lua_sethook(L, nullptr, 0, 0);
	lua_getfield(L, LUA_REGISTRYINDEX, "aegisub_init_jit_original");
	lua_getfield(L, -1, "table");
	lua_insert(L, -2);
	for (auto name : {"on", "off", "status"}) {
		lua_getfield(L, -2, name);
		auto function = lua_tocfunction(L, -1);
		lua_pop(L, 1);
		if (function == InitJitMode || function == InitJitStatus) {
			lua_getfield(L, -1, name);
			lua_setfield(L, -3, name);
		}
	}
	lua_pop(L, 2);
	lua_getfield(L, LUA_REGISTRYINDEX, "aegisub_init_jit_enabled");
	bool enabled = lua_toboolean(L, -1);
	lua_pop(L, 1);
	for (auto key : {"aegisub_init_cancelled", "aegisub_init_jit_original", "aegisub_init_jit_enabled"}) {
		lua_pushnil(L);
		lua_setfield(L, LUA_REGISTRYINDEX, key);
	}
	luaJIT_setmode(L, 0, LUAJIT_MODE_ENGINE | (enabled ? LUAJIT_MODE_ON : LUAJIT_MODE_OFF));
}
}
