#include <stdbool.h>
#include <stdint.h>
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
 
typedef struct {
    bool is_valid;
    const char* reason;
    int confidence;
} ValidationResult;

typedef ValidationResult (*FieldValidator)(const void* field_ptr, size_t field_size, const char* field_name);

typedef struct {
    size_t offset;           
    size_t size;            
    const char* name;       
    FieldValidator validator;
} FieldDescriptor;

typedef struct {
    const char* struct_name;
    size_t struct_size;
    const FieldDescriptor* fields;
    size_t field_count;
} StructDescriptor;

ValidationResult validate_uint_range(const void* field_ptr, size_t field_size, const char* field_name, 
                                    UINT min_val, UINT max_val) {
    ValidationResult result = {false, "Invalid field", 0};
    
    if (field_size != sizeof(UINT)) {
        result.reason = "Invalid field size for UINT";
        return result;
    }
    
    UINT value = *(const UINT*)field_ptr;
    
    if (value >= min_val && value <= max_val) {
        result.is_valid = true;
        result.reason = "Value in expected range";
        result.confidence = 90;
    } else if (value == 0) {
        result.is_valid = true;
        result.reason = "Zero value (possibly uninitialized)";
        result.confidence = 20;
    } else {
        result.reason = "Value outside expected range";
        result.confidence = -20;
    }
    
    return result;
}

ValidationResult validate_hwnd(const void* field_ptr, size_t field_size, const char* field_name) {
    ValidationResult result = {false, "Invalid HWND", 0};
    
    if (field_size != sizeof(HWND)) {
        result.reason = "Invalid field size for HWND";
        result.confidence = -50;
        return result;
    }
    
    HWND hwnd = *(const HWND*)field_ptr;
    
    if (hwnd == NULL) {
        result.is_valid = true;
        result.reason = "NULL HWND (valid)";
        result.confidence = 80;
    } else if (IsWindow(hwnd)) {
        result.is_valid = true;
        result.reason = "Valid window handle";
        result.confidence = 95;
    } else {
        // Check if it's a reasonable pointer value
        if ((uintptr_t)hwnd > 0x10000 && (uintptr_t)hwnd < 0x7FFFFFFF) {
            result.is_valid = true;
            result.reason = "Plausible window handle (cannot verify)";
            result.confidence = 50;
        } else {
            result.reason = "Invalid window handle";
            result.confidence = -50;
        }
    }
    
    return result;
}

ValidationResult validate_d3dformat(const void* field_ptr, size_t field_size, const char* field_name) {
    ValidationResult result = {false, "Invalid D3DFORMAT", 0};
    
    if (field_size != sizeof(D3DFORMAT)) {
        result.reason = "Invalid field size for D3DFORMAT";
        result.confidence = -50;
        return result;
    }
    
    D3DFORMAT format = *(const D3DFORMAT*)field_ptr;
    
    // Common D3D8 formats
    switch (format) {
        case D3DFMT_R8G8B8:
        case D3DFMT_A8R8G8B8:
        case D3DFMT_X8R8G8B8:
        case D3DFMT_R5G6B5:
        case D3DFMT_X1R5G5B5:
        case D3DFMT_A1R5G5B5:
        case D3DFMT_A4R4G4B4:
        case D3DFMT_R3G3B2:
        case D3DFMT_A8:
        case D3DFMT_A8R3G3B2:
        case D3DFMT_X4R4G4B4:
        case D3DFMT_A8P8:
        case D3DFMT_P8:
        case D3DFMT_L8:
        case D3DFMT_A8L8:
        case D3DFMT_A4L4:
            result.is_valid = true;
            result.reason = "Known D3DFORMAT";
            result.confidence = 95;
            break;
        case D3DFMT_UNKNOWN:
            result.is_valid = true;
            result.reason = "D3DFMT_UNKNOWN (valid but unusual)";
            result.confidence = 60;
            break;
        default:
            if ((DWORD)format >= 32 && (DWORD)format <= 200) {
                result.is_valid = true;
                result.reason = "Plausible D3DFORMAT value";
                result.confidence = 40;
            } else {
                result.reason = "Unlikely D3DFORMAT value";
                result.confidence = 10;
            }
            break;
    }
    
    return result;
}

ValidationResult validate_d3ddevtype(const void* field_ptr, size_t field_size, const char* field_name) {
    ValidationResult result = {false, "Invalid D3DDEVTYPE", 0};
    
    if (field_size != sizeof(D3DDEVTYPE)) {
        result.reason = "Invalid field size for D3DDEVTYPE";
        return result;
    }
    
    D3DDEVTYPE devtype = *(const D3DDEVTYPE*)field_ptr;
    
    switch (devtype) {
        case D3DDEVTYPE_HAL:
        case D3DDEVTYPE_REF:
        case D3DDEVTYPE_SW:
            result.is_valid = true;
            result.reason = "Valid D3DDEVTYPE";
            result.confidence = 95;
            break;
        default:
            if ((DWORD)devtype <= 10) {
                result.is_valid = true;
                result.reason = "Plausible D3DDEVTYPE value";
                result.confidence = 10;
            } else {
                result.reason = "Invalid D3DDEVTYPE";
                result.confidence = -50;
            }
            break;
    }
    
    return result;
}

ValidationResult validate_flags(const void* field_ptr, size_t field_size, const char* field_name) {
    ValidationResult result = {false, "Invalid behavior flags", 0};
    
    if (field_size != sizeof(DWORD)) {
        result.reason = "Invalid field size for DWORD behavior flags";
        return result;
    }
    
    DWORD flags = *(const DWORD*)field_ptr;
    
    // D3D8 Device Creation Behavior Flags
    const DWORD VALID_FLAGS = 
        D3DCREATE_FPU_PRESERVE |                    // 0x00000002
        D3DCREATE_MULTITHREADED |                   // 0x00000004
        D3DCREATE_PUREDEVICE |                      // 0x00000010
        D3DCREATE_SOFTWARE_VERTEXPROCESSING |       // 0x00000020
        D3DCREATE_HARDWARE_VERTEXPROCESSING |       // 0x00000040
        D3DCREATE_MIXED_VERTEXPROCESSING;           // 0x00000080
    
    // Check if flags is zero (valid but unusual)
    if (flags == 0) {
        result.is_valid = true;
        result.reason = "No behavior flags set (valid but unusual)";
        result.confidence = 40;
        return result;
    }
    
    // Check for invalid flag combinations
    DWORD vertex_processing_flags = flags & (D3DCREATE_SOFTWARE_VERTEXPROCESSING | 
                                            D3DCREATE_HARDWARE_VERTEXPROCESSING | 
                                            D3DCREATE_MIXED_VERTEXPROCESSING);
    
    int vertex_processing_count = 0;
    if (vertex_processing_flags & D3DCREATE_SOFTWARE_VERTEXPROCESSING) vertex_processing_count++;
    if (vertex_processing_flags & D3DCREATE_HARDWARE_VERTEXPROCESSING) vertex_processing_count++;
    if (vertex_processing_flags & D3DCREATE_MIXED_VERTEXPROCESSING) vertex_processing_count++;
    
    if (vertex_processing_count > 1) {
        result.reason = "Invalid: Multiple vertex processing flags set";
        result.confidence = -50;
        return result;
    }
    
    if (vertex_processing_count == 0) {
        result.reason = "Missing vertex processing flag (required)";
        result.confidence = 20;
        return result;
    }
    
    // Check for pure device with software vertex processing (invalid combination)
    if ((flags & D3DCREATE_PUREDEVICE) && (flags & D3DCREATE_SOFTWARE_VERTEXPROCESSING)) {
        result.reason = "Invalid: Pure device cannot use software vertex processing";
        result.confidence = -50;
        return result;
    }
    
    // Check for unknown/invalid flags
    DWORD unknown_flags = flags & ~VALID_FLAGS;
    if (unknown_flags != 0) {
        result.is_valid = true;
        result.reason = "Contains unknown flags (may be valid for newer versions d3d9?)";
        result.confidence = 30;
        return result;
    }
    
    // All flags are valid and properly combined
    result.is_valid = true;
    
    // Give higher confidence for common flag combinations
    if (flags == D3DCREATE_HARDWARE_VERTEXPROCESSING) {
        result.reason = "Standard hardware vertex processing";
        result.confidence = 95;
    } else if (flags == D3DCREATE_SOFTWARE_VERTEXPROCESSING) {
        result.reason = "Software vertex processing";
        result.confidence = 90;
    } else if (flags == D3DCREATE_MIXED_VERTEXPROCESSING) {
        result.reason = "Mixed vertex processing";
        result.confidence = 85;
    } else if (flags == (D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_PUREDEVICE)) {
        result.reason = "Pure hardware device (high performance)";
        result.confidence = 95;
    } else if (flags & D3DCREATE_MULTITHREADED) {
        result.reason = "Valid flags with multithreading";
        result.confidence = 85;
    } else {
        result.reason = "Valid flag combination";
        result.confidence = 80;
    }
    
    return result;
}

