#include "legacy104_narrow.h"
#include "../font.h"
#include "../plugin.h"
#include <intrin.h>

// 裸跳板要用 C 名字引用（内联汇编解析不了 C++ 修饰名）
extern "C" float gta4chs_104_width_dispatch(unsigned glyph, float raw_width, float extra_width);
extern "C" std::uintptr_t gta4chs_104_width_back = 0;

// fnX 内联宽度跳板。入口：bl=字形码，xmm0=字形宽，xmm1=extra_width
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
            std::uint16_t glyph = 0;
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

            feeder width_feeder; // 量宽路径（GetCharacterSizeNormal）
            feeder draw_feeder;  // 绘制路径（PrintChar）
            feeder size_feeder;  // RS 循环里的 GetCharacterSizeDrawing

            draw_snapshot last_draw{};
            draw_site site = draw_site::rs;

            // 已画出但没结算进 x 的字宽：每段首字 RS 少调一次 GSD，不补就叠字
            bool owe_advance = false;

            // 最近一次真画出去的位置与宽度，串尾补画靠它算落点
            bool painted = false;
            float last_x = 0.0f;
            float last_y = 0.0f;
            float last_width = 0.0f;
            bool last_buffered = false;

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

        // 解一个 UTF-8 序列，返回字节数；残缺或非法一律退化成单字节
        std::size_t scan_utf8(const char *p, char32_t &cp)
        {
            const auto b0 = static_cast<unsigned char>(p[0]);

            if (b0 < 0x80)
            {
                cp = b0;
                return 1;
            }

            char32_t value = 0;
            std::size_t len = 1;

            // 首字节判定必须与 feeder 同口径：0xF8-0xFF 非法，否则警星一个都不画
            if ((b0 & 0xE0) == 0xC0)
            {
                value = b0 & 0x1Fu;
                len = 2;
            }
            else if ((b0 & 0xF0) == 0xE0)
            {
                value = b0 & 0x0Fu;
                len = 3;
            }
            else if ((b0 & 0xF8) == 0xF0)
            {
                value = b0 & 0x07u;
                len = 4;
            }
            else
            {
                // 0x80-0xBF 与 0xF8-0xFF 是合法单字节图标，按单字节透传
                cp = b0;
                return 1;
            }

            // 严格 UTF-8：续字节每个都要在 0x80-0xBF，否则按自同步丢残尾
            for (std::size_t k = 1; k < len; ++k)
            {
                const auto bk = static_cast<unsigned char>(p[k]);

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
                char32_t cp = 0;
                const auto len = scan_utf8(src + i, cp);

                dst[out] = static_cast<GTAChar>(cp > 0xFFFF ? 0xFFFD : cp);

                if (record_offsets && out < kScratchChars)
                {
                    state().u8_offsets[out] = i;
                }

                ++out;
                i += len;
            }

            if (record_offsets)
            {
                state().u8_offsets[out] = i;
            }

            dst[out] = 0;
            return out;
        }

        // 单字符量宽：原版字形表内的走引擎原路径（含 2 字节 UTF-8 的 U+0080-U+00FF）
        float char_size_normal(GTAChar cp)
        {
            return is_native_codepoint(cp)
                ? plugin.game.Font_GetCharacterSizeNormal(to_glyph(cp))
                : CFont::GetCHSCharacterSizeNormal();
        }

        float char_size_drawing(GTAChar cp, bool use_extra_width)
        {
            return is_native_codepoint(cp)
                ? plugin.game.Font_GetCharacterSizeDrawing(to_glyph(cp), use_extra_width)
                : CFont::GetCHSCharacterSizeDrawing(use_extra_width);
        }
}

    void set_draw_site(draw_site site)
    {
        state().site = site;
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
        ++state().measure_depth;
    }

    void EndMeasurePass()
    {
        // 测量遍只挡绘制，feed / 挂账照旧（也要靠 GSN / GSW 拿宽度）
        if (state().measure_depth > 0)
        {
            --state().measure_depth;
        }
    }

    void reset_feeders()
    {
        auto &st = state();

        // 异线程调用一律不生效：清的是渲染线程正在用的活状态
        if (g_render_tid != 0 && ::GetCurrentThreadId() != g_render_tid)
        {
            return;
        }

        // 正在收续字节的 feeder 不能动且必须全有或全无：部分复位会让 feeder 相位错开
        if (st.draw_feeder.busy() || st.size_feeder.busy() || st.width_feeder.busy())
        {
            return;
        }

        st.draw_feeder.reset();
        st.size_feeder.reset();
        st.width_feeder.reset();

        // 有未结清的宽度挂账时不能作废快照：上一个字还没被 GSD 结算
        if (!st.owe_advance)
        {
            st.last_draw = draw_snapshot{};
            st.owe_advance = false;
        }
    }

    void flush_string_tail(float x, float y)
    {
        auto &st = state();

        // 串尾没等到续字节的字节再也吐不出来 → 少最后一个字；落点从上一个字右边缘推算
        if (st.draw_feeder.tail_pending() && st.measure_depth == 0)
        {
            const auto byte = st.draw_feeder.pending_lead();
            const float px = st.painted ? st.last_x + st.last_width : x;
            const float py = st.painted ? st.last_y : y;

            plugin.game.Font_PrintChar(px, py, to_glyph(byte), st.last_buffered);
        }

        // 串确实结束了，残留的半截状态必须清掉
        st.draw_feeder.reset();
        st.size_feeder.reset();
        st.width_feeder.reset();

        st.last_draw = draw_snapshot{};
        st.owe_advance = false;
        st.painted = false;
    }

    GTAChar *utf16_scratch()
    {
        return state().u16;
    }

    char *utf8_scratch()
    {
        return state().u8;
    }

    feeder::result feeder::feed(std::uint16_t glyph)
    {
        // 补吐是「一次 feed 出两字符」的例外，只在补吐轮置位，必须每轮先清
        _has_second = false;
        return feed_inner(decode_glyph(glyph));
    }

    feeder::result feeder::feed_inner(unsigned char b)
    {
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
                const auto r2 = feed_inner(b);

                if (r2 == result::complete)
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
            return result::complete;
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
        return scan_utf8(ptr, cp);
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

    float GetCharacterSizeNormalDispatch104(std::uint16_t glyph)
    {
        auto &st = state();

        if (st.width_feeder.feed(glyph) != feeder::result::complete)
        {
            // 首字节 / 中间字节不产出字符，返回 0 以免 x 被重复推进
            return 0.0f;
        }

        float w = char_size_normal(static_cast<GTAChar>(st.width_feeder.codepoint()));

        // 补吐轮一次出两字符：引擎一个字节只推进一次 x，两个宽度必须一起给
        if (st.width_feeder.has_second())
        {
            w += char_size_normal(static_cast<GTAChar>(st.width_feeder.second_codepoint()));
        }

        return w;
    }

    float GetCharacterSizeDrawingDispatch104(std::uint16_t glyph, bool use_extra_width)
    {
        auto &st = state();

        // 快照命中 = 这一字节刚被 PrintChar 处理过，直接复用它的解码结果
        const bool matched = st.last_draw.result >= 0
            && ((st.last_draw.glyph & 0xFFu) == (glyph & 0xFFu));

        // 异线程量宽请求不属于本线程字节流，喂 feeder 或销账都会伤到渲染线程
        if (!matched && g_render_tid != 0 && ::GetCurrentThreadId() != g_render_tid)
        {
            return 0.0f;
        }

        const auto r = matched ? static_cast<feeder::result>(st.last_draw.result) : st.size_feeder.feed(glyph);
        const auto cp = matched ? st.last_draw.cp : static_cast<GTAChar>(st.size_feeder.codepoint());
        const bool has2 = matched ? st.last_draw.has_second != 0 : st.size_feeder.has_second();
        const auto cp2 = matched ? st.last_draw.cp2 : static_cast<GTAChar>(st.size_feeder.second_codepoint());

        const bool utf8_byte = decode_glyph(glyph) >= 0x80;
        float w = 0.0f;

        if (r == feeder::result::complete)
        {
            w = char_size_drawing(cp, use_extra_width);

            if (has2)
            {
                w += char_size_drawing(cp2, use_extra_width);
            }

            st.owe_advance = false;
        }
        // 上一个画出来的汉字没拿到宽度，借这一轮补账
        else if (st.owe_advance && utf8_byte)
        {
            w = CFont::GetCHSCharacterSizeDrawing(use_extra_width);
            st.owe_advance = false;
        }

        return w;
    }

    void PrintCharDispatch104(float x, float y, std::uint16_t glyph, bool buffered)
    {
        auto &st = state();

        // 只在这里认定管线归属：绘制必然在渲染线程，GSD 有被异线程调用的记录
        g_render_tid = ::GetCurrentThreadId();

        auto render_state = plugin.game.game_addr.pFont_RenderState;

        // TokenType != 0 时要画 blip 精灵：整机旁路（'~' 后的字节可能是汉字首字节）；TokenType 也绝不能清
        if (render_state != nullptr && render_state->TokenType != 0)
        {
            plugin.game.Font_PrintChar(x, y, static_cast<GTAChar>(glyph), buffered);
            return;
        }

        const auto r = st.draw_feeder.feed(glyph);

        st.last_draw.glyph = glyph;
        st.last_draw.result = static_cast<int>(r);
        st.last_draw.cp = static_cast<GTAChar>(st.draw_feeder.codepoint());
        st.last_draw.has_second = st.draw_feeder.has_second() ? 1 : 0;
        st.last_draw.cp2 = static_cast<GTAChar>(st.draw_feeder.second_codepoint());

        if (r != feeder::result::complete)
        {
            return;
        }

        const auto cp = static_cast<GTAChar>(st.draw_feeder.codepoint());

        // 原版字形表内的字符走引擎原路径（TokenType != 0 已在开头旁路）
        if (is_native_codepoint(cp))
        {
            const float w1 = char_size_drawing(cp, true);
            plugin.game.Font_PrintChar(x, y, to_glyph(cp), buffered);
            // 补吐轮：第二个字符画在第一个之后，x 由紧随的 GSD 一次结清

            if (st.draw_feeder.has_second())
            {
                const auto cp2 = static_cast<GTAChar>(st.draw_feeder.second_codepoint());
                plugin.game.Font_PrintChar(x + w1, y, to_glyph(cp2), buffered);
                st.last_width = w1 + char_size_drawing(cp2, true);
            }
            else
            {
                st.last_width = w1;
            }

            st.painted = true;
            st.last_x = x;
            st.last_y = y;
            st.last_buffered = buffered;
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
        if (st.site == draw_site::rs)
        {
            st.owe_advance = true;
        }

        if (st.measure_depth > 0)
        {
            return;
        }

        CFont::PrintCHSChar(x, y, cp);

        st.painted = true;
        st.last_x = x;
        st.last_y = y;
        st.last_width = CFont::GetCHSCharacterSizeDrawing(true);
        st.last_buffered = buffered;
    }

    const char *SkipWord104(const char *str)
    {
        if (str == nullptr)
        {
            return str;
        }

        reset_feeders();

        const auto n = decode_utf8(str, state().u16, kScratchChars);

        // 返回 str+0：换行循环靠 SkipWord 推进，越界返回 str+1 风险更大
        if (n == 0)
        {
            return str;
        }

        const auto advanced = CFont::SkipWord(state().u16) - state().u16;

        // 一步没进 = 原版「首字节就是空格 / '~'」，原版此时原样返回入参，必须照抄
        if (advanced <= 0)
        {
            return str + state().u8_offsets[0];
        }

        return str + state().u8_offsets[advanced > static_cast<long>(n) ? n : advanced];
    }

    float GetStringWidthRemake104(const char *str, bool get_all)
    {
        if (str == nullptr)
        {
            return 0.0f;
        }

        reset_feeders();

        if (decode_utf8(str, state().u16, kScratchChars) == 0)
        {
            return 0.0f;
        }

        return CFont::GetStringWidthRemake(state().u16, get_all);
    }

    // GetMaxWordWidth 专用缓冲，不能用 u16：内层 ParseToken104 会原地覆盖它
    GTAChar g_max_word_scratch[kScratchChars];

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
            plugin.game.game_addr.fnFont_AddTokenStringWidth, buf, width, render_index);
    }

    float GetDrawingWidth104(unsigned glyph, float raw_width, float extra_width)
    {
        auto &st = state();
        const auto rs = plugin.game.game_addr.pFont_RenderState;
        const float edge = rs->fEdgeSize;

        // 不匹配就返回 -edge 抵消引擎随后加的 fEdgeSize，净推进 0；只比较低 8 位
        if ((st.last_draw.glyph & 0xFFu) != (glyph & 0xFFu) || st.last_draw.result < 0)
        {
            return -edge;
        }

        const auto cp = st.last_draw.cp;
        float out;

        if (static_cast<feeder::result>(st.last_draw.result) != feeder::result::complete)
        {
            // pending 同理：不抵消每个中间字节都会把 x 推进 edge*scaleX
            out = -edge;
        }
        else if (is_native_codepoint(cp))
        {
            out = (raw_width + extra_width) / *plugin.game.game_addr.pFont_ResolutionX;
        }
        else
        {
            // 要的是「除完分辨率、还没加 fEdgeSize」的中间量
            out = CFont::GetCHSCharacterSizeDrawing(true) / rs->fScaleX - edge;
        }

        // 补吐轮第二字符：引擎没算过 raw_width，按同一中间量口径补
        if (st.last_draw.has_second != 0)
        {
            out += char_size_drawing(st.last_draw.cp2, extra_width != 0.0f) / rs->fScaleX - edge;
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
}

extern "C" float gta4chs_104_width_dispatch(unsigned glyph, float raw_width, float extra_width)
{
    return gta4chs::legacy104::narrow::GetDrawingWidth104(glyph, raw_width, extra_width);
}
