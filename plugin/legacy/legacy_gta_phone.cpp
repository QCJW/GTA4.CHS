#include "../gta_phone.h"
#include "legacy.h"
#include "../gta_string.h"

namespace gta4chs::legacy
{
void install_phone()
{
    // 用到"Unknown1"的地方往后一个调用的函数里面
    // TBoGT重玩任务选择界面
    injector::MakeCALL(injector::aslr_ptr(pick2(0x803D5F, 0x8AE33F)).get(), gta_string::gtaExpandString3);

    // Native: DISPLAY_TEXT_WITH_LITERAL_SUBSTRING(1FCB5241)里面第二个调用strncpy的地方
    injector::MakeCALL(injector::aslr_ptr(pick2(0xBB326E, 0xB56B8E)).get(), gta_string::gtaUTF8strncpy);
    injector::MakeNOP(injector::aslr_ptr(pick2(0xBB3282, 0xB56BA2)).get(), 5);
}
} // namespace gta_phone
