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
            test_hook();
            break;
        }
        case DLL_PROCESS_DETACH:
            break;
    }
    return TRUE;
}

static void init_call_hooks()
{
    
}

void __thiscall gui_load_something(uint32_t unknown_flag_1,uint32_t unknown_flag_2,char *gui_data_loc_maybe)
{
    OutputDebugStringA("-- UI Patch Hooked --");
    char* info = new char[123];
    sprintf(info, "Flag 1 %d, Flag 2 %d\nStr:\n%s", unknown_flag_1, unknown_flag_2, gui_data_loc_maybe);
    OutputDebugStringA(info);
}

static void test_hook()
{
    ASI::MemoryRegion gui_load_mreg (ASI::AddrOf(0x070820), 5);
    ASI::BeginRewrite(gui_load_mreg);
    *(unsigned char *)(ASI::AddrOf(0x070820)) = 0xE9;
    *(int *)(ASI::AddrOf(0x070821)) = (int)(&gui_load_something) - ASI::AddrOf(0x070825);
    ASI::EndRewrite(gui_load_mreg);
}