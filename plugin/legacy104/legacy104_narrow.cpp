#include "legacy104_narrow.h"
#include "../font.h"
#include "../plugin.h"
#include <intrin.h>

// 裸跳板要用 C 名字引用（内联汇编解析不了 C++ 修饰名）
extern "C" float gta4chs_104_width_dispatch(unsigned code, float raw_width, float extra_width);
extern "C" std::uintptr_t gta4chs_104_width_back = 0;

// fnX 内联宽度跳板。入口：bl=字符-0x20，xmm0=字形宽，xmm1=extra_width
extern "C" void __declspec(naked) gta4chs_104_width_trampoline()
{
    __asm
    {
            sub esp, 12
            movss dword ptr[esp + 4], xmm0
            movss dword ptr[esp + 8], xmm1
            movzx eax, bl
            mov dword ptr[esp], eax
            call gta4chs_104_width_dispatch
            add esp, 12
            sub esp, 4
            fstp dword ptr[esp]
            movss xmm0, dword ptr[esp]
            add esp, 4
            mov eax, gta4chs_104_width_back
            jmp eax
    }
}

namespace gta4chs::legacy104::narrow
{
    namespace
    {
        constexpr std::size_t kScratchChars = 1024;
        constexpr std::size_t kScratchBytes = kScratchChars * 4;

        // 状态按线程隔离（见 thread_state），这里只留 TLS 槽索引
        DWORD g_tls = TLS_OUT_OF_INDEXES;

        // 只有字节级管线线程能动这些状态；串级钩子并发跑会叠字
        DWORD g_render_tid = 0;

        // TokenType：0 普通 / 256..300 按键 / >=1000 blip。PrintChar 靠它选 blip 纹理，绝不能清
        inline std::uint32_t *rs_token_type()
        {
            return injector::aslr_ptr(0xEA22A0).get<std::uint32_t>();
        }

        // 攒批队列（3 段 ×170 条），画汉字前必须排空
        void flush_font_queue(unsigned font_index)
        {
            // 索引 3 会读到 pFont_BlipWidth，拿它当条数 memset 会写崩
            if (font_index > 2u)
            {
                font_index = 2u;
            }

            auto *counts = injector::aslr_ptr(0x10089F0).get<std::uint32_t>();

            if (counts == nullptr || counts[font_index] == 0u)
            {
                return;
            }

            injector::cstd<void(int)>::call(injector::aslr_ptr(0x7F2080).get<void>(),
                                           static_cast<int>(font_index));
        }

        // PrintChar 最近一次 feed 的快照；RS 里恒先于 GSD，各维护一个 feeder 会失步
        struct draw_snapshot
        {
            std::uint16_t chr = 0;
            int result = -1;
            GTAChar cp = 0;

            int has_second = 0;
            GTAChar cp2 = 0;
        };

        // 状态按线程隔离：串级钩子并发跑会互相踩 → 菜单整行逐帧乱跳
        struct thread_state
        {
            GTAChar u16[kScratchChars]{};
            char u8[kScratchBytes]{};
            std::size_t u8_offsets[kScratchChars + 1]{};

            feeder width_feeder;   // 量宽路径（GetCharacterSizeNormal）
            feeder draw_feeder;     // 绘制路径（PrintChar）
            feeder size_feeder;     // RS 循环里的 GetCharacterSizeDrawing
            feeder inline_feeder; // fnX 循环内联宽度的字节状态

            draw_snapshot last_draw{};
            // 已画出但没结算进 x 的字宽：每段首字 RS 少调一次 GSD，不补就叠字
            bool owe_advance = false;
            GTAChar owe_cp = 0;
            // 最近一次真画出去的位置与宽度，串尾补画靠它算落点
            draw_site site = draw_site::rs;

            bool painted = false;
            float last_px = 0.0f;
            float last_py = 0.0f;
            float last_pw = 0.0f;
            bool last_pbuffered = false;

            // >0 = 正处于「只测量不绘制」那一遍（见 BeginMeasurePass）
            int measure_depth = 0;
        };

