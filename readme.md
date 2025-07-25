# Gold Guild Patch

A compatibility patch that addresses UI rendering issues in *Europa 1400: Gold* when running on versions of Windows newer than Windows XP.

## Overview

This patch resolves graphical and layout problems caused by changes in how modern versions of Windows handle rendering surfaces.

Specifically, the game creates image surfaces using `IDirect3DDevice8::CreateImageSurface` from `d3d8.dll`. On Windows XP, when a surface is requested with non-power-of-two dimensions, the system automatically pads it to the next valid power of two. On later versions of Windows, this behavior is no longer guaranteed, leading to truncated textures and broken UI rendering.

This patch restores compatibility by ensuring surfaces are correctly padded to match the game's expectations.

## Installation

1. Download the `patch.asi` file.
2. Copy `patch.asi` into the **root directory** of your *Europa 1400: Gold* installation.
3. **Important:** This patch targets `Europa1400Gold_TL.exe`.  
   Make sure you launch the game using this executable, it will **not** work with `Europa1400Gold.exe`.

## Technical Notes

For those interested in the technical details, below are some function addresses identified using Ghidra that may be useful for further reverse engineering or mod development:

### Relevant Function Addresses

- `0053B1B0`: Initialization function for the `choosecharacter_talent` menu.
- `00470820`: Loads a menu from compressed asset data.
- `00470B8B`: Sets the alpha transparency of menu backgrounds (default is ~0.75).
- `0046E940`: Manages the positioning of GUI elements relative to their parent elements.
- `004699C0`: Changes the sprite/image for small UI elements (e.g., scroll arrows, question marks in save slots).
- `0041A380`: Creates the draw surface by calling `Direct3DCreate8` from `d3d8.dll`.
- `004705FB`: Called when setting the resolution; should correspond to a call to `IDirect3DDevice8::CreateTexture`.
