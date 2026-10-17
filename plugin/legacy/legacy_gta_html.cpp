#include "../gta_html.h"
#include "../gta_html_link.h"
#include "legacy.h"
#include "../gta_string.h"
#include "../plugin.h"

namespace gta4chs::legacy
{
void install_html()
{
    // 替换html字符串处理过程

    // Native: ADD_STRING_TO_HTML_SCRIPT_OBJECT(7EB70379)里面
    injector::MakeCALL(injector::aslr_ptr(pick2(0x4533F2, 0x4BF002)).get(), gta_string::gtaTruncateString2);

    // IDA Names: const CHtmlDataNode::`vftable'
    // 两个使用此虚表的函数
    injector::MakeCALL(injector::aslr_ptr(pick2(0x43BE5C, 0x4B417C)).get(), gta_string::gtaExpandStringGxt);
    injector::MakeCALL(injector::aslr_ptr(pick2(0x43BF3B, 0x4B425B)).get(), gta_string::gtaExpandStringGxt);

    // Native: GET_STRING_FROM_STRING(434534BE)，调用strncpy的地方
    injector::MakeCALL(injector::aslr_ptr(pick2(0xBB00DB, 0xB539FB)).get(), gta_string::gtaUTF8strncpy);
    injector::MakeNOP(injector::aslr_ptr(pick2(0xBB00E7, 0xB53A07)).get(), 5);

    // 跟上面同一个函数，调用memmove的地方
    injector::MakeCALL(injector::aslr_ptr(pick2(0xBB00FE, 0xB53A1E)).get(), gta_string::gtaSpecialMemmove);
    injector::MakeNOP(injector::aslr_ptr(pick2(0xBB0108, 0xB53A28)).get(), 7);

    // 在调用CFont_GetMaxWordWidth之前调用的函数里面，设置相关CFont参数的
    // 使用了浮点数 EC 51 38 3F的函数中
    // 网页邮件文字的比例值，直接用日语版的数值
    injector::WriteMemory<uchar>(injector::aslr_ptr(pick2(0x44D835, 0x4C3955)).get(), 0xEBu, true);

    // Native: GET_WEB_PAGE_LINK_AT_POSN(C1C5B1B)中最后调用的函数里的判断逻辑，跟30.0比较
    injector::WriteMemory<uchar>(injector::aslr_ptr(pick2(0x43CB17, 0x4B4E37)).get(), 0xEBu, true);

    // IDA Names: const CHtmlTextFormat::`vftable'
    // 其中一个使用此虚表的函数
    // 计算超链接响应宽度的逻辑
    // 原来无条件跳「同一行」分支，换行的链接宽度算成负数就点不到了；
    // 挂跳板改锚点到当前行行首，汇合点是两条分支的 jmp 目标
    gta_html_link::install_link_rect_fix(pick2(0x43F71A, 0x4B7A2A), pick2(0x43F777, 0x4B7A87), 0x274);
}
} // namespace gta4chs::legacy
