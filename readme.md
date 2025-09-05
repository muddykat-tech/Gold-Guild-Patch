# Gold Guild Patch

![game](https://img.shields.io/badge/game-Europa%201400%20Gold-gold)
![GitHub release](https://img.shields.io/github/v/release/muddykat-tech/Gold-Guild-Patch)
![File Type](https://img.shields.io/badge/type-ASI%20Plugin-green)

> A compatibility patch for *Europa 1400: Gold* that fixes UI rendering issues on modern versions of Windows (Vista and newer).

---

## Overview

*Europa 1400: Gold* experiences UI rendering problems on modern Windows systems due to a surface padding compatibility issue in the DirectX 8 rendering pipeline.

---

## Rendering Bug in GOG Version on Modern Windows
> **Note**: All addresses and offsets referenced apply specifically to the **GOG** version of the game.

### Overview
The game uses Direct3D 8 for rendering. During initialization:

- It creates an `IDirect3D8` interface via `Direct3DCreate8`
- Retrieves hardware capabilities using `IDirect3D8::GetDeviceCaps`
- Copies the resulting `D3DCAPS8` structure into a global memory region at address `0x014ce740`

This global struct (mistakenly assumed to come from `EnumAdapterModes` in earlier analysis) contains the `TextureCaps` field, which informs surface allocation logic later in the pipeline.

### The Bug

On Windows versions past XP, `GetDeviceCaps` does **not** return the legacy `D3DPTEXTURECAPS_POW2` and related flags. The game relies on these flags to determine if textures should be padded to the nearest power of two.

Without them, surfaces are incorrectly sized which causes **truncated or misaligned UI textures**.

### Missing Flags

The following `D3DCAPS8.TextureCaps` flags are present on Windows XP but absent on later windows versions:

- `D3DPTEXTURECAPS_POW2`
- `D3DPTEXTURECAPS_NONPOW2CONDITIONAL`
- `D3DPTEXTURECAPS_CUBEMAP_POW2`
- `D3DPTEXTURECAPS_VOLUMEMAP_POW2`

These are required by the game's logic to apply correct surface padding during texture allocation (`FN_allocate_image_surface` at `0x04770a0`).

### Patch Methods
There are **three practical methods** to correct this behavior:

#### 1. **Patch the Surface Allocation Check (v1.1 approach)**  
Remove the conditional check against `TextureCaps` during surface allocation, forcing the game to always apply power-of-two padding when appropriate.  

- Small binary patch with minimal footprint  
- Works regardless of the actual `D3DCAPS8` values returned  
- Minimal impact on unrelated systems or rendering paths  
- **Highly version-specific**: This method depends on the decompiled layout of the GOG release; other builds (e.g., retail CD or Steam) would require separate patches due to differences in compilation and layout

#### 2. **Hook and Correct at Runtime (v1.0 approach)**  
Inject a trampoline into the surface allocation function to dynamically recalculate and enforce power-of-two surface dimensions at runtime.

- Enables dynamic logic correction without modifying existing control flow  
- Slightly more invasive than method 1, but robust and accurate  
- Requires access to the D3D8 device pointer and in-depth knowledge of the function's calling conventions  
- **Also version-specific**, due to address and layout differences between builds

#### 3. **Intercept Direct3D8 via Wrapper (`d3d8.dll` Proxy)**  
Create or modify a `d3d8.dll` proxy to intercept `GetDeviceCaps`, injecting the missing texture capability flags (e.g., `D3DPTEXTURECAPS_POW2`) before the data reaches the game.

- Preserves original game logic and compatibility paths  
- Can be layered into existing Direct3D8-to-9 wrappers like dgVoodoo or DXWrapper  
- **Game-version independent**: Does not rely on binary layout, making it the most portable of the three solutions
  
### Summary

The rendering issue is caused by the game's hardcoded reliance on legacy `D3DCAPS8` flags that are no longer reported on modern systems. The global struct at `0x014ce740` reflects the result of `GetDeviceCaps`, and the absence of `D3DPTEXTURECAPS_POW2` causes improper surface sizing.

### Execution Flow Diagram

<p align="center">
  <img src="flowchart.svg" alt="Technical Flow Diagram">
</p>

---

## Patch Versions

| Version | Description | Example | Link |
|---------|-------------|---------|------|
| **1.1** *(Current)* | Forces surface dimensions to the **next power of two** Removes conditional check at `0x0477152` and `0x0477153` | `1153x1024 -> 2048x1024` | [Download v1.1](https://github.com/muddykat-tech/Gold-Guild-Patch/releases/tag/v1.1) |
| **1.0** *(Legacy)* | Hooking `IDirect3DDevice8::CreateImageSurface` in `d3d8.dll` via the game’s global device pointer and creating a trampoline to manually calculate surface sizes. | Same behavior as above | [Download v1.0](https://github.com/muddykat-tech/Gold-Guild-Patch/releases/tag/v1.0) |

> Both versions are specific to the **GOG** version. The Steam version is incompatible due to different binary layout.

---

## Compatibility

- **Supported**: GOG version (`Europa1400Gold_TL.exe`)
- **Not Supported**: Steam version (compiled differently; patch is incompatible)

---

## Installation

1. Download `patch.asi`.
2. Place `patch.asi` in the **root directory** of your *Europa 1400: Gold* installation.
3. Launch the game using `Europa1400Gold_TL.exe`.

---

## Technical Notes

> The information below is based on reverse engineering conducted using Ghidra. While care has been taken to ensure accuracy, some structures and function purposes may be misidentified or incomplete. Updates will be made as new findings are validated.

### Key Function Addresses

 
| Address | Description |
|---------|-------------|
| `0x0053B1B0` | Initializes `choosecharacter_talent` menu |
| `0x00470820` | Loads menu from compressed assets |
| `0x00470B8B` | Sets menu background alpha (default ≈ 0.75) |
| `0x0046E940` | Positions GUI elements |
| `0x004699C0` | Sets small UI sprites (e.g., scroll arrows) |
| `0x0041A380` | `FN_InitializeD3D8` function, which calls `Direct3DCreate8` from `d3d8.dll` |
| `0x004705FB` | Triggers during resolution setup; leads to `CreateTexture` |
| `0x004770A0` | Allocates new image surface |
| `0x00477152` | Conditional checks for image surface padding calculation|
| `0x0067D91D` | Byte controlling padding behavior (set to `0x00` on modern Windows; `0x01` on XP) |
| `0x0041ad08` | Appears to copy `D3DPRESENT_PARAMETERS` from `PTR_AdapterData` into `PTR_014ce740` using `memcpy` (0x34 bytes); also sets `014ce77c` |
| `0x014ce77c` | Assumed to be `TextureCaps` in the `PTR_014ce740` structure | 

