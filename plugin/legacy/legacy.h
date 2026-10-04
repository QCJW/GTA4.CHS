#pragma once
#include <cstdint>

// 三合一通用插件：1.0.7.0 / 1.0.8.0 走经验证的地址表（legacy 路径），
// 1.2.0.x（完整版/CE）走特征码引擎（master 路径）。
namespace gta4chs::legacy
{
    enum class game_version : unsigned char
    {
        v107, // GTA4 1.0.7.0 / 罪都 NGE v1.07（内核 1.0.7.0）
        v108  // GTA4 1.0.8.0 / 罪都 NGE v1.08（内核 1.0.8.0）
    };

    // 宿主 EXE 版本探测结果
    enum class detected : unsigned char
    {
        v107,
        v108,
        complete_edition, // 1.2.0.x
        unknown
    };

    detected detect_host_version();

    // install_all 进入后立即设置，pick() 据此选地址
    extern game_version g_version;

    // 同一挂钩站点在 1.0.7 / 1.0.8 下的硬编码地址（VA，与 injector::aslr_ptr 口径一致）
    inline std::uintptr_t pick(std::uintptr_t a107, std::uintptr_t a108)
    {
        return g_version == game_version::v107 ? a107 : a108;
    }

    bool install_all(game_version v);
}
