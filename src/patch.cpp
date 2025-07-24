#include <windows.h>
#include <stdio.h>
#include "asi/asi.h"
#include "patch.h"
#include <d3d8.h>
extern "C" {
    #include "MinHook.h"
}
#include <windows.h>
#include <stdio.h>
#include "asi/asi.h"
#include "patch.h"
#include <d3d8.h>
extern "C" {
    #include "MinHook.h"
}

#define IDirect3D8_PTR_ADDR 0x6B5DDC

// Global device pointer to cache the device once we find it
IDirect3DDevice8* g_pDevice = nullptr;

typedef HRESULT(WINAPI* CreateDevice_t)(
    IDirect3D8* pD3D,
    UINT Adapter,
    D3DDEVTYPE DeviceType,
    HWND hFocusWindow,
    DWORD BehaviorFlags,
    D3DPRESENT_PARAMETERS* pPresentationParameters,
    IDirect3DDevice8** ppReturnedDeviceInterface);

CreateDevice_t oCreateDevice = nullptr;

IDirect3DDevice8* GetGameDeviceFixed()
{
    return g_pDevice;
}

typedef HRESULT(WINAPI* CreateTexture_t)(
    IDirect3DDevice8* pDevice,
    UINT Width,
    UINT Height,
    UINT Levels,
    DWORD Usage,
    D3DFORMAT Format,
    D3DPOOL Pool,
    IDirect3DTexture8** ppTexture);
    
CreateTexture_t oCreateTexture = nullptr;

typedef HRESULT(WINAPI* CreateImageSurface_t)(
    IDirect3DDevice8* pDevice,
    UINT Width,
    UINT Height,
    D3DFORMAT Format,
    IDirect3DSurface8** ppSurface);

CreateImageSurface_t oCreateImageSurface = nullptr;

typedef HRESULT(WINAPI* Present_t)(
    IDirect3DDevice8* pDevice,
    CONST RECT* pSourceRect,
    CONST RECT* pDestRect,
    HWND hDestWindowOverride,
    CONST RGNDATA* pDirtyRegion);

Present_t oPresent = nullptr;

typedef HRESULT(WINAPI* SetViewport_t)(
    IDirect3DDevice8* pDevice,
    CONST D3DVIEWPORT8* pViewport);

SetViewport_t oSetViewport = nullptr;

typedef HRESULT(WINAPI* SetRenderTarget_t)(
    IDirect3DDevice8* pDevice,
    IDirect3DSurface8* pRenderTarget,
    IDirect3DSurface8* pNewZStencil);

SetRenderTarget_t oSetRenderTarget = nullptr;

typedef HRESULT(WINAPI* SetClipPlane_t)(
    IDirect3DDevice8* pDevice,
    DWORD Index,
    CONST float* pPlane);

SetClipPlane_t oSetClipPlane = nullptr;

HRESULT WINAPI hkCreateTexture(
    IDirect3DDevice8* pDevice,
    UINT Width,
    UINT Height,
    UINT Levels,
    DWORD Usage,
    D3DFORMAT Format,
    D3DPOOL Pool,
    IDirect3DTexture8** ppTexture)
{
    
    if(Width == 1024 && Height == 1024)
    {
        OutputDebugStringA("[Hook] CreateImage - Scaling 1024x1024 to 1280x1280\n");
        return oCreateTexture(pDevice, 1280, 1280, Levels, Usage, Format, Pool, ppTexture);
    }

    return oCreateTexture(pDevice, Width, Height, Levels, Usage, Format, Pool, ppTexture);
}


typedef HRESULT(WINAPI* SetTransform_t)(
    IDirect3DDevice8* pDevice,
    D3DTRANSFORMSTATETYPE State,
    CONST D3DMATRIX* pMatrix);

