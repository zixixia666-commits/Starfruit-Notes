<p align="center">
  <img src="assets/starfruit-notes-icon.png" width="180" alt="Starfruit Notes icon">
</p>

<h1 align="center">Starfruit Notes</h1>

<p align="center">一个轻量、安静、只保存在本机的 Windows 桌面待办便签。</p>

## 特点

- 原生 C++ / Win32 单文件程序，不依赖 Electron 或后台服务。
- 新增、编辑、勾选、删除和拖拽排序任务。
- 次日自动归档；存在未完成任务时询问是否延续。
- 米纸、素白、夜墨三套主题。
- 支持贴在桌面、始终置顶、托盘控制和开机启动。
- 全局快捷键 `Ctrl+Alt+N` 快速显示或隐藏。
- 数据完全保存在本地，保留最近 30 天历史。

## 下载与运行

适用于 Windows 10/11 x64。正式发布后可从 [GitHub Releases](https://github.com/zixixia666-commits/Starfruit-Notes/releases) 下载 `Starfruit Notes.exe`，无需安装即可运行。

> 程序目前未进行代码签名，首次下载时 Windows SmartScreen 可能显示未知发布者提示。

## 使用

- 在输入框中输入任务，按 Enter 添加。
- 点击圆圈完成或取消完成；双击任务进行编辑。
- 拖动任务调整顺序；悬停后点击 `×` 删除。
- 右键托盘图标切换桌面模式、置顶、主题和开机启动。
- 数据目录：`%APPDATA%/Starfruit Notes`。

## 从源码构建

需要 Visual Studio 2022（Desktop development with C++）和 CMake 3.22+。

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

生成文件：`build/Release/Starfruit Notes.exe`

## 项目结构

```text
src/        Win32 应用源码与资源
assets/     应用图标素材
tools/      图标打包工具
```

## 隐私

Starfruit Notes 不联网、不收集遥测数据，也不需要账户。任务内容只写入本机用户目录。
