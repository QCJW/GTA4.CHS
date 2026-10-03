#include "gta_mail_reply.h"
#include "gta_string.h"
#include "plugin.h"

namespace gta_mail_reply
{
// 413332
// 5E49FF
struct mr_read_eax_edx
{
    void operator()(injector::reg_pack &regs) const
    {
        auto ptr = reinterpret_cast<char *>(regs.eax + regs.edx);
        auto ptr2 = ptr;
        regs.eax = utf8::unchecked::next(ptr2);
        auto offset = ptr2 - ptr;
        regs.edx += offset;
    }
};

// 412A9C
struct mr_read_edx_eax
{
    void operator()(injector::reg_pack &regs) const
    {
        auto ptr = reinterpret_cast<char *>(regs.edx + regs.eax);
        auto ptr2 = ptr;
        regs.ebx = utf8::unchecked::next(ptr2);
        auto offset = ptr2 - ptr;
        regs.eax += offset;
    }
};

// 同上，但结果写回 edx
struct mr_read_edx_eax2
{
    void operator()(injector::reg_pack &regs) const
    {
        auto ptr = reinterpret_cast<char *>(regs.edx + regs.eax);
        auto ptr2 = ptr;
        regs.edx = utf8::unchecked::next(ptr2);
        auto offset = ptr2 - ptr;
        regs.eax += offset;
    }
};

struct mr_read_back_esi_eax
{
    void operator()(injector::reg_pack &regs) const
    {
        auto pclass = reinterpret_cast<class_for_mr *>(regs.esi);
        auto ptr = &pclass->f48[pclass->f448];
        auto ptr2 = ptr;
        regs.eax = utf8::unchecked::prior(ptr2);
        pclass->f448 -= (ptr - ptr2);
    }
};

// 5E49DE
struct mr_read_back_esi_ecx
{
    void operator()(injector::reg_pack &regs) const
    {
        auto pclass = reinterpret_cast<class_for_mr *>(regs.esi);
        auto ptr = &pclass->f48[pclass->f448];
        auto ptr2 = ptr;
        regs.ebx = utf8::unchecked::prior(ptr2);
        pclass->f448 -= (ptr - ptr2);
    }
};

// 412A7A
struct mr_read_back_eax_ecx
{
    void operator()(injector::reg_pack &regs) const
    {
        auto pclass = reinterpret_cast<class_for_mr *>(regs.eax);
        auto ptr = &pclass->f48[pclass->f448];
        auto ptr2 = ptr;
        regs.eax = utf8::unchecked::prior(ptr2);
        pclass->f448 -= (ptr - ptr2);
    }
};

struct mr_write_esi_ecx
{
    void operator()(injector::reg_pack &regs) const
    {
        auto pclass = reinterpret_cast<class_for_mr *>(regs.esi);
        auto ptr = &pclass->f48[pclass->f448];
        auto offset = utf8::unchecked::append(regs.eax, ptr) - ptr;
        pclass->f448 += offset;
    }
};

// 5E4CCE
struct mr_write_esi_eax
{
    void operator()(injector::reg_pack &regs) const
    {
        auto pclass = reinterpret_cast<class_for_mr *>(regs.esi);
        auto ptr = &pclass->f48[pclass->f448];
        auto offset = utf8::unchecked::append(regs.ebx, ptr) - ptr;
        pclass->f448 += offset;
    }
};

struct mr_write_edi_ebp
{
    void operator()(injector::reg_pack &regs) const
    {
        auto ptr = reinterpret_cast<char *>(regs.edi + regs.ebp);

        // 对&nbsp之类字符的特殊处理，从sub_5E5250的逻辑来看，这么偷懒是可以的
        // 目前中文字库最小的值是0x2014
        if (regs.eax > 0x100 && regs.eax < 0x200)
            regs.eax -= 0x100;

        auto offset = utf8::unchecked::append(regs.eax, ptr) - ptr;
        regs.edi += offset;
        regs.ecx = regs.esi;
    }
};

// 8C5AB1
struct dlc_truncate
{
    void operator()(injector::reg_pack &regs) const
    {
        auto ptr = &plugin.game.game_addr.pDLCTruncateBuffer[regs.esi + regs.eax];

        auto offset = utf8::unchecked::append(*reinterpret_cast<const GTAChar *>(regs.edi), ptr) - ptr;
        regs.eax += offset - 1;
    }
};

void register_patches()
{
    // Native: ADD_FIRST_N_CHARACTERS_OF_STRING_TO_HTML_SCRIPT_OBJECT(75FC34EF)里面
    // 邮件回复
    // 要用的函数的地址
    plugin.game.game_addr.fnMailAppendByteString = injector::aslr_ptr(0x4C4B90).get();

    // 接收宽字符，但添加的是utf8
    injector::MakeCALL(injector::aslr_ptr(0x4BEF90).get(), gta_string::gtaMailAppendWideStringAsUtf8);

    // 去掉span标签
    // mov edx,标签串; mov eax,esi; call fnMailAppendByteString 为一组，共两组，整组NOP
    injector::MakeNOP(injector::aslr_ptr(0x4BEF7D).get(), 12);
    injector::MakeNOP(injector::aslr_ptr(0x4BEF95).get(), 12);

    // 读utf8序列
    // 用到"!--"的函数
    injector::MakeInline<mr_read_eax_edx>(injector::aslr_ptr(0x46A564).get(), injector::aslr_ptr(0x46A564 + 7).get());

    // IDA Names: const CHtmlParser::`vftable'
    // 虚表的第三个函数
    injector::MakeInline<mr_read_edx_eax>(injector::aslr_ptr(0x4C3AF1).get(), injector::aslr_ptr(0x4C3AF1 + 7).get());

    // CHtmlParser虚表第三个函数开头调用了MailGetStringChar
    // 在MailGetStringChar里面
    // 读utf8序列
    injector::MakeInline<mr_read_eax_edx>(injector::aslr_ptr(0x469E0C).get(), injector::aslr_ptr(0x469E0C + 7).get());

    // 在上述用到"!--"的函数里面
    // 读上一个utf8序列
    injector::MakeInline<mr_read_back_esi_eax>(injector::aslr_ptr(0x46A543).get(),
                                               injector::aslr_ptr(0x46A543 + 17).get());

    injector::MakeInline<mr_read_back_esi_eax>(injector::aslr_ptr(0x46A59A).get(),
                                               injector::aslr_ptr(0x46A59A + 17).get());

    // 在上述CHtmlParser虚表第三个函数里面
    // 读上一个utf8序列
    injector::MakeInline<mr_read_back_eax_ecx>(injector::aslr_ptr(0x469DEA).get(),
                                               injector::aslr_ptr(0x469DEA + 18).get());

    // 在上述用到"!--"的函数里面
    // 写utf8序列
    injector::MakeInline<mr_write_esi_ecx>(injector::aslr_ptr(0x46A732).get(), injector::aslr_ptr(0x46A732 + 13).get());

    // 在上述CHtmlParser虚表第三个函数里面
    // 写utf8序列
    injector::MakeInline<mr_write_esi_eax>(injector::aslr_ptr(0x4C3D5F).get(), injector::aslr_ptr(0x4C3D5F + 19).get());

    // 在上述CHtmlParser虚表第三个函数调用的函数里面
    // 写utf8序列
    injector::MakeInline<mr_write_edi_ebp>(injector::aslr_ptr(0x4C427A).get(), injector::aslr_ptr(0x4C427A + 8).get());

    // Native: CONVERT_THEN_ADD_STRING_TO_HTML_SCRIPT_OBJECT(72EC0AA6)的转换过程里面
    plugin.game.game_addr.pDLCTruncateBuffer = injector::aslr_ptr(0x11FB738).get();

    // DLC不支持日语而加的转换逻辑
    injector::MakeInline<dlc_truncate>(injector::aslr_ptr(0x8A36C1).get(), injector::aslr_ptr(0x8A36C1 + 8).get());

    // Native: GET_FIRST_N_CHARACTERS_OF_STRING(108B4A25)调用strncpy的地方
    injector::MakeCALL(injector::aslr_ptr(0xB538F6).get(), gta_string::gtaUTF8strncpy);
    injector::MakeNOP(injector::aslr_ptr(0xB538FF).get(), 7);

    // "!--"函数里同构的内联分支，漏补会把多字节字符退化成单字节逻辑
    // 第二处读取（判断"!--"）
    injector::MakeInline<mr_read_eax_edx>(injector::aslr_ptr(0x46A5BB).get(), injector::aslr_ptr(0x46A5BB + 7).get());
    // 第三处回退
    injector::MakeInline<mr_read_back_esi_eax>(injector::aslr_ptr(0x46A66D).get(),
                                               injector::aslr_ptr(0x46A66D + 17).get());
    // 另三处写序列
    injector::MakeInline<mr_write_esi_ecx>(injector::aslr_ptr(0x46A5F3).get(), injector::aslr_ptr(0x46A5F3 + 13).get());
    injector::MakeInline<mr_write_esi_ecx>(injector::aslr_ptr(0x46A633).get(), injector::aslr_ptr(0x46A633 + 13).get());
    injector::MakeInline<mr_write_esi_ecx>(injector::aslr_ptr(0x46A705).get(), injector::aslr_ptr(0x46A705 + 13).get());

    // vftable[3] 把读到的字符写回缓冲，不补会把多字节码点截成低字节
    injector::MakeInline<mr_write_esi_ecx>(injector::aslr_ptr(0x4C3A7E).get(), injector::aslr_ptr(0x4C3A7E + 19).get());
    injector::MakeInline<mr_write_esi_ecx>(injector::aslr_ptr(0x4C3BEF).get(), injector::aslr_ptr(0x4C3BEF + 19).get());

    // vftable[3] 里回退一个字符
    injector::MakeInline<mr_read_back_esi_ecx>(injector::aslr_ptr(0x4C3ACF).get(), injector::aslr_ptr(0x4C3ACF + 18).get());

    // "!--"函数的第三处读取
    injector::MakeInline<mr_read_edx_eax2>(injector::aslr_ptr(0x46A68E).get(), injector::aslr_ptr(0x46A68E + 7).get());

    // 解析 &xxx; 实体的函数
    injector::MakeInline<mr_read_back_esi_eax>(injector::aslr_ptr(0x4C430D).get(), injector::aslr_ptr(0x4C430D + 18).get());
    injector::MakeInline<mr_read_edx_eax2>(injector::aslr_ptr(0x4C432F).get(), injector::aslr_ptr(0x4C432F + 7).get());

    // 跳空白再回退的函数，不补会在缓冲为空时把多字节首字节单独截断
    injector::MakeInline<mr_read_back_esi_eax>(injector::aslr_ptr(0x46A48A).get(),
                                               injector::aslr_ptr(0x46A48A + 18).get());
    injector::MakeInline<mr_read_eax_edx>(injector::aslr_ptr(0x46A4AC).get(), injector::aslr_ptr(0x46A4AC + 7).get());
    injector::MakeInline<mr_write_esi_ecx>(injector::aslr_ptr(0x46A4EC).get(), injector::aslr_ptr(0x46A4EC + 19).get());
}
} // namespace gta_mail_reply
