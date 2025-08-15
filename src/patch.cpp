#include <windows.h>
#include <stdio.h>
#include <d3d8.h>
#include <string>
#include <vector>
#include "asi/asi.h"
#include "patch.h"
#include "logger.h"

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

CreateDevice_t oCreateDevice = nullptr;
CreateImageSurface_t oCreateImageSurface = nullptr;
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

bool IsValidMemoryRegion(const void* address, size_t size) {
    if (!address) return false;

    MEMORY_BASIC_INFORMATION mbi;
    SIZE_T result = VirtualQuery(address, &mbi, sizeof(mbi));
    if (result == 0 || mbi.State != MEM_COMMIT) return false;

    if (!(mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
                         PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))) {
        return false;
    }

    uintptr_t start = (uintptr_t)address;
    uintptr_t end = start + size - 1;
    uintptr_t region_start = (uintptr_t)mbi.BaseAddress;
    uintptr_t region_end = region_start + mbi.RegionSize - 1;

    return start >= region_start && end <= region_end;
}

bool SafeReadMemory(const void* source, void* dest, size_t size) {
    if (!source || !dest || size == 0) return false;

    if (!IsValidMemoryRegion(source, size)) {
        WriteLogf("memory", "[-] Invalid memory region at 0x%p (%zu bytes)", source, size);
        return false;
    }

    try {
        memcpy(dest, source, size);
        return true;
    } catch (int error) {
        WriteLogf("memory", "[-] Exception while reading memory at 0x%p", source);
        return false;
    }
}

void PrintMemoryHex(const void* data, size_t size, const char* label) {
    if (!data) {
        WriteLogf(label, "Data pointer is NULL");
        return;
    }

    const unsigned char* bytes = (const unsigned char*)data;
    WriteLogf(label, "Memory dump (%zu bytes):", size);

    for (size_t i = 0; i < size; i += 16) {
        char hex[64] = {0};
        char ascii[17] = {0};

        for (size_t j = 0; j < 16 && (i + j) < size; j++) {
            sprintf_s(hex + (j * 3), sizeof(hex) - (j * 3), "%02X ", bytes[i + j]);
            ascii[j] = (bytes[i + j] >= 32 && bytes[i + j] < 127) ? bytes[i + j] : '.';
        }

        WriteLogf(label, "  %04X: %-48s %s", (unsigned int)i, hex, ascii);
    }
}




const char* D3DFormatToString(D3DFORMAT fmt) {
    switch (fmt) {
        case D3DFMT_UNKNOWN:      return "D3DFMT_UNKNOWN";
        case D3DFMT_R8G8B8:       return "D3DFMT_R8G8B8";
        case D3DFMT_A8R8G8B8:     return "D3DFMT_A8R8G8B8";
        case D3DFMT_X8R8G8B8:     return "D3DFMT_X8R8G8B8";
        case D3DFMT_R5G6B5:       return "D3DFMT_R5G6B5";
        case D3DFMT_X1R5G5B5:     return "D3DFMT_X1R5G5B5";
        case D3DFMT_A1R5G5B5:     return "D3DFMT_A1R5G5B5";
        case D3DFMT_A4R4G4B4:     return "D3DFMT_A4R4G4B4";
        case D3DFMT_R3G3B2:       return "D3DFMT_R3G3B2";
        case D3DFMT_A8:           return "D3DFMT_A8";
        case D3DFMT_A8R3G3B2:     return "D3DFMT_A8R3G3B2";
        case D3DFMT_X4R4G4B4:     return "D3DFMT_X4R4G4B4";
        case D3DFMT_A2B10G10R10:  return "D3DFMT_A2B10G10R10";
        case D3DFMT_G16R16:       return "D3DFMT_G16R16";
        case D3DFMT_D16_LOCKABLE: return "D3DFMT_D16_LOCKABLE";
        case D3DFMT_D32:          return "D3DFMT_D32";
        case D3DFMT_D15S1:        return "D3DFMT_D15S1";
        case D3DFMT_D24S8:        return "D3DFMT_D24S8";
        case D3DFMT_D24X8:        return "D3DFMT_D24X8";
        case D3DFMT_D24X4S4:      return "D3DFMT_D24X4S4";
        case D3DFMT_D16:          return "D3DFMT_D16";

        case D3DFMT_INDEX16:      return "D3DFMT_INDEX16";
        case D3DFMT_INDEX32:      return "D3DFMT_INDEX32";

        case D3DFMT_VERTEXDATA:   return "D3DFMT_VERTEXDATA";

        case D3DFMT_FORCE_DWORD:  return "D3DFMT_FORCE_DWORD";

        default: return "UNKNOWN_D3DFORMAT";
    }
}

