# ParamRoulette

一个 **VST 宿主插件**：自己加载另一个插件，然后随机化那个插件的参数。

除了逐参数随机化，还有两件普通随机化器做不到的事：

- **快照插值（morph）** —— 存 4 个喜欢的状态，用 1D 滑条或 2D 平面在它们之间连续过渡
- **运动语义** —— 按「上扬 / 下降 / 先扬后抑」这种音乐意图生成变体，
  而不是逐参数掷骰子。包括改写插件的**包络轮廓**（对能认出格式的插件）

前身是一个只能在 REAPER 里跑的 ReaScript；这个版本是 JUCE 写的宿主插件，任何 DAW 都能用。

---

## 文档

| 文档 | 内容 |
|---|---|
| **[MANUAL.md](MANUAL.md)** | 使用手册。界面逐块说明、运动语义怎么用、fauna 实战配置、常见问题 |
| **[VERIFY.md](VERIFY.md)** | 验证记录。每个功能的实测数据、每个 bug 的根因和证据、验收清单 |

---

## 构建

需要 CMake ≥ 3.22 和一个 C++17 编译器。JUCE 8.0.10 由 CMake 自动拉取
（**版本号钉死**——它是最后一个自带 VST3 SDK 的 tag）。

### 64 位（现代 VST3 / 64 位 VST2）

```bash
cmake -B build -A x64
cmake --build build --config Release --parallel
```

### 32 位（老 VST2，比如 fauna）

**先补 VST2 SDK 头文件**，见 [vst2sdk/README.md](vst2sdk/README.md)——
它不在版本库里，缺了会 FATAL_ERROR 并打印出需要的路径。

```bash
cmake -B build32 -A Win32
cmake --build build32 --config Release --parallel
```

### 产物

```
build/ParamRoulette_artefacts/Release/VST3/ParamRoulette.vst3
build/ParamRouletteSynth_artefacts/Release/VST3/ParamRoulette Synth.vst3
build32/ParamRoulette_artefacts/Release/VST3/ParamRoulette x86.vst3
build32/ParamRouletteSynth_artefacts/Release/VST3/ParamRoulette Synth x86.vst3
```

每个 target 还会出一个同名 Standalone `.exe`——**不用装、不经过桥、双击就开**，
验证想法最快的路子。

### 为什么是两套独立插件

Windows 的加载器不允许 64 位进程加载 32 位 DLL，反之亦然。所以 32 位版和 64 位版
是**两个不同的插件**（名字 / PLUGIN_CODE / CID 都不同），不是同一个插件的两个架构。
故意的：合成一个 bundle 的话 DAW 会优先挑原生的 64 位那份，就永远走不到能加载
32 位插件的那条路。

在 64 位 REAPER 里，32 位版会被 REAPER 自动扔进它自带的 `reaper_host32.exe`
桥接进程运行，我们的代码在那个进程里跑，加载 32 位子插件就是同架构的普通加载。

---

## 源码地图

| 文件 | 职责 |
|---|---|
| `PluginProcessor` | 宿主本体。托管子插件、透传音频/MIDI、暴露 morph 参数、状态存取 |
| `PluginEditor` | 主界面装配 |
| `ParamModel` | 参数分类（开关 / 档位 / 连续）、blob 检测、默认排除规则 |
| `Randomizer` | 三类随机化语义 + 异步三值体检（`InertProbe`） |
| `SnapshotBank` | 4 个快照 slot + 1D/2D 权重 + 按类插值 |
| `MotionEngine` | 运动语义：角色发现、锚点交叉淡入、分层强度 |
| `ContourIO` | 通过插件状态块改写包络轮廓（CK Envelope 格式） |
| `ParamListView` | 参数列表 + 拖动涂抹勾选 + 筛选 |
| `MorphPad` | 1D 滑条 / 2D 平面 morph 控件 |
| `PluginScanner` | VST3 + VST2 扫描、插件表持久化、每插件的勾选记忆 |
| `HostedPluginWindow` | 子插件界面的浮动窗口 |
| `Utf8.h` | `_u8` 字面量后缀。**中文字面量必须带它**，见下 |

### 无头验收测试

```bash
cmake --build build32 --target pr_probe --config Release --parallel
"./build32/pr_probe_artefacts/Release/pr_probe.exe" > probe_out.txt
```

[Tests/ProbeFauna.cpp](Tests/ProbeFauna.cpp) 会**真加载 fauna**，跑 12 组检查：
加载、参数分类、随机化、三值体检、低强度翻转率、运动引擎、轮廓读写往返、
漂移测试、UTF-8 回归、program 列表、不可插值参数。不开界面、不开音频设备、不出声。

---

## 给以后改代码的人

**中文字面量必须带 `_u8` 后缀。** `juce::String(const char*)` 走的是
`CharPointer_ASCII`，每个字节当一个码点（Latin-1 语义），不加后缀界面上就是一片
`æ` 和方框。JUCE 自己在 `juce_String.cpp:307` 的注释里写了这件事，但那是个
`jassert`——**Release 构建里被编掉，完全静默**。

```cpp
lStatus.setText ("未加载插件"_u8, juce::dontSendNotification);
```

`/utf-8` 编译选项解决不了：它保证字面量里躺着正确的 UTF-8 字节，
而 JUCE 怎么解释这些字节是 JUCE 的事。

---

## 许可

JUCE 8 是 GPLv3-或-商业双授权，本项目按 GPLv3 使用。

`vst2sdk/` 那两个头文件是 Steinberg 的 VST2 SDK，**不在版本库里**，
也不随本项目分发（Steinberg 2018 年 10 月起停发 VST2 授权）。
