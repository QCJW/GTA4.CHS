#include "font.h"
#include "gta_string.h"
#include "plugin.h"

static const float fChsWidth = 32.0f;
static const float fChsWidthFix = -1.0f;
static const float fSpriteWidth = 64.0f;
static const float fSpriteHeight = 66.0f;
static const float fTextureResolution = 4096.0f;

void* CNFont;

namespace
{
    // —— 默认后端：1.0.7 / 1.0.8 / CE 的 16 位宽字符管线 ——
    int ParseTokenWide(const GTAChar *str, GTAChar *token_string, TokenStruct *token_data)
    {
        return plugin.game.Font_ParseToken(str, token_string, token_data);
    }

    void AddTokenStringWidthWide(const GTAChar *text, float *width, int render_index)
    {
        plugin.game.Font_AddTokenStringWidth(text, width, render_index);
    }

    bool PrepareCjkDrawWide(int n_font)
    {
        return n_font == 0 || n_font == 1 || n_font == 3;
    }

    FontBackend MakeWideBackend()
    {
        FontBackend be;
        be.parse_token = &ParseTokenWide;
        be.add_token_string_width = &AddTokenStringWidthWide;
        be.prepare_cjk_draw = &PrepareCjkDrawWide;
        return be;
    }

    FontBackend g_backend = MakeWideBackend();

    // 两版只有 CFontInfo / CFontDetails 步长不同（0x250/0x44 vs 0x258/0x48），字段偏移一致，差异全在这张表里。
    CFontInfo& font_info_at(int index)
    {
        auto base = reinterpret_cast<char*>(plugin.game.game_addr.pFont_Datas);
        return *reinterpret_cast<CFontInfo*>(base + static_cast<std::size_t>(index) * g_backend.font_info_stride);
    }

    CFontDetails& font_details_at(int index)
    {
        auto base = reinterpret_cast<char*>(plugin.game.game_addr.pFont_Details);
        return *reinterpret_cast<CFontDetails*>(base + static_cast<std::size_t>(index) * g_backend.font_details_stride);
    }

    // 取引擎当前字体槽位的纹理；pTexture 偏移随版本变（1.0.4 少两个 int → +0x238，高版本 +0x240）。
    void *font_texture_at(int index)
    {
        if (index < 0 || index >= 4)
        {
            return nullptr;
        }

        auto base = reinterpret_cast<char *>(plugin.game.game_addr.pFont_Datas);

        auto tex = *reinterpret_cast<void **>(base + static_cast<std::size_t>(index) * g_backend.font_info_stride +
                                              g_backend.texture_offset);

        // 字体未加载完时这里是残留值（实测 0x44000000），绑上去会解引用野指针；只收像堆指针的值。
        const auto v = reinterpret_cast<std::uintptr_t>(tex);

        if (v < 0x10000u || v > 0x7FFFFFFFu || (v & 3u) != 0u)
        {
            return nullptr;
        }

        return tex;
    }
}

// 取引擎槽位纹理：步长 / pTexture 偏移由后端表决定（见 font.h）。
void* FontTextureAt(int index)
{
    return font_texture_at(index);
}

void InstallFontBackend(const FontBackend& backend)
{
    g_backend = backend;
}

void* __fastcall CFont::LoadTextureCB(void* pDictionary, int, uint hash)
{
    auto result = plugin.game.Dictionary_grcTexturePC_GetElementByKey(pDictionary, hash);

    CNFont = plugin.game.Dictionary_grcTexturePC_GetElementByKey(pDictionary,
        plugin.game.Hash_HashStringFromSeediCase("font_chs"));

    return result;
}

const GTAChar* CFont::SkipWord_Prolog(std::uintptr_t address)
{
    auto ptr = reinterpret_cast<const GTAChar*>(address & 0x7FFFFFFF);

    if ((address & 0x80000000) == 0)
    {
        ptr = SkipWord(ptr);
    }

    SkipSpecialPunctuationMarks(ptr);

    return ptr;
}

bool CFont::IsSpecialPunctuationMark(GTAChar chr)
{
#if 0
    return false;
#else
    // —、。《》「」『』！，－：；？～
    return
        // chr == L'—' ||
        chr == L'、' || chr == L'。' ||
        // chr == L'《' ||
        chr == L'》' ||
        // chr == L'「' ||
        chr == L'」' ||
        // chr == L'『' ||
        chr == L'』' || chr == L'！' || chr == L'，' ||
        // chr == L'－' ||
        chr == L'：' || chr == L'；' || chr == L'？' || chr == L'～' || chr == L'…';
#endif
}

void CFont::SkipSpecialPunctuationMarks(const GTAChar*& str)
{
    while (IsSpecialPunctuationMark(*str))
    {
        ++str;
    }
}

