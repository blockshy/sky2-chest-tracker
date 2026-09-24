// Mod 文案的纯测试：验证八语完整、格式参数一致以及语言切换不串用上次结果。
// 不依赖玩家资源、Windows、ImGui 或真实游戏进程，适合公开 CI 直接编译运行。
#include "ui_text.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static std::vector<std::string> FormatArguments(const char* text) {
    std::vector<std::string> arguments;
    for (const char* at = text; *at; ++at) {
        if (*at != '%') continue;
        if (at[1] == '%') { ++at; continue; }
        const char* begin = at++;
        while (*at && !std::strchr("diuoxXfFeEgGaAcspn", *at)) ++at;
        if (!*at) { arguments.emplace_back("invalid"); break; }
        arguments.emplace_back(begin, at + 1);
    }
    return arguments;
}

int main() {
    using namespace tracker;
    unsigned failures = 0;
    const auto check = [&](bool value, const char* message, size_t index) {
        if (!value) { ++failures; std::printf("FAIL [%zu] %s\n", index, message); }
    };
    for (size_t i = 0; i < static_cast<size_t>(UiText::Count); ++i) {
        const auto& entry = kUiTexts[i];
        const char* values[] = {entry.chinese, entry.japanese, entry.english, entry.traditionalChinese,
            entry.german, entry.french, entry.spanish, entry.korean};
        const auto chinese = FormatArguments(entry.chinese);
        for (size_t n = 0; n < 8; ++n) {
            check(values[n] && *values[n], "all eight translations are required", i);
            check(chinese == FormatArguments(values[n]), "format arguments mismatch", i);
            SetDisplayLanguage(static_cast<Language>(n));
            check(std::strcmp(UiString(static_cast<UiText>(i)), values[n]) == 0,
                  "language change must immediately select matching text", i);
        }
    }
    std::printf("UI text: %zu entries, 8 languages, %u failures\n",
        static_cast<size_t>(UiText::Count), failures);
    return failures ? 1 : 0;
}
