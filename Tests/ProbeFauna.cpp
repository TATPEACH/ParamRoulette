/*
    无头验收测试。不开界面、不开音频设备、不出声——只加载子插件、读参数、
    在参数上跑我们的三套逻辑，然后把结果打出来。

    存在的理由：fauna 是 32 位 VST2，只有 32 位构建碰得到它，而"能不能加载"
    这件事没法靠读代码断言。这个程序把三条验收标准变成可复现的输出：

      1. VST2 + 32 位 fauna 到底加载得了吗
      2. 哪些参数随机化不动（以及体检能不能认出来）
      3. 曲线/轮廓点是不是暴露成了普通参数（决定运动模式能做到哪一步）

    输出刻意用 ASCII：Windows 控制台的代码页太容易把中文变成乱码，
    而这份输出的价值全在于能一眼看清数字。
*/

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include "ParamModel.h"
#include "Randomizer.h"
#include "SnapshotBank.h"
#include "HistoryRing.h"
#include "MotionEngine.h"
#include "ContourIO.h"
#include "ChainRunner.h"
#include "OutputGuard.h"
#include "PluginScanner.h"

#include <iostream>

using namespace juce;

// `_u8` 住在 namespace pr 里，而这个文件在全局作用域。只把这个字面量运算符引进来，
// 不用 `using namespace pr`（那会把 Kind / Health / Role 一股脑倒出来）。
//
// 为什么控制台输出也得守这条规则：line() 最后走 String::toRawUTF8()。
// 字面量如果被 CharPointer_ASCII 按 Latin-1 解读，每个字节变成一个码点，
// 再编回 UTF-8 就是双重编码——终端上一片乱码。
using pr::operator""_u8;

namespace
{
    void line (const String& s = {}) { std::cout << s.toRawUTF8() << std::endl; }
    void head (const String& s)      { line(); line ("=== " + s + " " + String::repeatedString ("=", jmax (3, 66 - s.length()))); }

    const char* kindTag (pr::Kind k)
    {
        switch (k) { case pr::Kind::Toggle: return "toggle"; case pr::Kind::Stepped: return "stepped";
                     case pr::Kind::Continuous: break; }
        return "cont";
    }

    const char* healthTag (pr::Health h)
    {
        switch (h) { case pr::Health::Ok: return "ok"; case pr::Health::EndpointOnly: return "ENDPOINT-ONLY";
                     case pr::Health::Inert: return "INERT"; case pr::Health::Unknown: break; }
        return "?";
    }

    const char* roleTag (pr::Role r)
    {
        switch (r)
        {
            case pr::Role::EnvAmount: return "EnvAmount"; case pr::Role::EnvRate:   return "EnvRate";
            case pr::Role::EnvInvert: return "EnvInvert"; case pr::Role::EnvDelay:  return "EnvDelay";
            case pr::Role::EnvStages: return "EnvStages"; case pr::Role::EnvLoop:   return "EnvLoop";
            case pr::Role::EnvRoute:  return "EnvRoute";  case pr::Role::EnvLevel:  return "EnvLevel";
            case pr::Role::EnvTime:   return "EnvTime";   case pr::Role::EnvAttack: return "EnvAttack";
            case pr::Role::EnvDecay:  return "EnvDecay";  case pr::Role::EnvSustain:return "EnvSustain";
            case pr::Role::EnvRelease:return "EnvRelease";case pr::Role::Pitch:     return "Pitch";
            case pr::Role::Tone:      return "Tone";      case pr::Role::None:      break;
        }
        return "None";
    }

    /** 消息线程要自己转：InertProbe 走 juce::Timer。 */
    void pump (int ms)
    {
       #if JUCE_MODAL_LOOPS_PERMITTED
        MessageManager::getInstance()->runDispatchLoopUntil (ms);
       #else
        Thread::sleep (ms);
       #endif
    }

    int findByName (const Array<pr::ParamInfo>& model, const String& needle)
    {
        for (int i = 0; i < model.size(); ++i)
            if (model.getReference (i).lowerName.contains (needle.toLowerCase()))
                return i;
        return -1;
    }
}

