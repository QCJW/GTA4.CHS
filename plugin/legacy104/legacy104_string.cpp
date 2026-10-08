#include "legacy104_string.h"
#include <cstring>

namespace gta4chs::legacy104
{
namespace
{
    unsigned append_utf8(uchar* out, std::uint32_t cp)
    {
        if (cp < 0x80u)
        {
            out[0] = static_cast<uchar>(cp);
            return 1;
        }

        if (cp < 0x800u)
        {
            out[0] = static_cast<uchar>(0xC0u | (cp >> 6));
            out[1] = static_cast<uchar>(0x80u | (cp & 0x3Fu));
            return 2;
        }

        if (cp < 0x10000u)
        {
            out[0] = static_cast<uchar>(0xE0u | (cp >> 12));
            out[1] = static_cast<uchar>(0x80u | ((cp >> 6) & 0x3Fu));
            out[2] = static_cast<uchar>(0x80u | (cp & 0x3Fu));
            return 3;
        }

        out[0] = static_cast<uchar>(0xF0u | (cp >> 18));
        out[1] = static_cast<uchar>(0x80u | ((cp >> 12) & 0x3Fu));
        out[2] = static_cast<uchar>(0x80u | ((cp >> 6) & 0x3Fu));
        out[3] = static_cast<uchar>(0x80u | (cp & 0x3Fu));
        return 4;
    }

    // 名字字段 0x100 字节 = 128 个 GTAChar，留一个给 '\0'
    constexpr unsigned kSlotNameCap = 127;
}

// 按字节预算逐序列拷贝
uchar* StrncpyByteLimited(uchar* dest, const uchar* source, unsigned size)
{
    if (dest == nullptr)
        return nullptr;

    if (source == nullptr || size == 0)
    {
        *dest = 0;
        return dest;
    }

    const unsigned limit = size - 1;
    unsigned used = 0;
    const uchar* cur = source;

    while (*cur != 0)
    {
        const uchar* next = cur;
        utf8::unchecked::next(next);

        const auto len = static_cast<unsigned>(next - cur);

        if (used + len > limit)
            break;

        std::memcpy(dest + used, cur, len);
        used += len;
        cur = next;
    }

    dest[used] = 0;
    return dest;
}

uchar* TruncateStringNarrow(uchar* dest, const uchar* source, unsigned size)
{
    if (dest == nullptr)
    {
        return nullptr;
    }

    if (source == nullptr || size == 0)
    {
        *dest = 0;
        return dest;
    }

    const unsigned limit = size - 1;
    unsigned used = 0;
    const uchar* cur = source;

    while (*cur != 0)
    {
        const uchar* next = cur;
        utf8::unchecked::next(next);

        const auto len = static_cast<unsigned>(next - cur);

        // 放不下就整个丢弃，不留半截序列
        if (used + len > limit)
        {
            break;
        }

        std::memcpy(dest + used, cur, len);
        used += len;
        cur = next;
    }

    dest[used] = 0;
    return dest;
}

uchar* TruncateStringWideToUtf8(uchar* dest, const GTAChar* source, unsigned size)
{
    if (dest == nullptr)
        return nullptr;

    if (source == nullptr || size == 0)
    {
        *dest = 0;
        return dest;
    }

    // 先就地还原被引擎写坏的宽串，中文才有码位可查 CJK 图集
    RepairByteExpandedWide(const_cast<GTAChar*>(source));

    const unsigned limit = size - 1;
    unsigned used = 0;

    for (const GTAChar* p = source; *p != 0; ++p)
    {
        std::uint32_t cp = *p;

        if (cp >= 0xD800u && cp <= 0xDBFFu && p[1] >= 0xDC00u && p[1] <= 0xDFFFu)
        {
            cp = 0x10000u + ((cp - 0xD800u) << 10) + (p[1] - 0xDC00u);
            ++p;
        }

        uchar buf[4];
        const auto len = append_utf8(buf, cp);

        if (used + len > limit)
            break;

        std::memcpy(dest + used, buf, len);
        used += len;
    }

    dest[used] = 0;

    return dest;
}

// 存档名是「窄 UTF-8 被逐字节扩成 UTF-16」存下来的（00E5 0085 …），码位不在 CJK 图集所以整段不绘制；低字节完整保留了原始字节，可无损还原
bool RepairByteExpandedWide(GTAChar* buf)
{
    if (buf == nullptr)
        return false;

    uchar bytes[kSlotNameCap];
    unsigned n = 0;
    bool has_high = false;

    for (; n < kSlotNameCap; ++n)
    {
        const GTAChar w = buf[n];

        if (w == 0)
            break;

        const unsigned hi = static_cast<unsigned>((w >> 8) & 0xFFu);
        const unsigned lo = static_cast<unsigned>(w & 0xFFu);

        // 只认两种逐字节扩展形态：0x00XX、0xFFXX
        if (hi == 0x00u)
        {
            if (lo >= 0x80u)
                has_high = true;
        }
        else if (hi == 0xFFu && lo >= 0x80u)
        {
            has_high = true;
        }
        else
        {
            return false;
        }

        bytes[n] = static_cast<uchar>(lo);
    }

    // 空串 / 纯 ASCII / 撞上限 一律不动
    if (n == 0 || n >= kSlotNameCap || !has_high)
        return false;

    GTAChar wide[kSlotNameCap];
    unsigned m = 0;
    bool shrunk = false;

    const uchar* it = bytes;
    const uchar* const end_it = bytes + n;

    while (it != end_it && m < kSlotNameCap)
    {
        const uchar* const prev = it;
        std::uint32_t cp = 0;

        try
        {
            cp = utf8::next(it, end_it);
        }
        catch (...)
        {
            break;
        }

        if (static_cast<unsigned>(it - prev) > 1)
            shrunk = true;

        if (cp < 0x10000u)
        {
            wide[m++] = static_cast<GTAChar>(cp);
        }
        else if (cp < 0x110000u && m + 1 < kSlotNameCap)
        {
            const auto v = cp - 0x10000u;
            wide[m++] = static_cast<GTAChar>(0xD800u + (v >> 10));
            wide[m++] = static_cast<GTAChar>(0xDC00u + (v & 0x3FFu));
        }
        else
        {
            break;
        }
    }

    // 一个多字节序列都没解出来，说明本来就不是被扩展的串
    if (m == 0 || !shrunk)
        return false;

    std::memcpy(buf, wide, m * sizeof(GTAChar));
    buf[m] = 0;

    return true;
}
}