        DWORD ensure_tls()
        {
            DWORD idx = g_tls;

            if (idx == TLS_OUT_OF_INDEXES)
            {
                const DWORD n = ::TlsAlloc();
                const DWORD prev = static_cast<DWORD>(::InterlockedCompareExchange(
                    reinterpret_cast<LONG *>(&g_tls),
                    static_cast<LONG>(n),
                    static_cast<LONG>(TLS_OUT_OF_INDEXES)));

                if (prev != TLS_OUT_OF_INDEXES)
                {
                    ::TlsFree(n);
                    idx = prev;
                }
                else
                {
                    idx = n;
                }
            }

            return idx;
        }

        thread_state &state()
        {
            const DWORD idx = ensure_tls();
            auto *p = static_cast<thread_state *>(::TlsGetValue(idx));

            if (p == nullptr)
            {
            p = new thread_state();
            ::TlsSetValue(idx, p);
            }

            return *p;
        }

        // 保留原标识符免得改几十处引用；宏只在完整标识符上生效，末尾 #undef
        #define g_u16            (state().u16)
        #define g_u8_offsets     (state().u8_offsets)
        #define g_u8             (state().u8)
        #define g_width_feeder   (state().width_feeder)
        #define g_draw_feeder    (state().draw_feeder)
        #define g_size_feeder    (state().size_feeder)
        #define g_inline_feeder  (state().inline_feeder)
        #define g_last_draw      (state().last_draw)
        #define g_owe_advance    (state().owe_advance)
        #define g_owe_cp         (state().owe_cp)
        #define g_site           (state().site)
        #define g_painted        (state().painted)
        #define g_last_px        (state().last_px)
        #define g_last_py        (state().last_py)
        #define g_last_pw        (state().last_pw)
        #define g_last_pbuffered (state().last_pbuffered)
        #define g_measure_depth  (state().measure_depth)

        // 记录 UTF-16 第 n 个字符 ↔ 字节偏移，只有 SkipWord104 需要；嵌套调用必须关掉
        std::size_t decode_utf8_impl(const char *src, GTAChar *dst, std::size_t dst_cap, bool record_offsets)
        {
            if (src == nullptr || dst_cap == 0)
            {
                return 0;
            }

            std::size_t out = 0;
            std::size_t i = 0;

            while (src[i] != '\0' && out + 1 < dst_cap)
            {
                const auto b0 = static_cast<unsigned char>(src[i]);
                char32_t cp = 0;
                std::size_t len = 1;

                if (b0 < 0x80)
                {
                    cp = b0;
                }
                else if ((b0 & 0xE0) == 0xC0)
                {
                    cp = b0 & 0x1Fu;
                    len = 2;
                }
                else if ((b0 & 0xF0) == 0xE0)
                {
                    cp = b0 & 0x0Fu;
                    len = 3;
                }
                else if ((b0 & 0xF8) == 0xF0)
                {
                    cp = b0 & 0x07u;
                    len = 4;
                }
                else
                {
                    cp = b0;
                }

            // 严格 UTF-8：续字节每个都要在 0x80-0xBF；0x80-0xFF 也可能是单字节图标
                for (std::size_t k = 1; k < len; ++k)
                {
                    const auto bk = static_cast<unsigned char>(src[i + k]);

                    if (bk == '\0' || (bk & 0xC0) != 0x80)
                    {
                        len = 1;
                        cp = b0;
                        break;
                    }

                    cp = (cp << 6) | (bk & 0x3Fu);
                }

                dst[out] = static_cast<GTAChar>(cp > 0xFFFF ? 0xFFFD : cp);

                if (record_offsets && out < kScratchChars)
                {
                    g_u8_offsets[out] = i;
                }

                ++out;
                i += len;
            }

            if (record_offsets)
            {
                g_u8_offsets[out] = i;
            }

            dst[out] = 0;
        return out;
    }
}

    void set_draw_site(draw_site site)
    {
        g_site = site;
    }

    void FlushBufferedPrimitives()
    {
        flush_font_queue(0);
        flush_font_queue(1);
        flush_font_queue(2);
    }

    // 带框文字一帧两遍：①算框(0x7F8F00) ②画字(0x7F8F60)。汉字不走引擎多态，① 只算不画
    void BeginMeasurePass()
    {
        ++g_measure_depth;
    }

