#include "../gta_menu.h"
#include "legacy104.h"
#include "legacy104_narrow.h"
#include "../font.h"

namespace gta4chs::legacy104
{
// 菜单这 4 站量宽要按「取全部宽度」，入参是 1.0.4 的窄串
float GetStringWidthGetAllDetour104(const char *str, bool )
{
    return narrow::GetStringWidthRemake104(str, true);
}

void install_menu()
{
    // ESC 菜单 Header 热区：push 0 → push 1
    injector::WriteMemory(injector::aslr_ptr(0x47EEE3).get(), true, true);
    injector::WriteMemory(injector::aslr_ptr(0x47EF01).get(), true, true);
    injector::WriteMemory(injector::aslr_ptr(0x47EF27).get(), true, true);
    injector::WriteMemory(injector::aslr_ptr(0x47EF4D).get(), true, true);
    injector::WriteMemory(injector::aslr_ptr(0x47EF73).get(), true, true);
    injector::WriteMemory(injector::aslr_ptr(0x47EF99).get(), true, true);
    injector::WriteMemory(injector::aslr_ptr(0x47EFBF).get(), true, true);
    injector::WriteMemory(injector::aslr_ptr(0x47F3CB).get(), true, true);

    // ESC 菜单 Header 间距
    injector::WriteMemory(injector::aslr_ptr(0x480F30).get(), true, true);
    injector::WriteMemory(injector::aslr_ptr(0x480F52).get(), true, true);
    injector::WriteMemory(injector::aslr_ptr(0x480F78).get(), true, true);
    injector::WriteMemory(injector::aslr_ptr(0x480F9E).get(), true, true);
    injector::WriteMemory(injector::aslr_ptr(0x480FC4).get(), true, true);
    injector::WriteMemory(injector::aslr_ptr(0x480FEA).get(), true, true);
    injector::WriteMemory(injector::aslr_ptr(0x481010).get(), true, true);

    // 视频编辑器菜单 Header 热区
    injector::MakeCALL(injector::aslr_ptr(0x47F477).get(), GetStringWidthGetAllDetour104);
    injector::MakeCALL(injector::aslr_ptr(0x47F49C).get(), GetStringWidthGetAllDetour104);
    injector::MakeCALL(injector::aslr_ptr(0x47F4C1).get(), GetStringWidthGetAllDetour104);
    injector::MakeCALL(injector::aslr_ptr(0x47F4E6).get(), GetStringWidthGetAllDetour104);

    // 视频编辑器菜单 Header 间距
    injector::WriteMemory(injector::aslr_ptr(0x481BDE).get(), true, true);
    injector::WriteMemory(injector::aslr_ptr(0x481C00).get(), true, true);
    injector::WriteMemory(injector::aslr_ptr(0x481C26).get(), true, true);
    injector::WriteMemory(injector::aslr_ptr(0x481C4C).get(), true, true);
}
}
