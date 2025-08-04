# Gold Guild Patch
![game](https://img.shields.io/badge/game-Europa%201400%20Gold-gold) ![GitHub release](https://img.shields.io/github/v/release/muddykat-tech/Gold-Guild-Patch) ![File Type](https://img.shields.io/badge/type-ASI%20Plugin-green)
> A compatibility patch for *Europa 1400: Gold* that fixes UI rendering issues on modern versions of Windows (Vista and newer).

## Overview

*Europa 1400: Gold* experiences UI rendering problems on modern Windows systems due to a surface padding compatibility issue in the DirectX 8 rendering pipeline.

### The Problem
> Addresses are GOG Specific

![Technical Flow Diagram](flowchart.png)

A simplified overview of the game's rendering system follows this process:

1. **IDirectD3D8 Creation**: `FN_CreateRender_maybe: see addr 0x041a380` is called to set up DirectX rendering
2. **Display Information Detection**: The system attempts to detect data about the display
3. **Suspected Initial Failure Point**: On Windows Vista and newer, `IDirect3D8::EnumAdapterModes: called at 0x041a4ca` this seems to be interrupted by `d3d10warp.dll` loading, causing the padding detection method to encounter an error and zero out the structure it was filling. (this data seems to be stored in a global ptr (`PTR_AdapterData`) to a custom struct)
4. **D3D8 Device Creation**: After creating the IDirectD3D8 Struct, the game then creates the Device and uses the information in PTR_AdapterData to set `TextureCaps`
5. **Surface Allocation**: When `FN_allocate_image_surface: see addr 0x04770a0` is later called, the surface padding calculations are based on the data from `TextureCaps` which is errounous in windows 10.
6. **Result**: UI textures appear truncated because surface dimensions aren't properly padded to the next power of two and seem to be floored or rounded to a power of two.

### Fix
This patch has evolved through two different approaches.

**Version 1.1**
Currently this patch bypasses the unreliable data by directly removing the conditional check at addresses `0x0477152` and `0x0477153`. Instead of relying on the failed detection routine, the patch ensures that surface dimensions are always calculated to the next highest power of two.

> Example: 194x256 -> 256x256, 1153x1024 -> 2048x1024

**Version 1.0**
The Patch used function trampolining and hooked IDirect3DDevice8::CreateImageSurface calls to manually calculated the next power of two for surface dimensions. This method intercepted the function calls using the game's stored device references, thus it is *still* game version specific and will only work for the GOG version of the game.

### Version History
- [Version 1.1](https://github.com/muddykat-tech/Gold-Guild-Patch/releases/tag/v1.1) - **Latest release** - Removes the conditional check, forcing the games padding calculations to always occur
- [Version 1.0](https://github.com/muddykat-tech/Gold-Guild-Patch/releases/tag/v1.0) - Initial release using function trampolining approach

## Compatibility

- **Supported**: GOG version (`Europa1400Gold_TL.exe`)
- **Not Supported**: Steam version (compiled differently; patch incompatible)

## Installation

1. Download `patch.asi`.
2. Place `patch.asi` in the **root directory** of your *Europa 1400: Gold* installation.
3. Launch the game using `Europa1400Gold_TL.exe`.

## Technical Details
> GOG version specific

For modders and reverse engineers, the following function addresses (identified via Ghidra) are relevant:

- `0x0053B1B0`: Initializes `choosecharacter_talent` menu
- `0x00470820`: Loads menu from compressed assets
- `0x00470B8B`: Sets menu background alpha (~0.75 default)
- `0x0046E940`: Positions GUI elements
- `0x004699C0`: Sets small UI sprites (e.g., scroll arrows)
- `0x0041A380`: Calls `Direct3DCreate8` from `d3d8.dll`
- `0x004705FB`: Triggered during resolution set; leads to `CreateTexture`
- `0x004770a0`: called when allocating a new image surface
- `0x00477152` / `0x00477153`: Conditional check controlling padding logic for surface dimensions replaces DAT_0067d91d != '\0' check.
- `0x0067d91d`: Location of the byte that toggles if the image surface padding is enabled or not, in modern versions of windows this is consistantly reset to 00h. (on XP it is set as 01h)
