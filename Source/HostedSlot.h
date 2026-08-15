#pragma once

#include "ParamModel.h"
#include "SnapshotBank.h"
#include "HistoryRing.h"
#include "MotionEngine.h"
#include "ChainRunner.h"

namespace pr
{

/** 一个链位持有的全部状态。

    以前这些字段都散在 ParamRouletteProcessor 上（单插件时代）。收成一个结构体
    是为了让「6 个链位」变成 `std::array<HostedSlot, kMaxChain>`，而不是把每个
    字段都复制六份。

    **界面基本不用改，靠的是 processor 把取值接口全部转发到"当前聚焦的链位"**：
    `getModel()` → `chain[focused].model`。这样 PluginEditor 那 1000 行里
    只需要多一行链位选择按钮。

    每个链位有自己独立的：参数模型、勾选表、运动基座、历史。这是必须的——
    一条链上的 EQ 和 disperser 的"我不想随机这些"完全是两回事。
*/
struct HostedSlot
{
    std::unique_ptr<juce::AudioPluginInstance> plugin;
    juce::PluginDescription                    desc;

    juce::Array<ParamInfo> model;
    juce::Array<bool>      checked;

    MotionProfile profile;

    // —— 运动模式的基座。四个字段必须一起改，所以别在别处零散赋值。
    Snapshot     base;
    int          baseProgram    = -1;   ///< 锁定基座时插件在哪个 program
    juce::String baseName;              ///< 锁定时那个 program 的名字
    int          baseGeneration = 0;    ///< 重锁次数，界面上显示成 #N

    int contourGroups       = 0;        ///< 加载时探到的可识别轮廓组数
    int lastContoursWritten = 0;

    bool bypassed = false;

    /** 子插件要的声道数比宿主给的多时用的暂存区（合成器挂在效果器版上会这样）。
        在 prepareSlot 里分配好，音频线程只读不分配。 */
    juce::AudioBuffer<float> scratch;
    int                      channels = 0;

    /** 随机化历史。每链位一份——回退 EQ 不该把 disperser 也拽回去。 */
    HistoryRing history { kMaxHistory, kMaxHistoryBytes };

    static constexpr int    kMaxHistory      = 20;
    /** 只限条数不够：Serum 之类的状态块能到 MB 级，20 份就是几百兆常驻内存。 */
    static constexpr size_t kMaxHistoryBytes = 64u << 20;

    bool isLoaded() const noexcept { return plugin != nullptr; }

    /** 音频线程要的那几个字段。每块现取，不缓存——`bypassed` 随时会变。 */
    ChainLink link() noexcept
    {
        ChainLink l;
        l.plugin   = plugin.get();
        l.scratch  = &scratch;
        l.channels = channels;
        l.bypassed = bypassed;
        return l;
    }

    /** 换插件 / 卸载时把跟"这个具体插件"绑定的东西全清掉。
        **不清 bypassed**：那是用户对这个**位置**的设置，换插件不该重置它。 */
    void resetForNewPlugin()
    {
        model.clear();
        checked.clear();
        profile = {};
        base = Snapshot {};
        baseProgram = -1;
        baseName.clear();
        baseGeneration = 0;
        contourGroups = 0;
        lastContoursWritten = 0;
        history.clear();
    }
};

} // namespace pr
