#pragma once

#include "EditorBase.h"
#include "HostedPluginWindow.h"
#include "GridLookAndFeel.h"
#include "HardwareLookAndFeel.h"
#include "RackLookAndFeel.h"

namespace pr
{

/** editor 外壳——DAW 拿到的就是它。

    自己几乎不画东西，只做这几件事：
      1. 持有**当前皮肤**（`GridEditor` 或 `HardwareEditor`），换皮肤时整个删掉重建
      2. 持有子插件的浮动窗口（每链位一个）和两个工具窗口（扫描 / 标签）。
         它们归外壳而不归皮肤：换外观时不会被关掉——扫描可能正跑到一半
      3. 持有两套 LookAndFeel，和 editor 同寿
      4. 记住用户选的皮肤：存进一个小文件，下次打开插件（包括别的实例）还是这一套

    为什么不让 DAW 重新打开 editor 来换皮肤：插件没有办法要求宿主重建 editor。
    所以 editor 本身是个不变的壳，皮肤是它里面可以换的一层。 */
class ParamRouletteEditor final : public juce::AudioProcessorEditor,
                                  private EditorHost
{
public:
    explicit ParamRouletteEditor (ParamRouletteProcessor&);
    ~ParamRouletteEditor() override;

    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    /** 立即换皮肤（同步）。窗口尺寸保持不变（会夹进新皮肤的尺寸范围）。 */
    void setSkin (Skin);
    Skin getSkin() const noexcept { return skin; }

    /** 各套皮肤的默认尺寸 / 最小尺寸。硬件 / Rack 皮肤的旋钮和表头比 Grid 的控件占地方，
        最小尺寸大一点（1160×760）——再小旋钮就得缩到看不清，那就违背拟物的初衷了。
        Rack 的默认尺寸正好是机架 100% 缩放（85 HP × 两排 3U + 菜单栏）。 */
    static juce::Point<int> defaultSize (Skin);
    static juce::Point<int> minimumSize (Skin);

private:
    //== EditorHost ================================================================
    void openHostedWindow (int chainSlot) override;
    void closeHostedWindows (int chainSlot) override;
    bool isHostedWindowOpen (int chainSlot) const override;
    void hostedWindowsMoved (int from, int to) override;
    void showScanWindow() override;
    void showTagWindow() override;
    void requestSkin (Skin) override;
    juce::LookAndFeel_V4& lookAndFeelFor (Skin) override;

    void createView (Skin);
    void applySizeLimits (Skin);

    /** 皮肤偏好单独存一个小文件（和插件设置文件放一起），不放进共享的设置文件：
        同一个进程里每个插件实例都各自持有一份设置文件的内存副本，实例 B 存别的东西时
        会把它那份过时的"皮肤 = grid"一起写回去，把实例 A 刚换的皮肤冲掉。 */
    juce::File skinFile() const;
    Skin loadSkinPreference() const;
    void saveSkinPreference (Skin);

    ParamRouletteProcessor& proc;

    // 两套 LookAndFeel 声明在所有组件**前面** = 最后析构：工具窗口、对话框、皮肤都挂在
    // 它们上面，LookAndFeel 先死的话那些组件手里就是悬空引用。
    GridLookAndFeel     gridLnf;
    HardwareLookAndFeel hwLnf;
    RackLookAndFeel     rackLnf;

    std::array<std::unique_ptr<HostedPluginWindow>, kMaxChain> hostedWindows;
    std::unique_ptr<juce::DocumentWindow> scanWindow;
    std::unique_ptr<juce::DocumentWindow> tagWindow;
    std::unique_ptr<EditorBase> view;
    Skin skin = Skin::grid;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ParamRouletteEditor)
};

} // namespace pr
