#pragma once
#include "../../common/common.h"
#include "../font.h"

namespace gta4chs::legacy104::narrow
{
    // 引擎把 `字节 - 0x20` 当字形码传进来；这里还原成原始字节
    inline unsigned char decode_glyph(std::uint16_t glyph)
    {
        return static_cast<unsigned char>((glyph + 0x20u) & 0xFFu);
    }

    // 原版字形表内的字符要按引擎口径传回去
    inline GTAChar to_glyph(GTAChar codepoint)
    {
        return static_cast<GTAChar>(codepoint - 0x20u);
    }

    // 原版字形表只到 0xFF（引擎 iPropValues / iTextureMap 各 255 项）
    inline bool is_native_codepoint(GTAChar codepoint)
    {
        return IsNativeChar(codepoint);
    }

    // 1.0.4 的 token 结构是窄字符版：f0[4] / f10[4][32] / f110，总长 0x94
    struct TokenStruct104
    {
        int f0[4];
        char f10[4][32];

        union
        {
            int f110 = 0;
            uchar a110[4];
        };
    };

    VALIDATE_SIZE(TokenStruct104, 0x94);

    // PrintChar 有两个调用点（RS / fnX），引擎侧无法区分，只能在钩子侧标记
    enum class draw_site
    {
        rs,
        fnx
    };

    void set_draw_site(draw_site site);

    // 逐字节 UTF-8 解码器：靠它跨调用保存中间状态
    class feeder
    {
    public:
        enum class result
        {
            pending,  // 多字节序列未收满：本次不产出字符
            complete, // 序列收满：codepoint() 为完整字符
        };

        result feed(std::uint16_t glyph);

        char32_t codepoint() const { return _out_cp; }

        // 补吐轮一次出两个字符（lead + 同轮判定的当前字节），调用点必须一起算
        bool has_second() const { return _has_second; }
        char32_t second_codepoint() const { return _second_cp; }

        // 串结束还压着没吐出来的字节，必须补画
        bool tail_pending() const { return _need != 0 && _have == 0; }
        unsigned char pending_lead() const { return _lead; }

        // 序列收到一半：此时绝不能复位，后面续字节会全判非法
        bool busy() const { return _need != 0; }

        void reset()
        {
            _cp = 0;
            _out_cp = 0;
            _need = 0;
            _have = 0;
            _lead = 0;
            _has_second = false;
            _second_cp = 0;
        }

    private:
        result feed_inner(unsigned char);

        // _cp 是序列累积器，不是本轮输出（_out_cp 才是）
        char32_t _cp = 0;
        char32_t _out_cp = 0;
        char32_t _second_cp = 0;
        bool _has_second = false;
        int _need = 0;
        int _have = 0;

        // 上一个被当成首字节的字节值，没等来续字节时靠它补吐
        unsigned char _lead = 0;
    };

    // 取一个 UTF-8 序列的字节长度并输出码点（非法 / 串尾按单字节透传）
    std::size_t peek_utf8(const char *ptr, char32_t &cp);

    // UTF-8 → UTF-16，返回写入的 GTAChar 数（不含结尾 0）
    std::size_t decode_utf8(const char *src, GTAChar *dst, std::size_t dst_cap);

    // UTF-16 → UTF-8，返回写入的字节数（不含结尾 0）
    std::size_t encode_utf8(const GTAChar *src, char *dst, std::size_t dst_cap);

    // 转码暂存（字体钩子都在渲染线程触发，按线程各一份即可）
    GTAChar *utf16_scratch();
    char *utf8_scratch();

    // 串级入口：清掉可能残留的半截序列
    void reset_feeders();

    // 串级出口：补画串尾那个字节，并清掉本串状态
    void flush_string_tail(float x, float y);

    // 画汉字前排空引擎攒批的原生字形（汉字立即、原生字延迟，不排空谁盖谁逐帧不同）
    void FlushBufferedPrimitives();

    // 带框文字一帧两遍：先算框、再画字；汉字不走引擎多态，算框那遍要挡住绘制
    void BeginMeasurePass();
    void EndMeasurePass();

    float GetCharacterSizeNormalDispatch104(std::uint16_t glyph);
    float GetCharacterSizeDrawingDispatch104(std::uint16_t glyph, bool use_extra_width);
    void PrintCharDispatch104(float x, float y, std::uint16_t glyph, bool buffered);

    const char *SkipWord104(const char *str);
    float GetStringWidthRemake104(const char *str, bool get_all);
    float GetMaxWordWidth104(const char *text);

    // 引擎侧 ParseToken / AddTokenStringWidth 是窄串实现，这两个适配器在边界转码
    int Font_ParseToken104(const GTAChar *str, GTAChar *token_string, TokenStruct *token_data);
    void Font_AddTokenStringWidth104(const GTAChar *text, float *width, int render_index);

    // fnX 把 GetCharacterSizeDrawing 内联了，在 addss/divss 处插跳板
    void install_inline_width_hook(std::uintptr_t site, std::uintptr_t back);

    // 跳板 C++ 端：返回「已除分辨率、未加 fEdgeSize / 未乘 fScaleX」的宽度
    float GetDrawingWidth104(unsigned glyph, float raw_width, float extra_width);
}
