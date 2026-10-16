#pragma once

#include "ParamModel.h"
#include "ChainDefs.h"
#include <array>
#include <vector>

namespace pr
{

inline constexpr int kNumSlots = 4;

/** 2D pad 的四角对应 slot 0..3，摆位是：
        0 ── 1
        │    │
        2 ── 3
    y 向下增长（与屏幕坐标一致）。
*/
struct Snapshot
{
    bool               used = false;
    juce::String       name;
    std::vector<float> values;      ///< 全部参数的归一化值，按参数下标对齐
    juce::MemoryBlock  childState;  ///< 子插件 getStateInformation，用于精确召回

    /** 这份快照是从**哪个插件**抓的（PluginDescription::createIdentifierString）。

        为什么必须记：`childState` 是一团只有那个插件认得的字节。喂给别的插件
        轻则被忽略，重则崩溃或者污染它的内部状态。单插件时代不可能搞混，
        但现在有六个链位，"在链位 0 存的快照、切到链位 1 召回"是一步之遥的误操作。

        空 = 老工程存的快照（那时没记这个字段），此时不做校验——
        对老工程来说链上只有一位，混不起来。 */
    juce::String pluginId;
};

enum class MorphMode { OneD = 0, TwoD = 1 };

/** 整条链的一份快照 —— 每个链位一份 `Snapshot`。

    为什么是整条链而不是单个插件：一个"音色"在链上就是"这几个插件各自的状态"。
    只存聚焦那一位的话，用户存了两份快照去 morph，听到的是链上一位在动、
    其余不动 —— 那不是他以为存下来的那个声音。

    空的链位留着空 `Snapshot`（`used == false`），morph 时自然被跳过，
    所以"只装了一个插件"的情况完全退化成以前的行为。 */
struct ChainSnapshot
{
    std::array<Snapshot, kMaxChain> perSlot;
    juce::String name;

    bool anyUsed() const noexcept
    {
        for (const auto& s : perSlot)
            if (s.used)
                return true;
        return false;
    }
};

/** 快照要操作的东西，每链位一份。

    刻意**不**直接传 `HostedSlot`：那个结构已经 include 了本文件，
    反向依赖会成环。而且这样也说清了 SnapshotBank 到底需要什么——
    只要插件指针、参数模型、勾选表三样。 */
struct SnapTarget
{
    juce::AudioPluginInstance*    plugin  = nullptr;
    const juce::Array<ParamInfo>* model   = nullptr;
    const juce::Array<bool>*      checked = nullptr;
};

/** 抓一份快照：全部参数的归一化值（按**参数下标**对齐）+ 子插件完整状态块。
    slot 和「运动模式的基座」共用这一份实现，免得两边各存一套、语义悄悄跑偏。 */
Snapshot captureSnapshot (juce::AudioPluginInstance&, const juce::String& name = {});

/** 精确还原一份快照。有状态块就用状态块（连宿主看不见的内部状态也回来），
    没有就退回逐参数写。

    **插件对不上就什么都不做并返回 false**：状态块是插件私有格式，喂错了
    轻则被忽略、重则崩溃。见 `Snapshot::pluginId`。 */
bool restoreSnapshot (const Snapshot&, juce::AudioPluginInstance&);

/** 这份快照能不能安全地还原进这个插件？ */
bool snapshotMatchesPlugin (const Snapshot&, const juce::AudioPluginInstance&);

/** 快照 ⇄ ValueTree。slot 和运动模式的基座共用同一份格式，
    所以这两个是自由函数而不是 SnapshotBank 的成员。 */
juce::ValueTree snapshotToValueTree (const Snapshot&, const juce::Identifier& type);
Snapshot        snapshotFromValueTree (const juce::ValueTree&);

/** morph 权重。最多 4 项，不分配堆内存。 */
struct Weights
{
    struct Entry { int slot = -1; float w = 0.0f; };

    std::array<Entry, kNumSlots> e {};
    int n = 0;

    void add (int slot, float w) noexcept
    {
        if (n < kNumSlots) { e[(size_t) n] = { slot, w }; ++n; }
    }

    bool  isEmpty() const noexcept { return n == 0; }
    float weightOf (int slot) const noexcept
    {
        for (int i = 0; i < n; ++i)
            if (e[(size_t) i].slot == slot)
                return e[(size_t) i].w;
        return 0.0f;
    }
};

class SnapshotBank
{
public:
    SnapshotBank() = default;

    //==============================================================================
    // 快照现在覆盖**整条链**。targets 按链位下标对齐，空位传默认值即可。

    void capture (int snapSlot, const SnapTarget* targets, int numTargets,
                  const juce::String& name = {});
    void clearSlot (int snapSlot);
    void clearAll();

    /** 链上的插件挪了位置（见 `moveChainPosition`），每份快照里那一位的状态跟着挪。
        不挪的话召回时"链位 3 的状态"会去找现在在链位 3 的另一个插件——
        身份校验会挡住不喂错，但那个插件就再也召回不出来了。 */
    void moveChainPosition (int from, int to);

    /** 精确召回：每链位用它自己的状态块还原，连宿主看不见的内部状态也一起回来。
        返回实际还原了几个链位。 */
    int recall (int snapSlot, const SnapTarget* targets, int numTargets);

    const ChainSnapshot& operator[] (int snapSlot) const { return slots[(size_t) snapSlot]; }
    bool  isUsed (int snapSlot) const noexcept   { return juce::isPositiveAndBelow (snapSlot, kNumSlots)
                                                          && slots[(size_t) snapSlot].anyUsed(); }
    int   numUsed() const noexcept;

    //==============================================================================
    /** 算权重。1D 把 x 铺在"已用快照"的 N-1 段上；2D 走四角双线性，
        空快照的权重剔除后对剩下的重新归一化。 */
    Weights computeWeights (MorphMode mode, float x, float y) const;

    /** 按权重写回**整条链**。只动 checked[i] == true 的参数，
        返回实际写了多少个。必须在消息线程上调用。 */
    int applyMorph (const SnapTarget* targets, int numTargets,
                    MorphMode mode, float x, float y);

    /** 当前"权重最大者"是哪个快照（-1 = 没有）。UI 用来高亮。 */
    int  getWinner() const noexcept { return currentWinner; }
    void resetWinner() noexcept     { currentWinner = -1; }

    //==============================================================================
    juce::ValueTree toValueTree() const;
    void            restoreFromValueTree (const juce::ValueTree& v);

private:
    int pickWinner (const Weights& w);

    /** 给一个链位做插值。拆出来是因为 applyMorph 现在要在 6 个链位上各做一遍，
        原来那一大坨内联着看不清是"每链位"还是"每快照"的循环。 */
    int morphOneSlot (int chainSlot, const SnapTarget&, const Weights&, int winner);

    std::array<ChainSnapshot, kNumSlots> slots;

    // 迟滞用：权重最大者是全局的（只和权重有关，和具体哪个参数无关），
    // 所以一个 int 就够，不需要每参数一份。
    int currentWinner = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SnapshotBank)
};

} // namespace pr
