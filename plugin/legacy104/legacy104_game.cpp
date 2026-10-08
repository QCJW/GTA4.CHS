#include "../gta_game.h"
#include "legacy104.h"
#include "../plugin.h"

namespace gta4chs::legacy104
{
// 引擎侧地址表（1.0.4.0）：注释标注的是消费它的宿主函数
void install_game()
{
    // GET_CURRENT_EPISODE 内的 pGameEpisodeID
    plugin.game.game_addr.pGameEpisodeID = injector::aslr_ptr(0x102E7F4).get();

    // PrintChar 使用
    plugin.game.game_addr.fnGraphics_SetRenderState = injector::aslr_ptr(0x7AAD90).get();

    // CFontInfo 数组，stride 0x250（高版本 0x258）
    plugin.game.game_addr.pFont_Datas = injector::aslr_ptr(0x1009300).get();

    // 配 `token_type - 255` 索引；与引擎的 0x1009DF4 + token_type*4 是同一张表
    plugin.game.game_addr.pFont_ButtonWidths = injector::aslr_ptr(0x100A1F0).get();

    // GetStringWidth 使用；1.0.4 里不与 ButtonWidths 相邻
    plugin.game.game_addr.pFont_BlipWidth = injector::aslr_ptr(0x10089FC).get();

    // CFontDetails 数组，stride 0x44（高版本 0x48）
    plugin.game.game_addr.pFont_Details = injector::aslr_ptr(0x100EB30).get();

    // magic: AD 7F 33 31
    plugin.game.game_addr.pFont_RenderState = injector::aslr_ptr(0xEA2270).get();

    // GetCharacterSizeNormal 使用；1.0.4 槽位在 RS+0x34（高版本 +0x38）
    plugin.game.game_addr.pFont_ResolutionX = injector::aslr_ptr(0xEA22A4).get();

    // ProcessString 使用
    plugin.game.game_addr.fnFont_GetRenderIndex = injector::aslr_ptr(0x7F2660).get();

    // RenderSingleBuffer（AD 7F 33 31 所在函数）使用
    plugin.game.game_addr.fnFont_PrintChar = injector::aslr_ptr(0x7F3040).get();

    // GetStringWidth 使用
    plugin.game.game_addr.fnFont_GetCharacterSizeNormal = injector::aslr_ptr(0x7F2690).get();

    // PrintChar 使用
    plugin.game.game_addr.fnFont_GetCharacterSizeDrawing = injector::aslr_ptr(0x7F1FD0).get();

    // PrintChar 使用
    plugin.game.game_addr.fnFont_Render2DPrimitive = injector::aslr_ptr(0x7F2F60).get();

    // "font3" 附近使用
    plugin.game.game_addr.fnHash_HashStringFromSeediCase = injector::aslr_ptr(0x45A300).get();

    // GetStringWidth 使用
    plugin.game.game_addr.fnFont_ParseToken = injector::aslr_ptr(0x7F5B00).get();

    // ProcessString 使用
    plugin.game.game_addr.fnFont_ProcessToken = injector::aslr_ptr(0x7F7220).get();

    // "font3" 附近使用
    plugin.game.game_addr.fnDictionary_GetElementByKey = injector::aslr_ptr(0x42F180).get();

    // GetStringWidth 使用
    plugin.game.game_addr.fnFont_AddTokenStringWidth = injector::aslr_ptr(0x7F35B0).get();
}
}