const char* IsLikelyValid(BOOL condition) {
    return condition ? "[VALID (heuristic)]" : "[SUSPICIOUS VALUE]";
}

const char* DeviceTypeToString(D3DDEVTYPE type) {
    switch (type) {
        case D3DDEVTYPE_HAL: return "D3DDEVTYPE_HAL";
        case D3DDEVTYPE_REF: return "D3DDEVTYPE_REF";
        case D3DDEVTYPE_SW:  return "D3DDEVTYPE_SW";
        case D3DDEVTYPE_FORCE_DWORD: return "D3DDEVTYPE_FORCE_DWORD";
        default: return "UNKNOWN_DEVICE_TYPE";
    }
}

void PrintD3DDisplayMode(const D3DDISPLAYMODE* mode, const char* label) {
    if (!mode) {
        WriteLog(label, "D3DDISPLAYMODE is NULL");
        return;
    }

    WriteLog(label, "Testing D3DDISPLAYMODE Dump:");

    WriteLogf(label, "  Width: uint(%u) hex(%x) %s",
              mode->Width, mode->Width,
              IsLikelyValid(mode->Width > 0 && mode->Width <= 16384));

    WriteLogf(label, "  Height: uint(%u) hex(%x) %s",
              mode->Height, mode->Height,
              IsLikelyValid(mode->Height > 0 && mode->Height <= 16384));

    WriteLogf(label, "  RefreshRate: uint(%u) hex(%x) %s",
              mode->RefreshRate, mode->RefreshRate,
              IsLikelyValid(mode->RefreshRate > 0 && mode->RefreshRate <= 240));

    const char* fmtStr = D3DFormatToString(mode->Format);
    WriteLogf(label, "  Format: hex(%x) %s %s",
              mode->Format, fmtStr,
              IsLikelyValid(strcmp(fmtStr, "UNKNOWN_D3DFORMAT") != 0));
}

void PrintD3DSurfaceDesc(const D3DSURFACE_DESC* desc, const char* label) {
    if (!desc) {
        WriteLog(label, "D3DSURFACE_DESC is NULL");
        return;
    }

    WriteLog(label, "Testing D3DSURFACE_DESC Dump:");

    // Format - Check if value is within known D3DFORMAT enum range (D3DFMT_UNKNOWN to D3DFMT_FORCE_DWORD)
    WriteLogf(label, "  Format: hex(%x) %s %s",
              desc->Format, D3DFormatToString(desc->Format),
              IsLikelyValid(strcmp(D3DFormatToString(desc->Format), "UNKNOWN_D3DFORMAT") != 0));

    // Type - Valid D3DRESOURCETYPE range (from D3DRTYPE_SURFACE (1) to D3DRTYPE_INDEXBUFFER (5) in D3D8)
    WriteLogf(label, "  Type: uint(%u) hex(%x) %s",
              desc->Type, desc->Type,
              IsLikelyValid(desc->Type >= 1 && desc->Type <= 5));

    // Usage - Usually a bitmask (e.g., D3DUSAGE_RENDERTARGET = 0x00000001). Allow for common flag values.
    WriteLogf(label, "  Usage: uint(%u) hex(%x) %s",
              desc->Usage, desc->Usage,
              IsLikelyValid(desc->Usage <= 0x0000001F)); // Loosely defined upper bound

    // Pool - Known D3DPOOL values: 0 (DEFAULT), 1 (MANAGED), 2 (SYSTEMMEM), 3 (SCRATCH)
    WriteLogf(label, "  Pool: uint(%u) hex(%x) %s",
              desc->Pool, desc->Pool,
              IsLikelyValid(desc->Pool >= 0 && desc->Pool <= 3));

    // Size - Should not be 0, arbitrary upper bound (e.g., < 100MB)
    WriteLogf(label, "  Size: uint(%u) hex(%x) %s",
              desc->Size, desc->Size,
              IsLikelyValid(desc->Size > 0 && desc->Size < (100 * 1024 * 1024)));

    // MultiSampleType - 0 (NONE) to max enum, e.g., 6 in D3D8
    WriteLogf(label, "  MultiSampleType: uint(%u) hex(%x) %s",
              desc->MultiSampleType, desc->MultiSampleType,
              IsLikelyValid(desc->MultiSampleType >= 0 && desc->MultiSampleType <= 6));

    // Width and Height - Should be non-zero, and within common texture size bounds
    WriteLogf(label, "  Width: uint(%u) hex(%x) %s",
              desc->Width, desc->Width,
              IsLikelyValid(desc->Width > 0 && desc->Width <= 16384));

    WriteLogf(label, "  Height: uint(%u) hex(%x) %s",
              desc->Height, desc->Height,
              IsLikelyValid(desc->Height > 0 && desc->Height <= 16384));
}