SetTransform_t oSetTransform = nullptr;
HRESULT WINAPI hkSetTransform(
    IDirect3DDevice8* pDevice,
    D3DTRANSFORMSTATETYPE State,
    CONST D3DMATRIX* pMatrix)
{
    // Only log projection matrix changes as they affect clipping
     // Check for projection matrix and fix if corrupted
    if (State == D3DTS_PROJECTION && pMatrix)
    {
        char info[512];
       
        
        OutputDebugStringA("[Hook] Detected corrupted projection matrix! Creating corrected matrix.\n");
        
        // Create a proper orthographic projection matrix for UI rendering
        D3DMATRIX correctedMatrix;
        D3DVIEWPORT8 viewport;
        if (SUCCEEDED(pDevice->GetViewport(&viewport)))
        {
            float width = (float)viewport.Width;
            float height = (float)viewport.Height;
            
            char viewportInfo[256];
            sprintf(viewportInfo, "[Hook] Using viewport dimensions: %.0fx%.0f for projection fix\n", width, height);
            OutputDebugStringA(viewportInfo);
            
            // Create orthographic projection matrix
            // This maps screen coordinates directly (0,0) to (width,height)
            memset(&correctedMatrix, 0, sizeof(D3DMATRIX));
            correctedMatrix._11 = 2.0f / width;   // X scale
            correctedMatrix._22 = -2.0f / height; // Y scale (negative for screen coordinates)
            correctedMatrix._33 = 1.0f;           // Z scale
            correctedMatrix._44 = 1.0f;           // W scale
            correctedMatrix._41 = -1.0f;          // X offset
            correctedMatrix._42 = 1.0f;           // Y offset
            
            OutputDebugStringA("[Hook] Using corrected orthographic projection matrix\n");
            return oSetTransform(pDevice, State, &correctedMatrix);
        }
        else
        {
            OutputDebugStringA("[Hook] Could not get viewport, using 1152x864 fallback projection\n");
            memset(&correctedMatrix, 0, sizeof(D3DMATRIX));
            correctedMatrix._11 = 2.0f / 1152.0f;
            correctedMatrix._22 = -2.0f / 864.0f;
            correctedMatrix._33 = 1.0f;
            correctedMatrix._44 = 1.0f;
            correctedMatrix._41 = -1.0f;
            correctedMatrix._42 = 1.0f;
            
            return oSetTransform(pDevice, State, &correctedMatrix);
        }
        
    }

    return oSetTransform(pDevice, State, pMatrix);
}

HRESULT WINAPI hkSetClipPlane(
    IDirect3DDevice8* pDevice,
    DWORD Index,
    CONST float* pPlane)
{
    if (pPlane)
    {
        char info[512];
        sprintf(info, "[Hook] SetClipPlane[%u]: (%.3f, %.3f, %.3f, %.3f)\n",
                Index, pPlane[0], pPlane[1], pPlane[2], pPlane[3]);
        OutputDebugStringA(info);
    }

    return oSetClipPlane(pDevice, Index, pPlane);
}

HRESULT WINAPI hkCreateImageSurface(
    IDirect3DDevice8* pDevice,
    UINT Width,
    UINT Height,
    D3DFORMAT Format,
    IDirect3DSurface8** ppSurface)
{
    char info[512];
    sprintf(info, "[Hook] CreateImageSurface:\n%ux%u\nFormat: %d\n", Width, Height, Format);
    OutputDebugStringA(info);
    
    if(Width == 1024 && Height == 1024)
    {
        OutputDebugStringA("[Hook] CreateImageSurface - Scaling 1024x1024 to 1280x1280\n");
        return oCreateImageSurface(pDevice, 1280, 1280, Format, ppSurface);
    }
    
    return oCreateImageSurface(pDevice, Width, Height, Format, ppSurface);
}

HRESULT WINAPI hkPresent(
    IDirect3DDevice8* pDevice,
    CONST RECT* pSourceRect,
    CONST RECT* pDestRect,
    HWND hDestWindowOverride,
    CONST RGNDATA* pDirtyRegion)
{

    return oPresent(pDevice, pSourceRect, pDestRect, hDestWindowOverride, pDirtyRegion);
}

HRESULT WINAPI hkSetViewport(
    IDirect3DDevice8* pDevice,
    CONST D3DVIEWPORT8* pViewport)
{    
    return oSetViewport(pDevice, pViewport);
}

HRESULT WINAPI hkSetRenderTarget(
    IDirect3DDevice8* pDevice,
    IDirect3DSurface8* pRenderTarget,
    IDirect3DSurface8* pNewZStencil)
{
    if (pRenderTarget)
    {
        D3DSURFACE_DESC desc;
        if (SUCCEEDED(pRenderTarget->GetDesc(&desc)))
        {
            char info[512];
            sprintf(info, "[Hook] SetRenderTarget: %ux%u Format:%d\n", 
                    desc.Width, desc.Height, desc.Format);
            OutputDebugStringA(info);
        }
        else
        {
            OutputDebugStringA("[Hook] SetRenderTarget: Could not get surface description\n");
        }
    }
    else
    {
        OutputDebugStringA("[Hook] SetRenderTarget: NULL render target (back buffer)\n");
    }
    
    return oSetRenderTarget(pDevice, pRenderTarget, pNewZStencil);
}


