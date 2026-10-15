#include "PluginEditor.h"
#include "GridEditor.h"
#include "HardwareEditor.h"
#include "PluginTagView.h"

namespace pr
{

namespace
{
    /** 通用的工具窗口壳子。扫描界面和标签界面都用它——
        两个都是"一个内容组件 + 一个关闭按钮"，没必要各写一个类。 */
    class ToolWindow final : public juce::DocumentWindow
    {
    public:
        ToolWindow (const juce::String& title,
                    std::unique_ptr<juce::Component> content,
                    int w, int h,
                    juce::LookAndFeel& lf,
                    juce::Colour background,
                    std::function<void()> onCloseRequested)
            : juce::DocumentWindow (title, background, juce::DocumentWindow::closeButton),
              onClose (std::move (onCloseRequested))
        {
            // 顶层窗口不会从 editor 继承 LookAndFeel，得自己设，不然里面又是默认的蓝灰色
            setLookAndFeel (&lf);
            setUsingNativeTitleBar (true);
            setContentOwned (content.release(), false);
            setResizable (true, false);
            centreWithSize (w, h);
            setVisible (true);
        }

        ~ToolWindow() override
        {
            clearContentComponent();
            setLookAndFeel (nullptr);
        }

        /** 换皮肤时窗口不关（扫描可能正跑到一半），原地换外观。 */
        void applySkin (juce::LookAndFeel& lf, juce::Colour background)
        {
            setLookAndFeel (&lf);
            setBackgroundColour (background);
        }

        void closeButtonPressed() override
        {
            if (onClose != nullptr)
                onClose();      // 通常会把 this 删掉，后面不能再有代码
        }

    private:
        std::function<void()> onClose;
    };

