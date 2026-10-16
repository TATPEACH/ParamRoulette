// 界面截图工具。
//
// 为什么要有它：这个插件的界面只能在 DAW 里看到，而我（写代码的这一方）看不到
// 原生窗口。没有它的话，所有视觉改动都是盲改——圆角半径、字号、配色对不对，
// 只能等你装进 REAPER 才知道。
//
// 它构造**真的** processor 和 editor（不是 mock），通过 PR_TEST_HOOKS 那个
// 测试专用入口同步装几个假插件（参数名照着 fauna 起），然后把 editor 整个画进
// 一张 PNG。正式构建里那个入口不存在。
//
// 设置文件重定向到 ParamRoulette-uisnap.settings（PR_SETTINGS_NAME_OVERRIDE），
// 所以不会碰你真正的插件表 / 勾选记忆 / 标签。
//
//   pr_uisnap.exe [输出目录]          默认 build/ui_snap
//
// 三套皮肤各出一组：01~05 是 Grid，hw_ 开头的是硬件拟物风，rk_ 开头的是 VCV Rack 风。
// Grid 那几张改共享代码前后要**逐像素一致**（种子固定），拿来确认改一套皮肤没有碰到另一套。

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "GridLookAndFeel.h"
#include "HardwareLookAndFeel.h"
#include "HardwareEditor.h"
#include "RackLookAndFeel.h"
#include "RackEditor.h"
#include "PluginTagView.h"

#include <iostream>

using namespace juce;
using pr::operator""_u8;

namespace
{
    void line (const String& s = {}) { std::cout << s.toRawUTF8() << std::endl; }

    void pump (int ms)
    {
        MessageManager::getInstance()->runDispatchLoopUntil (ms);
    }

    /** 在组件树里找第一个 T（深度优先）。 */
    template <typename T>
    T* findChild (Component& root)
    {
        for (auto* c : root.getChildren())
        {
            if (auto* t = dynamic_cast<T*> (c))
                return t;
            if (auto* t = findChild<T> (*c))
                return t;
        }
        return nullptr;
    }

    //==============================================================================
    /** 假插件。用 JUCE 原生参数，所以开关 / 档位 / 连续三类都能被正确分类。
        状态块就是参数值的 float 数组，快照 / 历史 / 基座都能正常往返。 */
    class FakePlugin final : public AudioPluginInstance
    {
    public:
        struct Spec
        {
            String name;
            enum Type { Cont, Bool, Choice } type = Cont;
            float  def = 0.5f;
            String unit;            ///< 连续量的显示单位（"Hz" / "dB" / "%" / ""）
            float  lo = 0.0f, hi = 1.0f;
            StringArray choices;
        };

        FakePlugin (String nm, String fmt, bool synth, const std::vector<Spec>& specs)
            : AudioPluginInstance (synth ? BusesProperties().withOutput ("Out", AudioChannelSet::stereo(), true)
                                         : BusesProperties().withInput  ("In",  AudioChannelSet::stereo(), true)
                                                            .withOutput ("Out", AudioChannelSet::stereo(), true)),
              pluginName (std::move (nm)), format (std::move (fmt)), isSynth (synth)
        {
            int n = 0;
            for (const auto& s : specs)
            {
                const ParameterID id { "p" + String (n++), 1 };

                // AudioPluginInstance 把 addParameter 藏成了 private（它要求宿主侧参数都是
                // HostedParameter）。我们的代码只走 AudioProcessorParameter 那层接口，
                // 从不调 getHostedParameter，所以限定名调基类版本是安全的。
                switch (s.type)
                {
                    case Spec::Bool:
                        AudioProcessor::addParameter (new AudioParameterBool (id, s.name, s.def >= 0.5f));
                        break;

                    case Spec::Choice:
                        AudioProcessor::addParameter (new AudioParameterChoice (id, s.name, s.choices,
                                                                jlimit (0, s.choices.size() - 1,
                                                                        roundToInt (s.def * (float) (s.choices.size() - 1)))));
                        break;

                    case Spec::Cont:
                    default:
                    {
                        const auto unit = s.unit;
                        const float lo = s.lo, hi = s.hi;
                        auto attrs = AudioParameterFloatAttributes().withStringFromValueFunction (
                            [unit, lo, hi] (float v, int)
                            {
                                const float x = lo + (hi - lo) * v;
                                if (unit == "Hz")  return x >= 1000.0f ? String (x / 1000.0f, 2) + " kHz"
                                                                       : String (x, 0) + " Hz";
                                if (unit == "dB")  return String (x, 1) + " dB";
                                if (unit == "%")   return String (x * 100.0f, 1) + " %";
                                if (unit == "ms")  return String (x, 0) + " ms";
                                return String (x, 3);
                            });

                        AudioProcessor::addParameter (new AudioParameterFloat (id, s.name, NormalisableRange<float> (0.0f, 1.0f),
                                                               s.def, attrs));
                        break;
                    }
                }
            }
        }

