#include "auto4_lua_init.h"

#include <libaegisub/fs.h>
#include <libaegisub/lua/modules.h>
#include <libaegisub/lua/script_reader.h>
#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <memory>
#include <thread>

TEST(AutomationInitialization, CancellationInterruptsAnInfiniteLuaLoop) {
	std::atomic<bool> cancelled{false};
	std::atomic<bool> started{false};
	int result = 0;
	std::thread worker([&] {
		std::unique_ptr<lua_State, decltype(&lua_close)> state(luaL_newstate(), lua_close);
		Automation4::SetInitCancellation(state.get(), &cancelled);
		started = true;
		luaL_openlibs(state.get());
		result = luaL_dostring(state.get(), "jit.on(); while true do end");
	});
	while (!started) std::this_thread::yield();
	cancelled = true;
	worker.join();
	EXPECT_NE(result, 0);
}

TEST(AutomationInitialization, LuaAndMoonScriptWorkAndHookIsRemovedAfterInitialization) {
	std::unique_ptr<lua_State, decltype(&lua_close)> state(luaL_newstate(), lua_close);
	auto L = state.get();
	agi::lua::preload_modules(L);
	std::atomic<bool> cancelled{false};
	Automation4::SetInitCancellation(L, &cancelled);
	auto include = std::getenv("AEGISUB_AUTOMATION_INCLUDE_DIR");
	ASSERT_NE(include, nullptr);
	ASSERT_TRUE(agi::lua::Install(L, {include})) << lua_tostring(L, -1);
	ASSERT_EQ(luaL_dostring(L, "return require('moonscript').loadstring('return 42')()"), 0) << lua_tostring(L, -1);
	EXPECT_EQ(lua_tointeger(L, -1), 42);
	lua_pop(L, 1);
	EXPECT_NE(luaL_dostring(L, "error('invalid script')"), 0);
	lua_pop(L, 1);
	EXPECT_EQ(luaL_dostring(L, "return 21 * 2"), 0);
	EXPECT_EQ(lua_tointeger(L, -1), 42);
	Automation4::ClearInitCancellation(L);
	cancelled = true;
	EXPECT_EQ(lua_gethook(L), nullptr);
	EXPECT_EQ(luaL_dostring(L, "local x = 0; for i = 1, 20000 do x = x + 1 end; assert(x == 20000)"), 0);
}

TEST(AutomationInitialization, ExplicitJitPreferenceSurvivesPublication) {
	std::atomic<bool> cancelled{false};
	std::unique_ptr<lua_State, decltype(&lua_close)> state(luaL_newstate(), lua_close);
	auto L = state.get();
	luaL_openlibs(L);
	Automation4::SetInitCancellation(L, &cancelled);
	ASSERT_EQ(luaL_dostring(L, "jit.off()"), 0);
	Automation4::ClearInitCancellation(L);
	ASSERT_EQ(luaL_dostring(L, "assert(not jit.status())"), 0);
}
