#include "../gta_save.h"
#include "legacy104.h"
#include "legacy104_string.h"

namespace gta4chs::legacy104
{
void install_save()
{
    // 0x7E8730 是「宽 → 窄」；上游取低字节的降级会让汉字全烂，这里做真转码
    injector::MakeCALL(injector::aslr_ptr(0x7A5CEA).get(), TruncateStringWideToUtf8);

    // MO_SLOT / 覆盖提示这几处是原版窄 strncpy，会切断多字节序列，换成安全截断
    injector::MakeCALL(injector::aslr_ptr(0x798E45).get(), TruncateStringNarrow);

    injector::MakeCALL(injector::aslr_ptr(0x7A747F).get(), TruncateStringNarrow);
    injector::MakeCALL(injector::aslr_ptr(0x7A7490).get(), TruncateStringNarrow);
    injector::MakeCALL(injector::aslr_ptr(0x7A755D).get(), TruncateStringNarrow);
    injector::MakeCALL(injector::aslr_ptr(0x7A756E).get(), TruncateStringNarrow);
}
}
