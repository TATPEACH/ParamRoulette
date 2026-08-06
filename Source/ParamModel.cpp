#include "ParamModel.h"

namespace pr
{

namespace
{
    // 移植自 Lua 版的 DEFAULT_SKIP。Lua 那边写成模式串（"^wet$" / "_gui_"），
    // 这里拆成"完全相等"和"包含 / 前缀"两组，行为一致但不用背一个模式引擎。
    const char* const kExactSkip[] = {
        "wet", "dry", "bypass", "delta", "wet mix", "preset", "program", "pdc"
    };
}

juce::String kindLabel (Kind k)
{
    switch (k)
    {
        case Kind::Toggle:     return "开关"_u8;
        case Kind::Stepped:    return "档位"_u8;
        case Kind::Continuous: break;
    }
    return "连续"_u8;
}

juce::String skipReasonLabel (SkipReason r)
{
    switch (r)
    {
        case SkipReason::Blob:           return "值像数据块"_u8;
        case SkipReason::NotAutomatable: return "插件禁止自动化"_u8;
        case SkipReason::MetaParameter:  return "宏参数"_u8;
        case SkipReason::NameRule:       return "默认排除名单"_u8;
        case SkipReason::None:           break;
    }
    return {};
}

juce::String healthLabel (Health h)
{
    switch (h)
    {
        case Health::Ok:           return "正常"_u8;
        case Health::EndpointOnly: return "只收端点"_u8;
        case Health::Inert:        return "写不进"_u8;
        case Health::Unknown:      break;
    }
    return "未测"_u8;
}

bool looksLikeBinary (const juce::String& s)
{
    if (s.length() < 3)
        return false;

    int total = 0, nonAscii = 0;

    for (auto it = s.begin(); it != s.end(); ++it)
    {
        const auto cp = *it;

        if (cp < 32 && cp != 9)     // 控制字符（制表符除外）：这是数据，不是给人看的值
            return true;

        ++total;
        if (cp > 127)
            ++nonAscii;
    }

    if (total < 8)                  // 别误伤 "低通" / "−6.0 dB" 这种短的合法中文/符号值
        return false;

    return nonAscii >= total * 0.4;
}

Kind classify (const juce::AudioProcessorParameter& p)
{
    const int n = p.getNumSteps();

    // 两档 = 开关。这条必须排在 isBoolean() 前面并且不能依赖它：
    // 托管 VST3 时 isBoolean() 恒为 false（见头文件注释），只有档数说得准。
    if (n == 2)
        return Kind::Toggle;

    if (p.isBoolean())
        return Kind::Toggle;

    // 连续参数 getNumSteps() 返回 AudioProcessor::getDefaultNumParameterSteps()（默认 0x7fffffff
    // 量级），被 <= 10000 自然挡掉。上限与 Lua 版的 `nsteps >= 1 and nsteps <= 10000` 一致。
    if (n > 2 && n <= 10000)
        return Kind::Stepped;

    return Kind::Continuous;
}

SkipReason defaultSkipReason (const juce::AudioProcessorParameter& p,
                              const juce::String& lowerName,
                              bool blob)
{
    if (blob)
        return SkipReason::Blob;

    // 下面这两条是 JUCE 独有的信号，Lua 版没有对应物。
    // 注意：托管 VST3 时 isMetaParameter() 恒为 false（VST3Parameter 不覆写它），
    // 所以这条对 VST3 实际不生效——留着是为了别的格式，不指望它挡住宏参数。
    if (! p.isAutomatable())
        return SkipReason::NotAutomatable;
    if (p.isMetaParameter())
        return SkipReason::MetaParameter;

    for (auto* w : kExactSkip)
        if (lowerName == w)
            return SkipReason::NameRule;

    if (lowerName.contains ("_gui_") || lowerName.startsWith ("gui_"))
        return SkipReason::NameRule;

    // 选择器类参数**不**默认排除。随机一个调制目标是不是好主意取决于你想要什么
    // ——那是用户的决定，不是我们的。（morph 是另一回事：取两个枚举的平均得到的是
    // 第三个无关选项，那是正确性问题，见 ParamInfo::noInterpolate。）
    return SkipReason::None;
}

bool looksLikeSelector (const juce::String& lowerName)
{
    // 这些词后面跟的是一串离散码，不是连续量。插值它们等于随机重接线。
    static const char* const words[] = {
        "dest", "src", "source", "target", "route", "routing",
        "wave", "waveform", "shape", "type", "mode", "sync", "select", "sel",
        "preset", "program", "bank", "algo", "curve"
    };

    for (auto* w : words)
        if (lowerName.contains (w))
            return true;

    return false;
}

void snapshotPrevValues (juce::Array<ParamInfo>& model)
{
    for (auto& info : model)
    {
        info.prevValue   = info.value;
        info.prevDisplay = info.display;
        info.changed     = false;
    }
}

int markChangedValues (juce::Array<ParamInfo>& model)
{
    // 门槛比 kWriteEpsilon 松一点：只是给人看的，别把 1e-6 的浮点噪声标成"改了"
    constexpr float kEps = 1.0e-4f;

    int n = 0;
    for (auto& info : model)
    {
        info.changed = std::abs (info.value - info.prevValue) > kEps
                    || info.display != info.prevDisplay;
        if (info.changed)
            ++n;
    }

    return n;
}

juce::Array<ParamInfo> buildModel (juce::AudioPluginInstance& plugin)
{
    juce::Array<ParamInfo> model;

    const auto& params = plugin.getParameters();
    model.ensureStorageAllocated (params.size());

    for (int i = 0; i < params.size(); ++i)
    {
        auto* p = params[i];
        if (p == nullptr)
            continue;

        ParamInfo info;
        info.index   = i;
        info.name    = p->getName (128);
        if (info.name.isEmpty())
            info.name = "参数 "_u8 + juce::String (i);

        info.lowerName = info.name.toLowerCase();
        info.kind      = classify (*p);
        info.numSteps  = (info.kind == Kind::Continuous) ? 0 : p->getNumSteps();
        info.value     = p->getValue();
        info.display   = p->getCurrentValueAsText();
        info.blob      = looksLikeBinary (info.display);
        info.skipReason = defaultSkipReason (*p, info.lowerName, info.blob);
        info.noInterpolate = (info.kind != Kind::Continuous) || looksLikeSelector (info.lowerName);
        info.prevValue = info.value;
        info.prevDisplay = info.display;

        model.add (std::move (info));
    }

    return model;
}

void refreshValues (juce::AudioPluginInstance& plugin, juce::Array<ParamInfo>& model)
{
    const auto& params = plugin.getParameters();

    for (auto& info : model)
    {
        if (! juce::isPositiveAndBelow (info.index, params.size()))
            continue;

        if (auto* p = params[info.index])
        {
            info.value   = p->getValue();
            info.display = p->getCurrentValueAsText();
        }
    }
}

} // namespace pr
