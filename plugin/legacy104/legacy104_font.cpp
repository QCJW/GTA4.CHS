#include "../gta_font.h"
#include "legacy104.h"
#include "legacy104_narrow.h"
#include "../font.h"
#include "../plugin.h"

// CJK 图集句柄（外部链接），104 自己写；必须在文件作用域，否则被判成内部链接
extern void *CNFont;

// 104 的 ProcessString 用 ebp 当全局基址，不能用 107 那个「打 0x80000000」（会在 0x7F8BC9 被解引用）；
// 改为只登记 ProcessToken 返回值，由 SkipWord_Prolog104 比对决定是否跳词
namespace gta4chs::legacy104
{
namespace
{
    // 「刚出 token」的一次性登记；串级入口也要清，跨串残留会误判下一个 SkipWord
    static const char *g_token_ret = nullptr;

    // 菜单/字幕入口：窄串→UTF-16 复用高版本翻译逻辑，出口再编码回窄串
    void ProcessStringRoutine104(float x, float y, const char *str, void *a4)
    {
        auto wide = narrow::utf16_scratch();

        // 串级入口：清掉 feeder 可能残留的半截序列
        narrow::reset_feeders();

        // 新串开始，作废上一串留下的 token 登记
        g_token_ret = nullptr;

        if (narrow::decode_utf8(str, wide, 1024) == 0)
        {
            plugin.game.Font_ProcessString(x, y, reinterpret_cast<const GTAChar *>(str), a4);
            narrow::flush_string_tail(x, y);
            return;
        }

        auto translated = plugin.string_table.GetString(wide);

        if (translated == wide)
        {
            plugin.game.Font_ProcessString(x, y, reinterpret_cast<const GTAChar *>(str), a4);
            narrow::flush_string_tail(x, y);
            return;
        }

        auto out = narrow::utf8_scratch();
        narrow::encode_utf8(translated, out, 4096);

        plugin.game.Font_ProcessString(x, y, reinterpret_cast<const GTAChar *>(out), a4);

        // 串出口：补画串尾那个字符，并清掉逐字节路径的残留状态
        narrow::flush_string_tail(x, y);
    }

    // 0x7F8B5A：cdecl、8 参、调用点自己清栈；走 injector::cstd，别用裸汇编 call
    const char *__cdecl GuardedProcessToken104(const char *text, uint *color, bool get_color,
        uchar *color_code, int *key_number, bool *is_new_line_token, char *text_to_show, void *token_data)
    {
        auto ret = injector::cstd<const char * (const char *, uint *, bool, uchar *, int *, bool *, char *, void *)>::call(
            plugin.game.game_addr.fnFont_ProcessToken, text, color, get_color, color_code,
            key_number, is_new_line_token, text_to_show, token_data);

        // 只登记返回值本身——它就是交给 SkipWord 的那个串指针
        g_token_ret = ret;
        return ret;
    }

    // 命中就「不跳词」；无论命中与否都要清登记，留着会在别的调用点误伤
    const char *SkipWord_Prolog104(std::uintptr_t address)
    {
        auto ptr = reinterpret_cast<const char *>(address & 0x7FFFFFFF);

        if (ptr == nullptr)
        {
            g_token_ret = nullptr;
            return nullptr;
        }

        const bool from_token = (ptr == g_token_ret);
        // 主循环每轮开头先调它、且排在 ProcessToken 之前，在这里作废上一轮登记
        g_token_ret = nullptr;

        if (!from_token)
        {
            ptr = narrow::SkipWord104(ptr);
        }

        while (*ptr != '\0')
        {
            char32_t cp = 0;
            // peek 不出码位也必须前进，否则主循环原地打转挂死游戏
            const auto len = narrow::peek_utf8(ptr, cp);

            if (len == 0)
            {
                ++ptr;
                break;
            }

            if (!CFont::IsSpecialPunctuationMark(static_cast<GTAChar>(cp)))
            {
                break;
            }

            ptr += len;
        }

        return ptr;
    }

    float __cdecl GuardedGetStringWidth104(const char *str, bool get_all)
    {
        g_token_ret = nullptr;

        return narrow::GetStringWidthRemake104(str, get_all);
    }

    float __cdecl GuardedGetMaxWordWidth104(const char *text)
    {
        return narrow::GetMaxWordWidth104(text);
    }

    // 必须返回原串指针而不是 nullptr：引擎立刻 `cmp byte ptr [esi],0`
    const char *__cdecl GuardedSkipWord104(std::uintptr_t address)
    {
        return SkipWord_Prolog104(address);
    }

    void __cdecl GuardedProcessString104(float x, float y, const char *str, void *a4)
    {
        ProcessStringRoutine104(x, y, str, a4);
    }

    // 测量遍（0x7F8F00）只算框尺寸；汉字不走虚函数机制，不标记会被画两遍
    void __cdecl GuardedProcessString104Measure(float x, float y, const char *str, void *a4)
    {
        narrow::BeginMeasurePass();
        ProcessStringRoutine104(x, y, str, a4);
        narrow::EndMeasurePass();
    }

    float __cdecl GuardedCharSizeNormal104(std::uint16_t chr)
    {
        return narrow::GetCharacterSizeNormalDispatch104(chr);
    }

    float __cdecl GuardedCharSizeDrawing104(std::uint16_t chr, bool use_extra_width)
    {
        return narrow::GetCharacterSizeDrawingDispatch104(chr, use_extra_width);
    }

