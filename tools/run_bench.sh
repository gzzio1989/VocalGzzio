#!/usr/bin/env bash
# 全チェーン負荷・遅延ベンチ。事前に本体を一度ビルドしておくこと(cmake --build build)。
# flags.make のクォート(-DJucePlugin_Name=\"...\")はシェル経由だと壊れるので、
# Python でそのまま分解して g++ に渡す。
set -e
cd "$(dirname "$0")/.."
python3 - <<'EOF'
import subprocess, shlex, re, sys
B = 'build'
txt = open(f'{B}/CMakeFiles/VocalGzzio.dir/flags.make').read()
defs = re.search(r'CXX_DEFINES = (.*)', txt).group(1)
incs = re.search(r'CXX_INCLUDES = (.*)', txt).group(1)
args = ['g++','-O2','-std=c++17'] + shlex.split(defs) + shlex.split(incs) + \
       ['-ISource','tools/bench_plugin.cpp',
        f'{B}/VocalGzzio_artefacts/Release/libVocalGzzio_SharedCode.a', f'{B}/libVocalGzzioAssets.a',
        '/usr/lib/x86_64-linux-gnu/libasound.so','/usr/lib/x86_64-linux-gnu/libfontconfig.so',
        '/usr/lib/x86_64-linux-gnu/libfreetype.so','-ldl','-lpthread','-lrt','-o','/tmp/bench_plugin']
sys.exit(subprocess.run(args).returncode)
EOF
/tmp/bench_plugin
