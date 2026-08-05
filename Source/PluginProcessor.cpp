#include <cmath>
#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
VocalGzzioProcessor::VocalGzzioProcessor()
    : AudioProcessor (BusesProperties()
        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
        .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    // v1.5.0 autosave: restore the last-used settings even when the host never
    // hands us a saved project state (fresh insert, unsaved project, standalone).
    // A host-provided setStateInformation later simply overwrites this.
    if (auto f = autosaveFile(); f.existsAsFile())
        if (auto xml = juce::XmlDocument::parse (f))
            applyStateXml (*xml);

    apvts.state.addListener (this);
    stateDirty.store (false);
    startTimer (500);   // autosave poll: writes ~1.2 s after the last change
}

VocalGzzioProcessor::~VocalGzzioProcessor()
{
    stopTimer();
    cancelPendingUpdate();          // v2.1.0: 実行待ちのMIDI切替を破棄
    if (stateDirty.load())
        flushAutosaveNow();
    apvts.state.removeListener (this);
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout
VocalGzzioProcessor::createParameterLayout()
{
    using P = juce::AudioParameterFloat;
    using R = juce::NormalisableRange<float>;

    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    auto freqRange = [] (float lo, float hi) {
        R r (lo, hi, 1.0f);
        r.setSkewForCentre (std::sqrt (lo * hi));
        return r;
    };

    using A = juce::AudioParameterFloatAttributes;
    auto unit = [] (const char* u) { return A().withLabel (u); };
    auto pid  = [] (const char* id) { return juce::ParameterID { id, 1 }; };

    // Cleanup
    layout.add (std::make_unique<P>(pid ("gate"),     "Gate",       R (-80.0f, -20.0f, 0.5f), -80.0f, unit ("dB")));
    layout.add (std::make_unique<P>(pid ("lowcut"),   "Low Cut",    freqRange (60.0f, 220.0f), 100.0f, unit ("Hz")));
    layout.add (std::make_unique<P>(pid ("mud"),      "Mud",        R (-12.0f, 0.0f, 0.1f),    -3.0f, unit ("dB")));
    layout.add (std::make_unique<P>(pid ("harsh"),    "Harsh",      R (-12.0f, 0.0f, 0.1f),    -1.5f, unit ("dB")));
    layout.add (std::make_unique<P>(pid ("denoise"),  "De-Noise",   R (0.0f, 100.0f, 1.0f),     0.0f, unit ("%")));
    // Dynamics
    layout.add (std::make_unique<P>(pid ("comp1"),    "Peak Comp",  R (0.0f, 100.0f, 1.0f),    35.0f, unit ("%")));
    layout.add (std::make_unique<P>(pid ("comp2"),    "Level Comp", R (0.0f, 100.0f, 1.0f),    30.0f, unit ("%")));
    layout.add (std::make_unique<P>(pid ("attack"),   "Attack",     R (1.0f, 60.0f, 1.0f),      8.0f, unit ("ms")));
    layout.add (std::make_unique<P>(pid ("release"),  "Release",    R (30.0f, 500.0f, 1.0f),  120.0f, unit ("ms")));
    layout.add (std::make_unique<P>(pid ("deess"),    "De-Esser",   R (0.0f, 100.0f, 1.0f),    35.0f, unit ("%")));
    // v2.3.0 口まわりのノイズ対策(初期値0% = 従来と完全に同じ音)
    layout.add (std::make_unique<P>(pid ("pop_amt"),  "De-Plosive", R (0.0f, 100.0f, 1.0f),     0.0f, unit ("%")));
    layout.add (std::make_unique<P>(pid ("lip_amt"),  "De-Click",   R (0.0f, 100.0f, 1.0f),     0.0f, unit ("%")));
    layout.add (std::make_unique<P>(pid ("res_amt"),  "Smooth (De-Resonance)", R (0.0f, 100.0f, 1.0f), 0.0f, unit ("%"))); // v2.4.0 なめらか
    layout.add (std::make_unique<P>(pid ("in_gain"),  "Mic Volume", R (-24.0f, 24.0f, 0.1f),    0.0f, unit ("dB"))); // v2.4.0 マイク音量(入力トリム)
    layout.add (std::make_unique<P>(pid ("ride_amt"), "Volume Keep (Rider)", R (0.0f, 100.0f, 1.0f), 0.0f, unit ("%"))); // v2.4.0 音量キープ
    // v2.6.0 ジー音(電源ハム)の自動除去 / ことば(子音エンハンサー)。初期値0% = 従来と同じ音
    layout.add (std::make_unique<P>(pid ("hum_amt"),  "Hum Removal",  R (0.0f, 100.0f, 1.0f), 0.0f, unit ("%")));
    layout.add (std::make_unique<P>(pid ("cons_amt"), "Consonant",    R (0.0f, 100.0f, 1.0f), 0.0f, unit ("%")));
    // v2.7.0 こぶし(しゃくり・こぶし保護)。初期値0% = 従来と完全に同じ挙動
    layout.add (std::make_unique<P>(pid ("orn_amt"),  "Ornament Guard", R (0.0f, 100.0f, 1.0f), 0.0f, unit ("%")));
    // Tone
    layout.add (std::make_unique<P>(pid ("presence"), "Presence",   R (-6.0f, 9.0f, 0.1f),      1.5f, unit ("dB")));
    // v1.9.5: 艶 — 歌手のフォルマント帯(3kHz)を母音のときだけ持ち上げる
    layout.add (std::make_unique<P>(pid ("ring"),     "Ring",       R (0.0f, 100.0f, 1.0f),     0.0f, unit ("%")));
    // v1.9.7: 低遅延モード（モニター用）。FFT窓 1024->512 で遅延が半分
    // v2.9.0 ★「低遅延」(768→384サンプル)を廃止して「セッション」に置き換えた。
    // 理由: オンラインセッションで快適とされる往復30msは**ネットとPCを通した合計の実測値**で、
    // 自分のパソコンの持ち分は半分も残らない。オーディオIFのAD/DAとバッファだけで
    // 7〜8ms使うので、プラグインに許される追加遅延は実質ゼロ。半分の8.7msでも足りない。
    // → 「減らす」ではなく「足さない」を保証する。ONの間は遅延をふやす3機能
    //   (ピッチ補正/ボイス変換/ハモリ)を素通しにして、申告も実測も0サンプルにする。
    layout.add (std::make_unique<juce::AudioParameterBool>(pid ("session"), "Session Mode", false));
    layout.add (std::make_unique<P>(pid ("air"),      "Air",        R (0.0f, 9.0f, 0.1f),       2.0f, unit ("dB")));
    layout.add (std::make_unique<P>(pid ("drive"),    "Warmth",     R (0.0f, 100.0f, 1.0f),    15.0f, unit ("%")));
    layout.add (std::make_unique<P>(pid ("sustain"),  "Sustain",    R (0.0f, 100.0f, 1.0f),     0.0f, unit ("%")));
    // Output & space
    layout.add (std::make_unique<P>(pid ("makeup"),   "Makeup",     R (0.0f, 18.0f, 0.1f),      3.0f, unit ("dB")));
    layout.add (std::make_unique<P>(pid ("mix"),      "Mix",        R (0.0f, 100.0f, 1.0f),   100.0f, unit ("%")));
    layout.add (std::make_unique<P>(pid ("width"),    "Width",      R (0.0f, 100.0f, 1.0f),     0.0f, unit ("%")));
    layout.add (std::make_unique<P>(pid ("doubler"),  "Doubler",    R (0.0f, 100.0f, 1.0f),     0.0f, unit ("%")));
    layout.add (std::make_unique<P>(pid ("delay"),    "Delay",      R (0.0f, 100.0f, 1.0f),     0.0f, unit ("%")));
    layout.add (std::make_unique<P>(pid ("revsize"),  "Rev Size",   R (0.0f, 100.0f, 1.0f),    30.0f, unit ("%")));
    layout.add (std::make_unique<P>(pid ("revmix"),   "Reverb",     R (0.0f, 100.0f, 1.0f),    10.0f, unit ("%")));
    layout.add (std::make_unique<juce::AudioParameterBool>(pid ("revon"), "Reverb On", true));

    // ---- v1.8.0 voice changer + 5-voice unison ----
    layout.add (std::make_unique<P>(pid ("vc_pitch"), "Voice Pitch",   R (-12.0f, 12.0f, 0.1f), 0.0f,  unit ("st")));
    layout.add (std::make_unique<P>(pid ("vc_form"),  "Voice Formant", R (-12.0f, 12.0f, 0.1f), 0.0f,  unit ("st")));
    layout.add (std::make_unique<P>(pid ("vc_mix"),   "Voice Mix",     R (0.0f, 100.0f, 1.0f),  100.0f, unit ("%")));
    layout.add (std::make_unique<juce::AudioParameterBool>(pid ("vc_on"), "Voice FX On", false));
    layout.add (std::make_unique<P>(pid ("jn_mix"),   "Unison Mix",    R (0.0f, 100.0f, 1.0f),  55.0f, unit ("%")));
    layout.add (std::make_unique<juce::AudioParameterBool>(pid ("jn_on"), "Unison x5 On", false));
    // v2.0.0: 9項目め「反行(Contrary)」を末尾に追加(既存プロジェクトの保存値は不変)。
    // 7番の旧名 "Auto (contrary)" は実装上「上/下の持ち替え」なので名前を実態に合わせた。
    layout.add (std::make_unique<juce::AudioParameterChoice>(pid ("jn_harm"), "Unison Harmony",
                    juce::StringArray { "Unison", "3rd up", "3rd down", "6th up",
                                        "6th down", "5th up", "3rd + 5th", "Auto flip",
                                        "Contrary" }, 0));
    // v1.9.9: ハモだけ出力（原音を消してハモリ声部だけを出す。別トラック録りや
    //         「自分の声を聴きながらハモリを重ねる」使い方のため）
    layout.add (std::make_unique<juce::AudioParameterBool>(pid ("jn_solo"), "Harmony Only", false));

    // ---- v2.0.0 エモート: ポップス/バラードを「感動的に聴かせる」ための3機能 ----
    // 息   = 小声のときだけ息の帯域(4.5kHz+)を持ち上げるアップワード・エキスパンダ
    // エモ = ロングトーンでリバーブと広がりがふわっと開く(サビ終わりの「泣き」)
    // サビリフト = サビを自動検出して空間系をまとめて持ち上げるオートアレンジ
    layout.add (std::make_unique<P>(pid ("br_amt"),   "Breath",      R (0.0f, 100.0f, 1.0f), 0.0f, unit ("%")));
    layout.add (std::make_unique<P>(pid ("emo_amt"),  "Emo Bloom",   R (0.0f, 100.0f, 1.0f), 0.0f, unit ("%")));
    layout.add (std::make_unique<P>(pid ("lift_amt"), "Chorus Lift", R (0.0f, 100.0f, 1.0f), 0.0f, unit ("%")));

    // ---- v1.9.0 auto-tune (pitch correction) ----
    // Choice labels are neutral/English here; the editor re-labels the combos per
    // language (same approach as jn_harm). Scale order matches gz::scale::* enum.
    layout.add (std::make_unique<juce::AudioParameterBool>(pid ("at_on"), "Pitch Correct On", false));
    layout.add (std::make_unique<juce::AudioParameterChoice>(pid ("at_key"), "Pitch Correct Key",
                    juce::StringArray { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" }, 0));
    layout.add (std::make_unique<juce::AudioParameterChoice>(pid ("at_scale"), "Pitch Correct Scale",
                    juce::StringArray { "Chromatic","Major","Minor","Harm Minor",
                                        "Penta Maj","Penta Min","Blues","Dorian","Mixolydian" }, 1));
    layout.add (std::make_unique<P>(pid ("at_amount"), "Pitch Correct Amount", R (0.0f, 100.0f, 1.0f), 80.0f, unit ("%")));
    layout.add (std::make_unique<P>(pid ("at_speed"),  "Retune Speed",     R (0.0f, 100.0f, 1.0f), 30.0f, unit ("%")));

    // Smart Dynamic EQ (zero-latency IIR; auto resonance suppression + manual bands)
    layout.add (std::make_unique<juce::AudioParameterBool>(pid ("seq_on"), "Smart EQ On", false));
    layout.add (std::make_unique<juce::AudioParameterChoice>(pid ("seq_mode"), "Smart EQ Mode",
                    juce::StringArray { juce::String::fromUTF8 ("\xe8\x87\xaa\xe5\x8b\x95"),      // 自動
                                        juce::String::fromUTF8 ("\xe6\x89\x8b\xe5\x8b\x95") }, 0)); // 手動
    layout.add (std::make_unique<P>(pid ("seq_amount"), "SEQ Amount", R (0.0f, 100.0f, 1.0f), 45.0f, unit ("%")));
    layout.add (std::make_unique<P>(pid ("seq_focus"),  "SEQ Focus",  R (0.0f, 100.0f, 1.0f), 50.0f, unit ("%")));
    layout.add (std::make_unique<P>(pid ("seq_f1"), "SEQ Freq 1", R (120.0f, 8000.0f, 1.0f, 0.35f),  350.0f, unit ("Hz")));
    layout.add (std::make_unique<P>(pid ("seq_d1"), "SEQ Depth 1", R (0.0f, 15.0f, 0.1f),              6.0f, unit ("dB")));
    layout.add (std::make_unique<P>(pid ("seq_f2"), "SEQ Freq 2", R (120.0f, 8000.0f, 1.0f, 0.35f), 1500.0f, unit ("Hz")));
    layout.add (std::make_unique<P>(pid ("seq_d2"), "SEQ Depth 2", R (0.0f, 15.0f, 0.1f),              6.0f, unit ("dB")));
    layout.add (std::make_unique<P>(pid ("seq_f3"), "SEQ Freq 3", R (120.0f, 8000.0f, 1.0f, 0.35f), 4500.0f, unit ("Hz")));
    layout.add (std::make_unique<P>(pid ("seq_d3"), "SEQ Depth 3", R (0.0f, 15.0f, 0.1f),              6.0f, unit ("dB")));

    // Tuner
    layout.add (std::make_unique<P>(pid ("refpitch"), "Ref Pitch",  R (415.0f, 445.0f, 1.0f), 440.0f, unit ("Hz")));

    // ---- v1.4.0 ----
    using B = juce::AudioParameterBool;
    using C = juce::AudioParameterChoice;

    // red-lamp module bypass (default ON keeps old projects sounding identical)
    layout.add (std::make_unique<B>(pid ("gate_on"), "Gate On",     true));
    layout.add (std::make_unique<B>(pid ("dn_on"),   "De-Noise On", true));
    layout.add (std::make_unique<B>(pid ("ds_on"),   "De-Esser On", true));
    layout.add (std::make_unique<B>(pid ("dbl_on"),  "Doubler On",  true));
    layout.add (std::make_unique<B>(pid ("dly_on"),  "Delay On",    true));
    layout.add (std::make_unique<B>(pid ("mega_on"), "Megaphone On", true));
    layout.add (std::make_unique<B>(pid ("cho_on"),  "Chorus On",   true));
    layout.add (std::make_unique<B>(pid ("robo_on"), "Robot On",    true));

    // reverb type (0 = legacy sound so old projects are unchanged; new types are
    // APPENDED so stored indices from v1.4/v1.5 keep their meaning)
    layout.add (std::make_unique<C>(pid ("rev_type"), "Reverb Type",
        juce::StringArray { juce::String::fromUTF8 ("\xe3\x83\x8e\xe3\x83\xbc\xe3\x83\x9e\xe3\x83\xab"),
                            juce::String::fromUTF8 ("\xe3\x83\xab\xe3\x83\xbc\xe3\x83\xa0"),
                            juce::String::fromUTF8 ("\xe3\x83\x97\xe3\x83\xac\xe3\x83\xbc\xe3\x83\x88"),
                            juce::String::fromUTF8 ("\xe3\x83\x9b\xe3\x83\xbc\xe3\x83\xab"),
                            juce::String::fromUTF8 ("\xe3\x83\x81\xe3\x83\xa3\xe3\x83\xbc\xe3\x83\x81"),
                            juce::String::fromUTF8 ("\xe3\x82\xb9\xe3\x83\x97\xe3\x83\xaa\xe3\x83\xb3\xe3\x82\xb0"),
                            juce::String::fromUTF8 ("\xe3\x82\xb7\xe3\x83\x9e\xe3\x83\xbc") }, 0));

    // delay: tempo sync + time + feedback + feedback highcut + manual BPM (host BPM wins)
    layout.add (std::make_unique<C>(pid ("dly_sync"), "Delay Sync",
        juce::StringArray { juce::String::fromUTF8 ("\x6d\x73\xe6\x8c\x87\xe5\xae\x9a"),
                            "1/4",
                            "1/8",
                            juce::String::fromUTF8 ("\xe4\xbb\x98\xe7\x82\xb9\x31\x2f\x38"),
                            juce::String::fromUTF8 ("\x31\x2f\x38\xe4\xb8\x89\xe9\x80\xa3"),
                            "1/16",
                            juce::String::fromUTF8 ("\xe4\xbb\x98\xe7\x82\xb9\x31\x2f\x34") }, 0));
    layout.add (std::make_unique<P>(pid ("dly_ms"),  "Delay Time",  R (60.0f, 900.0f, 1.0f),  340.0f, unit ("ms")));
    layout.add (std::make_unique<P>(pid ("dly_fb"),  "Delay FB",    R (0.0f, 90.0f, 1.0f),     28.0f, unit ("%")));
    layout.add (std::make_unique<P>(pid ("dly_hc"),  "Delay HiCut", freqRange (1000.0f, 12000.0f), 5000.0f, unit ("Hz")));
    layout.add (std::make_unique<P>(pid ("bpm"),     "BPM",         R (50.0f, 300.0f, 1.0f),  120.0f, A()));

    // auto-duck: lower delay/reverb wet while the vocal is present
    layout.add (std::make_unique<P>(pid ("duck"),    "Ducking",     R (0.0f, 100.0f, 1.0f),     0.0f, unit ("%")));

    // megaphone / distortion
    layout.add (std::make_unique<C>(pid ("mega_type"), "Mega Type",
        juce::StringArray { juce::String::fromUTF8 ("\xe6\x8b\xa1\xe5\xa3\xb0\xe5\x99\xa8"),
                            juce::String::fromUTF8 ("\xe3\x83\xa9\xe3\x82\xb8\xe3\x82\xaa"),
                            juce::String::fromUTF8 ("\xe3\x83\xad\xe3\x83\xbc\xe3\x83\x95\xe3\x82\xa1\xe3\x82\xa4") }, 0));
    layout.add (std::make_unique<P>(pid ("mega_amt"), "Mega Amount", R (0.0f, 100.0f, 1.0f),    0.0f, unit ("%")));

    // chorus
    layout.add (std::make_unique<P>(pid ("cho_amt"),  "Chorus",      R (0.0f, 100.0f, 1.0f),    0.0f, unit ("%")));

    // robot voice (ring mod)
    layout.add (std::make_unique<P>(pid ("robo_freq"), "Robot Freq", freqRange (20.0f, 800.0f), 80.0f, unit ("Hz")));
    layout.add (std::make_unique<P>(pid ("robo_mix"),  "Robot Mix",  R (0.0f, 100.0f, 1.0f),    0.0f, unit ("%")));

    // F6-style manual bands: per-band Q (default keeps the old fixed 2.5 sound)
    layout.add (std::make_unique<P>(pid ("seq_q1"), "SEQ Q 1", R (0.5f, 8.0f, 0.1f), 2.5f, A()));
    layout.add (std::make_unique<P>(pid ("seq_q2"), "SEQ Q 2", R (0.5f, 8.0f, 0.1f), 2.5f, A()));
    layout.add (std::make_unique<P>(pid ("seq_q3"), "SEQ Q 3", R (0.5f, 8.0f, 0.1f), 2.5f, A()));

    return layout;
}

//==============================================================================
void VocalGzzioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;

    // ---- v1.8.0 voice changer + unison ----
    for (auto& s : vcSh) s.prepare (sampleRate);
    for (auto& s : unSh) s.prepare (sampleRate);
    pitchDet.prepare (sampleRate);            // v1.9.0 auto-tune F0 detector
    atCorrection = 0.0f;
    atWetMix = 0.0f;
    vcWasActive = jnWasActive = false;
    vcMono.assign ((size_t) juce::jmax (samplesPerBlock, 16), 0.0f);
    vcTmp .assign ((size_t) juce::jmax (samplesPerBlock, 16), 0.0f);
    vcDry[0].assign ((size_t) juce::jmax (samplesPerBlock, 16), 0.0f);   // v2.6.0 安全弁
    vcDry[1].assign ((size_t) juce::jmax (samplesPerBlock, 16), 0.0f);
    {   // ensemble timing offsets per "member" (ms), clamped to the buffer
        const float ms[4] = { 11.0f, 17.0f, 24.0f, 31.0f };
        for (int v = 0; v < 4; ++v)
        {
            unDelaySmp[v] = juce::jlimit (1, 4095, (int) std::lround (ms[v] * 0.001 * sampleRate));
            unDelayW[v] = 0;
            std::fill (std::begin (unDelay[v]), std::end (unDelay[v]), 0.0f);
        }
    }
    // v2.6.0: 遅延の申告はこの関数の末尾で1回だけ行う(実行中は通知しない)。
    humKill.prepare (sampleRate);      // v2.6.0 ジー音
    consEnh.prepare (sampleRate);      // v2.6.0 ことば
    ornGuard.prepare (sampleRate);     // v2.7.0 こぶし

    juce::dsp::ProcessSpec spec;
    spec.sampleRate       = sampleRate;
    spec.maximumBlockSize = (juce::uint32) samplesPerBlock;
    spec.numChannels      = 2;

    hpf.prepare (spec); mud.prepare (spec); harsh.prepare (spec);
    presence.prepare (spec); air.prepare (spec);
    ringL.prepare (spec); ringR.prepare (spec);          // v1.9.5 艶
    ringDet.prepare (spec); sibDet.prepare (spec);
    ringDet.coefficients = Coefficients::makeBandPass (sampleRate, 3000.0, 1.2f);
    sibDet .coefficients = Coefficients::makeBandPass (sampleRate, 7500.0, 0.9f);
    ringL.coefficients = ringR.coefficients = Coefficients::makePeakFilter (sampleRate, 3000.0, 1.4f, 1.0f);
    ringEnv = sibEnv = 0.0f; ringGainDb = 0.0f; ringApplied = -99.0f;
    deessDetectHP.prepare (spec);

    juce::dsp::ProcessSpec mono = spec; mono.numChannels = 1;
    dsShelfL.prepare (mono); dsShelfR.prepare (mono);
    // v2.8.0: ここで2次の係数を入れておく。入れておかないと、最初に音が来た
    // ときに次数が 1→2 に変わって Filter::reset() が1回だけメモリを確保する
    // (＝音声スレッドでの確保)。準備段階で済ませておけばゼロになる。
    dsShelfL.coefficients = Coefficients::makeHighShelf (sampleRate, 6500.0, 0.8f, 1.0f);
    dsShelfR.coefficients = Coefficients::makeHighShelf (sampleRate, 6500.0, 0.8f, 1.0f);
    for (int b = 0; b < seqBands; ++b) { seqPeakL[b].prepare (mono); seqPeakR[b].prepare (mono); seqDet[b].prepare (mono); }

    comp1.prepare (spec);
    comp2.prepare (spec);
    comp2.setAttack (25.0f);
    comp2.setRelease (250.0f);

    makeup.prepare (spec);
    makeup.setRampDurationSeconds (0.02);
    reverb.prepare (spec);

    dryBuffer.setSize (2, samplesPerBlock, false, false, true);
    scratch.setSize   (2, samplesPerBlock, false, false, true);

    // gate constants
    gateEnvAtk    = 1.0f - std::exp (-1.0f / (0.004f * (float) sampleRate));
    gateEnvRel    = 1.0f - std::exp (-1.0f / (0.070f * (float) sampleRate));
    gateOpenCoef  = 1.0f - std::exp (-1.0f / (0.006f * (float) sampleRate));
    gateCloseCoef = 1.0f - std::exp (-1.0f / (0.140f * (float) sampleRate));
    gateEnv = 0; gateGain = 1;

    // ---- de-noise: LR4 crossovers @ 250 / 1200 / 5000 Hz ----
    for (auto* f : { &lrLP1, &lrHP1, &lrLP2, &lrHP2, &lrLP3, &lrHP3 })
    {
        f->prepare (spec);
        f->setType (juce::dsp::LinkwitzRileyFilterType::lowpass);
    }
    lrHP1.setType (juce::dsp::LinkwitzRileyFilterType::highpass);
    lrHP2.setType (juce::dsp::LinkwitzRileyFilterType::highpass);
    lrHP3.setType (juce::dsp::LinkwitzRileyFilterType::highpass);
    lrLP1.setCutoffFrequency (250.0f);  lrHP1.setCutoffFrequency (250.0f);
    lrLP2.setCutoffFrequency (1200.0f); lrHP2.setCutoffFrequency (1200.0f);
    lrLP3.setCutoffFrequency (5000.0f); lrHP3.setCutoffFrequency (5000.0f);
    for (auto& b : bandBuf) b.setSize (2, samplesPerBlock, false, false, true);
    dnEnvAtk    = 1.0f - std::exp (-1.0f / (0.002f * (float) sampleRate));
    dnEnvRel    = 1.0f - std::exp (-1.0f / (0.060f * (float) sampleRate));
    dnOpenCoef  = 1.0f - std::exp (-1.0f / (0.003f * (float) sampleRate));
    dnCloseCoef = 1.0f - std::exp (-1.0f / (0.045f * (float) sampleRate));
    dnFloorRise = std::pow (10.0f, 3.0f / 20.0f / (float) sampleRate);   // +3 dB/s adaptive drift
    for (int b = 0; b < 4; ++b) { dnEnv[b] = 0; dnGain[b] = 1; }
    // v1.5.0: a learned noise profile is part of the user's settings now, so it
    // survives prepareToPlay (levels are sample-rate independent). Only the
    // adaptive tracker restarts when nothing has been learned yet.
    if (! dnLearned)
        for (int b = 0; b < 4; ++b) dnFloor[b] = 1e-5f;
    learnCountdown.store (0);

    // ---- sustain constants ----
    susEnvAtk = 1.0f - std::exp (-1.0f / (0.005f * (float) sampleRate));
    susEnvRel = 1.0f - std::exp (-1.0f / (0.180f * (float) sampleRate));
    susEnv = 0; susLift = 0;

    // ---- v2.0.0 エモート ----
    {
        juce::dsp::ProcessSpec m1 = spec; m1.numChannels = 1;
        brShelfL.prepare (m1); brShelfR.prepare (m1);
        brShelfL.coefficients = brShelfR.coefficients =
            Coefficients::makeHighShelf (sampleRate, 4500.0, 0.71f, 1.0f);
        brEnvAtk = 1.0f - std::exp (-1.0f / (0.010f * (float) sampleRate));   // 10 ms
        brEnvRel = 1.0f - std::exp (-1.0f / (0.120f * (float) sampleRate));   // 120 ms
        brEnv = 0.0f; brGainDb = 0.0f; brApplied = -99.0f;
        emoHoldSec = 0.0f; emoBloom = 0.0f;
        liftFastDb = liftSlowDb = -60.0f; liftVal = 0.0f; liftUI.store (0.0f);
        jnLastHarm = -1; jnHeldSemi[0] = jnHeldSemi[1] = 0.0f; jnContra.reset();
    }

    // ---- v2.3.0 ポップ / リップ 除去 ----
    {
        juce::dsp::ProcessSpec m1 = spec; m1.numChannels = 1;
        auto tc = [sampleRate] (float ms) { return 1.0f - std::exp (-1.0f / (ms * 0.001f * (float) sampleRate)); };

        for (int ch = 0; ch < 2; ++ch)
            for (int st = 0; st < 2; ++st)
            {
                popHp[ch][st].prepare (m1);
                lipLp[ch][st].prepare (m1);
                popHp[ch][st].coefficients = Coefficients::makeHighPass (sampleRate, 190.0, 0.707f);
                lipLp[ch][st].coefficients = Coefficients::makeLowPass  (sampleRate, 2200.0, 0.707f);
                popHp[ch][st].reset(); lipLp[ch][st].reset();
            }
        popDet.prepare (m1); lipDet.prepare (m1);
        popMidDet.prepare (m1); lipBodyDet.prepare (m1);
        resTamer.prepare (sampleRate);   // v2.4.0 なめらか
        popDet     .coefficients = Coefficients::makeLowPass  (sampleRate, 120.0, 0.707f); // 破裂音の帯域
        popMidDet  .coefficients = Coefficients::makeHighPass (sampleRate, 170.0, 0.707f); // 歌なら必ず出る倍音側
        lipDet     .coefficients = Coefficients::makeBandPass (sampleRate, 3500.0, 0.7f);  // 粘着音の帯域
        lipBodyDet .coefficients = Coefficients::makeLowPass  (sampleRate, 800.0, 0.707f); // 歌っているかの判定用
        popDet.reset(); lipDet.reset(); popMidDet.reset(); lipBodyDet.reset();

        // 実測で決めた定数(tools/dsp_popclick.cpp の検証結果):
        //   ポップ単体 -7.4dB / 口の粘着音 -7.0dB / 伸ばした歌声 0.0dB。
        //   低い男声の歌い出しにわずかに反応するが、戻りを15msにしてあるので
        //   影響は -2.8dB × 数十ms に収まる(ゼロ遅延のため先読みができない分の割り切り)。
        // v2.4.0 マイク音量(15msでなめす=ジッパーノイズ防止) と 音量キープ
        inGainA   = tc (15.0f);  inGainNow = 1.0f;
        rideRmsA  = tc (300.0f); // ラウドネス計測: フレーズ単位のゆっくりした窓
        rideSlewA = tc (700.0f); // ゲインの動き: 人がフェーダーを触る速さ
        rideEnv2  = 0.0f; rideGDb = 0.0f;

        popLfFastA = tc (0.35f); popLfFastR = tc (45.0f);
        popLfSlowA = tc (18.0f); popLfSlowR = tc (250.0f);   // 中速: 歌の立ち上がりは追従できる速さ
        popMidA    = tc (1.0f);  popMidR    = tc (120.0f);
        popGA      = tc (0.30f); popGR      = tc (15.0f);    // 戻りは速く(誤反応を長引かせない)
        lipFastA   = tc (0.15f); lipFastR   = tc (6.0f);
        lipSlowA   = tc (10.0f); lipSlowR   = tc (150.0f);
        lipBodyA   = tc (1.0f);  lipBodyR   = tc (140.0f);
        lipGA      = tc (0.4f);  lipGR      = tc (22.0f);
        popLfFast = popLfSlow = popMid = popG = 0.0f;
        lipFast = lipSlow = lipBody = lipG = 0.0f;
        meterPop.store (0.0f); meterLip.store (0.0f);
    }

    // de-esser envelope constants (fast attack, medium release)
    dsEnvAtk = 1.0f - std::exp (-1.0f / (0.0015f * (float) sampleRate));
    dsEnvRel = 1.0f - std::exp (-1.0f / (0.050f  * (float) sampleRate));
    dsEnv = 0; dsGain = 1; dsCurrentReduction = 0;

    // Smart Dynamic EQ: reset filters + envelope constants
    for (int b = 0; b < seqBands; ++b)
    {
        seqPeakL[b].reset(); seqPeakR[b].reset(); seqDet[b].reset();
        seqEnv[b] = 0.0f; seqCut[b] = 0.0f; seqTarget[b] = 0.0f;
        // detector = gentle band-pass around the band centre
        seqDet[b].coefficients = Coefficients::makeBandPass (sampleRate, seqFreqHz[b], 1.2f);
        seqPeakL[b].coefficients = Coefficients::makePeakFilter (sampleRate, seqFreqHz[b], 2.0f, 1.0f);
        seqPeakR[b].coefficients = Coefficients::makePeakFilter (sampleRate, seqFreqHz[b], 2.0f, 1.0f);
    }
    seqEnvAtk = 1.0f - std::exp (-1.0f / (0.0030f * (float) sampleRate));   // ~3 ms
    seqEnvRel = 1.0f - std::exp (-1.0f / (0.0800f * (float) sampleRate));   // ~80 ms
    meterSEQ.store (0.0f);

    // doubler buffer (max ~40 ms)
    dblBuf.assign ((size_t) ((int) (0.045 * sampleRate) + 8), 0.0f);
    dblWrite = 0; dblLfoPhase = 0;

    // delay buffer (up to ~2 s for slow-tempo dotted notes)
    const int dlyLen = (int) (2.05 * sampleRate) + 8;
    dlyBufL.assign ((size_t) dlyLen, 0.0f);
    dlyBufR.assign ((size_t) dlyLen, 0.0f);
    dlyWrite = 0;
    dlyLpL = 0.0f; dlyLpR = 0.0f;
    dlyTimeSm = 0.34f;

    // ---- v1.4.0: character FX / chorus / reverb wet path / duck ----
    megaHP.prepare (spec); megaLP.prepare (spec); megaPeak.prepare (spec);
    roboPhase = 0.0f;

    chorus.prepare (spec);
    chorus.setCentreDelay (7.0f);
    chorus.setFeedback (0.0f);
    chorus.setMix (0.0f);

    revWet.setSize (2, samplesPerBlock, false, false, true);
    preBufL.assign ((size_t) ((int) (0.09 * sampleRate) + 8), 0.0f);
    preBufR.assign (preBufL.size(), 0.0f);
    preWrite = 0;
    revHPF.prepare (spec); revLPF.prepare (spec);

    // v1.6.0 spring / shimmer state
    springBufL.assign ((size_t) ((int) (0.033 * sampleRate) + 8), 0.0f);
    springBufR.assign (springBufL.size(), 0.0f);
    springW = 0; springLpL = springLpR = 0.0f;
    shimBufL.assign ((size_t) ((int) (0.140 * sampleRate) + 8), 0.0f);   // > 4x grain
    shimBufR.assign (shimBufL.size(), 0.0f);
    shimW = 0; shimPhase = 0.0f;
    // v2.8.0: ホストが prepareToPlay より大きいブロックを渡してくることがある
    // (オフラインの書き出しなど)。ここが唯一その保険の無いブロック長バッファで、
    // 配列の外に書き込む＝クラッシュの原因になり得たので、余裕を持たせておく。
    // 使う側にも jmin を入れてある(二重の保険)。
    shimFb.setSize (2, juce::jmax (samplesPerBlock * 2, 8192), false, false, true);
    shimFb.clear();
    shimLpL = shimLpR = shimHpL = shimHpR = 0.0f;

    // v2.8.0: Mixで混ぜ戻す原音を、加工側と同じだけ遅らせるためのリング。
    // 最大でシフターの申告遅延ぶん＋1ブロック＋余白があれば足りる。
    {
        // シフターの最大遅延は N-hop = 1024-256 = 768。窓の設定はこの下で行うので
        // latencySamples() をここで呼ぶと古い値になる。余裕をみて 1024 で確保する。
        const int maxLat = 1024;
        dryRing.setSize (2, maxLat + juce::jmax (samplesPerBlock * 2, 8192) + 8,
                         false, false, true);
        dryRing.clear();
        dryAligned.setSize (2, juce::jmax (samplesPerBlock * 2, 8192), false, false, true);
        dryAligned.clear();
        dryRingW = 0;
    }

    duckGainBuf.assign ((size_t) samplesPerBlock, 1.0f);
    duckEnv = 0.0f; duckGain = 1.0f;
    duckEnvAtk = 1.0f - std::exp (-1.0f / (0.002f * (float) sampleRate));
    duckEnvRel = 1.0f - std::exp (-1.0f / (0.080f * (float) sampleRate));
    duckAtk    = 1.0f - std::exp (-1.0f / (0.015f * (float) sampleRate));   // duck engages ~15 ms
    duckRel    = 1.0f - std::exp (-1.0f / (0.200f * (float) sampleRate));   // blooms back ~200 ms
    rmsAccum = 0.0f;

    // AUTO SETUP analysis band-pass bank (side-chain only, never touches audio)
    {
        const float centres[asBands] = { 55.f, 150.f, 350.f, 1000.f, 3200.f, 6500.f, 12000.f };
        const float qs[asBands]      = { 0.7f, 0.8f,  0.9f,  0.7f,   0.9f,   1.0f,   0.7f    };
        for (int b = 0; b < asBands; ++b)
        {
            asBP[b].prepare (spec);
            *asBP[b].state = *Coefficients::makeBandPass (sampleRate, centres[b], qs[b]);
            asBandSum[b].store (0.0);
        }
        asPrepared = true;
    }
    asScratch.setSize (2, samplesPerBlock, false, false, true);
    for (auto& c : chromaSum) c.store (0.0);
    // capture buffer holds the whole scan (max 12 s at the current rate) so the
    // audio thread only copies into it; analysis happens off-thread afterwards.
    keyCaptureBuf.assign ((size_t) juce::jmax (1, (int) (12.0 * sampleRate)), 0.0f);
    keyCaptureWrite.store (0);
    keyCaptureReady.store (false);
    keyAnalyzed = false;

    std::fill (std::begin (tunerBuf), std::end (tunerBuf), 0.0f);
    tunerPos.store (0);
    std::fill (std::begin (analyzerBuf), std::end (analyzerBuf), 0.0f);
    analyzerPos.store (0);
    for (int b = 0; b < seqBands; ++b)
    {
        seqCutUI [b].store (0.0f);
        seqFreqUI[b].store (seqFreqHz[b]);
    }

    // v2.6.0: 遅延の申告はここだけで行う。ホストがグラフを組む瞬間なので、
    // ここで伝えるぶんには何も壊れない(実行中に伝えると Cubase が固まる)。
    // プロジェクトを開いた時点でボイス変換やハモリがONなら、その遅延を申告する。
    {
        // v2.9.0: 窓はつねに 1024 点(遅延768サンプル)。低遅延モードは廃止した。
        {
            for (auto& sh : vcSh) sh.setWindow (10);
            for (auto& sh : unSh) sh.setWindow (10);
        }
        // v2.9.0 セッションモード: ONの間は遅延をふやす3機能を素通しにする。
        const bool sessionOn = apvts.getRawParameterValue ("session")->load() > 0.5f;
        const bool vcOn = apvts.getRawParameterValue ("vc_on")->load() > 0.5f && ! sessionOn;
        const bool atOn = apvts.getRawParameterValue ("at_on")->load() > 0.5f && ! sessionOn;
        // v2.9.0 ★ハモリ(jn_on)を申告から外した。
        //   ハモリは主メロの**横に**声を足す並列処理で、主メロ自体は遅れない。
        //   実測: ハモリだけONにすると申告768サンプルなのにピークは0サンプル
        //   = 17.4ms ぶん嘘の申告をしていた。DAWはその嘘を信じてトラックを
        //   17.4ms 前へ引っ張るので、**歌だけ走る**。さらに下の Mix そろえも
        //   voiceLatency を見ているため、原音だけ17.4ms遅れて混ざり
        //   (v2.8.0で直したはずの)コムフィルタが再発していた。
        //   主メロが実際に遅れるのは ボイス変換 / ピッチ補正 のときだけ。
       #if VOCALGZZIO_LITE
        const bool mainShifted = false;
       #else
        const bool mainShifted = vcOn || atOn;
       #endif
        voiceLatency = mainShifted ? vcSh[0].latencySamples() : 0;
        setLatencySamples (voiceLatency);
        reportedLatency.store (voiceLatency);
    }
    updateParameters();

    // v2.2.0 配信出力: 本線のサンプリングレートが変わったら開き直す
    // (レート比が変わるため。止まっているときは何もしない)
    streamOut.setHostSampleRate (sampleRate);
    if (streamWanted && isStandalone() && streamDevWanted.isNotEmpty())
        streamOut.start (streamDevWanted, sampleRate);
}

void VocalGzzioProcessor::updateParameters()
{
    const auto sr = currentSampleRate;
    auto p = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };
    auto toGain = [] (float db) { return juce::Decibels::decibelsToGain (db); };

    // v2.8.0: ここは processBlock から毎ブロック呼ばれる。makeXxx (ヒープ確保)
    // ではなく ArrayCoefficients (確保なし) を使う。→ PluginProcessor.h の ACoefs 参照
    *hpf.state      = ACoefs::makeHighPass   (sr, p ("lowcut"), 0.707f);
    *mud.state      = ACoefs::makePeakFilter (sr, 300.0f, 1.0f,  toGain (p ("mud")));
    *harsh.state    = ACoefs::makePeakFilter (sr, 3200.0f, 1.2f, toGain (p ("harsh")));
    *presence.state = ACoefs::makePeakFilter (sr, 4200.0f, 0.9f, toGain (p ("presence")));
    *air.state      = ACoefs::makeHighShelf  (sr, 11000.0f, 0.707f, toGain (p ("air")));
    *deessDetectHP.state = ACoefs::makeHighPass (sr, 5200.0f, 0.9f);

    // comp1: fast peak catcher; amount maps threshold -8..-30 dB, ratio 4:1
    const float c1 = p ("comp1") * 0.01f;
    comp1.setThreshold (juce::jmap (c1, -8.0f, -30.0f));
    comp1.setRatio (4.0f);
    comp1.setAttack  (juce::jmax (1.0f, p ("attack") * 0.5f));
    comp1.setRelease (p ("release") * 0.6f);

    // comp2: slow leveller; amount maps threshold -10..-35 dB, ratio 2.5:1
    const float c2 = p ("comp2") * 0.01f;
    comp2.setThreshold (juce::jmap (c2, -10.0f, -35.0f));
    comp2.setRatio (2.5f);

    makeup.setGainDecibels (p ("makeup"));

    // ---- reverb types: legacy "normal" keeps the exact pre-1.4 sound ----
    // Vocal-practice values (2026 sources): Plate = the lead-vocal workhorse,
    // bright/dense; Hall = long, 30 ms predelay for ballads; Room = short and
    // close; Spring = band-limited boingy vintage; Shimmer = +1 oct sparkle in
    // the tail. Wet path always gets HPF/LPF so tails never turn muddy.
    {
        const int  type  = (int) apvts.getRawParameterValue ("rev_type")->load();
        const float s    = juce::jlimit (0.0f, 1.0f, p ("revsize") * 0.01f);

        struct RevDef { float roomLo, roomHi, damp, width, hpHz, lpHz; };
        static const RevDef defs[7] = {
            { 0.00f, 1.00f, 0.55f, 1.0f,    20.0f, 20000.0f },   // normal (legacy)
            { 0.22f, 0.52f, 0.65f, 0.7f,   250.0f,  7500.0f },   // room
            { 0.42f, 0.74f, 0.30f, 1.0f,   300.0f,  9500.0f },   // plate
            { 0.62f, 0.94f, 0.45f, 1.0f,   250.0f,  8000.0f },   // hall
            { 0.85f, 1.00f, 0.50f, 1.0f,   200.0f,  6000.0f },   // church
            { 0.26f, 0.55f, 0.38f, 0.6f,   220.0f,  4500.0f },   // spring (narrow band)
            { 0.72f, 1.00f, 0.22f, 1.0f,   300.0f,  9000.0f }    // shimmer (open top)
        };
        const RevDef& d = defs[juce::jlimit (0, 6, type)];

        juce::dsp::Reverb::Parameters rp;
        rp.roomSize = juce::jmap (s, d.roomLo, d.roomHi);
        rp.damping  = d.damp;
        rp.width    = d.width;
        const bool revOn = apvts.getRawParameterValue ("revon")->load() > 0.5f;
        rp.wetLevel = revOn ? juce::jlimit (0.0f, 1.0f, p ("revmix") * 0.01f) : 0.0f;
        rp.dryLevel = 0.0f;   // dry stays in the main buffer; reverb runs on a wet-only copy
        reverb.setParameters (rp);

        *revHPF.state = ACoefs::makeHighPass (sr, d.hpHz, 0.707f);
        *revLPF.state = ACoefs::makeLowPass  (sr, d.lpHz, 0.707f);
    }

    // ---- v1.4.0 megaphone: band-pass + horn resonance, drive applied per sample ----
    {
        const int   type = (int) apvts.getRawParameterValue ("mega_type")->load();
        const float amt  = juce::jlimit (0.0f, 1.0f, p ("mega_amt") * 0.01f);

        struct MegaDef { float hpHz, lpHz, pkHz, pkQ, pkDbMax; };
        static const MegaDef mdefs[3] = {
            { 500.0f, 4000.0f, 1500.0f, 2.0f, 6.0f },   // bullhorn
            { 300.0f, 3400.0f, 1200.0f, 1.5f, 4.0f },   // radio
            { 200.0f, 3000.0f, 1000.0f, 1.0f, 3.0f }    // lo-fi
        };
        const MegaDef& m = mdefs[juce::jlimit (0, 2, type)];
        *megaHP.state   = ACoefs::makeHighPass   (sr, m.hpHz, 0.707f);
        *megaLP.state   = ACoefs::makeLowPass    (sr, m.lpHz, 0.707f);
        *megaPeak.state = ACoefs::makePeakFilter (sr, m.pkHz, m.pkQ,
                              toGain (m.pkDbMax * amt));
    }

    // ---- v1.4.0 chorus ----
    {
        const bool  on  = apvts.getRawParameterValue ("cho_on")->load() > 0.5f;
        const float amt = juce::jlimit (0.0f, 1.0f, p ("cho_amt") * 0.01f);
        chorus.setRate (0.8f);
        chorus.setDepth (0.15f + amt * 0.25f);
        chorus.setMix (on ? amt * 0.5f : 0.0f);
    }
}

//==============================================================================
bool VocalGzzioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

// v2.6.0: ブロック全体が有限か(NaN/Infを含まないか)を1回の加算走査で調べる。
// |x| の合計は打ち消し合わないので、どこかに NaN/Inf があれば合計も非有限になる。
// (全サンプル有限なのに合計だけ溢れるのは 1e38 級の壊れた音だけ = 検出して正解)
static inline bool gzBlockFinite (const float* p, int n) noexcept
{
    float acc = 0.0f;
    for (int i = 0; i < n; ++i) acc += std::abs (p[i]);
    return std::isfinite (acc);
}

void VocalGzzioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int numCh = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();

    // ---- v2.6.0 入力の防火壁 ----
    // ホストや他のプラグインから NaN/Inf が一度でも流れ込むと、内部の再帰状態
    // (フィルタ・コンプの包絡・ピッチシフタの位相メモリ)が汚染されて自然には
    // 戻らず、「最初しか音が出ない」状態になる。入口で見つけて 0 に置き換える。
    for (int ch = 0; ch < numCh; ++ch)
    {
        float* p = buffer.getWritePointer (ch);
        if (! gzBlockFinite (p, numSamples))
            for (int i = 0; i < numSamples; ++i)
                if (! std::isfinite (p[i])) p[i] = 0.0f;
    }

    // ---- v2.6.0 ジー音(電源ハム)の自動除去 ----
    // 何よりも先に消す。理由は3つ。
    //  ・ローカット(既定100Hz)より前なので、ハムの基本波まで丸ごと見える
    //  ・ゲートやコンプがジーに反応して呼吸するのを防げる
    //  ・ボイス変換に入る前に消しておかないと、ピッチシフトでジーが
    //    ぐしゃぐしゃに散らばって、あとからでは絶対に取れなくなる
    {
        const float humAmt = apvts.getRawParameterValue ("hum_amt")->load() * 0.01f;
        humKill.setAmount (humAmt);
        if (humAmt > 0.001f)
        {
            humKill.process (buffer.getWritePointer (0),
                             numCh > 1 ? buffer.getWritePointer (1) : nullptr,
                             numSamples);
            meterHumHz.store (humKill.detectedHz());
            meterHumDb.store (humKill.humLevelDb());
        }
        else if (meterHumHz.load() != 0) { meterHumHz.store (0); meterHumDb.store (-120.0f); }
    }

    processMidiSwitches (midi);   // v2.1.0: フットスイッチ/パッドの学習と切替検出

    updateParameters();

    // host tempo (0 when the host provides none -> manual BPM parameter is used)
    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) hostBpm.store ((float) *b);
            else                        hostBpm.store (0.0f);
        }
        else hostBpm.store (0.0f);
    }
    else hostBpm.store (0.0f);

    // v1.5.0: adopt a denoise profile restored from saved state (message thread
    // wrote the shared atomics; the audio thread owns the working copies).
    if (dnProfilePending.exchange (false, std::memory_order_acq_rel))
    {
        for (int b = 0; b < 4; ++b) dnFloor[b] = dnFloorShared[b].load();
        dnLearned = dnLearnedShared.load();
    }

    // ---- v2.4.0 マイク音量(入力トリム) ----
    // v2.8.0 ★位置を直した: 説明文には「ぜんぶの処理のいちばん手前で掛ける」と
    // 書いてあったのに、実際はゲート・ノイズ除去・ボイス変換のあと、しかも
    // INメーターを取ったあとに掛けていた。そのため
    //   ・マイク音量を上げても **INメーターが動かない**（壊れて見える）
    //   ・ゲートのしきい値が入力レベルに追従しない（小さい声が切られたまま）
    //   ・Mixで混ぜ戻す原音にトリムが乗らない
    // という3つが起きていた。ここ（メーターとゲートより前・原音コピーより前）が
    // 本来の位置。ジー音除去だけは意図的にこれより前に置いてある（学習中の
    // レベルが動かないほうが引き算が安定するため）。
    {
        const float tgt = juce::Decibels::decibelsToGain (
                              apvts.getRawParameterValue ("in_gain")->load());
        if (std::abs (tgt - 1.0f) > 1.0e-4f || std::abs (inGainNow - 1.0f) > 1.0e-4f)
        {
            auto* L = buffer.getWritePointer (0);
            auto* R = numCh > 1 ? buffer.getWritePointer (1) : nullptr;
            for (int n = 0; n < numSamples; ++n)
            {
                inGainNow += inGainA * (tgt - inGainNow);
                L[n] *= inGainNow;
                if (R) R[n] *= inGainNow;
            }
        }
    }

    dryBuffer.makeCopyOf (buffer, true);

    // ---- v1.4.0 AUTO SETUP: accumulate band energy of the raw voice for 5 s ----
    if (autoSetupCountdown.load() > 0 && asPrepared)
    {
        double sums[asBands] = {};
        float  pk = asPeak.load();
        for (int b = 0; b < asBands; ++b)
        {
            asScratch.makeCopyOf (dryBuffer, true);
            juce::dsp::AudioBlock<float> ab (asScratch);
            juce::dsp::ProcessContextReplacing<float> ac (ab);
            asBP[b].process (ac);
            double s = 0.0;
            for (int ch = 0; ch < asScratch.getNumChannels(); ++ch)
            {
                const float* r = asScratch.getReadPointer (ch);
                for (int n = 0; n < numSamples; ++n) s += (double) r[n] * r[n];
            }
            sums[b] = s;
        }
        for (int ch = 0; ch < dryBuffer.getNumChannels(); ++ch)
        {
            const float* r = dryBuffer.getReadPointer (ch);
            for (int n = 0; n < numSamples; ++n) pk = juce::jmax (pk, std::abs (r[n]));
        }
        for (int b = 0; b < asBands; ++b) asBandSum[b].store (asBandSum[b].load() + sums[b]);
        asSampleCount.store (asSampleCount.load() + numSamples);
        asPeak.store (pk);

        // level + dynamics statistics (used by the sing mode, cheap adds)
        {
            const float* r0 = dryBuffer.getReadPointer (0);
            double ss = 0.0;
            for (int n = 0; n < numSamples; ++n) ss += (double) r0[n] * r0[n];
            asSumSq.store (asSumSq.load() + ss);
            const float blockDb = (float) (10.0 * std::log10 (ss / juce::jmax (1, numSamples) + 1e-12));
            asBlockDbSum  .store (asBlockDbSum.load()   + blockDb);
            asBlockDbSqSum.store (asBlockDbSqSum.load() + (double) blockDb * blockDb);
            const int bc = asBlockCount.load() + 1;
            asBlockCount.store (bc);
            if (bc == 6 || (bc > 6 && blockDb < asMinBlockDb.load()))
                asMinBlockDb.store (blockDb);   // quietest block after warm-up
        }

        int left = autoSetupCountdown.load() - numSamples;
        autoSetupCountdown.store (left);
        if (left <= 0)
            autoSetupResult.store (100);   // 100 = "ready to apply" flag for the editor
    }

    // ---- v1.4.0 KEY/SCALE: capture the raw voice (cheap copy on the audio thread) ----
    // No pitch detection here: the autocorrelation used to run in this callback and its
    // burst overran small buffers. We now just store samples and analyse them off the
    // audio thread once the scan completes (finalizeKeyScanIfReady on the message thread).
    if (keyScanCountdown.load() > 0)
    {
        const float* in = dryBuffer.getReadPointer (0);
        const int cap = (int) keyCaptureBuf.size();
        int w = keyCaptureWrite.load (std::memory_order_relaxed);
        for (int n = 0; n < numSamples && w < cap; ++n) keyCaptureBuf[(size_t) w++] = in[n];
        keyCaptureWrite.store (w, std::memory_order_relaxed);

        const int left = juce::jmax (0, keyScanCountdown.load() - numSamples);
        keyScanCountdown.store (left);
        if (left == 0) keyCaptureReady.store (true, std::memory_order_release);
    }


    // tuner feed (dry mono)
    {
        const float* dl = dryBuffer.getReadPointer (0);
        const float* dr = dryBuffer.getReadPointer (juce::jmin (1, dryBuffer.getNumChannels() - 1));
        int pos = tunerPos.load (std::memory_order_relaxed);
        for (int n = 0; n < numSamples; ++n)
        {
            tunerBuf[pos] = 0.5f * (dl[n] + dr[n]);
            pos = (pos + 1) % tunerSize;
        }
        tunerPos.store (pos, std::memory_order_release);
    }

    // input meter
    {
        float pk = 0.0f;
        for (int ch = 0; ch < numCh; ++ch)
        {
            auto* q = dryBuffer.getReadPointer (ch);
            for (int n = 0; n < numSamples; ++n) pk = juce::jmax (pk, std::abs (q[n]));
        }
        const float cur = meterIn.load();
        meterIn.store (pk > cur ? pk : cur * 0.985f);
    }

    // ---- gate ----
    if (apvts.getRawParameterValue ("gate_on")->load() > 0.5f)
    {
        const float thr = juce::Decibels::decibelsToGain (apvts.getRawParameterValue ("gate")->load());
        for (int n = 0; n < numSamples; ++n)
        {
            float inPk = 0.0f;
            for (int ch = 0; ch < numCh; ++ch)
                inPk = juce::jmax (inPk, std::abs (buffer.getSample (ch, n)));
            gateEnv += (inPk > gateEnv ? gateEnvAtk : gateEnvRel) * (inPk - gateEnv);
            const float target = gateEnv >= thr ? 1.0f : 0.0f;
            gateGain += (target > gateGain ? gateOpenCoef : gateCloseCoef) * (target - gateGain);
            for (int ch = 0; ch < numCh; ++ch)
                buffer.setSample (ch, n, buffer.getSample (ch, n) * gateGain);
        }
    }
    else gateGain = 1.0f;   // v2.8.0: ゲートOFFなら「開いている」に戻す(下のノイズ除去が見る)

    // ---- de-noise (4-band adaptive downward expander, RX-style Learn) ----
    {
        const bool  dnOn   = apvts.getRawParameterValue ("dn_on")->load() > 0.5f;
        const float amount = apvts.getRawParameterValue ("denoise")->load() * 0.01f;
        int learn = learnCountdown.load();

        if ((amount > 0.001f && dnOn) || learn > 0)
        {
            // split into 4 bands (LR4 crossovers sum flat)
            bandBuf[0].makeCopyOf (buffer, true);
            bandBuf[1].makeCopyOf (buffer, true);
            {
                juce::dsp::AudioBlock<float> b0 (bandBuf[0]);
                juce::dsp::ProcessContextReplacing<float> c0 (b0);
                lrLP1.process (c0);                                  // band0 = LP250
                juce::dsp::AudioBlock<float> b1 (bandBuf[1]);
                juce::dsp::ProcessContextReplacing<float> c1x (b1);
                lrHP1.process (c1x);                                 // rest = HP250
            }
            bandBuf[2].makeCopyOf (bandBuf[1], true);
            {
                juce::dsp::AudioBlock<float> b1 (bandBuf[1]);
                juce::dsp::ProcessContextReplacing<float> c1x (b1);
                lrLP2.process (c1x);                                 // band1 = 250-1200
                juce::dsp::AudioBlock<float> b2 (bandBuf[2]);
                juce::dsp::ProcessContextReplacing<float> c2x (b2);
                lrHP2.process (c2x);                                 // rest = HP1200
            }
            bandBuf[3].makeCopyOf (bandBuf[2], true);
            {
                juce::dsp::AudioBlock<float> b2 (bandBuf[2]);
                juce::dsp::ProcessContextReplacing<float> c2x (b2);
                lrLP3.process (c2x);                                 // band2 = 1200-5000
                juce::dsp::AudioBlock<float> b3 (bandBuf[3]);
                juce::dsp::ProcessContextReplacing<float> c3x (b3);
                lrHP3.process (c3x);                                 // band3 = HP5000
            }

            const float maxAttenDb = dnOn ? 24.0f * amount : 0.0f;
            float attenSum = 0.0f;

            for (int n = 0; n < numSamples; ++n)
            {
                for (int b = 0; b < 4; ++b)
                {
                    float pk = 0.0f;
                    for (int ch = 0; ch < juce::jmin (numCh, 2); ++ch)
                        pk = juce::jmax (pk, std::abs (bandBuf[b].getSample (ch, n)));

                    dnEnv[b] += (pk > dnEnv[b] ? dnEnvAtk : dnEnvRel) * (pk - dnEnv[b]);

                    if (learn > 0)
                    {
                        dnFloorLearn[b] = juce::jmax (dnFloorLearn[b], dnEnv[b]);
                    }
                    else if (! dnLearned && gateGain > 0.99f)
                    {
                        // adaptive minimum tracking: fast down, slow drift up
                        // v2.8.0 ★ゲートが閉じている間は学習しない。
                        // 以前はゲート後の信号で床を測っていたので、フレーズの合間に
                        // ゲートが黙らせるたび床が最小値(1e-6)まで落ちていた。すると
                        // openThr = 床×2.5 が本物のノイズより下がってしまい、
                        // 「ゲートONだとノイズ除去がまったく効かない」状態になっていた。
                        // 戻りは +3dB/秒なので、一度落ちると20秒近く効かない。
                        if (dnEnv[b] < dnFloor[b]) dnFloor[b] = dnEnv[b];
                        else                       dnFloor[b] = juce::jmin (dnFloor[b] * dnFloorRise, 0.5f);
                        dnFloor[b] = juce::jmax (dnFloor[b], 1e-6f);
                    }

                    const float openThr = dnFloor[b] * 2.5f;   // ~+8 dB above floor
                    float targetGain = 1.0f;
                    if (dnEnv[b] < openThr)
                    {
                        const float below = juce::jlimit (0.0f, 1.0f,
                                              (openThr - dnEnv[b]) / juce::jmax (openThr, 1e-9f));
                        targetGain = juce::Decibels::decibelsToGain (-maxAttenDb * below);
                    }
                    dnGain[b] += (targetGain > dnGain[b] ? dnOpenCoef : dnCloseCoef)
                                     * (targetGain - dnGain[b]);
                    attenSum += 1.0f - dnGain[b];

                    for (int ch = 0; ch < numCh; ++ch)
                        bandBuf[b].setSample (ch, n,
                            bandBuf[b].getSample (juce::jmin (ch, 1), n) * dnGain[b]);
                }

                if (learn > 0) --learn;
            }

            if (learnCountdown.load() > 0)
            {
                learnCountdown.store (learn);
                if (learn <= 0)   // learning just finished: commit profile with margin
                {
                    for (int b = 0; b < 4; ++b)
                    {
                        dnFloor[b] = juce::jmax (dnFloorLearn[b] * 1.4f, 1e-6f);
                        dnFloorShared[b].store (dnFloor[b]);   // expose for state save
                    }
                    dnLearned = true;
                    dnLearnedShared.store (true);
                    markStateDirty();                          // autosave the new profile
                }
            }

            // recombine bands
            for (int ch = 0; ch < numCh; ++ch)
            {
                auto* out = buffer.getWritePointer (ch);
                for (int n = 0; n < numSamples; ++n)
                    out[n] = bandBuf[0].getSample (ch, n) + bandBuf[1].getSample (ch, n)
                           + bandBuf[2].getSample (ch, n) + bandBuf[3].getSample (ch, n);
            }

            meterDN.store (juce::jlimit (0.0f, 1.0f,
                               attenSum / (4.0f * (float) juce::jmax (1, numSamples))));
        }
        else
            meterDN.store (meterDN.load() * 0.9f);
    }

    juce::dsp::AudioBlock<float> block (buffer);
    juce::dsp::ProcessContextReplacing<float> ctx (block);

    // ---- v1.8.0 voice changer (formant-preserving) + 5-voice unison ----
    {
        // v2.9.0 セッションモード: ONの間、遅延をふやす3機能はここで素通しにする。
        // スイッチ自体は触らない(セッションを抜けたら元どおり鳴る)。
        const bool sessionOn = apvts.getRawParameterValue ("session")->load() > 0.5f;
        sessionActive.store (sessionOn);

        const bool vcOn = (apvts.getRawParameterValue ("vc_on")->load() > 0.5f)
                            && ! sessionOn
                          #if VOCALGZZIO_LITE
                            && false   // Lite版は非搭載
                          #endif
                            ;
        const bool jnOn = (apvts.getRawParameterValue ("jn_on")->load() > 0.5f)
                            && ! sessionOn
                          #if VOCALGZZIO_LITE
                            && false   // Lite版は非搭載
                          #endif
                            ;
        const bool atOn = (apvts.getRawParameterValue ("at_on")->load() > 0.5f)
                            && ! sessionOn
                          #if VOCALGZZIO_LITE
                            && false   // Lite版は非搭載
                          #endif
                            ;

        const bool pitchActive = vcOn || atOn;   // vcSh runs for the voice-changer AND/OR auto-tune

        // v1.9.0 修正: 停止中のシフターには前回の音・位相・OLA が残っている。
        // そのまま再開すると 1 窓ぶん(約17ms)、入力の10倍を超える音が飛び出す
        // ("押した瞬間に爆音" の原因)。有効化の瞬間に必ず初期化する。
        if (pitchActive && ! vcWasActive) for (auto& s : vcSh) s.reset();
        if (jnOn && ! jnWasActive)
        {
            for (auto& s : unSh) s.reset();
            jnContra.reset();                            // v2.0.0 反行の状態も初期化
            jnHeldSemi[0] = jnHeldSemi[1] = 0.0f;
        }
        vcWasActive = pitchActive;
        jnWasActive = jnOn;

        // v2.6.0: 再生中にホストへ「遅延が変わった」と伝えるのは**やめた**。
        //
        // Cubase はこの通知を受けると、その場でチャンネルの遅延補正グラフを
        // 組み直す。組み直しは「今なにをしていても」始まるので、
        //   ・再生中に来れば   → 音が止まる／出なくなる
        //   ・終了処理中に来れば → チャンネルの解放と取り合いになって固まる
        //     (「MixConsole をアンロード中」で止まるのはこれ)
        // という2種類の事故になる。音声スレッドから送ろうがメッセージ
        // スレッドから送ろうが、**タイミングを選べない**ことが問題なので、
        // 実行中は一切送らないことにした。
        //
        // 遅延の申告は prepareToPlay(ホストがグラフを組む安全な瞬間)だけで行う。
        // → 遅延ゼロという製品の前提は変わらない。ボイス変換をONにした状態で
        //   ミックスするときだけ、そのトラックが約17ms後ろにずれる。
        //   気になる場合はトラックディレイで戻すか、セッションモードで0にできる。
        // v2.9.0 ★jnOn を外した。ハモリは主メロの横に足す並列処理なので、
        //   主メロ自体は遅れない(実測0サンプル)。ここに jnOn を入れていたため、
        //   ハモリだけONのときに原音を17.4ms遅らせて混ぜていた＝Mixのコム再発。
        voiceLatency = pitchActive ? vcSh[0].latencySamples() : 0;
        // v2.9.0: 画面の「追加遅延」バッジはここの値を出す。prepareToPlay の
        // 申告値だけを見ていると、再生中にピッチ補正をONにしたとき
        // **バッジが +0.0ms のまま嘘をつく**。歌う人が知りたいのは
        // 「いま自分の声が何ms遅れて返ってくるか」なので、実際の値を毎ブロック置く。
        // (atomic への store。確保もロックもしない)
        reportedLatency.store (voiceLatency, std::memory_order_relaxed);

        // ---- v1.9.0 auto-tune: detect F0 (pre-shift), snap to the scale, glide the
        //      correction by the retune speed, and feed it to the shared shifter as
        //      an added pitch offset. Formant stays preserved (source-filter split). ----
        float atCorr = 0.0f;
        if (atOn || jnOn)
        {
            const int n = juce::jmin (numSamples, (int) vcMono.size());
            const float* Lin = buffer.getReadPointer (0);
            const float* Rin = numCh > 1 ? buffer.getReadPointer (1) : Lin;
            for (int i = 0; i < n; ++i) vcMono[(size_t) i] = 0.5f * (Lin[i] + Rin[i]);
            const float hz = pitchDet.process (vcMono.data(), n);

            const float refA   = apvts.getRawParameterValue ("refpitch")->load();
            const int   key    = (int) apvts.getRawParameterValue ("at_key")->load();
            const int   scId   = (int) apvts.getRawParameterValue ("at_scale")->load();
            const float amount = apvts.getRawParameterValue ("at_amount")->load() * 0.01f;
            const float speed  = apvts.getRawParameterValue ("at_speed")->load();

            const float blockSec = (float) numSamples / (float) currentSampleRate;

            // v1.9.3: 補正の基準を「速さ」で切り替える。
            //   速い側(ケロケロ) … 今この瞬間の音高を丸める。しゃくりも揺れも
            //                      すべて音符に吸着し、音程が階段状に動く。
            //   遅い側(自然)     … 約180msで追う「音の中心」だけを丸め、その差を
            //                      一定量として足す。歌手のビブラートは触らずに残る。
            float target = 0.0f;   // desired correction (semitones); 0 when unvoiced
            if (hz > 0.0f)
            {
                const float p = 69.0f + 12.0f * std::log2 (hz / refA);
                if (atPitchCenter <= 0.0f) atPitchCenter = p;          // 歌い出し
                const float aC = 1.0f - std::exp (-blockSec / 0.180f); // 180 ms
                atPitchCenter += (p - atPitchCenter) * aC;

                const float snapNow = gz::scale::snap (p, key, scId);
                const float snapCtr = gz::scale::snap (atPitchCenter, key, scId);
                const float corrHard    = juce::jlimit (-6.0f, 6.0f, snapNow - p);
                const float corrNatural = juce::jlimit (-6.0f, 6.0f, snapCtr - atPitchCenter);

                // speed 20%を境に、0%で完全に瞬時・70%以上で完全に中心基準へ
                const float centerW = juce::jlimit (0.0f, 1.0f, (speed - 20.0f) / 50.0f);
                const float corr = corrHard * (1.0f - centerW) + corrNatural * centerW;
                target = corr * amount;
            }
            else atPitchCenter = 0.0f;   // 無声区間でリセットし、次の歌い出しに備える
            atDetectedHz.store (hz);

            // ---- v2.7.0 こぶし(しゃくり・こぶし保護) ----
            // 音程の「動き方」だけを見て、しゃくり／こぶし／音の渡りの最中だけ
            // 補正を引っ込める。音そのものには一切触らないのでゼロ遅延のまま。
            // 詳細は Source/Ornament.h。
            {
                const float ornAmt = apvts.getRawParameterValue ("orn_amt")->load() * 0.01f;
                ornGuard.setAmount (ornAmt);
                // 半音単位の音高(無声は0以下)を渡す。基準ピッチは補正側と同じもの。
                const float pSemi = (hz > 0.0f) ? (69.0f + 12.0f * std::log2 (hz / refA)) : 0.0f;
                const float keep  = ornGuard.process (pSemi, blockSec);
                target *= keep;
                if (ornAmt > 0.001f)
                {
                    meterOrn.store (ornGuard.lastProtect());
                    meterOrnKind.store (ornGuard.lastKind());
                }
                else if (meterOrn.load() != 0.0f) { meterOrn.store (0.0f); meterOrnKind.store (0); }
            }

            // ケロケロ判定: 速さがほぼ0で、補正量も強い設定のとき
            const bool hard = (speed <= 8.0f) && (amount >= 0.90f);

            // retune-speed glide: tau grows with 'speed' (0 => instant snap,
            // 100 => ~180 ms smooth). Time constant is block-size independent.
            if (hard)
            {
                atCorrection = target;          // 平滑化なし = 音程が階段状に飛ぶ
            }
            else
            {
                const float tauMs = 1.5f + (speed * speed / 10000.0f) * 178.5f;
                const float alpha = 1.0f - std::exp (-blockSec / (tauMs * 0.001f));
                atCorrection += (target - atCorrection) * alpha;
            }
            atCorr = atCorrection;
            atCurrentCorrection.store (atCorr);

            // v1.9.0: 音程が合っているときはボコーダを通さず素の声のまま出す。
            // (ヒステリシス付き: 0.05半音を超えたら通し、0.02半音を下回ったら戻す)
            // v1.9.3: ただしケロケロ設定では常時通す。素の声に戻ると独特の
            //         質感が途切れて「効いていない」ように聞こえるため。
            if (hard)
            {
                atWetMix = 1.0f;
            }
            else
            {
                const float mag = std::abs (atCorr);
                const float want = (mag > 0.05f) ? 1.0f : (mag < 0.02f ? 0.0f : atWetMix);
                const float aMix = 1.0f - std::exp (-blockSec / 0.015f);   // 15 ms
                atWetMix += (want - atWetMix) * aMix;
            }
        }
        else { atCorrection = 0.0f; atWetMix = 0.0f; atPitchCenter = 0.0f; atDetectedHz.store (0.0f); atCurrentCorrection.store (0.0f);
               if (meterOrn.load() != 0.0f) { meterOrn.store (0.0f); meterOrnKind.store (0); } }

        if (pitchActive || jnOn)
        {
            const float pit  = (vcOn ? apvts.getRawParameterValue ("vc_pitch")->load() : 0.0f) + atCorr;
            const float frm  = vcOn ? apvts.getRawParameterValue ("vc_form")->load() : 0.0f;
            // 声変え時はユーザーのミックス。ピッチ補正だけのときは「補正が要る間だけ」通す。
            const float vmix = vcOn ? apvts.getRawParameterValue ("vc_mix")->load() * 0.01f : atWetMix;

            if (pitchActive)
                for (int ch = 0; ch < juce::jmin (numCh, 2); ++ch)
                {
                    float* p = buffer.getWritePointer (ch);
                    // v2.6.0 安全弁: 位相メモリ(再帰状態)は一度 NaN/Inf が入ると
                    // 自己回復せず永久に無音を出し続ける。加工前の音を取っておき、
                    // 壊れたブロックは元の音に差し戻してシフターを初期化する。
                    // 次のブロックからは何事もなく続く(欠けは最大1ブロック=数ms)。
                    const int nn = juce::jmin (numSamples, (int) vcDry[ch].size());
                    juce::FloatVectorOperations::copy (vcDry[ch].data(), p, nn);
                    vcSh[ch].setParams (pit, frm, vmix);
                    vcSh[ch].processBlock (p, numSamples);
                    if (! gzBlockFinite (p, numSamples))
                    {
                        juce::FloatVectorOperations::copy (p, vcDry[ch].data(), nn);
                        vcSh[ch].reset();
                    }
                }

            if (jnOn)
            {
                // mono feed (post voice-changer) for the 4 generated "members"
                const int n = juce::jmin (numSamples, (int) vcMono.size());
                const float* L = buffer.getReadPointer (0);
                const float* R = numCh > 1 ? buffer.getReadPointer (1) : L;
                for (int i = 0; i < n; ++i) vcMono[(size_t) i] = 0.5f * (L[i] + R[i]);

                const float jmix = apvts.getRawParameterValue ("jn_mix")->load() * 0.01f;
                const int   harm = (int) apvts.getRawParameterValue ("jn_harm")->load();
                // per-member character: detune (cents), formant (st), pan
                static const float dCents[4] = { +9.0f, -11.0f, +16.0f, -15.0f };
                static const float dForm [4] = { +1.5f,  -1.5f,  +3.2f,  -2.8f };
                static const float pan   [4] = { -0.6f,  +0.6f,  -0.3f,  +0.3f };
                // v1.9.8: ハモリを「半音固定」から「音階の度数」へ。長3度(4半音)と
                // 短3度(3半音)は音階上の位置で決まるので、度数で動かさないと必ずぶつかる。
                // v2.0.0: 8=反行(ContraryLine)を追加。UIも9項目すべて出すようにした。
                static const int hDeg[9][4] = {
                    {  0,  0,  0,  0 },   // 0 ユニゾン
                    {  0,  0, +2, +2 },   // 1 3度上
                    {  0,  0, -2, -2 },   // 2 3度下
                    {  0,  0, +5, +5 },   // 3 6度上(3度のオクターブ違い・柔らかい)
                    {  0,  0, -5, -5 },   // 4 6度下
                    {  0,  0, +4, +4 },   // 5 5度上(硬く澄んだ響き)
                    {  0,  0, +2, +4 },   // 6 3度＋5度
                    {  0,  0, +2, +2 },   // 7 おまかせ(上昇フレーズは下・下降は上)
                    {  0,  0,  0,  0 },   // 8 反行(メロディと逆に動く対旋律)
                };
                const int   hKey  = (int) apvts.getRawParameterValue ("at_key")->load();
                const int   hScId = (int) apvts.getRawParameterValue ("at_scale")->load();
                const float hzH   = atDetectedHz.load();

                // モードが変わったら、保持していた音程と反行の状態を捨てる
                if (harm != jnLastHarm)
                {
                    jnLastHarm = harm;
                    jnHeldSemi[0] = jnHeldSemi[1] = 0.0f;
                    jnContra.reset();
                }

                float hSemi[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
                if (hzH > 0.0f && harm > 0)
                {
                    // v2.8.0: ここだけ 440Hz 固定だった。基準ピッチを 442 などに
                    // していると主メロは 442、ハモリは 440 の格子に乗るので、
                    // 常に十数セントずれて「うなり」が出ていた。
                    const float hRefA = juce::jmax (100.0f,
                                            apvts.getRawParameterValue ("refpitch")->load());
                    const float pH = 69.0f + 12.0f * std::log2 (hzH / hRefA);
                    int flip = 1;
                    if (harm == 7)   // 上昇フレーズなら下へ、下降フレーズなら上へ
                    {
                        flip = (pH > jnLastPitch + 0.25f) ? -1
                             : (pH < jnLastPitch - 0.25f ? +1 : jnLastDir);
                        jnLastDir = flip;
                    }
                    // v2.0.0: 平滑係数をブロック長に合わせる(小バッファでも同じ速さ)
                    {
                        const float aJn = 1.0f - std::exp (-(float) numSamples
                                                           / (0.060f * (float) currentSampleRate));
                        jnLastPitch += (pH - jnLastPitch) * aJn;
                    }

                    if (harm == 8)   // 反行: 2声とも同じ対旋律(デチューンで厚み)
                    {
                        const float cn = jnContra.update (pH, hKey, hScId);
                        hSemi[2] = hSemi[3] = cn - pH;
                    }
                    else
                        for (int v = 2; v < 4; ++v)
                        {
                            const int deg = hDeg[harm][v] * flip;
                            if (deg == 0) continue;
                            float note = gz::scale::step (pH, hKey, hScId, deg);
                            // トライトーン(6半音)は強い濁り。1度ずらして避ける
                            if (std::abs ((int) std::lround (note - pH)) == 6)
                                note = gz::scale::step (pH, hKey, hScId, deg + (deg > 0 ? 1 : -1));
                            hSemi[v] = note - pH;      // 歌い手の抑揚に寄り添う
                        }

                    jnHeldSemi[0] = hSemi[2]; jnHeldSemi[1] = hSemi[3];
                }
                else if (harm > 0)
                {
                    // v2.0.0: 無声区間(子音・ブレス)はピッチ検出が0になる。ここで
                    // ハモリを0半音へ落とすと一瞬だけ「本人の声」が混ざってしまい、
                    // 特にハモだけ出力で原音漏れに聞こえる。直前の音程で歌い続ける。
                    hSemi[2] = jnHeldSemi[0]; hSemi[3] = jnHeldSemi[1];
                }

                float* outL = buffer.getWritePointer (0);
                float* outR = numCh > 1 ? buffer.getWritePointer (1) : outL;

                // v1.9.9「ハモだけ」: 原音を消してハモリ声部だけを残す。
                // v2.0.0修正: 以前はユニゾン隊(v0/v1=本人とほぼ同じ音程)も鳴らして
                // いたため「原音が消えていない」ように聞こえた。ハモリ音程が選ばれて
                // いるときは、実際にハモっている2声(v2/v3)だけを出す。
                const bool soloOn = apvts.getRawParameterValue ("jn_solo")->load() > 0.5f;
                if (soloOn)
                {
                    juce::FloatVectorOperations::clear (outL, numSamples);
                    if (outR != outL) juce::FloatVectorOperations::clear (outR, numSamples);
                }
                const bool skipUnisonPair = soloOn && harm > 0;

                for (int v = 0; v < 4; ++v)
                {
                    std::copy (vcMono.begin(), vcMono.begin() + n, vcTmp.begin());
                    unSh[v].setParams (dCents[v] / 100.0f + hSemi[v], dForm[v], 1.0f);
                    unSh[v].processBlock (vcTmp.data(), n);
                    // v2.6.0 安全弁: ハモリ声部も同様に。壊れたブロックはその声だけ
                    // 1ブロック休ませて(無音)、初期化して次から復帰する。
                    if (! gzBlockFinite (vcTmp.data(), n))
                    {
                        std::fill (vcTmp.begin(), vcTmp.begin() + n, 0.0f);
                        unSh[v].reset();
                    }

                    // ユニゾン隊はミュート(シフター・ディレイは回し続けて、解除した
                    // 瞬間に古い音が飛び出さないようにする)。2声になった分は+3dB。
                    const float g  = (skipUnisonPair && v < 2) ? 0.0f
                                   : (skipUnisonPair ? 0.64f : 0.45f) * jmix;
                    const float gL = g * 0.5f * (1.0f - pan[v]);
                    const float gR = g * 0.5f * (1.0f + pan[v]);
                    float* dl = unDelay[v]; int w = unDelayW[v]; const int d = unDelaySmp[v];
                    for (int i = 0; i < n; ++i)
                    {
                        dl[w] = vcTmp[(size_t) i];
                        int rp = w - d; if (rp < 0) rp += 4096;
                        const float s = dl[rp];
                        outL[i] += s * gL;
                        outR[i] += s * gR;
                        if (++w >= 4096) w = 0;
                    }
                    unDelayW[v] = w;
                }
            }
        }
    }

    // ---- v2.3.0 ポップ(破裂音) / リップ(口の粘着音) 除去 ----
    // ローカットの手前で処理する。検出側は加工前の低域を見たほうが確実なため。
    {
        const float popAmt = apvts.getRawParameterValue ("pop_amt")->load() * 0.01f;
        const float lipAmt = apvts.getRawParameterValue ("lip_amt")->load() * 0.01f;

        if (popAmt > 0.001f || lipAmt > 0.001f)
        {
            auto* L = buffer.getWritePointer (0);
            auto* R = numCh > 1 ? buffer.getWritePointer (1) : nullptr;
            float popPk = 0.0f, lipPk = 0.0f;

            for (int n = 0; n < numSamples; ++n)
            {
                const float x = (R != nullptr) ? 0.5f * (L[n] + R[n]) : L[n];

                // ===== ポップ(破裂音) =====
                // 「ぱ・ば行」は 120Hz 以下に短い爆発的なエネルギーが出る。
                // 低い歌声の基音と区別するため、(1)速い包絡が遅い包絡を大きく
                // 上回る＝突発的 (2)低域が中域より強い、の両方を条件にする。
                float gPop = 0.0f;
                if (popAmt > 0.001f)
                {
                    const float lf  = std::abs (popDet.processSample (x));
                    const float mid = std::abs (popMidDet.processSample (x));  // 170Hz以上(歌なら必ず出る)
                    popLfFast += (lf  > popLfFast ? popLfFastA : popLfFastR) * (lf  - popLfFast);
                    popLfSlow += (lf  > popLfSlow ? popLfSlowA : popLfSlowR) * (lf  - popLfSlow);
                    popMid    += (mid > popMid    ? popMidA    : popMidR)    * (mid - popMid);

                    if (popLfFast > 2.0e-4f)     // 無音では動かさない
                    {
                        const float burst = popLfFast / (popLfSlow * 2.0f + 1.0e-6f) - 1.0f;
                        const float domin = popLfFast / (popMid    * 1.6f + 1.0e-6f) - 1.0f;
                        gPop = juce::jlimit (0.0f, 1.0f, burst) * juce::jlimit (0.0f, 1.0f, domin);
                    }
                    gPop *= popAmt;
                }
                popG += (gPop > popG ? popGA : popGR) * (gPop - popG);
                popPk = juce::jmax (popPk, popG);

                // ===== リップ(口の粘着音) =====
                // 歌っていない静かな所で、3〜4kHz に一瞬だけ立つ鋭い音を狙う。
                // 歌の最中(bodyが大きい)は絶対に動かさないので子音は削れない。
                float gLip = 0.0f;
                if (lipAmt > 0.001f)
                {
                    const float hf = std::abs (lipDet.processSample (x));
                    const float bd = std::abs (lipBodyDet.processSample (x));   // 800Hz以下=歌っているか
                    lipFast += (hf > lipFast ? lipFastA : lipFastR) * (hf - lipFast);
                    lipSlow += (hf > lipSlow ? lipSlowA : lipSlowR) * (hf - lipSlow);
                    lipBody += (bd > lipBody ? lipBodyA : lipBodyR) * (bd - lipBody);

                    if (lipFast > 1.0e-4f && lipBody < 0.02f)      // 歌っていない区間のみ
                    {
                        const float spike = lipFast / (lipSlow * 4.0f + 1.0e-6f) - 1.0f;
                        gLip = juce::jlimit (0.0f, 1.0f, spike);
                    }
                    gLip *= lipAmt;
                }
                lipG += (gLip > lipG ? lipGA : lipGR) * (gLip - lipG);
                lipPk = juce::jmax (lipPk, lipG);

                // ===== 適用: 平行フィルタへ寄せる(係数の作り直しなし) =====
                for (int ch = 0; ch < juce::jmin (numCh, 2); ++ch)
                {
                    float* p = (ch == 0) ? L : R;
                    if (p == nullptr) break;
                    float v = p[n];
                    if (popG > 0.0005f)
                    {
                        float h = popHp[ch][1].processSample (popHp[ch][0].processSample (v));
                        v += popG * (h - v);
                    }
                    else { popHp[ch][0].processSample (v); popHp[ch][1].processSample (v); }

                    if (lipG > 0.0005f)
                    {
                        float l = lipLp[ch][1].processSample (lipLp[ch][0].processSample (v));
                        v += lipG * (l - v);
                    }
                    else { lipLp[ch][0].processSample (v); lipLp[ch][1].processSample (v); }

                    p[n] = v;
                }
            }
            meterPop.store (popPk);
            meterLip.store (lipPk);
        }
        else if (meterPop.load() > 0.0f || meterLip.load() > 0.0f)
        {
            popG = lipG = 0.0f;
            meterPop.store (0.0f); meterLip.store (0.0f);
            for (int ch = 0; ch < 2; ++ch)
                for (int st = 0; st < 2; ++st) { popHp[ch][st].reset(); lipLp[ch][st].reset(); }
        }
    }

    // ---- subtractive EQ ----
    hpf.process (ctx);
    mud.process (ctx);
    harsh.process (ctx);

    // ---- v2.4.0 なめらか(動的レゾナンス抑制) ----
    // 「こもり」「キンキン」(固定EQ)のあと・コンプの前。コンプより前に刺さりを
    // 削っておかないと、コンプが刺さりごと持ち上げてしまうため。ゼロ遅延。
    {
        const float resAmt = apvts.getRawParameterValue ("res_amt")->load() * 0.01f;
        resTamer.setAmount (resAmt);
        if (resAmt > 0.001f)
        {
            resTamer.process (buffer.getWritePointer (0),
                              numCh > 1 ? buffer.getWritePointer (1) : nullptr,
                              numSamples);
            meterRes.store (resTamer.lastMaxCutDb());
        }
        else if (meterRes.load() != 0.0f)
            meterRes.store (0.0f);
    }

    // ---- 2-stage compression with GR metering ----
    float prePk = 0.0f;
    for (int ch = 0; ch < numCh; ++ch)
    {
        auto* q = buffer.getReadPointer (ch);
        for (int n = 0; n < numSamples; ++n) prePk = juce::jmax (prePk, std::abs (q[n]));
    }
    prePk = juce::jmax (prePk, 1e-7f);

    if (apvts.getRawParameterValue ("comp1")->load() > 0.5f) comp1.process (ctx);
    if (apvts.getRawParameterValue ("comp2")->load() > 0.5f) comp2.process (ctx);

    {
        float postPk = 0.0f;
        for (int ch = 0; ch < numCh; ++ch)
        {
            auto* q = buffer.getReadPointer (ch);
            for (int n = 0; n < numSamples; ++n) postPk = juce::jmax (postPk, std::abs (q[n]));
        }
        const float grDb = 20.0f * std::log10 (juce::jmax (postPk, 1e-7f) / prePk);
        const float cur  = meterGR.load();
        meterGR.store (grDb < cur ? grDb : cur * 0.90f + grDb * 0.10f);
    }

    // ---- Smart Dynamic EQ (after compression, per best practice) ----
    processSmartEQ (buffer);

    // ---- de-esser (split-band style: detect >5.2k, duck a 6.5k high shelf) ----
    {
        const float amount = apvts.getRawParameterValue ("deess")->load() * 0.01f;
        const bool  dsOn   = apvts.getRawParameterValue ("ds_on")->load() > 0.5f;
        if (amount > 0.001f && dsOn)
        {
            scratch.makeCopyOf (buffer, true);
            juce::dsp::AudioBlock<float> sblock (scratch);
            juce::dsp::ProcessContextReplacing<float> sctx (sblock);
            deessDetectHP.process (sctx);   // detector band

            // envelope of sibilant band (block-wise per sample)
            const float thr = juce::Decibels::decibelsToGain (-30.0f + (1.0f - amount) * 12.0f);
            const float maxCutDb = 12.0f * amount + 4.0f;   // up to ~16 dB

            auto* sL = scratch.getReadPointer (0);
            auto* sR = scratch.getReadPointer (juce::jmin (1, scratch.getNumChannels() - 1));
            auto* bL = buffer.getWritePointer (0);
            auto* bR = numCh > 1 ? buffer.getWritePointer (1) : nullptr;

            for (int n = 0; n < numSamples; ++n)
            {
                const float det = juce::jmax (std::abs (sL[n]), std::abs (sR[n]));
                dsEnv += (det > dsEnv ? dsEnvAtk : dsEnvRel) * (det - dsEnv);

                // desired shelf cut in dB when sibilance exceeds threshold
                float wantDb = 0.0f;
                if (dsEnv > thr)
                    wantDb = juce::jlimit (0.0f, maxCutDb,
                                           20.0f * std::log10 (dsEnv / thr) * 1.5f);
                dsCurrentReduction += 0.02f * (wantDb - dsCurrentReduction);

                // update shelf every 32 samples (cheap enough, smooth enough)
                if ((n & 31) == 0)
                {
                    // v2.8.0: 32サンプルごとに new していた。確保なしの形へ。
                    const auto co = ACoefs::makeHighShelf (currentSampleRate, 6500.0f, 0.8f,
                                        juce::Decibels::decibelsToGain (-dsCurrentReduction));
                    *dsShelfL.coefficients = co;
                    *dsShelfR.coefficients = co;
                }
                bL[n] = dsShelfL.processSample (bL[n]);
                if (bR) bR[n] = dsShelfR.processSample (bR[n]);
            }
            meterDS.store (juce::jlimit (0.0f, 1.0f, dsCurrentReduction / 16.0f));
        }
        else
        {
            dsCurrentReduction *= 0.9f;
            meterDS.store (juce::jlimit (0.0f, 1.0f, dsCurrentReduction / 16.0f));
        }
    }

    // ---- v2.4.0 音量キープ(自動ゲインライド / Vocal Rider 相当) ----
    // ならし圧縮(コンプ=速い波)とは別系統。300msのラウドネスを見て、目標
    // (-18dBFS RMS)へ±9dBの範囲でゆっくりフェーダーを動かす。無音や息つぎ
    // (-45dBFS未満)ではゲインを凍結するので、ノイズ床を持ち上げない。
    // ただの掛け算なのでゼロ遅延のまま。コンプとディエッサーの後に置く。
    {
        const float amt = apvts.getRawParameterValue ("ride_amt")->load() * 0.01f;
        if (amt > 0.001f)
        {
            auto* L = buffer.getWritePointer (0);
            auto* R = numCh > 1 ? buffer.getWritePointer (1) : nullptr;
            const float maxDb = 9.0f;
            for (int n = 0; n < numSamples; ++n)
            {
                const float x = (R != nullptr) ? 0.5f * (L[n] + R[n]) : L[n];
                rideEnv2 += rideRmsA * (x * x - rideEnv2);
                const float rmsDb = 10.0f * std::log10 (rideEnv2 + 1.0e-12f);
                if (rmsDb > -45.0f)   // 歌って/話しているときだけ動く
                {
                    const float want = juce::jlimit (-maxDb, maxDb, -18.0f - rmsDb) * amt;
                    rideGDb += rideSlewA * (want - rideGDb);
                }
                const float g = juce::Decibels::decibelsToGain (rideGDb);
                L[n] *= g;
                if (R) R[n] *= g;
            }
            meterRide.store (rideGDb);
        }
        else if (meterRide.load() != 0.0f) { meterRide.store (0.0f); rideGDb = 0.0f; }
    }

    // ---- v2.6.0 ことば(子音エンハンサー) ----
    // コンプ・ディエッサーの「あと」に置く。コンプは母音を持ち上げて子音を
    // 相対的に埋めてしまうので、埋まった状態を見てから起こしたほうが正確。
    // ディエッサーより後なので、サ行を持ち上げ直してしまう心配もない。
    {
        const float consAmt = apvts.getRawParameterValue ("cons_amt")->load() * 0.01f;
        consEnh.setAmount (consAmt);
        if (consAmt > 0.001f)
        {
            consEnh.process (buffer.getWritePointer (0),
                             numCh > 1 ? buffer.getWritePointer (1) : nullptr,
                             numSamples);
            meterCons.store (consEnh.lastBoostDb());
        }
        else if (meterCons.load() != 0.0f)
            meterCons.store (0.0f);
    }

    // ---- additive EQ ----
    presence.process (ctx);
    air.process (ctx);

    // ---- v2.0.0 息(Breath): 小声のときだけ息の帯域を持ち上げる ----
    // バラードの「ささやき」を近くに感じさせる定番処理。大声では何もしないので
    // 歯擦音がきつくならない(ディエッサーの逆向きの動き)。
    {
        const float brAmt = apvts.getRawParameterValue ("br_amt")->load() * 0.01f;
        if (brAmt > 0.001f)
        {
            const float* q = buffer.getReadPointer (0);
            float pk = 0.0f;
            for (int n = 0; n < numSamples; ++n) pk = juce::jmax (pk, std::abs (q[n]));
            brEnv += (pk > brEnv ? brEnvAtk : brEnvRel) * (pk - brEnv);
            const float envDb = 20.0f * std::log10 (juce::jmax (brEnv, 1.0e-6f));

            // -20dB以下から効き始め、-42dBで最大。上限 +8dB * amt。
            float want = 0.0f;
            if (envDb < -20.0f)
                want = juce::jlimit (0.0f, 8.0f, (-20.0f - envDb) * 0.36f) * brAmt;
            brGainDb += 0.12f * (want - brGainDb);   // ~ブロック単位のスムージング

            if (std::abs (brGainDb - brApplied) > 0.25f)
            {
                brApplied = brGainDb;
                const auto c = ACoefs::makeHighShelf (currentSampleRate, 4500.0f, 0.71f,
                                   juce::Decibels::decibelsToGain (brApplied));   // v2.8.0: 確保なし
                *brShelfL.coefficients = c; *brShelfR.coefficients = c;
            }
            auto* L = buffer.getWritePointer (0);
            auto* R = numCh > 1 ? buffer.getWritePointer (1) : nullptr;
            for (int i = 0; i < numSamples; ++i)
            {
                L[i] = brShelfL.processSample (L[i]);
                if (R) R[i] = brShelfR.processSample (R[i]);
            }
        }
        else if (brApplied != -99.0f)
        {
            brApplied = -99.0f; brGainDb = 0.0f;
            brShelfL.reset(); brShelfR.reset();
        }
    }

    // ---- v1.9.5 艶 (Ring): 母音のときだけ 3kHz を持ち上げる ----
    {
        const float ringAmt = apvts.getRawParameterValue ("ring")->load() * 0.01f;   // 0..1
        if (ringAmt > 0.001f)
        {
            const float sr    = (float) currentSampleRate;
            const float aAtk  = 1.0f - std::exp (-1.0f / (0.004f * sr));   // 4 ms
            const float aRel  = 1.0f - std::exp (-1.0f / (0.060f * sr));   // 60 ms
            auto* L = buffer.getWritePointer (0);
            auto* R = (numCh > 1) ? buffer.getWritePointer (1) : nullptr;

            for (int i = 0; i < numSamples; ++i)
            {
                const float m = R ? 0.5f * (L[i] + R[i]) : L[i];
                const float rb = std::abs (ringDet.processSample (m));
                const float sb = std::abs (sibDet .processSample (m));
                ringEnv += (rb - ringEnv) * (rb > ringEnv ? aAtk : aRel);
                sibEnv  += (sb - sibEnv ) * (sb > sibEnv  ? aAtk : aRel);
            }

            // 3kHz帯が高域帯より優勢なら母音、そうでなければサ行とみなす
            const float dom  = ringEnv / (ringEnv + sibEnv + 1.0e-9f);
            const float open = juce::jlimit (0.0f, 1.0f, (dom - 0.45f) / 0.25f);
            // 無音や暗騒音では持ち上げない
            const float lvl  = juce::jlimit (0.0f, 1.0f, (ringEnv - 0.0004f) / 0.004f);
            const float want = 6.0f * ringAmt * open * lvl;        // 最大 +6 dB

            // 引くのは速く、足すのはゆっくり（サ行で刺さらないように）
            const float g = (want < ringGainDb) ? 0.35f : 0.06f;
            ringGainDb += (want - ringGainDb) * g;

            if (std::abs (ringGainDb - ringApplied) > 0.15f)
            {
                ringApplied = ringGainDb;
                const auto c = ACoefs::makePeakFilter (currentSampleRate, 3000.0f, 1.4f,
                                   juce::Decibels::decibelsToGain (ringApplied));   // v2.8.0: 確保なし
                *ringL.coefficients = c; *ringR.coefficients = c;
            }
            for (int i = 0; i < numSamples; ++i)
            {
                L[i] = ringL.processSample (L[i]);
                if (R) R[i] = ringR.processSample (R[i]);
            }
        }
        else if (ringGainDb != 0.0f) { ringGainDb = 0.0f; ringApplied = -99.0f; ringL.reset(); ringR.reset(); }
    }

    makeup.process (ctx);

    // ---- Warmth + Sustain (のび) + dry/wet ----
    // v2.0.0: ハモだけ出力中は、Mixツマミの「原音を混ぜ戻す」側を無効にする。
    // ここで dry(=入力そのまま) が混ざると、せっかく消した原音が復活してしまう。
    const bool hamoDake = apvts.getRawParameterValue ("jn_solo")->load() > 0.5f
                       && apvts.getRawParameterValue ("jn_on")->load()   > 0.5f;
    const float wet = hamoDake ? 1.0f : apvts.getRawParameterValue ("mix")->load() * 0.01f;
    const float dry = 1.0f - wet;
    const float driveAmt = apvts.getRawParameterValue ("drive")->load() * 0.01f;
    const float k = 1.0f + driveAmt * 5.0f;
    const float susAmt = apvts.getRawParameterValue ("sustain")->load() * 0.01f;

    // v2.8.0 ★Mixで混ぜ戻す原音を、加工側と同じだけ遅らせる。
    // ボイス変換/ピッチ補正/ハモリのどれかがONだと加工側は voiceLatency(約16ms)
    // 後ろにずれる。遅れていない原音をそこへ混ぜると 16ms のコムフィルタになり、
    // 62.5Hz おきに音が消える＝Mixを中間にしたときだけ「スカスカ」になっていた。
    // 原音は常にリングへ書き込み、必要なぶんだけ遅らせて読み出す。
    // (voiceLatency は prepareToPlay でしか変わらないので、ここは読むだけで安全)
    const juce::AudioBuffer<float>* dryMixSrc = &dryBuffer;
    if (voiceLatency > 0 && dryRing.getNumSamples() > 0
        && numSamples <= dryAligned.getNumSamples())
    {
        const int ringLen = dryRing.getNumSamples();
        const int lat     = juce::jmin (voiceLatency, ringLen - 1);
        for (int ch = 0; ch < juce::jmin (2, numCh); ++ch)
        {
            const float* s = dryBuffer.getReadPointer (juce::jmin (ch, dryBuffer.getNumChannels() - 1));
            float*       r = dryRing.getWritePointer (ch);
            float*       o = dryAligned.getWritePointer (ch);
            int w = dryRingW;
            for (int n = 0; n < numSamples; ++n)
            {
                r[w] = s[n];
                int rd = w - lat; if (rd < 0) rd += ringLen;
                o[n] = r[rd];
                if (++w >= ringLen) w = 0;
            }
        }
        dryRingW = (dryRingW + numSamples) % ringLen;
        dryMixSrc = &dryAligned;
    }

    for (int ch = 0; ch < numCh; ++ch)
    {
        auto* w = buffer.getWritePointer (ch);
        auto* d = dryMixSrc->getReadPointer (juce::jmin (ch, dryMixSrc->getNumChannels() - 1));
        for (int n = 0; n < numSamples; ++n)
        {
            float x = w[n];
            if (driveAmt > 0.001f)
                x = std::tanh (x * k) / k;

            // sustain: track envelope on ch0, lift the decaying tail + even harmonics
            if (susAmt > 0.001f)
            {
                if (ch == 0)
                {
                    const float pk = std::abs (x);
                    susEnv += (pk > susEnv ? susEnvAtk : susEnvRel) * (pk - susEnv);
                    const float envDb = 20.0f * std::log10 (juce::jmax (susEnv, 1e-6f));
                    // below -18 dB the tail gets lifted, up to +7 dB at -40 dB
                    float wantLift = 0.0f;
                    if (envDb < -18.0f && envDb > -55.0f)
                        wantLift = juce::jlimit (0.0f, 7.0f, (-18.0f - envDb) * 0.32f) * susAmt;
                    susLift += 0.002f * (wantLift - susLift);
                }
                const float lg = juce::Decibels::decibelsToGain (susLift);
                const float bias = 0.35f * susAmt;
                const float sat  = std::tanh (x * lg * (1.0f + susAmt) + bias) - std::tanh (bias);
                x = x * (1.0f - 0.5f * susAmt) + sat * 0.5f * susAmt + x * (lg - 1.0f) * 0.6f;
            }

            w[n] = x * wet + d[n] * dry;
        }
    }

    // ---- v1.4.0 character FX: robot voice (ring mod) then megaphone ----
    {
        const bool  roboOn = apvts.getRawParameterValue ("robo_on")->load() > 0.5f;
        const float roboM  = roboOn ? apvts.getRawParameterValue ("robo_mix")->load() * 0.01f : 0.0f;
        if (roboM > 0.001f)
        {
            const float f   = apvts.getRawParameterValue ("robo_freq")->load();
            const float inc = juce::MathConstants<float>::twoPi * f / (float) currentSampleRate;
            for (int n = 0; n < numSamples; ++n)
            {
                const float s = std::sin (roboPhase);
                roboPhase += inc;
                if (roboPhase > juce::MathConstants<float>::twoPi)
                    roboPhase -= juce::MathConstants<float>::twoPi;
                for (int ch = 0; ch < numCh; ++ch)
                {
                    const float x = buffer.getSample (ch, n);
                    buffer.setSample (ch, n, x * (1.0f - roboM) + x * s * roboM);
                }
            }
        }

        const bool  megaOn = apvts.getRawParameterValue ("mega_on")->load() > 0.5f;
        const float megaA  = megaOn ? apvts.getRawParameterValue ("mega_amt")->load() * 0.01f : 0.0f;
        if (megaA > 0.001f)
        {
            const int type = (int) apvts.getRawParameterValue ("mega_type")->load();
            scratch.makeCopyOf (buffer, true);
            juce::dsp::AudioBlock<float> mb (scratch);
            juce::dsp::ProcessContextReplacing<float> mc (mb);
            megaHP.process (mc);
            megaPeak.process (mc);
            megaLP.process (mc);

            const float mk   = 1.0f + megaA * (type == 1 ? 6.0f : 11.0f);   // radio drives softer
            const float qLev = type == 2 ? std::pow (2.0f, 9.0f - megaA * 6.0f) : 0.0f;   // lo-fi crush

            for (int ch = 0; ch < numCh; ++ch)
            {
                auto* w = buffer.getWritePointer (ch);
                auto* m = scratch.getReadPointer (juce::jmin (ch, scratch.getNumChannels() - 1));
                for (int n = 0; n < numSamples; ++n)
                {
                    float y = std::tanh (m[n] * mk) * 0.7f;
                    if (qLev > 0.0f)
                        y = std::round (y * qLev) / qLev;
                    w[n] = w[n] * (1.0f - megaA) + y * megaA;
                }
            }
        }
    }

    // ---- v2.0.0 エモ(ロングトーン検出) & サビリフト(サビ自動検出) ----
    // どちらも「検出だけ」をここで行い、後段の空間系(ひろがり・かさね・やまびこ・
    // ひびき)の送り量に係数として掛ける。音の経路そのものは一切変えないので、
    // 0%なら従来と完全に同じ音。遅延も増えない。
    float emoBloomNow = 0.0f, liftNow = 0.0f;
    {
        const float emoAmt  = apvts.getRawParameterValue ("emo_amt")->load()  * 0.01f;
        const float liftAmt = apvts.getRawParameterValue ("lift_amt")->load() * 0.01f;
        const float blockSec = (float) numSamples / (float) currentSampleRate;

        // ブロックRMS (処理後の歌声。空間系に入る直前のレベル)
        float sumSq = 0.0f;
        {
            const float* q = buffer.getReadPointer (0);
            for (int n = 0; n < numSamples; ++n) sumSq += q[n] * q[n];
        }
        const float rmsDb = 10.0f * std::log10 (juce::jmax (sumSq / (float) juce::jmax (1, numSamples), 1.0e-12f));

        if (emoAmt > 0.001f)
        {
            // 歌が -35dB より強いまま続いた時間を数える。0.35秒を超えたあたりから
            // 「ロングトーン」とみなして開き始め、1.2秒で全開。途切れたら素早く閉じる。
            if (rmsDb > -35.0f) emoHoldSec += blockSec;
            else                emoHoldSec  = 0.0f;
            const float t = juce::jlimit (0.0f, 1.0f, (emoHoldSec - 0.35f) / 0.85f);
            const float target = t * t * (3.0f - 2.0f * t) * emoAmt;        // smoothstep
            const float a = 1.0f - std::exp (-blockSec / (target > emoBloom ? 0.45f : 0.18f));
            emoBloom += (target - emoBloom) * a;
        }
        else { emoBloom = 0.0f; emoHoldSec = 0.0f; }
        emoBloomNow = emoBloom;

        if (liftAmt > 0.001f)
        {
            // 速い平均(1.2s)が遅い平均(8s)を約2.5dB上回る=サビ。ゆっくり持ち上げ、
            // Aメロに戻ったら少し早めに戻す。閾値付近のばたつきはsmoothstepで吸収。
            const float aF = 1.0f - std::exp (-blockSec / 1.2f);
            const float aS = 1.0f - std::exp (-blockSec / 8.0f);
            if (rmsDb > -55.0f)   // 無音は学習しない(曲間で基準が下がり切るのを防ぐ)
            {
                liftFastDb += (rmsDb - liftFastDb) * aF;
                liftSlowDb += (rmsDb - liftSlowDb) * aS;
            }
            const float over = liftFastDb - liftSlowDb - 1.0f;               // dB
            const float t = juce::jlimit (0.0f, 1.0f, over / 3.0f);
            const float target = t * t * (3.0f - 2.0f * t) * liftAmt;
            const float a = 1.0f - std::exp (-blockSec / (target > liftVal ? 1.5f : 0.6f));
            liftVal += (target - liftVal) * a;
        }
        else { liftVal = 0.0f; liftFastDb = liftSlowDb = -60.0f; }
        liftNow = liftVal;
        liftUI.store (liftNow);
    }

    // ---- Doubler (modulated short delay, mono-safe L/R inversion) + Width ----
    {
        const bool  dblOn    = apvts.getRawParameterValue ("dbl_on")->load() > 0.5f;
        const float dblAmt   = juce::jlimit (0.0f, 1.0f,
                                   (dblOn ? apvts.getRawParameterValue ("doubler")->load() * 0.01f : 0.0f)
                                   * (1.0f + 0.35f * liftNow));
        const float widthAmt = juce::jlimit (0.0f, 1.0f,
                                   apvts.getRawParameterValue ("width")->load() * 0.01f
                                   * (1.0f + 0.30f * emoBloomNow + 0.45f * liftNow)
                                   + 0.06f * emoBloomNow + 0.08f * liftNow);
        if (numCh >= 2 && (dblAmt > 0.001f || widthAmt > 0.001f))
        {
            auto* L = buffer.getWritePointer (0);
            auto* R = buffer.getWritePointer (1);
            const int sz = (int) dblBuf.size();
            const float baseDelay = 0.017f * (float) currentSampleRate;   // 17 ms
            const float lfoInc = juce::MathConstants<float>::twoPi * 0.7f / (float) currentSampleRate;

            for (int n = 0; n < numSamples; ++n)
            {
                const float mid = 0.5f * (L[n] + R[n]);
                dblBuf[(size_t) dblWrite] = mid;

                // modulated tap for the doubler voice
                dblLfoPhase += lfoInc;
                if (dblLfoPhase > juce::MathConstants<float>::twoPi)
                    dblLfoPhase -= juce::MathConstants<float>::twoPi;
                const float mod = std::sin (dblLfoPhase) * 0.004f * (float) currentSampleRate; // +-4ms
                float rpF = (float) dblWrite - (baseDelay + mod);
                while (rpF < 0) rpF += (float) sz;
                const int   i0 = (int) rpF % sz;
                const int   i1 = (i0 + 1) % sz;
                const float fr = rpF - std::floor (rpF);
                const float tap = dblBuf[(size_t) i0] * (1.0f - fr) + dblBuf[(size_t) i1] * fr;

                // fixed 12 ms tap for width decorrelation
                int wp = dblWrite - (int) (0.012f * (float) currentSampleRate);
                while (wp < 0) wp += sz;
                const float wtap = dblBuf[(size_t) wp];

                dblWrite = (dblWrite + 1) % sz;

                // v2.8.0: もとの左右差(side0)を捨てないようにした。以前は mid だけを
                // 使って書き戻していたので、ひろがり／かさねを少しでも上げた瞬間に
                // **それより前で作ったステレオが全部モノラルに潰れて**いた
                // (5人ユニゾンの左右の広がりが消えるのがいちばん分かりやすい)。
                const float side0 = 0.5f * (L[n] - R[n]);
                const float side  = dblAmt * 0.5f * tap + widthAmt * 0.8f * wtap;
                L[n] = mid + side0 + side;
                R[n] = mid - side0 - side;
            }
        }
    }

    // ---- v1.4.0 Chorus (wet-only voices; dry path untouched -> zero latency) ----
    {
        const bool  choOn  = apvts.getRawParameterValue ("cho_on")->load() > 0.5f;
        const float choAmt = apvts.getRawParameterValue ("cho_amt")->load() * 0.01f;
        if (choOn && choAmt > 0.001f)
        {
            juce::dsp::AudioBlock<float> cb (buffer);
            juce::dsp::ProcessContextReplacing<float> cc (cb);
            chorus.process (cc);
        }
    }

    // ---- v1.4.0 auto-duck detector: key = the finished vocal (before echoes) ----
    // Pro sidechain practice: fast engage, ~200 ms release so tails bloom in gaps.
    {
        if (duckGainBuf.size() < (size_t) numSamples)
            duckGainBuf.resize ((size_t) numSamples, 1.0f);

        const float duckAmt = apvts.getRawParameterValue ("duck")->load() * 0.01f;
        if (duckAmt > 0.001f)
        {
            const float thr      = juce::Decibels::decibelsToGain (-38.0f);
            const float duckedTo = juce::Decibels::decibelsToGain (-15.0f * duckAmt);
            const float* kL = buffer.getReadPointer (0);
            const float* kR = buffer.getReadPointer (juce::jmin (1, numCh - 1));
            for (int n = 0; n < numSamples; ++n)
            {
                const float pk = juce::jmax (std::abs (kL[n]), std::abs (kR[n]));
                duckEnv += (pk > duckEnv ? duckEnvAtk : duckEnvRel) * (pk - duckEnv);
                const float target = duckEnv > thr ? duckedTo : 1.0f;
                duckGain += (target < duckGain ? duckAtk : duckRel) * (target - duckGain);
                duckGainBuf[(size_t) n] = duckGain;
            }
        }
        else
        {
            duckGain = 1.0f;
            std::fill (duckGainBuf.begin(), duckGainBuf.begin() + numSamples, 1.0f);
        }
    }

    // ---- Delay (tempo-syncable echo, feedback highcut, ducked wet) ----
    {
        const bool  dlyOn = apvts.getRawParameterValue ("dly_on")->load() > 0.5f;
        const float dAmt  = juce::jlimit (0.0f, 1.0f,
                                (dlyOn ? apvts.getRawParameterValue ("delay")->load() * 0.01f : 0.0f)
                                * (1.0f + 0.25f * liftNow));   // v2.0.0 サビリフト
        if (dAmt > 0.001f && numCh >= 1)
        {
            // time: ms mode or note value from BPM (host BPM wins over manual)
            const int   sync = (int) apvts.getRawParameterValue ("dly_sync")->load();
            float timeSec;
            if (sync == 0)
                timeSec = apvts.getRawParameterValue ("dly_ms")->load() * 0.001f;
            else
            {
                float bpm = hostBpm.load();
                if (bpm < 20.0f) bpm = apvts.getRawParameterValue ("bpm")->load();
                static const float noteMult[7] = { 1.0f, 1.0f, 0.5f, 0.75f, 1.0f / 3.0f, 0.25f, 1.5f };
                timeSec = (60.0f / juce::jmax (20.0f, bpm)) * noteMult[juce::jlimit (0, 6, sync)];
            }
            timeSec = juce::jlimit (0.05f, 1.95f, timeSec);

            const int   sz     = (int) dlyBufL.size();
            const float fb     = juce::jlimit (0.0f, 0.9f, apvts.getRawParameterValue ("dly_fb")->load() * 0.01f);
            const float hcHz   = apvts.getRawParameterValue ("dly_hc")->load();
            const float hcCoef = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * hcHz / (float) currentSampleRate);
            const float slew   = 1.0f - std::exp (-1.0f / (0.050f * (float) currentSampleRate));

            auto* L = buffer.getWritePointer (0);
            auto* R = numCh > 1 ? buffer.getWritePointer (1) : nullptr;
            for (int n = 0; n < numSamples; ++n)
            {
                dlyTimeSm += slew * (timeSec - dlyTimeSm);   // tape-style glide, click-free
                float rpF = (float) dlyWrite - dlyTimeSm * (float) currentSampleRate;
                while (rpF < 0.0f) rpF += (float) sz;
                const int   i0 = (int) rpF % sz;
                const int   i1 = (i0 + 1) % sz;
                const float fr = rpF - std::floor (rpF);
                const float eL = dlyBufL[(size_t) i0] * (1.0f - fr) + dlyBufL[(size_t) i1] * fr;
                const float eR = dlyBufR[(size_t) i0] * (1.0f - fr) + dlyBufR[(size_t) i1] * fr;

                // feedback path highcut: repeats get darker and sink behind the vocal
                dlyLpL += hcCoef * (eL - dlyLpL);
                dlyLpR += hcCoef * (eR - dlyLpR);
                dlyBufL[(size_t) dlyWrite] = L[n] + dlyLpL * fb;
                dlyBufR[(size_t) dlyWrite] = (R ? R[n] : L[n]) + dlyLpR * fb;
                dlyWrite = (dlyWrite + 1) % sz;

                const float g = dAmt * 0.45f * duckGainBuf[(size_t) n];
                L[n] += eL * g;
                if (R) R[n] += eR * g;
            }
        }
    }

    // ---- Reverb (wet-only path: predelay -> tone filter -> duck -> add) ----
    {
        const bool  revOn  = apvts.getRawParameterValue ("revon")->load() > 0.5f;
        const float revMix = apvts.getRawParameterValue ("revmix")->load() * 0.01f;
        if (revOn && revMix > 0.001f)
        {
            revWet.makeCopyOf (buffer, true);
            const int rtype = (int) apvts.getRawParameterValue ("rev_type")->load();
            const float rsz = juce::jlimit (0.0f, 1.0f,
                                  apvts.getRawParameterValue ("revsize")->load() * 0.01f);

            // v1.6.0 SPRING: a short drip comb (~31 ms, LP in the loop) before the
            // tank gives the boingy flutter of a real spring pan. Zero latency.
            if (rtype == 5)
            {
                const int   sz = (int) springBufL.size();
                const int   D  = juce::jlimit (8, sz - 2, (int) (0.031 * currentSampleRate));
                const float fb = 0.40f + 0.25f * rsz;
                const float lpA = 1.0f - std::exp (-2.0f * juce::MathConstants<float>::pi
                                                   * 3000.0f / (float) currentSampleRate);
                auto* wL = revWet.getWritePointer (0);
                auto* wR = revWet.getWritePointer (juce::jmin (1, revWet.getNumChannels() - 1));
                for (int n = 0; n < numSamples; ++n)
                {
                    int rp = springW - D; while (rp < 0) rp += sz;
                    const float dL = springBufL[(size_t) rp];
                    const float dR = springBufR[(size_t) rp];
                    springLpL += lpA * (wL[n] + dL * fb - springLpL);
                    springLpR += lpA * (wR[n] + dR * fb - springLpR);
                    springBufL[(size_t) springW] = juce::jlimit (-1.5f, 1.5f, springLpL);
                    springBufR[(size_t) springW] = juce::jlimit (-1.5f, 1.5f, springLpR);
                    springW = (springW + 1) % sz;
                    wL[n] = wL[n] * 0.45f + dL * 0.95f;   // drips dominate the tank feed
                    wR[n] = wR[n] * 0.45f + dR * 0.95f;
                }
            }

            // v1.6.0 SHIMMER: last block's +1 oct wet re-enters the tank (block-
            // granular feedback loop; the dry signal path stays zero-latency).
            if (rtype == 6)
            {
                const float g = 0.16f + 0.16f * rsz;   // conservative: the RMS limiter
                                                       // below caps the loop anyway
                const int fbN = juce::jmin (numSamples, shimFb.getNumSamples());   // v2.8.0: 保険
                for (int ch = 0; ch < numCh; ++ch)
                    revWet.addFrom (ch, 0, shimFb,
                                    juce::jmin (ch, shimFb.getNumChannels() - 1), 0,
                                    fbN, g);
            }

            juce::dsp::AudioBlock<float> full (revWet);
            auto wb = full.getSubsetChannelBlock (0, (size_t) numCh).getSubBlock (0, (size_t) numSamples);
            juce::dsp::ProcessContextReplacing<float> wc (wb);
            reverb.process (wc);   // dryLevel = 0 -> revWet now holds the wet signal only

            // predelay per type (vocal practice: room 12 / plate 22 / hall 30 /
            // church 50 / spring 8 / shimmer 22 ms; normal keeps legacy = none)
            const int type = rtype;
            static const float preMs[7] = { 0.0f, 12.0f, 22.0f, 30.0f, 50.0f, 8.0f, 22.0f };
            const int preSamps = (int) (preMs[juce::jlimit (0, 6, type)] * 0.001f * currentSampleRate);
            if (preSamps > 0)
            {
                const int psz = (int) preBufL.size();
                auto* wL = revWet.getWritePointer (0);
                auto* wR = revWet.getWritePointer (juce::jmin (1, revWet.getNumChannels() - 1));
                for (int n = 0; n < numSamples; ++n)
                {
                    preBufL[(size_t) preWrite] = wL[n];
                    preBufR[(size_t) preWrite] = wR[n];
                    int rp = preWrite - preSamps; while (rp < 0) rp += psz;
                    wL[n] = preBufL[(size_t) rp];
                    if (numCh > 1) wR[n] = preBufR[(size_t) rp];
                    preWrite = (preWrite + 1) % psz;
                }
            }

            // tone filter on the tail (mud/harsh guard), then ducked add
            revHPF.process (wc);
            revLPF.process (wc);

            // v1.6.0 SHIMMER: build next block's feedback = +1 octave of the wet.
            // Classic dual-grain shifter: ring is read at 2x with two crossfaded
            // taps half a grain apart. Loop is conditioned by HP 250 / LP 6.5 kHz
            // and a hard clip, so it blooms without ever running away.
            if (rtype == 6)
            {
                const int   sz = (int) shimBufL.size();
                const float W  = (float) juce::jlimit (256, sz / 3, (int) (0.032 * currentSampleRate));
                const float lpA = 1.0f - std::exp (-2.0f * juce::MathConstants<float>::pi * 6500.0f / (float) currentSampleRate);
                const float hpA = 1.0f - std::exp (-2.0f * juce::MathConstants<float>::pi *  250.0f / (float) currentSampleRate);
                auto* rL = revWet.getReadPointer (0);
                auto* rR = revWet.getReadPointer (juce::jmin (1, revWet.getNumChannels() - 1));
                auto* fL = shimFb.getWritePointer (0);
                auto* fR = shimFb.getWritePointer (juce::jmin (1, shimFb.getNumChannels() - 1));
                const int shimN = juce::jmin (numSamples, shimFb.getNumSamples());   // v2.8.0: 配列外書き込みを防ぐ
                for (int n = 0; n < shimN; ++n)
                {
                    shimBufL[(size_t) shimW] = rL[n];
                    shimBufR[(size_t) shimW] = rR[n];

                    const float gp  = shimPhase;                       // 0..W
                    const float gp2 = gp + W * 0.5f >= W ? gp - W * 0.5f : gp + W * 0.5f;
                    const float a1  = 1.0f - std::abs (2.0f * gp  / W - 1.0f);
                    const float a2  = 1.0f - std::abs (2.0f * gp2 / W - 1.0f);
                    auto tap = [&] (float back, const std::vector<float>& buf)
                    {
                        float rp = (float) shimW - back;
                        while (rp < 0.0f) rp += (float) sz;
                        const int i0 = (int) rp, i1 = (i0 + 1) % sz;
                        const float fr = rp - (float) i0;
                        return buf[(size_t) i0] * (1.0f - fr) + buf[(size_t) i1] * fr;
                    };
                    const float sL = tap (2.0f * gp, shimBufL) * a1 + tap (2.0f * gp2, shimBufL) * a2;
                    const float sR = tap (2.0f * gp, shimBufR) * a1 + tap (2.0f * gp2, shimBufR) * a2;

                    shimHpL += hpA * (sL - shimHpL);
                    shimHpR += hpA * (sR - shimHpR);
                    shimLpL += lpA * ((sL - shimHpL) - shimLpL);
                    shimLpR += lpA * ((sR - shimHpR) - shimLpR);
                    fL[n] = juce::jlimit (-1.2f, 1.2f, shimLpL);
                    fR[n] = juce::jlimit (-1.2f, 1.2f, shimLpR);

                    shimW = (shimW + 1) % sz;
                    shimPhase += 1.0f; if (shimPhase >= W) shimPhase -= W;
                }

                // block-RMS limiter on the feedback: the loop can bloom but its
                // energy is hard-capped, so it can never run away over minutes.
                {
                    float sum = 0.0f;
                    for (int n = 0; n < numSamples; ++n)
                        sum += fL[n] * fL[n] + fR[n] * fR[n];
                    const float rms = std::sqrt (sum / (float) juce::jmax (1, numSamples * 2));
                    if (rms > 0.30f)
                    {
                        const float sc = 0.30f / rms;
                        for (int n = 0; n < numSamples; ++n) { fL[n] *= sc; fR[n] *= sc; }
                    }
                }
            }
            else if (shimFb.getNumSamples() > 0)
                shimFb.clear();   // other types: keep the loop silent

            // v2.0.0: エモ(ロングトーン)とサビリフトは、ここで響きの「量だけ」を
            // 増やす。テールの音色は同じなので、開いても閉じても違和感が出ない。
            const float revLift = 1.0f + 0.80f * emoBloomNow + 0.35f * liftNow;
            for (int ch = 0; ch < numCh; ++ch)
            {
                auto* w = buffer.getWritePointer (ch);
                auto* v = revWet.getReadPointer (juce::jmin (ch, revWet.getNumChannels() - 1));
                for (int n = 0; n < numSamples; ++n)
                    w[n] += v[n] * duckGainBuf[(size_t) n] * revLift;
            }
        }
    }

    // output meter (peak + smoothed RMS for the stream-loudness display)
    {
        float pk = 0.0f, sumSq = 0.0f;
        for (int ch = 0; ch < numCh; ++ch)
        {
            auto* q = buffer.getReadPointer (ch);
            for (int n = 0; n < numSamples; ++n)
            {
                const float a = std::abs (q[n]);
                pk = juce::jmax (pk, a);
                sumSq += a * a;
            }
        }
        const float cur = meterOut.load();
        meterOut.store (pk > cur ? pk : cur * 0.985f);

        const float ms = sumSq / (float) juce::jmax (1, numSamples * numCh);
        rmsAccum += 0.08f * (ms - rmsAccum);
        meterRmsDb.store (juce::jlimit (-60.0f, 0.0f,
                              10.0f * std::log10 (juce::jmax (rmsAccum, 1e-6f))));
    }

    // analyzer feed (post-processing mono mix, zero latency: read-only tap)
    {
        const float* ol = buffer.getReadPointer (0);
        const float* om = buffer.getReadPointer (juce::jmin (1, numCh - 1));
        int pos = analyzerPos.load (std::memory_order_relaxed);
        for (int n = 0; n < numSamples; ++n)
        {
            analyzerBuf[pos] = 0.5f * (ol[n] + om[n]);
            pos = (pos + 1) % analyzerSize;
        }
        analyzerPos.store (pos, std::memory_order_release);
    }

    // ---- v2.2.0 配信出力: 仕上がった音を配信用デバイスへも流す ----
    // FIFOに書くだけ(ロック・確保なし)。配信側が詰まっても本線には影響しない。
    if (streamOut.isRunning())
    {
        const float* L = buffer.getReadPointer (0);
        const float* R = numCh > 1 ? buffer.getReadPointer (1) : L;
        streamOut.push (L, R, numSamples);
    }

   #if VOCALGZZIO_TRIAL
    // ---- 体験版: 60秒ごとに0.6秒だけ音量を落とす ----
    // 音質はいつでも確認できるが、そのまま作品には使えない。
    // 無音やノイズではなく滑らかなディップにしてあるので、不具合と誤解されにくい。
    {
        const int   period = (int) (currentSampleRate * 60.0);   // 60 s
        const int   dipLen = (int) (currentSampleRate * 0.60);   // 0.6 s
        const int   fade   = (int) (currentSampleRate * 0.15);   // 出入り 0.15 s
        const float floorG = 0.12f;                              // -18 dB まで
        const int   n      = buffer.getNumSamples();
        const int   chans  = buffer.getNumChannels();

        for (int i = 0; i < n; ++i)
        {
            const int phase = trialCounter % period;
            float g = 1.0f;
            if (phase < dipLen)
            {
                if      (phase < fade)            g = 1.0f - (1.0f - floorG) * ((float) phase / (float) fade);
                else if (phase > dipLen - fade)   g = floorG + (1.0f - floorG) * ((float) (phase - (dipLen - fade)) / (float) fade);
                else                              g = floorG;
            }
            if (g < 1.0f)
                for (int ch = 0; ch < chans; ++ch)
                    buffer.getWritePointer (ch)[i] *= g;
            // v2.8.0: 折り返さないと int を使い切って(48kHzで約12.4時間)
            // phase が負になり、g が 1 を大きく超えて**爆音**になり得た。
            if (++trialCounter >= period) trialCounter = 0;
        }
    }
   #endif

}

