#pragma once
#include <cstdint>


extern uint32_t g_menu_return_addr;
static void test_hook();
static void init_call_hooks();
static void __declspec(naked) test_hook_2();
void __attribute__((no_caller_saved_registers)) gui_load_something(uint32_t a, uint32_t b, char* c);