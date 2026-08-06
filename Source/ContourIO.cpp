#include "ContourIO.h"

namespace pr
{

namespace
{
    using L = ContourLayout;

    /** 签名：uint32 1、uint32 128、byte 235。小端。 */
    const juce::uint8 kSignature[9] = { 0x01, 0x00, 0x00, 0x00,
                                        0x80, 0x00, 0x00, 0x00,
                                        0xEB };

    int recordOffset (const ContourSlot& s, int program) noexcept
    {
        return s.byteOffset + L::kHeaderBytes + program * L::kStride;
    }

    bool recordFits (const juce::MemoryBlock& mb, const ContourSlot& s, int program) noexcept
    {
        if (s.byteOffset < 0 || ! juce::isPositiveAndBelow (program, L::kNumPrograms))
            return false;

        const auto end = (size_t) recordOffset (s, program) + (size_t) L::kRecordBytes;
        return end <= mb.getSize();
    }
}

//==============================================================================
float ckDecodeFloat (const juce::uint8* p) noexcept
{
    juce::uint32 bits = 0;
    for (int k = 0; k < L::kValueBytes; ++k)
        bits |= (juce::uint32) (((juce::uint32) p[k] - 1u) & 0x7Fu) << (k * 7);

    float out {};
    static_assert (sizeof (out) == sizeof (bits), "");
    std::memcpy (&out, &bits, sizeof (out));
    return out;
}

void ckEncodeFloat (juce::uint8* dest, float value) noexcept
{
    juce::uint32 bits = 0;
    std::memcpy (&bits, &value, sizeof (bits));

    for (int k = 0; k < L::kValueBytes; ++k)
        dest[k] = (juce::uint8) ((((bits >> (k * 7)) & 0x7Fu) + 1u) & 0xFFu);
}

//==============================================================================
juce::Array<ContourSlot> findContourGroups (const juce::MemoryBlock& mb)
{
    juce::Array<ContourSlot> out;

    const auto* data = static_cast<const juce::uint8*> (mb.getData());
    const auto  size = mb.getSize();

    if (data == nullptr || size < sizeof (kSignature))
        return out;

    // 每组要放得下 128 条记录才算真的组，否则是巧合撞上的字节
    const size_t groupBytes = (size_t) L::kHeaderBytes
                            + (size_t) L::kNumPrograms * (size_t) L::kStride;

    for (size_t i = 0; i + sizeof (kSignature) <= size; ++i)
    {
        if (std::memcmp (data + i, kSignature, sizeof (kSignature)) != 0)
            continue;

        if (i + groupBytes > size)
            continue;

        out.add ({ out.size(), (int) i });

        if (out.size() >= L::kNumGroups)
            break;
    }

    return out;
}

//==============================================================================
int NativeContour::usedSegments() const noexcept
{
    int n = 0;
    for (int i = 0; i < L::kPoints; ++i)
        if (times[(size_t) i] != 0.0f && std::isfinite (times[(size_t) i]))
            ++n;

    return n;
}

bool readContour (const juce::MemoryBlock& mb, const ContourSlot& s, int program, NativeContour& out)
{
    if (! recordFits (mb, s, program))
        return false;

    const auto* rec = static_cast<const juce::uint8*> (mb.getData()) + recordOffset (s, program);

    if (rec[0] != L::kRecordBytes)      // 长度标记不对说明偏移错了，宁可什么都不做
        return false;

    for (int i = 0; i < L::kPoints; ++i)
    {
        out.levels[(size_t) i] = ckDecodeFloat (rec + L::kLevelOffset + i * L::kValueBytes);
        out.times [(size_t) i] = ckDecodeFloat (rec + L::kTimeOffset  + i * L::kValueBytes);
    }

    return true;
}

bool writeContour (juce::MemoryBlock& mb, const ContourSlot& s, int program, const NativeContour& c)
{
    if (! recordFits (mb, s, program))
        return false;

    auto* rec = static_cast<juce::uint8*> (mb.getData()) + recordOffset (s, program);

    if (rec[0] != L::kRecordBytes)
        return false;

    for (int i = 0; i < L::kPoints; ++i)
    {
        ckEncodeFloat (rec + L::kLevelOffset + i * L::kValueBytes, c.levels[(size_t) i]);
        ckEncodeFloat (rec + L::kTimeOffset  + i * L::kValueBytes, c.times [(size_t) i]);
    }

    return true;
}

//==============================================================================
ContourShape shapeForSemantic (MotionSemantic s) noexcept
{
    switch (s)
    {
        // 上扬 / 下降 用**同一条** ramp，方向靠 invert 参数。
        case MotionSemantic::Rise:
        case MotionSemantic::Fall:     return ContourShape::Ramp;

        // 先扬后抑 / 先抑后扬 用同一个 peak，同理。
        case MotionSemantic::RiseFall:
        case MotionSemantic::FallRise:
        case MotionSemantic::Wander:   return ContourShape::Peak;

        case MotionSemantic::Preserve: break;
    }
    return ContourShape::Keep;
}

int stagesForShape (ContourShape s) noexcept
{
    switch (s)
    {
        case ContourShape::Ramp: return 1;
        case ContourShape::Peak: return 2;
        case ContourShape::Keep: break;
    }
    return 0;
}

NativeContour makeShapeContour (const NativeContour& base, ContourShape shape,
                                float jitter, juce::Random& rng)
{
    NativeContour out = base;

    if (shape == ContourShape::Keep)
        return out;

    // 全部清零，只填要用的那 1~2 段。codex 在 fauna 上就是这么做的：
    // 保留基座的 16 段反而会让形状读不出来。
    out.levels.fill (0.0f);
    out.times .fill (0.0f);

    const float j = juce::jlimit (0.0f, 1.0f, jitter);

    if (shape == ContourShape::Ramp)
    {
        out.levels[0] = 1.0f;
        // 时长在 1.0 上小幅抖。每次都从 1.0 重算，不读上一次的结果，所以不会漂。
        out.times[0] = juce::jmax (0.05f, 1.0f + (rng.nextFloat() * 2.0f - 1.0f) * 0.35f * j);
        return out;
    }

    // peak：先到顶再回零。总时长固定 0.5，只随机拐点落在前段还是后段。
    constexpr float kTotal = 0.5f;
    const float split = 0.5f + (rng.nextFloat() * 2.0f - 1.0f) * 0.30f * j;   // 0.2 ~ 0.8

    out.levels[0] = 1.0f;
    out.levels[1] = 0.0f;
    out.times[0]  = juce::jmax (0.02f, kTotal * juce::jlimit (0.15f, 0.85f, split));
    out.times[1]  = juce::jmax (0.02f, kTotal - out.times[0]);
    return out;
}

//==============================================================================
ContourResult randomiseContours (juce::AudioPluginInstance& plugin, MotionSemantic sem,
                                 float motion, juce::Random& rng,
                                 const juce::MemoryBlock* baseState,
                                 int onlyGroup, int expectedProgram)
{
    JUCE_ASSERT_MESSAGE_THREAD

    ContourResult result;

    const auto shape = shapeForSemantic (sem);
    if (shape == ContourShape::Keep)
        return result;

    const int program = plugin.getCurrentProgram();

    // 用户中途换了 preset：基座已经对不上了，硬写只会毁掉新 preset。
    if (expectedProgram >= 0 && program != expectedProgram)
    {
        result.failed = true;
        return result;
    }

    if (! juce::isPositiveAndBelow (program, L::kNumPrograms))
        return result;

    // 每次都重新读当前状态，不缓存偏移：状态块长度会变（实测 239292~239303），
    // 缓存下来的字节偏移下一次就是错的。
    juce::MemoryBlock state;
    plugin.getStateInformation (state);

    const auto groups = findContourGroups (state);
    if (groups.isEmpty())
        return result;               // 不是认识的格式，什么都不做

    // 未改动的部分（尾部、flag 字节）从基座取。基座里找不到就退回当前状态。
    const juce::MemoryBlock* src = state.getSize() > 0 ? &state : nullptr;
    juce::Array<ContourSlot> baseGroups;

    if (baseState != nullptr && baseState->getSize() > 0)
    {
        baseGroups = findContourGroups (*baseState);
        if (baseGroups.size() == groups.size())
            src = baseState;
    }

    for (const auto& g : groups)
    {
        if (onlyGroup >= 0 && g.groupIndex != onlyGroup)
            continue;

        // 基座里对应那一组的偏移不一定和当前状态相同，得按组号取
        const ContourSlot readFrom = (src == baseState && g.groupIndex < baseGroups.size())
                                       ? baseGroups.getReference (g.groupIndex) : g;

        NativeContour base;
        if (! readContour (*src, readFrom, program, base))
            continue;

        if (! writeContour (state, g, program, makeShapeContour (base, shape, motion, rng)))
        {
            result.failed = true;
            return result;           // 写失败就别提交半套东西
        }

        ++result.written;
    }

    if (result.written > 0)
    {
        plugin.setStateInformation (state.getData(), (int) state.getSize());
        result.stages = stagesForShape (shape);
    }

    return result;
}

} // namespace pr
