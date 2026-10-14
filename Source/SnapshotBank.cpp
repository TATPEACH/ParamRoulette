#include "SnapshotBank.h"

namespace pr
{

namespace ids
{
    static const juce::Identifier SNAPSHOTS { "SNAPSHOTS" };
    static const juce::Identifier SLOT      { "SLOT" };
    static const juce::Identifier CHAINPOS  { "CHAINPOS" };   // v3 起：SLOT 下面一层，每链位一个
    static const juce::Identifier pos       { "pos" };        // 链位号
    static const juce::Identifier used      { "used" };
    static const juce::Identifier name      { "name" };
    static const juce::Identifier values    { "values" };
    static const juce::Identifier child     { "child" };
    static const juce::Identifier pluginId  { "pluginId" };   // v3 起；老工程里没有
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

    s.pluginId = plugin.getPluginDescription().createIdentifierString();
    s.name = name;
    s.used = true;
    return s;
}

bool snapshotMatchesPlugin (const Snapshot& s, const juce::AudioPluginInstance& plugin)
{
    // 老工程存的快照没有 pluginId。那个年代链上只有一位，混不起来，所以放行。
    if (s.pluginId.isEmpty())
        return true;

    return s.pluginId == plugin.getPluginDescription().createIdentifierString();
}

bool restoreSnapshot (const Snapshot& s, juce::AudioPluginInstance& plugin)
{
    if (! s.used)
        return false;

    // 插件对不上就什么都不做。childState 是那个插件私有的字节流，
    // 喂给别的插件轻则被忽略、重则崩溃或者污染它的内部状态。
    if (! snapshotMatchesPlugin (s, plugin))
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
void SnapshotBank::capture (int snapSlot, const SnapTarget* targets, int numTargets,
                            const juce::String& name)
{
    if (! juce::isPositiveAndBelow (snapSlot, kNumSlots) || targets == nullptr)
        return;

    auto& cs = slots[(size_t) snapSlot];
    cs = ChainSnapshot {};
    cs.name = name.isNotEmpty() ? name : ("快照 "_u8 + juce::String (snapSlot + 1));

    for (int i = 0; i < juce::jmin (numTargets, kMaxChain); ++i)
        if (auto* p = targets[i].plugin)
            cs.perSlot[(size_t) i] = captureSnapshot (*p, cs.name);

    currentWinner = -1;
}

void SnapshotBank::clearSlot (int snapSlot)
{
    if (! juce::isPositiveAndBelow (snapSlot, kNumSlots))
        return;

    slots[(size_t) snapSlot] = ChainSnapshot {};
    if (currentWinner == snapSlot)
        currentWinner = -1;
}

void SnapshotBank::clearAll()
{
    for (auto& s : slots)
        s = ChainSnapshot {};
    currentWinner = -1;
}

int SnapshotBank::recall (int snapSlot, const SnapTarget* targets, int numTargets)
{
    if (! isUsed (snapSlot) || targets == nullptr)
        return 0;

    const auto& cs = slots[(size_t) snapSlot];
    int restored = 0;

    for (int i = 0; i < juce::jmin (numTargets, kMaxChain); ++i)
    {
        auto* p = targets[i].plugin;

        // 身份对不上的链位自动跳过（restoreSnapshot 内部会挡）。
        // 用户在别的链位装了别的插件之后召回旧快照就是这种情况。
        if (p != nullptr && restoreSnapshot (cs.perSlot[(size_t) i], *p))
            ++restored;
    }

    if (restored > 0)
        currentWinner = snapSlot;

    return restored;
}

int SnapshotBank::numUsed() const noexcept
{
    int n = 0;
    for (auto& s : slots)
        if (s.anyUsed())
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
        if (slots[(size_t) i].anyUsed())
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
        if (slots[(size_t) i].anyUsed())
            sum += raw[i];

    if (sum <= 1.0e-9f)
    {
        // 光标正好落在某个空角上，已用快照的权重全是 0。
        // 退回"raw 最大的那个已用快照"，至少还是连续的。
        int best = used[0];
        for (int i = 1; i < nUsed; ++i)
            if (raw[used[i]] > raw[best])
                best = used[i];
        w.add (best, 1.0f);
        return w;
    }

    for (int i = 0; i < kNumSlots; ++i)
        if (slots[(size_t) i].anyUsed() && raw[i] > 0.0f)
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

int SnapshotBank::applyMorph (const SnapTarget* targets, int numTargets,
                              MorphMode mode, float x, float y)
{
    if (targets == nullptr)
        return 0;

    const auto w = computeWeights (mode, x, y);
    if (w.isEmpty())
        return 0;

    const int winner = pickWinner (w);
    if (winner < 0)
        return 0;

    int written = 0;

    // 逐链位插值。每位各自比身份 —— 用户可能只换了链上某一位的插件，
    // 那一位跳过、其余照插值，比"整份快照作废"有用得多。
    for (int slot = 0; slot < juce::jmin (numTargets, kMaxChain); ++slot)
        written += morphOneSlot (slot, targets[slot], w, winner);

    return written;
}

int SnapshotBank::morphOneSlot (int chainSlot, const SnapTarget& target,
                                const Weights& w, int winner)
{
    auto* plugin = target.plugin;

    if (plugin == nullptr || target.model == nullptr || target.checked == nullptr)
        return 0;

    const auto& model   = *target.model;
    const auto& checked = *target.checked;
    const auto& params    = plugin->getParameters();
    const auto  numParams = (size_t) params.size();
    int written = 0;

    // 这一份快照能不能用在这个链位的插件上？
    //
    // 光比参数个数不够：两个不同插件参数数相同的情况一点都不罕见（同厂的
    // 姊妹产品、同一插件的两个版本），那时插值出来的是两组毫无关系的值的平均。
    // 所以先比身份，再比长度。
    const auto usable = [this, chainSlot, numParams, plugin] (int snapSlot)
    {
        const auto& s = slots[(size_t) snapSlot].perSlot[(size_t) chainSlot];
        return s.used
            && s.values.size() == numParams
            && snapshotMatchesPlugin (s, *plugin);
    };

    const auto valueAt = [this, chainSlot] (int snapSlot, size_t idx)
    {
        return slots[(size_t) snapSlot].perSlot[(size_t) chainSlot].values[idx];
    };

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
                const int snapSlot = w.e[(size_t) i].slot;
                if (! usable (snapSlot))
                    continue;
                acc += valueAt (snapSlot, idx) * w.e[(size_t) i].w;
                sum += w.e[(size_t) i].w;
            }
            if (sum <= 0.0f)
                continue;
            v = acc / sum;
        }
        else
        {
            // toggle / stepped 没有"中间态"，权重最大的快照说了算
            if (! usable (winner))
                continue;
            v = valueAt (winner, idx);
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

    // 身份也要存。不然工程重开之后校验就失效了，而"切错链位召回快照"
    // 恰恰最容易在重开工程、界面还没看清的时候发生。
    if (s.pluginId.isNotEmpty())
        node.setProperty (ids::pluginId, s.pluginId, nullptr);

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

    // 老工程里没有这个属性 → 留空 → 校验放行（那时链上只有一位，混不起来）。
    s.pluginId = node.getProperty (ids::pluginId, juce::String()).toString();

    // 两样都空就当没存过——否则会拿一份空快照去"精确还原"，把插件清成默认值
    if (s.values.empty() && s.childState.getSize() == 0)
        s.used = false;

    return s;
}

//==============================================================================
// SNAPSHOTS
//   └─ SLOT (快照 1..4)           name
//       └─ CHAINPOS (链位 0..5)   pos + Snapshot 的字段
//
// v2 的格式是 SLOT 直接就装 Snapshot 的字段（那时只有一个插件）。
// 读的那边靠"SLOT 里有没有 CHAINPOS 子节点"分辨，见 restoreFromValueTree。
juce::ValueTree SnapshotBank::toValueTree() const
{
    juce::ValueTree root (ids::SNAPSHOTS);

    for (int i = 0; i < kNumSlots; ++i)
    {
        const auto& cs = slots[(size_t) i];

        juce::ValueTree node (ids::SLOT);
        node.setProperty (ids::name, cs.name, nullptr);

        for (int c = 0; c < kMaxChain; ++c)
        {
            if (! cs.perSlot[(size_t) c].used)
                continue;                       // 空链位不写，省掉一堆空节点

            auto child = snapshotToValueTree (cs.perSlot[(size_t) c], ids::CHAINPOS);
            child.setProperty (ids::pos, c, nullptr);
            node.appendChild (child, nullptr);
        }

        root.appendChild (node, nullptr);
    }

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

        auto& cs = slots[(size_t) i];
        cs.name = node.getProperty (ids::name, juce::String()).toString();

        const int numPos = node.getNumChildren();

        if (numPos == 0)
        {
            // v2：SLOT 自己就是一份 Snapshot（那时链上只有一位）→ 装进链位 0。
            cs.perSlot[0] = snapshotFromValueTree (node);
        }
        else
        {
            for (int c = 0; c < numPos; ++c)
            {
                const auto child = node.getChild (c);
                if (! child.hasType (ids::CHAINPOS))
                    continue;

                const int pos = juce::jlimit (0, kMaxChain - 1,
                                              (int) child.getProperty (ids::pos, 0));
                cs.perSlot[(size_t) pos] = snapshotFromValueTree (child);
            }
        }

        if (cs.name.isEmpty())
            cs.name = "快照 "_u8 + juce::String (i + 1);
    }
}

} // namespace pr