//==============================================================================
// AUTO SETUP: turn the 5 s band-energy capture into clean-up EQ moves.
// Frequency targets follow standard vocal-EQ practice: HPF 80-120 Hz, mud
// 250-500 Hz, harsh 3-5 kHz, air 10 kHz+, de-ess 5-9 kHz. Everything here lands
// in the zero-latency core chain; nothing adds delay.
void VocalGzzioProcessor::applyAutoSetup()
{
    const int64_t n = asSampleCount.load();
    if (n < 4096)
        return;

    double e[asBands];
    double total = 1e-12;
    for (int b = 0; b < asBands; ++b) { e[b] = asBandSum[b].load() / (double) n; total += e[b]; }
    auto frac = [&] (int b) { return (float) (e[b] / total); };   // 0..1 share of energy

    const float rumble  = frac (0);           // <80 Hz
    const float bodyLow = frac (1);           // 80-250
    const float mud     = frac (2);           // 250-500
    const float midE    = frac (3);           // 500-2k
    const float pres    = frac (4);           // 2-5k
    const float sib     = frac (5);           // 5-9k
    const float airE    = frac (6);           // 9k+
    const float highSum = pres + sib + airE;

    auto setP = [this] (const juce::String& id, float v)
    {
        if (auto* prm = apvts.getParameter (id))
            prm->setValueNotifyingHost (apvts.getParameterRange (id).convertTo0to1 (v));
    };

    // ---- v1.5.0 SING mode: build a complete singing preset from the capture ----
    if (asMode.load() == 1)
    {
        const double n64   = (double) juce::jmax ((int64_t) 1, asSampleCount.load());
        const float  rmsDb = (float) (10.0 * std::log10 (asSumSq.load() / n64 + 1e-12));
        const float  pkDb  = juce::Decibels::gainToDecibels (juce::jmax (asPeak.load(), 1e-6f));
        const float  crest = pkDb - rmsDb;                       // transient-ness
        const int    bc    = juce::jmax (1, asBlockCount.load());
        const float  mDb   = (float) (asBlockDbSum.load() / bc);
        const float  varDb = (float) juce::jmax (0.0, asBlockDbSqSum.load() / bc - (double) mDb * mDb);
        const float  sdDb  = std::sqrt (varDb);                  // phrase-to-phrase dynamics
        const float  floorDb = asMinBlockDb.load();              // quietest moment ~ room noise

        // 1) level: aim the average at about -16 dBFS for streaming
        setP ("makeup", juce::jlimit (0.0f, 12.0f, -16.0f - rmsDb));

        // 2) compression scaled by crest factor and dynamics spread
        float c1 = crest > 15.0f ? 55.0f : crest > 11.0f ? 45.0f : 34.0f;
        float c2 = crest > 15.0f ? 40.0f : crest > 11.0f ? 34.0f : 26.0f;
        if (sdDb > 5.0f) c2 += 8.0f;                             // uneven phrases: more levelling
        setP ("comp1", juce::jlimit (0.0f, 70.0f, c1));
        setP ("comp2", juce::jlimit (0.0f, 70.0f, c2));
        setP ("attack", 8.0f);
        setP ("release", 140.0f);

        // 3) corrective + colour EQ from the spectral shares
        setP ("lowcut", juce::jlimit (70.0f, 120.0f, 80.0f + rumble * 400.0f));
        const float mudRatio = mud / juce::jmax (1e-4f, bodyLow + midE);
        setP ("mud",   juce::jlimit (0.0f, 8.0f, (mudRatio - 0.35f) * 22.0f));
        setP ("harsh", juce::jlimit (0.0f, 7.0f, (pres / juce::jmax (1e-4f, highSum) - 0.4f) * 20.0f));
        setP ("presence", juce::jlimit (0.5f, 4.0f, (0.30f - pres / juce::jmax (1e-4f, highSum)) * 12.0f + 1.5f));
        setP ("air",   juce::jlimit (1.0f, 6.0f, (0.28f - airE / juce::jmax (1e-4f, highSum)) * 22.0f + 1.0f));
        // the WARMTH knob is the "drive" parameter (soft saturation): thin voices
        // get more body, already-warm voices keep it light
        setP ("drive", bodyLow < 0.18f ? 28.0f : 12.0f);

        // 4) sibilance + noise
        setP ("deess", juce::jlimit (20.0f, 65.0f, (sib / juce::jmax (1e-4f, highSum)) * 140.0f));
        const float dn = floorDb > -50.0f ? 30.0f : floorDb > -62.0f ? 18.0f : 8.0f;
        setP ("denoise", dn);
        // v2.6.0 おまかせで2つの新機能も入れる。
        //  ジー音 … ハムが無ければ何もしない作りなので、常にONで安全
        //  ことば … 歌は歌詞が届いてこそ。控えめな 35% を既定に
        setP ("hum_amt",  100.0f);
        setP ("cons_amt",  35.0f);

        // 5) singing feel: sustain and a pleasant space
        setP ("sustain", 30.0f);
        setP ("width",   28.0f);
        setP ("doubler", 0.0f);      // かさねはデフォOFF (お好みで後から)
        setP ("delay",   0.0f);      // やまびこはデフォOFF
        setP ("revsize", 42.0f);
        setP ("revmix",  18.0f);
        setP ("mix",     100.0f);

        // result: 10..12 = sing done (tilt), +100 if LEARN is recommended
        const float tilt = highSum - (rumble + bodyLow + mud);
        int r = tilt > 0.15f ? 10 : tilt < -0.15f ? 11 : 12;
        if (dn >= 18.0f) r += 100;                               // noisy room: suggest LEARN
        autoSetupResult.store (r);
        return;
    }

    // low cut: more rumble -> higher HPF (bounded to the musical 70-120 Hz zone)
    setP ("lowcut", juce::jlimit (70.0f, 120.0f, 80.0f + rumble * 400.0f));

    // mud dip: only if 250-500 Hz dominates the low end
    const float mudRatio = mud / juce::jmax (1e-4f, bodyLow + midE);
    setP ("mud", juce::jlimit (0.0f, 8.0f, (mudRatio - 0.35f) * 22.0f));

    // harshness: strong presence share invites a gentle 3-5 kHz cut
    setP ("harsh", juce::jlimit (0.0f, 7.0f, (pres / juce::jmax (1e-4f, highSum) - 0.4f) * 20.0f));

    // de-esser: driven by sibilance share of the highs
    setP ("deess", juce::jlimit (0.0f, 70.0f, (sib / juce::jmax (1e-4f, highSum)) * 140.0f));

    // air: dull tops get a lift, bright/sibilant tops do not
    setP ("air", juce::jlimit (0.0f, 6.0f, (0.28f - airE / juce::jmax (1e-4f, highSum)) * 22.0f));

    // gentle noise floor cleanup based on the quietest capture level
    setP ("denoise", juce::jlimit (0.0f, 30.0f, asPeak.load() < 0.2f ? 25.0f : 10.0f));
    // v2.6.0 しゃべりでは「ことば」を歌より強めに(聞き取りやすさが最優先)
    setP ("hum_amt",  100.0f);
    setP ("cons_amt",  50.0f);

    // トーク配信は残響が聞き取りを妨げるため、ひびき・やまびこをデフォOFF
    setP ("revmix", 0.0f);
    setP ("delay",  0.0f);

    // pick the nearest mic-preset tilt (very rough: bright vs warm vs neutral)
    int nearest = -1;
    const float tilt = highSum - (rumble + bodyLow + mud);   // + = bright, - = warm
    if      (tilt >  0.15f) nearest = 0;   // bright/condenser-ish
    else if (tilt < -0.15f) nearest = 1;   // warm/dynamic-ish
    else                    nearest = 2;   // neutral
    autoSetupResult.store (nearest);
}

