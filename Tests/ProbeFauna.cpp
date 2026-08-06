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
#include "MotionEngine.h"
#include "ContourIO.h"

#include <iostream>

using namespace juce;

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
        line ("RESULT: FAIL - no plugin description could be produced from any candidate path.");
        return 1;
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
                          + "   (codex 说 bird 5 约 0.85，没有向上余量)");

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

    head ("DONE");
    pr::restoreSnapshot (base, *plugin);
    plugin->releaseResources();
    plugin.reset();

    line ("plugin released, base restored.");
    return 0;
}