ValidationResult validate_display_width(const void* field_ptr, size_t field_size, const char* field_name) {
    return validate_uint_range(field_ptr, field_size, field_name, 1, 32768);
}

ValidationResult validate_display_height(const void* field_ptr, size_t field_size, const char* field_name) {
    return validate_uint_range(field_ptr, field_size, field_name, 1, 32768);
}

ValidationResult validate_refresh_rate(const void* field_ptr, size_t field_size, const char* field_name) {
    return validate_uint_range(field_ptr, field_size, field_name, 0, 240);
}

ValidationResult validate_adapter_ordinal(const void* field_ptr, size_t field_size, const char* field_name) {
    return validate_uint_range(field_ptr, field_size, field_name, 0, 16);
}

ValidationResult validate_backbuffer_count(const void* field_ptr, size_t field_size, const char* field_name) {
    return validate_uint_range(field_ptr, field_size, field_name, 1, 8);
}

ValidationResult validate_d3dmultisample_type(const void* field_ptr, size_t field_size, const char* field_name) {
    ValidationResult result = {false, "Invalid D3DMULTISAMPLE_TYPE", 0};
    
    if (field_size != sizeof(D3DMULTISAMPLE_TYPE)) {
        result.reason = "Invalid field size for D3DMULTISAMPLE_TYPE";
        return result;
    }
    
    D3DMULTISAMPLE_TYPE mstype = *(const D3DMULTISAMPLE_TYPE*)field_ptr;
    
    switch (mstype) {
        case D3DMULTISAMPLE_NONE:
            result.is_valid = true;
            result.reason = "No multisampling (most common)";
            result.confidence = 95;
            break;
        case D3DMULTISAMPLE_2_SAMPLES:
        case D3DMULTISAMPLE_3_SAMPLES:
        case D3DMULTISAMPLE_4_SAMPLES:
        case D3DMULTISAMPLE_5_SAMPLES:
        case D3DMULTISAMPLE_6_SAMPLES:
        case D3DMULTISAMPLE_7_SAMPLES:
        case D3DMULTISAMPLE_8_SAMPLES:
        case D3DMULTISAMPLE_9_SAMPLES:
        case D3DMULTISAMPLE_10_SAMPLES:
        case D3DMULTISAMPLE_11_SAMPLES:
        case D3DMULTISAMPLE_12_SAMPLES:
        case D3DMULTISAMPLE_13_SAMPLES:
        case D3DMULTISAMPLE_14_SAMPLES:
        case D3DMULTISAMPLE_15_SAMPLES:
        case D3DMULTISAMPLE_16_SAMPLES:
            result.is_valid = true;
            result.reason = "Valid multisampling level";
            result.confidence = 90;
            break;
        default:
            if ((DWORD)mstype <= 20) {
                result.is_valid = true;
                result.reason = "Plausible multisampling value";
                result.confidence = 40;
            } else {
                result.reason = "Invalid multisampling value";
                result.confidence = -25;
            }
            break;
    }
    
    return result;
}

ValidationResult validate_bool(const void* field_ptr, size_t field_size, const char* field_name) {
    ValidationResult result = {false, "Invalid BOOL value", 0};
    
    if (field_size != sizeof(BOOL)) {
        result.reason = "Invalid field size for BOOL";
        return result;
    }
    
    BOOL value = *(const BOOL*)field_ptr;
    
    if (value == TRUE) {
        result.is_valid = true;
        result.reason = "TRUE (valid)";
        result.confidence = 95;
    } else if (value == FALSE) {
        result.is_valid = true;
        result.reason = "FALSE (valid)";
        result.confidence = 95;
    } else if (value == 0) {
        result.is_valid = true;
        result.reason = "Zero (equivalent to FALSE)";
        result.confidence = 90;
    } else if (value == 1) {
        result.is_valid = true;
        result.reason = "One (equivalent to TRUE)";
        result.confidence = 90;
    } else if (value > 0 && value < 256) {
        result.is_valid = true;
        result.reason = "Non-zero (treated as TRUE)";
        result.confidence = 20;
    } else {
        result.reason = "Suspicious BOOL value";
        result.confidence = -20;
    }
    
    return result;
}

ValidationResult validate_primitive_misc_caps(const void* field_ptr, size_t field_size, const char* field_name) {
    const DWORD known_flags =
        D3DPMISCCAPS_MASKZ |
        D3DPMISCCAPS_CULLNONE |
        D3DPMISCCAPS_CULLCW |
        D3DPMISCCAPS_CULLCCW |
        D3DPMISCCAPS_COLORWRITEENABLE |
        D3DPMISCCAPS_CLIPPLANESCALEDPOINTS |
        D3DPMISCCAPS_CLIPTLVERTS |
        D3DPMISCCAPS_TSSARGTEMP |
        D3DPMISCCAPS_BLENDOP |
        D3DPMISCCAPS_NULLREFERENCE;

    ValidationResult result = {false, "Invalid PrimitiveMiscCaps value", 0};

    if (field_size != sizeof(DWORD)) {
        result.reason = "Invalid field size for PrimitiveMiscCaps";
        return result;
    }

    DWORD value = *(const DWORD*)field_ptr;
    DWORD unknown_flags = value & ~known_flags;

    if (unknown_flags == 0) {
        result.is_valid = true;
        result.reason = "All PrimitiveMiscCaps flags are valid";
        result.confidence = 90;
    } else {
        result.is_valid = true; // Still plausible, just suspicious
        result.reason = "Contains unknown PrimitiveMiscCaps flags";
        result.confidence = 40;
    }

    return result;
}

ValidationResult validate_raster_caps(const void* field_ptr, size_t field_size, const char* field_name) {
    const DWORD known_flags =
        D3DPRASTERCAPS_DITHER |
        D3DPRASTERCAPS_PAT |
        D3DPRASTERCAPS_ZTEST |
        D3DPRASTERCAPS_FOGVERTEX |
        D3DPRASTERCAPS_FOGTABLE |
        D3DPRASTERCAPS_ANTIALIASEDGES |
        D3DPRASTERCAPS_MIPMAPLODBIAS |
        D3DPRASTERCAPS_ZBIAS |
        D3DPRASTERCAPS_ZBUFFERLESSHSR |
        D3DPRASTERCAPS_FOGRANGE |
        D3DPRASTERCAPS_ANISOTROPY |
        D3DPRASTERCAPS_WBUFFER |
        D3DPRASTERCAPS_WFOG |
        D3DPRASTERCAPS_ZFOG |
        D3DPRASTERCAPS_COLORPERSPECTIVE |
        D3DPRASTERCAPS_STRETCHBLTMULTISAMPLE;

    ValidationResult result = {false, "Invalid RasterCaps value", 0};

    if (field_size != sizeof(DWORD)) {
        result.reason = "Invalid field size for RasterCaps";
        return result;
    }

    DWORD value = *(const DWORD*)field_ptr;
    DWORD unknown_flags = value & ~known_flags;

    if (unknown_flags == 0) {
        result.is_valid = true;
        result.reason = "All RasterCaps flags are valid";
        result.confidence = 90;
    } else {
        result.is_valid = true;
        result.reason = "Contains unknown RasterCaps flags";
        result.confidence = 40;
    }

    return result;
}

ValidationResult validate_present_flags(const void* field_ptr, size_t field_size, const char* field_name) {
    ValidationResult result = {false, "Invalid present flags", 0};
    
    if (field_size != sizeof(DWORD)) {
        result.reason = "Invalid field size for present flags";
        return result;
    }
    
    DWORD flags = *(const DWORD*)field_ptr;
    
    // D3D8 Present flags
    const DWORD VALID_FLAGS = D3DPRESENTFLAG_LOCKABLE_BACKBUFFER;
    
    if (flags == 0) {
        result.is_valid = true;
        result.reason = "No present flags (common)";
        result.confidence = 90;
        return result;
    }
    
    // Check for unknown flags
    DWORD unknown_flags = flags & ~VALID_FLAGS;
    if (unknown_flags != 0) {
        if (unknown_flags < 0x100) {
            result.is_valid = true;
            result.reason = "Contains unknown flags (may be valid)";
            result.confidence = 25;
        } else {
            result.reason = "Invalid flags detected";
            result.confidence = -10;
        }
        return result;
    }
    
    // All flags are known and valid
    result.is_valid = true;
    result.reason = "Valid present flags";
    result.confidence = 85;
    
    return result;
}

