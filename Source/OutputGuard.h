#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

namespace pr
{

/** 链尾的安全网：削掉爆音、清掉非法样本。

    **为什么必须自己写一个，不指望用户挂个 limiter。**
    随机化一个全通滤波器的反馈量、或者一组共振器的 Q，炸到 +40 dB 甚至 NaN
    是完全现实的结果——尤其是"随机加载若干效果器再随机参数"这条路。
    爆音会伤耳朵和监听设备，而 NaN 一旦进了混音总线，很多插件的内部状态
    会永久污染，只能重开工程。这两件都不能指望用户事后补救。

    **不是母带处理器。** 没有 lookahead（那要报 PDC 延迟，会让宿主给整条链
    加补偿，得不偿失），所以极快的瞬态会有一点软削波染色。这是有意的取舍：
    安全优先，音质其次。真要做母带就在后面串真的 limiter。

    信号链（就地处理）：

      1. **非法样本清洗** —— NaN / Inf → 0，并置一个 latch 让界面能报警
      2. **前馈增益衰减** —— 按声道联动的瞬时峰值算目标增益，
         快 attack（1 ms）/ 慢 release（100 ms）平滑
      3. **有界软削波** —— 只在 knee 以上起作用，兜住 attack 那 1 ms 的过冲

    第 3 步为什么不能全程 tanh：`tanh(u) ≈ u - u³/3`，在 u = 0.5 时已经有 4%
    的误差，也就是**低电平信号也被染色**。所以 knee 以下完全透明，
    knee 以上才压缩，代价是分段函数，收益是不碰正常电平。
*/
class OutputGuard
{
public:
    OutputGuard() = default;

    void prepare (double sampleRate, int numChannels);
    void reset() noexcept;

    /** 输出上限，dBFS。0 = 满刻度；默认 −1 dB 留一点余量给下游的转换器。 */
    void setCeilingDb (float db) noexcept;

    void setEnabled (bool on) noexcept { enabled = on; }
    bool isEnabled() const noexcept    { return enabled; }

    /** 就地处理一块。返回这一块里最大的增益衰减（dB，≥ 0）。

        关掉的时候**仍然清洗非法样本**——NaN 防护不是可选项，
        它防的是"污染整条总线"而不是"声音太大"。 */
    float process (juce::AudioBuffer<float>&) noexcept;

    /** 自上次询问以来出现过非法样本吗？读一次就清。
        界面用它显示「子插件输出了 NaN」——这条信息必须让用户看到，
        否则他只会听到突然静音，而完全不知道发生了什么。 */
    bool consumeSawNonFinite() noexcept { return sawNonFinite.exchange (false); }

    /** 最近一块的增益衰减（dB，≥ 0），给界面上的表头用。 */
    float getGainReductionDb() const noexcept { return lastGrDb.load(); }

    static constexpr float kAttackMs  = 1.0f;
    static constexpr float kReleaseMs = 100.0f;

    /** knee 以下完全透明。0.8 = 上限的 80%（约 −1.9 dB）以下不碰。 */
    static constexpr float kKneeFraction = 0.8f;

private:
    static float coeffFor (float ms, double sampleRate) noexcept;

    double sr        = 44100.0;
    float  ceilingDb = -1.0f;
    float  ceiling   = 0.891f;      ///< 线性，= 10^(ceilingDb/20)
    float  knee      = 0.713f;

    float attackCoeff  = 0.0f;
    float releaseCoeff = 0.0f;

    /** 当前增益。**跨块保持**——每块重置成 1.0 的话，
        持续超标的信号会在每个块边界上重新过冲一次，听起来是周期性的爆裂。 */
    float gain = 1.0f;

    bool enabled = true;

    std::atomic<bool>  sawNonFinite { false };
    std::atomic<float> lastGrDb     { 0.0f };
};

} // namespace pr