void CFont::AddSpecialPunctuationMarksWidth(const GTAChar*& str, float* width)
{
    while (IsSpecialPunctuationMark(*str))
    {
        *width += GetCharacterSizeNormalDispatch(*str - 0x20);
        ++str;
    }
}

const GTAChar* CFont::SkipWord(const GTAChar* str)
{
    if (str == nullptr)
    {
        return str;
    }

    auto begin = str;
    auto current = str;

    while (true)
    {
        GTAChar chr = *current;

        if (chr == ' ' || chr == '~' || chr == 0)
        {
            break;
        }

        if (!IsNativeChar(chr))
        {
            if (current == begin)
            {
                ++current;
            }

            break;
        }
        else
        {
            ++current;
        }
    }

    SkipSpecialPunctuationMarks(current);

    return current;
}

const GTAChar* CFont::SkipSpaces(const GTAChar* text)
{
    if (text == nullptr)
        return text;

    while (*text == ' ')
    {
        ++text;
    }

    return text;
}

float CFont::GetMaxWordWidth(const GTAChar* text)
{
    if (text == nullptr)
        return 0.0f;

    // 同 GetStringWidthRemake：分词扫描也要在译文上做，否则词边界与绘制不一致。
    text = plugin.string_table.GetString(text);

    float max_word_width = 0.0f;

    while (*text != 0)
    {
        float word_width = GetStringWidthRemake(text, false);

        if (word_width > max_word_width)
            max_word_width = word_width;

        text = SkipSpaces(SkipWord(text));
    }

    return max_word_width;
}

float CFont::GetStringWidthRemake(const GTAChar* str, bool get_all)
{
    float current_width = 0.0f, max_width = 0.0f;
    bool had_word = false;
    auto render_index = plugin.game.Font_GetRenderIndex();
    auto& using_details = font_details_at(render_index);

    TokenStruct token_data;
    GTAChar token_string[64];

    if (str == nullptr)
    {
        return 0.0f;
    }

    // 量宽必须看译文（绘制侧已换过），否则译文一长就压到下一项；未收录时 GetString 原样返回，行为不变。
    str = plugin.string_table.GetString(str);

    while (true)
    {
        auto chr = *str;

        if (chr == 0)
        {
            break;
        }
        else if (chr == ' ')
        {
            if (!get_all)
                break;
        }
        // 原版俄语/日语 gxt 里 token 名串每个字符都带 0x807E（'~' → 0x807E）。
        else if (chr == '~' || chr == 0x807E)
        {
            // token
            // 原版 0x7F7DF5..0x7F7E07：!get_all 且已经产生过宽度就收尾
            if (had_word && !get_all)
                break;

            // 91BEC3
            token_string[0] = 0;

            int token_type = g_backend.parse_token(++str, token_string, &token_data);

            if (token_data.f110 == 0)
            {
                // 91C0B4
                // 两个区间的判定口径随版本变，见 FontBackend::button_token_max / restrict_token_string_width。
                if (token_type >= 256 && token_type <= g_backend.button_token_max)
                {
                    // 91C0C3
                    current_width +=
                        plugin.game.game_addr.pFont_ButtonWidths[token_type - 255] * using_details.fBlipScaleX;
                    had_word = true;
                }
                else if (!g_backend.restrict_token_string_width || (token_type >= 1 && token_type <= 63))
                {
                    // 91C116
                    if (gta_string::gtaWcslen(token_string) > 0)
                    {
                        // 91C122
                        g_backend.add_token_string_width(token_string, &current_width, render_index);
                        had_word = true;
                    }
                }
            }
            else
            {
                // 91BF11
                for (int token_index = 0; token_index < 4; ++token_index)
                {
                    switch (token_data.a110[token_index])
                    {
                    case 1: {
                        // 91BF26
                        current_width += plugin.game.game_addr.pFont_ButtonWidths[token_data.f0[token_index] - 255] *
                            using_details.fBlipScaleX;
                        had_word = true;

                        break;
                    }

                    case 2: {
                        // 91BF53
                        g_backend.add_token_string_width(token_data.f10[token_index], &current_width, render_index);
                        had_word = true;
                        break;
                    }

                    default: {
                        break;
                    }
                    }
                }
            }

            // 91BF9E
            if (token_type >= 1000)
            {
                // 91BFA6
                current_width += *plugin.game.game_addr.pFont_BlipWidth * using_details.fBlipScaleX;
                had_word = true;
            }

            // token 尾扫描：与原版一致，并列比较 '~'(0x7E) 和带标记的 0x807E（0x91C47B / 0x91C480）
            while (*str != '~' && *str != 0x807E)
            {
                // 91BFE0
                if (*str == 'n')
                {
                    max_width = std::max(max_width, current_width);
                    current_width = 0.0f;
                }

                ++str;
            }

            // 91C013
            // 跳过后边的'~'
            ++str;

            AddSpecialPunctuationMarksWidth(str, &current_width);

            // token 出口一律收尾（与 1.0.7 同口径）：1.0.4 原版不收尾，与「token 出口标记」互斥 ——
            // 同一个词会被量两次、行宽翻倍，表现为 token 后立刻换行。
            if (!get_all)
            {
                break;
            }

            continue;
        }
        else if (!IsNativeChar(chr))
        {
            // 汉字
            // 有可能是英语单词+汉字，要断开
            if (had_word && !get_all)
            {
                break;
            }
        }

        // 累加字符宽度
        current_width += GetCharacterSizeNormalDispatch(chr - 0x20);
        had_word = true;
        ++str;

        auto old_ptr = str;
        AddSpecialPunctuationMarksWidth(str, &current_width);

        // 解决标英交替时的宽度计算错误
        if (str != old_ptr && !get_all)
        {
            break;
        }

        // 计算汉字宽度之后立即判断一次分词
        if (!IsNativeChar(chr) && !get_all)
        {
            break;
        }
    }

    return std::max(current_width, max_width);
}

