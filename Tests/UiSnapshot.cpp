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

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include "PluginProcessor.h"
#include "PluginEditor.h"

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

    // 确定性：每次从空的截图专用设置文件开始
    File::getSpecialLocation (File::userApplicationDataDirectory)
        .getChildFile ("ParamRoulette").getChildFile ("ParamRoulette-uisnap.settings").deleteFile();

    line ("ParamRoulette UI snapshot");
    line ("output: " + outDir.getFullPathName());

    {
        const auto names = Font::findAllTypefaceNames();
        for (const auto* fam : { "Noto Sans SC", "Microsoft YaHei UI", "Segoe UI Variable Text" })
            line (String ("font ") + fam + " : "
                  + (names.contains (fam) ? "yes  styles=" + Font::findAllTypefaceStyles (fam).joinIntoString ("/")
                                          : String ("no")));
    }

    struct Scenario { const char* file; bool populated; bool motion; bool twoD; bool minSize; const char* filter; };

    const Scenario scenarios[] =
    {
        { "01_main",   true,  false, false, false, "" },
        { "02_motion", true,  true,  true,  false, "eg" },
        { "03_empty",  false, false, false, false, "" },
        { "04_min",    true,  false, false, true,  "" },
    };

    for (const auto& sc : scenarios)
    {
        line();
        line (String ("== ") + sc.file);

        Rig rig;
        rig.proc = std::make_unique<pr::ParamRouletteProcessor>();
        rig.proc->setPlayConfigDetails (2, 2, 48000.0, 512);
        rig.proc->prepareToPlay (48000.0, 512);

        if (sc.populated)
            populate (*rig.proc, sc.motion);

        *rig.proc->pMorphMode = sc.twoD ? 1 : 0;
        *rig.proc->pMorphX    = sc.twoD ? 0.32f : 0.4f;
        *rig.proc->pMorphY    = 0.62f;
        rig.proc->filterText   = sc.filter;
        rig.proc->onlyMatching = String (sc.filter).isNotEmpty();

        rig.editor.reset (rig.proc->createEditorIfNeeded());

        if (rig.editor == nullptr)
        {
            line ("  FAILED: no editor");
            return 1;
        }

        if (sc.minSize)
            if (auto* con = rig.editor->getConstrainer())
                rig.editor->setSize (con->getMinimumWidth(), con->getMinimumHeight());

        // 让 20Hz 定时器、ChangeBroadcaster 的异步回调都跑几轮
        pump (450);

        render (*rig.editor, outDir.getChildFile (String (sc.file) + ".png"), 1.0f);
    }

    line();
    line ("== 05_tags");
    {
        // 插件标签窗口单独画一张：塞一串假的已扫描插件，一部分预先打好标签
        pr::GridLookAndFeel lf;
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

        pr::PluginTagView view (sc);
        view.setLookAndFeel (&lf);
        view.setSize (660, 560);

        // 「只看未分类」默认是开的，截图要看到彩色标签就把它关掉
        for (auto* c : view.getChildren())
            if (auto* tb = dynamic_cast<ToggleButton*> (c))
                tb->setToggleState (false, sendNotificationSync);

        pump (200);
        render (view, outDir.getChildFile ("05_tags.png"), 1.0f);
        view.setLookAndFeel (nullptr);
    }

    line();
    line ("done.");
    return 0;
}