    // RS 与 fnX 各一个包装：fnX 宽度内联算（不经 GSD），x 推进机制不同
    void __cdecl GuardedPrintChar104RS(float x, float y, std::uint16_t chr, bool buffered)
    {
        narrow::set_draw_site(narrow::draw_site::rs);
        narrow::PrintCharDispatch104(x, y, chr, buffered);
    }

    void __cdecl GuardedPrintChar104FnX(float x, float y, std::uint16_t chr, bool buffered)
    {
        narrow::set_draw_site(narrow::draw_site::fnx);
        narrow::PrintCharDispatch104(x, y, chr, buffered);
    }

    // 画汉字前排空攒批队列：汉字立即、原生字延迟，不排空谁盖谁逐帧不同 → 闪烁
    bool PrepareCjkDraw104(int)
    {
        narrow::FlushBufferedPrimitives();
        return true;
    }

    // 画完必须还纹理：不还后续 2D 图元会拿 CJK 图集采样 → 主菜单闪
    void FinishCjkDraw104(int n_font)
    {
        if (auto *tex = FontTextureAt(n_font))
        {
            plugin.game.Graphics_SetRenderState(tex);
        }
    }

    // 只记录最后一次查到的 font_chs：无条件覆盖会把 CNFont 清成 NULL
    void *__fastcall LoadTextureCB104(void *pDictionary, int, uint hash)
    {
        auto result = plugin.game.Dictionary_grcTexturePC_GetElementByKey(pDictionary, hash);

        if (auto *found = plugin.game.Dictionary_grcTexturePC_GetElementByKey(
                pDictionary, plugin.game.Hash_HashStringFromSeediCase("font_chs")))
        {
            CNFont = found;
        }

        return result;
    }

    void register_font_backend()
    {
        FontBackend be;
        be.font_info_stride = 0x250;
        be.font_details_stride = 0x44;
        be.texture_offset = 0x238;
        be.parse_token = &narrow::Font_ParseToken104;
        be.add_token_string_width = &narrow::Font_AddTokenStringWidth104;
        be.prepare_cjk_draw = &PrepareCjkDraw104;
        be.finish_cjk_draw = &FinishCjkDraw104;

        // 1.0.4 按钮表只到 0x12B，且只有 [1,63] 才量 token 串宽度
        be.button_token_max = 299;
        be.restrict_token_string_width = true;

        InstallFontBackend(be);
    }
}

void install_font()
{
    // font.cpp 靠这张表抹平版本差异，必须先登记再挂钩子
    register_font_backend();

    // CFont::ProcessString（1.0.4）
    plugin.game.game_addr.fnFont_ProcessString = injector::aslr_ptr(0x7F88F0).get();

    // 菜单项文本
    injector::MakeCALL(injector::aslr_ptr(0x7F8F91).get(), GuardedProcessString104);

    // 带框文字的「测量遍」：此前只汉化画字那遍，框按英文行数算 → 字压到框边
    injector::MakeCALL(injector::aslr_ptr(0x7F8F2F).get(), GuardedProcessString104Measure);

    // 另外三个直连 ProcessString 的点：窄串直连把每个字节当字形码 → 灰白杂字
    injector::MakeCALL(injector::aslr_ptr(0x4A8E05).get(), GuardedProcessString104);
    injector::MakeCALL(injector::aslr_ptr(0x4BD430).get(), GuardedProcessString104);
    injector::MakeCALL(injector::aslr_ptr(0x4BD4BF).get(), GuardedProcessString104);

    // 登记 ProcessToken 返回值，消费端 SkipWord_Prolog104；直接决定换行位置
    injector::MakeCALL(injector::aslr_ptr(0x7F8B5A).get(), GuardedProcessToken104);

    // 跳词 / 量宽 / 最大词宽：整串转码后复用高版本实现
    injector::MakeJMP(injector::aslr_ptr(0x7F23B0).get(), GuardedSkipWord104);

    injector::MakeJMP(injector::aslr_ptr(0x7F7CF0).get(), GuardedGetStringWidth104);
    injector::MakeJMP(injector::aslr_ptr(0x7F8830).get(), GuardedGetMaxWordWidth104);

    // 字符宽度：引擎逐字节驱动，用 feeder 攒完整序列（ATSW 两站 + GSW 一站）
    injector::MakeCALL(injector::aslr_ptr(0x7F35E7).get(), GuardedCharSizeNormal104);
    injector::MakeCALL(injector::aslr_ptr(0x7F3643).get(), GuardedCharSizeNormal104);
    injector::MakeCALL(injector::aslr_ptr(0x7F7FC7).get(), GuardedCharSizeNormal104);

    // 绘制宽度（RS 内 +0x506）
    injector::MakeCALL(injector::aslr_ptr(0x7F7C46).get(), GuardedCharSizeDrawing104);

    // 绘制字符：RS 内 +0x436、fnX 0x7F36A0 内 +0x189
    injector::MakeCALL(injector::aslr_ptr(0x7F7B76).get(), GuardedPrintChar104RS);
    injector::MakeCALL(injector::aslr_ptr(0x7F3829).get(), GuardedPrintChar104FnX);

    // fnX 把 GetCharacterSizeDrawing 内联了，在 addss/divss 处插跳板补等价逻辑
    narrow::install_inline_width_hook(0x7F38A4, 0x7F38B0);

    // 加载 fonts.wtd 中的 font_chs
    injector::MakeCALL(injector::aslr_ptr(0x7F543B).get(), LoadTextureCB104);
    injector::MakeCALL(injector::aslr_ptr(0x7F593B).get(), LoadTextureCB104);
}
}