void CFont::ProcessStringRoutine(float x, float y, const GTAChar* str, void* a4)
{
    plugin.game.Font_ProcessString(x, y, plugin.string_table.GetString(str), a4);
}

// 量宽读 CFontDetails、绘制读 CFontRenderState，别再试图统一：
//   ① details 刷进 RS → 解码失步；② 量宽读 RS → 测量遍不灌 RS，读到残值 → 逐帧闪；
//   ③ 绘制读 details → 拿到上一串锁存 → 字距错乱。换行误差只能从「分词 / 量了多少」这一侧修。
float CFont::GetCHSCharacterSizeNormal()
{
    // 槽位还没起来时这些指针可能是空的，除 0 会让宽度变成 NaN 一路传下去
    const float *res_x_ptr = plugin.game.game_addr.pFont_ResolutionX;
    const float res_x = (res_x_ptr != nullptr && *res_x_ptr != 0.0f) ? *res_x_ptr : 1.0f;

    auto& using_details = font_details_at(plugin.game.Font_GetRenderIndex());
    auto& using_font_data = font_info_at(using_details.nFont);

    float extra_width = using_font_data.fWidthOfSpaceBetweenChars[using_details.nExtraWidthIndex];

    return (((fChsWidth + fChsWidthFix + extra_width) / res_x +
        using_details.fEdgeSize2) *
        using_details.fScaleX);
}

float CFont::GetCharacterSizeNormalDispatch(GTAChar chr)
{
    if (IsNativeChar(chr + 0x20))
    {
        return plugin.game.Font_GetCharacterSizeNormal(chr);
    }
    else
    {
        return GetCHSCharacterSizeNormal();
    }
}

float CFont::GetCHSCharacterSizeDrawing(bool use_extra_width)
{
    float extra_width = 0.0f;

    // 必须在绘制当下读 RS（RS-flush 时由样式块灌好）；换成 CFontDetails 会拿到上一串残值。
    auto render_state = plugin.game.game_addr.pFont_RenderState;
    auto& using_font_data = font_info_at(render_state->nFont);

    if (use_extra_width)
    {
        extra_width = using_font_data.fWidthOfSpaceBetweenChars[render_state->nExtraWidthIndex];
    }

    const float *res_x_ptr = plugin.game.game_addr.pFont_ResolutionX;
    const float res_x = (res_x_ptr != nullptr && *res_x_ptr != 0.0f) ? *res_x_ptr : 1.0f;

    return ((fChsWidth + fChsWidthFix + extra_width) / res_x +
        render_state->fEdgeSize) *
        render_state->fScaleX;
}

float CFont::GetCharacterSizeDrawingDispatch(GTAChar chr, bool use_extra_width)
{
    if (IsNativeChar(chr + 0x20))
    {
        return plugin.game.Font_GetCharacterSizeDrawing(chr, use_extra_width);
    }
    else
    {
        return GetCHSCharacterSizeDrawing(use_extra_width);
    }
}

