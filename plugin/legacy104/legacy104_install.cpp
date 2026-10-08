#include "legacy104.h"
#include "../plugin.h"
#include <cstring>
#include <exception>

namespace gta4chs::legacy104
{
    // 各模块安装函数，地址全部按 1.0.4.0 静态标定，不经任何跨版本选择器
    void install_bench();
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
        // 装钩前先验宿主：地址是静态标定的，认错版本写进去必崩。
        struct site_fingerprint
        {
            std::uintptr_t va;
            uchar bytes[8];
        };

        constexpr site_fingerprint kFingerprints[] = {
            { 0x7F88F0, { 0x81, 0xEC, 0x88, 0x05, 0x00, 0x00, 0xA1, 0xE0 } }, // CFont::ProcessString
            { 0x7F7CF0, { 0x81, 0xEC, 0x64, 0x08, 0x00, 0x00, 0xA1, 0xE0 } }, // CFont::GetStringWidth
            { 0x4A5880, { 0x85, 0xC0, 0x7D, 0x1A, 0x8B, 0xC7, 0x8D, 0x50 } }, // data-node set-string
            { 0x7E8730, { 0x56, 0x8B, 0x74, 0x24, 0x0C, 0x85, 0xF6, 0x75 } }, // 宽→窄（存档名）
            { 0xA1427E, { 0xE8, 0x5D, 0x21, 0x28, 0x00, 0x83, 0xC4, 0x0C } }, // 手机 strncpy 调用点
        };

        bool host_is_104()
        {
            for (const auto &fp : kFingerprints)
            {
                auto *p = injector::aslr_ptr(fp.va).get<const uchar>();

                if (p == nullptr || std::memcmp(p, fp.bytes, sizeof(fp.bytes)) != 0)
                {
                    return false;
                }
            }

            return true;
        }

        // 单个模块出错不该拖垮其余模块
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

    bool install_all()
    {
        // 指纹不对就整体放弃：装错版本的钩子比不装危险得多
        if (!host_is_104())
        {
            return false;
        }

        // 音频设备名走 DSOUND 枚举，104 只导入了 ANSI 版本，须换成宽版本并把结果转 UTF-8
        step(install_bench);
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
