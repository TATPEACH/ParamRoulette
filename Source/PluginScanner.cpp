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

} // namespace pr