ValidationResult validate_viewport_coordinate(const void* field_ptr, size_t field_size, const char* field_name) {
    ValidationResult result = {false, "Invalid viewport coordinate", 0};
    
    if (field_size != sizeof(DWORD)) {
        result.reason = "Invalid field size for viewport coordinate";
        return result;
    }
    
    DWORD coord = *(const DWORD*)field_ptr;
    
    if (coord == 0) {
        result.is_valid = true;
        result.reason = "Zero coordinate (common for X/Y origin)";
        result.confidence = 90;
    } else if (coord <= 16384) { 
        result.is_valid = true;
        result.reason = "Valid screen coordinate";
        result.confidence = 90;
    } else if (coord <= 65536) {
        result.is_valid = true;
        result.reason = "Large but plausible coordinate";
        result.confidence = 60;
    } else {
        result.reason = "Unreasonably large coordinate";
        result.confidence = -10;
    }
    
    return result;
}

ValidationResult validate_viewport_dimension(const void* field_ptr, size_t field_size, const char* field_name) {
    ValidationResult result = {false, "Invalid viewport dimension", 0};
    
    if (field_size != sizeof(DWORD)) {
        result.reason = "Invalid field size for viewport dimension";
        return result;
    }
    
    DWORD dimension = *(const DWORD*)field_ptr;
    
    // Viewport dimensions must be > 0 and reasonable
    if (dimension == 0) {
        result.reason = "Zero dimension (invalid for viewport)";
        result.confidence = -50;
    } else if (dimension >= 1 && dimension <= 16384) {
        result.is_valid = true;
        result.reason = "Valid viewport dimension";
        result.confidence = 95;
    } else if (dimension <= 65536) {
        result.is_valid = true;
        result.reason = "Large but plausible dimension";
        result.confidence = 70;
    } else {
        result.reason = "Unreasonably large dimension";
        result.confidence = -25;
    }
    
    return result;
}

ValidationResult validate_z_depth(const void* field_ptr, size_t field_size, const char* field_name) {
    ValidationResult result = {false, "Invalid Z depth value", 0};
    
    if (field_size != sizeof(float)) {
        result.reason = "Invalid field size for float Z depth";
        return result;
    }
    
    float z_value = *(const float*)field_ptr;
    
    // Check for invalid float values (NaN, infinity)
    if (z_value != z_value) {  // NaN check
        result.reason = "Z depth is NaN";
        result.confidence = -5;
        return result;
    }
    
    if (z_value == INFINITE || z_value == -INFINITE) {
        result.reason = "Z depth is infinite";
        result.confidence = -5;
        return result;
    }
    
    // Standard D3D Z depth range is 0.0 to 1.0
    if (z_value >= 0.0f && z_value <= 1.0f) {
        if (z_value == 0.0f) {
            result.is_valid = true;
            result.reason = "MinZ = 0.0 (standard near plane)";
            result.confidence = 95;
        } else if (z_value == 1.0f) {
            result.is_valid = true;
            result.reason = "MaxZ = 1.0 (standard far plane)";
            result.confidence = 95;
        } else {
            result.is_valid = true;
            result.reason = "Valid Z depth in standard range";
            result.confidence = 90;
        }
    } else if (z_value > -10.0f && z_value < 10.0f) {
        // Some applications use custom Z ranges
        result.is_valid = true;
        result.reason = "Z depth in plausible custom range";
        result.confidence = 60;
    } else if (z_value > -1000.0f && z_value < 1000.0f) {
        result.is_valid = true;
        result.reason = "Z depth in extended range";
        result.confidence = 50;
    } else {
        result.reason = "Z depth outside reasonable range";
        result.confidence = -10;
    }
    
    return result;
}

ValidationResult validate_d3dswapeffect(const void* field_ptr, size_t field_size, const char* field_name) {
    ValidationResult result = {false, "Invalid D3DSWAPEFFECT", 0};
    
    if (field_size != sizeof(D3DSWAPEFFECT)) {
        result.reason = "Invalid field size for D3DSWAPEFFECT";
        return result;
    }
    
    D3DSWAPEFFECT swapeffect = *(const D3DSWAPEFFECT*)field_ptr;
    
    switch (swapeffect) {
        case D3DSWAPEFFECT_DISCARD:
            result.is_valid = true;
            result.reason = "Discard swap effect (most common)";
            result.confidence = 95;
            break;
        case D3DSWAPEFFECT_FLIP:
            result.is_valid = true;
            result.reason = "Flip swap effect";
            result.confidence = 90;
            break;
        case D3DSWAPEFFECT_COPY:
            result.is_valid = true;
            result.reason = "Copy swap effect";
            result.confidence = 85;
            break;
        case D3DSWAPEFFECT_COPY_VSYNC:
            result.is_valid = true;
            result.reason = "Copy with VSync swap effect";
            result.confidence = 80;
            break;
        default:
            if ((DWORD)swapeffect <= 10) {
                result.is_valid = true;
                result.reason = "Plausible swap effect value";
                result.confidence = 20;
            } else {
                result.reason = "Invalid swap effect";
                result.confidence = -5;
            }
            break;
    }
    
    return result;
}

ValidationResult validate_device_type_caps(const void* field_ptr, size_t field_size, const char* field_name) {
    return validate_d3ddevtype(field_ptr, field_size, field_name);
}

ValidationResult validate_adapter_ordinal_caps(const void* field_ptr, size_t field_size, const char* field_name) {
    return validate_adapter_ordinal(field_ptr, field_size, field_name);
}

ValidationResult validate_caps_flags(const void* field_ptr, size_t field_size, const char* field_name) {
    ValidationResult result = {false, "Invalid caps flags", 0};
    
    if (field_size != sizeof(DWORD)) {
        result.reason = "Invalid field size for DWORD caps flags";
        return result;
    }
    
    DWORD caps = *(const DWORD*)field_ptr;
    
    // D3DCAPS flags - these are the main device capability flags
    const DWORD VALID_CAPS = 
        D3DCAPS_READ_SCANLINE;                    // 0x00020000
    
    // Most caps will be 0 or have the READ_SCANLINE flag
    if (caps == 0) {
        result.is_valid = true;
        result.reason = "No caps flags (common)";
        result.confidence = 85;
    } else if (caps == D3DCAPS_READ_SCANLINE) {
        result.is_valid = true;
        result.reason = "READ_SCANLINE cap";
        result.confidence = 90;
    } else if ((caps & ~VALID_CAPS) == 0) {
        result.is_valid = true;
        result.reason = "Valid caps combination";
        result.confidence = 80;
    } else if (caps < 0x100000) {
        result.is_valid = true;
        result.reason = "Plausible caps value";
        result.confidence = 50;
    } else {
        result.reason = "Suspicious caps value";
        result.confidence = 20;
    }
    
    return result;
}

ValidationResult validate_caps2_flags(const void* field_ptr, size_t field_size, const char* field_name) {
    ValidationResult result = {false, "Invalid caps2 flags", 0};
    
    if (field_size != sizeof(DWORD)) {
        result.reason = "Invalid field size for DWORD caps2 flags";
        return result;
    }
    
    DWORD caps2 = *(const DWORD*)field_ptr;
    
    // D3DCAPS2 flags
    const DWORD VALID_CAPS2 = 
        D3DCAPS2_NO2DDURING3DSCENE |             // 0x00000002
        D3DCAPS2_FULLSCREENGAMMA |               // 0x00020000
        D3DCAPS2_CANRENDERWINDOWED |             // 0x00080000
        D3DCAPS2_CANCALIBRATEGAMMA |             // 0x00100000
        D3DCAPS2_RESERVED |                      // 0x02000000
        D3DCAPS2_CANMANAGERESOURCE |             // 0x10000000
        D3DCAPS2_DYNAMICTEXTURES;                // 0x20000000
    
    if (caps2 == 0) {
        result.is_valid = true;
        result.reason = "No caps2 flags";
        result.confidence = 70;
    } else if ((caps2 & ~VALID_CAPS2) == 0) {
        result.is_valid = true;
        result.reason = "Valid caps2 combination";
        result.confidence = 90;
    } else if (caps2 < 0x80000000) {
        result.is_valid = true;
        result.reason = "Plausible caps2 value";
        result.confidence = 50;
    } else {
        result.reason = "Suspicious caps2 value";
        result.confidence = 10;
    }
    
    return result;
}

