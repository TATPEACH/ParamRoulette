#include "SnapshotBank.h"

namespace pr
{

namespace ids
{
    static const juce::Identifier SNAPSHOTS { "SNAPSHOTS" };
    static const juce::Identifier SLOT      { "SLOT" };
    static const juce::Identifier used      { "used" };
    static const juce::Identifier name      { "name" };
    static const juce::Identifier values    { "values" };
    static const juce::Identifier child     { "child" };
}

// 权重最大者换人的门槛。没有这个迟滞，在 pad 对角线上来回微动会让
// 开关类参数疯狂抖动——听感上就是持续的爆音。
static constexpr float kHysteresis = 0.02f;

// 小于这个变化就不推给插件。60Hz × 300 参数 = 18k 次 setValue/秒，扛不住。
static constexpr float kWriteEpsilon = 1.0e-4f;

//==============================================================================
Snapshot captureSnapshot (juce::AudioPluginInstance& plugin, const juce::String& name)
{
    Snapshot s;
    const auto& params = plugin.getParameters();

    s.values.reserve ((size_t) params.size());
    for (auto* p : params)
        s.values.push_back (p != nullptr ? p->getValue() : 0.0f);

    plugin.getStateInformation (s.childState);

    s.name = name;
    s.used = true;
    return s;
}

bool restoreSnapshot (const Snapshot& s, juce::AudioPluginInstance& plugin)
{
    if (! s.used)
        return false;

    if (s.childState.getSize() > 0)
    {
        plugin.setStateInformation (s.childState.getData(), (int) s.childState.getSize());
        return true;
    }

    // 没存下状态块（有的插件 getStateInformation 给空），退回逐参数写
    const auto& params = plugin.getParameters();
    for (int i = 0; i < params.size() && (size_t) i < s.values.size(); ++i)
        if (auto* p = params[i])
            p->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, s.values[(size_t) i]));

    return true;
}

//==============================================================================
void SnapshotBank::capture (int slot, juce::AudioPluginInstance& plugin, const juce::String& name)
{
    if (! juce::isPositiveAndBelow (slot, kNumSlots))
        return;

    slots[(size_t) slot] = captureSnapshot (plugin,
                                            name.isNotEmpty() ? name
                                                              : ("Slot " + juce::String (slot + 1)));
    currentWinner = -1;
}

void SnapshotBank::clearSlot (int slot)
{
    if (! juce::isPositiveAndBelow (slot, kNumSlots))
        return;

    slots[(size_t) slot] = Snapshot {};
    if (currentWinner == slot)
        currentWinner = -1;
}

void SnapshotBank::clearAll()
{
    for (auto& s : slots)
        s = Snapshot {};
    currentWinner = -1;
}

bool SnapshotBank::recall (int slot, juce::AudioPluginInstance& plugin)
{
    if (! isUsed (slot))
        return false;

    if (! restoreSnapshot (slots[(size_t) slot], plugin))
        return false;

    currentWinner = slot;
    return true;
}

int SnapshotBank::numUsed() const noexcept
{
    int n = 0;
    for (auto& s : slots)
        if (s.used)
            ++n;
    return n;
}

//==============================================================================
Weights SnapshotBank::computeWeights (MorphMode mode, float x, float y) const
{
    Weights w;

    int used[kNumSlots];
    int nUsed = 0;
    for (int i = 0; i < kNumSlots; ++i)
        if (slots[(size_t) i].used)
            used[nUsed++] = i;

    if (nUsed == 0)
        return w;
    if (nUsed == 1)
    {
        w.add (used[0], 1.0f);
        return w;
    }

    x = juce::jlimit (0.0f, 1.0f, x);
    y = juce::jlimit (0.0f, 1.0f, y);

    if (mode == MorphMode::OneD)
    {
        // 已用的 slot 按序号排成一条线，x 铺在 N-1 段上。
        // N=2 时正好是"左端 slot1、右端 slot2、中点是两者中间值"。
        const float pos = x * (float) (nUsed - 1);
        const int   a   = juce::jlimit (0, nUsed - 2, (int) std::floor (pos));
        const float f   = juce::jlimit (0.0f, 1.0f, pos - (float) a);

        if (f < 1.0f) w.add (used[a],     1.0f - f);
        if (f > 0.0f) w.add (used[a + 1], f);
        return w;
    }

    // 2D：四角双线性。角位固定绑 slot 0..3（左上 右上 左下 右下）。
    const float raw[kNumSlots] = {
        (1.0f - x) * (1.0f - y),    // slot 0 左上
        x          * (1.0f - y),    // slot 1 右上
        (1.0f - x) * y,             // slot 2 左下
        x          * y              // slot 3 右下
    };

    float sum = 0.0f;
    for (int i = 0; i < kNumSlots; ++i)
        if (slots[(size_t) i].used)
            sum += raw[i];

    if (sum <= 1.0e-9f)
    {
        // 光标正好落在某个空角上，已用 slot 的权重全是 0。
        // 退回"raw 最大的那个已用 slot"，至少还是连续的。
        int best = used[0];
        for (int i = 1; i < nUsed; ++i)
            if (raw[used[i]] > raw[best])
                best = used[i];
        w.add (best, 1.0f);
        return w;
    }

    for (int i = 0; i < kNumSlots; ++i)
        if (slots[(size_t) i].used && raw[i] > 0.0f)
            w.add (i, raw[i] / sum);

    return w;
}