    void EndMeasurePass()
    {
        // 测量遍只挡绘制，feed / 挂账照旧（也要靠 GSN / GSW 拿宽度）
        if (g_measure_depth > 0)
        {
            --g_measure_depth;
        }
    }

    void reset_feeders()
    {
        // 异线程调用一律不生效：清的是渲染线程正在用的活状态

        if (g_render_tid != 0 && ::GetCurrentThreadId() != g_render_tid)
        {
            return;
        }
        // 正在收续字节的 feeder 不能动且必须全有或全无：部分复位会让 feeder 相位错开

        const bool draw_busy = g_draw_feeder.busy();
        const bool size_busy = g_size_feeder.busy();
        const bool width_busy = g_width_feeder.busy();
        const bool inline_busy = g_inline_feeder.busy();

        if (draw_busy || size_busy || width_busy || inline_busy)
        {
            return;
        }

        g_draw_feeder.reset();
        g_size_feeder.reset();
        g_width_feeder.reset();
        g_inline_feeder.reset();
        // 有未结清的宽度挂账时不能作废快照：上一个字还没被 GSD 结算

        if (!g_owe_advance)
        {
            g_last_draw = draw_snapshot{};
            g_owe_advance = false;
        }
    }

    void flush_string_tail(float x, float y)
    {
        // 串尾没等到续字节的字节再也吐不出来 → 少最后一个字；落点从上一个字右边缘推算

        if (g_draw_feeder.tail_pending() && g_measure_depth == 0)
        {
            const auto cp = static_cast<GTAChar>(g_draw_feeder.pending_lead());
            const float px = g_painted ? g_last_px + g_last_pw : x;
            const float py = g_painted ? g_last_py : y;

            plugin.game.Font_PrintChar(px, py, static_cast<GTAChar>(cp - 0x20), g_last_pbuffered);
        }
        // 串确实结束了，残留的半截状态必须清掉

        g_draw_feeder.reset();
        g_size_feeder.reset();
        g_width_feeder.reset();
        g_inline_feeder.reset();

        g_last_draw = draw_snapshot{};
        g_owe_advance = false;
        g_painted = false;
    }

    GTAChar *utf16_scratch()
    {
        return g_u16;
    }

    char *utf8_scratch()
    {
        return g_u8;
    }

    feeder::result feeder::feed(std::uint16_t raw)
    {
        // 补吐是「一次 feed 出两字符」的例外，只在补吐轮置位，必须每轮先清

        _has_second = false;
        return feed_inner(raw);
    }

    feeder::result feeder::feed_inner(std::uint16_t raw)
    {
        // 引擎传进来的是「字符-0x20」，先还原成原始字节

        const auto b = static_cast<unsigned char>((raw + 0x20) & 0xFF);

        if (_need != 0 && (b & 0xC0) == 0x80)
        {
            _cp = (_cp << 6) | (b & 0x3Fu);

            if (++_have == _need)
            {
                _need = 0;
                _have = 0;
                _out_cp = _cp;
                return result::complete;
            }

            return result::pending;
        }
        // 上个序列断在半截：靠 UTF-8 自同步丢残尾，并用当前字节立刻起头

        if (_need != 0)
        {
            // 首字节后一个续字节都没跟上 → 其实是单字节图标/重音字符，必须补吐

            if (_have == 0)
            {
                const auto lead = _lead;

            // 当前字节必须同轮判定：压 stash 等下一轮 → 串尾没有下一轮，少最后一个字
                reset();
                const auto r2 = feed_inner(raw);

                if (r2 == result::ascii || r2 == result::complete)
                {
                    _second_cp = _out_cp;
                    _has_second = true;
                }
            // 本轮先吐 lead；_cp 保持新序列的累积起点，绝不能覆盖

                _out_cp = lead;
                return result::complete;
            }

            reset();
        }

        if (b < 0x80)
        {
            _cp = b;
            _out_cp = b;
            return result::ascii;
        }
        // 首字节判定必须与整串解码器同口径：0xF8-0xFF 非法，否则警星一个都不画

        if ((b & 0xF8) == 0xF0)
        {
            _cp = b & 0x07u;
            _need = 3;
        }
        else if ((b & 0xF0) == 0xE0)
        {
            _cp = b & 0x0Fu;
            _need = 2;
        }
        else if ((b & 0xE0) == 0xC0)
        {
            _cp = b & 0x1Fu;
            _need = 1;
        }
        else
        {
        // 0x80-0xBF 与 0xF8-0xFF 是合法单字节图标，按单字节透传
            _cp = b;
            _out_cp = b;
            return result::complete;
        }

        _have = 0;
        _lead = b;
        return result::pending;
    }

