#include "../gta_mail_reply.h"
#include "legacy104.h"
#include "legacy104_string.h"
#include "../gta_string.h"
#include "../plugin.h"
#include <cstring>
#include <windows.h>

namespace gta4chs::legacy104
{
using gta_mail_reply::class_for_mr;

// f48 是固定 1024 字节 SSO 缓冲，f448 是游标；UTF-8 下 CJK 3 字节，不设边界就写穿

namespace
{
    constexpr int kMrBufCap = 0x400; // == sizeof(class_for_mr::f48)

    // 熔断：一次解析只碰几次 EOF，被反复命中说明跑飞
    bool g_panic = false;
    std::uint32_t g_eof_base = 0;
    std::uint32_t g_last_tick = 0;
    std::uint32_t g_mr_eof_skip = 0;

    void MrWatch()
    {
        const auto now = ::GetTickCount();

        // 空闲 1.5s 视为新一次解析，重新给预算
        if (now - g_last_tick > 1500u)
        {
            g_panic = false;
            g_eof_base = g_mr_eof_skip;
        }

        g_last_tick = now;

        if (!g_panic && g_mr_eof_skip - g_eof_base >= 2000u)
        {
            g_panic = true;
        }
    }

    // cp <= 0xFF 必须原样写一个字节：读端只推进 1 字节；只有 CJK 才真编成 UTF-8
    int MrEncode(std::uint32_t cp, uchar *out)
    {
        if (cp <= 0xFFu)
        {
            out[0] = static_cast<uchar>(cp);
            return 1;
        }

        return static_cast<int>(utf8::unchecked::append(cp, out) - out);
    }

    // 越界码点（EOF 哨兵 -1）：按原版 `mov [..], al` 只写低字节
    int MrEncodeRaw(std::uint32_t cp, uchar *out)
    {
        if (cp <= 0x10FFFFu)
            return MrEncode(cp, out);

        out[0] = static_cast<uchar>(cp & 0xFFu);
        return 1;
    }

    // 写前先算要几字节，放不下就整个丢弃（不推进游标）
    bool MrWriteAt(uchar* ptr, int cursor, std::uint32_t cp, int& out_len)
    {
        uchar tmp[8];
        const auto n = MrEncodeRaw(cp, tmp);

        if (cursor < 0 || cursor + n > kMrBufCap)
        {
            return false;
        }

        std::memcpy(ptr, tmp, static_cast<std::size_t>(n));
        out_len = n;
        return true;
    }

    // +0x44F 是「还有更多要读」标志，只能按裸偏移写；EOF 时原版没碰它 → while 出不去
    void MrClearMoreFlag(class_for_mr *pclass)
    {
        *reinterpret_cast<unsigned char *>(reinterpret_cast<char *>(pclass) + 0x44F) = 0;
    }