bool HookGameCreateTexture()
{
    OutputDebugStringA("[+] Attempting to hook IDirect3DDevice8::CreateTexture\n");
    IDirect3DDevice8* device = GetGameDeviceFixed();
    if (!device)
    {
        OutputDebugStringA("[-] Failed to get game device pointer.\n");
        return false;
    }

    void** vtable = *reinterpret_cast<void***>(device);
    if (!vtable)
    {
        OutputDebugStringA("[-] Device vtable pointer is null.\n");
        return false;
    }

    void* pCreateTexture = vtable[20];
    if (!pCreateTexture)
    {
        OutputDebugStringA("[-] CreateTexture method pointer is null.\n");
        return false;
    }

    OutputDebugStringA("[+] Device and CreateTexture method pointer are valid.\n");

    if (MH_CreateHook(pCreateTexture, reinterpret_cast<LPVOID>(hkCreateTexture), reinterpret_cast<void**>(&oCreateTexture)) != MH_OK)
    {
        OutputDebugStringA("[-] MH_CreateHook failed for CreateTexture.\n");
        return false;
    }

    if (MH_EnableHook(pCreateTexture) != MH_OK)
    {
        OutputDebugStringA("[-] MH_EnableHook failed for CreateTexture.\n");
        return false;
    }

    OutputDebugStringA("[+] Successfully hooked IDirect3DDevice8::CreateTexture.\n");
    return true;
}

bool HookGameCreateImageSurface()
{
    OutputDebugStringA("[+] Attempting to hook IDirect3DDevice8::CreateImageSurface\n");
    IDirect3DDevice8* device = GetGameDeviceFixed();
    if (!device)
    {
        OutputDebugStringA("[-] Failed to get game device pointer.\n");
        return false;
    }

    void** vtable = *reinterpret_cast<void***>(device);
    if (!vtable)
    {
        OutputDebugStringA("[-] Device vtable pointer is null.\n");
        return false;
    }

    void* pCreateImageSurface = vtable[27];
    if (!pCreateImageSurface)
    {
        OutputDebugStringA("[-] CreateImageSurface method pointer is null.\n");
        return false;
    }

    OutputDebugStringA("[+] Device and CreateImageSurface method pointer are valid.\n");

    if (MH_CreateHook(pCreateImageSurface, reinterpret_cast<LPVOID>(hkCreateImageSurface), reinterpret_cast<void**>(&oCreateImageSurface)) != MH_OK)
    {
        OutputDebugStringA("[-] MH_CreateHook failed for CreateImageSurface.\n");
        return false;
    }

    if (MH_EnableHook(pCreateImageSurface) != MH_OK)
    {
        OutputDebugStringA("[-] MH_EnableHook failed for CreateImageSurface.\n");
        return false;
    }

    OutputDebugStringA("[+] Successfully hooked IDirect3DDevice8::CreateImageSurface.\n");
    return true;
}


bool HookSetTransform()
{
    OutputDebugStringA("[+] Attempting to hook IDirect3DDevice8::SetTransform\n");
    IDirect3DDevice8* device = GetGameDeviceFixed();
    if (!device)
    {
        OutputDebugStringA("[-] Failed to get game device pointer.\n");
        return false;
    }

    void** vtable = *reinterpret_cast<void***>(device);
    if (!vtable)
    {
        OutputDebugStringA("[-] Device vtable pointer is null.\n");
        return false;
    }

    void* pCreateImageSurface = vtable[37];
    if (!pCreateImageSurface)
    {
        OutputDebugStringA("[-] SetTransform method pointer is null.\n");
        return false;
    }

    OutputDebugStringA("[+] Device and CreateImageSurface method pointer are valid.\n");

    if (MH_CreateHook(pCreateImageSurface, reinterpret_cast<LPVOID>(hkSetTransform), reinterpret_cast<void**>(&oSetTransform)) != MH_OK)
    {
        OutputDebugStringA("[-] MH_CreateHook failed for CreateImageSurface.\n");
        return false;
    }

    if (MH_EnableHook(pCreateImageSurface) != MH_OK)
    {
        OutputDebugStringA("[-] MH_EnableHook failed for CreateImageSurface.\n");
        return false;
    }

    OutputDebugStringA("[+] Successfully hooked IDirect3DDevice8::CreateImageSurface.\n");
    return true;
}