        const String getName() const override              { return pluginName; }
        void prepareToPlay (double, int) override           {}
        void releaseResources() override                    {}
        void processBlock (AudioBuffer<float>&, MidiBuffer&) override {}
        double getTailLengthSeconds() const override        { return 0.0; }
        bool acceptsMidi() const override                   { return isSynth; }
        bool producesMidi() const override                  { return false; }
        AudioProcessorEditor* createEditor() override       { return nullptr; }
        bool hasEditor() const override                     { return false; }

        // 给几个 program，preset 下拉才有东西可显示
        int getNumPrograms() override                       { return isSynth ? 12 : 1; }
        int getCurrentProgram() override                    { return isSynth ? 9 : 0; }
        void setCurrentProgram (int) override               {}
        const String getProgramName (int i) override
        {
            static const char* names[] = { "blank", "bird 1", "bird 2", "bird 3", "bird 4", "frog",
                                           "insect", "owl", "crow", "bird 5", "whale", "wolf" };
            return isSynth ? String (names[jlimit (0, 11, i)]) : String ("Default");
        }
        void changeProgramName (int, const String&) override {}

        void getStateInformation (MemoryBlock& mb) override
        {
            MemoryOutputStream out (mb, false);
            for (auto* p : getParameters())
                out.writeFloat (p->getValue());
        }

        void setStateInformation (const void* data, int size) override
        {
            MemoryInputStream in (data, (size_t) size, false);
            for (auto* p : getParameters())
                if (! in.isExhausted())
                    p->setValueNotifyingHost (in.readFloat());
        }

        void fillInPluginDescription (PluginDescription& d) const override
        {
            d.name              = pluginName;
            d.descriptiveName   = pluginName;
            d.pluginFormatName  = format;
            d.manufacturerName  = "Demo";
            d.category          = isSynth ? "Synth" : "Fx";
            d.fileOrIdentifier  = "demo://" + pluginName;
            d.uniqueId          = pluginName.hashCode();
            d.deprecatedUid     = pluginName.hashCode();
            d.isInstrument      = isSynth;
            d.numInputChannels  = isSynth ? 0 : 2;
            d.numOutputChannels = 2;
        }

        PluginDescription describe() const
        {
            PluginDescription d;
            fillInPluginDescription (d);
            return d;
        }

    private:
        String pluginName, format;
        bool   isSynth;
    };

    using Spec = FakePlugin::Spec;

    std::unique_ptr<FakePlugin> makeFauna()
    {
        // 名字照着 fauna 的参数表起，这样角色识别 / 运动 profile 那一行也有真实内容
        std::vector<Spec> s;
        for (int g = 1; g <= 4; ++g)
        {
            const String eg = "eg" + String (g);
            s.push_back ({ eg + " amt",   Spec::Cont, 0.3f, "%" });
            s.push_back ({ eg + " inv",   Spec::Bool, 0.0f });
            s.push_back ({ eg + " rate",  Spec::Cont, 0.55f, "%" });
            s.push_back ({ eg + " stages",Spec::Cont, 0.1f });
            s.push_back ({ eg + " repeat",Spec::Bool, 0.0f });
            s.push_back ({ eg + ".dest",  Spec::Choice, 0.2f, {}, 0, 1,
                           { "off", "len", "tens", "lp.f", "mem x", "mem y", "noise" } });
        }
        s.push_back ({ "len",        Spec::Cont, 0.42f });
        s.push_back ({ "len scale",  Spec::Cont, 0.5f });
        s.push_back ({ "lp.f",       Spec::Cont, 0.66f, "Hz", 20.0f, 18000.0f });
        s.push_back ({ "mem x pos",  Spec::Cont, 0.31f, "%" });
        s.push_back ({ "mem y pos",  Spec::Cont, 0.74f, "%" });
        s.push_back ({ "n.hp",       Spec::Cont, 0.2f, "Hz", 20.0f, 18000.0f });
        s.push_back ({ "n.lp",       Spec::Cont, 0.8f, "Hz", 20.0f, 18000.0f });
        for (int r = 1; r <= 4; ++r)
            for (int k = 1; k <= 5; ++k)
                s.push_back ({ "r" + String (r) + "-" + String (k), Spec::Cont, 0.5f });
        s.push_back ({ "send1.amt",  Spec::Cont, 0.4f, "%" });
        s.push_back ({ "spl.1.lp",   Spec::Cont, 0.6f, "Hz", 20.0f, 18000.0f });
        s.push_back ({ "tens",       Spec::Cont, 0.85f });
        s.push_back ({ "loud",       Spec::Bool, 1.0f });
        s.push_back ({ "amp",        Spec::Cont, 0.7f, "dB", -48.0f, 6.0f });
        s.push_back ({ "trans",      Spec::Cont, 0.5f });

        return std::make_unique<FakePlugin> ("fauna", "VST", true, s);
    }

    std::unique_ptr<FakePlugin> makeDisperser()
    {
        return std::make_unique<FakePlugin> ("Disperser", "VST3", false, std::vector<Spec> {
            { "Amount",    Spec::Cont, 0.6f, "%" },
            { "Frequency", Spec::Cont, 0.35f, "Hz", 20.0f, 20000.0f },
            { "Pinch",     Spec::Cont, 0.5f, "%" },
            { "Mix",       Spec::Cont, 1.0f, "%" },
            { "Bypass",    Spec::Bool, 0.0f } });
    }