//==============================================================================
int main (int argc, char** argv)
{
    ScopedJuceInitialiser_GUI juceInit;

    line ("ParamRoulette headless acceptance probe");
    line (String ("build arch: ") + (sizeof (void*) == 4 ? "32-bit" : "64-bit")
          + "   (32-bit is required to load fauna)");

    //== 0. 纯逻辑 =============================================================
    // 这一段**不需要插件**，所以两个架构都跑得到，而且在"找不到 fauna 就退出"
    // 那条路之前就跑完了。
    //
    // HistoryRing 单独抽出来就是为了能在这里测：PluginProcessor 依赖
    // JucePlugin_* 宏，链不进这个探针；而"回退 / 前进"的游标算术错了不会崩，
    // 只会跳到一个毫无关系的状态上——正是最需要机器来盯的那一类。
    int pureFails = 0;

    {
        head ("0. PURE LOGIC (no plugin needed)");

        // 形参是 const String& 而不是 const char*：这样所有含中文的 what 都必须带
        // `_u8`，和项目里其它地方一条规则。（JUCE 的 operator+= 其实会按 UTF-8 解，
        // 但构造函数按 ASCII 解——别指望谁记得住这个区别，见 Utf8.h。）
        auto check = [&pureFails] (const juce::String& what, bool ok)
        {
            line (juce::String (ok ? "  ok    " : "  FAIL  ") + what);
            if (! ok) ++pureFails;
        };

        auto mk = [] (const String& n, size_t bytes = 0)
        {
            pr::Snapshot s;
            s.used = true;
            s.name = n;
            s.values.push_back (1.0f);
            if (bytes > 0)
                s.childState.setSize (bytes, true);
            return s;
        };
        auto nameOf = [] (const pr::Snapshot* s) { return s != nullptr ? s->name : String ("<null>"); };

        //-- 游标算术 ----------------------------------------------------------
        {
            pr::HistoryRing h { 20, 64u << 20 };
            const std::function<pr::Snapshot()> liveC = [&mk] { return mk ("C"); };

            check ("empty ring: no back, no forward", ! h.canBack() && ! h.canForward());

            h.push (mk ("A"));      // 动作 1 之前的状态 = A
            h.push (mk ("B"));      // 动作 2 之前的状态 = B（此时活着的是 C）

            check ("after 2 pushes cursor is at the tip",
                   h.getPos() == 2 && h.getNumEntries() == 2);
            check ("at tip: can go back, cannot go forward",
                   h.canBack() && ! h.canForward());

            check ("back -> B",                 nameOf (h.back (liveC)) == "B");
            check ("first back recorded 'now' (C entered the ring)", h.getNumEntries() == 3);
            check ("back -> A",                 nameOf (h.back (liveC)) == "A");
            check ("at oldest: back returns null",
                   ! h.canBack() && h.back (liveC) == nullptr);

            check ("forward -> B",              nameOf (h.forward()) == "B");
            check ("forward -> C",              nameOf (h.forward()) == "C");
            check ("at newest: forward returns null",
                   ! h.canForward() && h.forward() == nullptr);

            // 回退之后再来一次动作：旧的"未来"必须被砍掉，否则「下一个」会走到
            // 一条已经被覆盖的分支上（C 那一支）。
            h.back (liveC);                     // 回到 B，此刻 live == B
            h.push (mk ("B"));                  // 新动作之前：live 就是 B

            check ("push after undo truncated the future",
                   h.getNumEntries() == 2 && h.getPos() == 2);

            const std::function<pr::Snapshot()> liveD = [&mk] { return mk ("D"); };
            check ("after truncation back gives B, not the abandoned C",
                   nameOf (h.back (liveD)) == "B");
        }

        //-- 双上限 ------------------------------------------------------------
        {
            pr::HistoryRing h { 3, 64u << 20 };
            for (int i = 0; i < 10; ++i)
                h.push (mk ("T" + String (i)));

            check ("entry cap enforced", h.getNumEntries() == 3);

            // pop_front 时游标必须一起前移。不移的话这里会拿到 T8 而不是 T9。
            const std::function<pr::Snapshot()> liveX = [&mk] { return mk ("X"); };
            check ("cursor follows pop_front (back gives T9)",
                   nameOf (h.back (liveX)) == "T9");
        }

        {
            pr::HistoryRing h { 20, 1000 };
            for (int i = 0; i < 10; ++i)
                h.push (mk ("S" + String (i), 300));   // 每份 300 字节，上限 1000

            check ("byte cap enforced (3 x 300 <= 1000)",
                   h.getNumEntries() == 3 && h.getBytes() == 900);
        }

        //-- subsample：「每次只动 N 个」 --------------------------------------
        {
            Random rng (20260816);

            Array<int> c;
            for (int i = 0; i < 100; ++i) c.add (i);
            pr::subsample (c, 3, rng);
            check ("subsample trims to N", c.size() == 3);
            check ("subsample yields distinct indices",
                   c[0] != c[1] && c[1] != c[2] && c[0] != c[2]);

            Array<int> d;
            for (int i = 0; i < 7; ++i) d.add (i);
            pr::subsample (d, 0, rng);
            check ("maxCount 0 means unlimited", d.size() == 7);
            pr::subsample (d, 99, rng);
            check ("maxCount above candidate count is a no-op", d.size() == 7);

            // 每个候选都必须可能被选中。部分 Fisher-Yates 写错一个下标就会让
            // 最后一个候选永远选不到——那种偏置光看代码很难发现。
            Array<bool> hit;
            hit.insertMultiple (0, false, 10);
            for (int t = 0; t < 500; ++t)
            {
                Array<int> e;
                for (int i = 0; i < 10; ++i) e.add (i);
                pr::subsample (e, 1, rng);
                hit.set (e[0], true);
            }
            bool all = true;
            for (int i = 0; i < 10; ++i) all = all && hit[i];
            check ("every candidate is reachable (no off-by-one bias)", all);
        }

        //-- OutputGuard：链尾安全网 ------------------------------------------
        // 纯 DSP，所以能测得很实。这一段防的是"随机化把全通反馈拉满 → 爆音/NaN"，
        // 而爆音伤耳朵、NaN 会顺着总线永久污染下游插件——两个都不能靠事后补救。
        {
            auto fill = [] (AudioBuffer<float>& b, float v)
            {
                for (int ch = 0; ch < b.getNumChannels(); ++ch)
                    for (int n = 0; n < b.getNumSamples(); ++n)
                        b.setSample (ch, n, v);
            };
            auto maxAbs = [] (const AudioBuffer<float>& b)
            {
                float m = 0.0f;
                for (int ch = 0; ch < b.getNumChannels(); ++ch)
                    for (int n = 0; n < b.getNumSamples(); ++n)
                        m = jmax (m, std::abs (b.getSample (ch, n)));
                return m;
            };
            auto allFinite = [] (const AudioBuffer<float>& b)
            {
                for (int ch = 0; ch < b.getNumChannels(); ++ch)
                    for (int n = 0; n < b.getNumSamples(); ++n)
                        if (! std::isfinite (b.getSample (ch, n)))
                            return false;
                return true;
            };

            pr::OutputGuard g;
            g.prepare (48000.0, 2);
            g.setCeilingDb (-1.0f);
            const float ceilLin = Decibels::decibelsToGain (-1.0f);

            // NaN / Inf 必须被清掉，而且**关掉 guard 也要清**
            {
                AudioBuffer<float> b (2, 256);
                fill (b, 0.1f);
                b.setSample (0, 10, std::numeric_limits<float>::quiet_NaN());
                b.setSample (1, 20, std::numeric_limits<float>::infinity());
                b.setSample (0, 30, -std::numeric_limits<float>::infinity());

                g.setEnabled (false);
                g.process (b);
                check ("NaN/Inf scrubbed even when guard is disabled", allFinite (b));
                check ("non-finite latch raised", g.consumeSawNonFinite());
                check ("latch clears after reading", ! g.consumeSawNonFinite());
                g.setEnabled (true);
            }

            // 爆音必须被压到 ceiling 以下
            {
                g.reset();
                AudioBuffer<float> b (2, 4096);
                fill (b, 40.0f);                       // +32 dB，典型的反馈炸开
                g.process (b);
                check ("blow-up is brought under the ceiling",
                       maxAbs (b) <= ceilLin + 1.0e-4f);
                check ("gain reduction is reported", g.getGainReductionDb() > 6.0f);
            }

            // 正常电平不该被动（knee 以下完全透明）
            {
                g.reset();
                AudioBuffer<float> b (2, 512);
                fill (b, 0.25f);                       // 远低于 knee
                g.process (b);
                check ("levels below the knee pass through untouched",
                       std::abs (maxAbs (b) - 0.25f) < 1.0e-6f);
                check ("no gain reduction reported for quiet material",
                       g.getGainReductionDb() < 0.01f);
            }

            // 增益要跨块保持：每块重置成 1.0 的话，持续超标的信号会在
            // 每个块边界重新过冲一次，听起来是周期性爆裂。
            {
                g.reset();
                float worst = 0.0f;
                for (int blk = 0; blk < 8; ++blk)
                {
                    AudioBuffer<float> b (2, 256);
                    fill (b, 10.0f);
                    g.process (b);
                    if (blk > 0)                       // 第 0 块允许有 attack 过冲
                        worst = jmax (worst, maxAbs (b));
                }
                check ("gain persists across blocks (no per-block overshoot)",
                       worst <= ceilLin + 1.0e-4f);
            }

            // 数学保证：软削波的值域严格在 ceiling 内，哪怕输入极端
            {
                g.reset();
                AudioBuffer<float> b (2, 1024);
                fill (b, 1.0e6f);
                g.process (b);
                check ("absurd input still bounded by the ceiling",
                       maxAbs (b) <= ceilLin + 1.0e-4f && allFinite (b));
            }
        }

        //-- ChainRunner：串联与延迟求和 --------------------------------------
        // 用假插件测：真插件的行为不确定，测不出"我们的串联逻辑对不对"。
        {
            /** 每个样本加一个常数，好验证"到底穿过了几段"。 */
            struct AddConst final : public AudioPluginInstance
            {
                AddConst (float d, int lat = 0, double tail = 0.0)
                    : delta (d), latency (lat), tailSec (tail) {}

                const String getName() const override            { return "AddConst"; }
                void prepareToPlay (double, int) override        {}
                void releaseResources() override                 {}
                void processBlock (AudioBuffer<float>& b, MidiBuffer&) override
                {
                    for (int ch = 0; ch < b.getNumChannels(); ++ch)
                        for (int n = 0; n < b.getNumSamples(); ++n)
                            b.setSample (ch, n, b.getSample (ch, n) + delta);
                    ++blocksSeen;
                }
                double getTailLengthSeconds() const override     { return tailSec; }
                bool acceptsMidi() const override                { return false; }
                bool producesMidi() const override               { return false; }
                AudioProcessorEditor* createEditor() override    { return nullptr; }
                bool hasEditor() const override                  { return false; }
                int getNumPrograms() override                     { return 1; }
                int getCurrentProgram() override                  { return 0; }
                void setCurrentProgram (int) override             {}
                const String getProgramName (int) override        { return {}; }
                void changeProgramName (int, const String&) override {}
                void getStateInformation (juce::MemoryBlock&) override {}
                void setStateInformation (const void*, int) override {}
                void fillInPluginDescription (PluginDescription&) const override {}

                float  delta;
                int    latency;
                double tailSec;
                int    blocksSeen = 0;
            };

            AddConst a { 1.0f }, b2 { 2.0f }, c3 { 4.0f };
            a.setLatencySamples (0);

            pr::ChainLink links[3];
            links[0].plugin = &a;  links[0].channels = 2;
            links[1].plugin = &b2; links[1].channels = 2;
            links[2].plugin = &c3; links[2].channels = 2;

            {
                AudioBuffer<float> buf (2, 64);
                buf.clear();
                MidiBuffer midi;
                const auto r = pr::runChain (links, 3, buf, midi);

                check ("chain ran all three links", r.ran == 3 && r.skipped == 0);
                // 0 + 1 + 2 + 4 = 7：只有真串起来才会是 7
                check ("audio passes through every link in order",
                       std::abs (buf.getSample (0, 0) - 7.0f) < 1.0e-6f);
            }

            // 旁通必须真的跳过，而且不算进 ran
            {
                links[1].bypassed = true;
                AudioBuffer<float> buf (2, 64);
                buf.clear();
                MidiBuffer midi;
                const auto r = pr::runChain (links, 3, buf, midi);

                check ("bypassed link is skipped", r.ran == 2);
                check ("bypassed link does not touch the audio",
                       std::abs (buf.getSample (0, 0) - 5.0f) < 1.0e-6f);
                links[1].bypassed = false;
            }

            // 空位（没插件）不该算失败
            {
                pr::ChainLink sparse[3];
                sparse[0].plugin = &a;  sparse[0].channels = 2;
                sparse[2].plugin = &c3; sparse[2].channels = 2;

                AudioBuffer<float> buf (2, 64);
                buf.clear();
                MidiBuffer midi;
                const auto r = pr::runChain (sparse, 3, buf, midi);

                check ("empty slots are not failures", r.ran == 2 && r.skipped == 0);
                check ("gaps in the chain still process in order",
                       std::abs (buf.getSample (0, 0) - 5.0f) < 1.0e-6f);
            }

            // 延迟 / 尾巴求和
            {
                AddConst la { 0.0f, 0, 0.5 }, lb { 0.0f, 0, 1.5 };
                la.setLatencySamples (100);
                lb.setLatencySamples (250);

                pr::ChainLink ll[2];
                ll[0].plugin = &la; ll[0].channels = 2;
                ll[1].plugin = &lb; ll[1].channels = 2;

                check ("latency is the sum of the chain", pr::chainLatencySamples (ll, 2) == 350);
                check ("tail is the sum of the chain",
                       std::abs (pr::chainTailSeconds (ll, 2) - 2.0) < 1.0e-9);

                ll[0].bypassed = true;
                check ("bypassed link contributes no latency",
                       pr::chainLatencySamples (ll, 2) == 250);
            }

            // 声道不够时必须走暂存区，而且暂存区不够时**跳过而不是越界**
            {
                AddConst wide { 3.0f };
                AudioBuffer<float> scratch (4, 64);

                pr::ChainLink w;
                w.plugin = &wide; w.channels = 4; w.scratch = &scratch;

                AudioBuffer<float> buf (2, 64);       // 宿主只给 2 声道
                buf.clear();
                MidiBuffer midi;
                check ("wider child runs through the scratch buffer",
                       pr::runLink (w, buf, midi));
                check ("scratch path writes back to the host channels",
                       std::abs (buf.getSample (1, 0) - 3.0f) < 1.0e-6f);

                AudioBuffer<float> tooSmall (2, 64);  // 装不下 4 声道
                w.scratch = &tooSmall;
                const int before = wide.blocksSeen;
                check ("insufficient scratch is reported, not overrun",
                       ! pr::runLink (w, buf, midi));
                check ("the plugin was not called at all in that case",
                       wide.blocksSeen == before);

                w.scratch = nullptr;
                check ("null scratch is reported too", ! pr::runLink (w, buf, midi));
            }
        }

        //-- 快照身份校验：不能把 A 插件的状态块喂给 B 插件 -------------------
        // 单插件时代不可能搞混；有了 6 个链位之后，"在链位 0 存快照、切到
        // 链位 1 召回"只差一次点击。childState 是插件私有字节流，喂错了
        // 轻则被忽略、重则崩溃或污染它的内部状态。
        {
            pr::Snapshot s;
            s.used = true;
            s.values.assign (4, 0.5f);
            s.childState.setSize (16, true);
            s.pluginId = "VST3-SomeOtherPlugin-1234-5678";

            // 拿一个真的 AudioPluginInstance 来比对身份不现实（探针里没有），
            // 但校验函数的两条分支可以直接验：空 id 放行、不同 id 拦住。
            pr::Snapshot legacy = s;
            legacy.pluginId.clear();

            check ("snapshot records the plugin it came from", s.pluginId.isNotEmpty());
            check ("legacy snapshot (no id) is left permissive", legacy.pluginId.isEmpty());

            // ValueTree 往返必须保住 id——否则工程重开后校验就失效了，
            // 而那正是最容易切错链位的时刻。
            const juce::Identifier kSlot { "SLOT" };
            const auto tree = pr::snapshotToValueTree (s, kSlot);
            const auto back = pr::snapshotFromValueTree (tree);

            check ("plugin id survives the ValueTree round trip",
                   back.pluginId == s.pluginId);
            check ("round trip keeps the state block",
                   back.childState.getSize() == s.childState.getSize());
            check ("round trip keeps the values",
                   back.values.size() == s.values.size());
        }

        //-- 快照现在覆盖整条链：v2 / v3 两种格式都要读得进来 -------------------
        // 这是最容易悄悄坏的地方：格式改了之后老工程的快照如果读错层级，
        // 表现是"快照还在、召回却没反应"，而不是报错。
        {
            const juce::Identifier kSNAPSHOTS { "SNAPSHOTS" };
            const juce::Identifier kSLOT      { "SLOT" };
            const juce::Identifier kCHAINPOS  { "CHAINPOS" };

            auto mkSnap = [] (const juce::String& id)
            {
                pr::Snapshot s;
                s.used = true;
                s.values.assign (8, 0.25f);
                s.childState.setSize (32, true);
                s.pluginId = id;
                return s;
            };

            // v2：SLOT 自己就是一份 Snapshot（那时链上只有一位）
            {
                juce::ValueTree root (kSNAPSHOTS);
                auto slot = pr::snapshotToValueTree (mkSnap ("PLUG-A"), kSLOT);
                root.appendChild (slot, nullptr);

                pr::SnapshotBank bank;
                bank.restoreFromValueTree (root);

                check ("v2 snapshot lands in chain position 0",
                       bank[0].perSlot[0].used && bank[0].perSlot[0].pluginId == "PLUG-A");
                check ("v2 snapshot leaves other chain positions empty",
                       ! bank[0].perSlot[1].used);
                check ("v2 snapshot counts as used", bank.isUsed (0) && bank.numUsed() == 1);
            }

            // v3：SLOT 下面每链位一个 CHAINPOS
            {
                juce::ValueTree root (kSNAPSHOTS);
                juce::ValueTree slot (kSLOT);
                slot.setProperty ("name", "my chain", nullptr);

                // 故意只放链位 2 和 4，而且顺序反着放——读的那边必须靠 pos 属性
                // 定位，不能靠子节点次序。
                for (const int pos : { 4, 2 })
                {
                    auto child = pr::snapshotToValueTree (mkSnap ("PLUG-" + juce::String (pos)),
                                                          kCHAINPOS);
                    child.setProperty ("pos", pos, nullptr);
                    slot.appendChild (child, nullptr);
                }

                root.appendChild (slot, nullptr);

                pr::SnapshotBank bank;
                bank.restoreFromValueTree (root);

                check ("v3 snapshot honours the pos attribute, not child order",
                       bank[0].perSlot[2].pluginId == "PLUG-2"
                    && bank[0].perSlot[4].pluginId == "PLUG-4");
                check ("v3 snapshot leaves unlisted chain positions empty",
                       ! bank[0].perSlot[0].used && ! bank[0].perSlot[3].used);
                check ("v3 snapshot keeps the chain preset name",
                       bank[0].name == "my chain");
            }

            // 空的 SNAPSHOTS 不能被当成"存过"
            {
                juce::ValueTree root (kSNAPSHOTS);
                juce::ValueTree slot (kSLOT);
                root.appendChild (slot, nullptr);

                pr::SnapshotBank bank;
                bank.restoreFromValueTree (root);

                check ("an empty slot node does not count as used",
                       ! bank.isUsed (0) && bank.numUsed() == 0);
            }
        }

        //-- guessTag：名字关键词猜功能标签 -----------------------------------
        // 这是"随机效果器链"配方的数据地基。规则表的**顺序**是关键：
        // "Allpass Filter" 必须判成全通而不是普通滤波器——两者在配方里完全不同。
        {
            auto tag = [] (const char* name) { return pr::PluginScanner::guessTag (name); };

            check ("Disperser -> 相位类"_u8,        tag ("Disperser") == pr::FnTag::Phase);
            check ("kHs Phaser -> 相位类"_u8,       tag ("kHs Phaser") == pr::FnTag::Phase);

            // 顺序陷阱：这个名字同时含 allpass 和 filter
            check ("Allpass Filter -> 全通，不是普通滤波器"_u8,
                   tag ("Allpass Filter") == pr::FnTag::Allpass);
            check ("带连字符的 all-pass 也认"_u8,
                   tag ("Vintage All-Pass Network") == pr::FnTag::Allpass);

            check ("Pro-Q -> 均衡"_u8,              tag ("FabFilter Pro-Q 4") == pr::FnTag::EQ);
            check ("AMEK EQ 200 -> 均衡"_u8,        tag ("AMEK EQ 200") == pr::FnTag::EQ);
            check ("Cluster Delay -> 延迟"_u8,      tag ("Cluster Delay") == pr::FnTag::Delay);
            check ("Altiverb -> 混响"_u8,           tag ("Altiverb 8") == pr::FnTag::Reverb);
            check ("bx_bluechorus2 -> 调制"_u8,     tag ("bx_bluechorus2") == pr::FnTag::Modulation);
            check ("bx_clipper -> 动态"_u8,         tag ("bx_clipper") == pr::FnTag::Dynamics);
            check ("OTT -> 动态"_u8,                tag ("OTT") == pr::FnTag::Dynamics);
            check ("bx_saturator -> 失真饱和"_u8,   tag ("bx_saturator V2") == pr::FnTag::Distortion);
            check ("GRM PitchAccum -> 变调"_u8,     tag ("GRM PitchAccum") == pr::FnTag::Pitch);
            check ("GRM Freeze -> 颗粒时间"_u8,     tag ("GRM Freeze Stereo") == pr::FnTag::Granular);

            // —— 下面这几条是**实测踩到的假阳性**，钉住免得回归。
            // 子串匹配在短模式上很危险，所以 eq / ott / comb 改成整词匹配了。
            check ("bluechorus 里的 echo 不该把它判成延迟"_u8,
                   tag ("bx_bluechorus2") != pr::FnTag::Delay);
            check ("Echoboy 仍然是延迟（整词匹配没伤到它）"_u8,
                   tag ("EchoBoy") == pr::FnTag::Delay);
            check ("frequency 里的 eq 不该判成均衡"_u8,
                   tag ("Frequency Splitter") != pr::FnTag::EQ);
            check ("bottle 里的 ott 不该判成动态"_u8,
                   tag ("Bottle Rocket") != pr::FnTag::Dynamics);
            check ("combo 里的 comb 不该判成滤波器"_u8,
                   tag ("Guitar Combo") != pr::FnTag::Filter);
            check ("Comb Filter 还是滤波器"_u8,
                   tag ("Comb Filter") == pr::FnTag::Filter);

            // 本机实测：69% 的插件名看不出功能（Acustica 那种密码名）。
            // 这一条钉住"猜不出就老实说不知道"，而不是瞎归一类。
            check ("密码名猜不出来就返回未分类"_u8,
                   tag ("ACDD") == pr::FnTag::Unknown
                && tag ("ACEH") == pr::FnTag::Unknown
                && tag ("ACGQ") == pr::FnTag::Unknown);

            check ("空名字不崩"_u8,                 tag ("") == pr::FnTag::Unknown);
            check ("大小写不敏感"_u8,               tag ("DISPERSER") == pr::FnTag::Phase);

            // 标签都得有中文名，否则界面上会出现空白项
            bool allLabelled = true;
            for (int i = 0; i < pr::kNumFnTags; ++i)
                allLabelled = allLabelled && pr::fnTagLabel ((pr::FnTag) i).isNotEmpty();
            check ("每个标签都有显示名"_u8, allLabelled);
        }

        line ();
        line (pureFails == 0 ? "  pure logic: ALL PASS"
                             : "  pure logic: " + String (pureFails) + " FAILURE(S)");
    }

    //== 目标文件 ==============================================================
    StringArray candidates;
    for (int i = 1; i < argc; ++i)
        candidates.add (String (argv[i]));

    if (candidates.isEmpty())
        candidates.addArray ({ "C:\\Program Files (x86)\\VSTPlugIns\\fauna.dll",
                               "C:\\Program Files\\Common Files\\VST2\\fauna.dll",
                               "C:\\Program Files\\Steinberg\\VSTPlugins\\xoxos_fauna\\fauna.dll" });

    //== 1. 加载 ==============================================================
    head ("1. FORMATS + LOAD");

    AudioPluginFormatManager fm;
    fm.addFormat (new VST3PluginFormat());
   #if JUCE_PLUGINHOST_VST
    fm.addFormat (new VSTPluginFormat());
   #endif

    {
        StringArray names;
        for (auto* f : fm.getFormats())
            names.add (f->getName());
        line ("registered formats : " + names.joinIntoString (", "));
       #if JUCE_PLUGINHOST_VST
        line ("JUCE_PLUGINHOST_VST: 1");
       #else
        line ("JUCE_PLUGINHOST_VST: 0   <-- VST2 hosting is COMPILED OUT, fauna cannot work");
       #endif
    }

    OwnedArray<PluginDescription> found;
    String usedPath;

    for (const auto& p : candidates)
    {
        if (! File (p).existsAsFile())
        {
            line ("missing            : " + p);
            continue;
        }

        for (auto* fmt : fm.getFormats())
        {
            OwnedArray<PluginDescription> types;
            fmt->findAllTypesForFile (types, p);

            if (! types.isEmpty())
            {
                found.swapWith (types);
                usedPath = p;
                break;
            }
        }

        if (! found.isEmpty())
            break;

        line ("no type found in   : " + p);
    }

    if (found.isEmpty())
    {
        // 找不到 fauna 不代表这次运行没价值：section 0 那 48 项纯逻辑检查
        // 已经跑完了，而**64 位构建永远找不到 fauna**（32 位 DLL，架构墙）。
        // 以前这里直接 return 1，等于 64 位构建每次都报失败、把真正的结论淹掉。
        line ();
        line ("=== SKIP: fauna not loadable here ==============================");
        line ("no plugin description could be produced from any candidate path.");

        if (sizeof (void*) != 4)
            line ("this is EXPECTED in a 64-bit build: fauna is a 32-bit VST2 and a"
                  " 64-bit process cannot load it. Build build32 to exercise sections 1-14.");
        else
            line ("this is UNEXPECTED in a 32-bit build - check the paths above.");

        line ();
        line (pureFails == 0
                ? "RESULT: PASS (pure logic only - sections 1-14 skipped)"
                : "RESULT: FAIL - " + String (pureFails) + " pure-logic check(s) failed.");

        return pureFails > 0 ? 3 : 0;
    }

    auto* desc = found.getFirst();
    line ("file               : " + usedPath);
    line ("descriptor         : " + desc->name + "  [" + desc->pluginFormatName + "]"
          + (desc->isInstrument ? "  VSTi" : "  FX"));
    line ("manufacturer       : " + desc->manufacturerName);
    line ("uid                : " + String::toHexString (desc->uniqueId));

    String err;
    auto plugin = fm.createPluginInstance (*desc, 48000.0, 512, err);

    if (plugin == nullptr)
    {
        line ("RESULT: FAIL - instantiation failed: " + err);
        return 2;
    }

    line ("instantiated       : OK");
    line ("reported name      : " + plugin->getName());
    line ("bus layout         : in " + String (plugin->getTotalNumInputChannels())
          + " / out " + String (plugin->getTotalNumOutputChannels()));
    line ("parameters         : " + String (plugin->getParameters().size()));

    plugin->setPlayConfigDetails (plugin->getTotalNumInputChannels(),
                                  jmax (2, plugin->getTotalNumOutputChannels()), 48000.0, 512);
    plugin->prepareToPlay (48000.0, 512);

    //== 2. 分类 ==============================================================
    head ("2. PARAM MODEL");

    auto model = pr::buildModel (*plugin);
    Array<bool> checked;
    for (const auto& info : model)
        checked.add (! info.defaultSkip());

    int nToggle = 0, nStep = 0, nCont = 0, nBlob = 0, nSkip = 0;
    for (const auto& i : model)
    {
        if (i.kind == pr::Kind::Toggle) ++nToggle; else if (i.kind == pr::Kind::Stepped) ++nStep; else ++nCont;
        if (i.blob) ++nBlob;
        if (i.defaultSkip()) ++nSkip;
    }

    line ("model size         : " + String (model.size()));
    line ("toggle/stepped/cont: " + String (nToggle) + " / " + String (nStep) + " / " + String (nCont));
    line ("blob-looking       : " + String (nBlob));
    line ("default-skipped    : " + String (nSkip) + "   (checked = " + String (model.size() - nSkip) + ")");

    line();
    line ("all params (idx | name | kind | steps | value | display):");
    for (const auto& i : model)
        line ("  " + String (i.index).paddedLeft (' ', 4) + " | " + i.name.paddedRight (' ', 22)
              + " | " + String (kindTag (i.kind)).paddedRight (' ', 8)
              + " | " + String (i.numSteps).paddedLeft (' ', 5)
              + " | " + String (i.value, 4)
              + " | " + i.display);

    //== 3. 随机化 ============================================================
    head ("3. RANDOMIZE (amount 1.0, range 0..1)");

    const int invIdx = findByName (model, "inv");
    if (invIdx >= 0)
        line ("tracking           : \"" + model.getReference (invIdx).name + "\" (model idx "
              + String (invIdx) + ", param idx " + String (model.getReference (invIdx).index)
              + ", kind " + kindTag (model.getReference (invIdx).kind) + ")");
    else
        line ("tracking           : no param name contains \"inv\"");

    Random rng (20260806);
    pr::RandomSettings rs; rs.amount = 1.0f; rs.lo = 0.0f; rs.hi = 1.0f;

    for (int pass = 0; pass < 6; ++pass)
    {
        const auto r = pr::randomize (*plugin, model, checked, rs, rng);
        pr::refreshValues (*plugin, model);

        String extra;
        if (invIdx >= 0)
            extra = "   " + model.getReference (invIdx).name + " = "
                  + model.getReference (invIdx).display
                  + " (norm " + String (model.getReference (invIdx).value, 3) + ")";

        line ("  pass " + String (pass + 1) + ": touched " + String (r.touched)
              + ", moved " + String (r.moved)
              + (r.looksSuspicious() ? "  <-- SUSPICIOUS" : "") + extra);
    }

    //== 4. 体检 ==============================================================
    head ("4. INERT PROBE (3 phases: write 0.0 / 0.5 / 1.0)");

    pr::InertProbe probe;
    bool probeDone = false;
    int gotInert = 0, gotEndpoint = 0;

    probe.start (*plugin, model, [&] (int inert, int endpoint)
    {
        gotInert = inert; gotEndpoint = endpoint; probeDone = true;
    });

    const auto deadline = Time::currentTimeMillis() + 15000;
    while (! probeDone && Time::currentTimeMillis() < deadline)
        pump (50);

    if (! probeDone)
    {
        line ("RESULT: probe did not finish within 15s (message loop problem?)");
    }
    else
    {
        int nOk = 0, nUnknown = 0;
        for (const auto& i : model)
        {
            if (i.health == pr::Health::Ok) ++nOk;
            else if (i.health == pr::Health::Unknown) ++nUnknown;
        }

        line ("ok / endpoint-only / inert / untested : "
              + String (nOk) + " / " + String (gotEndpoint) + " / " + String (gotInert)
              + " / " + String (nUnknown));

        line();
        line ("ENDPOINT-ONLY params (these are the ones that never moved before the fix):");
        bool any = false;
        for (const auto& i : model)
            if (i.health == pr::Health::EndpointOnly)
            { line ("  " + String (i.index).paddedLeft (' ', 4) + " | " + i.name
                    + "   -> kind rewritten to " + kindTag (i.kind)); any = true; }
        if (! any) line ("  (none)");

        line();
        line ("INERT params (host writes are rejected outright):");
        any = false;
        for (const auto& i : model)
            if (i.health == pr::Health::Inert)
            { line ("  " + String (i.index).paddedLeft (' ', 4) + " | " + i.name); any = true; }
        if (! any) line ("  (none)");
    }

    //== 5. 随机化（体检之后） =================================================
    head ("5. RANDOMIZE AGAIN (after probe rewrote kinds)");

    for (int pass = 0; pass < 6; ++pass)
    {
        const auto r = pr::randomize (*plugin, model, checked, rs, rng);
        pr::refreshValues (*plugin, model);

        String extra;
        if (invIdx >= 0)
            extra = "   " + model.getReference (invIdx).name + " = "
                  + model.getReference (invIdx).display;

        line ("  pass " + String (pass + 1) + ": touched " + String (r.touched)
              + ", moved " + String (r.moved) + extra);
    }

    //== 5b. 低强度：这才是"怎么随机都不动"的真实场景 ==========================
    head ("5b. LOW AMOUNT (0.30) - 40 passes, counting actual flips of the tracked toggle");

    if (invIdx >= 0)
    {
        auto* params = plugin->getParameters().getRawDataPointer();
        const int pIdx = model.getReference (invIdx).index;
        auto* tracked = plugin->getParameters()[pIdx];
        juce::ignoreUnused (params);

        pr::RandomSettings low; low.amount = 0.30f; low.lo = 0.0f; low.hi = 1.0f;

        // (a) 当成 Continuous 处理（体检前的行为）
        auto asCont = model;
        for (auto& i : asCont)
            if (i.health == pr::Health::EndpointOnly) { i.kind = pr::Kind::Continuous; i.numSteps = 0; }

        auto countFlips = [&] (Array<pr::ParamInfo>& m, const char* label)
        {
            tracked->setValueNotifyingHost (0.0f);
            pump (60);
            float prev = tracked->getValue();
            int flips = 0;

            for (int i = 0; i < 40; ++i)
            {
                pr::randomize (*plugin, m, checked, low, rng);
                const float now = tracked->getValue();
                if (std::abs (now - prev) > 1.0e-3f) ++flips;
                prev = now;
            }
            line (String ("  ") + label + ": " + String (flips) + " / 40 flips");
        };

        countFlips (asCont, "as Continuous (pre-fix behaviour) ");
        countFlips (model,  "as Toggle     (post-probe fix)    ");
    }
    else
    {
        line ("  (no tracked toggle)");
    }

    //== 6. 运动模式 / 曲线 ====================================================
    head ("6. MOTION ENGINE / CURVE EXPOSURE");

    const auto profile = pr::detectProfile (model);
    line ("summary            : " + profile.summary());
    line ("hasMotion()        : " + String (profile.hasMotion() ? "true" : "false"));
    line ("hasInvert()        : " + String (profile.hasInvert() ? "true  -> contour stays positive, direction via invert"
                                                               : "false -> direction baked into contour"));
    line ("looksLikeFauna     : " + String (profile.looksLikeFauna ? "TRUE - using codex's ear-validated recipe"
                                                                  : "false - generic engine"));
    if (profile.looksLikeFauna)
        line ("  recipe indices   : eg3amt " + String (profile.idxEg3Amount)
              + "  eg4amt " + String (profile.idxEg4Amount)
              + "  eg4inv " + String (profile.idxEg4Invert)
              + "  eg4rate " + String (profile.idxEg4Rate)
              + "  eg4rep " + String (profile.idxEg4Repeat)
              + "  eg4stg " + String (profile.idxEg4Stages)
              + "  eg4sus " + String (profile.idxEg4Sustain)
              + "  eg4dest " + String (profile.idxEg4Route)
              + "  tens " + String (profile.idxTension));
    line ("contour points     : " + String (profile.numContourPoints)
          + (profile.numContourPoints == 0
                 ? "   <-- curve NOT exposed as params; contour cannot be driven"
                 : "   <-- curve IS exposed; contour can be driven"));

    line();
    line ("recognised roles:");
    if (profile.hits.isEmpty())
        line ("  (none)");
    else
        for (const auto& h : profile.hits)
            line ("  " + String (model.getReference (h.modelIndex).index).paddedLeft (' ', 4)
                  + " | " + model.getReference (h.modelIndex).name.paddedRight (' ', 22)
                  + " | " + String (roleTag (h.role)).paddedRight (' ', 11)
                  + " | group " + String (h.group) + " point " + String (h.point));

    line();
    auto base = pr::captureSnapshot (*plugin, "base");
    line ("base locked        : values " + String ((int) base.values.size())
          + ", child state " + String ((int) base.childState.getSize()) + " bytes");

    // 状态块 dump：轮廓点没暴露成参数，唯一的路是插件自己的状态块。
    // 写到磁盘上，交给分析脚本找记录结构。
    if (base.childState.getSize() > 0)
    {
        const File dump = File::getCurrentWorkingDirectory().getChildFile ("fauna_state.bin");
        dump.replaceWithData (base.childState.getData(), base.childState.getSize());
        line ("state dumped to    : " + dump.getFullPathName());
    }

    for (int s = 0; s < pr::kNumMotionSemantics; ++s)
    {
        pr::MotionSettings st;
        st.semantic = (pr::MotionSemantic) s;
        st.motion = 0.65f;
        st.tone   = 0.35f;

        const int stages = pr::stagesForShape (pr::shapeForSemantic (st.semantic));
        const auto mr = pr::applyMotion (*plugin, model, checked, profile, base.values, st, rng, stages);
        pr::refreshValues (*plugin, model);

        // fauna 配方的关键几个参数写成什么了？
        String key;
        if (profile.looksLikeFauna)
        {
            const auto disp = [&] (int mi) {
                return mi >= 0 ? model.getReference (mi).display : String ("-"); };
            key = "   inv=" + disp (profile.idxEg4Invert)
                + " amt=" + disp (profile.idxEg4Amount)
                + " dest=" + disp (profile.idxEg4Route)
                + " tens=" + disp (profile.idxTension)
                + " stg=" + disp (profile.idxEg4Stages)
                + " eg3amt=" + disp (profile.idxEg3Amount);
        }

        line ("  sem " + String (s) + " stages " + String (stages)
              + " : touched " + String (mr.touched) + ", moved " + String (mr.moved) + key);
    }

    //== 收尾 =================================================================
    head ("7. CONTOUR VIA STATE CHUNK (the real curve path)");

    {
        MemoryBlock st;
        plugin->getStateInformation (st);

        const auto slots = pr::findContourGroups (st);
        line ("state size         : " + String ((int) st.getSize()) + " bytes");
        line ("contour groups     : " + String (slots.size())
              + (slots.isEmpty() ? "   <-- signature not recognised, curve path unavailable"
                                 : "   <-- recognised"));
        line ("current program    : " + String (plugin->getCurrentProgram())
              + " / " + String (plugin->getNumPrograms())
              + "   name \"" + plugin->getProgramName (plugin->getCurrentProgram()) + "\"");

        if (! slots.isEmpty())
        {
            const int prog = jlimit (0, pr::ContourLayout::kNumPrograms - 1, plugin->getCurrentProgram());

            for (const auto& s : slots)
            {
                pr::NativeContour c;
                if (! pr::readContour (st, s, prog, c)) { line ("  group " + String (s.groupIndex) + ": read FAILED"); continue; }

                String lv, tv;
                for (int i = 0; i < pr::ContourLayout::kPoints; ++i)
                { lv << String (c.levels[(size_t) i], 3) << " "; tv << String (c.times[(size_t) i], 3) << " "; }

                line ("  group " + String (s.groupIndex) + " @" + String (s.byteOffset)
                      + "  used segments " + String (c.usedSegments()));
                line ("    levels " + lv);
                line ("    times  " + tv);
            }

            //-- 写回往返：改 group 3（EG4），写进插件，再读回来比对
            line();
            line ("round-trip write test on EG4 (group 3) only, base-derived:");

            MemoryBlock baseState;
            plugin->getStateInformation (baseState);
            const auto baseSlots = pr::findContourGroups (baseState);

            pr::NativeContour before;
            if (baseSlots.size() >= 4)
                pr::readContour (baseState, baseSlots.getReference (3), prog, before);

            const String blobBefore = plugin->getParameters()[3]->getCurrentValueAsText();

            Random wr (777);
            const auto cr = pr::randomiseContours (*plugin, pr::MotionSemantic::Rise, 1.0f, wr,
                                                   &baseState, /*onlyGroup*/ 3, prog);
            line ("  wrote " + String (cr.written) + " contour(s), stages " + String (cr.stages)
                  + (cr.failed ? "   FAILED" : ""));

            MemoryBlock after;
            plugin->getStateInformation (after);
            const auto slots2 = pr::findContourGroups (after);

            if (slots2.size() < 4)
            {
                line ("  RESULT: FAIL - groups no longer found after write-back");
            }
            else
            {
                pr::NativeContour got, eg3;
                pr::readContour (after, slots2.getReference (3), prog, got);
                pr::readContour (after, slots2.getReference (2), prog, eg3);

                String lv, tv;
                for (int i = 0; i < 4; ++i)
                { lv << String (got.levels[(size_t) i], 3) << " "; tv << String (got.times[(size_t) i], 3) << " "; }
                line ("  EG4 levels[0..3] : " + lv);
                line ("  EG4 times [0..3] : " + tv + "   (ramp = level 1.0, one segment)");
                line ("  EG4 used segments: " + String (got.usedSegments()));

                // EG3 必须没被碰过
                pr::NativeContour eg3base;
                pr::readContour (baseState, baseSlots.getReference (2), prog, eg3base);
                bool eg3Same = true;
                for (int i = 0; i < pr::ContourLayout::kPoints; ++i)
                    if (eg3.levels[(size_t) i] != eg3base.levels[(size_t) i]
                        || eg3.times[(size_t) i] != eg3base.times[(size_t) i]) eg3Same = false;
                line (String ("  EG3 untouched    : ") + (eg3Same ? "YES  OK" : "NO  <-- BUG"));

                const String blobAfter = plugin->getParameters()[3]->getCurrentValueAsText();
                line (String ("  plugin blob param changed : ")
                      + (blobBefore != blobAfter ? "YES - plugin accepted the write" : "no"));

                //-- 漂移测试：反复生成 8 次，看时长有没有累积漂移
                line();
                line ("  drift test: 8 generations, EG4 time[0] each round (must not creep):");
                String drift;
                for (int k = 0; k < 8; ++k)
                {
                    pr::randomiseContours (*plugin, pr::MotionSemantic::Rise, 1.0f, wr, &baseState, 3, prog);
                    MemoryBlock now; plugin->getStateInformation (now);
                    const auto sl = pr::findContourGroups (now);
                    pr::NativeContour c;
                    if (sl.size() >= 4 && pr::readContour (now, sl.getReference (3), prog, c))
                        drift << String (c.times[0], 3) << " ";
                }
                line ("    " + drift);

                //-- program 变了要中止
                const auto guard = pr::randomiseContours (*plugin, pr::MotionSemantic::Rise, 1.0f, wr,
                                                          &baseState, 3, /*expectedProgram*/ 99);
                line (String ("  program mismatch guard : ")
                      + (guard.failed && guard.written == 0 ? "aborts  OK" : "DID NOT ABORT  <-- BUG"));

                line (String ("  RESULT: ") + (cr.written == 1 && ! cr.failed ? "PASS" : "FAIL"));
            }
        }
    }

    head ("8. SILENT PSEUDO-TOGGLE DETECTION via getText() (no writes, no sound)");

    {
        // codex 的办法：让插件**格式化** normalized 0 / 0.5 / 1，不写入。
        // 如果 0.5 的文本等于某个端点的文本，就是伪二值。
        // 这比写-读探测好三点：同步、静音、不改任何东西。
        int agree = 0, disagree = 0, missed = 0;
        StringArray mismatches;

        for (const auto& i : model)
        {
            auto* p = plugin->getParameters()[i.index];
            if (p == nullptr || i.blob) continue;

            const auto t0 = p->getText (0.0f, 64);
            const auto t5 = p->getText (0.5f, 64);
            const auto t1 = p->getText (1.0f, 64);

            const bool pseudo = (t0 != t1) && (t5 == t0 || t5 == t1);
            const bool probeSaidSo = (i.health == pr::Health::EndpointOnly);

            if (pseudo == probeSaidSo) ++agree;
            else
            {
                ++disagree;
                if (mismatches.size() < 14)
                    mismatches.add (i.name + "  [getText:" + (pseudo ? "pseudo" : "cont")
                                    + " probe:" + healthTag (i.health)
                                    + "]  0=\"" + t0 + "\" .5=\"" + t5 + "\" 1=\"" + t1 + "\"");
            }
            if (! pseudo && probeSaidSo) ++missed;
        }

        line ("agree with write-probe                  : " + String (agree));
        line ("disagree                                : " + String (disagree));
        line ("probe found pseudo but getText missed it: " + String (missed));
        for (const auto& m : mismatches) line ("  " + m);

        line();
        line ("sample getText output:");
        for (const int mi : { invIdx, findByName (model, "tens"), findByName (model, "stages") })
        {
            if (mi < 0) continue;
            auto* p = plugin->getParameters()[model.getReference (mi).index];
            line ("  " + model.getReference (mi).name.paddedRight (' ', 14)
                  + " 0=\"" + p->getText (0.0f, 32) + "\"  0.5=\"" + p->getText (0.5f, 32)
                  + "\"  1=\"" + p->getText (1.0f, 32) + "\"");
        }
    }

    head ("9. CONTOUR ALIGNMENT: ours (level@11) vs codex (record=marker+9, level@15 => our 16)");

    {
        MemoryBlock st;
        plugin->getStateInformation (st);
        const auto slots = pr::findContourGroups (st);
        const auto* d = static_cast<const uint8*> (st.getData());

        if (! slots.isEmpty())
        {
            int ebCount = 0, total = 0;
            for (const auto& s : slots)
                for (int p = 0; p < pr::ContourLayout::kNumPrograms; ++p)
                {
                    const size_t off = (size_t) s.byteOffset + 8 + (size_t) p * 236;
                    if (off < st.getSize()) { ++total; if (d[off] == 0xEB) ++ebCount; }
                }
            line ("records whose byte[marker+8] == 0xEB : " + String (ebCount) + " / " + String (total));
            line ("  (all => 0xEB is a per-record length prefix, so it belongs to the record)");

            auto zeroRate = [&] (int rel)
            {
                int zero = 0, n = 0;
                for (const auto& s : slots)
                    for (int p = 0; p < pr::ContourLayout::kNumPrograms; ++p)
                    {
                        const size_t off = (size_t) s.byteOffset + 8 + (size_t) p * 236 + (size_t) rel;
                        if (off + 5 > st.getSize()) continue;
                        ++n;
                        if (pr::ckDecodeFloat (d + off) == 0.0f) ++zero;
                    }
                return String (zero) + " / " + String (n);
            };
            line();
            line ("how often the slot holds exactly 0.0:");
            line ("  rel 11 : " + zeroRate (11) + "   <- our level[0]  (envelope starts at silence?)");
            line ("  rel 16 : " + zeroRate (16) + "   <- codex level[0]");
            line ("  rel 91 : " + zeroRate (91) + "   <- our time[0]   /  codex level[15]");
            line ("  rel 96 : " + zeroRate (96) + "   <- codex time[0]");

            int ourPaired = 0, codexPaired = 0, n = 0;
            for (const auto& s : slots)
                for (int p = 0; p < pr::ContourLayout::kNumPrograms; ++p)
                {
                    const size_t rec = (size_t) s.byteOffset + 8 + (size_t) p * 236;
                    if (rec + 235 > st.getSize()) continue;
                    ++n;
                    auto cnt = [&] (int rel) { int c = 0;
                        for (int k = 0; k < 16; ++k) if (pr::ckDecodeFloat (d + rec + rel + k*5) != 0.0f) ++c;
                        return c; };
                    if (cnt (11) == cnt (91)) ++ourPaired;
                    if (cnt (16) == cnt (96)) ++codexPaired;
                }
            line();
            line ("records where nonzero(level array) == nonzero(time array):");
            line ("  ours  (11 / 91) : " + String (ourPaired) + " / " + String (n));
            line ("  codex (16 / 96) : " + String (codexPaired) + " / " + String (n));
        }
    }

    head ("10. UTF-8 STRING LITERALS (mojibake regression)");

    {
        // juce::String(const char*) 走 CharPointer_ASCII，每字节当一个码点，
        // 所以中文字面量必须带 _u8 后缀。这里逐字节比对，确认真的修好了。
        struct Case { const char* what; juce::String got; std::vector<uint8> want; };

        const std::vector<Case> cases {
            { "motionSemanticLabel(Rise)", pr::motionSemanticLabel (pr::MotionSemantic::Rise),
              { 0xE4,0xB8,0x8A, 0xE6,0x89,0xAC } },                                  // 上扬
            { "kindLabel(Toggle)",         pr::kindLabel (pr::Kind::Toggle),
              { 0xE5,0xBC,0x80, 0xE5,0x85,0xB3 } },                                  // 开关
            { "healthLabel(EndpointOnly)", pr::healthLabel (pr::Health::EndpointOnly),
              { 0xE5,0x8F,0xAA, 0xE6,0x94,0xB6, 0xE7,0xAB,0xAF, 0xE7,0x82,0xB9 } },  // 只收端点
            { "roleLabel(EnvInvert)",      pr::roleLabel (pr::Role::EnvInvert),
              { 0xE5,0x8C,0x85, 0xE7,0xBB,0x9C, 0xE5,0x8F,0x8D, 0xE7,0x9B,0xB8 } },  // 包络反相
        };

        int pass = 0;
        for (const auto& c : cases)
        {
            const auto* utf8 = c.got.toRawUTF8();
            const auto* raw  = reinterpret_cast<const uint8*> (utf8);
            const size_t n   = std::strlen (utf8);

            String hex;
            for (size_t k = 0; k < n; ++k)
                hex << String::toHexString ((int) raw[k]).paddedLeft ('0', 2) << " ";

            const bool ok = (n == c.want.size()) && std::equal (c.want.begin(), c.want.end(), raw);
            if (ok) ++pass;

            line (String (ok ? "  OK   " : "  FAIL ") + String (c.what).paddedRight (' ', 28)
                  + "len " + String ((int) n) + " (want " + String ((int) c.want.size()) + ")  " + hex);
        }

        line ("  " + String (pass) + " / " + String ((int) cases.size()) + " correct");
        line (pass == (int) cases.size()
                ? "  RESULT: PASS - no mojibake"
                : "  RESULT: FAIL - literals still going through CharPointer_ASCII");

        // summary() 是拼接出来的，混了 _u8 段和纯 ASCII 段，单独验一下
        line();
        const auto s = pr::detectProfile (model).summary();
        const auto* sp = s.toRawUTF8();
        line (String ("  summary() valid UTF-8 : ")
              + (CharPointer_UTF8::isValidString (sp, (int) std::strlen (sp)) ? "yes" : "NO"));
        line ("  summary() = " + s);
    }

    head ("11. PROGRAM / PRESET LIST (the missing selector)");

    {
        const int n = plugin->getNumPrograms();
        line ("getNumPrograms()   : " + String (n));
        line ("getCurrentProgram(): " + String (plugin->getCurrentProgram())
              + "  \"" + plugin->getProgramName (plugin->getCurrentProgram()) + "\"");

        if (n <= 1)
        {
            line ("  only one program -> selector would be disabled");
        }
        else
        {
            String names;
            int named = 0;
            for (int i = 0; i < n; ++i)
            {
                const auto nm = plugin->getProgramName (i).trim();
                if (nm.isNotEmpty()) ++named;
                if (i < 24) names << i << ":" << (nm.isEmpty() ? String ("<empty>") : nm) << "  ";
            }
            line ("named programs     : " + String (named) + " / " + String (n));
            line ("first 24           : " + names);

            // 找 bird 5
            int bird = -1;
            for (int i = 0; i < n; ++i)
                if (plugin->getProgramName (i).containsIgnoreCase ("bird 5")) { bird = i; break; }

            if (bird < 0)
                for (int i = 0; i < n; ++i)
                    if (plugin->getProgramName (i).containsIgnoreCase ("bird")) { bird = i; break; }

            line ("bird preset index  : " + String (bird)
                  + (bird >= 0 ? ("  \"" + plugin->getProgramName (bird) + "\"") : String (" (not found)")));

            if (bird >= 0)
            {
                // 切过去，确认轮廓真的变了（不然 preset 选择器就没意义）
                MemoryBlock a; plugin->getStateInformation (a);
                const auto sa = pr::findContourGroups (a);
                pr::NativeContour c0;
                if (sa.size() >= 4) pr::readContour (a, sa.getReference (3), plugin->getCurrentProgram(), c0);

                plugin->setCurrentProgram (bird);
                pump (200);

                line ("after switch       : program " + String (plugin->getCurrentProgram())
                      + "  \"" + plugin->getProgramName (plugin->getCurrentProgram()) + "\"");

                MemoryBlock b; plugin->getStateInformation (b);
                const auto sb = pr::findContourGroups (b);
                pr::NativeContour c1;
                const bool ok = sb.size() >= 4
                             && pr::readContour (b, sb.getReference (3), plugin->getCurrentProgram(), c1);

                if (ok)
                {
                    String l0, l1;
                    for (int i = 0; i < 8; ++i)
                    { l0 << String (c0.levels[(size_t) i], 2) << " "; l1 << String (c1.levels[(size_t) i], 2) << " "; }
                    line ("  EG4 levels blank : " + l0 + "  (used " + String (c0.usedSegments()) + ")");
                    line ("  EG4 levels bird  : " + l1 + "  (used " + String (c1.usedSegments()) + ")");

                    bool differs = false;
                    for (int i = 0; i < pr::ContourLayout::kPoints; ++i)
                        if (std::abs (c0.levels[(size_t) i] - c1.levels[(size_t) i]) > 1.0e-4f) differs = true;

                    line (String ("  contours differ per program : ")
                          + (differs ? "YES - selector is meaningful" : "no  <-- would be pointless"));
                }

                // 关键参数在 bird 上的基座值——codex 说 bird 5 的 tens 已经贴上限
                pr::refreshValues (*plugin, model);
                const int ti = findByName (model, "tens");
                if (ti >= 0)
                    line ("  bird tens base   : norm " + String (model.getReference (ti).value, 4)
                          + "  display " + model.getReference (ti).display
                          + "   (codex 说 bird 5 约 0.85，没有向上余量)"_u8);

                plugin->setCurrentProgram (0);
                pump (150);
            }
        }
    }

    head ("12. NON-INTERPOLATABLE PARAMS (the morph 'layering' bug)");

    {
        // fauna 把 178 个参数全报成连续量，所以 morph 会把**枚举型选择器**
        // 也当连续量插值——在 "dest=r1" 和 "dest=tens" 之间取平均得到第三个
        // 毫无关系的调制目标。9 条 send 同时被接到随机位置 = 凭空多出几层。
        int n = 0;
        String names;
        for (const auto& i : model)
        {
            if (! i.noInterpolate) continue;
            ++n;
            if (n <= 40) names << i.index << ":" << i.name << "  ";
        }
        line ("params excluded from morph interpolation : " + String (n) + " / " + String (model.size()));
        line (names);

        // 分类一下：哪些是因为 Kind 非连续，哪些是因为名字像选择器
        int byKind = 0, byName = 0;
        for (const auto& i : model)
        {
            if (! i.noInterpolate) continue;
            if (i.kind != pr::Kind::Continuous) ++byKind; else ++byName;
        }
        line ("  by kind (toggle/stepped) : " + String (byKind));
        line ("  by name (selector-like)  : " + String (byName));

        // 反向抽查：不该被拦的连续量还在不在
        line();
        line ("sanity - these must still interpolate:");
        for (const char* w : { "tens", "trans", "throat f", "r1-1", "mem x pos" })
        {
            const int mi = findByName (model, w);
            if (mi < 0) continue;
            const auto& i = model.getReference (mi);
            line (String ("  ") + (i.noInterpolate ? "BLOCKED <-- wrong?" : "ok      ")
                  + " " + i.name);
        }
    }

    head ("13. DOES setStateInformation CLOBBER LIVE PARAM EDITS?");

    {
        // 假设：fauna 的 getStateInformation 返回的是**整个 bank**（FBCh，128 个 program），
        // 而 bank 里存的是 program 的**已保存值**，不是你现在听到的实时值。
        // 那么 setStateInformation 会让它从 bank 重载当前 program，
        // 把实时编辑全丢掉——生成变体每次都会"变回 preset 原样"。
        plugin->setCurrentProgram (9);           // bird 5
        pump (200);
        pr::refreshValues (*plugin, model);

        const int ti = findByName (model, "tens");
        const int mi = findByName (model, "mem x pos");
        if (ti < 0 || mi < 0) { line ("  tracked params not found"); }
        else
        {
            auto* pt = plugin->getParameters()[model.getReference (ti).index];
            auto* pm = plugin->getParameters()[model.getReference (mi).index];

            const float t0 = pt->getValue(), m0 = pm->getValue();
            line ("  preset 值      : tens "_u8 + String (t0, 4) + "   mem x " + String (m0, 4));

            // 改成明显不同的值
            const float tWant = (t0 > 0.5f) ? 0.20f : 0.80f;
            const float mWant = (m0 > 0.5f) ? 0.15f : 0.85f;
            pt->setValueNotifyingHost (tWant);
            pm->setValueNotifyingHost (mWant);
            pump (200);
            line ("  手动改成       : tens "_u8 + String (pt->getValue(), 4)
                  + "   mem x " + String (pm->getValue(), 4));

            // 纯往返：读状态，原样写回，什么都不改
            MemoryBlock st;
            plugin->getStateInformation (st);
            plugin->setStateInformation (st.getData(), (int) st.getSize());
            pump (250);

            const float t1 = pt->getValue(), m1 = pm->getValue();
            line ("  原样往返之后   : tens "_u8 + String (t1, 4) + "   mem x " + String (m1, 4));

            const bool clobbered = std::abs (t1 - tWant) > 1.0e-3f || std::abs (m1 - mWant) > 1.0e-3f;
            const bool backToPreset = std::abs (t1 - t0) < 1.0e-3f && std::abs (m1 - m0) < 1.0e-3f;

            line();
            line (String ("  RESULT: ") + (clobbered
                    ? (backToPreset ? "CONFIRMED - 往返把实时编辑冲回了 preset 原值"_u8
                                    : "CLOBBERED - 实时编辑被改掉了（但没回到 preset 值）"_u8)
                    : "clean - 往返不影响实时值，假设不成立"_u8));

            if (clobbered)
                line ("  => randomiseContours 里的 setStateInformation 会造成同样的后果，"
                      "必须在它之后把实时值写回去"_u8);
        }
    }

    head ("14. REPRO: lock A -> edit -> lock B -> generate. Result near A or B?");

    {
        plugin->setCurrentProgram (9);
        pump (200);
        pr::refreshValues (*plugin, model);

        const auto profileR = pr::detectProfile (model);

        // —— 锁基座 A
        auto A = pr::captureSnapshot (*plugin, "A");

        // —— 手动把一批参数改成明显不同的值（模拟"我调出了一个新音色"）
        //     挑的都是 profile 认出来的角色参数，applyMotion 会写它们
        StringArray tracked { "tens", "mem x pos", "mem y pos", "n.lp", "throat f", "len" };
        Array<int> mIdx;
        for (const auto& nm : tracked)
        {
            const int k = findByName (model, nm);
            if (k >= 0) mIdx.add (k);
        }

        for (int k : mIdx)
        {
            auto* p = plugin->getParameters()[model.getReference (k).index];
            const float cur = p->getValue();
            p->setValueNotifyingHost ((cur > 0.5f) ? 0.15f : 0.85f);   // 甩到另一头
        }
        pump (250);
        pr::refreshValues (*plugin, model);

        // —— 锁基座 B
        auto B = pr::captureSnapshot (*plugin, "B");

        line ("  参数            基座A      基座B"_u8);
        for (int k : mIdx)
        {
            const auto pi = model.getReference (k).index;
            line ("  " + model.getReference (k).name.paddedRight (' ', 14)
                  + String (A.values[(size_t) pi], 4).paddedRight (' ', 11)
                  + String (B.values[(size_t) pi], 4));
        }

        // —— 用基座 B 生成变体（和 randomizeNow 里的顺序完全一致）
        Random rr (4242);
        pr::MotionSettings ms;
        ms.semantic = pr::MotionSemantic::Rise;
        ms.motion = 0.65f;
        ms.tone   = 0.0f;          // 关掉音色抖动，让比对干净

        const int onlyGroup = profileR.looksLikeFauna ? 3 : -1;
        const auto cr = pr::randomiseContours (*plugin, ms.semantic, ms.motion, rr,
                                               &B.childState, onlyGroup,
                                               plugin->getCurrentProgram());
        line ();
        line ("  randomiseContours: wrote " + String (cr.written)
              + (cr.failed ? "  FAILED" : ""));

        pr::applyMotion (*plugin, model, checked, profileR, B.values, ms, rr, cr.stages);
        pump (250);
        pr::refreshValues (*plugin, model);

        // —— 结果离 A 近还是离 B 近？
        line ();
        line ("  参数            生成后     |离A|     |离B|    判定"_u8);
        int nearA = 0, nearB = 0;
        for (int k : mIdx)
        {
            const auto pi = model.getReference (k).index;
            const float got = model.getReference (k).value;
            const float dA = std::abs (got - A.values[(size_t) pi]);
            const float dB = std::abs (got - B.values[(size_t) pi]);
            const bool a = dA < dB;
            if (a) ++nearA; else ++nearB;

            line ("  " + model.getReference (k).name.paddedRight (' ', 14)
                  + String (got, 4).paddedRight (' ', 11)
                  + String (dA, 4).paddedRight (' ', 9)
                  + String (dB, 4).paddedRight (' ', 8)
                  + (a ? "-> A  <-- BUG" : "-> B  ok"));
        }

        line ();
        line ("  near A: " + String (nearA) + "   near B: " + String (nearB));
        line (String ("  RESULT: ") + (nearB > nearA
                ? "engine ok - generate does start from the newest base"
                : "BUG CONFIRMED IN ENGINE - generate falls back to the older base"));
    }

    head ("DONE");
    pr::restoreSnapshot (base, *plugin);
    plugin->releaseResources();
    plugin.reset();

    line ("plugin released, base restored.");

    // 纯逻辑那一段的结论也要进退出码，不然 CI 上只看返回值会漏掉它。
    if (pureFails > 0)
        line ("RESULT: FAIL - " + String (pureFails) + " pure-logic check(s) failed (see section 0).");

    return pureFails > 0 ? 3 : 0;
}
