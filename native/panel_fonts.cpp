// 独立窗口使用玩家本机 Windows 字体，真实页面截图复用同一字体加载路径。
// 不复制或分发系统/游戏字体，八语覆盖检查仍使用原生名称和全部内置文案。
#include "panel_fonts.h"
#include "tracker.h"
#include "ui_text.h"
#include "localized_names.h"
#include <array>
namespace tracker {
static std::array<bool, kLanguageCount> g_languageFontComplete{};
// 根据当前合并字体的 cmap 检查各语言实际会用到的文字；不修改全局语言，
// 不栅格化整个字库，不提前上传几千个字的纹理。相同码点只查询一次字体源。
// 检查 Mod 文案与原生资源专名，避免日文字体能显示“地图”却缺少简中专名时误报完整。
static void CheckPanelFontCoverage(ImFont* font) {
    std::array<uint8_t, 0x10000> glyphCache{}; // 0 未检查；1 存在；2 缺失。
    const char* names[] = {"Simplified Chinese", "Japanese", "English", "Traditional Chinese",
                           "German", "French", "Spanish", "Korean"};
    for (unsigned language = 0; language < kLanguageCount; ++language) {
        bool complete = font != nullptr;
        unsigned firstMissing = 0;
        const auto checkText = [&](const char* text) {
            if (!text || !*text || !font) return;
            const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, nullptr, 0);
            if (!count) { complete = false; return; }
            std::wstring wide(static_cast<size_t>(count), L'\0');
            if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, wide.data(), count)) {
                complete = false;
                return;
            }
            for (const wchar_t character : wide) {
                const auto codepoint = static_cast<unsigned>(character);
                if (codepoint < 0x20) continue; // 换行、制表符、终止符不需要可见字形。
                auto& cached = glyphCache[codepoint];
                if (!cached) cached = font->IsGlyphInFont(static_cast<ImWchar>(codepoint)) ? 1 : 2;
                if (cached == 2) { complete = false; if (!firstMissing) firstMissing = codepoint; }
            }
        };
        for (const auto& entry : kUiTexts) {
            const char* texts[] = {entry.chinese, entry.japanese, entry.english, entry.traditionalChinese,
                entry.german, entry.french, entry.spanish, entry.korean};
            checkText(texts[language]);
        }
#if SKY2_HAS_MAP_LOCALIZATION
        for (const auto& map : kLocalizedMaps) checkText(map.paths[language]);
#endif
#if SKY2_HAS_TRAVEL_LOCALIZATION
        for (const auto& target : kLocalizedTravel) {
            checkText(target.names[language]);
            checkText(target.groups[language]);
        }
        for (const auto& region : kLocalizedRegions) checkText(region.names[language]);
#endif
        g_languageFontComplete[language] = complete;
        if (!complete) {
            char diagnostic[256]{};
            std::snprintf(diagnostic, sizeof(diagnostic),
                "Font coverage incomplete for %s (first missing U+%04X). Install matching Windows supplemental fonts.",
                names[language], firstMissing);
            Log(diagnostic);
        }
    }
}

// 只使用玩家机器已安装的字体，不随 Mod 打包或分发 Windows 字体。
// 所有语言合并到同一字库：跟随游戏切换语言时无需重建纹理，也不会重置列表位置。
// 中文、日文、韩文字体分别择优加载一份，避免把所有候选字体同时驻留内存。
void LoadStandaloneFonts(ImGuiIO& io) {
    g_languageFontComplete.fill(false);
    wchar_t windows[MAX_PATH]{};
    GetWindowsDirectoryW(windows, MAX_PATH);
    const std::wstring folder = std::wstring(windows) + L"\\Fonts\\";
    static const ImWchar glyphs[] = {
        0x0020, 0x024f, 0x1100, 0x11ff, 0x2000, 0x26ff, 0x3000, 0x318f,
        0x31f0, 0x31ff, 0x3400, 0x9fff, 0xac00, 0xd7af, 0xff00, 0xffef, 0
    };
    bool loaded = false;
    const auto load = [&](const wchar_t* filename) {
        const auto path = folder + filename;
        const DWORD attributes = GetFileAttributesW(path.c_str());
        // AddFontFromFileTTF 对不存在的路径会触发断言，必须先检查再交给 ImGui。
        if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY)) return false;
        char utf8[MAX_PATH * 3]{};
        if (!WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, utf8, sizeof(utf8), nullptr, nullptr)) return false;
        ImFontConfig config;
        config.MergeMode = loaded;
        if (!io.Fonts->AddFontFromFileTTF(utf8, 20.0f, &config, glyphs)) return false;
        loaded = true;
        return true;
    };
    // Segoe UI 是英文 Windows 的可靠基础字体；缺失时仍保留 ImGui 的内置拉丁字形。
    if (!load(L"segoeui.ttf")) {
        ImFontConfig config;
        config.SizePixels = 20.0f;
        io.Fonts->AddFontDefault(&config);
        loaded = true;
    }
    // 英文原生地点同样包含 ① 等圈号，常规 Segoe UI 不提供这些字符。
    // 单独合并 Windows 的符号字体，使英文系统无需为了这些名称安装中日韩字体。
    load(L"seguisym.ttf");
    bool chinese = false, japanese = false, korean = false;
    for (const auto* name : {L"msyh.ttc", L"msyh.ttf", L"simhei.ttf", L"simsun.ttc", L"Deng.ttf", L"msjh.ttc", L"mingliu.ttc", L"NotoSansSC-Regular.ttf"})
        if (load(name)) { chinese = true; break; }
    for (const auto* name : {L"YuGothM.ttc", L"YuGothR.ttc", L"meiryo.ttc", L"msgothic.ttc", L"NotoSansJP-Regular.ttf"})
        if (load(name)) { japanese = true; break; }
    // 韩文有独立音节区，不能把“有汉字字体”误判为可显示韩文。
    // 即使中日字体已存在也单独补入，确保游戏运行时切到韩文无需重新初始化面板。
    for (const auto* name : {L"malgun.ttf", L"gulim.ttc", L"batang.ttc", L"NotoSansKR-Regular.ttf"})
        if (load(name)) { korean = true; break; }
    // 部分非中日 Windows 只装了其他 CJK 字库；它们仅作最终兜底，不优先改变字形。
    if (!chinese || !japanese || !korean)
        for (const auto* name : {L"NotoSansCJK-Regular.ttc", L"arialuni.ttf"})
            if (load(name)) break;
    // 文件名仅用于候选选择，语言可显示性最终由实际字形判定。
    CheckPanelFontCoverage(io.Fonts->Fonts.Size ? io.Fonts->Fonts[0] : nullptr);
}

}