// v1.4.0 Krumhansl-Schmuckler key finding. Correlates the captured chroma with
// the 24 major/minor probe-tone profiles; the best Pearson r gives tonic+mode.
// v1.4.0 P5: runs the (heavy) autocorrelation + chroma binning over the captured
// audio ONCE, on the message thread, after the scan window has filled. This is the
// work that used to run inside processBlock; moving it here keeps the audio callback
// real-time safe (no multi-millisecond burst -> no crackle at small buffer sizes).
void VocalGzzioProcessor::finalizeKeyScanIfReady()
{
    if (! keyCaptureReady.load (std::memory_order_acquire) || keyAnalyzed)
        return;
    keyAnalyzed = true;

    const int total = juce::jmin ((int) keyCaptureBuf.size(), keyCaptureWrite.load());
    const double sr = currentSampleRate > 0.0 ? currentSampleRate : 48000.0;
    for (auto& c : chromaSum) c.store (0.0);
    if (total < 2048) return;

    // Decimate to ~12 kHz first (voice fundamentals sit well below its Nyquist).
    // This shrinks the autocorrelation ~16x so the whole scan is analysed in a few
    // tens of ms on the message thread -- no audio-thread burst, no UI stall.
    const int D = juce::jlimit (1, 8, (int) std::lround (sr / 12000.0));
    const double dsr = sr / D;
    const int dn = total / D;
    std::vector<float> dec ((size_t) dn, 0.0f);
    for (int i = 0; i < dn; ++i)
    {
        float acc = 0.0f;
        for (int k = 0; k < D; ++k) acc += keyCaptureBuf[(size_t) (i * D + k)];
        dec[(size_t) i] = acc / (float) D;               // boxcar decimation (cheap anti-alias)
    }

    const int win = juce::jmin (1024, dn);
    const int hop = win;                                  // no overlap (histogram needs coverage, not density)
    const int minLag = juce::jmax (2, (int) (dsr / 1200.0));   // up to 1200 Hz
    const int maxLag = juce::jmin (win - 1, (int) (dsr / 70.0)); // down to 70 Hz
    if (maxLag <= minLag + 1) return;

    for (int start = 0; start + win <= dn; start += hop)
    {
        const float* w = dec.data() + start;
        double e = 0.0;
        for (int i = 0; i < win; ++i) e += (double) w[i] * w[i];
        if (e <= win * 1e-5) continue;                    // silence gate

        double bestV = 0.0; int bestLag = 0;
        for (int lag = minLag; lag < maxLag; ++lag)
        {
            double s = 0.0;
            for (int i = 0; i + lag < win; ++i) s += (double) w[i] * w[i + lag];
            if (s > bestV) { bestV = s; bestLag = lag; }
        }
        if (bestLag > 0)
        {
            const double f = dsr / (double) bestLag;
            if (f >= 70.0 && f <= 1200.0)
            {
                const double midi = 69.0 + 12.0 * std::log2 (f / 440.0);
                const int    pc   = ((int) std::llround (midi) % 12 + 12) % 12;
                chromaSum[pc].store (chromaSum[pc].load() + std::sqrt (e));
            }
        }
    }
}

