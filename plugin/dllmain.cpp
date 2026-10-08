#include "plugin.h"

BOOL WINAPI DllMain(HMODULE module, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        ::DisableThreadLibraryCalls(module);

        // DllMain 持有 loader lock，Init 里不能向上抛出
        try
        {
            plugin.Init(module);
        }
        catch (...)
        {
        }

        // 关键：返回 FALSE 会让 Windows 立刻卸载本 DLL（表现为「ASI 根本没加载」且无痕迹），失败也要留在进程里。
        return TRUE;
    }

    return TRUE;
}