ValidationResult validate_caps3_flags(const void* field_ptr, size_t field_size, const char* field_name) {
    ValidationResult result = {false, "Invalid caps3 flags", 0};
    
    if (field_size != sizeof(DWORD)) {
        result.reason = "Invalid field size for DWORD caps3 flags";
        return result;
    }
    
    DWORD caps3 = *(const DWORD*)field_ptr;
    
    // D3DCAPS3 flags
    const DWORD VALID_CAPS3 = 
        D3DCAPS3_ALPHA_FULLSCREEN_FLIP_OR_DISCARD |  // 0x00000020
        D3DCAPS3_RESERVED;                           // 0x8000001f
    
    if (caps3 == 0) {
        result.is_valid = true;
        result.reason = "No caps3 flags";
        result.confidence = 80;
    } else if ((caps3 & ~VALID_CAPS3) == 0) {
        result.is_valid = true;
        result.reason = "Valid caps3 combination";
        result.confidence = 85;
    } else if (caps3 < 0x100) {
        result.is_valid = true;
        result.reason = "Plausible caps3 value";
        result.confidence = 50;
    } else {
        result.reason = "Suspicious caps3 value";
        result.confidence = 25;
    }
    
    return result;
}

ValidationResult validate_presentation_intervals(const void* field_ptr, size_t field_size, const char* field_name) {
    ValidationResult result = {false, "Invalid presentation intervals", 0};
    
    if (field_size != sizeof(DWORD)) {
        result.reason = "Invalid field size for presentation intervals";
        return result;
    }
    
    DWORD intervals = *(const DWORD*)field_ptr;
    
    // D3DPRESENT interval flags
    const DWORD VALID_INTERVALS = 
        D3DPRESENT_INTERVAL_DEFAULT |            // 0x00000000
        D3DPRESENT_INTERVAL_ONE |                // 0x00000001
        D3DPRESENT_INTERVAL_TWO |                // 0x00000002
        D3DPRESENT_INTERVAL_THREE |              // 0x00000004
        D3DPRESENT_INTERVAL_FOUR |               // 0x00000008
        D3DPRESENT_INTERVAL_IMMEDIATE;           // 0x80000000
    
    if (intervals == 0 || intervals == D3DPRESENT_INTERVAL_DEFAULT) {
        result.is_valid = true;
        result.reason = "Default presentation interval";
        result.confidence = 90;
    } else if ((intervals & ~VALID_INTERVALS) == 0) {
        result.is_valid = true;
        result.reason = "Valid presentation intervals";
        result.confidence = 95;
    } else if (intervals < 0x100) {
        result.is_valid = true;
        result.reason = "Plausible presentation interval";
        result.confidence = 60;
    } else {
        result.reason = "Invalid presentation intervals";
        result.confidence = 20;
    }
    
    return result;
}

ValidationResult validate_cursor_caps(const void* field_ptr, size_t field_size, const char* field_name) {
    ValidationResult result = {false, "Invalid cursor caps", 0};
    
    if (field_size != sizeof(DWORD)) {
        result.reason = "Invalid field size for cursor caps";
        return result;
    }
    
    DWORD cursor_caps = *(const DWORD*)field_ptr;
    
    // D3DCURSORCAPS flags
    const DWORD VALID_CURSOR_CAPS = 
        D3DCURSORCAPS_COLOR |                    // 0x00000001
        D3DCURSORCAPS_LOWRES;                    // 0x00000002
    
    if (cursor_caps == 0) {
        result.is_valid = true;
        result.reason = "No cursor capabilities";
        result.confidence = 85;
    } else if ((cursor_caps & ~VALID_CURSOR_CAPS) == 0) {
        result.is_valid = true;
        result.reason = "Valid cursor capabilities";
        result.confidence = 90;
    } else if (cursor_caps < 0x10) {
        result.is_valid = true;
        result.reason = "Plausible cursor caps";
        result.confidence = 50;
    } else {
        result.reason = "Invalid cursor capabilities";
        result.confidence = 20;
    }
    
    return result;
}

ValidationResult validate_dev_caps(const void* field_ptr, size_t field_size, const char* field_name) {
    ValidationResult result = {false, "Invalid device caps", 0};
    
    if (field_size != sizeof(DWORD)) {
        result.reason = "Invalid field size for device caps";
        return result;
    }
    
    DWORD dev_caps = *(const DWORD*)field_ptr;
    
    // Common D3DDEVCAPS flags
    const DWORD COMMON_DEV_CAPS = 
        D3DDEVCAPS_EXECUTESYSTEMMEMORY |         // 0x00000010
        D3DDEVCAPS_EXECUTEVIDEOMEMORY |          // 0x00000020
        D3DDEVCAPS_TLVERTEXSYSTEMMEMORY |        // 0x00000040
        D3DDEVCAPS_TLVERTEXVIDEOMEMORY |         // 0x00000080
        D3DDEVCAPS_TEXTURESYSTEMMEMORY |         // 0x00000100
        D3DDEVCAPS_TEXTUREVIDEOMEMORY |          // 0x00000200
        D3DDEVCAPS_DRAWPRIMTLVERTEX |            // 0x00000400
        D3DDEVCAPS_CANRENDERAFTERFLIP |          // 0x00000800
        D3DDEVCAPS_TEXTURENONLOCALVIDMEM |       // 0x00001000
        D3DDEVCAPS_DRAWPRIMITIVES2 |             // 0x00002000
        D3DDEVCAPS_SEPARATETEXTUREMEMORIES |     // 0x00004000
        D3DDEVCAPS_DRAWPRIMITIVES2EX |           // 0x00008000
        D3DDEVCAPS_HWTRANSFORMANDLIGHT |         // 0x00010000
        D3DDEVCAPS_CANBLTSYSTONONLOCAL |         // 0x00020000
        D3DDEVCAPS_HWRASTERIZATION |             // 0x00080000
        D3DDEVCAPS_PUREDEVICE |                  // 0x00100000
        D3DDEVCAPS_QUINTICRTPATCHES |            // 0x00200000
        D3DDEVCAPS_RTPATCHES |                   // 0x00400000
        D3DDEVCAPS_RTPATCHHANDLEZERO |           // 0x00800000
        D3DDEVCAPS_NPATCHES;                     // 0x01000000
    
    if (dev_caps == 0) {
        result.reason = "No device capabilities (suspicious)";
        result.confidence = 30;
    } else {
        // Check for essential caps that most devices should have
        bool has_essential_caps = (dev_caps & (D3DDEVCAPS_DRAWPRIMITIVES2 | D3DDEVCAPS_HWRASTERIZATION));
        
        if (has_essential_caps) {
            result.is_valid = true;
            result.reason = "Valid device capabilities";
            result.confidence = 90;
        } else if ((dev_caps & ~COMMON_DEV_CAPS) == 0) {
            result.is_valid = true;
            result.reason = "Recognized device caps";
            result.confidence = 80;
        } else if (dev_caps < 0x10000000) {
            result.is_valid = true;
            result.reason = "Plausible device caps";
            result.confidence = 60;
        } else {
            result.reason = "Suspicious device capabilities";
            result.confidence = 30;
        }
    }
    
    return result;
}

ValidationResult validate_memory_size(const void* field_ptr, size_t field_size, const char* field_name) {
    ValidationResult result = {false, "Invalid memory size", 0};
    
    if (field_size != sizeof(DWORD)) {
        result.reason = "Invalid field size for memory size";
        return result;
    }
    
    DWORD memory_size = *(const DWORD*)field_ptr;
    
    if (memory_size == 0) {
        result.is_valid = true;
        result.reason = "Zero memory (AGP/integrated graphics)";
        result.confidence = 70;
    } else if (memory_size >= 1024*1024 && memory_size <= 1024*1024*1024) { // 1MB to 1GB
        result.is_valid = true;
        if (memory_size >= 16*1024*1024) {
            result.reason = "Typical video memory size";
            result.confidence = 95;
        } else {
            result.reason = "Small but valid video memory";
            result.confidence = 80;
        }
    } else if (memory_size < 1024*1024) {
        result.reason = "Unusually small memory size";
        result.confidence = 40;
    } else {
        result.reason = "Unreasonably large memory size";
        result.confidence = 20;
    }
    
    return result;
}