bool HookGamePresent()
{
    OutputDebugStringA("[+] Attempting to hook IDirect3DDevice8::Present\n");
    IDirect3DDevice8* device = GetGameDeviceFixed();
    if (!device)
    {
        OutputDebugStringA("[-] Failed to get game device pointer.\n");
        return false;
    }

    void** vtable = *reinterpret_cast<void***>(device);
    if (!vtable)
    {
        OutputDebugStringA("[-] Device vtable pointer is null.\n");
        return false;
    }

    void* pPresent = vtable[15];
    if (!pPresent)
    {
        OutputDebugStringA("[-] Present method pointer is null.\n");
        return false;
    }

    OutputDebugStringA("[+] Device and Present method pointer are valid.\n");

    if (MH_CreateHook(pPresent, reinterpret_cast<LPVOID>(hkPresent), reinterpret_cast<void**>(&oPresent)) != MH_OK)
    {
        OutputDebugStringA("[-] MH_CreateHook failed for Present.\n");
        return false;
    }

    if (MH_EnableHook(pPresent) != MH_OK)
    {
        OutputDebugStringA("[-] MH_EnableHook failed for Present.\n");
        return false;
    }

    OutputDebugStringA("[+] Successfully hooked IDirect3DDevice8::Present.\n");
    return true;
}

bool HookGameSetViewport()
{
    OutputDebugStringA("[+] Attempting to hook IDirect3DDevice8::SetViewport\n");
    IDirect3DDevice8* device = GetGameDeviceFixed();
    if (!device)
    {
        OutputDebugStringA("[-] Failed to get game device pointer.\n");
        return false;
    }

    void** vtable = *reinterpret_cast<void***>(device);
    if (!vtable)
    {
        OutputDebugStringA("[-] Device vtable pointer is null.\n");
        return false;
    }

    void* pSetViewport = vtable[40];
    if (!pSetViewport)
    {
        OutputDebugStringA("[-] SetViewport method pointer is null.\n");
        return false;
    }

    OutputDebugStringA("[+] Device and SetViewport method pointer are valid.\n");

    if (MH_CreateHook(pSetViewport, reinterpret_cast<LPVOID>(hkSetViewport), reinterpret_cast<void**>(&oSetViewport)) != MH_OK)
    {
        OutputDebugStringA("[-] MH_CreateHook failed for SetViewport.\n");
        return false;
    }

    if (MH_EnableHook(pSetViewport) != MH_OK)
    {
        OutputDebugStringA("[-] MH_EnableHook failed for SetViewport.\n");
        return false;
    }

    OutputDebugStringA("[+] Successfully hooked IDirect3DDevice8::SetViewport.\n");
    return true;
}

bool HookGameSetRenderTarget()
{
    OutputDebugStringA("[+] Attempting to hook IDirect3DDevice8::SetRenderTarget\n");
    IDirect3DDevice8* device = GetGameDeviceFixed();
    if (!device)
    {
        OutputDebugStringA("[-] Failed to get game device pointer.\n");
        return false;
    }

    void** vtable = *reinterpret_cast<void***>(device);
    if (!vtable)
    {
        OutputDebugStringA("[-] Device vtable pointer is null.\n");
        return false;
    }

    void* pSetRenderTarget = vtable[31];
    if (!pSetRenderTarget)
    {
        OutputDebugStringA("[-] SetRenderTarget method pointer is null.\n");
        return false;
    }

    OutputDebugStringA("[+] Device and SetRenderTarget method pointer are valid.\n");

    if (MH_CreateHook(pSetRenderTarget, reinterpret_cast<LPVOID>(hkSetRenderTarget), reinterpret_cast<void**>(&oSetRenderTarget)) != MH_OK)
    {
        OutputDebugStringA("[-] MH_CreateHook failed for SetRenderTarget.\n");
        return false;
    }

    if (MH_EnableHook(pSetRenderTarget) != MH_OK)
    {
        OutputDebugStringA("[-] MH_EnableHook failed for SetRenderTarget.\n");
        return false;
    }

    OutputDebugStringA("[+] Successfully hooked IDirect3DDevice8::SetRenderTarget.\n");
    return true;
}

