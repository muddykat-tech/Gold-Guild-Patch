# Gold Guild Patch

![game](https://img.shields.io/badge/game-Europa%201400%20Gold-gold)
![GitHub release](https://img.shields.io/github/v/release/muddykat-tech/Gold-Guild-Patch)
![File Type](https://img.shields.io/badge/type-ASI%20Plugin-green)

> A compatibility patch for *Europa 1400: Gold* that fixes UI rendering issues on modern versions of Windows (Vista and newer).

---

## Overview

*Europa 1400: Gold* experiences UI rendering problems on modern Windows systems due to a surface padding compatibility issue in the DirectX 8 rendering pipeline.

---

## The Problem

> **Note**: All addresses and offsets referenced apply specifically to the **GOG** version of the game.

A simplified overview of the game's rendering system follows this process:

1. **IDirectD3D8 Creation**: `FN_InitializeD3D8` is called to set up DirectX rendering
2. **Display Information Detection**: The system attempts to detect data about the display
3. **Suspected Initial Failure Point**: On Windows Vista and newer, `IDirect3D8::EnumAdapterModes: called at 0x041a4ca` this seems to be interrupted by `d3d10warp.dll` loading, causing the method to encounter an error and zero out the structure it was filling. (this data is stored in a global ptr (`PTR_AdapterData`) which seems to be a custom structure)
4. **D3D8 Device Creation**: After creating the IDirectD3D8 Struct, the game then creates the Device and uses the information in PTR_AdapterData to set `TextureCaps: see addr 0x014ce77c`
5. **Surface Allocation**: When `FN_allocate_image_surface: see addr 0x04770a0` is later called, the surface padding calculation condition is based on the data from `TextureCaps` which is erroneous in windows 10.
6. **Result**: UI textures appear truncated because surface dimensions aren't properly padded to the next power of two and seem to be floored or rounded to a power of two.

### Execution Flow Diagram

<p align="center">
  <img src="flowchart.svg" alt="Technical Flow Diagram">
</p>

---

## Patch Versions

| Version | Description | Fix Method | Example | Link |
|---------|-------------|------------|---------|------|
| **1.1** *(Current)* | Removes conditional check at `0x0477152` and `0x0477153` | Forces surface dimensions to the **next power of two**, bypassing DirectX capability detection | `194x256 → 256x256`<br>`1153x1024 → 2048x1024` | [Download v1.1](https://github.com/muddykat-tech/Gold-Guild-Patch/releases/tag/v1.1) |
| **1.0** *(Legacy)* | Hooks into `IDirect3DDevice8::CreateImageSurface` using function trampolining | Manually calculates correct surface sizes from game device reference | Same behavior as above, but less stable | [Download v1.0](https://github.com/muddykat-tech/Gold-Guild-Patch/releases/tag/v1.0) |

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

