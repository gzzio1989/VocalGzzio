#pragma once

#include <juce_core/juce_core.h>

namespace gz
{
// 検証版の保存先を通常版から隔離する。製品版は環境変数に影響されない。
inline juce::File dataDirectory()
{
   #if VOCALGZZIO_TESTING
    const auto path = juce::SystemStats::getEnvironmentVariable (
        "VOCALGZZIO_TEST_DATA_DIR", {});
    return juce::File::getCurrentWorkingDirectory().getChildFile (
        path.isNotEmpty() ? path : juce::String (".vocalgzzio-test-data"));
   #else
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("VocalGzzio");
   #endif
}
}
