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

int __cdecl gui_load_something(uint32_t a, uint32_t b, char* name)
{
    char info[512];
    sprintf(info, "Hooked into gui load.\nFlag 1:%x\nFlag 2:%x\nGUI name ptr:%p\nGUI name string:%s\n", 
            a, b, name, name ? name : "(null)");
    OutputDebugStringA(info);
    return 0;
}

static void __declspec(naked) test_hook_2()
{
    asm volatile(
        "movl 0xc(%%esp),%%eax    \n\t"   // Get 3rd param (gui_name)
        "pushl %%eax               \n\t"  // Push as 3rd arg
        "movl 0xc(%%esp),%%eax     \n\t"  // Get 2nd param (unknown_flag_2) - note offset changed due to previous push
        "pushl %%eax               \n\t"  // Push as 2nd arg  
        "movl 0xc(%%esp),%%eax     \n\t"  // Get 1st param (unknown_flag_1) - note offset changed due to previous push
        "pushl %%eax               \n\t"  // Push as 1st arg

        "call %P1                  \n\t"  // Call our hook function
        "addl $0xc, %%esp          \n\t"  // Clean up our 3 pushed parameters
        "subl $0x248, %%esp        \n\t"  // Original function's stack allocation
        "jmp *%0                   \n\t"  // Jump to continue original function
        : : 
        "o" (g_menu_return_addr),
        "i" (gui_load_something)
    );
}

static void test_hook()
{
    ASI::MemoryRegion gui_load_mreg (ASI::AddrOf(0x070820), 6);
    ASI::BeginRewrite(gui_load_mreg);
    *(unsigned char *)(ASI::AddrOf(0x070820)) = 0xE9; //jmp
    *(int *)(ASI::AddrOf(0x070821)) = (int)(&test_hook_2) - ASI::AddrOf(0x070825);
    *(unsigned char *)(ASI::AddrOf(0x070825)) = 0x90; //nop
    ASI::EndRewrite(gui_load_mreg);
}
