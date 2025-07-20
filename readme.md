# Gold Guild Patch
A patch to the UI issues present when running the game on versions of windows past WinXP.



This fix works by modifying the code behind how the affected menus are drawn, based on a working example from the choosecharacter_talent menu.

I suspect that the reason this menu works without issue is due to the width being smaller.

For those interested below I've listed out function addresses I've found in Ghidra that may be of interest to others.
00772514
### Relevant Addressess
- **0053b1b0** initalization function for choosecharacter_talent menu.
- **00470820** function used to setup a menu (loading it from the compressed asset data.)
- **00470b8b** sets the alpha for a menu background from some array. (default seems to be 0.75); (in menu setup)

An interesting note is that if you 'load' the choosecharacter_talent for other menus instead of their default, for example the load game menu.
The UI issues don't show up, I assume this is due to the x size of the talent menu being smaller, and that some kind of max texture width is affecting the rendering. 