    std::size_t peek_utf8(const char *ptr, char32_t &cp)
    {
        const auto b0 = static_cast<unsigned char>(ptr[0]);

        if (b0 < 0x80)
        {
            cp = b0;
            return 1;
        }

        std::size_t len;
        char32_t value;

        if ((b0 & 0xE0) == 0xC0)
        {
            len = 2;
            value = b0 & 0x1Fu;
        }
        else if ((b0 & 0xF0) == 0xE0)
        {
            len = 3;
            value = b0 & 0x0Fu;
        }
        else if ((b0 & 0xF8) == 0xF0)
        {
            len = 4;
            value = b0 & 0x07u;
        }
        else
        {
            cp = b0;
            return 1;
        }

        for (std::size_t k = 1; k < len; ++k)
        {
            const auto bk = static_cast<unsigned char>(ptr[k]);
            // 与 decode_utf8 同口径，否则 0xCD 填充线会被合成多字节

            if (bk == '\0' || (bk & 0xC0) != 0x80)
            {
                cp = b0;
                return 1;
            }

            value = (value << 6) | (bk & 0x3Fu);
        }

        cp = value;
        return len;
    }

    std::size_t decode_utf8(const char *src, GTAChar *dst, std::size_t dst_cap)
    {
        return decode_utf8_impl(src, dst, dst_cap, true);
    }

    std::size_t encode_utf8(const GTAChar *src, char *dst, std::size_t dst_cap)
    {
        if (src == nullptr || dst_cap == 0)
        {
            return 0;
        }

        std::size_t n = 0;

        for (std::size_t i = 0; src[i] != 0 && n + 4 < dst_cap; ++i)
        {
            char32_t cp = src[i];
            // 先合成代理对再编码，否则退化成 CESU-8

            if (cp >= 0xD800 && cp <= 0xDBFF && src[i + 1] >= 0xDC00 && src[i + 1] <= 0xDFFF)
            {
                cp = 0x10000u + ((cp - 0xD800u) << 10) + (src[i + 1] - 0xDC00u);
                ++i;
            }

            if (cp < 0x80)
            {
                dst[n++] = static_cast<char>(cp);
            }
            else if (cp < 0x800)
            {
                dst[n++] = static_cast<char>(0xC0 | (cp >> 6));
                dst[n++] = static_cast<char>(0x80 | (cp & 0x3F));
            }
            else if (cp < 0x10000)
            {
                dst[n++] = static_cast<char>(0xE0 | (cp >> 12));
                dst[n++] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                dst[n++] = static_cast<char>(0x80 | (cp & 0x3F));
            }
            else
            {
                dst[n++] = static_cast<char>(0xF0 | (cp >> 18));
                dst[n++] = static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
                dst[n++] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                dst[n++] = static_cast<char>(0x80 | (cp & 0x3F));
            }
        }

        dst[n] = '\0';
        return n;
    }
    // 单字符量宽：原版字形表内的走引擎原路径（含 2 字节 UTF-8 的 U+0080-U+00FF）

    static float char_size_normal(GTAChar cp)
    {
        return IsNativeChar(cp)
            ? plugin.game.Font_GetCharacterSizeNormal(static_cast<GTAChar>(cp - 0x20))
            : CFont::GetCHSCharacterSizeNormal();
    }

    static float char_size_drawing(GTAChar cp, bool use_extra_width)
    {
        return IsNativeChar(cp)
            ? plugin.game.Font_GetCharacterSizeDrawing(static_cast<GTAChar>(cp - 0x20), use_extra_width)
            : CFont::GetCHSCharacterSizeDrawing(use_extra_width);
    }

