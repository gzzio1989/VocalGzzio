#!/usr/bin/env bash
# VocalGzzio 検証ツールをまとめて回す（Linux / WSL / MSYS どれでも）
# 使い方: tools/ の中で  bash run_tests.sh
set -u
cd "$(dirname "$0")"
JUCE=${JUCE_MODULES:-../build/_deps/juce-src/modules}
fail=0

run () {   # run <名前> <ビルドコマンド...>
  local name=$1; shift
  printf '\n========== %s ==========\n' "$name"
  if ! "$@"; then echo "!! ビルド失敗: $name"; fail=1; return; fi
  if ! "/tmp/$name"; then fail=1; fi
}

# --- JUCE に依存しない DSP テスト ---
for t in dsp_ornament dsp_hum dsp_consonant dsp_resonance dsp_popclick dsp_ride dsp_mixalign dsp_longrun; do
  [ -f "$t.cpp" ] || continue
  run "$t" g++ -O2 -std=c++17 -I../Source "$t.cpp" -o "/tmp/$t"
done

# --- ボイス変換のメモリ安全性（ASan + UBSan） ---
if [ -f dsp_voiceshift_stress.cpp ]; then
  run dsp_voiceshift_stress g++ -O1 -g -std=c++17 -fsanitize=address,undefined \
      -I../Source dsp_voiceshift_stress.cpp -o /tmp/dsp_voiceshift_stress
fi

# --- 「音声コールバックの中でメモリを確保しない」の実測（JUCE が必要） ---
if [ -f dsp_noalloc.cpp ] && [ -d "$JUCE" ]; then
  cat > /tmp/juce_stub.cpp <<'EOF'
namespace juce { extern const char* const juce_compilationDate; extern const char* const juce_compilationTime;
                 const char* const juce_compilationDate = __DATE__; const char* const juce_compilationTime = __TIME__; }
EOF
  printf '\n========== dsp_noalloc ==========\n'
  if g++ -O2 -std=c++17 -DJUCE_STANDALONE_APPLICATION=0 -DJUCE_MODULE_AVAILABLE_juce_dsp=1 \
        -DJUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1 -DJUCE_USE_CURL=0 -DJUCE_WEB_BROWSER=0 -DNDEBUG=1 \
        -I"$JUCE" dsp_noalloc.cpp /tmp/juce_stub.cpp \
        "$JUCE/juce_dsp/juce_dsp.cpp" "$JUCE/juce_core/juce_core.cpp" \
        "$JUCE/juce_audio_basics/juce_audio_basics.cpp" "$JUCE/juce_audio_formats/juce_audio_formats.cpp" \
        "$JUCE/juce_events/juce_events.cpp" -o /tmp/dsp_noalloc -ldl -lpthread 2>/dev/null
  then /tmp/dsp_noalloc || fail=1
  else echo "(スキップ: JUCE のモジュールが見つからないか、ビルドに失敗)"; fi
else
  printf '\n(dsp_noalloc はスキップ: JUCE_MODULES を指定すると回せます)\n'
fi

# --- X投稿文の文字数（重み付き 280 まで） ---
printf '\n========== X投稿文の文字数 ==========\n'
for f in ../X投稿文_v2.*.md; do
  [ -f "$f" ] || continue
  out=$(python3 xlen.py "$f")
  echo "$out" | grep -q 'NG!' && { echo "$f"; echo "$out"; fail=1; }
done
echo "上限を超えている投稿文: $( [ $fail -eq 0 ] && echo なし || echo あり )"

# --- MSVC の16進エスケープ食い込み検査（Linux では警告すら出ないので必須） ---
printf '\n========== MSVC 16進エスケープ検査 ==========\n'
if grep -rnoP '\\x[0-9a-fA-F]{2}[0-9a-fA-F]' ../Source/*.h ../Source/*.cpp; then
  echo "!! 上の行は MSVC で error C7744 になります（文字列を \" \" で分割してください）"; fail=1
else
  echo "問題なし"
fi

printf '\n===== %s =====\n' "$( [ $fail -eq 0 ] && echo '全部 PASS' || echo 'FAIL あり' )"
exit $fail
