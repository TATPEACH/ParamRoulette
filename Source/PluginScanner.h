#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "Utf8.h"

namespace pr
{

/** VST3 + VST2 扫描 + 插件表持久化 + 每插件的排除表记忆。

    **架构和格式是两件事，别混。** 注册了 VST2 也不代表能加载 32 位插件：
    64 位进程装不下 32 位 DLL，这是 Windows 加载器层面的限制。所以 fauna
    （32 位 VST2）只有 32 位构建能碰。设置文件按架构分开存，见 .cpp 里的
    `settingsName()`。
*/
class PluginScanner
{
public:
    PluginScanner();
    ~PluginScanner();

    juce::AudioPluginFormatManager& getFormatManager() noexcept { return formatManager; }
    juce::KnownPluginList&          getKnownPlugins()  noexcept { return knownPlugins; }
    juce::PropertiesFile*           getProperties()    noexcept { return props.get(); }

    /** 把插件表写回磁盘。扫描完 / 关窗时调。 */
    void savePluginList();

    /** 供 UI 用的扫描界面。 */
    std::unique_ptr<juce::PluginListComponent> createListComponent();

    //==============================================================================
    // 每插件的排除表。移植 Lua 版 SetExtState(persist=true) 的行为：
    // 换了工程、换了实例，同一个插件的"我不想随机这些"依然记得。
    // 存"被排除的下标"而不是全量布尔——三百个参数里通常只排除几个。

    /** 这个插件有没有存过排除表。
        必须和 loadExclusions 分开问：空列表有两种含义——"没记过"（该用默认排除规则）
        和"记过，而且用户把默认排除的那几个也勾上了"。混为一谈的话，用户手动勾上
        wet/dry 之后下次加载又会被默认规则打回去。 */
    bool             hasExclusions (const juce::String& pluginId) const;
    juce::Array<int> loadExclusions (const juce::String& pluginId) const;
    void             saveExclusions (const juce::String& pluginId, const juce::Array<int>& excluded);

private:
    juce::AudioPluginFormatManager        formatManager;
    juce::KnownPluginList                 knownPlugins;
    std::unique_ptr<juce::PropertiesFile> props;
    juce::File                            deadMansPedal;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginScanner)
};

} // namespace pr
