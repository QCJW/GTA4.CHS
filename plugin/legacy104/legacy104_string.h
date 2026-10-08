#pragma once
#include "../../common/stdinc.h"
#include "../../common/common.h"

// 1.0.4 的 UI 全程是窄串（UTF-8），根目录 gta_string 那几个函数按宽管线写，不适用。size 是字节上限（strncpy 语义）。
namespace gta4chs::legacy104
{
    // 网页/邮件的固定缓冲拷贝点必须用它：按字符数算会写出 3 倍字节。
    uchar* StrncpyByteLimited(uchar* dest, const uchar* source, unsigned size);

    uchar* TruncateStringNarrow(uchar* dest, const uchar* source, unsigned size);

    // 存档名专用：宽源 → UTF-8 窄目标。
    uchar* TruncateStringWideToUtf8(uchar* dest, const GTAChar* source, unsigned size);

    // 存档名被引擎逐字节扩成 UTF-16（低字节仍是原 UTF-8 字节），还原后原地写回；返回是否修正过
    bool RepairByteExpandedWide(GTAChar* buf);
}
