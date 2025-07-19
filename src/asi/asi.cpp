#include "asi.h"
#include <cstdio>

namespace ASI
{
/// allows rewriting code in a given memory region by overwriting permissions for that memory region
bool BeginRewrite(MemoryRegion &mem_region)
{
    return VirtualProtect((LPVOID)mem_region.memory_offset,
                          (SIZE_T)mem_region.memory_length, 0x40, // 0x40 - enable everything?
                          &mem_region.old_reg_perm);
}

/// ends rewriting by restoring old permissions in a given memory region
bool EndRewrite(MemoryRegion &mem_region)
{
    DWORD tmp_old_region_permission;
    bool b = VirtualProtect((LPVOID)mem_region.memory_offset,
                            (SIZE_T)mem_region.memory_length,
                            mem_region.old_reg_perm,
                            &tmp_old_region_permission) != 0;
    if (b)
        FlushInstructionCache((HANDLE)0xFFFFFFFF, 0, 0);
    return b;
}

unsigned int GAME_BASE;
int WINDOW_OFFSET;
unsigned int APPMAIN_OFFSET;
void SetGameBase()
{
    GAME_BASE = (unsigned int)GetModuleHandleA("Europa1400Gold_TL.exe");
}

/// required for everything to work... why?
bool Init(HMODULE lib_module)
{
    if (!DisableThreadLibraryCalls(lib_module))
        return false;
    ASI::SetGameBase();
    if (CheckVersion(GOLD_206))
    {
        return true;
    }
    return false;
}

/// check version of the game that was hooked into
bool __stdcall CheckVersion(Version sf_version)
{
    switch (sf_version)
    {
        case GOLD_206:
            // Locate In Game Reference to Version
            char game_buf[64];
            sprintf(game_buf, (char *)ASI::AddrOf(0x2a3838));
            if (strncmp(game_buf, "Europa 1400 - Gold Edition - 2.06", 34) || strncmp(game_buf, "Die Gilde - Gold Edition - 2.06", 32))
                return true;
            return false;
            break;
        default:
            return false;
    }
    return false;
}
}
