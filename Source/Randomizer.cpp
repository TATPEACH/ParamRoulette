#include "Randomizer.h"

namespace pr
{

// 判"值变了"的门槛。归一化空间里 1e-5 已经远低于任何插件的显示精度。
static constexpr float kMoveEpsilon = 1.0e-5f;

bool randomizeOne (juce::AudioProcessorParameter& p,
                   Kind k, int numSteps,
                   float amount, float lo, float hi,
                   juce::Random& rng)
{
    const float cur = p.getValue();

    if (k == Kind::Toggle)
    {
        // 开关没有"中间值"，强度只能当翻转概率用。amount=0.3 就是三成的开关会被重掷。
        if (rng.nextFloat() >= amount)
            return false;

        const float v = rng.nextBool() ? 1.0f : 0.0f;
        p.setValueNotifyingHost (v);
        return std::abs (v - cur) > kMoveEpsilon;
    }

    const float target = lo + rng.nextFloat() * (hi - lo);
    float v = cur + (target - cur) * amount;        // 强度混合：从当前值往目标值挪 amount 那么多

    if (k == Kind::Stepped && numSteps > 1)
    {
        // 吸附到合法档位，用**随机化取整**而不是 std::round。
        //
        // std::round 在这里会死锁：档位少的时候 amount<0.5 让 v 永远落在 cur 那一格里，
        // round 把它压回 cur，参数一辈子不动。实测 2 档 + 强度 0.4 的改变率是 0.0%。
        // floor(x + rand) 的期望值和 round(x) 一样，但"想挪 5%"变成"5% 概率挪一档"，
        // 而不是"0% 概率挪"。顺带修掉 round 在两个端点上只占半格权重的偏置。
        const float steps = (float) (numSteps - 1);
        v = std::floor (v * steps + rng.nextFloat()) / steps;
    }

    v = juce::jlimit (0.0f, 1.0f, v);
    p.setValueNotifyingHost (v);
    return std::abs (v - cur) > kMoveEpsilon;
}

void subsample (juce::Array<int>& candidates, int maxCount, juce::Random& rng)
{
    if (maxCount <= 0 || candidates.size() <= maxCount)
        return;

    // 部分 Fisher-Yates：第 i 轮从 [i, n) 里挑一个换到 i 位。
    // 只走 maxCount 轮，所以代价是 O(maxCount) 而不是洗完整个数组。
    const int n = candidates.size();
    for (int i = 0; i < maxCount; ++i)
    {
        const int j = i + rng.nextInt (n - i);
        if (j != i)
            candidates.swap (i, j);
    }

    candidates.removeRange (maxCount, n - maxCount);
}

RandomResult randomize (juce::AudioPluginInstance& plugin,
                        const juce::Array<ParamInfo>& model,
                        const juce::Array<bool>& checked,
                        RandomSettings settings,
                        juce::Random& rng)
{
    settings.normalise();

    const auto& params = plugin.getParameters();
    RandomResult result;

    // 先收候选、再抽样、最后才动手。
    // 不能边遍历边"按概率跳过"——那样实际改动个数是二项分布，
    // 用户设 2 会时不时得到 0 个或 5 个，"每次只动 N 个"就不成立了。
    juce::Array<int> candidates;
    candidates.ensureStorageAllocated (model.size());

    for (int k = 0; k < model.size(); ++k)
    {
        if (k >= checked.size() || ! checked.getUnchecked (k))
            continue;

        const auto& info = model.getReference (k);
        if (! juce::isPositiveAndBelow (info.index, params.size()))
            continue;

        if (params[info.index] != nullptr)
            candidates.add (k);
    }

    subsample (candidates, settings.maxParams, rng);

    for (const int k : candidates)
    {
        const auto& info = model.getReference (k);
        auto* p = params[info.index];

        ++result.touched;
        if (randomizeOne (*p, info.kind, info.numSteps,
                          settings.amount, settings.lo, settings.hi, rng))
            ++result.moved;
    }

    return result;
}

//==============================================================================
namespace
{
    // 三轮分别写这三个值。顺序有意从 0 走到 1：写 0.5 时当前值是 0.0，
    // 所以"被弹回"表现为读回 0.0，和"吸附到最近端点"读回的也是 0.0——
    // 两者在归一化空间里无法区分，但对我们要做的决定（别写中间值）没有区别。
    constexpr float kProbeValues[InertProbe::kPhases] = { 0.0f, 0.5f, 1.0f };

