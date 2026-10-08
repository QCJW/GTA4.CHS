#include "gta_toupper.h"
#include "plugin.h"
#include "byte_pattern.h"

namespace gta_toupper
{
    namespace
    {
        // 宽字符版补上界，8 位版没有上界可补，只能把整条外语分支短路掉。
        enum class patch_kind
        {
            add_upper_bound,
            ascii_only,
        };

        // 漏上界那条分支的形状随版本不同，按候选描述依次尝试：
        //   CE 1.2.0.x：cmp cx,<0xE0 寄存器>(3 字节) / jb rel8 / jmp rel8，补齐块用 cmp cx,0xFF
        //   1.0.7/1.0.8：cmp dx,0xE0(5 字节) / jb rel8 / jmp rel8，补齐块用 cmp dx,0xFF
        //   1.0.4.0：cmp cl,0xE0 / jb rel8 / jmp rel8，8 位，见 ascii_only 的说明
        // 两个 rel8 只是布局距离，交给 byte_pattern 通配；换版本编码变了就往数组里补。
        struct variant_desc
        {
            const char *pattern;
            std::ptrdiff_t jb_imm;   // jb 的 rel8 相对特征码起点的偏移
            std::ptrdiff_t jmp_insn; // jmp 指令起点偏移
            std::ptrdiff_t jmp_imm;  // jmp 的 rel8 偏移，改写的就是它
            std::uint8_t cmp_modrm;  // 补齐块 cmp 的 ModRM（决定比较 cx 还是 dx）
            patch_kind kind;
        };

        constexpr variant_desc kVariants[] = {
            {"66 3B CF 72 ? EB ?", 4, 5, 6, 0xF9, patch_kind::add_upper_bound},        // CE 1.2.0.43/59
            {"66 81 FA E0 00 72 ? EB ?", 6, 7, 8, 0xFA, patch_kind::add_upper_bound}, // 1.0.7.0 / 1.0.8.0
            {"80 F9 E0 72 ? EB ?", 4, 5, 6, 0x00, patch_kind::ascii_only},            // 1.0.4.0
        };

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

        // 8 位转大写块：sub <r8>,0x20（80 /5 20）或 sub al,0x20（2C 20）。
        bool is_dec_0x20_r8(const std::uint8_t *p)
        {
            return (p[0] == 0x80 && (p[1] & 0x38) == 0x28 && p[2] == 0x20) ||
                   (p[0] == 0x2C && p[1] == 0x20);
        }

        // 8 位写回块：mov [<r32>], <r8>。
        bool is_store_r8(const std::uint8_t *p)
        {
            return p[0] == 0x88 && (p[1] & 0xC0) == 0x00;
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

        // 1.0.4 是 UTF-8 字节流：0xE0 以上全是首字节，减 0x20 会打散汉字；8 位补上界等于没补，故短路整条外语分支。
        bool apply_ascii_only(std::uint8_t *hit, const variant_desc &desc, std::uint8_t *text_beg,
                              std::uint8_t *text_end)
        {
            if (hit < text_beg || hit + desc.jmp_imm >= text_end)
            {
                return false;
            }

            // 由两条分支自带的 rel8 反推块地址，不依赖任何硬编码 VA。
            auto write_block = hit + desc.jb_imm + 1 + static_cast<std::int8_t>(hit[desc.jb_imm]);
            auto upper_block = hit + desc.jmp_imm + 1 + static_cast<std::int8_t>(hit[desc.jmp_imm]);

            if (write_block + 2 > text_end || upper_block + 3 > text_end ||
                !is_dec_0x20_r8(upper_block) || !is_store_r8(write_block))
            {
                return false;
            }

            // 整段 = cmp(3) + jb(2) + jmp(2)
            auto span = desc.jmp_imm + 1;
            std::ptrdiff_t rel = write_block - (hit + 2);

            if (!fits_rel8(rel))
            {
                return false;
            }

            patch_byte(hit, 0xEB);
            patch_byte(hit + 1, static_cast<std::uint8_t>(rel));

            for (std::ptrdiff_t i = 2; i < span; ++i)
            {
                patch_byte(hit + i, 0x90);
            }

            auto proc = ::GetCurrentProcess();
            ::FlushInstructionCache(proc, hit, static_cast<SIZE_T>(span));

            return true;
        }

        bool apply_one(std::uint8_t *hit, const variant_desc &desc, std::uint8_t *text_beg,
                       std::uint8_t *text_end)
        {
            if (desc.kind == patch_kind::ascii_only)
            {
                return apply_ascii_only(hit, desc, text_beg, text_end);
            }

            if (hit < text_beg || hit + desc.jmp_imm >= text_end)
            {
                return false;
            }

            // 由两条分支自带的 rel8 反推块地址，不依赖任何硬编码 VA。
            auto write_block = hit + desc.jb_imm + 1 + static_cast<std::int8_t>(hit[desc.jb_imm]);
            auto upper_block = hit + desc.jmp_imm + 1 + static_cast<std::int8_t>(hit[desc.jmp_imm]);

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
            std::ptrdiff_t rel_site = slot - (hit + desc.jmp_imm + 1);

            // 装不下就整体放弃，绝不写出半截补丁。
            if (!fits_rel8(rel_ja) || !fits_rel8(rel_jmp) || !fits_rel8(rel_site))
            {
                return false;
            }

            std::uint8_t tramp[kTrampSize] = {
                0x66, 0x81, desc.cmp_modrm,
                static_cast<std::uint8_t>(kUpperBound & 0xFF),
                static_cast<std::uint8_t>(kUpperBound >> 8),
                0x77, static_cast<std::uint8_t>(rel_ja),
                0xEB, static_cast<std::uint8_t>(rel_jmp)};

            // 先铺补齐代码，再改分支跳转：最坏情况只是没人跳过去，不会跑飞。
            for (std::size_t i = 0; i < kTrampSize; ++i)
            {
                patch_byte(slot + i, tramp[i]);
            }

            patch_byte(hit + desc.jmp_imm, static_cast<std::uint8_t>(rel_site));

            auto proc = ::GetCurrentProcess();
            ::FlushInstructionCache(proc, slot, kTrampSize);
            ::FlushInstructionCache(proc, hit + desc.jmp_insn, 2);

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

        for (const auto &desc : kVariants)
        {
            byte_pattern matcher;

            matcher.set_pattern(desc.pattern);
            matcher.set_range(text_beg, text_end);
            matcher.search();

            auto hits = matcher.get();

            if (hits.size() != 1)
            {
                // 没命中或命中多处都换下一套特征码，绝不挑一个盲改。
                continue;
            }

            if (apply_one(hits.front().p<std::uint8_t>(), desc, text_beg, text_end))
            {
                return true;
            }
        }

        return false;
    }
}