bool VocalGzzioProcessor::getKeyResult (int& tonic, bool& isMinor, float& confidence)
{
    finalizeKeyScanIfReady();   // message-thread: analyse the capture on first call

    double chroma[12];
    double total = 0.0;
    for (int i = 0; i < 12; ++i) { chroma[i] = chromaSum[i].load(); total += chroma[i]; }
    if (total < 1e-6) return false;

    static const double MAJ[12] = { 6.35,2.23,3.48,2.33,4.38,4.09,2.52,5.19,2.39,3.66,2.29,2.88 };
    static const double MIN[12] = { 6.33,2.68,3.52,5.38,2.60,3.53,2.54,4.75,3.98,2.69,3.34,3.17 };

    const double cMean = total / 12.0;
    auto corr = [&] (const double* prof, int shift)
    {
        double pMean = 0.0; for (int i = 0; i < 12; ++i) pMean += prof[i]; pMean /= 12.0;
        double num = 0.0, dc = 0.0, dp = 0.0;
        for (int i = 0; i < 12; ++i)
        {
            const double cv = chroma[(i + shift) % 12] - cMean;
            const double pv = prof[i] - pMean;
            num += cv * pv; dc += cv * cv; dp += pv * pv;
        }
        return (dc > 0.0 && dp > 0.0) ? num / std::sqrt (dc * dp) : -1.0;
    };

    double best = -2.0; int bestKey = 0; bool bestMinor = false;
    for (int k = 0; k < 12; ++k)
    {
        const double rMaj = corr (MAJ, k);
        if (rMaj > best) { best = rMaj; bestKey = k; bestMinor = false; }
        const double rMin = corr (MIN, k);
        if (rMin > best) { best = rMin; bestKey = k; bestMinor = true; }
    }
    tonic = bestKey; isMinor = bestMinor;
    confidence = (float) juce::jlimit (0.0, 1.0, best);
    return true;
}

