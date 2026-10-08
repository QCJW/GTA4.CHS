#include "../gta_phone.h"
#include "legacy.h"
#include "../gta_string.h"

namespace gta4chs::legacy
{
void install_phone()
{
    injector::MakeCALL(injector::aslr_ptr(pick2(0x803CEE, 0x8AE2CE)).get(), gta_string::gtaExpandString3); // bank0
    injector::MakeCALL(injector::aslr_ptr(pick2(0x803CAF, 0x8AE28F)).get(), gta_string::gtaExpandString3); // bank1

    // Native: DISPLAY_TEXT_WITH_LITERAL_SUBSTRING(1FCB5241)里面第二个调用strncpy的地方
    injector::MakeCALL(injector::aslr_ptr(pick2(0xBB326E, 0xB56B8E)).get(), gta_string::gtaUTF8strncpy);
    injector::MakeNOP(injector::aslr_ptr(pick2(0xBB3282, 0xB56BA2)).get(), 5);
}
} // namespace gta_phone
