#include "ChainRunner.h"

namespace pr
{

bool runLink (const ChainLink& link, juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    auto* p = link.plugin;

    if (p == nullptr || link.bypassed)
        return true;                // 空位 / 旁通：直通，不算失败

    const int numSamples = buffer.getNumSamples();
    const int need = link.channels;
    const int have = buffer.getNumChannels();

    if (need <= have)
    {
        p->processBlock (buffer, midi);
        return true;
    }

    // 子插件要的声道比宿主给的多。必须走暂存区，否则它会越界读写。
    auto* scratch = link.scratch;

    if (scratch == nullptr
        || scratch->getNumChannels() < need
        || scratch->getNumSamples() < numSamples)
    {
        // 只可能是 prepareToPlay 还没跑过、或者宿主中途加大了块。
        // 宁可这一段不出声，也不越界。
        return false;
    }

    for (int ch = 0; ch < need; ++ch)
    {
        if (ch < have) scratch->copyFrom (ch, 0, buffer, ch, 0, numSamples);
        else           scratch->clear (ch, 0, numSamples);
    }

    // 这个 AudioBuffer 只是包住已有指针，不分配内存——音频线程上不能 malloc。
    juce::AudioBuffer<float> view (scratch->getArrayOfWritePointers(), need, 0, numSamples);
    p->processBlock (view, midi);

    for (int ch = 0; ch < have; ++ch)
        buffer.copyFrom (ch, 0, *scratch, ch, 0, numSamples);

    return true;
}

ChainRunResult runChain (const ChainLink* links, int numLinks,
                         juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi,
                         float* peakAfter)
{
    ChainRunResult out;

    if (links == nullptr)
        return out;

    auto peakNow = [&buffer]
    {
        float p = 0.0f;
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            p = juce::jmax (p, buffer.getMagnitude (ch, 0, buffer.getNumSamples()));
        return p;
    };

    for (int i = 0; i < numLinks; ++i)
    {
        const auto& link = links[i];

        if (link.isActive())
        {
            if (runLink (link, buffer, midi))
                ++out.ran;
            else
                ++out.skipped;
        }

        if (peakAfter != nullptr)
            peakAfter[i] = peakNow();
    }

    return out;
}

int chainLatencySamples (const ChainLink* links, int numLinks)
{
    if (links == nullptr)
        return 0;

    int total = 0;

    for (int i = 0; i < numLinks; ++i)
        if (links[i].isActive())
            total += juce::jmax (0, links[i].plugin->getLatencySamples());

    return total;
}

double chainTailSeconds (const ChainLink* links, int numLinks)
{
    if (links == nullptr)
        return 0.0;

    double total = 0.0;

    for (int i = 0; i < numLinks; ++i)
        if (links[i].isActive())
            total += juce::jmax (0.0, links[i].plugin->getTailLengthSeconds());

    return total;
}

} // namespace pr
