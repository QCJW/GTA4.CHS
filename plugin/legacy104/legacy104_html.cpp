#include "../gta_html.h"
#include "legacy104.h"
#include "legacy104_string.h"
#include "../gta_string.h"
#include "../plugin.h"

namespace gta4chs::legacy104
{
namespace
{
// 网页/邮件日文字档的横向比例：CJK 等宽，按拉丁宽度排会被拉伸
constexpr float kWebMailScaleXRatio = 0.72f;

using set_scale_fn = void(__cdecl *)(float, float);
set_scale_fn g_orig_set_scale = nullptr;

void __cdecl web_mail_set_scale_104(float scale_x, float scale_y)
{
    if (g_orig_set_scale == nullptr)
    {
        return;
    }

    g_orig_set_scale(scale_x * kWebMailScaleXRatio, scale_y);
}
}

void install_html()
{
    // GET_STRING_FROM_STRING 里的 strncpy；size 是字节数，不能用按字符数计的版本
    injector::MakeCALL(injector::aslr_ptr(0xA11657).get(), StrncpyByteLimited);
    injector::MakeNOP(injector::aslr_ptr(0xA11663).get(), 5);

    // 同一函数里的 memmove
    injector::MakeCALL(injector::aslr_ptr(0xA1167A).get(), gta_string::gtaSpecialMemmove);
    injector::MakeNOP(injector::aslr_ptr(0xA1168D).get(), 7);

    {
        // 1.0.4 的 0x4B1920 是单档 SetScale(v, v)（恒 1.0），没有 0.72/1.0 双档分支，这里补倍率
        auto site = injector::aslr_ptr(0x4B193D).get();
        g_orig_set_scale = reinterpret_cast<set_scale_fn>(injector::GetBranchDestination(site).get<void *>());
        injector::MakeCALL(site, web_mail_set_scale_104);
    }

    // GET_WEB_PAGE_LINK_AT_POSN 里跟 30.0 比较的判断
    injector::WriteMemory<uchar>(injector::aslr_ptr(0x4A63C7).get(), 0xEBu, true);

    // CHtmlTextFormat vftable：超链接响应宽度计算
    injector::WriteMemory<uchar>(injector::aslr_ptr(0x4A8F91).get(), 0xEBu, true);
}
}