    float GetCharacterSizeNormalDispatch104(std::uint16_t chr)
    {
        // 伪字形码不是「字节-0x20」，原样交回引擎；只比较低 8 位（eax 高位是残留垃圾）

        if ((chr & 0xFFu) >= kPseudoGlyphCode)
        {
            return plugin.game.Font_GetCharacterSizeNormal(static_cast<GTAChar>(chr & 0xFFu));
        }

        const auto r = g_width_feeder.feed(chr);
        // 首字节 / 中间字节不产出字符，返回 0 以免 x 被重复推进

        if (r != feeder::result::ascii && r != feeder::result::complete)
        {
            return 0.0f;
        }

        float w = char_size_normal(static_cast<GTAChar>(g_width_feeder.codepoint()));
        // 补吐轮一次出两字符：引擎一个字节只推进一次 x，两个宽度必须一起给

        if (g_width_feeder.has_second())
        {
            w += char_size_normal(static_cast<GTAChar>(g_width_feeder.second_codepoint()));
        }

        return w;
    }

    float GetCharacterSizeDrawingDispatch104(std::uint16_t chr, bool use_extra_width)
    {
        // 同上；必须早于 g_last_draw / g_size_feeder / g_owe_advance 返回

        if ((chr & 0xFFu) >= kPseudoGlyphCode)
        {
            return plugin.game.Font_GetCharacterSizeDrawing(static_cast<GTAChar>(chr & 0xFFu),
                                                            use_extra_width);
        }

        const bool matched = g_last_draw.result >= 0
            && ((g_last_draw.chr & 0xFFu) == (chr & 0xFFu));
        // 异线程量宽请求不属于本线程字节流，喂 feeder 或销账都会伤到渲染线程

        if (!matched && g_render_tid != 0 && ::GetCurrentThreadId() != g_render_tid)
        {
            return 0.0f;
        }

        const auto r = matched ? static_cast<feeder::result>(g_last_draw.result) : g_size_feeder.feed(chr);
        const auto cp = matched ? g_last_draw.cp : static_cast<GTAChar>(g_size_feeder.codepoint());
        const bool has2 = matched ? g_last_draw.has_second != 0 : g_size_feeder.has_second();
        const auto cp2 = matched ? g_last_draw.cp2 : static_cast<GTAChar>(g_size_feeder.second_codepoint());

        const bool utf8_byte = static_cast<unsigned char>((chr + 0x20) & 0xFF) >= 0x80;
        float w = 0.0f;
        // 快照失配 = 这一字节没有配套 PrintChar，必然从孤立续字节起步

        if (r == feeder::result::ascii || r == feeder::result::complete)
        {
            w = char_size_drawing(cp, use_extra_width);

            if (has2)
            {
                w += char_size_drawing(cp2, use_extra_width);
            }

            g_owe_advance = false;
        // 上一个画出来的汉字没拿到宽度，借这一轮补账
        }
        else if (g_owe_advance && utf8_byte)
        {
            w = CFont::GetCHSCharacterSizeDrawing(use_extra_width);
            g_owe_advance = false;
        }

        return w;
    }