// bands for resonances that stick out above the spectral average and ducks them
// only while they are prominent. Manual mode lets the user place up to 3 bands.
void VocalGzzioProcessor::processSmartEQ (juce::AudioBuffer<float>& buffer)
{
    if (apvts.getRawParameterValue ("seq_on")->load() < 0.5f)
    {
        // relax gently when switched off
        for (int b = 0; b < seqBands; ++b)
        {
            seqCut[b] *= 0.9f; seqTarget[b] = 0.0f;
            seqCutUI[b].store (seqCut[b]);
        }
        meterSEQ.store (meterSEQ.load() * 0.9f);
        return;
    }

    const double sr    = currentSampleRate;
    const int    numCh = buffer.getNumChannels();
    const int    numSamples = buffer.getNumSamples();
    const int    mode  = (int) apvts.getRawParameterValue ("seq_mode")->load();   // 0 auto, 1 manual

    // ---- band configuration for this block ----
    int   N = seqBands;
    float wantF[seqBands], maxCut[seqBands], tiltDb[seqBands], qArr[seqBands];
    float Q, threshOff, ratio;

    auto pval = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };

    if (mode == 0)   // AUTO
    {
        N = seqBands;
        const float autoF[seqBands] = { 315.f, 630.f, 1250.f, 2500.f, 5000.f, 8000.f };
        const float amount = juce::jlimit (0.0f, 1.0f, pval ("seq_amount") * 0.01f);
        const float focus  = juce::jlimit (0.0f, 1.0f, pval ("seq_focus")  * 0.01f);
        Q         = 1.0f + focus * 3.0f;          // wider .. narrower
        threshOff = 9.0f - focus * 8.0f;          // easier to trigger at high focus
        ratio     = 0.7f;
        for (int b = 0; b < N; ++b)
        {
            wantF[b]  = autoF[b];
            maxCut[b] = amount * 12.0f;           // up to 12 dB
            tiltDb[b] = std::log2 (autoF[b] / 1500.0f) * 1.5f;   // highs trigger a bit more
            qArr[b]   = Q;
        }
    }
    else             // MANUAL (3 bands, F6-style: per-band freq / depth / Q)
    {
        N = 3;
        wantF[0]  = juce::jlimit (120.0f, 8000.0f, pval ("seq_f1"));
        wantF[1]  = juce::jlimit (120.0f, 8000.0f, pval ("seq_f2"));
        wantF[2]  = juce::jlimit (120.0f, 8000.0f, pval ("seq_f3"));
        maxCut[0] = pval ("seq_d1"); maxCut[1] = pval ("seq_d2"); maxCut[2] = pval ("seq_d3");
        qArr[0]   = juce::jlimit (0.5f, 8.0f, pval ("seq_q1"));
        qArr[1]   = juce::jlimit (0.5f, 8.0f, pval ("seq_q2"));
        qArr[2]   = juce::jlimit (0.5f, 8.0f, pval ("seq_q3"));
        Q = qArr[0]; threshOff = 4.0f; ratio = 0.8f;
        for (int b = 0; b < N; ++b) tiltDb[b] = 0.0f;
    }

    // rebuild detector coeff only when a band's centre frequency changed
    for (int b = 0; b < N; ++b)
        if (std::abs (wantF[b] - seqFreqHz[b]) > 0.5f)
        {
            seqFreqHz[b] = wantF[b];
            *seqDet[b].coefficients = ACoefs::makeBandPass (sr, seqFreqHz[b], 1.2f);   // v2.8.0: 確保なし
        }

    // ---- sample loop ----
    float* L = buffer.getWritePointer (0);
    float* R = numCh > 1 ? buffer.getWritePointer (1) : nullptr;

    int idx = 0;
    for (int n = 0; n < numSamples; ++n)
    {
        const float det = R ? 0.5f * (L[n] + R[n]) : L[n];

        for (int b = 0; b < N; ++b)
        {
            const float d  = seqDet[b].processSample (det);
            const float ad = std::abs (d);
            seqEnv[b] += (ad > seqEnv[b] ? seqEnvAtk : seqEnvRel) * (ad - seqEnv[b]);
        }

        if ((idx & 31) == 0)
        {
            // spectral average of the active bands (dB)
            float sumDb = 0.0f, lvl[seqBands];
            for (int b = 0; b < N; ++b) { lvl[b] = 20.0f * std::log10 (juce::jmax (seqEnv[b], 1e-6f)); sumDb += lvl[b]; }
            const float avgDb = sumDb / (float) N;

            float maxc = 0.0f;
            for (int b = 0; b < N; ++b)
            {
                const float eff    = lvl[b] + tiltDb[b];
                const float excess = eff - avgDb - threshOff;
                seqTarget[b] = excess > 0.0f ? juce::jmin (excess * ratio, maxCut[b]) : 0.0f;

                // rebuild the peak coeff when the applied gain or Q moved enough
                if (std::abs (seqCut[b] - seqApplied[b]) > 0.05f
                     || std::abs (qArr[b] - seqAppliedQ[b]) > 0.02f)
                {
                    // v2.8.0: 32サンプルごとに new していた。確保なしの形へ。
                    const auto co = ACoefs::makePeakFilter (sr, seqFreqHz[b], qArr[b],
                                        juce::Decibels::decibelsToGain (-seqCut[b]));
                    *seqPeakL[b].coefficients = co;
                    *seqPeakR[b].coefficients = co;
                    seqApplied[b]  = seqCut[b];
                    seqAppliedQ[b] = qArr[b];
                }
                maxc = juce::jmax (maxc, seqCut[b]);
            }
            meterSEQ.store (juce::jlimit (0.0f, 1.0f, maxc / 12.0f));
        }

        // smooth each cut toward its target (fast down-to-cut, slower release)
        for (int b = 0; b < N; ++b)
        {
            const float df = seqTarget[b] - seqCut[b];
            seqCut[b] += (df > 0.0f ? seqEnvAtk : seqEnvRel) * df;
        }

        // apply the peak filters in cascade
        float xL = L[n], xR = R ? R[n] : 0.0f;
        for (int b = 0; b < N; ++b)
        {
            xL = seqPeakL[b].processSample (xL);
            if (R) xR = seqPeakR[b].processSample (xR);
        }
        // guard against denormals / non-finite
        if (! std::isfinite (xL)) xL = 0.0f;
        L[n] = xL;
        if (R) { if (! std::isfinite (xR)) xR = 0.0f; R[n] = xR; }

        ++idx;
    }

    // mirror band state for the UI graph (display only, once per block)
    seqBandsUI.store (N);
    seqQUI.store (Q);
    for (int b = 0; b < seqBands; ++b)
    {
        seqCutUI [b].store (b < N ? seqCut[b] : 0.0f);
        seqFreqUI[b].store (seqFreqHz[b]);
        seqQBandUI[b].store (b < N ? qArr[b] : Q);
    }
}

