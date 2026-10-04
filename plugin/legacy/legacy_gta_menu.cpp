#include "../gta_menu.h"
#include "legacy.h"
#include "../font.h"

namespace gta4chs::legacy
{
    // master 已移除 GetStringWidthGetAllDetour，legacy 菜单 4 个 CALL 仍需要：
    // 原实现就是无条件按“取全部宽度”调用 GetStringWidthRemake。
    float GetStringWidthGetAllDetour(const GTAChar *str, bool /*get_all*/)
    {
        return CFont::GetStringWidthRemake(str, true);
    }

void install_menu(game_version ver)
{
    (void)ver;
    // 密集调用GetStringWidthJump的一个函数
    // Esc菜单Header热区
    injector::WriteMemory(injector::aslr_ptr(legacy::pick(0x4134F0, 0x483B10)).get(), true, true);
    injector::WriteMemory(injector::aslr_ptr(legacy::pick(0x41350E, 0x483B2E)).get(), true, true);
    injector::WriteMemory(injector::aslr_ptr(legacy::pick(0x413534, 0x483B54)).get(), true, true);
    injector::WriteMemory(injector::aslr_ptr(legacy::pick(0x41355A, 0x483B7A)).get(), true, true);
    injector::WriteMemory(injector::aslr_ptr(legacy::pick(0x413580, 0x483BA0)).get(), true, true);
    injector::WriteMemory(injector::aslr_ptr(legacy::pick(0x4135A6, 0x483BC6)).get(), true, true);
    injector::WriteMemory(injector::aslr_ptr(legacy::pick(0x4135CC, 0x483BEC)).get(), true, true);
    injector::WriteMemory(injector::aslr_ptr(legacy::pick(0x4139E1, 0x484001)).get(), true, true);

    // 密集调用GetStringWidthJump的另一个函数
    // Esc菜单Header间距
    injector::WriteMemory(injector::aslr_ptr(legacy::pick(0x41558B, 0x48561B)).get(), true, true);
    injector::WriteMemory(injector::aslr_ptr(legacy::pick(0x4155AD, 0x48563D)).get(), true, true);
    injector::WriteMemory(injector::aslr_ptr(legacy::pick(0x4155D3, 0x485663)).get(), true, true);
    injector::WriteMemory(injector::aslr_ptr(legacy::pick(0x4155F9, 0x485689)).get(), true, true);
    injector::WriteMemory(injector::aslr_ptr(legacy::pick(0x41561F, 0x4856AF)).get(), true, true);
    injector::WriteMemory(injector::aslr_ptr(legacy::pick(0x415645, 0x4856D5)).get(), true, true);
    injector::WriteMemory(injector::aslr_ptr(legacy::pick(0x41566B, 0x4856FB)).get(), true, true);

    // //'Esc菜单Header热区'同一个函数里
    // // 视频编辑器菜单Header热区
    injector::MakeCALL(injector::aslr_ptr(legacy::pick(0x413A9A, 0x4840BA)).get(), GetStringWidthGetAllDetour);
    injector::MakeCALL(injector::aslr_ptr(legacy::pick(0x413ABF, 0x4840DF)).get(), GetStringWidthGetAllDetour);
    injector::MakeCALL(injector::aslr_ptr(legacy::pick(0x413AE4, 0x484104)).get(), GetStringWidthGetAllDetour);
    injector::MakeCALL(injector::aslr_ptr(legacy::pick(0x413B09, 0x484129)).get(), GetStringWidthGetAllDetour);

    // //'Esc菜单Header间距'同一个函数里
    // // 视频编辑器菜单Header间距
    injector::WriteMemory(injector::aslr_ptr(legacy::pick(0x41641C, 0x4864AC)).get(), true, true);
    injector::WriteMemory(injector::aslr_ptr(legacy::pick(0x41643E, 0x4864CE)).get(), true, true);
    injector::WriteMemory(injector::aslr_ptr(legacy::pick(0x416464, 0x4864F4)).get(), true, true);
    injector::WriteMemory(injector::aslr_ptr(legacy::pick(0x41648A, 0x48651A)).get(), true, true);
}
} // namespace gta_menu