void PrintD3DCaps(const D3DCAPS8* params, const char* label) {
    if (!params) {
        WriteLogf(label, "_D3DCAPS8 is NULL");
        return;
    }

    WriteLogf(label, "Accessing Data at 0x%x", &params);
    WriteLogf(label, "Testing D3DCAPS8 Dump:");

    // DeviceType
    WriteLogf(label, "  DeviceType: uint(%u) hex(%x) [%s] %s",
        params->DeviceType,
        params->DeviceType,
        DeviceTypeToString(params->DeviceType),
        IsLikelyValid(params->DeviceType >= D3DDEVTYPE_HAL && params->DeviceType <= D3DDEVTYPE_SW)
    );

    // AdapterOrdinal, usually small number
    WriteLogf(label, "  AdapterOrdinal: uint(%u) hex(%x) %s",
        params->AdapterOrdinal,
        params->AdapterOrdinal,
        IsLikelyValid(params->AdapterOrdinal < 16)
    );

    // These are bitfields.
    WriteLogf(label, "  Caps: uint(%u) hex(%x) [Bitfield]",
        params->Caps,
        params->Caps
    );
    WriteLogf(label, "  Caps2: uint(%u) hex(%x) [Bitfield]",
        params->Caps2,
        params->Caps2
    );
    WriteLogf(label, "  Caps3: uint(%u) hex(%x) [Bitfield]",
        params->Caps3,
        params->Caps3
    );

    WriteLogf(label, "  PresentationIntervals: uint(%u) hex(%x) [Bitfield]",
        params->PresentationIntervals,
        params->PresentationIntervals
    );

    WriteLogf(label, "  CursorCaps: uint(%u) hex(%x) [Bitfield]",
        params->CursorCaps,
        params->CursorCaps
    );

    WriteLogf(label, "  DevCaps: uint(%u) hex(%x) [Bitfield]",
        params->DevCaps,
        params->DevCaps
    );

    WriteLogf(label, "  PrimitiveMiscCaps: uint(%u) hex(%x) [Bitfield]",
        params->PrimitiveMiscCaps,
        params->PrimitiveMiscCaps
    );

    WriteLogf(label, "  RasterCaps: uint(%u) hex(%x) [Bitfield]",
        params->RasterCaps,
        params->RasterCaps
    );

    WriteLogf(label, "  ZCmpCaps: uint(%u) hex(%x) [Bitfield]",
        params->ZCmpCaps,
        params->ZCmpCaps
    );

    WriteLogf(label, "  SrcBlendCaps: uint(%u) hex(%x) [Bitfield]",
        params->SrcBlendCaps,
        params->SrcBlendCaps
    );

    WriteLogf(label, "  DestBlendCaps: uint(%u) hex(%x) [Bitfield]",
        params->DestBlendCaps,
        params->DestBlendCaps
    );

    WriteLogf(label, "  AlphaCmpCaps: uint(%u) hex(%x) [Bitfield]",
        params->AlphaCmpCaps,
        params->AlphaCmpCaps
    );

    WriteLogf(label, "  ShadeCaps: uint(%u) hex(%x) [Bitfield]",
        params->ShadeCaps,
        params->ShadeCaps
    );

    WriteLogf(label, "  TextureCaps: uint(%u) hex(%x) [Bitfield]",
        params->TextureCaps,
        params->TextureCaps
    );

    WriteLogf(label, "  TextureFilterCaps: uint(%u) hex(%x) [Bitfield]",
        params->TextureFilterCaps,
        params->TextureFilterCaps
    );

    WriteLogf(label, "  CubeTextureFilterCaps: uint(%u) hex(%x) [Bitfield]",
        params->CubeTextureFilterCaps,
        params->CubeTextureFilterCaps
    );

    WriteLogf(label, "  VolumeTextureAddressCaps: uint(%u) hex(%x) [Bitfield]",
        params->VolumeTextureAddressCaps,
        params->VolumeTextureAddressCaps
    );

    WriteLogf(label, "  LineCaps: uint(%u) hex(%x) [Bitfield]",
        params->LineCaps,
        params->LineCaps
    );

    WriteLogf(label, "  MaxTextureWidth: uint(%u) hex(%x) %s",
        params->MaxTextureWidth,
        params->MaxTextureWidth,
        IsLikelyValid(params->MaxTextureWidth > 0 && params->MaxTextureWidth <= 8192)
    );

    WriteLogf(label, "  MaxTextureHeight: uint(%u) hex(%x) %s",
        params->MaxTextureHeight,
        params->MaxTextureHeight,
        IsLikelyValid(params->MaxTextureHeight > 0 && params->MaxTextureHeight <= 8192)
    );

    // MaxVolumeExtent
    WriteLogf(label, "  MaxVolumeExtent: uint(%u) hex(%x) %s",
        params->MaxVolumeExtent,
        params->MaxVolumeExtent,
        IsLikelyValid(params->MaxVolumeExtent > 0 && params->MaxVolumeExtent <= 2048)
    );

    // MaxTextureRepeat (used for wrapping/tiling)
    WriteLogf(label, "  MaxTextureRepeat: uint(%u) hex(%x) %s",
        params->MaxTextureRepeat,
        params->MaxTextureRepeat,
        IsLikelyValid(params->MaxTextureRepeat > 0 && params->MaxTextureRepeat <= 32768)
    );

    // MaxTextureAspectRatio (typical is 1–8, sometimes up to 32)
    WriteLogf(label, "  MaxTextureAspectRatio: uint(%u) hex(%x) %s",
        params->MaxTextureAspectRatio,
        params->MaxTextureAspectRatio,
        IsLikelyValid(params->MaxTextureAspectRatio >= 1 && params->MaxTextureAspectRatio <= 32)
    );

    // MaxAnisotropy (commonly 1–16)
    WriteLogf(label, "  MaxAnisotropy: uint(%u) hex(%x) %s",
        params->MaxAnisotropy,
        params->MaxAnisotropy,
        IsLikelyValid(params->MaxAnisotropy >= 1 && params->MaxAnisotropy <= 16)
    );

    // MaxVertexW (used in homogeneous coordinate clipping, usually a float)
    WriteLogf(label, "  MaxVertexW: float(%f) hex(%x) %s",
        params->MaxVertexW,
        *(DWORD*)&params->MaxVertexW,
        IsLikelyValid(params->MaxVertexW > 1.0f && params->MaxVertexW < 1000000.0f)
    );

    
    WriteLogf(label, "  GuardBandLeft: float(%f) hex(%x) %s",
    params->GuardBandLeft,
    *(DWORD*)&params->GuardBandLeft,
    IsLikelyValid(params->GuardBandLeft < 0.0f && params->GuardBandLeft > -10000.0f)
    );

    WriteLogf(label, "  GuardBandTop: float(%f) hex(%x) %s",
        params->GuardBandTop,
        *(DWORD*)&params->GuardBandTop,
        IsLikelyValid(params->GuardBandTop < 0.0f && params->GuardBandTop > -10000.0f)
    );

    WriteLogf(label, "  GuardBandRight: float(%f) hex(%x) %s",
        params->GuardBandRight,
        *(DWORD*)&params->GuardBandRight,
        IsLikelyValid(params->GuardBandRight > 0.0f && params->GuardBandRight < 10000.0f)
    );

    WriteLogf(label, "  GuardBandBottom: float(%f) hex(%x) %s",
        params->GuardBandBottom,
        *(DWORD*)&params->GuardBandBottom,
        IsLikelyValid(params->GuardBandBottom > 0.0f && params->GuardBandBottom < 10000.0f)
    );

    WriteLogf(label, "  ExtentsAdjust: float(%f) hex(%x) %s",
        params->ExtentsAdjust,
        *(DWORD*)&params->ExtentsAdjust,
        IsLikelyValid(params->ExtentsAdjust >= 0.0f && params->ExtentsAdjust <= 1.0f)
    );

    WriteLogf(label, "  StencilCaps: uint(%u) hex(%x) [Bitfield]",
        params->StencilCaps,
        params->StencilCaps
    );

    WriteLogf(label, "  FVFCaps: uint(%u) hex(%x) [Bitfield]",
        params->FVFCaps,
        params->FVFCaps
    );

    WriteLogf(label, "  TextureOpCaps: uint(%u) hex(%x) [Bitfield]",
        params->TextureOpCaps,
        params->TextureOpCaps
    );

    WriteLogf(label, "  VertexProcessingCaps: uint(%u) hex(%x) [Bitfield]",
        params->VertexProcessingCaps,
        params->VertexProcessingCaps
    );

    WriteLogf(label, "  MaxTextureBlendStages: uint(%u) hex(%x) %s",
        params->MaxTextureBlendStages,
        params->MaxTextureBlendStages,
        IsLikelyValid(params->MaxTextureBlendStages > 0 && params->MaxTextureBlendStages <= 8)
    );

    WriteLogf(label, "  MaxSimultaneousTextures: uint(%u) hex(%x) %s",
        params->MaxSimultaneousTextures,
        params->MaxSimultaneousTextures,
        IsLikelyValid(params->MaxSimultaneousTextures > 0 && params->MaxSimultaneousTextures <= 8)
    );

}

