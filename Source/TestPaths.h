#pragma once

#include <juce_core/juce_core.h>

namespace gz
{
inline bool isTestDataIsolated() noexcept
{
   #if VOCALGZZIO_TESTING
    // テスト・スナップショット用バイナリ単体起動時（環境変数未指定時）でも実ユーザーの AppData を絶対に汚染しないよう常時隔離する
    return true;
   #else
    return false;
   #endif
}

// 検証版の保存先を通常版から常時隔離する。
inline juce::File dataDirectory()
{
   #if VOCALGZZIO_TESTING
    const auto path = juce::SystemStats::getEnvironmentVariable (
        "VOCALGZZIO_TEST_DATA_DIR", {});
    if (path.isNotEmpty())
        return juce::File::isAbsolutePath (path)
                   ? juce::File (path)
                   : juce::File::getCurrentWorkingDirectory().getChildFile (path);
    return juce::File::getSpecialLocation (juce::File::tempDirectory)
        .getChildFile ("VocalGzzio_TestIsolated");
   #else
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("VocalGzzio");
   #endif
}
}