bool HookGameSetClipPlane()
{
    OutputDebugStringA("[+] Attempting to hook IDirect3DDevice8::SetClipPlane\n");
    IDirect3DDevice8* device = GetGameDeviceFixed();
    if (!device)
    {
        OutputDebugStringA("[-] Failed to get game device pointer.\n");
        return false;
    }

    void** vtable = *reinterpret_cast<void***>(device);
    if (!vtable)
    {
        OutputDebugStringA("[-] Device vtable pointer is null.\n");
        return false;
    }

    void* pSetClipPlane = vtable[48];
    if (!pSetClipPlane)
    {
        OutputDebugStringA("[-] SetClipPlane method pointer is null.\n");
        return false;
    }

    OutputDebugStringA("[+] Device and SetClipPlane method pointer are valid.\n");

    if (MH_CreateHook(pSetClipPlane, reinterpret_cast<LPVOID>(hkSetClipPlane), reinterpret_cast<void**>(&oSetClipPlane)) != MH_OK)
    {
        OutputDebugStringA("[-] MH_CreateHook failed for SetClipPlane.\n");
        return false;
    }

    if (MH_EnableHook(pSetClipPlane) != MH_OK)
    {
        OutputDebugStringA("[-] MH_EnableHook failed for SetClipPlane.\n");
        return false;
    }

    OutputDebugStringA("[+] Successfully hooked IDirect3DDevice8::SetClipPlane.\n");
    return true;
}

HRESULT WINAPI hkCreateDevice(
    IDirect3D8* pD3D,
    UINT Adapter,
    D3DDEVTYPE DeviceType,
    HWND hFocusWindow,
    DWORD BehaviorFlags,
    D3DPRESENT_PARAMETERS* pPresentationParameters,
    IDirect3DDevice8** ppReturnedDeviceInterface)
{
    HRESULT result = oCreateDevice(pD3D, Adapter, DeviceType, hFocusWindow, BehaviorFlags, pPresentationParameters, ppReturnedDeviceInterface);
    if (SUCCEEDED(result) && ppReturnedDeviceInterface && *ppReturnedDeviceInterface)
    {
          if(oCreateTexture == nullptr)
          {
            g_pDevice = *ppReturnedDeviceInterface;
            OutputDebugStringA("[+] Device captured from CreateDevice hook!\n");        
            HookGameCreateTexture();
            HookGameCreateImageSurface();
            HookGamePresent();
            HookGameSetViewport();
            HookGameSetRenderTarget();
            HookGameSetClipPlane();
            HookSetTransform();
          }
    }
    
    return result;
}


bool HookCreateDevice()
{
    IDirect3D8* pD3D = *reinterpret_cast<IDirect3D8**>(IDirect3D8_PTR_ADDR);
    if (!pD3D)
    {
        OutputDebugStringA("[-] Failed to get IDirect3D8 pointer.\n");
        return false;
    }

    void** vtable = *reinterpret_cast<void***>(pD3D);
    if (!vtable)
    {
        OutputDebugStringA("[-] IDirect3D8 vtable pointer is null.\n");
        return false;
    }

    void* pCreateDevice = vtable[15];
    if (!pCreateDevice)
    {
        OutputDebugStringA("[-] CreateDevice method pointer is null.\n");
        return false;
    }

    if (MH_Initialize() != MH_OK)
    {
        OutputDebugStringA("[-] MH_Initialize failed.\n");
        return false;
    }

    if (MH_CreateHook(pCreateDevice, reinterpret_cast<LPVOID>(hkCreateDevice), reinterpret_cast<void**>(&oCreateDevice)) != MH_OK)
    {
        OutputDebugStringA("[-] MH_CreateHook failed for CreateDevice.\n");
        return false;
    }

    if (MH_EnableHook(pCreateDevice) != MH_OK)
    {
        OutputDebugStringA("[-] MH_EnableHook failed for CreateDevice.\n");
        return false;
    }

    OutputDebugStringA("[+] Successfully hooked IDirect3D8::CreateDevice.\n");
    return true;
}


void UnhookCreateTexture()
{
    MH_DisableHook(MH_ALL_HOOKS);
    MH_Uninitialize();
}


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
            UnhookCreateTexture();
            break;
    }
    return TRUE;
}

uint32_t g_menu_return_addr;
static void init_call_hooks()
{
    g_menu_return_addr = (ASI::AddrOf(0x70826));
    HookCreateDevice();
}

int __cdecl gui_load_something(uint32_t a, uint32_t b, char* name)
{
    char info[512];
    
    // Adding this will force the choose character talent menu background to load for the load game menu
    // if(!strcmp(name, "menu\\loadgame_new"))
    // {
    //     sprintf(name, "menu\\choosecharacter_talent");
    // }
    //MENU\\MAIN_MENU
    // sprintf(info, "Hooked into gui load.\nFlag 1:%x\nFlag 2:%x\nGUI name ptr:%p\nGUI name string:%s\n", 
    //         a, b, name, name ? name : "(null)");
    // OutputDebugStringA(info);
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