void PrintD3DPresentParameters(const D3DPRESENT_PARAMETERS* params, const char* label) {
    if (!params) {
        WriteLogf(label, " D3DPRESENT_PARAMETERS is NULL");
        return;
    }
    
    WriteLogf(label, " Testing D3DPRESENT_PARAMETERS:");
    WriteLogf(label, "  BackBufferWidth: %u", params->BackBufferWidth);
    WriteLogf(label, "  BackBufferHeight: %u", params->BackBufferHeight);
    WriteLogf(label, "  BackBufferFormat: %d", params->BackBufferFormat);
    WriteLogf(label, "  BackBufferCount: %u", params->BackBufferCount);
    WriteLogf(label, "  MultiSampleType: %d", params->MultiSampleType);
    WriteLogf(label, "  SwapEffect: %d", params->SwapEffect);
    WriteLogf(label, "  hDeviceWindow: 0x%p", params->hDeviceWindow);
    WriteLogf(label, "  Windowed: %s", params->Windowed ? "TRUE" : "FALSE");
    WriteLogf(label, "  EnableAutoDepthStencil: %s", params->EnableAutoDepthStencil ? "TRUE" : "FALSE");
    WriteLogf(label, "  AutoDepthStencilFormat: %d", params->AutoDepthStencilFormat);
    WriteLogf(label, "  Flags: 0x%08X", params->Flags);
    WriteLogf(label, "  FullScreen_RefreshRateInHz: %u", params->FullScreen_RefreshRateInHz);
    WriteLogf(label, "  FullScreen_PresentationInterval: %u", params->FullScreen_PresentationInterval);
}

