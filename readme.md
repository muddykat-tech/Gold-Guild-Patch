# Gold Guild Patch

A compatibility patch for *Europa 1400: Gold* that fixes UI rendering issues on modern versions of Windows (Vista and newer).

## Overview

The game creates image surfaces using `IDirect3DDevice8::CreateImageSurface`. Correct behavior depends on surface dimensions being padded to the next power of two. If this padding does not occur, textures may appear truncated, causing broken UI elements.

The logic responsible for this padding depends on a conditional check located at addresses `0x00477152` and `0x00477153`. On modern systems, this check fails, causing the game to skip the padding logic.

This patch modifies the behavior so that surface dimensions are always padded as expected, restoring correct UI rendering.

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
