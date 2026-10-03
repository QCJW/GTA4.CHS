#pragma once
#include "../common/stdinc.h"

namespace gta_toupper
{
    // exe 的宽字符串转大写例程漏了 0xFF 上界，所有 CJK 会被减 0x20。
    bool apply();
}
