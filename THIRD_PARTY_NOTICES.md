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
- Dear ImGui 和 MinHook 源码在构建时按锁定提交下载，不在本仓库复制保存；安装包将项目许可、本声明及 Dear ImGui、MinHook（含 HDE）、ED9ModManager 的完整许可合并为 `LICENSES.txt`，保留全部 Required Notices 和版权声明。独立版安装后位于 `Sky2ChestTracker/LICENSES.txt`，ASI 版位于 `plugins/Sky2ChestTracker/LICENSES.txt`。
- 原始游戏资源和用户存档归各自权利人所有，不纳入源码版本控制或公开发布包。
- ASI 分发使用 ThirteenAG 的 [Ultimate ASI Loader v9.7.4](https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases/tag/v9.7.4)，MIT 许可。它在单独的 Loader 包中原样分发，不链接进宝箱 DLL；仅将官方 x64 压缩包内的 `dinput8.dll` 改名为本游戏使用的 `xinput1_4.dll`。源码仓库的 `loader-dependency.json` 固定版本、上游提交与 SHA-256。[官方许可](https://github.com/ThirteenAG/Ultimate-ASI-Loader/blob/v9.7.4/license) 随 Loader 包安装到 `plugins/Sky2ChestTracker/UltimateASILoader.LICENSE.txt`，仓库另存有 `licenses/Ultimate-ASI-Loader.txt` 副本。本项目的非商业许可不替换或限制 Loader 原有的 MIT 许可。

Loader 许可由 Loader 安装／卸载流程独立管理；移除宝箱 ASI 时仍保留该文件，直到公共 Loader 也被移除。游戏目录不安装普通文档及安装收据。