    // 回退一个 UTF-8 序列：落点钳回 f48[0]，并保证 back >= 1（否则循环打转）
    bool MrBackOne(class_for_mr *pclass, std::uint32_t &cp, int &back)
    {
        if (g_panic)
        {
            if (pclass->f448 > 0)
                --pclass->f448;

            MrClearMoreFlag(pclass);
            cp = 0xFFFFFFFFu;
            back = 1;
            return true;
        }

        if (pclass->f448 <= 0)
        {
            pclass->f448 = 0;
            cp = 0;
            back = 0;
            return false;
        }

        auto ptr = &pclass->f48[pclass->f448];
        auto ptr2 = ptr;
        cp = utf8::unchecked::prior(ptr2);

        // f48 里的 0xFF 是 EOF 哨兵被 movzx 截断的产物，必须还原成真 EOF
        if (cp == 0xFFu)
        {
            cp = 0xFFFFFFFFu;
            MrClearMoreFlag(pclass);
        }

        if (ptr2 < &pclass->f48[0])
            ptr2 = &pclass->f48[0];

        back = static_cast<int>(ptr - ptr2);

        if (back <= 0)
            back = 1;

        if (back > pclass->f448)
            back = pclass->f448;

        pclass->f448 -= back;
        return true;
    }
}

bool MrPanicked()
{
    return g_panic;
}

// class_for_mr 布局与 1.0.7 一致，只是把单字节步进换成 UTF-8 序列步进。读方向：next()
struct mr_read_eax_edx
{
    void operator()(injector::reg_pack &regs) const
    {
        MrWatch();

        if (g_panic)
        {
            regs.eax = static_cast<std::uintptr_t>(-1);
            return;
        }

        auto ptr = reinterpret_cast<char *>(regs.eax + regs.edx);
        auto ptr2 = ptr;
        regs.eax = utf8::unchecked::next(ptr2);
        auto offset = ptr2 - ptr;
        regs.edx += offset;
    }
};

struct mr_read_edx_eax
{
    void operator()(injector::reg_pack &regs) const
    {
        MrWatch();

        if (g_panic)
        {
            regs.ebx = static_cast<std::uintptr_t>(-1);
            return;
        }

        auto ptr = reinterpret_cast<char *>(regs.edx + regs.eax);
        auto ptr2 = ptr;
        regs.ebx = utf8::unchecked::next(ptr2);
        auto offset = ptr2 - ptr;
        regs.eax += offset;
    }
};

struct mr_read_edx_eax2
{
    void operator()(injector::reg_pack &regs) const
    {
        MrWatch();

        if (g_panic)
        {
            regs.edx = static_cast<std::uintptr_t>(-1);
            return;
        }

        auto ptr = reinterpret_cast<char *>(regs.edx + regs.eax);
        auto ptr2 = ptr;
        regs.edx = utf8::unchecked::next(ptr2);
        auto offset = ptr2 - ptr;
        regs.eax += offset;
    }
};

// 读方向：回退一个序列（prior()）
struct mr_read_back_esi_eax
{
    void operator()(injector::reg_pack &regs) const
    {
        MrWatch();

        auto pclass = reinterpret_cast<class_for_mr *>(regs.esi);

        std::uint32_t cp = 0;
        int back = 0;
        MrBackOne(pclass, cp, back);

        regs.eax = cp;
    }
};

struct mr_read_back_esi_ecx
{
    void operator()(injector::reg_pack &regs) const
    {
        MrWatch();

        auto pclass = reinterpret_cast<class_for_mr *>(regs.esi);

        std::uint32_t cp = 0;
        int back = 0;
        MrBackOne(pclass, cp, back);

        regs.ebx = cp;
    }
};

struct mr_read_back_eax_ecx
{
    void operator()(injector::reg_pack &regs) const
    {
        MrWatch();

        // eax 进来是 this，出去被改成读到的字符，必须先存成局部量
        auto pclass = reinterpret_cast<class_for_mr *>(regs.eax);

        std::uint32_t cp = 0;
        int back = 0;
        MrBackOne(pclass, cp, back);

        regs.eax = cp;
    }
};

// 写方向：把读到的字符编成 UTF-8 写回 f48
struct mr_write_esi_ecx
{
    void operator()(injector::reg_pack &regs) const
    {
        MrWatch();

        auto pclass = reinterpret_cast<class_for_mr *>(regs.esi);

        // EOF 必须照原版把 0xFF 压进 f48：调用方靠「f448 从 0 变 1」确认吃到字符
        if (regs.eax == static_cast<std::uintptr_t>(-1))
        {
            ++g_mr_eof_skip;

            if (pclass->f448 >= 0 && pclass->f448 < 0x400)
            {
                pclass->f48[pclass->f448] = 0xFF;
                pclass->f448 += 1;
            }

            return;
        }

        if (g_panic)
        {
            return;
        }

        int n = 0;

        if (MrWriteAt(&pclass->f48[pclass->f448], pclass->f448, static_cast<std::uint32_t>(regs.eax), n))
            pclass->f448 += n;
    }
};

struct mr_write_esi_eax
{
    void operator()(injector::reg_pack &regs) const
    {
        MrWatch();

        auto pclass = reinterpret_cast<class_for_mr *>(regs.esi);

        if (g_panic)
        {
            return;
        }

        int n = 0;

        if (MrWriteAt(&pclass->f48[pclass->f448], pclass->f448, regs.ebx, n))
            pclass->f448 += n;
    }
};

struct mr_write_edi_ebp
{
    void operator()(injector::reg_pack &regs) const
    {
        MrWatch();

        if (g_panic)
        {
            regs.edi += 1;
            regs.ecx = regs.esi;
            return;
        }

        auto ptr = reinterpret_cast<char *>(regs.edi + regs.ebp);

        // &nbsp 之类；中文字库最小码位 0x2014，这么偷懒可以
        if (regs.eax > 0x100 && regs.eax < 0x200)
            regs.eax -= 0x100;

        const auto offset = MrEncode(regs.eax, reinterpret_cast<uchar *>(ptr));
        regs.edi += offset;
        regs.ecx = regs.esi;
    }
};

namespace
{
    // 标签主循环 0x4B1020 唯一出口是 '>'，EOF 后永远等不到：整段换跳板开逃逸门
    std::uintptr_t g_tag_exit_va = 0;
    std::uintptr_t g_tag_back_va = 0;