void CFont::PrintCHSChar(float x, float y, GTAChar chr)
{
    auto [row, column] = plugin.char_table.GetCharPos(chr);
    auto render_state = plugin.game.game_addr.pFont_RenderState;

    // 原版游戏实际截取的图块在64*80格子中的坐标是
    //  x 0.0
    //  y 4.4
    //  w 64.0
    //  h 78.4912

    // 通过尝试得出的relative_char_rect在64*78.4912矩形中的偏移
    float x_delta = 0.0f;
    float y_delta = 5.0f;

    // 先计算字符图片在64*78.4912矩形内的位置
    CRect relative_char_rect;
    relative_char_rect.bottom_left.x = x_delta;
    relative_char_rect.top_right.x = relative_char_rect.bottom_left.x + fSpriteWidth;
    relative_char_rect.top_right.y = y_delta;
    relative_char_rect.bottom_left.y = relative_char_rect.top_right.y + fSpriteHeight;

    // 截取纹理的位置
    CRect texture_rect;
    texture_rect.bottom_left.x = static_cast<float>(column) * fSpriteWidth / fTextureResolution;
    texture_rect.bottom_left.y = static_cast<float>(row + 1) * fSpriteHeight / fTextureResolution;
    texture_rect.top_right.x = static_cast<float>(column + 1) * fSpriteWidth / fTextureResolution;
    texture_rect.top_right.y = static_cast<float>(row) * fSpriteHeight / fTextureResolution;

    const float *res_x_ptr = plugin.game.game_addr.pFont_ResolutionX;
    const float res_x = (res_x_ptr != nullptr && *res_x_ptr != 0.0f) ? *res_x_ptr : 1.0f;

    float old_screen_character_width =
        (fChsWidth / res_x + render_state->fEdgeSize) * render_state->fScaleX;

    float old_screen_character_height = render_state->fScaleY * 0.06558f;

    // 64*78.4912图块在屏幕上的位置
    CRect old_screen_rect;
    old_screen_rect.bottom_left.x = x;
    old_screen_rect.bottom_left.y = y + old_screen_character_height;
    old_screen_rect.top_right.x = x + old_screen_character_width;
    old_screen_rect.top_right.y = y;

    auto flt_proj = [](float old_lb, float old_ub, float old_val, float new_lb, float new_ub) {
        auto old_range = old_ub - old_lb;
        auto old_diff = old_val - old_lb;
        auto new_range = new_ub - new_lb;

        return old_diff / old_range * new_range + new_lb;
        };

    // 将virtual_char_rect投影到old_screen_rect中，得到real_screen_rect
    CRect real_screen_rect;

    // 计算y的第二个参数越小，字的y长度越大
    real_screen_rect.top_right.x = flt_proj(0.0f, 64.0f, relative_char_rect.top_right.x, old_screen_rect.bottom_left.x,
        old_screen_rect.top_right.x);

    real_screen_rect.top_right.y = flt_proj(0.0f, 74.4912f, relative_char_rect.top_right.y, old_screen_rect.top_right.y,
        old_screen_rect.bottom_left.y);

    real_screen_rect.bottom_left.x = flt_proj(0.0f, 64.0f, relative_char_rect.bottom_left.x,
        old_screen_rect.bottom_left.x, old_screen_rect.top_right.x);

    real_screen_rect.bottom_left.y = flt_proj(0.0f, 74.4912f, relative_char_rect.bottom_left.y,
        old_screen_rect.top_right.y, old_screen_rect.bottom_left.y);

    // 是否绑 CJK 图集由后端决定。上游写死 switch(nFont){0,1,3}，1.0.4 菜单用 nFont=2 → 一次都不绑
    // → 整片方块，故 104 改成全部槽位都绑。两条坑勿再试：
    //   ① 改绑引擎槽位 2 的纹理 → 那不是 CJK 图集，开始页 / HUD 全乱码；
    //   ② 已绑就跳过重绑 → 引擎会换回自己的纹理 → 汉字按引擎图集采样。
    const bool bound_cjk_atlas = g_backend.prepare_cjk_draw(render_state->nFont);

    if (bound_cjk_atlas)
    {
        // 纹理只认字典里的 CNFont（font_chs）
        plugin.game.Graphics_SetRenderState(CNFont);
    }

    plugin.game.Font_Render2DPrimitive(&real_screen_rect, &texture_rect, render_state->field_18, false);

    // 收尾交给后端：1.0.4 必须把纹理还给引擎，否则 2D 立即图元会拿 CJK 图集采样（菜单闪）；高版本不收尾。
    if (bound_cjk_atlas && g_backend.finish_cjk_draw != nullptr)
    {
        g_backend.finish_cjk_draw(render_state->nFont);
    }
}

void CFont::PrintCharDispatch(float x, float y, GTAChar chr, bool buffered)
{
    if (plugin.game.game_addr.pFont_RenderState->TokenType != 0 || IsNativeChar(chr + 0x20))
    {
        plugin.game.Font_PrintChar(x, y, chr, buffered);
    }
    else
    {
        if ((chr + 0x20) == 0x3000)
        {
            return;
        }

        if (y < -0.06558f || y > 1.0f)
        {
            return;
        }

        if (-(GetCHSCharacterSizeDrawing(true) / plugin.game.game_addr.pFont_RenderState->fScaleX) > x || x > 1.0f)
        {
            return;
        }

        PrintCHSChar(x, y, chr + 0x20);
    }
}