//==============================================================================
void VocalGzzioProcessor::readTunerBuffer (std::vector<float>& dest) const
{
    dest.resize (tunerSize);
    const int pos = tunerPos.load (std::memory_order_acquire);
    for (int i = 0; i < tunerSize; ++i)
        dest[(size_t) i] = tunerBuf[(pos + i) % tunerSize];
}

//==============================================================================
void VocalGzzioProcessor::readAnalyzerBuffer (std::vector<float>& dest) const
{
    dest.resize (analyzerSize);
    const int pos = analyzerPos.load (std::memory_order_acquire);
    for (int i = 0; i < analyzerSize; ++i)
        dest[(size_t) i] = analyzerBuf[(pos + i) % analyzerSize];
}

//==============================================================================
juce::AudioProcessorEditor* VocalGzzioProcessor::createEditor()
{
    return new VocalGzzioEditor (*this);
}

// ---- v1.5.0 state: params + learned denoise profile in one XML ----
//==============================================================================
// v2.1.0 MIDIスイッチ
// オーディオスレッド側: MIDIを見て「学習」か「実行待ちフラグ」だけを立てる。
// ロック・確保・ホスト通知は一切しない。実際の切替は handleAsyncUpdate で。
void VocalGzzioProcessor::processMidiSwitches (juce::MidiBuffer& midi)
{
    if (midi.getNumEvents() == 0) return;

    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        int type = 0, num = -1, ch = 0; bool press = false, release = false;

        if (m.isNoteOn())            { type = 1; num = m.getNoteNumber();      ch = m.getChannel(); press = true; }
        else if (m.isNoteOff())      { type = 1; num = m.getNoteNumber();      ch = m.getChannel(); release = true; }
        else if (m.isController())   { type = 2; num = m.getControllerNumber(); ch = m.getChannel();
                                       press   = m.getControllerValue() >= 64;
                                       release = m.getControllerValue() <  64; }
        else continue;

        // --- 学習モード: 次に来た操作(NoteOn/CCの押し込み)を割り当てる ---
        const int armed = midiLearnArmed.load();
        if (armed >= 0 && armed < kMidiSlots && press)
        {
            midiMap[armed].type.store (type);
            midiMap[armed].num .store (num);
            midiMap[armed].ch  .store (ch);
            midiCcOn[armed] = true;              // 割当直後の同じ押下では発火させない
            midiLearnArmed.store (-1);
            midiUiDirty.fetch_add (1);
            markStateDirty();                    // 割当は自動保存にも載せる
            continue;
        }

        // --- 割当と照合(チャンネル不問)。押した瞬間だけ発火(エッジ検出) ---
        for (int s = 0; s < kMidiSlots; ++s)
        {
            if (midiMap[s].type.load() != type || midiMap[s].num.load() != num) continue;
            if (press && ! midiCcOn[s])
            {
                midiCcOn[s] = true;
                midiPending.fetch_or (1u << s);
                triggerAsyncUpdate();
            }
            else if (release)
                midiCcOn[s] = false;
        }
    }
}

