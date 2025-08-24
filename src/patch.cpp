#include <windows.h>
#include <stdio.h>
#include <d3d8.h>
#include <string>
#include <vector>
#include "asi/asi.h"
#include "patch.h"
#include "logger.h"
#include "d3d8Validation.h"

extern "C" {
    #include "MinHook.h"
}

// Constants
#define IDirect3D8_PTR_ADDR       0x6B5DDC
#define IDirect3D8_UNKNOWN_PTR    0x14ce740
#define IDirect3DDevice8_PTR_ADDR 0x6B5DE0

// Global variables
IDirect3DDevice8* g_pDevice = nullptr;
uint32_t g_menu_return_addr = 0;

typedef HRESULT(WINAPI* CreateDevice_t)(IDirect3D8*, UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS*, IDirect3DDevice8**);
typedef HRESULT(WINAPI* CreateImageSurface_t)(IDirect3DDevice8*, UINT, UINT, D3DFORMAT, IDirect3DSurface8**);
typedef HRESULT(WINAPI* EnumAdapterModes_t)(UINT, UINT, D3DDISPLAYMODE *);

CreateDevice_t oCreateDevice = nullptr;
CreateImageSurface_t oCreateImageSurface = nullptr;
EnumAdapterModes_t oEnumAdapterModes = nullptr;
D3DCAPS8 caps;

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

HRESULT WINAPI hkEnumAdapterModes(THIS_ UINT Adapter, UINT Mode, D3DDISPLAYMODE * pMode)
{

    HRESULT result = oEnumAdapterModes(Adapter, Mode, pMode);
    WriteLogf("EnumAdapterModes", "Enum Mode Data: %x | %x | %x", Adapter, Mode, pMode);
    return result;
}

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
    
    WriteLog("CreateDevice", "Called into IDirect3D8::CreateDevice");
    Validate();
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

struct HookInfo {
    const char* name;
    int vtable_index;
    void* hook_function;
    void** original_function;
};

bool SetupHook(const HookInfo& info) {
    WriteLogfSimple("[+] Attempting to hook IDirect3DDevice8::%s", info.name);
    
    IDirect3DDevice8* device = GetGameDevice();
    if (!device) {
        WriteLogfSimple("[-] Failed to get game device pointer");
        return false;
    }

    void** vtable = *reinterpret_cast<void***>(device);
    if (!vtable) {
        WriteLogfSimple("[-] Device vtable pointer is null");
        return false;
    }

    void* method_ptr = vtable[info.vtable_index];
    if (!method_ptr) {
        WriteLogfSimple("[-] %s method pointer is null", info.name);
        return false;
    }

    WriteLogfSimple("[+] Device and %s method pointer are valid", info.name);

    if (MH_CreateHook(method_ptr, info.hook_function, info.original_function) != MH_OK) {
        WriteLogfSimple("[-] MH_CreateHook failed for %s", info.name);
        return false;
    }

    if (MH_EnableHook(method_ptr) != MH_OK) {
        WriteLogfSimple("[-] MH_EnableHook failed for %s", info.name);
        return false;
    }

    WriteLogfSimple("[+] Successfully hooked IDirect3DDevice8::%s", info.name);
    return true;
}

bool HookCreateDevice() {
    WriteLogSimple("[+] Attempting to hook IDirect3D8::CreateDevice");
    
    IDirect3D8* pD3D = *reinterpret_cast<IDirect3D8**>(IDirect3D8_PTR_ADDR);
    if (!pD3D) {
        WriteLogSimple("[-] Failed to get IDirect3D8 pointer");
        return false;
    }

    void** vtable = *reinterpret_cast<void***>(pD3D);
    if (!vtable) {
        WriteLogSimple("[-] IDirect3D8 vtable pointer is null");
        return false;
    }

    void* pCreateDevice = vtable[15];
    if (!pCreateDevice) {
        WriteLogSimple("[-] CreateDevice method pointer is null");
        return false;
    }

    if (MH_Initialize() != MH_OK) {
        WriteLogSimple("[-] MH_Initialize failed");
        return false;
    }

    if (MH_CreateHook(pCreateDevice, reinterpret_cast<LPVOID>(hkCreateDevice), 
                     reinterpret_cast<void**>(&oCreateDevice)) != MH_OK) {
        WriteLogSimple("[-] MH_CreateHook failed for CreateDevice");
        return false;
    }

    if (MH_EnableHook(pCreateDevice) != MH_OK) {
        WriteLogSimple("[-] MH_EnableHook failed for CreateDevice");
        return false;
    }

    void* pEnumDeviceModes = vtable[7];
    if(MH_CreateHook(pEnumDeviceModes, reinterpret_cast<LPVOID>(hkEnumAdapterModes),
        reinterpret_cast<void**>(&oEnumAdapterModes)) != MH_OK)
    {
        WriteLogSimple("[-], MH_EnableHook failed for EnumDeviceModes");
        return false;
    } 

    WriteLogSimple("[+] Successfully hooked IDirect3D8::EnumDeviceModes");
    WriteLogSimple("[+] Successfully hooked IDirect3D8::CreateDevice");
    return true;
}

typedef HRESULT(WINAPI* TestCooperativeLevel_t)();
TestCooperativeLevel_t oTestCooperativeLevel = nullptr;

HRESULT WINAPI hkTestCooperativeLevel()
{
    HRESULT result = oTestCooperativeLevel();
    WriteLogf("TestCooperativeLevel", "Called into Function returning: [%s]", result == D3D_OK ? "True" : "False");
    return result;
}

bool SetupDeviceHooks() {
    HookInfo hooks[] = {{"CreateImageSurface", 27, reinterpret_cast<void*>(hkCreateImageSurface), reinterpret_cast<void**>(&oCreateImageSurface)},
                        {"TestCooperativeLevel", 3, reinterpret_cast<void*>(hkTestCooperativeLevel), reinterpret_cast<void**>(&oTestCooperativeLevel)}};

    bool all_success = true;
    for (const auto& hook : hooks) {
        if (!SetupHook(hook)) {
            all_success = false;
        }
    }

    return all_success;
}

void InitializeHooks() {
    WriteLogSimple("=== D3D8 Hook Initialization Started ===");
    
    if (!HookCreateDevice()) {
        WriteLogSimple("[-] Failed to hook CreateDevice");
        return;
    }
    
    IDirect3DDevice8* device = *reinterpret_cast<IDirect3DDevice8**>(IDirect3DDevice8_PTR_ADDR);
    if (device) {
        g_pDevice = device;
        WriteLogSimple("[+] Found existing device pointer");
    }
    
    if (g_pDevice) {
        SetupDeviceHooks();
    } else {
        WriteLogSimple("[!] Device not available yet, hooks will be set up after device creation");
    }
    
    WriteLogSimple("=== D3D8 Hook Initialization Complete ===");
}

void Cleanup() {
    WriteLogSimple("=== Cleaning up hooks ===");
    MH_DisableHook(MH_ALL_HOOKS);
    MH_Uninitialize();
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
        case DLL_PROCESS_ATTACH:
            if (!ASI::Init(hModule)) {
                return FALSE;
            }
            InitializeLogger();
            WriteLogSimple("=== UI Patch Loading ===");
            InitializeHooks();
            WriteLogSimple("=== UI Patch Loaded ===");
            break;
            
        case DLL_PROCESS_DETACH:
            Cleanup();
            break;
    }
    return TRUE;
}