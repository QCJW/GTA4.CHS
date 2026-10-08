#pragma once
#include <cstdint>

// 1.0.4 专用适配层：UI 文本管线是 8-bit 窄字符（UTF-8），与 legacy/（1.0.7 / 1.0.8 的 16 位 GTAChar）完全不通用。
// 唯一对外接触点是字体后端表（font.h 的 FontBackend）：安装时登记一次窄字符后端。
namespace gta4chs::legacy104
{
    bool install_all();

    // 熔断状态：单次解析里 EOF 被反复命中即判跑飞，之后所有站点返回 -1（引擎循环出口一律 `cmp eax,-1`）
    bool MrPanicked();
}
