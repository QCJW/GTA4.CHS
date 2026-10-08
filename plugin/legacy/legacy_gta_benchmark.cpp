#include "../gta_benchmark.h"
#include "legacy.h"
#include "../gta_string.h"

namespace gta4chs::legacy
{
void install_benchmark()
{
    // Benchmark输出文字转码
    const auto base = pick2(0x407420, 0x477D00);

    for (const auto offset : {0x46Cu, 0x8D3u, 0x90Fu, 0x94Bu, 0x987u, 0x9C3u, 0x9FFu, 0xA3Bu, 0xA77u, 0xAB3u,
                              0x137Fu, 0x1483u, 0x19F3u, 0x1A3Au, 0x1A85u, 0x1AC1u, 0x1AFDu, 0x1B39u, 0x1B6Au,
                              0x207Cu})
    {
        injector::MakeCALL(injector::aslr_ptr(base + offset).get(), gta_string::gtaTruncateString2);
    }
}
} // namespace gta_benchmark
