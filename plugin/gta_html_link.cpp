#include "gta_html_link.h"
#include "gta_string.h"
#include <cstdint>

namespace gta_html_link
{
namespace
{
// 排版函数栈上 +0x30 处是排版状态对象，收尾用到三个槽；
// 「页原点指针」在栈参数里，偏移各版本不同（1.0.4/1.0.7/1.0.8 = 0x274，1.2.0.x = 0x27C）
constexpr std::uintptr_t kLineHeightOff = 0x54; // 本行最高字号高度，换行时清零
constexpr std::uintptr_t kPenXOff = 0x58;       // 行内已推进的横向偏移，换行时清零
constexpr std::uintptr_t kLineYOff = 0x5C;      // 当前行纵向偏移，换行时 += 行高

#pragma pack(push, 1)
struct html_link_rect
{
    uchar pad0[0x20];
    float width;  // +0x20
    float height; // +0x24
    uchar pad1[0x2C - 0x28];
    float x;      // +0x2C
    float y;      // +0x30
};
#pragma pack(pop)

// 链接锚点在 <a>（节点类型 2）进入时就记成了「页原点 + 当时行内偏移」，而链接文字
// 可能随后被挤到下一行：此时行内偏移已清零、行 y 已下移，锚点却还停在上一行行尾，
// w = 原点x + 行内偏移 − 锚点x 就成了负数，命中区 [x, x+w) 为空 → 点不到。
// 英文原本很少撞到行尾所以不显，汉化后字宽变了就常撞上。
// 判定「不在同一行」就把锚点改到当前行行首：被推下去的链接正好就是从行首开始画的。
void __cdecl html_fix_link_rect(html_link_rect *link, const float *origin, const char *state)
{
    const float line_h = *reinterpret_cast<const float *>(state + kLineHeightOff);
    const float pen_x = *reinterpret_cast<const float *>(state + kPenXOff);
    const float line_y = *reinterpret_cast<const float *>(state + kLineYOff);

    const float cur_x = origin[0] + pen_x;
    const float cur_y = origin[1] + line_y;

    // 原版是 ucomiss 判相等；这里用 ±0.5 的容差，避免 x87/SSE 舍入差 1 ULP 就误判换行
    // （行间距十几像素，0.5 足够区分）
    const float dy = cur_y - link->y;
    const bool same_line = dy > -0.5f && dy < 0.5f;

    // 同一行：跟原版一样，矩形右端推到笔位
    if (same_line && cur_x >= link->x)
    {
        link->width = cur_x - link->x;
    }
    else
    {
        // 换行了：锚点已经失效，改锚到当前行行首
        link->x = origin[0];
        link->width = pen_x > 0.0f ? pen_x : 0.0f;
        link->y = cur_y;
    }

    if (line_h > 0.0f)
    {
        link->height = line_h;
    }
}

using fix_link_rect_fn = void(__cdecl *)(html_link_rect *, const float *, const char *);
fix_link_rect_fn g_fix_link_rect = html_fix_link_rect;
std::uintptr_t g_link_rect_done_va = 0;
std::uintptr_t g_origin_off = 0x274;

__declspec(naked) void html_link_rect_guard()
{
    __asm
    {
        pushad
        mov eax, esp
        add eax, 36                       ; pushad(32) + 返回地址(4) → 调用方 esp
        mov edx, dword ptr [g_origin_off] ; 页原点指针所在栈槽
        mov ecx, dword ptr [eax + edx]
        push eax                          ; 排版状态对象所在栈帧
        push ecx
        push esi                          ; 当前打开的链接节点
        call dword ptr [g_fix_link_rect]
        add esp, 12
        popad
        add esp, 4                        ; 本函数不 ret
        jmp dword ptr [g_link_rect_done_va]
    }
}
}

void install_link_rect_fix(std::uintptr_t hook_va, std::uintptr_t done_va, std::uintptr_t origin_off)
{
    g_origin_off = origin_off;
    g_link_rect_done_va = reinterpret_cast<std::uintptr_t>(injector::aslr_ptr(done_va).get<unsigned char>());
    injector::MakeCALL(injector::aslr_ptr(hook_va).get(), html_link_rect_guard);
    injector::MakeNOP(injector::aslr_ptr(hook_va + 5).get(), 4);
}
} // namespace gta_html_link
