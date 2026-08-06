#pragma once

#include "ParamModel.h"
#include <array>

namespace pr
{

struct RandomSettings
{
    float amount = 1.0f;    ///< 强度：0 = 不动，1 = 完全重掷
    float lo     = 0.0f;    ///< 取值下限（归一化）
    float hi     = 1.0f;    ///< 取值上限（归一化）

    /** lo > hi 时交换，与 Lua 版一致。 */
    void normalise() noexcept { if (lo > hi) std::swap (lo, hi); }
};

struct RandomResult
{
    int touched = 0;    ///< 尝试改动的参数个数
    int moved   = 0;    ///< 新值确实与旧值不同的个数

    /** touched 明显大于 moved 就说明有一批参数在空转。
        这个数是**写入前**算的（新值 != 旧值），不依赖读回——
        JUCE 的 getValue() 读的是它自己的缓存，写完立刻读永远相等，
        拿它做 verify 是自欺欺人。真要问插件的意见得用 InertProbe。 */
    bool looksSuspicious() const noexcept { return touched >= 8 && moved * 4 < touched; }
};

/** 单个参数的随机化。三类语义与 ReaScript 版逐条对应。返回值是否真的变了。

    numSteps 由调用方从 ParamInfo 带进来，不在这里重新问插件——
    分类和吸附必须用同一个档数，否则会出现"按开关分类、按 17 档吸附"这种错位。
*/
bool randomizeOne (juce::AudioProcessorParameter& p,
                   Kind k, int numSteps,
                   float amount, float lo, float hi,
                   juce::Random& rng);

/** 批量随机化，只动 checked[i] == true 的参数。

    必须在消息线程上调用：一次重掷是几百次 setValueNotifyingHost，
    在音频线程上做等于当场分配内存加抢锁。
*/
RandomResult randomize (juce::AudioPluginInstance& plugin,
                        const juce::Array<ParamInfo>& model,
                        const juce::Array<bool>& checked,
                        RandomSettings settings,
                        juce::Random& rng);

//==============================================================================
/** 体检：问出每个参数到底**收什么值**。

    为什么必须异步：JUCE 的 `AudioProcessorParameter::getValue()` 对托管 VST3 读的是
    宿主侧缓存（`cachedParamValues`），`setValue` 会同步把缓存改掉。所以
    "写完立刻读" 永远相等，同步 verify 测不出任何东西。真正的信号是插件通过
    edit controller 把值**弹回来**——那是异步的，得让消息循环跑一拍才看得到。

    （REAPER 的 ReaScript 里不一样：`TrackFX_GetParam` 会去问插件本体，
    所以那边同步回读是有效的。别把两边的做法混起来。）

    为什么要探三个值而不是一个：只写端点的话，「伪二值」参数会给出**假的 Ok**——
    端点它收，所以体检说没问题；可随机化写的是中间值，一直被弹回。体检和随机化
    对同一个参数给出相反结论，而这类参数恰好就是最需要被认出来的那类。
    所以三轮：

      1. 写 0.0 → 等 kSettleMs → 读回
      2. 写 0.5 → 等 kSettleMs → 读回
      3. 写 1.0 → 等 kSettleMs → 读回
      4. 分类，然后把原值写回去

    分类：两端都收且 0.5 也落在 0.5 → Ok；两端都收但 0.5 被吸到某个端点上
    → EndpointOnly（并把 kind 改写成 Toggle，之后只写端点）；三个都没收
    → Inert。0.5 落在别处说明是多档位吸附，交给随机取整处理，不特殊对待。

    代价：三轮共 3 × kSettleMs，而且会把所有参数依次扫到 0、0.5、1。
    **会出声，而且可能很响。** UI 上要先警告。体检期间不能换插件实例，换之前必须 cancel()。
*/
class InertProbe : private juce::Timer
{
public:
    InertProbe() = default;
    ~InertProbe() override;

    /** numInert = 写什么都没用的个数；numEndpointOnly = 只收端点的个数。 */
    using Finished = std::function<void (int numInert, int numEndpointOnly)>;

    /** 开始体检。model 会被就地改写 health（以及伪二值参数的 kind / numSteps）。
        plugin 与 model 必须活过整个体检（约 kPhases × kSettleMs 毫秒）。 */
    void start (juce::AudioPluginInstance& plugin,
                juce::Array<ParamInfo>& model,
                Finished onFinished);

    bool isRunning() const noexcept { return target != nullptr; }

    /** 中止并尽力把原值写回去。 */
    void cancel();

    static constexpr int kSettleMs = 250;
    static constexpr int kPhases   = 3;

    /** 总耗时，给 UI 上的警告文案用。 */
    static constexpr int totalMs() noexcept { return kPhases * kSettleMs; }

private:
    void timerCallback() override;
    void writePhase (int phase);
    void readPhase (int phase);
    void classify();
    void restoreAll();
    void reset();

    juce::AudioPluginInstance* target = nullptr;
    juce::Array<ParamInfo>*    model  = nullptr;
    juce::Array<float>         before;      ///< 与 model 等长，按 model 下标对齐
    juce::Array<bool>          probed;      ///< 这一项到底测没测（blob 跳过）
    std::array<juce::Array<float>, (size_t) kPhases> reads;   ///< 每轮的读回值
    int                        phase = 0;
    Finished                   onFinished;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (InertProbe)
};

} // namespace pr
