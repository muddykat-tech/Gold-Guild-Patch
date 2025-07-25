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

// Logging function
void WriteLog(const std::string& message) {
    std::lock_guard<std::mutex> lock(g_log_mutex);
    
    if (!g_log_file.is_open()) {
        g_log_file.open("d3d8_hook.log", std::ios::app);
    }
    
    if (g_log_file.is_open()) {
        // Get current time
        SYSTEMTIME st;
        GetLocalTime(&st);
        
        char timestamp[64];
        sprintf(timestamp, "[%02d:%02d:%02d.%03d] ", 
                st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
        
        g_log_file << timestamp << message << std::endl;
        g_log_file.flush();
    }
    
    // Also output to debug console
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

// Function pointer typedefs
typedef HRESULT(WINAPI* CreateDevice_t)(IDirect3D8*, UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS*, IDirect3DDevice8**);
typedef HRESULT(WINAPI* CreateTexture_t)(IDirect3DDevice8*, UINT, UINT, UINT, DWORD, D3DFORMAT, D3DPOOL, IDirect3DTexture8**);
typedef HRESULT(WINAPI* CreateImageSurface_t)(IDirect3DDevice8*, UINT, UINT, D3DFORMAT, IDirect3DSurface8**);
typedef HRESULT(WINAPI* Present_t)(IDirect3DDevice8*, CONST RECT*, CONST RECT*, HWND, CONST RGNDATA*);
typedef HRESULT(WINAPI* SetViewport_t)(IDirect3DDevice8*, CONST D3DVIEWPORT8*);
typedef HRESULT(WINAPI* SetRenderTarget_t)(IDirect3DDevice8*, IDirect3DSurface8*, IDirect3DSurface8*);
typedef HRESULT(WINAPI* SetClipPlane_t)(IDirect3DDevice8*, DWORD, CONST float*);
typedef HRESULT(WINAPI* SetTransform_t)(IDirect3DDevice8*, D3DTRANSFORMSTATETYPE, CONST D3DMATRIX*);

// Original function pointers
CreateDevice_t oCreateDevice = nullptr;
CreateTexture_t oCreateTexture = nullptr;
CreateImageSurface_t oCreateImageSurface = nullptr;
Present_t oPresent = nullptr;
SetViewport_t oSetViewport = nullptr;
SetRenderTarget_t oSetRenderTarget = nullptr;
SetClipPlane_t oSetClipPlane = nullptr;
SetTransform_t oSetTransform = nullptr;

// Helper function to get device pointer
IDirect3DDevice8* GetGameDevice() {
    return g_pDevice;
}

const char* D3DFormatToString(D3DFORMAT format) {
    switch (format) {
        case D3DFMT_UNKNOWN:              return "D3DFMT_UNKNOWN";
        case D3DFMT_R8G8B8:               return "D3DFMT_R8G8B8";
        case D3DFMT_A8R8G8B8:             return "D3DFMT_A8R8G8B8";
        case D3DFMT_X8R8G8B8:             return "D3DFMT_X8R8G8B8";
        case D3DFMT_R5G6B5:               return "D3DFMT_R5G6B5";
        case D3DFMT_X1R5G5B5:             return "D3DFMT_X1R5G5B5";
        case D3DFMT_A1R5G5B5:             return "D3DFMT_A1R5G5B5";
        case D3DFMT_A4R4G4B4:             return "D3DFMT_A4R4G4B4";
        case D3DFMT_R3G3B2:               return "D3DFMT_R3G3B2";
        case D3DFMT_A8:                   return "D3DFMT_A8";
        case D3DFMT_A8R3G3B2:             return "D3DFMT_A8R3G3B2";
        case D3DFMT_X4R4G4B4:             return "D3DFMT_X4R4G4B4";
        case D3DFMT_A2B10G10R10:          return "D3DFMT_A2B10G10R10";
        case D3DFMT_G16R16:               return "D3DFMT_G16R16";
        case D3DFMT_A8P8:                 return "D3DFMT_A8P8";
        case D3DFMT_P8:                   return "D3DFMT_P8";
        case D3DFMT_L8:                   return "D3DFMT_L8";
        case D3DFMT_A8L8:                 return "D3DFMT_A8L8";
        case D3DFMT_A4L4:                 return "D3DFMT_A4L4";
        case D3DFMT_V8U8:                 return "D3DFMT_V8U8";
        case D3DFMT_L6V5U5:               return "D3DFMT_L6V5U5";
        case D3DFMT_X8L8V8U8:             return "D3DFMT_X8L8V8U8";
        case D3DFMT_Q8W8V8U8:             return "D3DFMT_Q8W8V8U8";
        case D3DFMT_V16U16:               return "D3DFMT_V16U16";
        case D3DFMT_W11V11U10:            return "D3DFMT_W11V11U10";
        case D3DFMT_A2W10V10U10:          return "D3DFMT_A2W10V10U10";
        case D3DFMT_UYVY:                 return "D3DFMT_UYVY";
        case D3DFMT_YUY2:                 return "D3DFMT_YUY2";
        case D3DFMT_DXT1:                 return "D3DFMT_DXT1";
        case D3DFMT_DXT2:                 return "D3DFMT_DXT2";
        case D3DFMT_DXT3:                 return "D3DFMT_DXT3";
        case D3DFMT_DXT4:                 return "D3DFMT_DXT4";
        case D3DFMT_DXT5:                 return "D3DFMT_DXT5";
        case D3DFMT_D16_LOCKABLE:         return "D3DFMT_D16_LOCKABLE";
        case D3DFMT_D32:                  return "D3DFMT_D32";
        case D3DFMT_D15S1:                return "D3DFMT_D15S1";
        case D3DFMT_D24S8:                return "D3DFMT_D24S8";
        case D3DFMT_D16:                  return "D3DFMT_D16";
        case D3DFMT_D24X8:                return "D3DFMT_D24X8";
        case D3DFMT_D24X4S4:              return "D3DFMT_D24X4S4";
        case D3DFMT_VERTEXDATA:           return "D3DFMT_VERTEXDATA";
        case D3DFMT_INDEX16:              return "D3DFMT_INDEX16";
        case D3DFMT_INDEX32:              return "D3DFMT_INDEX32";
        case D3DFMT_FORCE_DWORD:          return "D3DFMT_FORCE_DWORD";
        default: {
            // For unknown formats, show the raw value
            static char buffer[32];
            sprintf(buffer, "UNKNOWN_FORMAT(0x%08X)", static_cast<DWORD>(format));
            return buffer;
        }
    }
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
        if (oCreateTexture == nullptr) {
            g_pDevice = *ppReturnedDeviceInterface;
            WriteLog("[+] Device created and stored");
        }
    }
    
    return result;
}

HRESULT WINAPI hkCreateTexture(IDirect3DDevice8* pDevice, UINT Width, UINT Height, UINT Levels,
                              DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DTexture8** ppTexture) {

    WriteLogf("[Hook] CreateTexture (%ux%u) Format: %s Usage: 0x%x", Width, Height, D3DFormatToString(Format), Usage);
    return oCreateTexture(pDevice, Width, Height, Levels, Usage, Format, Pool, ppTexture);
}

HRESULT WINAPI hkCreateImageSurface(IDirect3DDevice8* pDevice, UINT Width, UINT Height,
                                   D3DFORMAT Format, IDirect3DSurface8** ppSurface) {
    WriteLogf("[Hook] CreateImageSurface: %ux%u Format: %s", Width, Height, D3DFormatToString(Format));
    UINT nextWidth = Width; 
    UINT nextHeight = Height;

    if (!isPowerOfTwo(Width)) {
        nextWidth = nextPowerOfTwo(Width);
        WriteLogf("[Hook] CreateImageSurface - Scaling Width from %u to %u", Width, nextWidth);
    }

    if (!isPowerOfTwo(Height)) {
        nextHeight = nextPowerOfTwo(Height);
        WriteLogf("[Hook] CreateImageSurface - Scaling Height from %u to %u", Height, nextHeight);
    }
    
    return oCreateImageSurface(pDevice, nextWidth, nextHeight, Format, ppSurface);
}

HRESULT WINAPI hkPresent(IDirect3DDevice8* pDevice, CONST RECT* pSourceRect, CONST RECT* pDestRect,
                        HWND hDestWindowOverride, CONST RGNDATA* pDirtyRegion) {
    return oPresent(pDevice, pSourceRect, pDestRect, hDestWindowOverride, pDirtyRegion);
}

HRESULT WINAPI hkSetViewport(IDirect3DDevice8* pDevice, CONST D3DVIEWPORT8* pViewport) {
    return oSetViewport(pDevice, pViewport);
}

HRESULT WINAPI hkSetRenderTarget(IDirect3DDevice8* pDevice, IDirect3DSurface8* pRenderTarget,
                                IDirect3DSurface8* pNewZStencil) {
    if (pRenderTarget) {
        D3DSURFACE_DESC desc;
        if (SUCCEEDED(pRenderTarget->GetDesc(&desc))) {
            WriteLogf("[Hook] SetRenderTarget: %ux%u Format: %d", desc.Width, desc.Height, desc.Format);
        } else {
            WriteLog("[Hook] SetRenderTarget: Could not get surface description");
        }
    } else {
        WriteLog("[Hook] SetRenderTarget: NULL render target (back buffer)");
    }
    
    return oSetRenderTarget(pDevice, pRenderTarget, pNewZStencil);
}

HRESULT WINAPI hkSetClipPlane(IDirect3DDevice8* pDevice, DWORD Index, CONST float* pPlane) {
    if (pPlane) {
        WriteLogf("[Hook] SetClipPlane[%u]: (%.3f, %.3f, %.3f, %.3f)", 
                 Index, pPlane[0], pPlane[1], pPlane[2], pPlane[3]);
    }
    return oSetClipPlane(pDevice, Index, pPlane);
}

HRESULT WINAPI hkSetTransform(IDirect3DDevice8* pDevice, D3DTRANSFORMSTATETYPE State, CONST D3DMATRIX* pMatrix) {
    return oSetTransform(pDevice, State, pMatrix);
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
    // Define all device hooks
    HookInfo hooks[] = {
        {"CreateTexture", 20, reinterpret_cast<void*>(hkCreateTexture), reinterpret_cast<void**>(&oCreateTexture)},
        {"CreateImageSurface", 27, reinterpret_cast<void*>(hkCreateImageSurface), reinterpret_cast<void**>(&oCreateImageSurface)},
        {"Present", 15, reinterpret_cast<void*>(hkPresent), reinterpret_cast<void**>(&oPresent)},
        {"SetViewport", 40, reinterpret_cast<void*>(hkSetViewport), reinterpret_cast<void**>(&oSetViewport)},
        {"SetRenderTarget", 31, reinterpret_cast<void*>(hkSetRenderTarget), reinterpret_cast<void**>(&oSetRenderTarget)},
        {"SetClipPlane", 48, reinterpret_cast<void*>(hkSetClipPlane), reinterpret_cast<void**>(&oSetClipPlane)},
        {"SetTransform", 37, reinterpret_cast<void*>(hkSetTransform), reinterpret_cast<void**>(&oSetTransform)}
    };

    bool all_success = true;
    for (const auto& hook : hooks) {
        if (!SetupHook(hook)) {
            all_success = false;
        }
    }

    return all_success;
}

int __cdecl gui_load_handler(uint32_t flag1, uint32_t flag2, char* name) {
    if (name) {
        WriteLogf("[GUI] Loading: %s (flags: 0x%x, 0x%x)", name, flag1, flag2);
    }
    return 0;
}

static void __declspec(naked) gui_hook_trampoline() {
    asm volatile(
        "movl 0xc(%%esp),%%eax    \n\t"   // Get 3rd param (gui_name)
        "pushl %%eax               \n\t"  // Push as 3rd arg
        "movl 0xc(%%esp),%%eax     \n\t"  // Get 2nd param - offset changed due to push
        "pushl %%eax               \n\t"  // Push as 2nd arg  
        "movl 0xc(%%esp),%%eax     \n\t"  // Get 1st param - offset changed due to push
        "pushl %%eax               \n\t"  // Push as 1st arg
        "call %P1                  \n\t"  // Call our handler
        "addl $0xc, %%esp          \n\t"  // Clean up pushed parameters
        "subl $0x248, %%esp        \n\t"  // Original function's stack allocation
        "jmp *%0                   \n\t"  // Jump to continue original function
        : : 
        "o" (g_menu_return_addr),
        "i" (gui_load_handler)
    );
}

void SetupGuiHook() {
    WriteLog("[+] Setting up GUI hook");
    
    g_menu_return_addr = ASI::AddrOf(0x70826);
    
    ASI::MemoryRegion gui_load_region(ASI::AddrOf(0x070820), 6);
    ASI::BeginRewrite(gui_load_region);
    
    *(unsigned char*)(ASI::AddrOf(0x070820)) = 0xE9;
    *(int*)(ASI::AddrOf(0x070821)) = reinterpret_cast<int>(&gui_hook_trampoline) - ASI::AddrOf(0x070825);
    *(unsigned char*)(ASI::AddrOf(0x070825)) = 0x90;
    
    ASI::EndRewrite(gui_load_region);
    
    WriteLog("[+] GUI hook installed successfully");
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
    
    SetupGuiHook();
    
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