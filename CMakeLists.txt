cmake_minimum_required(VERSION 3.22)

project(ParamRoulette VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set_property(GLOBAL PROPERTY USE_FOLDERS YES)

if (MSVC)
    # 源码里有中文字符串字面量。不加 /utf-8 的话 MSVC 按系统代码页（GBK）解释源文件，
    # 界面上全是乱码；JUCE 的 String(const char*) 又假定入参是 UTF-8。
    #
    # /MP<N>：**这是唯一真正管用的编译并发开关。**
    # JUCE 的 juce_recommended_config_flags 会加一个裸 `/MP`
    # （见 JUCEHelperTargets.cmake:123），它按逻辑核数派生子进程，而且
    # **完全无视 MSBuild 的 -m**——所以 `cmake --build -- -m:4` 根本限不住编译并发，
    # 只限住了同时构建几个项目。
    #
    # 满载时子进程 cl.exe 会崩，父进程报 `error D8040 创建子进程或与子进程通讯时出错`
    # ——那个错误码是 /MP 专属的，没有 /MP 就不会出现。降低这个数既能减载，
    # 也能让真正的编译错误以正常诊断的形式出现，而不是变成一句"子进程失联"。
    set(PR_COMPILE_JOBS "6" CACHE STRING
        "并行编译进程数（覆盖 JUCE 的裸 /MP = 逻辑核数）。构建不稳就调小，比如 2")
    add_compile_options(/utf-8 /MP${PR_COMPILE_JOBS})
    message(STATUS "ParamRoulette: 编译并发 /MP${PR_COMPILE_JOBS}")
endif()

# 构建后把 .vst3 拷到 C:\Program Files\Common Files\VST3 需要管理员权限，
# 所以默认关掉。要开就 -DPR_COPY_AFTER_BUILD=ON 并用管理员身份构建。
option(PR_COPY_AFTER_BUILD "构建后把 .vst3 装到系统 VST3 目录（需要管理员权限）" OFF)

# ---------------------------------------------------------------- LTO
# 默认**关掉**全程序优化。
#
# 理由很简单：LTCG 的代码生成阶段是整个构建里最重的一步（单线程、吃内存、
# 持续满载几分钟），而这个项目的音频热路径就是把 buffer 转手给子插件，
# LTO 的收益近乎为零。想开就 -DPR_ENABLE_LTO=ON。
#
# 下面这段是踩坑记录，留着免得以后再走一遍弯路。
#
# 开发期间这台机器上反复出现 `fatal error C1001: 内部编译器错误`，报错位置
# 每次都不一样（tuple / pngrtran.c / jfdctint.c / xmemory / hb-iter.hh），
# 外加 `D8040 创建子进程或与子进程通讯时出错`，还蓝屏两次
# （0x1E KMODE_EXCEPTION_NOT_HANDLED、0x7F arg1=8 双重错误）。
#
# 先后误判成两件事，**两个都不对**：
#   1. "MSVC 19.50 的 LTCG 后端 bug" —— 因为报错发生在"正在生成代码"阶段、
#      编译器内部路径是 Utc/src/p2（后端代码生成器）。看着很像，但不是。
#   2. "i7-14700KF 硅劣化" —— 13/14 代 K 系列确实有公认的稳定性缺陷，
#      而且我查到微码已是最新的 0x12F，就推断是已经劣化。也不是。
#
# 真正原因：内存 XMP 从 profile 1 调到了 profile 2。改回去就好了。
#
# 教训值得记下来：`Win32_PhysicalMemory` **不暴露时序**。我查到"额定 3200、
# 实配 3200，没超频"就据此排除了内存——那是错的推理，同频率下更紧的时序在
# WMI 里长得一模一样。**随机位置的编译器 ICE + 重负载下双重错误，
# 第一嫌疑永远是内存**，别绕到编译器或 CPU 上去。
option(PR_ENABLE_LTO "开启全程序优化（LTCG）。它是构建里最重的一步，收益对本项目近乎为零" OFF)

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
    Source/ChainRunner.cpp
    Source/OutputGuard.cpp
    Source/PluginScanner.cpp
    Source/HostedPluginWindow.cpp
    Source/Theme.cpp
    Source/GridLookAndFeel.cpp
    Source/Widgets.cpp
    Source/ChainStrip.cpp
    Source/ParamListView.cpp
    Source/PluginTagView.cpp
    Source/MorphPad.cpp
    Source/PluginProcessor.cpp
    Source/EditorBase.cpp
    Source/GridEditor.cpp
    Source/HwTheme.cpp
    Source/HardwareLookAndFeel.cpp
    Source/HwWidgets.cpp
    Source/HwPanels.cpp
    Source/HardwareEditor.cpp
    Source/RackTheme.cpp
    Source/RackLookAndFeel.cpp
    Source/RackWidgets.cpp
    Source/RackPanels.cpp
    Source/RackBrowser.cpp
    Source/RackEditor.cpp
    Source/PluginEditor.cpp)

