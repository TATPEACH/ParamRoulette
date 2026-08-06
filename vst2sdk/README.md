# VST2 SDK 头文件（第三方，不是本项目的代码）

只有两个文件：`pluginterfaces/vst2.x/aeffect.h` 和 `aeffectx.h`。

## 为什么在这儿

JUCE 8 的 `juce_VSTPluginFormat.cpp`（第 60-61 行）硬性 include 这两个头文件，
但 JUCE 早就不再随包分发它们了。它自己的注释说：

> If the following files cannot be found then you are probably trying to host
> VST2 plug-ins. To do this you must have a VST2 SDK in your header search paths...
> The VST2 SDK can be obtained from the vstsdk3610_11_06_2018_build_37 (or older)
> VST3 SDK or JUCE version 5.3.2.

没有它们，`JUCE_PLUGINHOST_VST=1` 编不过，也就托管不了任何 VST2（包括 fauna）。

## 出处

`https://github.com/R-Tur/VST_SDK_2.4` → `pluginterfaces/vst2.x/`

下载前核对过：JUCE 8.0.10 在 `juce_VSTPluginFormat.cpp` / `juce_VSTCommon.h` /
`juce_VSTMidiEventList.h` 里一共引用 **206** 个 `Vst2::` 标识符，这两个头文件
**全部覆盖，一个不缺**。

## 授权

Steinberg 的 VST2 SDK。**Steinberg 已于 2018 年 10 月停止发放 VST2 授权**，
所以这不是能拿去分发的东西。本项目自用，不打包、不发布。
如果哪天要分发，这个目录必须先处理掉。
