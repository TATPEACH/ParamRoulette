#pragma once

#include "ParamModel.h"
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
};

enum class MorphMode { OneD = 0, TwoD = 1 };

/** 抓一份快照：全部参数的归一化值（按**参数下标**对齐）+ 子插件完整状态块。
    slot 和「运动模式的基座」共用这一份实现，免得两边各存一套、语义悄悄跑偏。 */
Snapshot captureSnapshot (juce::AudioPluginInstance&, const juce::String& name = {});

/** 精确还原一份快照。有状态块就用状态块（连宿主看不见的内部状态也回来），
    没有就退回逐参数写。 */
bool restoreSnapshot (const Snapshot&, juce::AudioPluginInstance&);

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
    void capture (int slot, juce::AudioPluginInstance& plugin, const juce::String& name = {});
    void clearSlot (int slot);
    void clearAll();

    /** 精确召回：用子插件自己的状态块还原，连宿主看不见的内部状态也一起回来。 */
    bool recall (int slot, juce::AudioPluginInstance& plugin);

    const Snapshot& operator[] (int slot) const  { return slots[(size_t) slot]; }
    bool  isUsed (int slot) const noexcept       { return juce::isPositiveAndBelow (slot, kNumSlots)
                                                          && slots[(size_t) slot].used; }
    int   numUsed() const noexcept;

    //==============================================================================
    /** 算权重。1D 把 x 铺在"已用 slot"的 N-1 段上；2D 走四角双线性，
        空 slot 的权重剔除后对剩下的重新归一化。 */
    Weights computeWeights (MorphMode mode, float x, float y) const;

    /** 按权重写回。只动 checked[i] == true 的参数，返回实际写了多少个。
        必须在消息线程上调用。 */
    int applyMorph (juce::AudioPluginInstance& plugin,
                    const juce::Array<ParamInfo>& model,
                    const juce::Array<bool>& checked,
                    MorphMode mode, float x, float y);

    /** 当前"权重最大者"是哪个 slot（-1 = 没有）。UI 用来高亮。 */
    int  getWinner() const noexcept { return currentWinner; }
    void resetWinner() noexcept     { currentWinner = -1; }

    //==============================================================================
    juce::ValueTree toValueTree() const;
    void            restoreFromValueTree (const juce::ValueTree& v);

private:
    int pickWinner (const Weights& w);

    std::array<Snapshot, kNumSlots> slots;

    // 迟滞用：权重最大者是全局的（只和权重有关，和具体哪个参数无关），
    // 所以一个 int 就够，不需要每参数一份。
    int currentWinner = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SnapshotBank)
};

} // namespace pr
