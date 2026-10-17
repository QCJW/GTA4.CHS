#pragma once
#include <cstdint>
namespace gta_html_link
{
    void install_link_rect_fix(std::uintptr_t hook_va, std::uintptr_t done_va, std::uintptr_t origin_off);
}
