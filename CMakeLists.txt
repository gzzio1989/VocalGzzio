cmake_minimum_required(VERSION 3.22)

project(VocalGzzio VERSION 2.9.0 LANGUAGES C CXX)

# v2.9.0: 構成した瞬間に版を出す。
# ここに出る番号が、画面左上の表示(JucePlugin_VersionString)と
# exe のプロパティ(FILEVERSION)の**両方**の出どころ。
# ★版を上げたときは build フォルダを消してから構成すること。
#   Visual Studio(MSBuild)は juceaide が作る版リソースを日付だけで判断するため、
#   作り置きの build に対して構成し直しても古い番号のまま残ることがある。
#   （「入れ直したのに画面の版が古いまま」の原因はこれ）
message(STATUS "VocalGzzio: バージョン ${PROJECT_VERSION} で構成します")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.12
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(JUCE)

# Plugin formats per platform: AU is added on macOS so the same tree builds
# Mac binaries (e.g. on a GitHub Actions macOS runner). iOS/AUv3 is built
# separately via Xcode (see docs/iOS guide).
if(APPLE)
  set(GZ_FORMATS AU VST3 Standalone)
else()
  set(GZ_FORMATS VST3 Standalone)
endif()

juce_add_plugin(VocalGzzio
    PLUGIN_MANUFACTURER_CODE  Gzzo
    PLUGIN_CODE               Vcgj
    COMPANY_NAME              "Gzzio"
    BUNDLE_ID                 "com.gzzio.vocalgzzio"
    FORMATS                   ${GZ_FORMATS}
    PRODUCT_NAME              "VocalGzzio"
    VST3_CATEGORIES           Fx Dynamics EQ
    # v1.9.0: ship the VST3 WITHOUT a moduleinfo.json manifest. The JUCE 8 auto-
    # manifest (VST3 SDK "moduleinfo") is the common failure point for VST3 scans
    # in several newer hosts (SONAR 2025, FL Studio 2025, Reason, Waveform, JUCE
    # hosts): if the class UID written into the manifest does not match the
    # binary's factory UID, findClassMatchingDescription() returns no match and
    # the host silently excludes the plug-in. On Windows the manifest step can
    # also fail/produce a blank file ($(PROCESSOR_ARCHITECTURE) quirk). With the
    # manifest disabled, hosts fall back to loading the module and enumerating its
    # factory directly (the classic, universally-supported path) — a hair slower
    # to scan, but recognised everywhere.
    VST3_AUTO_MANIFEST        FALSE
    IS_SYNTH                  FALSE
    # v2.1.0: MIDIスイッチ（フットスイッチ/パッドでA/B・ひびき等を切替）のため
    # MIDI入力バスを有効にする。これがFALSEだとVST3にMIDI入力端子が生えず、
    # DAWからMIDIをルーティングできない（単体起動版も受け取れない）。
    NEEDS_MIDI_INPUT          TRUE
    NEEDS_MIDI_OUTPUT         FALSE)

juce_add_binary_data(VocalGzzioAssets
    SOURCES Resources/character.png Resources/puniguji_kawaii.png Resources/kawaii_font.ttf)

target_sources(VocalGzzio PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

# v1.9.2: 日本語ロケールのWindowsではMSVCがソースをCP932として読み、
# 全角で終わるコメント行が次の行を巻き込んでコメント化してしまう。
# /utf-8 を渡してUTF-8として解釈させる（C4819警告も消える）。
if (MSVC)
    target_compile_options(VocalGzzio PRIVATE /utf-8)
endif()

# v1.9.4: 体験版ビルド。-DVOCALGZZIO_TRIAL=ON で有効。
# 60秒ごとに0.6秒だけ音量が下がるので、音質は評価できるが制作には使えない。
option(VOCALGZZIO_LITE "Build the free Lite edition (no pitch tools)" OFF)
if (VOCALGZZIO_LITE)
    target_compile_definitions(VocalGzzio PUBLIC VOCALGZZIO_LITE=1)
    message(STATUS "VocalGzzio: building LITE edition")
endif()

option(VOCALGZZIO_TRIAL "Build the time-limited trial version" OFF)
if (VOCALGZZIO_TRIAL)
    target_compile_definitions(VocalGzzio PUBLIC VOCALGZZIO_TRIAL=1)
    message(STATUS "VocalGzzio: building TRIAL version")
endif()

target_compile_definitions(VocalGzzio PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(VocalGzzio PRIVATE
    VocalGzzioAssets
    juce::juce_audio_utils
    juce::juce_dsp
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)


