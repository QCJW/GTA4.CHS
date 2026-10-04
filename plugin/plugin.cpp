#include "plugin.h"
#include "font.h"
#include "game.h"
#include "char_table.h"
#include "gta_string.h"
#include "gta_benchmark.h"
#include "gta_font.h"
#include "gta_game.h"
#include "gta_html.h"
#include "gta_menu.h"
#include "gta_phone.h"
#include "gta_save.h"
#include "gta_whm.h"
#include "gta_mail_reply.h"
#include "gta_toupper.h"
#include "legacy/legacy.h"
#include <windows.h>

// legacy 模块定义在 gta4chs 命名空间下
namespace legacy = gta4chs::legacy;

CPlugin plugin;

void CPlugin::RegisterPatchSteps(batch_matching &batch_matcher)
{
    gta_benchmark::register_patches(batch_matcher);
    gta_font::register_patches(batch_matcher);
    gta_game::register_patches(batch_matcher);
    gta_html::register_patches(batch_matcher);
    gta_mail_reply::register_patches(batch_matcher);
    gta_menu::register_patches(batch_matcher);
    gta_phone::register_patches(batch_matcher);
    gta_save::register_patches(batch_matcher);
    gta_whm::register_patches(batch_matcher);
}

HANDLE CPlugin::GetGameModule() const
{
    return game_module_path.GetModule();
}

HANDLE CPlugin::GetPluginModule() const
{
    return plugin_module_path.GetModule();
}

std::filesystem::path CPlugin::GetGameRoot() const
{
    return game_module_path.GetModuleDir();
}

std::filesystem::path CPlugin::GetPluginAsset(const std::filesystem::path &rest_path) const
{
    return plugin_module_path.GetModuleDir() / "GTA4.CHS" / rest_path;
}

bool CPlugin::Init(HMODULE module)
{
    game_module_path.SetModule(::GetModuleHandleW(nullptr));
    plugin_module_path.SetModule(module);

    // 三合一：运行时按宿主 EXE 版本选择挂钩路径。
    //   1.0.7.0 / 1.0.8.0：经验证的硬编码地址表（legacy）；
    //   1.2.0.x（完整版/CE）：特征码批量扫描（batch_matching）；
    //   识别不出来时默认走特征码路径——特征码失败会安全放弃加载，不会写飞。
    const auto detected = legacy::detect_host_version();

    if (detected == legacy::detected::v107 || detected == legacy::detected::v108)
    {
        ::OutputDebugStringA(detected == legacy::detected::v107
                                 ? "[GTA4.CHS] 识别为 1.0.7.0，走 legacy 地址表路径\r\n"
                                 : "[GTA4.CHS] 识别为 1.0.8.0，走 legacy 地址表路径\r\n");

        if (!legacy::install_all(detected == legacy::detected::v107 ? legacy::game_version::v107
                                                                    : legacy::game_version::v108))
        {
            return false;
        }
    }
    else
    {
        ::OutputDebugStringA("[GTA4.CHS] 识别为 1.2.0.x（完整版/CE）或未知版本，走特征码路径\r\n");

        batch_matching batch_matcher;

        RegisterPatchSteps(batch_matcher);

        batch_matcher.perform_search();

        if (!batch_matcher.is_all_succeed())
        {
            ::OutputDebugStringA("[GTA4.CHS] 特征码未全部命中，放弃加载（请用 DebugView 查看未命中步骤）\r\n");
            return false;
        }

        batch_matcher.run_callbacks();
    }

    // 根因修复：exe 内“宽字符串转大写”函数的 ch>=0xE0 分支漏上界，
    // 导致所有 CJK 被减 0x20（ESC 地图图例地名：博阿博 -> 区队区）。
    // 不登记进 batch_matching：换版本没命中只该跳过这一处，不能拖垮其余补丁。
    gta_toupper::apply();

    char_table.LoadTable(GetPluginAsset("char_table.dat"));
    whm_table.LoadTable(GetPluginAsset("whm_table.dat"));
    string_table.LoadTable(GetPluginAsset("custom_translations.json"));

    return true;
}
