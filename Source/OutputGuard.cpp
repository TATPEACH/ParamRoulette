#include "OutputGuard.h"

namespace pr
{

float OutputGuard::coeffFor (float ms, double sampleRate) noexcept
{
    // 一阶低通的单极点系数：时间常数 t 秒内衰减到 1/e。
    const double t = juce::jmax (1.0e-5, (double) ms * 0.001);
    return (float) std::exp (-1.0 / (t * sampleRate));
}

void OutputGuard::prepare (double sampleRate, int /*numChannels*/)
{
    sr = sampleRate > 0.0 ? sampleRate : 44100.0;

    attackCoeff  = coeffFor (kAttackMs,  sr);
    releaseCoeff = coeffFor (kReleaseMs, sr);

    reset();
}

void OutputGuard::reset() noexcept
{
    gain = 1.0f;
    lastGrDb.store (0.0f);
    sawNonFinite.store (false);
}

void OutputGuard::setCeilingDb (float db) noexcept
{
    ceilingDb = juce::jlimit (-24.0f, 0.0f, db);
    ceiling   = juce::Decibels::decibelsToGain (ceilingDb);
    knee      = ceiling * kKneeFraction;
}

float OutputGuard::process (juce::AudioBuffer<float>& buffer) noexcept
{
    const int numCh = buffer.getNumChannels();
    const int numSm = buffer.getNumSamples();

    if (numCh <= 0 || numSm <= 0)
        return 0.0f;

    auto* const* chans = buffer.getArrayOfWritePointers();

    // —— 第一步：非法样本清洗。
    // 这一步**即使 guard 关掉也要做**。NaN 不是"声音太大"，它会顺着总线
    // 污染下游插件的内部状态（一个 NaN 进了反馈延迟线就永远出不来了）。
    bool sawBad = false;

    for (int ch = 0; ch < numCh; ++ch)
    {
        auto* d = chans[ch];

        for (int n = 0; n < numSm; ++n)
        {
            if (! std::isfinite (d[n]))
            {
                d[n] = 0.0f;
                sawBad = true;
            }
        }
    }

    if (sawBad)
    {
        sawNonFinite.store (true);

        // 内部状态也可能已经被污染（gain 参与了乘法）。归位重来。
        gain = 1.0f;
    }

    if (! enabled)
    {
        lastGrDb.store (0.0f);
        return 0.0f;
    }

    // —— 第二步：前馈增益衰减。
    // 峰值按声道**联动**取最大值：各声道独立压缩会让立体声像随电平摆动。
    float minGain = 1.0f;

    for (int n = 0; n < numSm; ++n)
    {
        float peak = 0.0f;
        for (int ch = 0; ch < numCh; ++ch)
            peak = juce::jmax (peak, std::abs (chans[ch][n]));

        const float target = (peak > ceiling) ? (ceiling / peak) : 1.0f;

        // 降增益走 attack（快），回增益走 release（慢）。
        const float c = (target < gain) ? attackCoeff : releaseCoeff;
        gain = target + c * (gain - target);

        minGain = juce::jmin (minGain, gain);

        for (int ch = 0; ch < numCh; ++ch)
        {
            // —— 第三步：有界软削波。
            // attack 有 1 ms 惯性，所以极快的瞬态会在增益跟上之前冲过 ceiling。
            // 这一段兜住它，并且**在数学上保证**输出不超过 ceiling：
            // tanh 的值域是 (−1, 1)，所以 knee + (ceiling − knee) · tanh(...)
            // 严格小于 ceiling。
            float x = chans[ch][n] * gain;

            if (const float a = std::abs (x); a > knee)
            {
                const float over = (a - knee) / juce::jmax (1.0e-6f, ceiling - knee);
                const float mag  = knee + (ceiling - knee) * std::tanh (over);
                x = (x < 0.0f) ? -mag : mag;
            }

            chans[ch][n] = x;
        }
    }

    const float grDb = (minGain < 1.0f) ? -juce::Decibels::gainToDecibels (minGain) : 0.0f;
    lastGrDb.store (grDb);
    return grDb;
}

} // namespace pr
