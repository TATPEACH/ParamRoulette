cmake_minimum_required(VERSION 3.22)

project(ParamRoulette VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set_property(GLOBAL PROPERTY USE_FOLDERS YES)

if (MSVC)
    # 源码里有中文字符串字面量。不加 /utf-8 的话 MSVC 按系统代码页（GBK）解释源文件，
    # 界面上全是乱码；JUCE 的 String(const char*) 又假定入参是 UTF-8。
    add_compile_options(/utf-8)
endif()

# 构建后把 .vst3 拷到 C:\Program Files\Common Files\VST3 需要管理员权限，
# 所以默认关掉。要开就 -DPR_COPY_AFTER_BUILD=ON 并用管理员身份构建。
option(PR_COPY_AFTER_BUILD "构建后把 .vst3 装到系统 VST3 目录（需要管理员权限）" OFF)

# ---------------------------------------------------------------- VST2
# JUCE 8 的 juce_VSTPluginFormat.cpp 硬性 include <pluginterfaces/vst2.x/aeffect.h>，
# 但 JUCE 早就不随包分发它了。头文件放在 vst2sdk/ 下，出处和授权见那边的 README。
#
# 没有它就只能托管 VST3，也就**碰不到 fauna**（32 位 VST2）。
set(PR_VST2_SDK "${CMAKE_CURRENT_SOURCE_DIR}/vst2sdk")
if (EXISTS "${PR_VST2_SDK}/pluginterfaces/vst2.x/aeffect.h")
    set(PR_HAS_VST2 TRUE)
    message(STATUS "ParamRoulette: 找到 VST2 SDK -> 开启 VST2 托管")
else()
    set(PR_HAS_VST2 FALSE)
    message(WARNING "ParamRoulette: 没找到 ${PR_VST2_SDK}/pluginterfaces/vst2.x/aeffect.h"
                    " —— 只能托管 VST3，fauna 这类 VST2 插件用不了")
endif()

# 32 位构建才能加载 fauna。判据是指针宽度，不是生成器参数——
# 用 -A Win32 配出来的 build 目录会让这里变成 4。
if (CMAKE_SIZEOF_VOID_P EQUAL 4)
    set(PR_ARCH_TAG "x86")
    set(PR_NAME_SUFFIX " x86")
    set(PR_CODE_FX  "Pr32")
    set(PR_CODE_SYN "Ps32")
    set(PR_VST2_DEF 1)      # 32 位构建的全部意义就是加载 32 位 VST2，不许关
    message(STATUS "ParamRoulette: 32 位构建（能原生加载 32 位 VST2，如 fauna）")
else()
    set(PR_ARCH_TAG "x64")
    set(PR_NAME_SUFFIX "")
    set(PR_CODE_FX  "Prlt")
    set(PR_CODE_SYN "Prls")
    set(PR_VST2_DEF 0)
    message(STATUS "ParamRoulette: 64 位构建（加载不了 32 位插件，这是 Windows 加载器的限制）")
endif()

if (PR_HAS_VST2)
    set(PR_VST2_DEF 1)
else()
    if (PR_VST2_DEF)
        message(FATAL_ERROR "32 位构建必须有 VST2 SDK，否则它没有存在意义。"
                            "把 aeffect.h / aeffectx.h 放到 ${PR_VST2_SDK}/pluginterfaces/vst2.x/")
    endif()
endif()

# ---------------------------------------------------------------- JUCE
include(FetchContent)

# 8.0.10 是最后一个自带 VST3 SDK 的 tag（modules/juce_audio_processors/format_types/VST3_SDK/）。
# master 和 9.0.0 已经把它移掉了，换上去会因为找不到 SDK 头文件而配置失败。别动这个版本号。
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.10
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE)
FetchContent_MakeAvailable(JUCE)

