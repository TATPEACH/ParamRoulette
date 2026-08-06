#include "HostedPluginWindow.h"

namespace pr
{

HostedPluginWindow::HostedPluginWindow (juce::AudioPluginInstance& plugin,
                                        std::function<void()> onCloseRequested)
    : juce::DocumentWindow (plugin.getName(),
                            juce::LookAndFeel::getDefaultLookAndFeel()
                                .findColour (juce::ResizableWindow::backgroundColourId),
                            juce::DocumentWindow::closeButton),
      onClose (std::move (onCloseRequested))
{
    setUsingNativeTitleBar (true);

    juce::AudioProcessorEditor* ui = nullptr;

    if (plugin.hasEditor())
        ui = plugin.createEditorIfNeeded();

    if (ui == nullptr)
        ui = new juce::GenericAudioProcessorEditor (plugin);

    // setContentOwned 接管 ui 的所有权；析构里的 clearContentComponent() 会删掉它，
    // 顺带触发 AudioProcessor::editorBeingDeleted，把子插件的 activeEditor 清干净。
    setContentOwned (ui, true);
    setResizable (ui->isResizable(), false);

    centreWithSize (getWidth(), getHeight());
    setVisible (true);
}

HostedPluginWindow::~HostedPluginWindow()
{
    clearContentComponent();
}

void HostedPluginWindow::closeButtonPressed()
{
    if (onClose != nullptr)
        onClose();      // 通常会把 this 删掉，后面不能再有代码
}

} // namespace pr
