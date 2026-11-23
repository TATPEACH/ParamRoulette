# VST2 SDK 头文件（第三方，不在本仓库里）

需要两个文件：`pluginterfaces/vst2.x/aeffect.h` 和 `aeffectx.h`，放进这个目录下对应的位置。
**本仓库不包含它们，也不能包含**（见下面的授权）。

## 为什么需要

JUCE 8 的 `juce_VSTPluginFormat.cpp`（第 60-61 行）硬性 include 这两个头文件，
但 JUCE 早就不再随包分发它们了。它自己的注释说：

> If the following files cannot be found then you are probably trying to host
> VST2 plug-ins. To do this you must have a VST2 SDK in your header search paths...
> The VST2 SDK can be obtained from the vstsdk3610_11_06_2018_build_37 (or older)
> VST3 SDK or JUCE version 5.3.2.

没有它们，`JUCE_PLUGINHOST_VST=1` 编不过，也就托管不了任何 VST2（包括 fauna）：

- **64 位构建**：没有也能编，只是只能托管 VST3（CMake 会给一条警告）
- **32 位构建**：必须有，否则直接 FATAL_ERROR——32 位版存在的意义就是加载老的 32 位 VST2

## 去哪儿找

照 JUCE 上面那段注释：Steinberg 2018 年 6 月那一版 VST3 SDK（`vstsdk3610_11_06_2018_build_37`）
或更早的版本里带着这两个头文件，JUCE 5.3.2 里也有。手上有的话，拷过来就行。

核对过：JUCE 8.0.10 在 `juce_VSTPluginFormat.cpp` / `juce_VSTCommon.h` /
`juce_VSTMidiEventList.h` 里一共引用 **206** 个 `Vst2::` 标识符，VST 2.4 SDK 的这两个头文件
**全部覆盖，一个不缺**。

## 授权

这是 Steinberg 的 VST2 SDK。**Steinberg 已于 2018 年 10 月停止发放 VST2 授权**，
所以它不是能拿去分发的东西：放头文件的 `pluginterfaces/` 被 `.gitignore` 挡在版本库外面，
本项目也不发布带 VST2 托管功能的构建产物。
