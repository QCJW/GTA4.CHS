#pragma once
#include "../common/common.h"
#include "../common/stdinc.h"

enum eTextAlignment
{
    ALIGN_CENTER = 0,    // 居中对齐
    ALIGN_LEFT = 1,      // 左对齐
    ALIGN_RIGHT = 2,     // 右对齐
    ALIGN_LEFT_RIGHT = 3 // 多余的宽度会平分到每个空格中
};

class CFontDetails
{
public:
    uint field_0;
    float fScaleX;
    float fScaleY;
    float fBlipScaleX;
    int alignment;
    bool bDrawBox;
    bool field_15;
    bool bProportional;
    bool field_17;
    bool bUseUnderscore;
    bool bUseColor;
    bool field_1A;
    char pad1[1];
    uint BoxColor;
    float fWrapX;
    float fCentreWrapX;
    uchar nFont;
    uchar nExtraWidthIndex;
    char pad2[2];
    float fDropShadowSize;
    uint DropShadowColor;
    float fEdgeSize2;
    float fEdgeSize;
    float fLineHeight;
    float fExtraSpaceWidth;
    bool bIgnoreWidthLimit;
    char pad3[3];
};

VALIDATE_SIZE(CFontDetails, 0x48);

class CFontInfo
{
public:
    uchar iPropValues[255];
    uchar iTextureMap[255];
    char pad1[2];
    float fUnpropValue;
    float fWidthOfSpaceBetweenChars[5];
    float fJapaneseSubFont1SpaceWidth;
    float fJapaneseSubFont2SpaceWidth;
    int iMainFontStart;
    int iMainFontEnd;
    int iSubFont1Start;
    int iSubFont1End;
    int iSubFont2Start;
    int iSubFont2End;
    int iCommonFontStart;
    int iCommonFontEnd;
    void* pTexture;
    float fTextureWidth;
    float fTextureHeight;
    float fSpriteSize;
    float field_250;
    float field_254;
};
VALIDATE_SIZE(CFontInfo, 0x258);

class CFontRenderState
{
public:
    uint magic;
    float field_4;
    float field_8;
    float fScaleX;
    float fScaleY;
    float field_14;
    uint field_18;
    float field_1C;
    int field_20;
    bool field_24;
    uchar nExtraWidthIndex;
    bool bProportional;
    bool field_27;
    bool bUseUnderscore;
    uchar nFont;
    char pad1[2];
    float fEdgeSize;
    uint TokenType;
    bool field_34;
    char pad2[3];
};
VALIDATE_SIZE(CFontRenderState, 0x38);

class CFontBuffer
{
public:
    CFontRenderState render_state;
    GTAChar buffer[996];
};
VALIDATE_SIZE(CFontBuffer, 2048);

struct TokenStruct
{
    int f0[4];
    GTAChar f10[4][32];

    union {
        int f110 = 0;
        uchar a110[4];
    };
};
VALIDATE_SIZE(TokenStruct, 0x114);

// —— 字体后端表 ——
// 版本差异（步长 / token / 编码）全收进这张表，安装时登记一次；1.0.4 由 legacy104 登记，高版本用默认值。
struct FontBackend
{
    // 步长与 CFontInfo::pTexture 偏移。1.0.4 的 CFontInfo 少两个 int → 0x250 / 0x44 / +0x238；
    // 高版本 sizeof(CFontInfo) / 0x48 / +0x240。
    std::size_t font_info_stride = sizeof(CFontInfo);
    std::size_t font_details_stride = sizeof(CFontDetails);
    std::size_t texture_offset = 0x240; // offsetof(CFontInfo, pTexture)

    // token 解析与 token 串宽度。1.0.4 引擎侧是窄串，喂宽串会读到 '\0' 且字段偏移错位，需在边界转码。
    int (*parse_token)(const GTAChar *str, GTAChar *token_string, TokenStruct *token_data) = nullptr;
    void (*add_token_string_width)(const GTAChar *text, float *width, int render_index) = nullptr;

    // 画一个汉字前的准备；返回值 = 是否绑了 CJK 图集（绑了就得在画完时还回去）。
    // 1.0.4 还要排空引擎攒批的原生字形（否则逐帧闪）且所有槽位都绑；高版本只认 0/1/3。
    bool (*prepare_cjk_draw)(int n_font) = nullptr;

    // 画完一个汉字后的收尾（nullptr = 不收尾）。只有 1.0.4 需要：把纹理还给引擎，
    // 否则紧随的 2D 立即图元会拿 CJK 图集采样（菜单闪）。
    void (*finish_cjk_draw)(int n_font) = nullptr;

    // 「单个 token」宽度分支的判定口径两版不同（反汇编确证）：
    //   1.0.4 按钮表只认 [0x100,0x12B]（上界 299），且只有 [1,63] 才调 AddTokenStringWidth；
    //   高版本沿用上游：不在按钮区间内一律按 token 串量宽，上界 300。不照抄则 `~s~` 会被多算一次 → 宽度偏大。
    int button_token_max = 300;
    bool restrict_token_string_width = false;
};

// 登记当前版本的字体后端。必须在任何字体钩子跑起来之前调用一次。
void InstallFontBackend(const FontBackend& backend);

// 取引擎第 index 个字体槽位的纹理；越界或读到的值不像堆指针时返回 nullptr。
void* FontTextureAt(int index);

class CFont
{
public:
    static void* __fastcall LoadTextureCB(void*, int, uint);

    static float GetCharacterSizeNormalDispatch(GTAChar chr);
    static float GetCHSCharacterSizeNormal();

    static float GetCharacterSizeDrawingDispatch(GTAChar chr, bool use_extra_width);
    static float GetCHSCharacterSizeDrawing(bool use_extra_width);

    static void PrintCharDispatch(float x, float y, GTAChar chr, bool buffered);
    static void PrintCHSChar(float x, float y, GTAChar chr);


    // 判断字符是否为不能放在行首的标点符号
    static bool IsSpecialPunctuationMark(GTAChar chr);
    // 将特殊字符看作单词的一部分
    static void SkipSpecialPunctuationMarks(const GTAChar*& str);
    static void AddSpecialPunctuationMarksWidth(const GTAChar*& str, float* width);

    static const GTAChar* SkipWord(const GTAChar* str);
    static const GTAChar* SkipWord_Prolog(std::uintptr_t address);
    static const GTAChar* SkipSpaces(const GTAChar* text);

    static float GetMaxWordWidth(const GTAChar* text);

    static float GetStringWidthRemake(const GTAChar* str, bool get_all);

    static void ProcessStringRoutine(float x, float y, const GTAChar* str, void* a4);
};
