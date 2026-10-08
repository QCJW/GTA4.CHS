#pragma once
#include "../common/stdinc.h"

class CWhmTable
{
public:
    void LoadTable(const std::filesystem::path& filename);

    //查表，将whm的原文替换成译文
    const uchar* GetTranslated(const uchar* src) const;

    // 字符串区视图，用于判断「指针是不是表里给的」（1.0.4 零拷贝会把译文指针写进 m_pData，规则见 legacy104/legacy104_whm.cpp）。
    std::span<const uchar> GetStrings() const
    {
        return m_strings;
    }

private:
    std::unordered_map<std::size_t, std::uintptr_t> m_offsets;
    std::vector<uchar> m_strings;
};
