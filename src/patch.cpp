#include <windows.h>
#include <stdio.h>
#include <d3d8.h>
#include <fstream>
#include <string>
#include <mutex>
#include "asi/asi.h"
#include "patch.h"

extern "C" {
    #include "MinHook.h"
}

// Constants
#define IDirect3D8_PTR_ADDR 0x6B5DDC
#define IDirect3DDevice8_PTR_ADDR 0x6B5DE0

// Global variables
IDirect3DDevice8* g_pDevice = nullptr;
uint32_t g_menu_return_addr = 0;
std::mutex g_log_mutex;
std::ofstream g_log_file;

// Function pointer typedefs
typedef HRESULT(WINAPI* CreateDevice_t)(IDirect3D8*, UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS*, IDirect3DDevice8**);
typedef HRESULT(WINAPI* CreateImageSurface_t)(IDirect3DDevice8*, UINT, UINT, D3DFORMAT, IDirect3DSurface8**);

CreateDevice_t oCreateDevice = nullptr;
CreateImageSurface_t oCreateImageSurface = nullptr;

IDirect3DDevice8* GetGameDevice() {
    return g_pDevice;
}

unsigned int nextPowerOfTwo(unsigned int x) {
    if (x == 0) return 1;
    x--;
    x |= x >> 1;
    x |= x >> 2;
    x |= x >> 4;
    x |= x >> 8;
    x |= x >> 16;
    return x + 1;
}

bool isPowerOfTwo(unsigned int x) {
    return x != 0 && (x & (x - 1)) == 0;
}

// Hook implementations
HRESULT WINAPI hkCreateDevice(IDirect3D8* pD3D, UINT Adapter, D3DDEVTYPE DeviceType, 
                             HWND hFocusWindow, DWORD BehaviorFlags, 
                             D3DPRESENT_PARAMETERS* pPresentationParameters,
                             IDirect3DDevice8** ppReturnedDeviceInterface) {
    HRESULT result = oCreateDevice(pD3D, Adapter, DeviceType, hFocusWindow, 
                                  BehaviorFlags, pPresentationParameters, ppReturnedDeviceInterface);
    
    if (SUCCEEDED(result) && ppReturnedDeviceInterface && *ppReturnedDeviceInterface) {
        if (oCreateImageSurface == nullptr) {
            g_pDevice = *ppReturnedDeviceInterface;
        }
    }
    
    return result;
}

HRESULT WINAPI hkCreateImageSurface(IDirect3DDevice8* pDevice, UINT Width, UINT Height,
                                   D3DFORMAT Format, IDirect3DSurface8** ppSurface) {
                                    
    UINT nextWidth = Width; 
    UINT nextHeight = Height;

    if (!isPowerOfTwo(Width)) {
        nextWidth = nextPowerOfTwo(Width);
    }

    if (!isPowerOfTwo(Height)) {
        nextHeight = nextPowerOfTwo(Height);
    }
    
    return oCreateImageSurface(pDevice, nextWidth, nextHeight, Format, ppSurface);
}

void WriteLog(const std::string& message) {    
    OutputDebugStringA(message.c_str());
}

void WriteLogf(const char* format, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    WriteLog(std::string(buffer));
}

struct HookInfo {
    const char* name;
    int vtable_index;
    void* hook_function;
    void** original_function;
};

bool SetupHook(const HookInfo& info) {
    WriteLogf("[+] Attempting to hook IDirect3DDevice8::%s", info.name);
    
    IDirect3DDevice8* device = GetGameDevice();
    if (!device) {
        WriteLog("[-] Failed to get game device pointer");
        return false;
    }

    void** vtable = *reinterpret_cast<void***>(device);
    if (!vtable) {
        WriteLog("[-] Device vtable pointer is null");
        return false;
    }

    void* method_ptr = vtable[info.vtable_index];
    if (!method_ptr) {
        WriteLogf("[-] %s method pointer is null", info.name);
        return false;
    }

    WriteLogf("[+] Device and %s method pointer are valid", info.name);

    if (MH_CreateHook(method_ptr, info.hook_function, info.original_function) != MH_OK) {
        WriteLogf("[-] MH_CreateHook failed for %s", info.name);
        return false;
    }

    if (MH_EnableHook(method_ptr) != MH_OK) {
        WriteLogf("[-] MH_EnableHook failed for %s", info.name);
        return false;
    }

    WriteLogf("[+] Successfully hooked IDirect3DDevice8::%s", info.name);
    return true;
}

bool HookCreateDevice() {
    WriteLog("[+] Attempting to hook IDirect3D8::CreateDevice");
    
    IDirect3D8* pD3D = *reinterpret_cast<IDirect3D8**>(IDirect3D8_PTR_ADDR);
    if (!pD3D) {
        WriteLog("[-] Failed to get IDirect3D8 pointer");
        return false;
    }

    void** vtable = *reinterpret_cast<void***>(pD3D);
    if (!vtable) {
        WriteLog("[-] IDirect3D8 vtable pointer is null");
        return false;
    }

    void* pCreateDevice = vtable[15];
    if (!pCreateDevice) {
        WriteLog("[-] CreateDevice method pointer is null");
        return false;
    }

    if (MH_Initialize() != MH_OK) {
        WriteLog("[-] MH_Initialize failed");
        return false;
    }

    if (MH_CreateHook(pCreateDevice, reinterpret_cast<LPVOID>(hkCreateDevice), 
                     reinterpret_cast<void**>(&oCreateDevice)) != MH_OK) {
        WriteLog("[-] MH_CreateHook failed for CreateDevice");
        return false;
    }

    if (MH_EnableHook(pCreateDevice) != MH_OK) {
        WriteLog("[-] MH_EnableHook failed for CreateDevice");
        return false;
    }

    WriteLog("[+] Successfully hooked IDirect3D8::CreateDevice");
    return true;
}

bool SetupDeviceHooks() {
    HookInfo hooks[] = {{"CreateImageSurface", 27, reinterpret_cast<void*>(hkCreateImageSurface), reinterpret_cast<void**>(&oCreateImageSurface)}};

    bool all_success = true;
    for (const auto& hook : hooks) {
        if (!SetupHook(hook)) {
            all_success = false;
        }
    }

    return all_success;
}

void InitializeHooks() {
    WriteLog("=== D3D8 Hook Initialization Started ===");
    
    if (!HookCreateDevice()) {
        WriteLog("[-] Failed to hook CreateDevice");
        return;
    }
    
    IDirect3DDevice8* device = *reinterpret_cast<IDirect3DDevice8**>(IDirect3DDevice8_PTR_ADDR);
    if (device) {
        g_pDevice = device;
        WriteLog("[+] Found existing device pointer");
    }
    
    if (g_pDevice) {
        SetupDeviceHooks();
    } else {
        WriteLog("[!] Device not available yet, hooks will be set up after device creation");
    }
    
    WriteLog("=== D3D8 Hook Initialization Complete ===");
}

void Cleanup() {
    WriteLog("=== Cleaning up hooks ===");
    MH_DisableHook(MH_ALL_HOOKS);
    MH_Uninitialize();
    
    if (g_log_file.is_open()) {
        g_log_file.close();
    }
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
        case DLL_PROCESS_ATTACH:
            if (!ASI::Init(hModule)) {
                return FALSE;
            }
            WriteLog("=== UI Patch Loading ===");
            InitializeHooks();
            WriteLog("=== UI Patch Loaded ===");
            break;
            
        case DLL_PROCESS_DETACH:
            Cleanup();
            break;
    }
    return TRUE;
}