# ---------------------------------------------------------------- 源码
set(PR_SOURCES
    Source/ParamModel.cpp
    Source/Randomizer.cpp
    Source/SnapshotBank.cpp
    Source/MotionEngine.cpp
    Source/ContourIO.cpp
    Source/PluginScanner.cpp
    Source/HostedPluginWindow.cpp
    Source/ParamListView.cpp
    Source/MorphPad.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

set(PR_HEADERS
    Source/Utf8.h
    Source/ParamModel.h
    Source/Randomizer.h
    Source/SnapshotBank.h
    Source/MotionEngine.h
    Source/ContourIO.h
    Source/PluginScanner.h
    Source/HostedPluginWindow.h
    Source/ParamListView.h
    Source/MorphPad.h
    Source/PluginProcessor.h
    Source/PluginEditor.h)

# ---------------------------------------------------------------- 两个 target
# 32 位版和 64 位版是**两个不同的插件**（名字、PLUGIN_CODE、BUNDLE_ID 都不同），
# 不是同一个插件的两个架构。故意这么做的：合成一个 bundle 的话 DAW 会优先挑原生的
# 64 位那份，我们就永远走不到 32 位那条路，也就碰不到 fauna。
#
# 效果器版：有音频输入，挂在音轨上托管效果类插件
juce_add_plugin(ParamRoulette
    COMPANY_NAME                "taozisang"
    BUNDLE_ID                   "com.taozisang.paramroulette.${PR_ARCH_TAG}"
    PLUGIN_MANUFACTURER_CODE    Tzsg
    PLUGIN_CODE                 ${PR_CODE_FX}
    PRODUCT_NAME                "ParamRoulette${PR_NAME_SUFFIX}"
    FORMATS                     VST3 Standalone
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            TRUE      # 托管的效果器可能是 MIDI 控制的
    NEEDS_MIDI_OUTPUT           TRUE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE      # filter 输入框要能打字，缺这个很多宿主会吞键
    COPY_PLUGIN_AFTER_BUILD     ${PR_COPY_AFTER_BUILD}
    VST3_CATEGORIES             Fx Tools)

# 乐器版：无音频输入，托管合成器（fauna 是 VSTi，走这个）
juce_add_plugin(ParamRouletteSynth
    COMPANY_NAME                "taozisang"
    BUNDLE_ID                   "com.taozisang.paramroulettesynth.${PR_ARCH_TAG}"
    PLUGIN_MANUFACTURER_CODE    Tzsg
    PLUGIN_CODE                 ${PR_CODE_SYN}
    PRODUCT_NAME                "ParamRoulette Synth${PR_NAME_SUFFIX}"
    FORMATS                     VST3 Standalone
    IS_SYNTH                    TRUE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           TRUE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD     ${PR_COPY_AFTER_BUILD}
    VST3_CATEGORIES             Instrument Synth)

foreach (target ParamRoulette ParamRouletteSynth)
    target_sources(${target} PRIVATE ${PR_SOURCES} ${PR_HEADERS})

    # VST2 SDK 用 PUBLIC：juce_VSTPluginFormat.cpp 是作为**这个** target 的一部分
    # 编译的，include 路径必须覆盖到它，不只是覆盖我们自己的源文件。
    target_include_directories(${target} PUBLIC Source ${PR_VST2_SDK})

    target_compile_definitions(${target} PUBLIC
        JUCE_PLUGINHOST_VST3=1        # 这一条是整个项目的地基：没它就不是宿主
        JUCE_PLUGINHOST_VST=${PR_VST2_DEF}   # 要 vst2sdk/ 里那两个头文件才能开
        JUCE_PLUGINHOST_AU=0
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_STRICT_REFCOUNTEDPOINTER=1
        JUCE_MODAL_LOOPS_PERMITTED=0) # 插件里开模态循环会锁死宿主
    # 没有 JUCE_DISPLAY_SPLASH_SCREEN：JUCE 8 已经不做启动画面了，
    # 设了只会换来一条 "the flag is ignored" 警告。

    target_link_libraries(${target}
        PRIVATE
            juce::juce_audio_utils      # 连带 audio_processors / audio_devices / gui_extra
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)

    source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/Source" PREFIX Source
                 FILES ${PR_SOURCES} ${PR_HEADERS})
endforeach()

# ---------------------------------------------------------------- 无头验收测试
# 只编纯逻辑那四个 .cpp（PluginProcessor / PluginEditor 依赖 JucePlugin_* 宏，
# 只有插件 target 里才有）。这个程序真加载子插件、真跑随机化和体检，
# 是唯一能回答"fauna 到底加载得了吗"的东西——读代码断言不了。
juce_add_console_app(pr_probe PRODUCT_NAME "pr_probe")

target_sources(pr_probe PRIVATE
    Tests/ProbeFauna.cpp
    Source/ParamModel.cpp
    Source/Randomizer.cpp
    Source/SnapshotBank.cpp
    Source/MotionEngine.cpp
    Source/ContourIO.cpp)

target_include_directories(pr_probe PUBLIC Source ${PR_VST2_SDK})

target_compile_definitions(pr_probe PUBLIC
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_VST=${PR_VST2_DEF}
    JUCE_PLUGINHOST_AU=0
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    JUCE_MODAL_LOOPS_PERMITTED=1   # 只有这里要：runDispatchLoopUntil 给 InertProbe 转消息循环
    JUCE_STANDALONE_APPLICATION=1)

target_link_libraries(pr_probe
    PRIVATE juce::juce_audio_utils
    PUBLIC  juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)
