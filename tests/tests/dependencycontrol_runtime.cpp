#include <libaegisub/fs.h>
#include <libaegisub/lua/modules.h>
#include <libaegisub/lua/script_reader.h>
#include <lua.hpp>
#include <gtest/gtest.h>

#include <cstdlib>
#include <memory>

TEST(DependencyControlRuntime, BundledMoonScriptAndLinkedLuaJitMeetRequirements) {
	std::unique_ptr<lua_State, decltype(&lua_close)> state(luaL_newstate(), lua_close);
	ASSERT_NE(nullptr, state.get());
	auto L = state.get();
	agi::lua::preload_modules(L);
	auto include_dir = std::getenv("AEGISUB_AUTOMATION_INCLUDE_DIR");
	ASSERT_NE(nullptr, include_dir);
	ASSERT_TRUE(agi::lua::Install(L, {include_dir})) << lua_tostring(L, -1);
	const char program[] = R"(
        assert(jit and jit.version, 'DependencyControl needs LuaJIT')
        local visited = false
        pairs(setmetatable({}, {__pairs = function() visited = true; return function() end end}))
        assert(visited, 'LuaJIT must enable LUAJIT_ENABLE_LUA52COMPAT (__pairs)')
        local version = require('moonscript.version').version
        local major, minor = version:match('^(%d+)%.(%d+)')
        assert(tonumber(major) > 0 or tonumber(minor) >= 3, 'MoonScript >= 0.3.0 required')
        local fn, err = require('moonscript').loadstring('return 42')
        assert(fn, err)
        assert(fn() == 42)
    )";
	ASSERT_EQ(0, luaL_dostring(L, program)) << lua_tostring(L, -1);
}

TEST(DependencyControlRuntime, PackagedModuleLoadsWithBundledFallbacks) {
	auto include_dir = std::getenv("AEGISUB_DEPCTRL_INCLUDE_DIR");
	if (!include_dir) return; // Exercised explicitly after Windows packaging in CI.
	ASSERT_NE(nullptr, std::getenv("DEPCTRL_USER_DIR")); // An isolated CI profile.
	std::unique_ptr<lua_State, decltype(&lua_close)> state(luaL_newstate(), lua_close);
	ASSERT_NE(nullptr, state.get());
	auto L = state.get();
	agi::lua::preload_modules(L);
	ASSERT_TRUE(agi::lua::Install(L, {include_dir})) << lua_tostring(L, -1);
	const char program[] = R"(
        require('l0.AegisubShims')
        local dc = require('l0.DependencyControl')
        assert(dc.version:getVersionString() == '0.9.0')
        local schema = require('l0.DependencyControl.config-schema')
        local legacy = {config = {updaterEnabled = false, updateInterval = 98765}, marker = 'user state'}
        assert(schema.migration.migrate(legacy, nil,
            'https://raw.githubusercontent.com/TypesettingTools/DependencyControl/publish/schemas/config/v0.9.0.json'))
        assert(legacy.config.updates.mode == 'off')
        assert(legacy.config.updates.checkInterval == 98765)
        assert(legacy.marker == 'user state')
        for alias, provider in pairs({
            ['BM.BadMutex'] = 'l0.DependencyControl.shims.BadMutex',
            ['PT.PreciseTimer'] = 'l0.DependencyControl.shims.PreciseTimer',
            ['DM.DownloadManager'] = 'l0.DependencyControl.shims.DownloadManager'
        }) do
            assert(require(alias) == require(provider), alias .. ' must use the bundled fallback')
        end
    )";
	ASSERT_EQ(0, luaL_dostring(L, program)) << lua_tostring(L, -1);
}
