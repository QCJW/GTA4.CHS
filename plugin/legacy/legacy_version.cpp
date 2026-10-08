#include "legacy.h"

#ifdef _WIN32
#include <windows.h>
#include <winver.h>
#include <vector>
#pragma comment(lib, "version.lib")

namespace gta4chs::legacy
{
    namespace
    {
        detected classify(std::uint16_t major, std::uint16_t minor, std::uint16_t patch)
        {
            if (major == 1 && minor == 0 && patch == 4)
            {
                return detected::v104;
            }

            if (major == 1 && minor == 0 && patch == 7)
            {
                return detected::v107;
            }

            if (major == 1 && minor == 0 && patch == 8)
            {
                return detected::v108;
            }

            if (major == 1 && minor == 2)
            {
                return detected::complete_edition;
            }

            return detected::unknown;
        }

        // 版本资源读不到时（个别重打包/加壳 EXE）的最后兜底：按文件大小分桶。
        // 标定样本：1.0.4=13,822,600；1.0.7=15,505,792；1.0.8=15,718,808；1.2.0.59=17,425,752。
        detected classify_by_size()
        {
            wchar_t path[MAX_PATH]{};

            if (::GetModuleFileNameW(nullptr, path, MAX_PATH) == 0)
            {
                return detected::unknown;
            }

            WIN32_FILE_ATTRIBUTE_DATA fad{};

            if (!::GetFileAttributesExW(path, GetFileExInfoStandard, &fad))
            {
                return detected::unknown;
            }

            ULARGE_INTEGER size{};
            size.LowPart = fad.nFileSizeLow;
            size.HighPart = fad.nFileSizeHigh;
            const auto n = size.QuadPart;

            static constexpr std::uint64_t kRefs[] = {
                13822600ull, // 1.0.4
                15505792ull, // 1.0.7
                15718808ull, // 1.0.8
                17425752ull, // 1.2.0.59
            };
            static constexpr detected kTags[] = {
                detected::v104, detected::v107, detected::v108, detected::complete_edition,
            };

            // 必须取最近的桶：两版 ±2% 窗口重叠 0.42M，真实 1.0.8 落在 1.0.7 窗口里，先命中先算会判错。
            std::uint64_t best_delta = ~0ull;
            int best_index = -1;

            for (int i = 0; i < 4; ++i)
            {
                const auto delta = (n > kRefs[i]) ? (n - kRefs[i]) : (kRefs[i] - n);

                if (delta < best_delta)
                {
                    best_delta = delta;
                    best_index = i;
                }
            }

            if (best_index < 0 || best_delta > kRefs[best_index] / 50) // 超出 ±2% 就不猜
            {
                return detected::unknown;
            }

            return kTags[best_index];
        }
    }

    detected detect_host_version()
    {
        wchar_t path[MAX_PATH]{};

        if (::GetModuleFileNameW(nullptr, path, MAX_PATH) == 0)
        {
            return detected::unknown;
        }

        DWORD handle = 0;
        DWORD info_size = ::GetFileVersionInfoSizeW(path, &handle);

        if (info_size > 0)
        {
            std::vector<std::uint8_t> buf(info_size);

            if (::GetFileVersionInfoW(path, 0, info_size, buf.data()))
            {
                VS_FIXEDFILEINFO *ffi = nullptr;
                UINT len = 0;

                if (::VerQueryValueW(buf.data(), L"\\", reinterpret_cast<LPVOID *>(&ffi), &len) &&
                    ffi && len >= sizeof(VS_FIXEDFILEINFO))
                {
                    auto d = classify(HIWORD(ffi->dwFileVersionMS), LOWORD(ffi->dwFileVersionMS),
                                      HIWORD(ffi->dwFileVersionLS));

                    if (d != detected::unknown)
                    {
                        return d;
                    }
                }
            }
        }

        return classify_by_size();
    }
}
#else
namespace gta4chs::legacy
{
    detected detect_host_version()
    {
        return detected::unknown;
    }
}
#endif