void PrintMemoryRange(uintptr_t start_addr, uintptr_t end_addr, const char* label, bool show_ascii = true, bool show_interpretation = false) {
    if (start_addr >= end_addr) {
        WriteLogf(label, " Invalid address range: 0x%08X to 0x%08X", start_addr, end_addr);
        return;
    }
    
    size_t size = end_addr - start_addr + 1;
    const void* data = reinterpret_cast<const void*>(start_addr);
    
    WriteLogf(label, " Reading memory range 0x%08X to 0x%08X (%zu bytes)", 
              start_addr, end_addr, size);
    
    if (!data) {
        WriteLogf(label," Data pointer is NULL");
        return;
    }
    
    if (!IsValidMemoryRegion(data, size)) {
        WriteLogf(label, "Memory region 0x%p (size: %zu) is not accessible", data, size);
        return;
    }
    
    std::vector<unsigned char> buffer(size);
    if (!SafeReadMemory(data, buffer.data(), size)) {
        WriteLogf(label, " Failed to safely read memory");
        return;
    }
    
    const unsigned char* bytes = buffer.data();
    WriteLogf(label, " Memory dump at 0x%p (%zu bytes):", data, size);
  
    // Print hex dump
    for (size_t i = 0; i < size; i += 16) {
        char hex_line[64] = {0};
        char ascii_line[17] = {0};
        
        for (size_t j = 0; j < 16 && (i + j) < size; j++) {
            sprintf_s(hex_line + (j * 3), sizeof(hex_line) - (j * 3), "%02X ", bytes[i + j]);
            if (show_ascii) {
                ascii_line[j] = (bytes[i + j] >= 32 && bytes[i + j] < 127) ? bytes[i + j] : '.';
            }
        }
        
        if (show_ascii) {
            WriteLogf(label, "  %04X: %-48s %s", (unsigned int)i, hex_line, ascii_line);
        } else {
            WriteLogf(label, "  %04X: %s", (unsigned int)i, hex_line);
        }
    }
    
    if (show_interpretation) {
        WriteLogf(label, "Data interpretation:");
        
        if (size >= 4) {
            WriteLogf(label, "  As DWORDs:");
            for (size_t i = 0; i + 3 < size; i += 4) {
                uint32_t value = *reinterpret_cast<const uint32_t*>(&buffer[i]);
                uint32_t offset = start_addr+(unsigned int)i;
                WriteLogf(label, "    [%04X]: 0x%08X (%u) (%x)", (unsigned int)i, offset, value, value);
                if (i >= (size-1)) { 
                    WriteLogf(label, "    ... (END)");
                    break;
                }
            }
        }
        
        if (size >= 4) {
            WriteLogf(label, "  As floats:");
            for (size_t i = 0; i + 3 < size; i += 4) {
                float value = *reinterpret_cast<const float*>(&buffer[i]);
                WriteLogf(label, "    [%04X]: %.6f", (unsigned int)i, value);
                if (i >= (size-1)) { 
                    WriteLogf(label, "    ... (END)");
                    break;
                }
            }
        }
    }
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
    
    WriteLog("D3DDISPLAYMODE","Validating Memory Location at 0x14ce77c");
    bool isValid = IsValidMemoryRegion((void*)0x14ce77c, sizeof(D3DDISPLAYMODE));
    WriteLogf("D3DDISPLAYMODE", "Memory is [%s]", (isValid ? "Valid" : "Invalid"));
    
    PrintD3DDisplayMode((D3DDISPLAYMODE*)0x14ce77c, "D3DDISPLAYMODE");
    PrintD3DDisplayMode((D3DDISPLAYMODE*)0x14ce860, "ORIGIN_MEMORY_TARGET");
    
    PrintMemoryRange(0x14ce77c, 0x14ce77c+0x34, "UNKNOWN_MEMORY_TARGET", true, true);
    PrintMemoryRange(0x14ce860, 0x14ce860+0x34, "ORIGIN_MEMORY_TARGET", true, true);

    PrintD3DPresentParameters((D3DPRESENT_PARAMETERS*)0x14ce860, "ORIGIN_MEMORY_TARGET");

    PrintD3DPresentParameters(pPresentationParameters, "PARAMS_BEFORE");
    PrintMemoryHex(pPresentationParameters, sizeof(D3DPRESENT_PARAMETERS), "PARAMS_RAW_BEFORE");
    
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

    WriteLogSimple("[+] Successfully hooked IDirect3D8::CreateDevice");
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