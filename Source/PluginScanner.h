#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "Utf8.h"

#include <map>

namespace pr
{

/** 插件的功能标签。

    **这是"随机效果器链"那件事的数据地基。** 想让配方（比如
    `modern rumble = 任意全通滤波器 + disperser + 任意修饰性效果器`）自动填满，
    首先得知道每个插件是干什么的。

    为什么必须人工打标签、不能自动推断（本机实测的数字）：

    | 手段 | 覆盖率 |
    |---|---|
    | VST3 声明的 `Sub Categories` | **不到 10%** —— 643 个 VST3 里只有 62 个是 bundle 形式，其中 68% 才有 moduleinfo.json |
    | 名字关键词 | 31% |
    | **完全看不出功能的** | **69%（525 个）** —— `ACDD` / `ACEH` / `ACGQ` 这类 Acustica 式密码名 |

    所以 `guessTag` 只是个"先猜一轮省点事"的辅助，剩下的靠用户自己点。 */
enum class FnTag
{
    Unknown = 0,
    EQ,             ///< 均衡
    Filter,         ///< 滤波器（低通/高通/带通/梳状）
    Allpass,        ///< 全通 —— 只改相位不改幅度谱。modern rumble 那类配方的核心
    Phase,          ///< 相位失真 / disperser / phaser
    Delay,
    Reverb,
    Modulation,     ///< chorus / flanger / tremolo / vibrato
    Dynamics,       ///< 压缩 / 限制 / 门限 / OTT / 多段
    Distortion,     ///< 失真 / 饱和 / 削波 / bitcrush
    Pitch,          ///< 变调 / 移频 / 谐波
    Granular,       ///< 颗粒 / 时间拉伸 / freeze
    Utility         ///< 工具（增益 / 声场 / 表 / 分析）
};

/** 给界面用的中文标签名。 */
juce::String fnTagLabel (FnTag);

/** 一共几个标签（含 Unknown）。界面拿它建下拉。 */
inline constexpr int kNumFnTags = (int) FnTag::Utility + 1;

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

    //==============================================================================
    // 每插件的**逐参数抖动幅度**（音色层用）。键是参数下标，值是归一化幅度。
    //
    // 和排除表一样按插件持久化，但**只存设过的那几个**：一个插件几百个参数，
    // 绝大多数用角色默认档就够了，全量写只是把设置文件撑大。
    bool                 hasSpans (const juce::String& pluginId) const;
    std::map<int, float> loadSpans (const juce::String& pluginId) const;
    void                 saveSpans (const juce::String& pluginId, const std::map<int, float>&);

    //==============================================================================
    // 每插件的功能标签。见 FnTag 的说明。
    FnTag loadTag (const juce::String& pluginId) const;
    void  saveTag (const juce::String& pluginId, FnTag);

    /** 按插件名猜一个标签。猜不出返回 Unknown。**只能盖住约三成**，
        剩下的靠人工——见 FnTag 的那张覆盖率表。 */
    static FnTag guessTag (const juce::String& pluginName);

    /** 所有已扫描到的插件里，带某个标签的有哪些。配方引擎将来靠它选插件。 */
    juce::Array<juce::PluginDescription> findByTag (FnTag) const;

    /** 已打标签的插件数 / 已扫描到的总数。界面用来显示进度。 */
    void getTagCoverage (int& tagged, int& total) const;

private:
    juce::AudioPluginFormatManager        formatManager;
    juce::KnownPluginList                 knownPlugins;
    std::unique_ptr<juce::PropertiesFile> props;
    juce::File                            deadMansPedal;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginScanner)
};

} // namespace pr
