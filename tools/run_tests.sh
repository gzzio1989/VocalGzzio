#!/usr/bin/env bash
# Windowsと同じCMake/CTestの一覧を使う。欠けた検査を成功として扱わない。
set -euo pipefail
repo=$(cd "$(dirname "$0")/.." && pwd)
build_dir=${VOCALGZZIO_TEST_BUILD:-"$repo/build/validation"}
configuration=${VOCALGZZIO_TEST_CONFIGURATION:-Release}
jobs=${VOCALGZZIO_TEST_JOBS:-3}
args=(-S "$repo" -B "$build_dir" "-DCMAKE_BUILD_TYPE=$configuration"
      -DVOCALGZZIO_BUILD_TESTS=ON -DVOCALGZZIO_TRIAL=OFF -DVOCALGZZIO_LITE=OFF)
if [[ -f "$repo/build/_deps/juce-src/CMakeLists.txt" ]]; then
    args+=("-DFETCHCONTENT_SOURCE_DIR_JUCE=$repo/build/_deps/juce-src")
fi
cmake "${args[@]}"
cmake --build "$build_dir" --config "$configuration" --target VocalGzzioTests --parallel "$jobs"
ctest --test-dir "$build_dir" -C "$configuration" --output-on-failure --no-tests=error --output-junit "$build_dir/results.xml" "$@"