    juce::Colour toolBackground (Skin s)          { return s == Skin::hardware ? hw::panel : theme::canvas; }
    PluginTagView::Look tagLook (Skin s)          { return s == Skin::hardware ? PluginTagView::Look::hardware : PluginTagView::Look::grid; }
}

//==============================================================================
ParamRouletteEditor::ParamRouletteEditor (ParamRouletteProcessor& p)
    : juce::AudioProcessorEditor (&p),
      proc (p)
{
    // 换插件前 processor 会**同步**调这个把子插件窗口关掉。装在这里、dtor 里第一件事就摘掉。
    proc.closeHostedWindow = [this] (int chainSlot) { closeHostedWindows (chainSlot); };

    // 宿主把焦点给 editor 本身的时候，快捷键也要能用（keyPressed 会转给皮肤）
    setWantsKeyboardFocus (true);

    skin = loadSkinPreference();

    setResizable (true, true);
    applySizeLimits (skin);
    createView (skin);

    const auto sz = defaultSize (skin);
    setSize (sz.x, sz.y);

    // 尺寸定了以后才开始第一次 rebuild——和皮肤还是 editor 本体时的顺序一样。
    view->start();
}

ParamRouletteEditor::~ParamRouletteEditor()
{
    // 顺序不能变：钩子里捕获了 this，先摘钩子再拆自己。
    proc.closeHostedWindow = nullptr;
    closeHostedWindows (-1);

    scanWindow.reset();
    tagWindow.reset();

    setLookAndFeel (nullptr);
    view.reset();
}

juce::Point<int> ParamRouletteEditor::defaultSize (Skin s)
{
    return s == Skin::hardware ? juce::Point<int> { 1280, 820 }
                               : juce::Point<int> { 1240, 780 };
}

juce::Point<int> ParamRouletteEditor::minimumSize (Skin s)
{
    return s == Skin::hardware ? juce::Point<int> { 1160, 760 }
                               : juce::Point<int> { 1100, 700 };
}

void ParamRouletteEditor::applySizeLimits (Skin s)
{
    // setResizeLimits 会把当前尺寸夹进新范围：从 Grid 的最小窗口换到硬件皮肤时窗口会长大一点
    const auto mn = minimumSize (s);
    setResizeLimits (mn.x, mn.y, 2400, 1600);
}

juce::LookAndFeel_V4& ParamRouletteEditor::lookAndFeelFor (Skin s)
{
    if (s == Skin::hardware)
        return hwLnf;

    return gridLnf;
}

//==============================================================================
void ParamRouletteEditor::createView (Skin s)
{
    view.reset();

    EditorHost& self = *this;     // 私有继承：转换只能在这里做，make_unique 里面做不了

    if (s == Skin::hardware)
        view = std::make_unique<HardwareEditor> (proc, self);
    else
        view = std::make_unique<GridEditor> (proc, self);

    // 右下角的拖拽角归外壳（AudioProcessorEditor 建的），它用外壳的 LookAndFeel 画——
    // 跟着皮肤换，否则硬件皮肤上会出现一个 Grid 风格的角。
    setLookAndFeel (&lookAndFeelFor (s));

    // 插到最底下：拖拽角是 always-on-top 的，但显式放在下面更稳。
    addAndMakeVisible (*view, 0);
    view->setBounds (getLocalBounds());
}

void ParamRouletteEditor::setSkin (Skin s)
{
    if (view != nullptr && s == skin)
        return;

    skin = s;
    applySizeLimits (s);
    createView (s);
    view->start();
    saveSkinPreference (s);

    // 开着的工具窗口跟着换外观
    for (auto* w : { scanWindow.get(), tagWindow.get() })
        if (auto* tw = dynamic_cast<ToolWindow*> (w))
            tw->applySkin (lookAndFeelFor (s), toolBackground (s));

    if (tagWindow != nullptr)
        if (auto* tv = dynamic_cast<PluginTagView*> (tagWindow->getContentComponent()))
            tv->setLook (tagLook (s));

    // 键盘焦点交给新皮肤，R / Ctrl+Z 这些快捷键换完马上能用
    if (isShowing())
        view->grabKeyboardFocus();
}

void ParamRouletteEditor::requestSkin (Skin s)
{
    // 调用者通常是旧皮肤上的一个按钮：在它自己的点击回调里把它删掉不安全，推迟一拍。
    juce::Component::SafePointer<ParamRouletteEditor> safe (this);
    juce::MessageManager::callAsync ([safe, s]
    {
        if (auto* self = safe.getComponent())
            self->setSkin (s);
    });
}

juce::File ParamRouletteEditor::skinFile() const
{
    // 跟着设置文件放在同一个目录，名字去掉架构后缀：设置文件是 32 / 64 位各一份的
    // （插件表不一样），但"我喜欢哪套外观"是人的偏好，fauna 那个 32 位实例里换了，
    // 64 位的也该跟着换。截图工具把设置文件重定向到了 -uisnap，这个也就跟着重定向，
    // 不会去改用户真正的偏好。
    if (auto* props = proc.getScanner().getProperties())
    {
        const auto settings = props->getFile();
        auto name = settings.getFileNameWithoutExtension();

        for (const auto* arch : { "-x86", "-x64" })
            if (name.endsWith (arch))
                name = name.dropLastCharacters (4);

        return settings.getSiblingFile (name + ".skin");
    }

    return {};
}

Skin ParamRouletteEditor::loadSkinPreference() const
{
    // 每次打开界面现读（几个字节）：同一个 REAPER 里可能同时有好几个实例、好几个 DLL
    // （效果器版 / 乐器版 / 32 位桥里那个），谁换了皮肤，别的实例下次打开界面就跟上。
    const auto f = skinFile();
    return f.existsAsFile() && f.loadFileAsString().trim() == "hardware" ? Skin::hardware : Skin::grid;
}

void ParamRouletteEditor::saveSkinPreference (Skin s)
{
    if (const auto f = skinFile(); f != juce::File())
        f.replaceWithText (s == Skin::hardware ? "hardware" : "grid");
}

//==============================================================================
void ParamRouletteEditor::resized()
{
    if (view != nullptr)
        view->setBounds (getLocalBounds());
}

bool ParamRouletteEditor::keyPressed (const juce::KeyPress& k)
{
    // 宿主有时把焦点给 editor 本身而不是里面的皮肤，快捷键照样转过去
    return view != nullptr && view->keyPressed (k);
}

//==============================================================================
void ParamRouletteEditor::openHostedWindow (int slot)
{
    if (! juce::isPositiveAndBelow (slot, kMaxChain))
        return;

    auto& win = hostedWindows[(size_t) slot];

    if (win != nullptr)
    {
        win->toFront (true);
        return;
    }

    auto* p = proc.getSlotPlugin (slot);
    if (p == nullptr)
        return;

    juce::Component::SafePointer<ParamRouletteEditor> safe (this);

    win = std::make_unique<HostedPluginWindow> (*p,
                                                [safe, slot]
                                                {
                                                    if (auto* self = safe.getComponent())
                                                    {
                                                        self->closeHostedWindows (slot);

                                                        if (self->view != nullptr)
                                                            self->view->windowsChanged();
                                                    }
                                                });
    if (view != nullptr)
        view->windowsChanged();
}

void ParamRouletteEditor::closeHostedWindows (int chainSlot)
{
    if (chainSlot < 0)
    {
        for (auto& w : hostedWindows)
            w.reset();

        return;
    }

    if (juce::isPositiveAndBelow (chainSlot, kMaxChain))
        hostedWindows[(size_t) chainSlot].reset();
}

bool ParamRouletteEditor::isHostedWindowOpen (int chainSlot) const
{
    return juce::isPositiveAndBelow (chainSlot, kMaxChain)
        && hostedWindows[(size_t) chainSlot] != nullptr;
}

//==============================================================================
void ParamRouletteEditor::showScanWindow()
{
    if (scanWindow != nullptr)
    {
        scanWindow->toFront (true);
        return;
    }

    juce::Component::SafePointer<ParamRouletteEditor> safe (this);

    scanWindow = std::make_unique<ToolWindow> ("插件扫描"_u8,
                                               proc.getScanner().createListComponent(),
                                               720, 500, lookAndFeelFor (skin), toolBackground (skin),
                                               [safe]
                                               {
                                                   if (auto* self = safe.getComponent())
                                                   {
                                                       self->proc.getScanner().savePluginList();
                                                       self->scanWindow.reset();
                                                   }
                                               });
}

void ParamRouletteEditor::showTagWindow()
{
    if (tagWindow != nullptr)
    {
        tagWindow->toFront (true);
        return;
    }

    juce::Component::SafePointer<ParamRouletteEditor> safe (this);

    tagWindow = std::make_unique<ToolWindow> ("插件功能标签"_u8,
                                              std::make_unique<PluginTagView> (proc.getScanner(), tagLook (skin)),
                                              660, 580, lookAndFeelFor (skin), toolBackground (skin),
                                              [safe]
                                              {
                                                  if (auto* self = safe.getComponent())
                                                  {
                                                      self->tagWindow.reset();

                                                      // 标签可能改了，信号链上的颜色要跟着变
                                                      if (self->view != nullptr)
                                                          self->view->windowsChanged();
                                                  }
                                              });
}

} // namespace pr
