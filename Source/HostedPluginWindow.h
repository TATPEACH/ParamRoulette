#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace pr
{

/** 被托管插件界面的独立浮动窗口。

    **生命周期是这里唯一要小心的事**：窗口持有的是子插件自己造的 editor，
    editor 里到处是指回子插件的裸指针。所以销毁顺序必须是
    「先关窗 → 再放实例」，反过来必崩。宿主换插件前一定要先把这个窗口 reset 掉。

    没有自带界面的插件（hasEditor() == false）退回 JUCE 的通用旋钮面板，
    总比"点了没反应"强。
*/
class HostedPluginWindow final : public juce::DocumentWindow
{
public:
    /** onCloseRequested 在用户点窗口右上角关闭时触发。
        回调里通常就是把持有本窗口的 unique_ptr reset 掉——
        所以回调返回后**不能**再碰 this，这里也确实没有后续代码。 */
    HostedPluginWindow (juce::AudioPluginInstance& plugin,
                        std::function<void()> onCloseRequested);

    ~HostedPluginWindow() override;

    void closeButtonPressed() override;

private:
    std::function<void()> onClose;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HostedPluginWindow)
};

} // namespace pr