int SnapshotBank::pickWinner (const Weights& w)
{
    if (w.isEmpty())
        return -1;

    int   best  = w.e[0].slot;
    float bestW = w.e[0].w;
    for (int i = 1; i < w.n; ++i)
        if (w.e[(size_t) i].w > bestW)
        {
            bestW = w.e[(size_t) i].w;
            best  = w.e[(size_t) i].slot;
        }

    if (currentWinner < 0 || ! isUsed (currentWinner))
    {
        currentWinner = best;
        return best;
    }

    if (best == currentWinner)
        return best;

    // 迟滞：挑战者得比在位者高出 kHysteresis 才换人
    if (bestW > w.weightOf (currentWinner) + kHysteresis)
        currentWinner = best;

    return currentWinner;
}

int SnapshotBank::applyMorph (juce::AudioPluginInstance& plugin,
                              const juce::Array<ParamInfo>& model,
                              const juce::Array<bool>& checked,
                              MorphMode mode, float x, float y)
{
    const auto w = computeWeights (mode, x, y);
    if (w.isEmpty())
        return 0;

    const int winner = pickWinner (w);
    if (winner < 0)
        return 0;

    const auto& params    = plugin.getParameters();
    const auto  numParams = (size_t) params.size();
    int written = 0;

    for (int k = 0; k < model.size(); ++k)
    {
        if (k >= checked.size() || ! checked.getUnchecked (k))
            continue;                                   // filter = 改动范围，morph 也守这条

        const auto& info = model.getReference (k);
        if (! juce::isPositiveAndBelow (info.index, params.size()))
            continue;

        auto* p = params[info.index];
        if (p == nullptr)
            continue;

        const auto idx = (size_t) info.index;
        float v = 0.0f;

        // noInterpolate 覆盖 Kind 的判断：fauna 把 9 个 send*.dest 全报成连续量，
        // 按连续量插值等于同时把九条调制线接到随机目标上——听起来是凭空多出几层，
        // 而且更响。见 ParamInfo::noInterpolate。
        if (info.kind == Kind::Continuous && ! info.noInterpolate)
        {
            float acc = 0.0f, sum = 0.0f;
            for (int i = 0; i < w.n; ++i)
            {
                const auto& s = slots[(size_t) w.e[(size_t) i].slot];
                if (s.values.size() != numParams || idx >= s.values.size())
                    continue;                           // 快照是另一个插件的，跳过
                acc += s.values[idx] * w.e[(size_t) i].w;
                sum += w.e[(size_t) i].w;
            }
            if (sum <= 0.0f)
                continue;
            v = acc / sum;
        }
        else
        {
            // toggle / stepped 没有"中间态"，权重最大的 slot 说了算
            const auto& s = slots[(size_t) winner];
            if (s.values.size() != numParams || idx >= s.values.size())
                continue;
            v = s.values[idx];
        }

        v = juce::jlimit (0.0f, 1.0f, v);
        if (std::abs (v - p->getValue()) > kWriteEpsilon)
        {
            p->setValueNotifyingHost (v);
            ++written;
        }
    }

    return written;
}

//==============================================================================
juce::ValueTree snapshotToValueTree (const Snapshot& s, const juce::Identifier& type)
{
    juce::ValueTree node (type);
    node.setProperty (ids::used, s.used, nullptr);
    node.setProperty (ids::name, s.name, nullptr);

    if (! s.used)
        return node;

    if (! s.values.empty())
        node.setProperty (ids::values,
                          juce::var (juce::MemoryBlock (s.values.data(),
                                                       s.values.size() * sizeof (float))),
                          nullptr);

    if (s.childState.getSize() > 0)
        node.setProperty (ids::child, juce::var (s.childState), nullptr);

    return node;
}

Snapshot snapshotFromValueTree (const juce::ValueTree& node)
{
    Snapshot s;
    if (! node.isValid())
        return s;

    s.used = (bool) node.getProperty (ids::used, false);
    s.name = node.getProperty (ids::name, juce::String()).toString();

    if (const auto* mb = node.getProperty (ids::values).getBinaryData())
    {
        const auto n = mb->getSize() / sizeof (float);
        s.values.assign (n, 0.0f);
        if (n > 0)
            std::memcpy (s.values.data(), mb->getData(), n * sizeof (float));
    }

    if (const auto* mb = node.getProperty (ids::child).getBinaryData())
        s.childState = *mb;

    // 两样都空就当没存过——否则会拿一份空快照去"精确还原"，把插件清成默认值
    if (s.values.empty() && s.childState.getSize() == 0)
        s.used = false;

    return s;
}

//==============================================================================
juce::ValueTree SnapshotBank::toValueTree() const
{
    juce::ValueTree root (ids::SNAPSHOTS);

    for (int i = 0; i < kNumSlots; ++i)
        root.appendChild (snapshotToValueTree (slots[(size_t) i], ids::SLOT), nullptr);

    return root;
}

void SnapshotBank::restoreFromValueTree (const juce::ValueTree& v)
{
    clearAll();

    if (! v.hasType (ids::SNAPSHOTS))
        return;

    for (int i = 0; i < juce::jmin (kNumSlots, v.getNumChildren()); ++i)
    {
        const auto node = v.getChild (i);
        if (! node.hasType (ids::SLOT))
            continue;

        auto& s = slots[(size_t) i];
        s = snapshotFromValueTree (node);

        if (s.name.isEmpty())
            s.name = "Slot " + juce::String (i + 1);
    }
}

} // namespace pr