    std::unique_ptr<FakePlugin> makeEq()
    {
        std::vector<Spec> s;
        for (int b = 1; b <= 6; ++b)
        {
            s.push_back ({ "Band " + String (b) + " Frequency", Spec::Cont, 0.15f * (float) b, "Hz", 10.0f, 30000.0f });
            s.push_back ({ "Band " + String (b) + " Gain",      Spec::Cont, 0.5f, "dB", -30.0f, 30.0f });
            s.push_back ({ "Band " + String (b) + " Q",         Spec::Cont, 0.4f });
            s.push_back ({ "Band " + String (b) + " Shape",     Spec::Choice, 0.0f, {}, 0, 1,
                           { "Bell", "Low Shelf", "Low Cut", "High Shelf", "High Cut", "Notch" } });
        }
        s.push_back ({ "Output Level", Spec::Cont, 0.5f, "dB", -36.0f, 36.0f });
        return std::make_unique<FakePlugin> ("FabFilter Pro-Q 4", "VST3", false, s);
    }

    //==============================================================================
    bool render (Component& c, const File& file, float scale)
    {
        const int w = roundToInt ((float) c.getWidth()  * scale);
        const int h = roundToInt ((float) c.getHeight() * scale);

        // 显式用软件图像：JUCE 8 在 Windows 上默认走 Direct2D，那种图像读回来要
        // 映射 GPU 内存，headless 下没必要冒这个险。
        Image img (Image::ARGB, w, h, true, SoftwareImageType());
        {
            Graphics g (img);
            g.addTransform (AffineTransform::scale (scale));
            c.paintEntireComponent (g, true);
        }

        file.deleteFile();
        FileOutputStream os (file);
        PNGImageFormat png;
        const bool ok = os.openedOk() && png.writeImageToStream (img, os);
        line (String (ok ? "  wrote " : "  FAILED ") + file.getFullPathName()
              + "  (" + String (w) + "x" + String (h) + ")");
        return ok;
    }

    struct Rig
    {
        std::unique_ptr<pr::ParamRouletteProcessor> proc;
        std::unique_ptr<AudioProcessorEditor>       editor;

        ~Rig()
        {
            editor.reset();     // editor 必须先死：它的析构要访问 processor
            proc.reset();
        }
    };

    /** 典型的工作状态：链上三个插件、fauna 聚焦、随机过一次、存了几个快照。 */
    void populate (pr::ParamRouletteProcessor& p, bool motion)
    {
        auto fauna = makeFauna();
        auto disp  = makeDisperser();
        auto eq    = makeEq();

        const auto dF = fauna->describe(), dD = disp->describe(), dE = eq->describe();

        // 标签存进截图专用的设置文件，链条上的模块才有颜色
        auto& sc = p.getScanner();
        sc.saveTag (dF.createIdentifierString(), pr::FnTag::Unknown);
        sc.saveTag (dD.createIdentifierString(), pr::FnTag::Phase);
        sc.saveTag (dE.createIdentifierString(), pr::FnTag::EQ);

        p.installForTesting (0, std::move (fauna), dF);
        p.installForTesting (1, std::move (disp),  dD);
        p.installForTesting (3, std::move (eq),    dE);

        p.setSlotBypassed (3, false);
        p.setFocusedSlot (1);
        p.setFocusedSlot (0);

        // 存两三个快照、随机一次，这样「旧 → 新」和 morph 都有东西可看
        p.captureSlot (0);
        p.randomizeNow();
        p.captureSlot (1);
        p.randomizeNow();
        if (motion)
            p.captureSlot (2);

        p.setMotionMode (motion);
        p.randomizeNow();
    }
}