    void PrintCharDispatch104(float x, float y, std::uint16_t chr, bool buffered)
    {
        // 只在这里认定管线归属：绘制必然在渲染线程，GSD 有被异线程调用的记录

        g_render_tid = ::GetCurrentThreadId();

        auto render_state = plugin.game.game_addr.pFont_RenderState;
        // TokenType != 0 时要画 blip 精灵：整机旁路（'~' 后的字节可能是汉字首字节）；TokenType 也绝不能清

        if (render_state != nullptr && render_state->TokenType != 0)
        {
            plugin.game.Font_PrintChar(x, y, static_cast<GTAChar>(chr), buffered);
            return;
        }

        const auto r = g_draw_feeder.feed(chr);

        g_last_draw.chr = chr;
        g_last_draw.result = static_cast<int>(r);
        g_last_draw.cp = static_cast<GTAChar>(g_draw_feeder.codepoint());
        g_last_draw.has_second = g_draw_feeder.has_second() ? 1 : 0;
        g_last_draw.cp2 = static_cast<GTAChar>(g_draw_feeder.second_codepoint());

        if (r != feeder::result::ascii && r != feeder::result::complete)
        {
            return;
        }

        const auto cp = static_cast<GTAChar>(g_draw_feeder.codepoint());
        // 原版字形表内的字符走引擎原路径（TokenType != 0 已在开头旁路）

        if (IsNativeChar(cp))
        {
            const float w1 = char_size_drawing(cp, true);
            plugin.game.Font_PrintChar(x, y, static_cast<GTAChar>(cp - 0x20), buffered);
            // 补吐轮：第二个字符画在第一个之后，x 由紧随的 GSD 一次结清

            if (g_draw_feeder.has_second())
            {
                const auto cp2 = static_cast<GTAChar>(g_draw_feeder.second_codepoint());
                plugin.game.Font_PrintChar(x + w1, y, static_cast<GTAChar>(cp2 - 0x20), buffered);
                g_last_pw = w1 + char_size_drawing(cp2, true);
            }
            else
            {
                g_last_pw = w1;
            }

            g_painted = true;
            g_last_px = x;
            g_last_py = y;
            g_last_pbuffered = buffered;
            return;
        }

        if (cp == 0x3000)
        {
            return;
        }

        if (y < -0.06558f || y > 1.0f)
        {
            return;
        }

        if (-(CFont::GetCHSCharacterSizeDrawing(true) / render_state->fScaleX) > x || x > 1.0f)
        {
            return;
        }
        // 只对 RS 挂账：fnX 宽度由内联跳板结算，挂这里会被下次 GSD 误销

        if (g_site == draw_site::rs)
        {
            g_owe_advance = true;
            g_owe_cp = cp;
        }

        if (g_measure_depth > 0)
        {
            return;
        }

        CFont::PrintCHSChar(x, y, cp);

        g_painted = true;
        g_last_px = x;
        g_last_py = y;
        g_last_pw = CFont::GetCHSCharacterSizeDrawing(true);
        g_last_pbuffered = buffered;
    }

    const char *SkipWord104(const char *str)
    {
        if (str == nullptr)
        {
            return str;
        }

        reset_feeders();

        const auto n = decode_utf8(str, g_u16, kScratchChars);

            // 返回 str+0：换行循环靠 SkipWord 推进，越界返回 str+1 风险更大
        if (n == 0)
        {
            return str;
        }

        const auto advanced = CFont::SkipWord(g_u16) - g_u16;

        // 一步没进 = 原版「首字节就是空格 / '~'」，原版此时原样返回入参，必须照抄
        if (advanced <= 0)
        {
            return str + g_u8_offsets[0];
        }

        if (static_cast<std::size_t>(advanced) > n)
        {
            return str + g_u8_offsets[n];
        }

        return str + g_u8_offsets[advanced];
    }

    float GetStringWidthRemake104(const char *str, bool get_all)
    {
        if (str == nullptr)
        {
            return 0.0f;
        }

        reset_feeders();

        if (decode_utf8(str, g_u16, kScratchChars) == 0)
        {
            return 0.0f;
        }

        const float w = CFont::GetStringWidthRemake(g_u16, get_all);

        return w;
    }

    // GetMaxWordWidth 专用缓冲，不能用 g_u16：内层 ParseToken104 会原地覆盖它
    static GTAChar g_max_word_scratch[kScratchChars];

    float GetMaxWordWidth104(const char *text)
    {
        if (text == nullptr)
        {
            return 0.0f;
        }

        reset_feeders();

        if (decode_utf8(text, g_max_word_scratch, kScratchChars) == 0)
        {
            return 0.0f;
        }

        float max_word_width = 0.0f;

        const GTAChar *p = plugin.string_table.GetString(g_max_word_scratch);
        int guard = 0;

        while (*p != 0)
        {
            // 保险丝：踩到原地返回就是死循环
            if (++guard > 4096)
            {
                break;
            }

            const float w = CFont::GetStringWidthRemake(p, false);

            if (w > max_word_width)
            {
                max_word_width = w;
            }

            // 照抄原版：SkipWord 后只前进一个分隔符，不是 SkipSpaces
            p = CFont::SkipWord(p);

            if (*p == 0)
            {
                break;
            }

            ++p;
        }

        return max_word_width;
    }

