#include <windows.h>
#include <stdio.h>
#include "asi/asi.h"
#include "patch.h"

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call,
                      LPVOID lpReserved)
{
    switch (ul_reason_for_call)
    {
        case DLL_PROCESS_ATTACH:
        {
            if (!ASI::Init(hModule))
                return FALSE;
            OutputDebugStringA("-- UI Patch Loading --");
            init_call_hooks();
            test_hook();
            OutputDebugStringA("-- UI Patch Hooked --");
            break;
        }
        case DLL_PROCESS_DETACH:
            break;
    }
    return TRUE;
}

uint32_t g_menu_return_addr;
static void init_call_hooks()
{
    g_menu_return_addr = (ASI::AddrOf(0x70826));
}

void __attribute__((no_caller_saved_registers)) gui_load_something(uint32_t a, uint32_t b, char* c)
{
    char info[256];
    sprintf(info, "Testing");
    OutputDebugStringA(info);
}

static void __declspec(naked) test_hook_2()
{
    asm (
        "mov 0x4(%%ebp),%%eax      \n\t"
        "push %%eax                \n\t"
        "mov 0x8(%%ebp),%%eax      \n\t"
        "push %%eax                \n\t"
        "mov 0xc(%%ebp),%%eax      \n\t"
        "push %%eax                \n\t"
        "call %P0                  \n\t"
        "sub $0x248, %%esp         \n\t"
         "jmp *%1            \n\t" : : 
         "i" (gui_load_something),
         "o" (g_menu_return_addr));
}

static void test_hook()
{
    ASI::MemoryRegion gui_load_mreg (ASI::AddrOf(0x070820), 6);
    ASI::BeginRewrite(gui_load_mreg);
    *(unsigned char *)(ASI::AddrOf(0x070820)) = 0x90; //nop
    *(unsigned char *)(ASI::AddrOf(0x070821)) = 0xE9; //jmp
    *(int *)(ASI::AddrOf(0x070822)) = (int)(&test_hook_2) - ASI::AddrOf(0x070826);
    ASI::EndRewrite(gui_load_mreg);
}
