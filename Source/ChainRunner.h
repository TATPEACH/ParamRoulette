#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "ChainDefs.h"

namespace pr
{

/** 一个链位在**音频线程**上需要的全部东西。

    刻意做成一个只有裸指针的小结构，而不是直接传 HostedSlot：
    这样串联逻辑不依赖 `JucePlugin_*` 宏，能被无头探针链进去测。
    音频线程上出的错（越界、静音、爆音）最难靠听来定位，必须能机器验证。 */
struct ChainLink
{
    juce::AudioPluginInstance* plugin  = nullptr;
    juce::AudioBuffer<float>*  scratch = nullptr;   ///< 声道数不够时的暂存区
    int  channels = 0;                              ///< 这个插件要几个声道
    bool bypassed = false;

    bool isActive() const noexcept { return plugin != nullptr && ! bypassed; }
};

struct ChainRunResult
{
    int ran     = 0;    ///< 实际跑过的链位数
    int skipped = 0;    ///< 因为暂存区不够而跳过的。**正常情况下恒为 0**，
                        ///< 非 0 说明 prepareToPlay 没跑过或者块变大了
};

/** 跑一个链位，就地改写 buffer。返回 false = 暂存区不足、什么都没做。

    声道数不够时走暂存区：子插件要的声道比宿主给的多（典型是把合成器挂进
    效果器版），直接把宿主 buffer 丢过去它会越界读写。 */
bool runLink (const ChainLink&, juce::AudioBuffer<float>&, juce::MidiBuffer&);

/** 按顺序跑整条链，前一个的输出就是后一个的输入。

    **MIDI 用同一个 buffer 串下去**，所以前一个插件的 MIDI 输出会进到后一个——
    这正是真实效果器链的行为（琶音器接合成器、MIDI 触发的门等等都靠这个）。 */
ChainRunResult runChain (const ChainLink* links, int numLinks,
                         juce::AudioBuffer<float>&, juce::MidiBuffer&);

/** 串联的总延迟 = 各段之和。旁通的和空的不算。 */
int chainLatencySamples (const ChainLink* links, int numLinks);

/** 串联的尾巴也是叠加的：前一段的混响尾巴还要再穿过后一段的延迟。 */
double chainTailSeconds (const ChainLink* links, int numLinks);

} // namespace pr