set(PR_HEADERS
    Source/Utf8.h
    Source/ParamModel.h
    Source/Randomizer.h
    Source/SnapshotBank.h
    Source/HistoryRing.h
    Source/MotionEngine.h
    Source/ContourIO.h
    Source/ChainDefs.h
    Source/ChainRunner.h
    Source/OutputGuard.h
    Source/HostedSlot.h
    Source/PluginScanner.h
    Source/HostedPluginWindow.h
    Source/Theme.h
    Source/GridLookAndFeel.h
    Source/Widgets.h
    Source/ChainStrip.h
    Source/ParamListView.h
    Source/PluginTagView.h
    Source/MorphPad.h
    Source/PluginProcessor.h
    Source/EditorBase.h
    Source/GridEditor.h
    Source/HwTheme.h
    Source/HardwareLookAndFeel.h
    Source/HwWidgets.h
    Source/HwPanels.h
    Source/HardwareEditor.h
    Source/RackTheme.h
    Source/RackLookAndFeel.h
    Source/RackWidgets.h
    Source/RackPanels.h
    Source/RackBrowser.h
    Source/RackEditor.h
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
            juce::juce_recommended_warning_flags)

    # LTO 默认关，见文件开头 PR_ENABLE_LTO 的说明（MSVC 19.50 的 LTCG 会崩）
    if (PR_ENABLE_LTO)
        target_link_libraries(${target} PUBLIC juce::juce_recommended_lto_flags)
    endif()

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
    Source/ContourIO.cpp
    Source/ChainRunner.cpp
    Source/OutputGuard.cpp
    # 只为了测 guessTag / fnTagLabel（都是 static / free 函数，不构造 PluginScanner，
    # 所以不会碰 PropertiesFile 和插件表文件）。
    Source/PluginScanner.cpp)

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

# ---------------------------------------------------------------- 界面截图
# 构造真的 processor + editor，装几个假插件，把整个界面画进 PNG。
# 原生插件窗口只有在 DAW 里才看得到——没有这个工具，所有视觉改动都是盲改。
#
#   cmake --build build --target pr_uisnap --config Release
#   build/pr_uisnap_artefacts/Release/pr_uisnap.exe        # → build/ui_snap/*.png
#
# 两个测试专用的宏，正式插件里都没有：
#   PR_TEST_HOOKS             —— processor 多出一个同步装插件的 installForTesting()
#   PR_SETTINGS_NAME_OVERRIDE —— 设置文件改名，免得析构时写坏用户真正的插件表
juce_add_console_app(pr_uisnap PRODUCT_NAME "pr_uisnap")

target_sources(pr_uisnap PRIVATE Tests/UiSnapshot.cpp ${PR_SOURCES})
target_include_directories(pr_uisnap PUBLIC Source ${PR_VST2_SDK})

target_compile_definitions(pr_uisnap PUBLIC
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_VST=${PR_VST2_DEF}
    JUCE_PLUGINHOST_AU=0
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    JUCE_MODAL_LOOPS_PERMITTED=1      # runDispatchLoopUntil 要它
    JUCE_STANDALONE_APPLICATION=1
    PR_TEST_HOOKS=1
    "PR_SETTINGS_NAME_OVERRIDE=\"ParamRoulette-uisnap\""
    # 插件 target 里这两个宏由 juce_add_plugin 提供；这里是普通程序，得自己给
    "JucePlugin_Name=\"ParamRoulette\""
    JucePlugin_IsSynth=0)

target_link_libraries(pr_uisnap
    PRIVATE juce::juce_audio_utils
    PUBLIC  juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)
