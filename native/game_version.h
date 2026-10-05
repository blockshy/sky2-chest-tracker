#pragma once

// 本文件是运行时、目录生成器、构建脚本和发行安装器共用的游戏版本权威来源。
// 只有完成地址与结构核验后才能更新当前指纹；生成目录不能自行扩展支持范围。
// Python/PowerShell 按下列单行 constexpr 声明读取，禁止改成宏、拼接或计算表达式。
namespace sky2::game_version {
inline constexpr char kSupportedExeSha256[] = "cab62e5872222efb2aaf272be47f14263db4e7132ad7df5255db8efbee9959ea";
inline constexpr char kSupportedFileVersion[] = "1.4.0.0";
inline constexpr char kSteamBuildId[] = "25721473";

// 上一发行版指纹仅允许安装器确认历史收据和迁移记录的归属，不能据此启用
// 当前 RVA、生成新目录或放行旧游戏。迁移记录必须保持原指纹及原始字节。
inline constexpr char kPreviousExeSha256[] = "d8b2911d1576216bdc22d070550e4f531e105de7ed2981885849669f4acf8aaf";
}
