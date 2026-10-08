#include "legacy.h"
#include "../plugin.h"
#include <cstring>
#include <exception>
#include <filesystem>
#include <system_error>

namespace gta4chs::legacy
{
    game_version g_version = game_version::v108;

    // 各模块安装函数（地址经 1.0.7 / 1.0.8 两个真实 EXE 静态标定）
    void install_benchmark();
    void install_font();
    void install_game();
    void install_html();
    void install_mail_reply();
    void install_menu();
    void install_phone();
    void install_save();
    void install_whm();

    namespace
    {
        // —— 宿主指纹自检（1.0.7 / 1.0.8 双向）——
        // 本目录 110 处站点全是硬编码 VA，认错版本就会把 CALL/JMP 写进无关的引擎代码 → 必崩。
        // 装第一个钩子前先双向比对，且以指纹命中的那一版为准（识别错了会被纠正）。
        // 特征码取自两个真实 EXE（imagebase 0x400000）；同一组 VA 在 1.0.4 / CE 上全部 MISS。
        struct site_fingerprint
        {
            std::uintptr_t va107;
            std::uintptr_t va108;
            unsigned char bytes107[8];
            unsigned char bytes108[8];
        };

        constexpr site_fingerprint kFingerprints[] = {
            {0x407420, 0x477D00, {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF0, 0xB8, 0x64},
             {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF0, 0xB8, 0x64}}, // benchmark 入口
            {0x7FE640, 0x88B370, {0x81, 0xEC, 0x84, 0x0A, 0x00, 0x00, 0xA1, 0x40},
             {0x81, 0xEC, 0x84, 0x0A, 0x00, 0x00, 0xA1, 0x40}}, // CFont::ProcessString
            {0x7FEEE1, 0x88BC11, {0xE8, 0x5A, 0xF7, 0xFF, 0xFF, 0x83, 0xC4, 0x20},
             {0xE8, 0x5A, 0xF7, 0xFF, 0xFF, 0x83, 0xC4, 0x20}}, // 菜单 ProcessString 调用点
            {0x8524C0, 0x85CF60, {0x80, 0x7C, 0x24, 0x08, 0x00, 0xA1, 0x24, 0xE8},
             {0x80, 0x7C, 0x24, 0x08, 0x00, 0xA1, 0xC0, 0x60}}, // game 分支
            {0x4533F2, 0x4BF002, {0xE8, 0x29, 0x3A, 0x39, 0x00, 0x8D, 0x44, 0x24},
             {0xE8, 0x69, 0x4B, 0x3B, 0x00, 0x8D, 0x44, 0x24}}, // html 调用点
        };

        bool site_matches(const site_fingerprint &fp, bool want107)
        {
            auto *p = injector::aslr_ptr(want107 ? fp.va107 : fp.va108).get<const unsigned char>();

            return p != nullptr &&
                   std::memcmp(p, want107 ? fp.bytes107 : fp.bytes108, sizeof(fp.bytes107)) == 0;
        }

        // 0 = 命中 1.0.7，1 = 命中 1.0.8，-1 = 都不命中或两版同时命中（异常）
        int probe_host()
        {
            bool ok107 = true;
            bool ok108 = true;

            for (const auto &fp : kFingerprints)
            {
                if (!site_matches(fp, true))
                {
                    ok107 = false;
                }

                if (!site_matches(fp, false))
                {
                    ok108 = false;
                }
            }

            if (ok107 && !ok108)
            {
                return 0;
            }

            if (ok108 && !ok107)
            {
                return 1;
            }

            return -1;
        }

        // 逃生开关：非标准发行版指纹不在表内时，放一个空的 GTA4.CHS\force_legacy_host.txt 强制按识别结果安装。
        bool host_check_bypassed()
        {
            std::error_code ec;

            return std::filesystem::exists(plugin.GetPluginAsset("force_legacy_host.txt"), ec);
        }

        // 单个模块出错不该拖垮其余模块，也不该让 DllMain 静默失败
        void step(void (*installer)())
        {
            try
            {
                installer();
            }
            catch (...)
            {
            }
        }
    }

    bool install_all(game_version v)
    {
        // 以指纹命中的版本为准，识别结果只作兜底：识别错了会被纠正，不会张冠李戴
        const int probed = probe_host();

        if (probed >= 0)
        {
            v = probed == 0 ? game_version::v107 : game_version::v108;
        }
        else if (!host_check_bypassed())
        {
            return false;
        }

        g_version = v;

        // 顺序与原 vcnge 插件 RegisterPatchSteps 保持一致
        step(install_benchmark);
        step(install_font);
        step(install_game);
        step(install_html);
        step(install_mail_reply);
        step(install_menu);
        step(install_phone);
        step(install_save);
        step(install_whm);

        return true;
    }
}
