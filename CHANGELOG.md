### Fixed

- Resolved an issue in `d3d8.dll` where `CreateImageSurface` did not apply the expected padding required by *Europa 1400: Gold*.  
  
As an example: 
The game requests a surface size of `574x512`, which was automatically padded to `1024x512` on Windows XP.  
On later versions of Windows, this behavior no longer occurs, resulting in textures being truncated as the surface size is floored to the next valid power-of-two.  
The padding is now correctly handled to maintain compatibility with the game's expectations.