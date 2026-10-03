#include "gta_toupper.h"
#include "plugin.h"

namespace gta_toupper
{
    namespace
    {
        struct fix_entry
        {
            const char *name;
            const std::uint8_t *pattern;
            std::size_t pattern_size;
            std::ptrdiff_t site_offset;  // 越界分支那条 jmp 的立即数字节，相对 pattern 起点
            std::uint8_t site_expect;    // 原始立即数（自检用）
            std::uint8_t site_patch;     // 修正后立即数（改跳到底下的补齐代码）
            std::ptrdiff_t tramp_offset; // 补齐代码相对 pattern 起点
            std::size_t tramp_size;
            const std::uint8_t *tramp;
        };

        // 9 字节特征：cmp dx,0xE0 ; jb skip ; jmp case_shift
        // 位于 1.0.8.0 的 sub_873C80 内 0x873CBA；越界分支的 jmp 在 0x873CC1。
        const std::uint8_t kPattern[] = {
            0x66, 0x81, 0xFA, 0xE0, 0x00, 0x72, 0x1F, 0xEB, 0x1A};

        const std::uint8_t kTramp[] = {
            0x66, 0x81, 0xFA, 0xFF, 0x00, 0x77, 0xE2, 0xEB, 0xDD};

        const fix_entry kFix = {
            "1.0.8.0 sub_873C80",
            kPattern, sizeof(kPattern),
            0x08, 0x1A, 0x34,           // jmp 立即数 0x1A -> 0x34（跳去 0x873CF7）
            0x3D, sizeof(kTramp), kTramp};

        const std::uint8_t *find_bytes(const std::uint8_t *beg, const std::uint8_t *end,
                                       const std::uint8_t *pat, std::size_t n,
                                       std::size_t &hit_count)
        {
            hit_count = 0;
            const std::uint8_t *first = nullptr;

            if (end < beg || static_cast<std::size_t>(end - beg) < n)
            {
                return nullptr;
            }

            for (auto p = beg; p <= end - n; ++p)
            {
                if (std::memcmp(p, pat, n) == 0)
                {
                    if (!first)
                    {
                        first = p;
                    }

                    ++hit_count;
                }
            }

            return first;
        }

        // 与 injector 的写法保持一致：WriteMemory 的第一个参数要能转换为 memory_pointer_tr
        inline void patch_byte(std::uint8_t *p, std::uint8_t v)
        {
            injector::WriteMemory<std::uint8_t>(injector::memory_pointer_raw(p), v, true);
        }
    }

    void apply()
    {
        auto base = reinterpret_cast<std::uintptr_t>(plugin.GetGameModule());
        auto dos = reinterpret_cast<IMAGE_DOS_HEADER *>(base);
        auto nt = reinterpret_cast<IMAGE_NT_HEADERS32 *>(base + dos->e_lfanew);
        auto sec = IMAGE_FIRST_SECTION(nt);
        std::uint8_t *text_beg = nullptr;
        std::uint8_t *text_end = nullptr;

        for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++sec)
        {
            if (std::memcmp(sec->Name, ".text", 5) == 0)
            {
                text_beg = reinterpret_cast<std::uint8_t *>(base + sec->VirtualAddress);
                text_end = text_beg + sec->Misc.VirtualSize;
                break;
            }
        }

        if (!text_beg)
        {
            return;
        }

        std::size_t hits = 0;
        auto found = find_bytes(text_beg, text_end, kFix.pattern, kFix.pattern_size, hits);

        if (!found || hits != 1)
        {
            return;
        }

        auto site = const_cast<std::uint8_t *>(found + kFix.site_offset);
        auto tramp = const_cast<std::uint8_t *>(found + kFix.tramp_offset);

        // 自检一：分支跳转的立即数得是待修正的原值。
        if (*site != kFix.site_expect)
        {
            return;
        }

        // 自检二：补齐代码要落的那段填充必须还是 int3（没被别的模组占用）。
        for (std::size_t i = 0; i < kFix.tramp_size; ++i)
        {
            if (tramp[i] != 0xCC)
            {
                return;
            }
        }

        // 先铺补齐代码，再改分支跳转：最坏情况只是没人跳过去，不会跑飞。
        for (std::size_t i = 0; i < kFix.tramp_size; ++i)
        {
            patch_byte(tramp + i, kFix.tramp[i]);
        }

        patch_byte(site, kFix.site_patch);
    }
}
