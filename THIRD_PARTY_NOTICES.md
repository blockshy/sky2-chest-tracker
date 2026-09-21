# 第三方来源

本项目采用 [PolyForm Noncommercial 1.0.0](LICENSE)。下列第三方组件保留各自原有许可和版权声明。

- `tools/formats.py` 的 BJSON 解码结构参考 ED9ModManager 的
  `src/modkit/bjson_decoder.cpp` 和 `.h`，并用 Python 实现严格边界检查。
  Required Notice: Copyright © 2026 lom2333 (https://github.com/lom2333/ED9ModManager)
  相关派生部分采用 [PolyForm Noncommercial 1.0.0](https://polyformproject.org/licenses/noncommercial/1.0.0/)。
- FPAC 格式资料：https://github.com/coinkillerl/FPACker 。
- 原生 DLL 包含 [Dear ImGui](https://github.com/ocornut/imgui)，MIT 许可，
  固定提交 `420f1793417e39560ca39a4209c55a9f204fde13`。
  完整许可和版权声明见 [licenses/Dear-ImGui.txt](licenses/Dear-ImGui.txt)。
- 原生 DLL 包含 [MinHook](https://github.com/TsudaKageyu/minhook)，
  固定提交 `8af6b4acae5a9388fd742b56fa79ece89d96f823`。
  MinHook 及其反汇编器的完整许可和版权声明见 [licenses/MinHook.txt](licenses/MinHook.txt)。
- ED9ModManager 参考版本为 `407178493f7c06f5882a41dda0a72b338f404d09`，
  许可副本见 [licenses/ED9ModManager.txt](licenses/ED9ModManager.txt)。其代码未链接进原生 DLL。
- Dear ImGui 和 MinHook 源码在构建时按锁定提交下载，不在本仓库复制保存；安装包另含 `dist/licenses/` 许可副本。
- 原始游戏资源和用户存档归各自权利人所有，不纳入源码版本控制或公开发布包。
