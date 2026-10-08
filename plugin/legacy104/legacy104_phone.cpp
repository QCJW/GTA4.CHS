#include "../gta_phone.h"
#include "legacy104.h"
#include "../gta_string.h"

namespace gta4chs::legacy104
{
// TBoGT 重玩任务选择界面在 1.0.4 无对应点：窄字符管线没有「UTF-8 → 宽展开」这一步
void install_phone()
{
    // DISPLAY_TEXT_WITH_LITERAL_SUBSTRING 里第二个 strncpy
    injector::MakeCALL(injector::aslr_ptr(0xA1427E).get(), gta_string::gtaUTF8strncpy);
    injector::MakeNOP(injector::aslr_ptr(0xA14292).get(), 5);
}
}
