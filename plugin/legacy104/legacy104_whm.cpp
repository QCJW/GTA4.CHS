#include "../gta_whm.h"
#include "legacy104.h"
#include "../class_pointer.hpp"
#include "../plugin.h"
#include <intrin.h>

namespace gta4chs::legacy104
{
namespace
{
    // 译文指针指向 whm_table 字符串区（不是堆分配），交给引擎 free 必崩
    bool whm_owns(const uchar *p)
    {
        const auto s = plugin.whm_table.GetStrings();

        return !s.empty() && p >= s.data() && p < s.data() + s.size();
    }
}

// 节点自有 set-string（0x4A5880）：换指针必须同步换长度，否则缓冲按原长分配，译文更长就溢出
struct html_data_node_set_string_104
{
    void operator()(injector::reg_pack &regs) const
    {
        // 补回被 NOP 掉的原指令
        regs.ecx = __readfsdword(0x2C);

        // 熔断时彻底放手，一律走原生路径
        if (MrPanicked())
        {
            return;
        }

        if (regs.edi == 0)
        {
            return;
        }

        const auto *src = reinterpret_cast<const uchar *>(regs.edi);
        const auto *translated = plugin.whm_table.GetTranslated(src);
        const auto tr_len = std::strlen(reinterpret_cast<const char *>(translated));

        if (translated == src)
            return;

        // 引擎后面按 eax+1 重新分配自有缓冲，长度完全由 eax 决定，不做截断
        regs.edi = reinterpret_cast<std::uintptr_t>(translated);
        regs.eax = static_cast<std::uintptr_t>(tr_len);
    }
};

// 静态正文是零拷贝从资源块恢复的（0x4A539C 处加 delta），全程不调 0x4A5880；只挂构造函数一条译文都查不到
struct html_data_node_load_104
{
    void operator()(injector::reg_pack &regs) const
    {
        auto *slot = reinterpret_cast<std::uintptr_t *>(reinterpret_cast<char *>(regs.esi) + 0xD8);

        // 补回被 NOP 的原指令；已经是译文指针就别再加 delta，否则直接飞掉
        if (!whm_owns(reinterpret_cast<const uchar *>(*slot)))
            *slot += regs.eax;

        const auto *src = reinterpret_cast<const uchar *>(*slot);

        if (src == nullptr)
            return;

        const auto *translated = plugin.whm_table.GetTranslated(src);

        if (translated != src)
            *slot = reinterpret_cast<std::uintptr_t>(translated);
    }
};

// 析构会 free(m_pData)：字符串区的指针置 0 让引擎跳过释放
struct html_data_node_dtor_104
{
    void operator()(injector::reg_pack &regs) const
    {
        const auto *p = *reinterpret_cast<const uchar *const *>(reinterpret_cast<const char *>(regs.esi) + 0xD8);

        regs.eax = whm_owns(p) ? 0u : reinterpret_cast<std::uintptr_t>(p);
    }
};

void install_whm()
{
    // 静态正文（零拷贝）+ 动态正文（set-string）两条路都要覆盖
    injector::MakeInline<html_data_node_load_104>(injector::aslr_ptr(0x4A539C).get(),
                                                  injector::aslr_ptr(0x4A53A2).get());
    injector::MakeInline<html_data_node_dtor_104>(injector::aslr_ptr(0x4A5833).get(),
                                                  injector::aslr_ptr(0x4A5839).get());
    injector::MakeInline<html_data_node_set_string_104>(injector::aslr_ptr(0x4A589E).get(),
                                                        injector::aslr_ptr(0x4A589E + 7).get());
}
}
