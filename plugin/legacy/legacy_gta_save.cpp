#include "../gta_save.h"
#include "legacy.h"
#include "../gta_string.h"

namespace gta4chs::legacy
{
void install_save()
{
    // 在使用"MO_SLOT"的函数里面
    injector::MakeCALL(injector::aslr_ptr(pick2(0x789F9F, 0x7C0C8F)).get(), gta_string::gtaExpandString);

    // 存档覆盖提示
    // 使用"SG_AUTO_SL_OVR2"的地方往前一点
    injector::MakeCALL(injector::aslr_ptr(pick2(0x82E6BA, 0x859B7A)).get(), gta_string::gtaExpandString2);

    // 使用"SG_SAVE_OVRWRT2"的地方往前一点
    injector::MakeCALL(injector::aslr_ptr(pick2(0x82E79D, 0x859C5D)).get(), gta_string::gtaExpandString2);

    // 使用"MO_BENCH_TEST"的函数里
    // 加载黑屏右下角的任务名
    injector::MakeCALL(injector::aslr_ptr(pick2(0x82D92F, 0x858DEF)).get(), gta_string::gtaExpandString);

    // 在上方被替换位置的前一个调用里面
    injector::MakeCALL(injector::aslr_ptr(pick2(0x82C59C, 0x857A5C)).get(), gta_string::gtaTruncateString);
}
} // namespace gta_save