    // 判"这次写入落在目标上"的门槛。比 kMoveEpsilon 松：插件的内部量化
    // （比如 float→int16 参数）会让 0.5 变成 0.499985，那不算被拒。
    constexpr float kLandEpsilon = 1.0e-3f;
}

InertProbe::~InertProbe()
{
    cancel();
}

void InertProbe::start (juce::AudioPluginInstance& plugin,
                        juce::Array<ParamInfo>& m,
                        Finished cb)
{
    JUCE_ASSERT_MESSAGE_THREAD

    cancel();

    target     = &plugin;
    model      = &m;
    onFinished = std::move (cb);
    phase      = 0;

    const auto& params = plugin.getParameters();

    before.clearQuick();
    probed.clearQuick();
    before.resize (m.size());
    probed.resize (m.size());

    for (auto& r : reads)
    {
        r.clearQuick();
        r.resize (m.size());
    }

    for (int k = 0; k < m.size(); ++k)
    {
        auto& info = m.getReference (k);
        info.health = Health::Unknown;

        probed.set (k, false);
        before.set (k, 0.0f);

        // blob 参数是插件的数据槽，往里灌极值再还原有真实风险，不值得为体检付这个代价
        if (info.skipReason == SkipReason::Blob)
            continue;

        if (! juce::isPositiveAndBelow (info.index, params.size()))
            continue;

        auto* p = params[info.index];
        if (p == nullptr)
            continue;

        before.set (k, p->getValue());
        probed.set (k, true);
    }

    writePhase (0);
    startTimer (kSettleMs);
}

void InertProbe::writePhase (int ph)
{
    if (target == nullptr || model == nullptr || ! juce::isPositiveAndBelow (ph, kPhases))
        return;

    const auto& params = target->getParameters();

    for (int k = 0; k < model->size() && k < probed.size(); ++k)
    {
        if (! probed.getUnchecked (k))
            continue;

        const auto& info = model->getReference (k);
        if (! juce::isPositiveAndBelow (info.index, params.size()))
            continue;

        if (auto* p = params[info.index])
            p->setValueNotifyingHost (kProbeValues[ph]);
    }
}

void InertProbe::readPhase (int ph)
{
    if (target == nullptr || model == nullptr || ! juce::isPositiveAndBelow (ph, kPhases))
        return;

    const auto& params = target->getParameters();
    auto& dest = reads[(size_t) ph];

    for (int k = 0; k < model->size() && k < probed.size(); ++k)
    {
        if (! probed.getUnchecked (k))
            continue;

        const auto& info = model->getReference (k);
        if (! juce::isPositiveAndBelow (info.index, params.size()))
            continue;

        if (auto* p = params[info.index])
            dest.set (k, p->getValue());
    }
}

void InertProbe::classify()
{
    if (model == nullptr)
        return;

    const auto landed = [] (float got, float wanted)
    {
        return std::abs (got - wanted) <= kLandEpsilon;
    };

    for (int k = 0; k < model->size() && k < probed.size(); ++k)
    {
        if (! probed.getUnchecked (k))
            continue;

        auto& info = model->getReference (k);

        const float lo  = reads[0].getUnchecked (k);
        const float mid = reads[1].getUnchecked (k);
        const float hi  = reads[2].getUnchecked (k);

        const bool gotLo  = landed (lo,  0.0f);
        const bool gotHi  = landed (hi,  1.0f);
        const bool gotMid = landed (mid, 0.5f);

        if (! gotLo && ! gotHi && ! gotMid)
        {
            // 三个值一个都没落上。可能是完全只读，也可能量程被插件锁死在某一段——
            // 无论哪种，随机化对它都是无效操作。
            info.health = Health::Inert;
            continue;
        }

        // 两端都收但 0.5 被吸到端点上 = 伪二值。改写 kind，这样 randomizeOne
        // 走 Toggle 分支、只写精确端点——这是唯一写得进去的值。
        if (gotLo && gotHi && ! gotMid
            && (landed (mid, 0.0f) || landed (mid, 1.0f)))
        {
            info.health   = Health::EndpointOnly;
            info.kind     = Kind::Toggle;
            info.numSteps = 2;
            info.noInterpolate = true;      // 只收端点的东西当然也不能插值
            continue;
        }

        info.health = Health::Ok;
    }
}

void InertProbe::timerCallback()
{
    stopTimer();

    if (target == nullptr || model == nullptr)
    {
        reset();
        return;
    }

    readPhase (phase);

    if (++phase < kPhases)
    {
        writePhase (phase);
        startTimer (kSettleMs);
        return;
    }

    classify();

    int numInert = 0, numEndpointOnly = 0;
    for (int k = 0; k < model->size(); ++k)
    {
        const auto h = model->getReference (k).health;
        if (h == Health::Inert)             ++numInert;
        else if (h == Health::EndpointOnly) ++numEndpointOnly;
    }

    restoreAll();

    // 先清状态再回调：回调里可能会销毁这个 probe
    auto cb = std::move (onFinished);
    reset();

    if (cb != nullptr)
        cb (numInert, numEndpointOnly);
}

void InertProbe::restoreAll()
{
    if (target == nullptr || model == nullptr)
        return;

    const auto& params = target->getParameters();

    for (int k = 0; k < model->size() && k < probed.size(); ++k)
    {
        if (! probed.getUnchecked (k))
            continue;

        const auto& info = model->getReference (k);
        if (! juce::isPositiveAndBelow (info.index, params.size()))
            continue;

        if (auto* p = params[info.index])
            p->setValueNotifyingHost (before.getUnchecked (k));
    }
}

void InertProbe::reset()
{
    target = nullptr;
    model  = nullptr;
    phase  = 0;
    before.clearQuick();
    probed.clearQuick();
    for (auto& r : reads)
        r.clearQuick();
    onFinished = nullptr;
}

void InertProbe::cancel()
{
    if (! isRunning())
        return;

    stopTimer();
    restoreAll();
    reset();
}

} // namespace pr