// メッセージスレッド側: フラグの立ったスロットのアクションを安全に実行する
void VocalGzzioProcessor::handleAsyncUpdate()
{
    const juce::uint32 pend = midiPending.exchange (0);
    if (pend == 0) return;

    auto toggleBool = [this] (const char* id)
    {
        if (auto* p = apvts.getParameter (id))
            p->setValueNotifyingHost (p->getValue() > 0.5f ? 0.0f : 1.0f);
    };
    auto cycleChoice = [this] (const char* id, int count, int step)
    {
        if (auto* p = apvts.getParameter (id))
        {
            const int cur  = (int) apvts.getRawParameterValue (id)->load();
            const int next = ((cur + step) % count + count) % count;
            p->setValueNotifyingHost (apvts.getParameterRange (id).convertTo0to1 ((float) next));
        }
    };

    for (int s = 0; s < kMidiSlots; ++s)
    {
        if ((pend & (1u << s)) == 0) continue;
        switch (midiMap[s].act.load())
        {
            case maAB:      abSwitch (1 - abCurrent.load());          break;
            case maRevOn:   toggleBool ("revon");                     break;
            case maRevType: cycleChoice ("rev_type", 7, +1);          break;
            case maJnOn:    toggleBool ("jn_on");                     break;
            case maJnSolo:  toggleBool ("jn_solo");                   break;
            case maJnHarm:  cycleChoice ("jn_harm", 9, +1);           break;
            case maAtOn:    toggleBool ("at_on");                     break;
            case maVcOn:    toggleBool ("vc_on");                     break;
            case maDlyOn:   toggleBool ("dly_on");                    break;
            case maKeyUp:   cycleChoice ("at_key", 12, +1);           break;
            case maKeyDown: cycleChoice ("at_key", 12, -1);           break;
            default: break;
        }
    }
}