    __declspec(naked) void mr_tag_eof_guard()
    {
        __asm
        {
            add esp, 4                   ; 本函数不 ret，丢掉 MakeCALL 压入的返回地址
            cmp eax, -1
            je  do_exit
            cmp al, 0FFh                 ; EOF 哨兵 0xFF（movzx 截断后的形态）
            je  do_exit
            cmp eax, 3Eh                 ; 原指令：给 0x4B1088 的 je 准备 ZF
            push ecx
            push edx
            mov  ecx, dword ptr [ebp + 448h]
            lea  edx, [ecx + 1]
            mov  dword ptr [ebp + 448h], edx
            mov  byte ptr [ecx + ebp + 48h], al
            pop  edx
            pop  ecx
            jmp  dword ptr [g_tag_back_va]
            ; EOF：跳循环出口
        do_exit:
            jmp  dword ptr [g_tag_exit_va]
        }
    }
}

namespace
{
    // 0x4B1280 的 EOF 出口补丁 [0x4B13D8, 0x4B13EA)：原版没动 0x44F，EOF 后内层循环没出口
    std::uintptr_t g_mr_more_va = 0;

    __declspec(naked) void mr_html_eof_stop()
    {
        __asm
        {
            add esp, 4                    ; 本函数不 ret
            cmp eax, -1                   ; 到 0x4B13AC 时 eax==-1 唯一含义就是 EOF
            jne normal
            mov byte ptr [esi + 44Fh], 0  ; EOF：没有更多可读，停掉调用方的内层循环
            jmp dword ptr [g_mr_more_va]
        normal:
            mov eax, dword ptr [esp + 14h] ; 原指令：取 maxlen
            add eax, -1
            cmp edi, eax
            jne done
            mov byte ptr [esi + 44Fh], 1   ; 缓冲装满：还要再读一段（正当路径）
        done:
            jmp dword ptr [g_mr_more_va]
        }
    }
}

void install_mail_reply()
{
    // 邮件回复（Native ADD_FIRST_N_CHARACTERS_OF_STRING_TO_HTML_SCRIPT_OBJECT 内）
    plugin.game.game_addr.fnMailAppendByteString = injector::aslr_ptr(0x4B4AE0).get();

    // 去掉 </span>；1.0.4 没有 <span content> 那一组
    injector::MakeNOP(injector::aslr_ptr(0x4B4A0F).get(), 10);
    injector::MakeNOP(injector::aslr_ptr(0x4B4A6F).get(), 10);

    // 读 utf8 序列：用到 "!--" 的函数
    injector::MakeInline<mr_read_eax_edx>(injector::aslr_ptr(0x467174).get(), injector::aslr_ptr(0x467174) + 7);

    // CHtmlParser vftable 第三个函数
    injector::MakeInline<mr_read_edx_eax>(injector::aslr_ptr(0x4B0C11).get(), injector::aslr_ptr(0x4B0C11) + 7);

    // MailGetStringChar 内
    injector::MakeInline<mr_read_eax_edx>(injector::aslr_ptr(0x466A1C).get(), injector::aslr_ptr(0x466A1C) + 7);

    // 回退一个序列："!--" 函数内两处
    injector::MakeInline<mr_read_back_esi_eax>(injector::aslr_ptr(0x467153).get(), injector::aslr_ptr(0x467153) + 17);
    injector::MakeInline<mr_read_back_esi_eax>(injector::aslr_ptr(0x4671AA).get(), injector::aslr_ptr(0x4671AA) + 17);

    // CHtmlParser vftable[3] 内回退
    injector::MakeInline<mr_read_back_eax_ecx>(injector::aslr_ptr(0x4669FA).get(), injector::aslr_ptr(0x4669FA) + 18);

    // 写 utf8 序列："!--" 函数内
    injector::MakeInline<mr_write_esi_ecx>(injector::aslr_ptr(0x467342).get(), injector::aslr_ptr(0x467342) + 13);

    // 写 utf8 序列：CHtmlParser vftable[3] 内
    injector::MakeInline<mr_write_esi_eax>(injector::aslr_ptr(0x4B0E7F).get(), injector::aslr_ptr(0x4B0E7F) + 19);

    // 写 utf8 序列：vftable[3] 调用的函数内
    injector::MakeInline<mr_write_edi_ebp>(injector::aslr_ptr(0x4B139A).get(), injector::aslr_ptr(0x4B139A) + 8);

    // GET_FIRST_N_CHARACTERS_OF_STRING 里的 strncpy：缓冲定长、size 是字节数
    injector::MakeCALL(injector::aslr_ptr(0xA1150D).get(), StrncpyByteLimited);
    injector::MakeNOP(injector::aslr_ptr(0xA11516).get(), 7);

    // "!--" 函数里同构的内联分支
    injector::MakeInline<mr_read_eax_edx>(injector::aslr_ptr(0x4671CB).get(), injector::aslr_ptr(0x4671CB) + 7);
    injector::MakeInline<mr_read_back_esi_eax>(injector::aslr_ptr(0x46727D).get(), injector::aslr_ptr(0x46727D) + 17);
    injector::MakeInline<mr_write_esi_ecx>(injector::aslr_ptr(0x467203).get(), injector::aslr_ptr(0x467203) + 13);
    injector::MakeInline<mr_write_esi_ecx>(injector::aslr_ptr(0x467243).get(), injector::aslr_ptr(0x467243) + 13);
    injector::MakeInline<mr_write_esi_ecx>(injector::aslr_ptr(0x467315).get(), injector::aslr_ptr(0x467315) + 13);

    // vftable[3] 把读到的字符写回缓冲
    injector::MakeInline<mr_write_esi_ecx>(injector::aslr_ptr(0x4B0B9E).get(), injector::aslr_ptr(0x4B0B9E) + 19);
    injector::MakeInline<mr_write_esi_ecx>(injector::aslr_ptr(0x4B0D0F).get(), injector::aslr_ptr(0x4B0D0F) + 19);

    // vftable[3] 里回退一个字符
    injector::MakeInline<mr_read_back_esi_ecx>(injector::aslr_ptr(0x4B0BEF).get(), injector::aslr_ptr(0x4B0BEF) + 18);

    // "!--" 函数的第三处读取
    injector::MakeInline<mr_read_edx_eax2>(injector::aslr_ptr(0x46729E).get(), injector::aslr_ptr(0x46729E) + 7);

    // 解析 &xxx; 实体的函数
    injector::MakeInline<mr_read_back_esi_eax>(injector::aslr_ptr(0x4B142D).get(), injector::aslr_ptr(0x4B142D) + 18);
    injector::MakeInline<mr_read_edx_eax2>(injector::aslr_ptr(0x4B144F).get(), injector::aslr_ptr(0x4B144F) + 7);

    // 跳空白再回退的函数
    injector::MakeInline<mr_read_back_esi_eax>(injector::aslr_ptr(0x46709A).get(), injector::aslr_ptr(0x46709A) + 18);
    injector::MakeInline<mr_read_eax_edx>(injector::aslr_ptr(0x4670BC).get(), injector::aslr_ptr(0x4670BC) + 7);
    injector::MakeInline<mr_write_esi_ecx>(injector::aslr_ptr(0x4670FC).get(), injector::aslr_ptr(0x4670FC) + 19);

    // 正面修复之二：给 0x4B1280 的 EOF 出口清掉「还要再读」标志
    g_mr_more_va = reinterpret_cast<std::uintptr_t>(injector::aslr_ptr(0x4B13EA).get<unsigned char>());
    injector::MakeCALL(injector::aslr_ptr(0x4B13D8).get(), mr_html_eof_stop);
    injector::MakeNOP(injector::aslr_ptr(0x4B13DD).get(), 0x4B13EA - 0x4B13DD);

    // 正面修复：给 0x4B1020 标签主循环开一个 EOF 逃逸门
    g_tag_exit_va = reinterpret_cast<std::uintptr_t>(injector::aslr_ptr(0x4B1218).get<unsigned char>());
    g_tag_back_va = reinterpret_cast<std::uintptr_t>(injector::aslr_ptr(0x4B1088).get<unsigned char>());
    injector::MakeCALL(injector::aslr_ptr(0x4B1072).get(), mr_tag_eof_guard);
    injector::MakeNOP(injector::aslr_ptr(0x4B1077).get(), 0x4B1088 - 0x4B1077);

    // f48 弹出点 `movzx reg, byte [eax+esi+0x48]` 一律不要挂：0x467090 不只服务 HTML 解析，
    // 普通文本渲染也调它，那时 esi 不是 class_for_mr，一读 [esi+0x448] 就是野内存

}
}
