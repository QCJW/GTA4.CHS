#include "legacy104.h"
#include "../plugin.h"
#include <windows.h>

// 1.0.4 只导入了 #2 = DirectSoundEnumerateA，设备描述是系统 ANSI 窄串（中文即 GBK），
// 这里把枚举换成宽版本，在回调里转 UTF-8 再交给引擎的回调。
namespace gta4chs::legacy104
{
    namespace
    {
        // 引擎自己的 DirectSoundEnumerateA 入口，宽版本拿不到时退回原行为
        constexpr std::uintptr_t kEnumerateA = 0xCB662Au;

        using EnumCallbackA = BOOL(CALLBACK*)(LPGUID, LPCSTR, LPCSTR, LPVOID);
        using EnumCallbackW = BOOL(CALLBACK*)(LPGUID, LPCWSTR, LPCWSTR, LPVOID);
        using EnumerateA = HRESULT(WINAPI*)(EnumCallbackA, LPVOID);
        using EnumerateW = HRESULT(WINAPI*)(EnumCallbackW, LPVOID);

        EnumCallbackA g_callback = nullptr;

        void CopyAsUtf8(LPCWSTR source, char* dest, int capacity)
        {
            if (source == nullptr
                || ::WideCharToMultiByte(CP_UTF8, 0, source, -1, dest, capacity, nullptr, nullptr) <= 0)
            {
                dest[0] = '\0';
            }
        }

        BOOL CALLBACK EnumCallbackUtf8(LPGUID guid, LPCWSTR description, LPCWSTR module, LPVOID context)
        {
            // 引擎的回调会把描述串复制一份再保存（0x47ADA0 → 0x409890），栈缓冲够用
            char description_utf8[256];
            char module_utf8[256];

            CopyAsUtf8(description, description_utf8, sizeof(description_utf8));
            CopyAsUtf8(module, module_utf8, sizeof(module_utf8));

            return g_callback(guid, description_utf8, module_utf8, context);
        }

        HRESULT WINAPI EnumerateUtf8(EnumCallbackA callback, LPVOID context)
        {
            // dsound.dll 已被引擎加载
            const auto dsound = ::GetModuleHandleW(L"dsound.dll");
            const auto wide = dsound == nullptr
                ? nullptr
                : reinterpret_cast<EnumerateW>(::GetProcAddress(dsound, "DirectSoundEnumerateW"));

            if (callback == nullptr || wide == nullptr)
            {
                return reinterpret_cast<EnumerateA>(injector::aslr_ptr(kEnumerateA).get<void>())(
                    callback, context);
            }

            // 枚举是同步的，回调期间不会有第二次进入
            g_callback = callback;

            return wide(EnumCallbackUtf8, context);
        }
    }

    void install_bench()
    {
        // 系统信息采集（0x47AE00）在这里枚举音频设备，回调 0x47ADA0
        injector::MakeCALL(injector::aslr_ptr(0x47B05Cu).get(), EnumerateUtf8);
    }
}