// v2.1.0 A/B(プロセッサ所有): 今の全状態を現スロットへ保存してから相手を読む。
// エディタが閉じていてもMIDIで切り替えられる。空のスロットへは「音を変えない」。
void VocalGzzioProcessor::abSwitch (int target)
{
    target = juce::jlimit (0, 1, target);
    const int cur = abCurrent.load();
    if (target == cur) return;

    omitAbInState = true;                  // v2.8.0: 入れ子を防ぐ
    getStateInformation (abSlot[cur]);
    omitAbInState = false;

    // MIDI割当は「機材の設定」なのでA/Bでは入れ替えない(古い割当の復活を防ぐ)
    int keep[kMidiSlots][4];
    for (int s = 0; s < kMidiSlots; ++s)
    {
        keep[s][0] = midiMap[s].act.load();  keep[s][1] = midiMap[s].type.load();
        keep[s][2] = midiMap[s].num.load();  keep[s][3] = midiMap[s].ch.load();
    }

    auto& t = abSlot[target];
    suppressDeviceRestore = true;          // 配信先(機材設定)は持ち越す
    if (t.getSize() > 0)
        setStateInformation (t.getData(), (int) t.getSize());
    suppressDeviceRestore = false;

    for (int s = 0; s < kMidiSlots; ++s)
    {
        midiMap[s].act.store (keep[s][0]);  midiMap[s].type.store (keep[s][1]);
        midiMap[s].num.store (keep[s][2]);  midiMap[s].ch.store (keep[s][3]);
    }

    abCurrent.store (target);
    abUiDirty.fetch_add (1);
}

void VocalGzzioProcessor::abCopyToOther()
{
    omitAbInState = true;                  // v2.8.0: 入れ子を防ぐ
    getStateInformation (abSlot[abCurrent.load() == 0 ? 1 : 0]);
    omitAbInState = false;
    abUiDirty.fetch_add (1);
}

std::unique_ptr<juce::XmlElement> VocalGzzioProcessor::makeStateXml()
{
    auto root = std::make_unique<juce::XmlElement> ("VOCALGZZIO");
    root->setAttribute ("ver", 2);
    if (auto params = apvts.copyState().createXml())
        root->addChildElement (params.release());
    auto* d = root->createNewChildElement ("DENOISE");
    d->setAttribute ("learned", dnLearnedShared.load());
    for (int b = 0; b < 4; ++b)
        d->setAttribute ("f" + juce::String (b), (double) dnFloorShared[b].load());

    // v2.2.0 配信出力の設定(単体起動版の機材設定。プラグイン版では使わない)
    auto* so = root->createNewChildElement ("STREAMOUT");
    so->setAttribute ("on", streamWanted ? 1 : 0);
    so->setAttribute ("dev", streamDevWanted);

    // v2.8.0 ★A/Bのもう片方を保存する。
    // これまで abSlot[] はメモリの中だけにあり、保存していなかった。そのため
    // 「Aで作る → A→Bで写す → Aを詰める → 保存して開き直す」と **Bが空** に
    // なり、Bを押しても(空スロットは音を変えない仕様なので)無反応だった。
    // 作り比べた片方が消えるのは実作業でいちばん困るので、載せることにした。
    if (! omitAbInState)
    {
        auto* ab = root->createNewChildElement ("AB");
        ab->setAttribute ("cur", abCurrent.load());
        for (int s = 0; s < 2; ++s)
            if (abSlot[s].getSize() > 0)
                ab->setAttribute ("s" + juce::String (s), abSlot[s].toBase64Encoding());
    }

    // v2.1.0 MIDIスイッチの割当(プロジェクト・自動保存の両方に載る)
    auto* mm = root->createNewChildElement ("MIDIMAP");
    for (int s = 0; s < kMidiSlots; ++s)
    {
        auto* e = mm->createNewChildElement ("SLOT");
        e->setAttribute ("act",  midiMap[s].act.load());
        e->setAttribute ("type", midiMap[s].type.load());
        e->setAttribute ("num",  midiMap[s].num.load());
        e->setAttribute ("ch",   midiMap[s].ch.load());
    }
    return root;
}

void VocalGzzioProcessor::applyStateXml (const juce::XmlElement& xml)
{
    restoringState = true;
    auto swapParams = [this] (const juce::XmlElement& p)
    {
        auto old = apvts.state;
        old.removeListener (this);
        apvts.replaceState (juce::ValueTree::fromXml (p));
        apvts.state.addListener (this);
    };

    if (xml.hasTagName ("VOCALGZZIO"))
    {
        if (auto* p = xml.getChildByName (apvts.state.getType()))
            swapParams (*p);
        if (auto* d = xml.getChildByName ("DENOISE"))
        {
            dnLearnedShared.store (d->getBoolAttribute ("learned", false));
            for (int b = 0; b < 4; ++b)
                dnFloorShared[b].store ((float) d->getDoubleAttribute ("f" + juce::String (b), 1e-5));
            dnProfilePending.store (true, std::memory_order_release);
        }
        // v2.8.0: A/Bのもう片方を戻す。A/B切替そのものによる復元では触らない
        // (切替中に自分のスロットを上書きしてしまうため)。
        if (auto* ab = xml.getChildByName ("AB"); ab != nullptr && ! suppressDeviceRestore)
        {
            for (int s = 0; s < 2; ++s)
            {
                abSlot[s].reset();
                const auto b64 = ab->getStringAttribute ("s" + juce::String (s));
                if (b64.isNotEmpty()) abSlot[s].fromBase64Encoding (b64);
            }
            abCurrent.store (juce::jlimit (0, 1, ab->getIntAttribute ("cur", 0)));
            abUiDirty.fetch_add (1);
        }

        // v2.2.0: 配信先は「機材の設定」。A/B切替のたびに開き直すと音が途切れる
        // ので、A/B経由の復元では触らない(MIDI割当と同じ扱い)。
        if (auto* so = xml.getChildByName ("STREAMOUT"); so != nullptr && ! suppressDeviceRestore)
        {
            streamDevWanted = so->getStringAttribute ("dev");
            streamWanted    = so->getIntAttribute ("on", 0) != 0;
            // 単体起動版のときだけ、保存されていた配信先を開き直す
            if (isStandalone())
            {
                if (streamWanted && streamDevWanted.isNotEmpty())
                    streamOut.start (streamDevWanted, currentSampleRate);
                else
                    streamOut.stop();
            }
        }
        if (auto* mm = xml.getChildByName ("MIDIMAP"))   // v2.1.0
        {
            int s = 0;
            for (auto* e : mm->getChildIterator())
            {
                if (s >= kMidiSlots) break;
                midiMap[s].act .store (e->getIntAttribute ("act", 0));
                midiMap[s].type.store (e->getIntAttribute ("type", 0));
                midiMap[s].num .store (e->getIntAttribute ("num", -1));
                midiMap[s].ch  .store (e->getIntAttribute ("ch", 0));
                ++s;
            }
            midiUiDirty.fetch_add (1);
        }
    }
    else if (xml.hasTagName (apvts.state.getType()))   // legacy v1.4.x chunk
    {
        swapParams (xml);
    }
    restoringState = false;
}

juce::File VocalGzzioProcessor::autosaveFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("VocalGzzio").getChildFile ("autosave.xml");
}

void VocalGzzioProcessor::flushAutosaveNow()
{
    auto f = autosaveFile();
    f.getParentDirectory().createDirectory();
    if (auto xml = makeStateXml())
        xml->writeTo (f, {});
    stateDirty.store (false);
}

void VocalGzzioProcessor::timerCallback()
{
    if (stateDirty.load()
        && juce::Time::getMillisecondCounter() - lastDirtyMs.load() > 1200)
        flushAutosaveNow();
}

void VocalGzzioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = makeStateXml())
        copyXmlToBinary (*xml, destData);
}

void VocalGzzioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        applyStateXml (*xml);
        markStateDirty();   // keep the autosave in sync with the restored project
    }
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new VocalGzzioProcessor();
}