ValidationResult validate_max_texture_dimension(const void* field_ptr, size_t field_size, const char* field_name) {
    ValidationResult result = {false, "Invalid texture dimension", 0};
    
    if (field_size != sizeof(DWORD)) {
        result.reason = "Invalid field size for texture dimension";
        return result;
    }
    
    DWORD dimension = *(const DWORD*)field_ptr;
    
    // Common power-of-2 texture sizes for D3D8 era
    if (dimension == 0) {
        result.reason = "Zero texture dimension (invalid)";
        result.confidence = -50;
    } else if (dimension >= 64 && dimension <= 16384) {
        // Check if it's a power of 2
        bool is_power_of_2 = (dimension & (dimension - 1)) == 0;
        if (is_power_of_2) {
            result.is_valid = true;
            result.reason = "Valid power-of-2 texture dimension";
            result.confidence = 95;
        } else {
            result.is_valid = true;
            result.reason = "Valid texture dimension (not power-of-2)";
            result.confidence = 80;
        }
    } else if (dimension < 64) {
        result.is_valid = true;
        result.reason = "Small texture dimension";
        result.confidence = 70;
    } else if (dimension <= 32768) {
        result.is_valid = true;
        result.reason = "Large texture dimension";
        result.confidence = 70;
    } else {
        result.reason = "Unreasonably large texture dimension";
        result.confidence = 10;
    }
    
    return result;
}

ValidationResult validate_max_vertex_index(const void* field_ptr, size_t field_size, const char* field_name) {
    ValidationResult result = {false, "Invalid vertex index", 0};
    
    if (field_size != sizeof(DWORD)) {
        result.reason = "Invalid field size for vertex index";
        return result;
    }
    
    DWORD max_index = *(const DWORD*)field_ptr;
    
    if (max_index == 0) {
        result.reason = "Zero max vertex index (invalid)";
        result.confidence = -30;
    } else if (max_index >= 65536 && max_index <= 16777216) { // 64K to 16M vertices
        result.is_valid = true;
        result.reason = "Reasonable max vertex index";
        result.confidence = 90;
    } else if (max_index < 65536) {
        result.is_valid = true;
        result.reason = "Low max vertex index";
        result.confidence = 70;
    } else if (max_index <= 0xFFFFFFFF) {
        result.is_valid = true;
        result.reason = "High max vertex index";
        result.confidence = 80;
    } else {
        result.reason = "Invalid vertex index value";
        result.confidence = -10;
    }
    
    return result;
}

ValidationResult validate_max_streams(const void* field_ptr, size_t field_size, const char* field_name) {
    ValidationResult result = {false, "Invalid max streams", 0};
    
    if (field_size != sizeof(DWORD)) {
        result.reason = "Invalid field size for max streams";
        return result;
    }
    
    DWORD max_streams = *(const DWORD*)field_ptr;
    
    if (max_streams == 0) {
        result.reason = "Zero max streams (invalid)";
        result.confidence = -40;
    } else if (max_streams >= 1 && max_streams <= 16) {
        result.is_valid = true;
        result.reason = "Valid max vertex streams";
        result.confidence = 95;
    } else if (max_streams <= 32) {
        result.is_valid = true;
        result.reason = "High but plausible max streams";
        result.confidence = 70;
    } else {
        result.reason = "Unreasonably high max streams";
        result.confidence = 20;
    }
    
    return result;
}

ValidationResult validate_shader_version(const void* field_ptr, size_t field_size, const char* field_name) {
    ValidationResult result = {false, "Invalid shader version", 0};
    
    if (field_size != sizeof(DWORD)) {
        result.reason = "Invalid field size for shader version";
        return result;
    }
    
    DWORD version = *(const DWORD*)field_ptr;
    
    // D3D8 shader versions: major in high byte, minor in low byte
    DWORD major = (version >> 8) & 0xFF;
    DWORD minor = version & 0xFF;
    
    if (version == 0) {
        result.is_valid = true;
        result.reason = "No shader support";
        result.confidence = 80;
    } else if (major == 1 && minor <= 4) { // D3D8 supports up to 1.4
        result.is_valid = true;
        result.reason = "Valid D3D8 shader version";
        result.confidence = 95;
    } else if (major <= 3 && minor <= 10) { // Later versions
        result.is_valid = true;
        result.reason = "Later shader version (D3D9+)";
        result.confidence = 70;
    } else if (version < 0x10000) {
        result.is_valid = true;
        result.reason = "Plausible shader version";
        result.confidence = 50;
    } else {
        result.reason = "Invalid shader version format";
        result.confidence = 20;
    }
    
    return result;
}

ValidationResult validate_texture_caps(const void* field_ptr, size_t field_size, const char* field_name) {
    ValidationResult result = {false, "Invalid texture caps", 0};
    
    if (field_size != sizeof(DWORD)) {
        result.reason = "Invalid field size for texture caps";
        return result;
    }
    
    DWORD caps = *(const DWORD*)field_ptr;

    const DWORD ALL_VALID_CAPS = D3DPTEXTURECAPS_PERSPECTIVE |
                                 D3DPTEXTURECAPS_POW2 |
                                 D3DPTEXTURECAPS_ALPHA |
                                 D3DPTEXTURECAPS_SQUAREONLY |
                                 D3DPTEXTURECAPS_TEXREPEATNOTSCALEDBYSIZE |
                                 D3DPTEXTURECAPS_ALPHAPALETTE |
                                 D3DPTEXTURECAPS_NONPOW2CONDITIONAL |
                                 D3DPTEXTURECAPS_PROJECTED |
                                 D3DPTEXTURECAPS_CUBEMAP |
                                 D3DPTEXTURECAPS_VOLUMEMAP |
                                 D3DPTEXTURECAPS_MIPMAP |
                                 D3DPTEXTURECAPS_MIPVOLUMEMAP |
                                 D3DPTEXTURECAPS_MIPCUBEMAP |
                                 D3DPTEXTURECAPS_CUBEMAP_POW2 |
                                 D3DPTEXTURECAPS_VOLUMEMAP_POW2; 
 
    if (caps == 0) {
        result.is_valid = true;
        result.reason = "No texture capabilities";
        result.confidence = 60;
    } else if ((caps & ~ALL_VALID_CAPS) == 0) {
        // All bits are valid D3D8 texture caps
        result.is_valid = true;
        result.reason = "Valid D3D8 texture caps";
        result.confidence = 95;
        
        // Check for logical inconsistencies
        if ((caps & D3DPTEXTURECAPS_MIPCUBEMAP) && !(caps & D3DPTEXTURECAPS_CUBEMAP)) {
            result.confidence = 80;
            result.reason = "Valid caps but MIPCUBEMAP without CUBEMAP";
        } else if ((caps & D3DPTEXTURECAPS_MIPVOLUMEMAP) && !(caps & D3DPTEXTURECAPS_VOLUMEMAP)) {
            result.confidence = 80;
            result.reason = "Valid caps but MIPVOLUMEMAP without VOLUMEMAP";
        } else if ((caps & D3DPTEXTURECAPS_CUBEMAP_POW2) && !(caps & D3DPTEXTURECAPS_CUBEMAP)) {
            result.confidence = 80;
            result.reason = "Valid caps but CUBEMAP_POW2 without CUBEMAP";
        } else if ((caps & D3DPTEXTURECAPS_VOLUMEMAP_POW2) && !(caps & D3DPTEXTURECAPS_VOLUMEMAP)) {
            result.confidence = 80;
            result.reason = "Valid caps but VOLUMEMAP_POW2 without VOLUMEMAP";
        }
    } else {
        // Some invalid bits are set
        DWORD invalid_bits = caps & ~ALL_VALID_CAPS;
        
        if ((invalid_bits & 0xFFFF0000) == 0) {
            // Only lower 16 bits have invalid flags - might be newer D3D version
            result.is_valid = true;
            result.reason = "Seems to contains unknown D3D texture caps";
            result.confidence = 70;
        } else if ((caps & ALL_VALID_CAPS) != 0) {
            // Has some valid caps mixed with invalid ones
            result.is_valid = true;
            result.reason = "Mixed valid/invalid texture caps";
            result.confidence = 50;
        } else {
            // Mostly invalid
            result.reason = "Invalid texture caps flags";
            result.confidence = -50;
        }
    }
    
    return result;
}


ValidationResult validate_driver_string(const void* field_ptr, size_t field_size, const char* field_name) {
    ValidationResult result = {false, "Invalid driver string", 0};
    
    if (field_size != MAX_DEVICE_IDENTIFIER_STRING) {
        result.reason = "Invalid field size for driver string";
        return result;
    }
    
    const char* driver_str = (const char*)field_ptr;
    
    // Check if string is null-terminated within the buffer
    size_t len = strnlen(driver_str, MAX_DEVICE_IDENTIFIER_STRING);
    
    if (len == 0) {
        result.reason = "Empty driver string";
        result.confidence = 30;
    } else if (len < MAX_DEVICE_IDENTIFIER_STRING) {
        // Check for reasonable characters
        bool has_printable = true;
        for (size_t i = 0; i < len; i++) {
            if (driver_str[i] < 32 || driver_str[i] > 126) {
                has_printable = false;
                break;
            }
        }
        
        if (has_printable) {
            result.is_valid = true;
            result.reason = "Valid driver string";
            result.confidence = 90;
        } else {
            result.reason = "Contains non-printable characters";
            result.confidence = 40;
        }
    } else {
        result.reason = "String not null-terminated";
        result.confidence = 20;
    }
    
    return result;
}

