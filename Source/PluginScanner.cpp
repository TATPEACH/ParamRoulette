#include "PluginScanner.h"

namespace pr
{

namespace
{
    constexpr const char* kPluginListKey = "pluginList";

    /** 排除表在 PropertiesFile 里的 key。identifier 形如
        "VST3-Serum-1a2b3c4d-5e6f7a8b"，里面没有 XML 属性不能带的字符，可以直接拼。 */
    juce::String exclusionKey (const juce::String& pluginId)
    {
        return "excl:" + pluginId;
    }

    /** 逐参数抖动幅度的 key。同一个 identifier，不同前缀。 */
    juce::String spanKey (const juce::String& pluginId)
    {
        return "span:" + pluginId;
    }

    /** 功能标签的 key。 */
    juce::String tagKey (const juce::String& pluginId)
    {
        return "tag:" + pluginId;
    }

    /** 32 位和 64 位构建**必须**用不同的设置文件。
        共用一份的话，64 位版会在插件表里列出一堆它根本加载不了的 32 位插件，
        反过来也一样——用户点了就是一个加载失败，而且看不出为什么。 */
    juce::String settingsName()
    {
        return sizeof (void*) == 4 ? "ParamRoulette-x86" : "ParamRoulette-x64";
    }
}

PluginScanner::PluginScanner()
{
    juce::PropertiesFile::Options opts;
    opts.applicationName     = settingsName();
    opts.filenameSuffix      = ".settings";
    opts.folderName          = "ParamRoulette";
    opts.osxLibrarySubFolder = "Application Support";
    opts.millisecondsBeforeSaving = 2000;

    props = std::make_unique<juce::PropertiesFile> (opts);

    // 扫描崩溃保护：PluginDirectoryScanner 在加载每个插件前把路径写进这个文件，
    // 加载成功再擦掉。上次扫描把宿主搞崩了的话，重来时会跳过那个凶手。
    deadMansPedal = props->getFile().getSiblingFile (settingsName() + "_DeadMansPedal");

    // VST3 + VST2 都注册。
    //
    // **注意这解决不了架构问题**：64 位进程装不了 32 位 DLL，这是 Windows 加载器的
    // 硬限制。fauna 是 32 位 VST2，所以要靠它就必须用 32 位构建（REAPER 会把 32 位
    // 插件自动扔进它自己的 reaper_host32.exe 桥接进程，我们的代码在那里面跑，
    // 加载 fauna 就成了同架构的普通加载）。
    formatManager.addFormat (new juce::VST3PluginFormat());
   #if JUCE_PLUGINHOST_VST
    formatManager.addFormat (new juce::VSTPluginFormat());
   #endif

    if (auto xml = props->getXmlValue (kPluginListKey))
        knownPlugins.recreateFromXml (*xml);
}

PluginScanner::~PluginScanner()
{
    savePluginList();
}

void PluginScanner::savePluginList()
{
    if (props == nullptr)
        return;

    if (auto xml = knownPlugins.createXml())
        props->setValue (kPluginListKey, xml.get());

    props->saveIfNeeded();
}

std::unique_ptr<juce::PluginListComponent> PluginScanner::createListComponent()
{
    auto c = std::make_unique<juce::PluginListComponent> (formatManager,
                                                          knownPlugins,
                                                          deadMansPedal,
                                                          props.get(),
                                                          /* async instantiation */ true);
    c->setOptionsButtonText ("扫描 / 选项"_u8);
    return c;
}

//==============================================================================
bool PluginScanner::hasExclusions (const juce::String& pluginId) const
{
    return props != nullptr
        && pluginId.isNotEmpty()
        && props->containsKey (exclusionKey (pluginId));
}

juce::Array<int> PluginScanner::loadExclusions (const juce::String& pluginId) const
{
    juce::Array<int> out;

    if (props == nullptr || pluginId.isEmpty())
        return out;

    const auto raw = props->getValue (exclusionKey (pluginId));
    if (raw.isEmpty())
        return out;

    for (const auto& tok : juce::StringArray::fromTokens (raw, ",", {}))
        if (tok.isNotEmpty())
            out.add (tok.getIntValue());

    return out;
}

void PluginScanner::saveExclusions (const juce::String& pluginId, const juce::Array<int>& excluded)
{
    if (props == nullptr || pluginId.isEmpty())
        return;

    juce::StringArray toks;
    for (int i : excluded)
        toks.add (juce::String (i));

    // 一个都不排除时也要写（写空串）。删掉 key 的话下次 hasExclusions 就成 false，
    // 用户"我全都要随机"的决定会被默认排除规则悄悄推翻。
    props->setValue (exclusionKey (pluginId), toks.joinIntoString (","));
    props->saveIfNeeded();
}

//==============================================================================
// 逐参数抖动幅度。格式 "172=0.03,88=0.02" —— **只存设过的那几个**，
// 不像排除表那样全量写：一个插件几百个参数，绝大多数用角色默认档就够了。
bool PluginScanner::hasSpans (const juce::String& pluginId) const
{
    return props != nullptr
        && pluginId.isNotEmpty()
        && props->containsKey (spanKey (pluginId));
}

std::map<int, float> PluginScanner::loadSpans (const juce::String& pluginId) const
{
    std::map<int, float> out;

    if (props == nullptr || pluginId.isEmpty())
        return out;

    for (const auto& tok : juce::StringArray::fromTokens (props->getValue (spanKey (pluginId)),
                                                          ",", {}))
    {
        if (tok.isEmpty())
            continue;

        const auto eq = tok.indexOfChar ('=');
        if (eq <= 0)
            continue;

        const int   idx = tok.substring (0, eq).getIntValue();
        const float val = tok.substring (eq + 1).getFloatValue();

        // 负数和 >1 都是无意义的幅度。宁可丢掉一条坏记录，
        // 也不要让一个脏值把某个参数抖到量程外面去。
        if (idx >= 0 && val >= 0.0f && val <= 1.0f)
            out[idx] = val;
    }

    return out;
}

void PluginScanner::saveSpans (const juce::String& pluginId, const std::map<int, float>& spans)
{
    if (props == nullptr || pluginId.isEmpty())
        return;

    juce::StringArray toks;
    for (const auto& [idx, val] : spans)
        toks.add (juce::String (idx) + "=" + juce::String (val, 4));

    if (toks.isEmpty())
        props->removeValue (spanKey (pluginId));    // 全清了就别留个空 key
    else
        props->setValue (spanKey (pluginId), toks.joinIntoString (","));

    props->saveIfNeeded();
}

//==============================================================================
juce::String fnTagLabel (FnTag t)
{
    switch (t)
    {
        case FnTag::EQ:         return "均衡"_u8;
        case FnTag::Filter:     return "滤波器"_u8;
        case FnTag::Allpass:    return "全通"_u8;
        case FnTag::Phase:      return "相位 / disperser"_u8;
        case FnTag::Delay:      return "延迟"_u8;
        case FnTag::Reverb:     return "混响"_u8;
        case FnTag::Modulation: return "调制"_u8;
        case FnTag::Dynamics:   return "动态"_u8;
        case FnTag::Distortion: return "失真 / 饱和"_u8;
        case FnTag::Pitch:      return "变调"_u8;
        case FnTag::Granular:   return "颗粒 / 时间"_u8;
        case FnTag::Utility:    return "工具"_u8;
        case FnTag::Unknown:    break;
    }

    return "未分类"_u8;
}

FnTag PluginScanner::guessTag (const juce::String& pluginName)
{
    const auto n = pluginName.toLowerCase();

    /** pattern 作为**独立词**出现吗（两边是非字母数字，或者贴着字符串边界）。

        短模式必须这么判，实测踩到三个坑：
          - `eq` 是 "frequency" / "sequence" 的子串 → 任何带 freq 的插件都会变均衡器
          - `ott` 是 "bottle" 的子串
          - `comb` 是 "combo" / "combustion" 的子串
        长而独特的模式（"disperser" / "granul"）用子串就够，还能顺带盖住
        "Wavefolder" 这种词中出现的情况。 */
    const auto hasToken = [&n] (const char* tok)
    {
        const juce::String t (tok);
        int from = 0;

        for (;;)
        {
            const int at = n.indexOf (from, t);
            if (at < 0)
                return false;

            const auto before = (at == 0) ? ' ' : n[at - 1];
            const auto afterI = at + t.length();
            const auto after  = (afterI >= n.length()) ? ' ' : n[afterI];

            const auto plain = [] (juce::juce_wchar c)
            {
                return juce::CharacterFunctions::isLetterOrDigit (c);
            };

            if (! plain (before) && ! plain (after))
                return true;

            from = at + 1;
        }
    };

    struct Rule
    {
        const char* pattern;
        FnTag       tag;
        bool        wholeWord = false;
    };

    // 顺序有讲究，每一条都是被实测打过脸的：
    //
    //   - "allpass" 在 "filter" 之前 —— 否则 "Allpass Filter" 判成普通滤波器，
    //     而全通和低通在配方里是完全不同的东西。
    //   - **调制类在延迟类之前** —— "bluechorus" 里含着 "echo"（e-c-h-o），
    //     bx_bluechorus2 会被判成延迟。反过来 "Echoboy" 里没有调制类关键词，
    //     所以这个顺序两头都对。
    //   - 动态类的 "clip" 在失真之前：clipper 主要用途是控峰值。
    static const Rule rules[] =
    {
        { "allpass",   FnTag::Allpass },
        { "all-pass",  FnTag::Allpass },
        { "disperser", FnTag::Phase },
        { "disperse",  FnTag::Phase },
        { "phaser",    FnTag::Phase },
        { "phase",     FnTag::Phase },

        { "granul",    FnTag::Granular },
        { "freeze",    FnTag::Granular },
        { "stretch",   FnTag::Granular },
        { "scatter",   FnTag::Granular },
        { "glitch",    FnTag::Granular },

        { "pitch",     FnTag::Pitch },
        { "harmon",    FnTag::Pitch },
        { "formant",   FnTag::Pitch },
        { "octav",     FnTag::Pitch },
        { "freqshift", FnTag::Pitch },

        // —— 调制必须在延迟之前，见上面
        { "chorus",    FnTag::Modulation },
        { "flang",     FnTag::Modulation },
        { "tremol",    FnTag::Modulation },
        { "vibrato",   FnTag::Modulation },
        { "ensemble",  FnTag::Modulation },

        { "reverb",    FnTag::Reverb },
        { "verb",      FnTag::Reverb },
        { "hall",      FnTag::Reverb },
        { "plate",     FnTag::Reverb },

        { "delay",     FnTag::Delay },
        { "echo",      FnTag::Delay },

        { "clip",      FnTag::Dynamics },
        { "limit",     FnTag::Dynamics },
        { "maximi",    FnTag::Dynamics },
        { "comp",      FnTag::Dynamics },
        { "ott",       FnTag::Dynamics,   true },
        { "multiband", FnTag::Dynamics },
        { "gate",      FnTag::Dynamics },
        { "expand",    FnTag::Dynamics },
        { "transient", FnTag::Dynamics },

        { "distort",   FnTag::Distortion },
        { "satur",     FnTag::Distortion },
        { "drive",     FnTag::Distortion },
        { "crush",     FnTag::Distortion },
        { "waveshap",  FnTag::Distortion },
        { "fold",      FnTag::Distortion },
        { "tube",      FnTag::Distortion },
        { "tape",      FnTag::Distortion },

        // FabFilter 的型号名里没有功能词，但它太常见了，值得单独认。
        // 同理的还有别家的密码名——那些只能靠人工，见 FnTag 的覆盖率表。
        { "pro-q",     FnTag::EQ },
        { "pro-c",     FnTag::Dynamics },
        { "pro-l",     FnTag::Dynamics },
        { "pro-mb",    FnTag::Dynamics },
        { "pro-ds",    FnTag::Dynamics },
        { "pro-r",     FnTag::Reverb },
        { "pro-g",     FnTag::Dynamics },
        { "saturn",    FnTag::Distortion },
        { "timeless",  FnTag::Delay },

        { "equali",    FnTag::EQ },
        { "eq",        FnTag::EQ,         true },

        { "filter",    FnTag::Filter },
        { "lowpass",   FnTag::Filter },
        { "highpass",  FnTag::Filter },
        { "bandpass",  FnTag::Filter },
        { "comb",      FnTag::Filter,     true },

        { "gain",      FnTag::Utility },
        { "utilit",    FnTag::Utility },
        { "meter",     FnTag::Utility },
        { "analyz",    FnTag::Utility },
        { "analys",    FnTag::Utility },
        { "width",     FnTag::Utility },
    };

    for (const auto& r : rules)
        if (r.wholeWord ? hasToken (r.pattern) : n.contains (r.pattern))
            return r.tag;

    return FnTag::Unknown;
}

FnTag PluginScanner::loadTag (const juce::String& pluginId) const
{
    if (props == nullptr || pluginId.isEmpty())
        return FnTag::Unknown;

    const int raw = props->getIntValue (tagKey (pluginId), (int) FnTag::Unknown);

    // 越界就当没打过。设置文件是纯文本，用户手改坏了不该让界面崩。
    if (raw <= 0 || raw >= kNumFnTags)
        return FnTag::Unknown;

    return (FnTag) raw;
}

void PluginScanner::saveTag (const juce::String& pluginId, FnTag tag)
{
    if (props == nullptr || pluginId.isEmpty())
        return;

    if (tag == FnTag::Unknown)
        props->removeValue (tagKey (pluginId));     // 别留一堆 "= 0"
    else
        props->setValue (tagKey (pluginId), (int) tag);

    props->saveIfNeeded();
}

juce::Array<juce::PluginDescription> PluginScanner::findByTag (FnTag tag) const
{
    juce::Array<juce::PluginDescription> out;

    for (const auto& d : knownPlugins.getTypes())
        if (loadTag (d.createIdentifierString()) == tag)
            out.add (d);

    return out;
}

void PluginScanner::getTagCoverage (int& tagged, int& total) const
{
    tagged = 0;
    total  = 0;

    for (const auto& d : knownPlugins.getTypes())
    {
        ++total;
        if (loadTag (d.createIdentifierString()) != FnTag::Unknown)
            ++tagged;
    }
}

} // namespace pr
