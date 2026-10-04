#include "legacy.h"

namespace gta4chs::legacy
{
    game_version g_version = game_version::v108;

    // 各模块安装函数（由 vcnge107/vcnge108 源码机械生成，地址经三个真实 EXE 静态标定）
    void install_benchmark(game_version ver);
    void install_font(game_version ver);
    void install_game(game_version ver);
    void install_html(game_version ver);
    void install_mail_reply(game_version ver);
    void install_menu(game_version ver);
    void install_phone(game_version ver);
    void install_save(game_version ver);
    void install_whm(game_version ver);

    bool install_all(game_version v)
    {
        g_version = v;

        // 顺序与原 vcnge 插件 RegisterPatchSteps 保持一致
        install_benchmark(v);
        install_font(v);
        install_game(v);
        install_html(v);
        install_mail_reply(v);
        install_menu(v);
        install_phone(v);
        install_save(v);
        install_whm(v);

        return true;
    }
}
