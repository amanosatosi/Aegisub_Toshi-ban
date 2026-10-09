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
        -- A fresh, offline portable install must still load the legacy FFI
        -- helper required by clipper2/ILL without running DepCtrl's updater.
        local oldffi = require('requireffi.requireffi')
        assert(type(oldffi) == 'table' and oldffi.version == '0.1.2')
        assert(getmetatable(oldffi) and type(getmetatable(oldffi).__call) == 'function')
        assert(dc.version:getVersionString() == '0.9.0')
        local schema = require('l0.DependencyControl.config-schema')
        local legacy = {config = {updaterEnabled = false, updateInterval = 98765}, marker = 'user state'}
        assert(schema.migration.migrate(legacy, nil,
            'https://raw.githubusercontent.com/TypesettingTools/DependencyControl/publish/schemas/config/v0.9.0.json'))
        assert(legacy.config.updates.mode == 'off')
        assert(legacy.config.updates.checkInterval == 98765)
        assert(legacy.marker == 'user state')
        -- The portable build includes native legacy modules in addition to
        -- DependencyControl's built-in replacements. Real modules must take
        -- precedence over the fallback aliases when both are installed.
        local moduleProvider = require('l0.DependencyControl.ModuleProvider')
        local legacyModules = {
            ['BM.BadMutex'] = {
                fallback = 'l0.DependencyControl.shims.BadMutex',
                methods = {'tryLock', 'lock', 'unlock'}
            },
            ['PT.PreciseTimer'] = {
                fallback = 'l0.DependencyControl.shims.PreciseTimer',
                methods = {'timeElapsed', 'sleep'}
            },
            ['DM.DownloadManager'] = {
                fallback = 'l0.DependencyControl.shims.DownloadManager',
                methods = {'addDownload', 'waitForFinish', 'clear'}
            }
        }
        for alias, spec in pairs(legacyModules) do
            assert(moduleProvider:getProvider(alias) == spec.fallback,
                alias .. ' must have a registered built-in fallback')
            local bundled = require(alias)
            local fallback = require(spec.fallback)
            assert(type(bundled) == 'table', alias .. ' must load from the portable package')
            assert(type(fallback) == 'table', spec.fallback .. ' must remain loadable')
            assert(bundled ~= fallback,
                alias .. ' must prefer the packaged legacy module, not its fallback')
            for _, method in ipairs(spec.methods) do
                assert(type(bundled[method]) == 'function',
                    alias .. ' is missing compatibility method ' .. method)
            end
        end
    )";
	ASSERT_EQ(0, luaL_dostring(L, program)) << lua_tostring(L, -1);
}
