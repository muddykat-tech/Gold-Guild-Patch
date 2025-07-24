# Gold Guild Patch
A patch to the UI issues present when running the game on versions of windows past WinXP.

The root cause seems to be due to an issue with the rendering space assuming it has access to 1152x864 when it can only draw things within 1024x768 *and* the fact that for some reason the starting draw point is approx 512 on the x axis but 0 on the y.

The game assumes it has access to 1152x864 as that is the largest resolution it should have, however in later additions of Windows this resolution seems to be broken.

For those interested below I've listed out function addresses I've found in Ghidra that may be of interest to others.
00772514
### Relevant Addressess
- **0053b1b0** initalization function for choosecharacter_talent menu.
- **00470820** function used to setup a menu (loading it from the compressed asset data.)
- **00470b8b** sets the alpha for a menu background from some array. (default seems to be 0.75); (in menu setup)
- **0046e940** seems to have some kind of relation to setting the position of a gui element with consideration of it's parent element.
- **004699c0** changes the sprite / image used for small ui elements (seems to be used in limited animation as well) think of (scroll arrows and the questionmarks on free slot save files)
- **0041a380** Seems to be the function used when creating the draw surface, calling d3d8.dll's Direct3DCreate8 function.
- **004705FB** Is called when setting the resolution IDirect3DDevice8::CreateTexture

An interesting note is that if you 'load' the choosecharacter_talent for other menus instead of their default, for example the load game menu.
The UI issues don't show up, I assume this is due to the x size of the talent menu being smaller, and that some kind of max texture width is affecting the rendering. 