#pragma once
#include <cstdint>


extern uint32_t g_menu_return_addr;
static void test_hook();
static void init_call_hooks();
static void __declspec(naked) test_hook_2();
void __attribute__((no_caller_saved_registers, cdecl)) gui_load_something(uint32_t unknown_flag_1, uint32_t unknown_flag_2, char *gui_data_loc_maybe);