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
        // 标定样本：1.0.7=15,505,792；1.0.8=15,718,808；1.2.0.59=17,425,752。
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

            const std::uint64_t k107 = 15505792ull;
            const std::uint64_t k108 = 15718808ull;
            const std::uint64_t kCE = 17425752ull;

            auto near_of = [n](std::uint64_t ref) {
                const std::uint64_t tol = ref / 50; // ±2%
                return n > ref - tol && n < ref + tol;
            };

            if (near_of(k107))
            {
                return detected::v107;
            }

            if (near_of(k108))
            {
                return detected::v108;
            }

            if (near_of(kCE))
            {
                return detected::complete_edition;
            }

            return detected::unknown;
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
