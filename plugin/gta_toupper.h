#pragma once
#include "../common/stdinc.h"

namespace gta_toupper
{
    // exe 的转大写例程漏了上界，CJK 会被减 0x20。宽字符版补 0xFF 上界即可；
    // 1.0.4 是 UTF-8 字节流，8 位补上界无意义，改为整条外语分支短路。
    bool apply();
}