//==============================================================================
int main (int argc, char** argv)
{
    ScopedJuceInitialiser_GUI juceInit;

    const File outDir = argc > 1 ? File::getCurrentWorkingDirectory().getChildFile (argv[1])
                                 : File::getCurrentWorkingDirectory().getChildFile ("build/ui_snap");
    outDir.createDirectory();

    // 确定性：每次从空的截图专用设置文件开始（皮肤偏好也是）
    {
        const auto dir = File::getSpecialLocation (File::userApplicationDataDirectory).getChildFile ("ParamRoulette");
        dir.getChildFile ("ParamRoulette-uisnap.settings").deleteFile();
        dir.getChildFile ("ParamRoulette-uisnap.skin").deleteFile();
        dir.getChildFile ("ParamRoulette-uisnap.rackview").deleteFile();
    }

    line ("ParamRoulette UI snapshot");
    line ("output: " + outDir.getFullPathName());

    {
        const auto names = Font::findAllTypefaceNames();
        for (const auto* fam : { "Noto Sans SC", "Microsoft YaHei UI", "Segoe UI Variable Text" })
            line (String ("font ") + fam + " : "
                  + (names.contains (fam) ? "yes  styles=" + Font::findAllTypefaceStyles (fam).joinIntoString ("/")
                                          : String ("no")));
    }

    // roundTrip：同一个界面先依次换到另外两套皮肤、再换回来，然后才截图。
    // 它必须和直接打开的那张**逐像素一样**——换皮肤不能留下任何残渣。
    // dark：Rack 的深色面板 + 房间调暗；browser：打开 Rack 的模块浏览器。
    struct Scenario { const char* file; bool populated; bool motion; bool twoD; bool minSize; const char* filter; pr::Skin skin;
                      bool roundTrip; bool dark; bool browser; };

    const Scenario scenarios[] =
    {
        { "01_main",      true,  false, false, false, "",   pr::Skin::grid,     false, false, false },
        { "02_motion",    true,  true,  true,  false, "eg", pr::Skin::grid,     false, false, false },
        { "03_empty",     false, false, false, false, "",   pr::Skin::grid,     false, false, false },
        { "04_min",       true,  false, false, true,  "",   pr::Skin::grid,     false, false, false },
        { "hw_01_main",   true,  false, false, false, "",   pr::Skin::hardware, false, false, false },
        { "hw_02_motion", true,  true,  true,  false, "eg", pr::Skin::hardware, false, false, false },
        { "hw_03_empty",  false, false, false, false, "",   pr::Skin::hardware, false, false, false },
        { "hw_04_min",    true,  false, false, true,  "",   pr::Skin::hardware, false, false, false },
        { "rk_01_main",   true,  false, false, false, "",   pr::Skin::rack,     false, false, false },
        { "rk_02_motion", true,  true,  true,  false, "eg", pr::Skin::rack,     false, false, false },
        { "rk_03_empty",  false, false, false, false, "",   pr::Skin::rack,     false, false, false },
        { "rk_04_min",    true,  false, false, true,  "",   pr::Skin::rack,     false, false, false },
        { "rk_07_dark",   true,  true,  false, false, "",   pr::Skin::rack,     false, true,  false },
        { "rk_08_browser",true,  false, false, false, "",   pr::Skin::rack,     false, false, true  },
        { "06_roundtrip_grid", true, false, false, false, "", pr::Skin::grid,     true, false, false },
        { "06_roundtrip_hw",   true, false, false, false, "", pr::Skin::hardware, true, false, false },
        { "06_roundtrip_rk",   true, false, false, false, "", pr::Skin::rack,     true, false, false },
    };

    // 浏览器那一张要有东西可列：塞一串假的已扫描插件，一部分打好标签
    auto addFakeKnownPlugins = [] (pr::PluginScanner& sc)
    {
        const char* names[] = { "Disperser", "FabFilter Pro-Q 4", "ACDD", "ACEH", "Valhalla VintageVerb",
                                "bx_saturator V2", "EchoBoy", "OTT", "kHs Phaser", "GRM Freeze Stereo",
                                "Allpass Network", "ACGQ", "Cluster Delay", "Pitch Monster" };
        const char* brands[] = { "Kilohearts", "FabFilter", "Acustica", "Acustica", "Valhalla DSP", "Plugin Alliance",
                                 "Soundtoys", "Xfer", "Kilohearts", "GRM", "", "Acustica", "Native Instruments", "Eventide" };

        for (int i = 0; i < (int) std::size (names); ++i)
        {
            PluginDescription d;
            d.name = names[i];
            d.manufacturerName = brands[i];
            d.pluginFormatName = String (names[i]).startsWith ("AC") ? "VST" : "VST3";
            d.fileOrIdentifier = "demo://" + String (names[i]);
            d.uniqueId = String (names[i]).hashCode();
            sc.getKnownPlugins().addType (d);

            if (const auto t = pr::PluginScanner::guessTag (names[i]); t != pr::FnTag::Unknown && String (names[i]).length() % 2 == 0)
                sc.saveTag (d.createIdentifierString(), t);
        }
    };

    for (const auto& sc : scenarios)
    {
        line();
        line (String ("== ") + sc.file);

        Rig rig;
        rig.proc = std::make_unique<pr::ParamRouletteProcessor>();
        rig.proc->seedForTesting (20261015);     // 固定种子：同一份代码出的图逐像素一致
        rig.proc->setPlayConfigDetails (2, 2, 48000.0, 512);
        rig.proc->prepareToPlay (48000.0, 512);

        if (sc.populated)
            populate (*rig.proc, sc.motion);

        if (sc.browser)
            addFakeKnownPlugins (rig.proc->getScanner());

        if (sc.dark)
        {
            pr::rk::view().darkPanels     = true;
            pr::rk::view().roomBrightness = 0.38f;
        }

        *rig.proc->pMorphMode = sc.twoD ? 1 : 0;
        *rig.proc->pMorphX    = sc.twoD ? 0.32f : 0.4f;
        *rig.proc->pMorphY    = 0.62f;
        rig.proc->filterText   = sc.filter;
        rig.proc->onlyMatching = String (sc.filter).isNotEmpty();

        rig.editor.reset (rig.proc->createEditorIfNeeded());

        auto* shell = dynamic_cast<pr::ParamRouletteEditor*> (rig.editor.get());

        if (shell == nullptr)
        {
            line ("  FAILED: no editor");
            return 1;
        }

        // 皮肤偏好是进程级的（换了一次之后新开的界面也是那一套），所以每张都显式指定，
        // 尺寸也按那套皮肤的默认值来——和用户第一次在那套皮肤下打开插件时一样。
        shell->setSkin (sc.skin);
        {
            const auto sz = pr::ParamRouletteEditor::defaultSize (sc.skin);
            shell->setSize (sz.x, sz.y);
        }

        if (sc.roundTrip)
        {
            // 依次走过另外两套再回来
            for (const auto other : { pr::Skin::grid, pr::Skin::hardware, pr::Skin::rack })
            {
                if (other == sc.skin)
                    continue;
                pump (150);
                shell->setSkin (other);
            }
            pump (150);
            shell->setSkin (sc.skin);

            // 换到最小尺寸更大的皮肤时窗口会被夹大；换回来以后按这套皮肤的默认尺寸重设
            const auto sz = pr::ParamRouletteEditor::defaultSize (sc.skin);
            shell->setSize (sz.x, sz.y);
        }

        if (sc.minSize)
            if (auto* con = rig.editor->getConstrainer())
                rig.editor->setSize (con->getMinimumWidth(), con->getMinimumHeight());

        // 让 20Hz 定时器、ChangeBroadcaster 的异步回调都跑几轮
        pump (450);

        // 截图里没有音频在跑，VU 指针会停在左端。摆到一个典型读数上（−3 VU），看得出表盘长什么样
        for (auto* c : shell->getChildren())
        {
            if (auto* hwEd = dynamic_cast<pr::HardwareEditor*> (c))
                hwEd->setVuForTesting (sc.populated ? -3.0f : -20.0f);

            // Rack 的信号灯同理：摆一个典型的电平（每往后一位低一点），看得出灯和插头长什么样
            if (auto* rkEd = dynamic_cast<pr::RackEditor*> (c))
            {
                rkEd->setLevelsForTesting (sc.populated ? 0.5f : 0.0f);

                if (sc.browser)
                {
                    rkEd->openBrowserForTesting (2);
                    pump (200);
                }
            }
        }

        render (*rig.editor, outDir.getChildFile (String (sc.file) + ".png"), 1.0f);

        if (sc.dark)
            pr::rk::view() = pr::rk::View {};
    }

    for (const auto look : { pr::PluginTagView::Look::grid, pr::PluginTagView::Look::hardware, pr::PluginTagView::Look::rack })
    {
        const String file = look == pr::PluginTagView::Look::hardware ? "hw_05_tags"
                          : look == pr::PluginTagView::Look::rack     ? "rk_05_tags" : "05_tags";
        line();
        line ("== " + file);

        // 插件标签窗口单独画一张：塞一串假的已扫描插件，一部分预先打好标签
        pr::GridLookAndFeel     gridLf;
        pr::HardwareLookAndFeel hwLf;
        pr::RackLookAndFeel     rkLf;
        pr::PluginScanner sc;

        const char* names[] = { "Disperser", "FabFilter Pro-Q 4", "ACDD", "ACEH", "Valhalla VintageVerb",
                                "bx_saturator V2", "EchoBoy", "OTT", "kHs Phaser", "GRM Freeze Stereo",
                                "Allpass Network", "ACGQ", "Cluster Delay", "Pitch Monster" };

        for (const auto* n : names)
        {
            PluginDescription d;
            d.name = n;
            d.pluginFormatName = String (n).startsWith ("AC") ? "VST" : "VST3";
            d.fileOrIdentifier = "demo://" + String (n);
            d.uniqueId = String (n).hashCode();
            sc.getKnownPlugins().addType (d);

            if (const auto t = pr::PluginScanner::guessTag (n); t != pr::FnTag::Unknown && String (n).length() % 2 == 0)
                sc.saveTag (d.createIdentifierString(), t);
        }

        pr::PluginTagView view (sc, look);
        view.setLookAndFeel (look == pr::PluginTagView::Look::hardware ? static_cast<LookAndFeel*> (&hwLf)
                           : look == pr::PluginTagView::Look::rack     ? static_cast<LookAndFeel*> (&rkLf)
                                                                        : static_cast<LookAndFeel*> (&gridLf));
        view.setSize (660, 560);

        // 「只看未分类」默认是开的，截图要看到彩色标签就把它关掉
        for (auto* c : view.getChildren())
            if (auto* tb = dynamic_cast<ToggleButton*> (c))
                tb->setToggleState (false, sendNotificationSync);

        pump (200);
        render (view, outDir.getChildFile (file + ".png"), 1.0f);
        view.setLookAndFeel (nullptr);
    }

    // Rack 的菜单和提示气泡：它们是单独的顶层窗口，截界面截不到，这里照着 PopupMenu 的
    // 画法把 LookAndFeel 的那几个函数直接调一遍，看样式（视图菜单 + 模块右键菜单 + 两种提示）
    {
        line();
        line ("== rk_09_menus");

        struct Row
        {
            enum Kind { header, item, separator, slider } kind;
            String text, shortcut;
            bool ticked = false, enabled = true, sub = false, hot = false;
            float value = 0.0f;
        };

        class Sheet final : public Component
        {
        public:
            std::vector<std::pair<Rectangle<int>, std::vector<Row>>> menus;
            String tooltip;
            Point<int> tooltipAt;

            void paint (Graphics& g) override
            {
                auto& lf = getLookAndFeel();
                const PopupMenu::Options opts;
                g.fillAll (pr::rk::rackBg);

                for (const auto& [area, rows] : menus)
                {
                    std::vector<int> hs;
                    int total = 0;
                    for (const auto& r : rows)
                    {
                        int iw = 0, ih = 0;
                        if (r.kind == Row::slider)
                            pr::RackMenuSlider (r.text, r.value, r.value, nullptr).getIdealSize (iw, ih);
                        else
                        {
                            lf.getIdealPopupMenuItemSizeWithOptions (r.text, r.kind == Row::separator, -1, iw, ih, opts);
                            if (r.kind == Row::header)
                                ih += ih / 2;       // JUCE 的 HeaderItemComponent 也是这么加的
                        }
                        hs.push_back (ih);
                        total += ih;
                    }

                    const int border = lf.getPopupMenuBorderSizeWithOptions (opts);
                    const auto box = area.withHeight (total + 2 * border);
                    {
                        Graphics::ScopedSaveState s (g);
                        g.setOrigin (box.getPosition());
                        lf.drawPopupMenuBackgroundWithOptions (g, box.getWidth(), box.getHeight(), opts);
                    }

                    int y = box.getY() + border;
                    for (size_t i = 0; i < rows.size(); ++i)
                    {
                        const auto& r = rows[i];
                        const Rectangle<int> local (0, 0, box.getWidth() - 2 * border, hs[i]);
                        Graphics::ScopedSaveState s (g);
                        g.setOrigin ({ box.getX() + border, y });

                        if (r.kind == Row::header)
                            lf.drawPopupMenuSectionHeaderWithOptions (g, local, r.text, opts);
                        else if (r.kind == Row::slider)
                        {
                            pr::RackMenuSlider sl (r.text, r.value, r.value, nullptr);
                            sl.setSize (local.getWidth(), local.getHeight());
                            sl.paint (g);
                        }
                        else
                        {
                            PopupMenu::Item it (r.text);
                            it.isSeparator = r.kind == Row::separator;
                            it.isTicked    = r.ticked;
                            it.isEnabled   = r.enabled;
                            it.shortcutKeyDescription = r.shortcut;
                            if (r.sub)
                                it.subMenu = std::make_unique<PopupMenu>();
                            lf.drawPopupMenuItemWithOptions (g, local, r.hot, it, opts);
                        }
                        y += hs[i];
                    }
                }

                const auto tb = lf.getTooltipBounds (tooltip, tooltipAt, getLocalBounds());
                Graphics::ScopedSaveState s (g);
                g.setOrigin (tb.getPosition());
                lf.drawTooltip (g, tooltip, tb.getWidth(), tb.getHeight());
            }
        };

        pr::RackLookAndFeel rkLf;
        Sheet sheet;
        sheet.setLookAndFeel (&rkLf);
        sheet.setSize (700, 440);

        const pr::rk::View defaults;
        sheet.menus.push_back ({ { 16, 16, 300, 0 }, {
            { Row::item,      "外观"_u8, {}, false, true, true },
            { Row::header,    "机架"_u8 },
            { Row::item,      "深色面板"_u8 },
            { Row::slider,    "线缆不透明度"_u8, {}, false, true, false, false, defaults.cableOpacity },
            { Row::slider,    "线缆张力"_u8,     {}, false, true, false, false, defaults.cableTension },
            { Row::slider,    "房间亮度"_u8,     {}, false, true, false, false, defaults.roomBrightness },
            { Row::slider,    "灯光晕"_u8,       {}, false, true, false, false, defaults.halo },
            { Row::separator, {} },
            { Row::item,      "恢复默认"_u8 } } });

        sheet.menus.push_back ({ { 340, 16, 300, 0 }, {
            { Row::header,    "fauna   ·   链位 1"_u8 },
            { Row::item,      "打开插件界面"_u8, {}, false, true, false, true },
            { Row::item,      "更换插件…"_u8 },
            { Row::item,      "旁通"_u8, "Ctrl+E", true },
            { Row::separator, {} },
            { Row::item,      "左移"_u8, "Ctrl+Left", false, false },
            { Row::item,      "右移"_u8, "Ctrl+Right" },
            { Row::separator, {} },
            { Row::item,      "卸载"_u8 } } });

        sheet.tooltip   = "fauna\n点一下聚焦 · 双击开插件界面 · 按住拖动调顺序"_u8;
        sheet.tooltipAt = { 40, 330 };

        pr::RackTip tip;
        sheet.addChildComponent (tip);
        tip.show ("强度：62 %"_u8, { 470, 380, 40, 40 });

        render (sheet, outDir.getChildFile ("rk_09_menus.png"), 1.0f);
        sheet.setLookAndFeel (nullptr);
    }

    // 换皮肤往返：和直接打开的那张必须逐像素一致
    {
        line();
        line ("== round trip");

        auto same = [&] (const char* a, const char* b)
        {
            const auto ia = ImageFileFormat::loadFrom (outDir.getChildFile (String (a) + ".png"));
            const auto ib = ImageFileFormat::loadFrom (outDir.getChildFile (String (b) + ".png"));

            bool eq = ia.isValid() && ib.isValid() && ia.getBounds() == ib.getBounds();
            for (int y = 0; eq && y < ia.getHeight(); ++y)
                for (int x = 0; eq && x < ia.getWidth(); ++x)
                    eq = ia.getPixelAt (x, y) == ib.getPixelAt (x, y);

            line (String ("  ") + (eq ? "ok    " : "FAIL  ") + b + (eq ? " == " : " != ") + a);
            return eq;
        };

        const bool ok1 = same ("01_main",    "06_roundtrip_grid");
        const bool ok2 = same ("hw_01_main", "06_roundtrip_hw");
        const bool ok3 = same ("rk_01_main", "06_roundtrip_rk");

        if (! (ok1 && ok2 && ok3))
            return 2;
    }

    // 在机架里拖模块 = ParamRouletteProcessor::moveSlot。这是这一轮唯一动了 processor
    // 状态的改动，而它碰的恰好是上一轮出过 bug 的地方（快照和链位的对应关系）——
    // 截图工具是唯一能构造真 processor + 假插件的地方，所以检查放在这里。
    {
        line();
        line ("== chain move (processor)");

        Rig rig;
        rig.proc = std::make_unique<pr::ParamRouletteProcessor>();
        rig.proc->seedForTesting (20261015);
        rig.proc->setPlayConfigDetails (2, 2, 48000.0, 512);
        rig.proc->prepareToPlay (48000.0, 512);
        populate (*rig.proc, false);       // 0 fauna、1 Disperser、3 Pro-Q；快照 1 / 2 存过

        auto& p = *rig.proc;
        int fails = 0;
        auto check = [&fails] (const String& what, bool ok)
        {
            line (String (ok ? "  ok    " : "  FAIL  ") + what);
            if (! ok) ++fails;
        };

        auto order = [&p]
        {
            String s;
            for (int i = 0; i < pr::kMaxChain; ++i)
                s << (p.isSlotLoaded (i) ? p.getSlotName (i).substring (0, 1) : String ("_"));
            return s;
        };

        const auto faunaId = p.getSlotPlugin (0)->getPluginDescription().createIdentifierString();
        const auto dispId  = p.getSlotPlugin (1)->getPluginDescription().createIdentifierString();

        p.setFocusedSlot (0);
        p.setSlotBypassed (3, true);
        check ("start: fD_F__", order() == "fD_F__");

        check ("moveSlot (0, 3) succeeds", p.moveSlot (0, 3));
        check ("order after move: D_Ff__ (the ones in between shift left)", order() == "D_Ff__");
        check ("focus followed fauna to position 3", p.getFocusedSlot() == 3 && p.getHostedName() == "fauna");
        check ("bypass moved with Pro-Q (now at 2), fauna is not bypassed",
               p.isSlotBypassed (2) && ! p.isSlotBypassed (3));
        check ("parameter model follows the plugin", p.getModel().size() == p.getSlotPlugin (3)->getParameters().size());
        check ("snapshot 1: fauna's state moved to position 3", p.getBank()[0].perSlot[3].pluginId == faunaId);
        check ("snapshot 1: Disperser's state moved to position 0", p.getBank()[0].perSlot[0].pluginId == dispId);

        // 召回必须把 fauna 的状态喂回 fauna（不是被身份校验挡掉、也不是喂给别人）
        {
            auto* fauna = p.getSlotPlugin (3);
            auto* prm   = fauna->getParameters()[0];
            const float stored = p.getBank()[0].perSlot[3].values.empty() ? -1.0f : p.getBank()[0].perSlot[3].values[0];
            prm->setValueNotifyingHost (stored < 0.5f ? 0.97f : 0.03f);
            p.recallSlot (0);
            check ("recalling snapshot 1 after the move restores fauna", std::abs (prm->getValue() - stored) < 1.0e-4f);
        }

        check ("moveSlot (3, 0) moves it back", p.moveSlot (3, 0) && order() == "fD_F__");
        check ("and the bypass is back on position 3", p.isSlotBypassed (3) && ! p.isSlotBypassed (0));
        check ("moving onto itself is refused", ! p.moveSlot (2, 2));

        if (fails > 0)
            return 3;
    }

    // Rack 外观里"手势 → 动作"的那一段接线：用鼠标事件拖模块（走 RackChainRow 的
    // mouseDown / Drag / Up）、Ctrl+← / → 挪聚焦的模块、Ctrl+E 旁通、Enter 开浏览器 / Esc 关。
    // moveSlot 本身上面查过了；这里查的是手势有没有接到对的地方、阈值对不对。
    {
        line();
        line ("== rack gestures");

        Rig rig;
        rig.proc = std::make_unique<pr::ParamRouletteProcessor>();
        rig.proc->seedForTesting (20261015);
        rig.proc->setPlayConfigDetails (2, 2, 48000.0, 512);
        rig.proc->prepareToPlay (48000.0, 512);
        populate (*rig.proc, false);       // 0 fauna、1 Disperser、3 Pro-Q

        rig.editor.reset (rig.proc->createEditorIfNeeded());
        auto* shell = dynamic_cast<pr::ParamRouletteEditor*> (rig.editor.get());
        if (shell == nullptr)
        {
            line ("  FAILED: no editor");
            return 4;
        }

        shell->setSkin (pr::Skin::rack);
        shell->setSize (1280, 820);
        pump (300);

        auto& p  = *rig.proc;
        auto* ed  = findChild<pr::RackEditor> (*shell);
        auto* row = findChild<pr::RackChainRow> (*shell);
        auto* br  = findChild<pr::RackBrowser> (*shell);

        if (ed == nullptr || row == nullptr || br == nullptr)
        {
            line ("  FAIL  rack editor parts not found");
            return 4;
        }

        int fails = 0;
        auto check = [&fails] (const String& what, bool ok)
        {
            line (String (ok ? "  ok    " : "  FAIL  ") + what);
            if (! ok) ++fails;
        };

        auto order = [&p]
        {
            String s;
            for (int i = 0; i < pr::kMaxChain; ++i)
                s << (p.isSlotLoaded (i) ? p.getSlotName (i).substring (0, 1) : String ("_"));
            return s;
        };

        auto mouse = [row] (Point<float> pos, Point<float> down, bool dragged)
        {
            const auto now = Time::getCurrentTime();
            return MouseEvent (Desktop::getInstance().getMainMouseSource(), pos,
                               ModifierKeys (ModifierKeys::leftButtonModifier),
                               MouseInputSource::defaultPressure, MouseInputSource::defaultOrientation,
                               MouseInputSource::defaultRotation, MouseInputSource::defaultTiltX,
                               MouseInputSource::defaultTiltY, row, row, now, down, now, 1, dragged);
        };

        // 按在模块的名字那一行（模块本体，不是键也不是 program 屏）
        auto grip = [row] (int slot) { const auto m = row->moduleArea (slot); return Point<float> (m.getCentreX(), m.getY() + 30.0f); };

        {
            const auto down = grip (0);
            const auto to   = down.translated (row->moduleArea (2).getCentreX() - row->moduleArea (0).getCentreX(), 0.0f);

            row->mouseDown (mouse (down, down, false));
            row->mouseDrag (mouse (down.translated (3.0f, 0.0f), down, true));
            check ("a 3 px wiggle is not a drag yet", ! row->isDraggingModule());
            row->mouseDrag (mouse (to, down, true));
            check ("past the threshold it becomes a module drag", row->isDraggingModule());
            row->mouseUp (mouse (to, down, true));
            pump (100);
            check ("drag fauna onto position 3: fD_F__ -> D_fF__", order() == "D_fF__");
            check ("fauna keeps the focus after the drop", p.getFocusedSlot() == 2 && p.getHostedName() == "fauna");
            check ("drag state is cleared on mouse up", ! row->isDraggingModule());
        }

        {
            const auto at = grip (0);
            row->mouseDown (mouse (at, at, false));
            row->mouseUp (mouse (at, at, false));
            pump (100);
            check ("a click without dragging only focuses", order() == "D_fF__" && p.getFocusedSlot() == 0);
            p.setFocusedSlot (2);
            pump (100);
        }

        ed->keyPressed (KeyPress (KeyPress::rightKey, ModifierKeys::ctrlModifier, 0));
        pump (100);
        check ("Ctrl+Right moves the focused module right", order() == "D_Ff__" && p.getFocusedSlot() == 3);
        ed->keyPressed (KeyPress (KeyPress::leftKey, ModifierKeys::ctrlModifier, 0));
        pump (100);
        check ("Ctrl+Left moves it back", order() == "D_fF__" && p.getFocusedSlot() == 2);

        ed->keyPressed (KeyPress ('E', ModifierKeys::ctrlModifier, 0));
        check ("Ctrl+E bypasses the focused module", p.isSlotBypassed (2));
        ed->keyPressed (KeyPress ('E', ModifierKeys::ctrlModifier, 0));
        check ("Ctrl+E again turns it back on", ! p.isSlotBypassed (2));

        ed->keyPressed (KeyPress (KeyPress::returnKey));
        check ("Enter opens the module browser", br->isVisible());
        ed->keyPressed (KeyPress (KeyPress::escapeKey));
        check ("Esc closes it", ! br->isVisible());

        if (fails > 0)
            return 4;
    }

    line();
    line ("done.");
    return 0;
}
