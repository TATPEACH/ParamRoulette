#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Utf8.h"

namespace pr
{

/** 参数分三类，随机化语义各不相同。从 ReaScript 版逐条移过来的：
    - Toggle     开关，按强度当概率翻转
    - Stepped    有限档位，随机后吸附到合法档位
    - Continuous 连续，在归一化空间插值
*/
enum class Kind
{
    Toggle,
    Stepped,
    Continuous
};

juce::String kindLabel (Kind k);

/** 为什么这个参数默认没被勾上。

    以前这是个 bool，结果就是"这一行怎么都随机不到，也看不出原因"。
    列出理由是有代价的（多一列文字），但"静默排除"是这类工具最容易积累的
    信任债——用户会以为随机化坏了，而不是以为自己没勾。
*/
enum class SkipReason
{
    None,
    Blob,               ///< 值文本看着像数据块，不是给人看的
    NotAutomatable,     ///< 插件自己声明"宿主别动我"（VST3 的 kCanAutomate 没置位）
    MetaParameter,      ///< 宏 / 预设选择器，动一下会连带改一片
    NameRule            ///< 命中 wet/dry/bypass/... 这类默认排除名单
};

juce::String skipReasonLabel (SkipReason r);

/** 体检结论。只有跑过 InertProbe 才会从 Unknown 变。

    `EndpointOnly` 是最要紧的一类，也是最难自己发现的：参数对宿主报的是连续量程
    （拿不到档位信息），但插件内部只认两个精确端点，写任何中间值都被原地弹回。
    fauna 的 `eg*.inv`（±10）就是这样——强度拉满也一动不动，因为随机化写的
    永远是中间值。体检认出这一类之后会把它的 kind 改写成 Toggle，
    随机化就只写 0.0 / 1.0 两个端点了。
*/
enum class Health
{
    Unknown,
    Ok,                 ///< 写进去能改变读回值
    EndpointOnly,       ///< 只收 0.0 / 1.0 精确端点，中间值被弹回（"伪二值"）
    Inert               ///< 写什么都被弹回原值：这个参数对宿主是只读的
};

juce::String healthLabel (Health h);

struct ParamInfo
{
    int          index = 0;             ///< 在 AudioProcessor::getParameters() 里的下标
    juce::String name;
    juce::String lowerName;             ///< 预先小写，filter 每帧要用
    Kind         kind = Kind::Continuous;
    int          numSteps = 0;          ///< Toggle 恒为 2；Stepped 为实际档数；Continuous 为 0
    bool         blob = false;          ///< 值看着像数据块而不是可读文本
    SkipReason   skipReason = SkipReason::None;
    juce::String display;               ///< 当前值文本，刷新时更新
    float        value = 0.0f;          ///< 当前归一化值，刷新时更新
    Health       health = Health::Unknown;

    /** 这个参数**不能插值**。

        枚举型选择器（调制目标 / 波形 / 模式）在归一化空间里是一串离散码，
        取两个值的平均得到的是**第三个毫无关系的选项**。fauna 把 9 个
        `send*.dest` 全报成连续量，morph 一插值就同时把九条调制线接到随机目标上，
        听起来就是凭空多出好几层、而且更响。

        randomize **不受这个影响**——随机挑一个目标是合理的；只有 morph 要守。 */
    bool noInterpolate = false;

    /** 音色层抖动的**逐参数幅度上限**（归一化）。负数 = 用角色默认档。

        为什么需要它：角色默认只有两档（音高 ±0.06 / 音色 ±0.20），线性缩放。
        而 codex 在 bird 5 上试听得出的建议是逐参数的——`tens ±0.02~0.04`、
        `lp.f ±0.015~0.03`、`mem x pos ±0.04~0.08`。想同时满足这些，
        全局那一个"音色强度"滑条做不到：调小了 `mem` 不够动，调大了 `tens` 就过了。

        **只影响音色 / 抖动层，不影响普通随机化。** 普通随机化的语义是
        "在 [lo,hi] 里取目标"，和"以基座为中心 ± span"是两回事；混在一起会
        静默改掉现有行为。codex 那张表本来就是"同一只鸟的变体"，正是音色层。 */
    float span = -1.0f;

    bool hasCustomSpan() const noexcept { return span >= 0.0f; }

    // —— 上一次随机化 / 生成变体之前的值，用来在界面上显示"旧 → 新"
    float        prevValue = 0.0f;
    juce::String prevDisplay;
    bool         changed = false;       ///< 上一次操作是否真的改动了它

    bool defaultSkip() const noexcept   { return skipReason != SkipReason::None; }
};

/** 名字看着像枚举选择器吗？（调制目标 / 源 / 波形 / 模式 / 同步开关） */
bool looksLikeSelector (const juce::String& lowerName);

/** 把当前值记进 prevValue/prevDisplay，并清掉 changed。
    随机化 / 生成变体**之前**调。 */
void snapshotPrevValues (juce::Array<ParamInfo>& model);

/** 比对当前值和 prevValue，设 changed。返回改动了几个。
    刷新完值**之后**调。 */
int markChangedValues (juce::Array<ParamInfo>& model);

/** 值文本看着像二进制数据块吗？

    移植自 Lua 版的 looks_like_binary。有些插件把内部状态塞进一个参数里当数据槽，
    随机化它等于往插件肚子里灌垃圾。启发式：出现控制字符，或者串够长且非 ASCII 占比 ≥40%。

    与 Lua 版的一处差异：juce::String 内部保证是合法 UTF-8（构造时就会清洗），
    所以"非法 UTF-8 编码"那条分支在这里没有对应物，只剩控制字符和非 ASCII 占比两条。
*/
bool looksLikeBinary (const juce::String& s);

/** 参数分类。

    注意这里**不能**用 `p.isBoolean()` 判开关。已核实（JUCE 8.0.10）：
    `AudioProcessorParameter::isBoolean()` 基类返回 false，而 `VST3Parameter`
    从不覆写它——所以托管 VST3 时它恒为 false，靠它判开关等于这条分支永远走不到。
    VST3 的开关表现为 `stepCount == 1`，经 JUCE 换算成 `getNumSteps() == 2`，
    所以这里以档数为准。isBoolean() 仍然留着，是为了 JUCE 原生 / 未来别的格式。
*/
Kind classify (const juce::AudioProcessorParameter& p);

/** 这个参数首次扫描时该不该默认排除，以及为什么。 */
SkipReason defaultSkipReason (const juce::AudioProcessorParameter& p,
                              const juce::String& lowerName,
                              bool blob);

/** 扫描整个插件的参数表。 */
juce::Array<ParamInfo> buildModel (juce::AudioPluginInstance& plugin);

/** 只刷新 value / display，不重建结构。 */
void refreshValues (juce::AudioPluginInstance& plugin, juce::Array<ParamInfo>& model);

} // namespace pr
