#pragma once

#include <juce_core/juce_core.h>

namespace gz
{
inline bool isTestDataIsolated() noexcept
{
   #if VOCALGZZIO_TESTING
    return juce::SystemStats::getEnvironmentVariable ("VOCALGZZIO_TEST_DATA_DIR", {}).isNotEmpty();
   #else
    return false;
   #endif
}

// 検証版の保存先を通常版から隔離する（VOCALGZZIO_TEST_DATA_DIR 指定時のみ隔離）。
inline juce::File dataDirectory()
{
   #if VOCALGZZIO_TESTING
    const auto path = juce::SystemStats::getEnvironmentVariable (
        "VOCALGZZIO_TEST_DATA_DIR", {});
    if (path.isNotEmpty())
        return juce::File::getCurrentWorkingDirectory().getChildFile (path);
   #endif
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("VocalGzzio");
}
}