    int Font_ParseToken104(const GTAChar *str, GTAChar *token_string, TokenStruct *token_data)
    {
        if (str == nullptr)
        {
        // 与引擎「空串直接返回 0xFF」一致
            return 0xFF;
        }
        // 引擎内部栈缓冲上限 0x28，这里留余量

        char in_buf[1024];
        char token_buf[64];
        TokenStruct104 td104{};

        encode_utf8(str, in_buf, sizeof(in_buf));
        token_buf[0] = '\0';

        const int token_type = injector::cstd<int(const char *, char *, TokenStruct104 *)>::call(
            plugin.game.game_addr.fnFont_ParseToken, in_buf, token_buf, &td104);

        if (token_data != nullptr)
        {
            token_data->f110 = td104.f110;

            for (int i = 0; i < 4; ++i)
            {
                token_data->f0[i] = td104.f0[i];
                decode_utf8_impl(td104.f10[i], token_data->f10[i], 32, false);
            }
        }

        if (token_string != nullptr)
        {
            decode_utf8_impl(token_buf, token_string, 64, false);
        }

        return token_type;
    }

    void Font_AddTokenStringWidth104(const GTAChar *text, float *width, int render_index)
    {
        if (text == nullptr || width == nullptr)
        {
            return;
        }

        char buf[512];
        encode_utf8(text, buf, sizeof(buf));

        injector::cstd<void(const char *, float *, int)>::call(
        // 首字节 / 中间字节不绘制，等序列收满在最后一个字节处一次画完
            plugin.game.game_addr.fnFont_AddTokenStringWidth, buf, width, render_index);
    }

    float GetDrawingWidth104(unsigned code, float raw_width, float extra_width)
    {
        const auto rs = plugin.game.game_addr.pFont_RenderState;
        const float edge = rs->fEdgeSize;

        // 不匹配就返回 -edge 抵消引擎随后加的 fEdgeSize，净推进 0；只比较低 8 位
        if ((g_last_draw.chr & 0xFFu) != (code & 0xFFu) || g_last_draw.result < 0)
        {
            return -edge;
        }

        const auto r = static_cast<feeder::result>(g_last_draw.result);
        const auto cp = g_last_draw.cp;
        float out;

        if (r != feeder::result::ascii && r != feeder::result::complete)
        {
            // pending 同理：不抵消每个中间字节都会把 x 推进 edge*scaleX
            out = -edge;
        }

        else if (IsNativeChar(cp))
        {
            out = (raw_width + extra_width) / *plugin.game.game_addr.pFont_ResolutionX;
        }
        else
        {
            // 要的是「除完分辨率、还没加 fEdgeSize」的中间量
            out = CFont::GetCHSCharacterSizeDrawing(true) / rs->fScaleX - edge;
        }

        // 补吐轮第二字符：引擎没算过 raw_width，按同一中间量口径补
        if (g_last_draw.has_second != 0)
        {
            out += char_size_drawing(g_last_draw.cp2, extra_width != 0.0f) / rs->fScaleX - edge;
        }

        return out;
    }

    void install_inline_width_hook(std::uintptr_t site, std::uintptr_t back)
    {
        // back 也要过 ASLR 翻译：跳板里的 jmp 用运行时地址
        gta4chs_104_width_back = reinterpret_cast<std::uintptr_t>(injector::aslr_ptr(back).get<void>());
        injector::MakeJMP(injector::aslr_ptr(site).get(), gta4chs_104_width_trampoline);
        injector::MakeNOP(injector::aslr_ptr(site + 5).get(), 7);
    }

    #undef g_u16
    #undef g_u8_offsets
    #undef g_u8
    #undef g_width_feeder
    #undef g_draw_feeder
    #undef g_size_feeder
    #undef g_inline_feeder
    #undef g_last_draw
    #undef g_owe_advance
    #undef g_owe_cp
    #undef g_site
    #undef g_painted
    #undef g_last_px
    #undef g_last_py
    #undef g_last_pw
    #undef g_last_pbuffered
}

extern "C" float gta4chs_104_width_dispatch(unsigned code, float raw_width, float extra_width)
{
    return gta4chs::legacy104::narrow::GetDrawingWidth104(code, raw_width, extra_width);
}
