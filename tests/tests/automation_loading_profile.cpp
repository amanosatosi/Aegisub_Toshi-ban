// Finite, trusted fixtures only: JIT-enabled samples are a performance control,
// never an option for loading arbitrary application scripts without cancellation.
#include "auto4_lua_init.h"
#include <libaegisub/fs.h>
#include <libaegisub/lua/modules.h>
#include <libaegisub/lua/script_reader.h>
#include <gtest/gtest.h>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <thread>

TEST(AutomationLoadingProfile, FiniteInterpretedAndJitInitializers) {
	if (!std::getenv("AEGISUB_LOAD_BENCH")) return;
	auto include = std::getenv("AEGISUB_AUTOMATION_INCLUDE_DIR");
	ASSERT_NE(include, nullptr);
	for (bool moon : {false, true}) for (int mode : {0, 1, 2, 3, 4}) for (int sample = 0; sample < 4; ++sample) {
		auto start = std::chrono::steady_clock::now();
		std::atomic<bool> cancelled{false};
		std::unique_ptr<lua_State, decltype(&lua_close)> state(luaL_newstate(), lua_close);
		auto L = state.get();
		agi::lua::preload_modules(L);
		bool guarded = mode == 0 || mode >= 3;
		if (guarded) Automation4::SetInitCancellation(L, &cancelled);
		if (mode >= 3) lua_sethook(L, Automation4::InitCancellationHook, LUA_MASKCOUNT, mode == 3 ? 100000 : 1000000);
		if (mode == 1) luaJIT_setmode(L, 0, LUAJIT_MODE_ENGINE | LUAJIT_MODE_OFF);
		ASSERT_TRUE(agi::lua::Install(L, {include})) << lua_tostring(L, -1);
		auto setup = std::chrono::steady_clock::now();
		char const *lua = "local n=0; for i=1,4000000 do n=n+i%97 end; return n";
		if (moon) {
			lua_getglobal(L, "require"); lua_pushstring(L, "moonscript"); lua_call(L, 1, 1);
			lua_getfield(L, -1, "loadstring"); lua_remove(L, -2);
			lua_pushstring(L, "n = 0\nfor i = 1, 4000000\n  n += i % 97\nreturn n");
			ASSERT_EQ(lua_pcall(L, 1, 1, 0), 0) << lua_tostring(L, -1);
		}
		else { ASSERT_EQ(luaL_loadstring(L, lua), 0); }
		auto parsed = std::chrono::steady_clock::now();
		ASSERT_EQ(lua_pcall(L, 0, 1, 0), 0) << lua_tostring(L, -1);
		EXPECT_GT(lua_tonumber(L, -1), 0);
		auto finished = std::chrono::steady_clock::now();
		if (guarded) Automation4::ClearInitCancellation(L);
		if (sample) std::cout << "AUTOMATION_JIT_SAMPLE," << (moon ? "moon" : "lua") << ','
			<< (mode == 0 ? "interpreted" : mode == 1 ? "interpreted_control" : mode == 2 ? "jit_control" : mode == 3 ? "hook_100000" : "hook_1000000") << ','
			<< std::chrono::duration<double, std::milli>(setup-start).count() << ','
			<< std::chrono::duration<double, std::milli>(parsed-setup).count() << ','
			<< std::chrono::duration<double, std::milli>(finished-parsed).count() << '\n';
	}
}

TEST(AutomationLoadingProfile, CancellationLatencyAtFiniteHookIntervals) {
	if (!std::getenv("AEGISUB_LOAD_BENCH")) return;
	for (int count : {10000, 100000, 1000000}) for (int sample = 0; sample < 3; ++sample) {
		std::atomic<bool> cancelled{false}, started{false};
		int result = 0;
		std::thread worker([&] {
			std::unique_ptr<lua_State, decltype(&lua_close)> state(luaL_newstate(), lua_close);
			auto L = state.get();
			luaL_openlibs(L);
			Automation4::SetInitCancellation(L, &cancelled);
			lua_sethook(L, Automation4::InitCancellationHook, LUA_MASKCOUNT, count);
			started = true;
			result = luaL_dostring(L, "jit.on(); local n=0; while true do n=n+1 end");
		});
		while (!started) std::this_thread::yield();
		std::this_thread::sleep_for(std::chrono::milliseconds(20));
		auto start = std::chrono::steady_clock::now();
		cancelled = true;
		worker.join();
		auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
		EXPECT_NE(result, 0);
		EXPECT_LT(elapsed, 1000);
		std::cout << "AUTOMATION_CANCEL_SAMPLE," << count << ',' << elapsed << '\n';
	}
}