ValidationResult validate_driver_version(const void* field_ptr, size_t field_size, const char* field_name) {
    ValidationResult result = {false, "Invalid driver version", 0};
    
    if (field_size != sizeof(LARGE_INTEGER)) {
        result.reason = "Invalid field size for driver version";
        return result;
    }
    
    LARGE_INTEGER version = *(const LARGE_INTEGER*)field_ptr;
    
    if (version.QuadPart == 0) {
        result.is_valid = true;
        result.reason = "Zero driver version";
        result.confidence = 60;
    } else if (version.QuadPart > 0 && version.QuadPart < 0x1000000000000LL) {
        result.is_valid = true;
        result.reason = "Plausible driver version";
        result.confidence = 85;
    } else {
        result.reason = "Suspicious driver version";
        result.confidence = 30;
    }
    
    return result;
}

ValidationResult validate_vendor_id(const void* field_ptr, size_t field_size, const char* field_name) {
    ValidationResult result = {false, "Invalid vendor ID", 0};
    
    if (field_size != sizeof(DWORD)) {
        result.reason = "Invalid field size for vendor ID";
        return result;
    }
    
    DWORD vendor_id = *(const DWORD*)field_ptr;
    
    // Common GPU vendor IDs
    switch (vendor_id) {
        case 0x10DE: // NVIDIA
            result.is_valid = true;
            result.reason = "NVIDIA vendor ID";
            result.confidence = 95;
            break;
        case 0x1002: // AMD/ATI
            result.is_valid = true;
            result.reason = "AMD/ATI vendor ID";
            result.confidence = 95;
            break;
        case 0x8086: // Intel
            result.is_valid = true;
            result.reason = "Intel vendor ID";
            result.confidence = 95;
            break;
        case 0x1039: // SiS
        case 0x5333: // S3 Graphics
        case 0x102B: // Matrox
        case 0x121A: // 3dfx
            result.is_valid = true;
            result.reason = "Known GPU vendor ID";
            result.confidence = 90;
            break;
        default:
            if (vendor_id > 0x1000 && vendor_id < 0xFFFF) {
                result.is_valid = true;
                result.reason = "Plausible vendor ID";
                result.confidence = 60;
            } else {
                result.reason = "Unknown vendor ID";
                result.confidence = 30;
            }
            break;
    }
    
    return result;
}


ValidationResult validate_guid(const void* field_ptr, size_t field_size, const char* field_name) {
    ValidationResult result = {false, "Invalid GUID", 0};
    if(!field_ptr) return result;

    const GUID* guid = (const GUID*) field_ptr;
    
    if((guid->Data4[0] & 0xC0) != 0x80)
    {
        return result;
    }

    uint16_t version = guid->Data3 >> 12;
    if(version < 1 || version > 5) return result;
    result.is_valid = true;
    result.confidence = 70;
    result.reason = "Possibly valid GUID";
    return result;
}


ValidationResult validate_whqllevel(const void* field_ptr, size_t field_size, const char* field_name) {
    ValidationResult result = {false, "Invalid WHQLLevel", 0};
    if(!field_ptr) return result;

    uint32_t whql = *(const uint32_t *) field_ptr;
    if(whql == 0 || whql == 1)
    {
        result.is_valid = true;
        result.confidence = 50;
        result.reason = "Possibly valid WHQL";
        return result;
    }

    uint16_t year = (whql >> 16) && 0xFFFF;
    uint8_t month = (whql >> 8) && 0xFF;
    uint8_t day = whql & 0xFF;

    if(year < 1999 || month == 0 || month > 12 || day == 0 || day > 31)
    {
        return result;
    }

    result.is_valid = true;
    result.confidence = 90;
    result.reason = "WHQL Level is valid year/month/day value";

    return result;  
}

