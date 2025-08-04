#include <windows.h>
#include <cstdio>
#include <d3d8.h>

bool PatchMemory(void* address, size_t size) {
    DWORD oldProtect;
    
    if (!VirtualProtect(address, size, PAGE_EXECUTE_READWRITE, &oldProtect)) {
        return false;
    }
    
    memset(address, 0x90, size);
    
    DWORD dummy;
    VirtualProtect(address, size, oldProtect, &dummy);
    
    return true;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        OutputDebugStringA("UI Patch: Loading...\n");
        
        if (PatchMemory((void*)0x477152, 2)) {
            OutputDebugStringA("UI Patch: Loaded successfully\n");
        } else {
            OutputDebugStringA("UI Patch: Failed to load\n");
        }
    }
    
    return TRUE;
}