#include "gta_toupper.h"
#include "plugin.h"
#include "byte_pattern.h"

namespace gta_toupper
{
    namespace
    {
        // 漏上界那条分支的形状：cmp cx,<0xE0 寄存器> / jb rel8 / jmp rel8。
        // 两个 rel8 只是布局距离，交给 byte_pattern 通配；换版本寄存器分配变了就往数组里补。
        constexpr const char *kPatterns[] = {"66 3B CF 72 ? EB ?"};

        constexpr std::ptrdiff_t kJbImm = 4;   // jb 的 rel8，相对特征码起点
        constexpr std::ptrdiff_t kJmpInsn = 5; // jmp 指令起点
        constexpr std::ptrdiff_t kJmpImm = 6;  // jmp 的 rel8，改写的就是它

        constexpr std::uint16_t kUpperBound = 0xFF; // Latin-1 小写区上界，超过则原样写回

        constexpr std::size_t kTrampSize = 9;    // cmp cx,imm16(5) + ja rel8(2) + jmp rel8(2)
        constexpr std::size_t kSlotWindow = 128; // 从写回块往后找 int3 槽位的跨度上限

        bool fits_rel8(std::ptrdiff_t d)
        {
            return d >= -128 && d <= 127;
        }

        // 转大写块：add <r32>,-0x20 或 sub <r32>,0x20，两种编码都认。
        bool is_dec_0x20(const std::uint8_t *p)
        {
            if (p[0] != 0x83)
            {
                return false;
            }

            auto op = p[1] & 0x38;

            return (op == 0x00 && p[2] == 0xE0) || (op == 0x28 && p[2] == 0x20);
        }

        // 写回块：mov [<r32>], <r16>。
        bool is_store_r16(const std::uint8_t *p)
        {
            return p[0] == 0x66 && p[1] == 0x89;
        }

        // 与 injector 的写法保持一致：WriteMemory 的第一个参数要能转换为 memory_pointer_tr
        void patch_byte(std::uint8_t *p, std::uint8_t v)
        {
            injector::WriteMemory<std::uint8_t>(injector::memory_pointer_raw(p), v, true);
        }

        // 往后找第一段装得下补齐代码的 int3 填充。
        std::uint8_t *find_slot(std::uint8_t *beg, std::uint8_t *end)
        {
            for (auto p = beg; p + kTrampSize <= end; ++p)
            {
                std::size_t n = 0;

                while (n < kTrampSize && p[n] == 0xCC)
                {
                    ++n;
                }

                if (n == kTrampSize)
                {
                    return p;
                }
            }

            return nullptr;
        }

        bool locate_text(std::uint8_t *&beg, std::uint8_t *&end)
        {
            auto base = reinterpret_cast<std::uintptr_t>(plugin.GetGameModule());
            auto dos = reinterpret_cast<IMAGE_DOS_HEADER *>(base);
            auto nt = reinterpret_cast<IMAGE_NT_HEADERS32 *>(base + dos->e_lfanew);
            auto sec = IMAGE_FIRST_SECTION(nt);

            for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++sec)
            {
                if (std::memcmp(sec->Name, ".text", 5) == 0)
                {
                    beg = reinterpret_cast<std::uint8_t *>(base + sec->VirtualAddress);
                    end = beg + sec->Misc.VirtualSize;
                    return true;
                }
            }

            return false;
        }

        bool apply_one(std::uint8_t *hit, std::uint8_t *text_beg, std::uint8_t *text_end)
        {
            if (hit < text_beg || hit + kJmpImm >= text_end)
            {
                return false;
            }

            // 由两条分支自带的 rel8 反推块地址，不依赖任何硬编码 VA。
            auto write_block = hit + kJbImm + 1 + static_cast<std::int8_t>(hit[kJbImm]);
            auto upper_block = hit + kJmpImm + 1 + static_cast<std::int8_t>(hit[kJmpImm]);

            // 语义自检：认不出转大写块 / 写回块就放弃，顺带天然防重复打补丁。
            if (write_block + 2 > text_end || upper_block + 3 > text_end ||
                !is_dec_0x20(upper_block) || !is_store_r16(write_block))
            {
                return false;
            }

            auto slot_end = text_end;
            if (write_block + kSlotWindow < slot_end)
            {
                slot_end = write_block + kSlotWindow;
            }

            auto slot = find_slot(write_block, slot_end);

            if (!slot)
            {
                return false;
            }

            std::ptrdiff_t rel_ja = write_block - (slot + 7);
            std::ptrdiff_t rel_jmp = upper_block - (slot + 9);
            std::ptrdiff_t rel_site = slot - (hit + kJmpImm + 1);

            // 装不下就整体放弃，绝不写出半截补丁。
            if (!fits_rel8(rel_ja) || !fits_rel8(rel_jmp) || !fits_rel8(rel_site))
            {
                return false;
            }

            std::uint8_t tramp[kTrampSize] = {
                0x66, 0x81, 0xF9,
                static_cast<std::uint8_t>(kUpperBound & 0xFF),
                static_cast<std::uint8_t>(kUpperBound >> 8),
                0x77, static_cast<std::uint8_t>(rel_ja),
                0xEB, static_cast<std::uint8_t>(rel_jmp)};

            // 先铺补齐代码，再改分支跳转：最坏情况只是没人跳过去，不会跑飞。
            for (std::size_t i = 0; i < kTrampSize; ++i)
            {
                patch_byte(slot + i, tramp[i]);
            }

            patch_byte(hit + kJmpImm, static_cast<std::uint8_t>(rel_site));

            auto proc = ::GetCurrentProcess();
            ::FlushInstructionCache(proc, slot, kTrampSize);
            ::FlushInstructionCache(proc, hit + kJmpInsn, 2);

            return true;
        }
    }

    bool apply()
    {
        std::uint8_t *text_beg = nullptr;
        std::uint8_t *text_end = nullptr;

        if (!locate_text(text_beg, text_end))
        {
            return false;
        }

        for (auto pattern : kPatterns)
        {
            byte_pattern matcher;

            matcher.set_pattern(pattern);
            matcher.set_range(text_beg, text_end);
            matcher.search();

            auto hits = matcher.get();

            if (hits.empty())
            {
                // 换版本没命中，试下一条特征码。
                continue;
            }

            if (hits.size() != 1)
            {
                continue;
            }

            if (apply_one(hits.front().p<std::uint8_t>(), text_beg, text_end))
            {
                return true;
            }
        }

        return false;
    }
}