#define FIELD_DESC(struct_type, field_name, validator_func) \
    { offsetof(struct_type, field_name), sizeof(((struct_type*)0)->field_name), #field_name, validator_func }

static const FieldDescriptor d3dadapter_identifier8_fields[] = {
    FIELD_DESC(D3DADAPTER_IDENTIFIER8, Driver, validate_driver_string),
    FIELD_DESC(D3DADAPTER_IDENTIFIER8, Description, validate_driver_string),
    FIELD_DESC(D3DADAPTER_IDENTIFIER8, DriverVersion, validate_driver_version),
    FIELD_DESC(D3DADAPTER_IDENTIFIER8, VendorId, validate_vendor_id),
    FIELD_DESC(D3DADAPTER_IDENTIFIER8, DeviceId, validate_vendor_id), 
    FIELD_DESC(D3DADAPTER_IDENTIFIER8, SubSysId, validate_vendor_id),
    FIELD_DESC(D3DADAPTER_IDENTIFIER8, Revision, validate_vendor_id), 

    FIELD_DESC(D3DADAPTER_IDENTIFIER8, DeviceIdentifier, validate_guid), 
    FIELD_DESC(D3DADAPTER_IDENTIFIER8, WHQLLevel, validate_whqllevel), 

};

static const FieldDescriptor d3dcaps8_fields[] = {
    FIELD_DESC(D3DCAPS8, DeviceType, validate_device_type_caps),
    FIELD_DESC(D3DCAPS8, AdapterOrdinal, validate_adapter_ordinal_caps),
    FIELD_DESC(D3DCAPS8, Caps, validate_caps_flags),
    FIELD_DESC(D3DCAPS8, Caps2, validate_caps2_flags),
    FIELD_DESC(D3DCAPS8, Caps3, validate_caps3_flags),
    FIELD_DESC(D3DCAPS8, PresentationIntervals, validate_presentation_intervals),
    FIELD_DESC(D3DCAPS8, CursorCaps, validate_cursor_caps),
    FIELD_DESC(D3DCAPS8, DevCaps, validate_dev_caps),
    FIELD_DESC(D3DCAPS8, PrimitiveMiscCaps, validate_primitive_misc_caps),
    FIELD_DESC(D3DCAPS8, RasterCaps, validate_raster_caps),
    FIELD_DESC(D3DCAPS8, TextureCaps, validate_texture_caps),
    FIELD_DESC(D3DCAPS8, MaxTextureWidth, validate_max_texture_dimension),
    FIELD_DESC(D3DCAPS8, MaxTextureHeight, validate_max_texture_dimension),
    FIELD_DESC(D3DCAPS8, MaxVolumeExtent, validate_max_texture_dimension),
    FIELD_DESC(D3DCAPS8, MaxTextureRepeat, validate_max_texture_dimension),
    FIELD_DESC(D3DCAPS8, MaxVertexIndex, validate_max_vertex_index),
    FIELD_DESC(D3DCAPS8, MaxStreams, validate_max_streams),
    FIELD_DESC(D3DCAPS8, VertexShaderVersion, validate_shader_version),
    FIELD_DESC(D3DCAPS8, PixelShaderVersion, validate_shader_version),
};

static const FieldDescriptor d3ddisplaymode_fields[] = {
    FIELD_DESC(D3DDISPLAYMODE, Width, validate_display_width),
    FIELD_DESC(D3DDISPLAYMODE, Height, validate_display_height),
    FIELD_DESC(D3DDISPLAYMODE, RefreshRate, validate_refresh_rate),
    FIELD_DESC(D3DDISPLAYMODE, Format, validate_d3dformat)
};

static const FieldDescriptor d3ddevice_creation_params_fields[] = {
    FIELD_DESC(D3DDEVICE_CREATION_PARAMETERS, AdapterOrdinal, validate_adapter_ordinal),
    FIELD_DESC(D3DDEVICE_CREATION_PARAMETERS, DeviceType, validate_d3ddevtype),
    FIELD_DESC(D3DDEVICE_CREATION_PARAMETERS, hFocusWindow, validate_hwnd),
    FIELD_DESC(D3DDEVICE_CREATION_PARAMETERS, BehaviorFlags, validate_flags),
};

static const FieldDescriptor d3dpresent_parameters_fields[] = {
    FIELD_DESC(D3DPRESENT_PARAMETERS, BackBufferWidth, validate_display_width),
    FIELD_DESC(D3DPRESENT_PARAMETERS, BackBufferHeight, validate_display_height),
    FIELD_DESC(D3DPRESENT_PARAMETERS, BackBufferFormat, validate_d3dformat),
    FIELD_DESC(D3DPRESENT_PARAMETERS, BackBufferCount, validate_backbuffer_count),
    FIELD_DESC(D3DPRESENT_PARAMETERS, MultiSampleType, validate_d3dmultisample_type),
    FIELD_DESC(D3DPRESENT_PARAMETERS, SwapEffect, validate_d3dswapeffect),
    FIELD_DESC(D3DPRESENT_PARAMETERS, hDeviceWindow, validate_hwnd),
    FIELD_DESC(D3DPRESENT_PARAMETERS, Windowed, validate_bool),
    FIELD_DESC(D3DPRESENT_PARAMETERS, EnableAutoDepthStencil, validate_bool),
    FIELD_DESC(D3DPRESENT_PARAMETERS, AutoDepthStencilFormat, validate_d3dformat),
    FIELD_DESC(D3DPRESENT_PARAMETERS, Flags, validate_present_flags)
};

static const FieldDescriptor d3dviewport8_fields[] = {
    FIELD_DESC(D3DVIEWPORT8, X, validate_viewport_coordinate),
    FIELD_DESC(D3DVIEWPORT8, Y, validate_viewport_coordinate),
    FIELD_DESC(D3DVIEWPORT8, Width, validate_viewport_dimension),
    FIELD_DESC(D3DVIEWPORT8, Height, validate_viewport_dimension),
    FIELD_DESC(D3DVIEWPORT8, MinZ, validate_z_depth),
    FIELD_DESC(D3DVIEWPORT8, MaxZ, validate_z_depth)
};




static const StructDescriptor known_structures[] = {
    {
        "D3DDISPLAYMODE",
        sizeof(D3DDISPLAYMODE),
        d3ddisplaymode_fields,
        sizeof(d3ddisplaymode_fields) / sizeof(d3ddisplaymode_fields[0])
    },
    {
        "D3DDEVICE_CREATION_PARAMETERS",
        sizeof(D3DDEVICE_CREATION_PARAMETERS),
        d3ddevice_creation_params_fields,
        sizeof(d3ddevice_creation_params_fields) / sizeof(d3ddevice_creation_params_fields[0])
    },
    {
        "D3DPRESENT_PARAMETERS",
        sizeof(D3DPRESENT_PARAMETERS),
        d3dpresent_parameters_fields,
        sizeof(d3dpresent_parameters_fields) / sizeof(d3dpresent_parameters_fields[0])
    },
    {
        "D3DVIEWPORT8",
        sizeof(D3DVIEWPORT8),
        d3dviewport8_fields,
        sizeof(d3dviewport8_fields) / sizeof(d3dviewport8_fields[0])
    },
    {
        "D3DADAPTER_IDENTIFIER8", 
        sizeof(D3DADAPTER_IDENTIFIER8),
        d3dadapter_identifier8_fields,
        sizeof(d3dadapter_identifier8_fields) / sizeof(d3dadapter_identifier8_fields[0])
    },
    {
        "D3DCAPS8",
        sizeof(D3DCAPS8),
        d3dcaps8_fields,
        sizeof(d3dcaps8_fields) / sizeof(d3dcaps8_fields[0])
    }
};

const char* DeviceTypeToString(D3DDEVTYPE type) {
    switch (type) {
        case D3DDEVTYPE_HAL: return "D3DDEVTYPE_HAL";
        case D3DDEVTYPE_REF: return "D3DDEVTYPE_REF";
        case D3DDEVTYPE_SW:  return "D3DDEVTYPE_SW";
        case D3DDEVTYPE_FORCE_DWORD: return "D3DDEVTYPE_FORCE_DWORD";
        default: return "UNKNOWN_DEVICE_TYPE";
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

bool ValidateStructure(const void* memory_addr, const StructDescriptor* desc, const char* log_category) {
    if (!memory_addr || !desc) {
        WriteLogf(log_category, "[-] NULL parameters");
        return false;
    }

    void* buffer = malloc(desc->struct_size);
    if (!buffer) {
        WriteLogf(log_category, "[-] Failed to allocate buffer");
        return false;
    }

    if (!SafeReadMemory(memory_addr, buffer, desc->struct_size)) {
        WriteLogf(log_category, "[-] Failed to read memory for %s", desc->struct_name);
        free(buffer);
        return false;
    }

    WriteLogf(log_category, "[+] Validating %s at 0x%p", desc->struct_name, memory_addr);
    
    bool overall_valid = true;
    int total_confidence = 0;
    int field_count = 0;

    for (size_t i = 0; i < desc->field_count; i++) {
        const FieldDescriptor* field = &desc->fields[i];
        const void* field_ptr = (const char*)buffer + field->offset;
        
        ValidationResult result = field->validator(field_ptr, field->size, field->name);
        
        WriteLogf(log_category, "  %s: %s (confidence: %d%%)",
                  field->name, result.reason, result.confidence);
        
        if (!result.is_valid && result.confidence < 50) {
            overall_valid = false;
        }
        
        total_confidence += result.confidence;
        field_count++;
    }

    int avg_confidence = field_count > 0 ? total_confidence / field_count : 0;
    
    WriteLogf(log_category, "[%c] %s validation %s (avg confidence: %d%%)",
              overall_valid ? '+' : '-',
              desc->struct_name,
              overall_valid ? "PASSED" : "FAILED",
              avg_confidence);

    free(buffer);
    return overall_valid && avg_confidence >= 60;
}

bool ValidateD3DDisplayMode(const void* memory_addr, const char* log_category) {
    return ValidateStructure(memory_addr, &known_structures[0], log_category);
}

bool ValidateD3DDeviceCreationParams(const void* memory_addr, const char* log_category) {
    return ValidateStructure(memory_addr, &known_structures[1], log_category);
}

typedef struct {
    const void* address;           
    uint64_t value;               
    size_t value_size;            
    const char* best_datatype;    
    int best_confidence;        
    int num_potential_matches;    
} ScanResult;

uint64_t ExtractRawValue(const void* addr, size_t size) {
    uint64_t value = 0;
    if (size > sizeof(uint64_t)) size = sizeof(uint64_t);
    
    memcpy(&value, addr, size);
    return value;
}

void FormatRawValue(uint64_t value, size_t size, char* buffer, size_t buffer_size) {
    switch (size) {
        case 1:
            snprintf(buffer, buffer_size, "0x%02X (%d)", (uint8_t)value, (int8_t)value);
            break;
        case 2:
            snprintf(buffer, buffer_size, "0x%04X (%d)", (uint16_t)value, (int16_t)value);
            break;
        case 4:
            snprintf(buffer, buffer_size, "0x%08X (%d)", (uint32_t)value, (int32_t)value);
            break;
        case 8:
            snprintf(buffer, buffer_size, "0x%016llX (%lld)", value, (int64_t)value);
            break;
        default:
            snprintf(buffer, buffer_size, "0x%llX", value);
            break;
    }
}

typedef struct {
    const void* address;
    const StructDescriptor* desc;
    int confidence;
    void* buffer; 
} IdentifiedStruct;

void ScanMemoryForStructures(const void* start_addr, size_t scan_size, const char* log_category) {
    const size_t num_structures = sizeof(known_structures) / sizeof(known_structures[0]);
    
    WriteLogf(log_category, "[+] Scanning memory region 0x%p - 0x%p (%zu bytes)", 
              start_addr, (const char*)start_addr + scan_size, scan_size);
    
    const size_t max_results = (scan_size / sizeof(DWORD)) + 1;
    ScanResult* results = (ScanResult*)calloc(max_results, sizeof(ScanResult));
    IdentifiedStruct* identified_structs = (IdentifiedStruct*)calloc(max_results, sizeof(IdentifiedStruct));
    
    if (!results || !identified_structs) {
        WriteLogf(log_category, "[-] Failed to allocate results buffer");
        if (results) free(results);
        if (identified_structs) free(identified_structs);
        return;
    }
    
    size_t result_count = 0;
    size_t identified_count = 0;
    
    for (size_t offset = 0; offset < scan_size;) {
        const void* test_addr = (const char*)start_addr + offset;
        
        ScanResult* current_result = &results[result_count];
        current_result->address = test_addr;
        current_result->best_confidence = -1;
        current_result->best_datatype = "UNKNOWN";
        current_result->num_potential_matches = 0;
        
        size_t remaining_size = scan_size - offset;
        
        if (remaining_size < sizeof(DWORD)) {
            current_result->value_size = remaining_size;
            if (IsValidMemoryRegion(test_addr, remaining_size)) {
                current_result->value = ExtractRawValue(test_addr, remaining_size);
            }
            result_count++;
            offset++;
            continue;
        }
        
        const StructDescriptor* best_struct = NULL;
        void* best_buffer = NULL;
        size_t best_struct_size = 0;
        
        for (size_t i = 0; i < num_structures; i++) {
            const StructDescriptor* desc = &known_structures[i];
            
            if (offset + desc->struct_size > scan_size) continue;
            
            void* buffer = malloc(desc->struct_size);
            if (!buffer) continue;
            
            if (!SafeReadMemory(test_addr, buffer, desc->struct_size)) {
                free(buffer);
                continue;
            }
            
            int total_confidence = 0;
            int field_count = 0;
            bool all_fields_valid = true;
            int passing_fields = 0;
            
            for (size_t j = 0; j < desc->field_count; j++) {
                const FieldDescriptor* field = &desc->fields[j];
                const void* field_ptr = (const char*)buffer + field->offset;
                
                ValidationResult result = field->validator(field_ptr, field->size, field->name);
                
                total_confidence += result.confidence;
                field_count++;
                
                if (result.is_valid && result.confidence >= 50) {
                    passing_fields++;
                } else if (!result.is_valid && result.confidence < 30) {
                    all_fields_valid = false;
                }
            }
            
            int avg_confidence = field_count > 0 ? total_confidence / field_count : 0;
            
            bool is_potential_match = false;
            
            if (all_fields_valid && avg_confidence >= 60) {
                is_potential_match = true;
            } else if (passing_fields >= (field_count / 2) && avg_confidence >= 45) {
                is_potential_match = true;
            } else if (field_count == 1 && avg_confidence >= 40) {
                is_potential_match = true;
            }
            
            if (is_potential_match) {
                current_result->num_potential_matches++;
                
                if (avg_confidence > current_result->best_confidence) {
                    current_result->best_confidence = avg_confidence;
                    current_result->best_datatype = desc->struct_name;
                    current_result->value_size = desc->struct_size;
                    current_result->value = ExtractRawValue(test_addr, 
                        desc->struct_size > sizeof(uint64_t) ? sizeof(uint64_t) : desc->struct_size);
                    
                    if (best_buffer) free(best_buffer);
                    best_struct = desc;
                    best_buffer = buffer;
                    best_struct_size = desc->struct_size;
                    buffer = NULL;
                }
                
                WriteLogf(log_category, "[!] Potential %s found at offset 0x%zx (0x%p) - confidence: %d%% (%d/%d fields passing)", 
                          desc->struct_name, offset, test_addr, avg_confidence, passing_fields, field_count);
            } else {
                WriteLogf(log_category, "[~] Tested %s at offset 0x%zx (0x%p) - confidence: %d%% (%d/%d fields passing) [REJECTED]", 
                          desc->struct_name, offset, test_addr, avg_confidence, passing_fields, field_count);
            }
            
            if (buffer) free(buffer);
        }
        
        size_t next_offset_increment = sizeof(DWORD);
        
        if (current_result->best_confidence >= 60 && best_struct) {
            identified_structs[identified_count].address = test_addr;
            identified_structs[identified_count].desc = best_struct;
            identified_structs[identified_count].confidence = current_result->best_confidence;
            identified_structs[identified_count].buffer = best_buffer;
            identified_count++;
            
            next_offset_increment = best_struct_size;
            
            WriteLogf(log_category, "[+] Identified %s at 0x%p, skipping %zu bytes to avoid overlap",
                      best_struct->struct_name, test_addr, best_struct_size);
        } else {
            if (current_result->best_confidence == -1) {
                current_result->value_size = sizeof(DWORD);
                if (IsValidMemoryRegion(test_addr, sizeof(DWORD))) {
                    current_result->value = ExtractRawValue(test_addr, sizeof(DWORD));
                }
                current_result->best_confidence = 0;
            }
            if (best_buffer) free(best_buffer);
        }
        
        result_count++;
        offset += next_offset_increment;
    }
    
    WriteLogf("scan_complete", "");
    WriteLogf("scan_complete", "=== IDENTIFIED STRUCTURES ===");
    WriteLogf("scan_complete", "Scanned Region: 0x%p - 0x%p (%zu bytes)", 
              start_addr, (const char*)start_addr + scan_size, scan_size);
    WriteLogf("scan_complete", "");
    
    if (identified_count > 0) {
        for (size_t i = 0; i < identified_count; i++) {
            const IdentifiedStruct* id_struct = &identified_structs[i];
            
            WriteLogf("scan_complete", "┌─ %s at 0x%p (Confidence: %d%%)",
                      id_struct->desc->struct_name, id_struct->address, id_struct->confidence);
            
            for (size_t j = 0; j < id_struct->desc->field_count; j++) {
                const FieldDescriptor* field = &id_struct->desc->fields[j];
                const void* field_ptr = (const char*)id_struct->buffer + field->offset;
                const void* field_addr = (const char*)id_struct->address + field->offset;
                
                ValidationResult result = field->validator(field_ptr, field->size, field->name);
                
                char field_value_str[64];
                uint64_t field_value = ExtractRawValue(field_ptr, field->size);
                FormatRawValue(field_value, field->size, field_value_str, sizeof(field_value_str));
                
                const char* tree_char = (j == id_struct->desc->field_count - 1) ? "└─" : "├─";
                
                WriteLogf("scan_complete", "│  %s %s: %s [0x%p] (%s - %d%%)",
                          tree_char, field->name, field_value_str, field_addr, 
                          result.reason, result.confidence);
            }
            
            WriteLogf("scan_complete", "│");
        }
    } else {
        WriteLogf("scan_complete", "No high-confidence structures identified.");
    }
    
    WriteLogf("scan_complete", "");
    WriteLogf("scan_complete", "=== UNIDENTIFIED MEMORY ===");
    
    bool has_unidentified = false;
    for (size_t i = 0; i < result_count; i++) {
        const ScanResult* result = &results[i];
        if (result->best_confidence < 60) {
            has_unidentified = true;
            
            char value_str[64];
            FormatRawValue(result->value, result->value_size, value_str, sizeof(value_str));
            
            WriteLogf("scan_complete", "0x%08p | %-20s | %s", 
                      result->address, value_str,
                      result->best_confidence > 0 ? "Low confidence match" : "Raw data");
        }
    }
    
    if (!has_unidentified) {
        WriteLogf("scan_complete", "All memory successfully identified as part of structures.");
    }
    
    WriteLogf("scan_complete", "");
    WriteLogf("scan_complete", "=== SCAN STATISTICS ===");
    
    int total_bytes_identified = 0;
    int high_confidence_structs = 0;
    int medium_confidence_structs = 0;
    int low_confidence_structs = 0;
    int multiple_matches = 0;
    
    for (size_t i = 0; i < identified_count; i++) {
        const IdentifiedStruct* id_struct = &identified_structs[i];
        total_bytes_identified += (int)id_struct->desc->struct_size;
        
        if (id_struct->confidence >= 80) high_confidence_structs++;
        else if (id_struct->confidence >= 60) medium_confidence_structs++;
        else low_confidence_structs++;
    }
    
    for (size_t i = 0; i < result_count; i++) {
        if (results[i].num_potential_matches > 1) multiple_matches++;
    }
    
    WriteLogf("scan_complete", "Total memory scanned: %zu bytes", scan_size);
    WriteLogf("scan_complete", "Memory identified as structures: %d bytes (%.1f%%)", 
              total_bytes_identified, (total_bytes_identified * 100.0) / scan_size);
    WriteLogf("scan_complete", "Structures found: %zu", identified_count);
    WriteLogf("scan_complete", "High confidence structures (80-100%%): %d", high_confidence_structs);
    WriteLogf("scan_complete", "Medium confidence structures (60-79%%): %d", medium_confidence_structs);
    WriteLogf("scan_complete", "Low confidence structures (<60%%): %d", low_confidence_structs);
    WriteLogf("scan_complete", "Addresses with multiple potential matches: %d", multiple_matches);
    WriteLogf("scan_complete", "");
    
    for (size_t i = 0; i < identified_count; i++) {
        if (identified_structs[i].buffer) {
            free(identified_structs[i].buffer);
        }
    }
    
    free(results);
    free(identified_structs);
}

void Validate() {
    ScanMemoryForStructures((void*)0x014ce740, 4096, "CheckMemory");
}