cmake_minimum_required(VERSION 3.22)

project(VocalGzzio VERSION 4.2.0 LANGUAGES C CXX)
# ★2026-08-18: 3.0.0 → 3.1.0。v3.0.0 は 8/13 に GitHub でリリース済みのため、
#  開発版の番号を1つ進めた（リリース済みの番号を開発で使い回さない）。

# ★2026-08-13: 2.12.0 → 3.0.0 に上げた。
#  v3.0 の開発中もずっと 2.12.0 のままだったので、
#   ・画面左上の表示
#   ・exe のプロパティ
#   ・インストーラーのファイル名（Installer.iss がここを読む）
#  が すべて「リリース済みの v2.12.0」と同じ番号になっていた。
#  実際に installer_output\VocalGzzio_Setup_v2.12.0.exe が
#  開発版に上書きされる事故が起きたので、番号ごと切り離した。
#  開発中のインストーラーには installer.bat が -dev を付ける。

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

# v3.0 体験版は「別の製品」としてビルドする（詳細は下の option の所）。
#  名前と PLUGIN_CODE を変える → 製品版と同じPCに共存でき、DAWも別物として見る。
#  ※ option() は下にあるが、-DVOCALGZZIO_TRIAL=ON はキャッシュに先に入るのでここで読める。
if (VOCALGZZIO_TRIAL)
    set(GZ_PRODUCT "VocalGzzio Trial")
    set(GZ_PLUGCODE Vcgt)
    set(GZ_BUNDLE "com.gzzio.vocalgzzio.trial")
    message(STATUS "VocalGzzio: 体験版として構成します (PRODUCT_NAME='VocalGzzio Trial')")
else()
    set(GZ_PRODUCT "VocalGzzio")
    set(GZ_PLUGCODE Vcgj)
    set(GZ_BUNDLE "com.gzzio.vocalgzzio")
endif()

juce_add_plugin(VocalGzzio
    PLUGIN_MANUFACTURER_CODE  Gzzo
    PLUGIN_CODE               ${GZ_PLUGCODE}
    COMPANY_NAME              "Gzzio"
    BUNDLE_ID                 "${GZ_BUNDLE}"
    FORMATS                   ${GZ_FORMATS}
    PRODUCT_NAME              "${GZ_PRODUCT}"
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "歌声や楽器を音声入力から取り込み、加工して再生します。"
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
    Source/Overview.cpp
    Source/PluginEditor.cpp)

# v1.9.2: 日本語ロケールのWindowsではMSVCがソースをCP932として読み、
# 全角で終わるコメント行が次の行を巻き込んでコメント化してしまう。
# /utf-8 を渡してUTF-8として解釈させる（C4819警告も消える）。
if (MSVC)
    target_compile_options(VocalGzzio PRIVATE /utf-8)
endif()

# ==================================================================
#  版（エディション）の切り替え   ★ここを取り違えると売上が消えます★
# ------------------------------------------------------------------
#  v4.0.0 で整理。以前はこの説明文が LITE の真上に置かれていて、
#  「体験版を作るつもりで LITE をONにする」と、時間制限のない
#  ほぼ製品版を無料配布してしまう配置になっていた。
#
#  VOCALGZZIO_TRIAL … 体験版（BOOTH 無料ダウンロード用）。
#      60秒ごとに0.6秒だけ音量が -18dB まで滑らかに下がる。
#      画面のバージョン表示の右に「　体験版」と出る。
#
#  VOCALGZZIO_LITE  … 別物。時間制限は無い。
#      声質変換・ハモリ・音程補正 を丸ごと外した機能制限版。
#      ★これは「無料版」ではありません。BOOTH では使いません。
#
#  製品版(2,500円) = 両方 OFF        体験版(無料) = TRIAL だけ ON
# ==================================================================
option(VOCALGZZIO_LITE  "Feature-limited edition (no pitch tools). This is NOT the free trial." OFF)
option(VOCALGZZIO_TRIAL "Free trial: output dips 0.6s every 60s" OFF)

if (VOCALGZZIO_LITE AND VOCALGZZIO_TRIAL)
    message(FATAL_ERROR
        "VOCALGZZIO_LITE と VOCALGZZIO_TRIAL は同時に立てられません。"
        " 無料配布するのは TRIAL の方です。")
endif()

if (VOCALGZZIO_LITE)
    target_compile_definitions(VocalGzzio PUBLIC VOCALGZZIO_LITE=1)
    message(STATUS "**************************************************")
    message(STATUS "  VocalGzzio: LITE 版（機能制限・時間制限なし）")
    message(STATUS "  これは体験版ではありません。無料で配るなら")
    message(STATUS "  -DVOCALGZZIO_TRIAL=ON を使ってください。")
    message(STATUS "**************************************************")
elseif (VOCALGZZIO_TRIAL)
    target_compile_definitions(VocalGzzio PUBLIC VOCALGZZIO_TRIAL=1)
    message(STATUS "==================================================")
    message(STATUS "  VocalGzzio: 体験版（60秒ごとに0.6秒ディップ）")
    message(STATUS "  → 無料配布用。zip 名に _体験版 を必ず付けること")
    message(STATUS "==================================================")
else()
    message(STATUS "==================================================")
    message(STATUS "  VocalGzzio: 製品版（制限なし・販売用）")
    message(STATUS "==================================================")
endif()

# ---- JUCE 8 のライセンス表示について（2026-08 調査） ----------------
#  JUCE 8 はスプラッシュ画面そのものを廃止した。
#  juce_gui_basics.cpp:60 に
#    #ifdef JUCE_DISPLAY_SPLASH_SCREEN
#      JUCE_COMPILER_WARNING("This version of JUCE does not use the
#                             splash screen, the flag ... is ignored")
#  とあり、この旗を立てると**警告が増えるだけ**で何も起きない。
#  JUCE_REPORT_APP_USAGE も JUCE 8 のソースには存在しない。
#  よって Starter(無料・年商2万ドル未満)で商用配布する場合でも、
#  ここに書き足す必要はない。書き足さないのが正解。
# --------------------------------------------------------------------
target_compile_definitions(VocalGzzio PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

option(VOCALGZZIO_BUILD_TESTS "Build the regression checks with CTest" OFF)
target_link_libraries(VocalGzzio PRIVATE
    VocalGzzioAssets
    juce::juce_audio_utils
    juce::juce_dsp
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
if (NOT VOCALGZZIO_BUILD_TESTS)
    target_link_libraries(VocalGzzio PUBLIC juce::juce_recommended_lto_flags)
endif()

# 同じ実装と設定で、Windows / macOS / Linux の回帰検査を実行する。
# 通常の配布用ビルドでは検査と設定保存先の切替を無効にしておく。
if (VOCALGZZIO_BUILD_TESTS)
    enable_testing()
    target_compile_definitions(VocalGzzio PUBLIC VOCALGZZIO_TESTING=1)
    add_subdirectory(tools)
endif()


