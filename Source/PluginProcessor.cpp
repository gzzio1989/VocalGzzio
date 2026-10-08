#include <cmath>
#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
// v4.0.0 版(エディション)の目印
// ------------------------------------------------------------------
// 製品版(2,500円)と体験版(無料)を取り違えて配ると売上が消える。
// 人の目や zip の名前に頼らず、**出来上がったバイナリそのもの**から
// 機械で判別できるように、合い言葉を埋めておく。
// tools/pack_booth.py がこの文字列を読んで、袋詰めの直前に検査する。
// Macでは最適化と未使用セクション除去の両方を止める必要がある。
// volatile ポインターを宣言するだけでは、そのポインターごと消える。
extern "C"
   #if defined(__APPLE__) && defined(__clang__)
    __attribute__((used))
   #endif
const char VocalGzzioEditionMarker[] =
   #if VOCALGZZIO_TRIAL
    "VOCALGZZIO-EDITION:TRIAL";
   #elif VOCALGZZIO_LITE
    "VOCALGZZIO-EDITION:LITE";
   #else
    "VOCALGZZIO-EDITION:PRODUCT";
   #endif

// リンカの未使用データ削除(/OPT:REF)に消されないよう、volatile 経由で握っておく。
static const char* volatile gVocalGzzioEditionKeep = VocalGzzioEditionMarker;

//==============================================================================
VocalGzzioProcessor::VocalGzzioProcessor()
    : AudioProcessor (BusesProperties()
        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
        .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    // v1.5.0 autosave: restore the last-used settings even when the host never
    // hands us a saved project state (fresh insert, unsaved project, standalone).
    // A host-provided setStateInformation later simply overwrites this.
   #if ! VOCALGZZIO_TRIAL   // 体験版は前回の設定を覚えない（保存しないので読む物も作らない）
    if (auto f = autosaveFile(); f.existsAsFile())
        if (auto xml = juce::XmlDocument::parse (f))
            applyStateXml (*xml);
   #endif

    apvts.state.addListener (this);
    stateDirty.store (false);
    usageLog.load();    // v2.10.0 使われ方（手元に貯めるだけ・送信しない）
    startTimer (500);   // autosave poll: writes ~1.2 s after the last change
}

VocalGzzioProcessor::~VocalGzzioProcessor()
{
    stopTimer();
    cancelPendingUpdate();          // v2.1.0: 実行待ちのMIDI切替を破棄
   #if ! VOCALGZZIO_TRIAL          // 体験版は閉じるときも autosave を書かない
    if (stateDirty.load())
        flushAutosaveNow();
   #endif
    usageLog.save();                // v2.10.0 使われ方を手元へ（送信はしない）
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
    // v3.1「ピックおさえ」(アコギだけ・設計書§3の追加ノブ)。
    //  既定 0 = 何もしない。既存のアコギの曲もビット単位で同じ音のまま開く。
    layout.add (std::make_unique<P>(pid ("pick_amt"), "Pick Tamer", R (0.0f, 100.0f, 1.0f), 0.0f, unit ("%")));
    layout.add (std::make_unique<P>(pid ("in_gain"),  "Mic Volume", R (-24.0f, 24.0f, 0.1f),    0.0f, unit ("dB"))); // v2.4.0 マイク音量(入力トリム)
    layout.add (std::make_unique<P>(pid ("ride_amt"), "Volume Keep (Rider)", R (0.0f, 100.0f, 1.0f), 0.0f, unit ("%"))); // v2.4.0 音量キープ
    // v2.6.0 ジー音(電源ハム)の自動除去 / ことば(子音エンハンサー)。初期値0% = 従来と同じ音
    layout.add (std::make_unique<P>(pid ("hum_amt"),  "Hum Removal",  R (0.0f, 100.0f, 1.0f), 0.0f, unit ("%")));
    layout.add (std::make_unique<P>(pid ("cons_amt"), "Consonant",    R (0.0f, 100.0f, 1.0f), 0.0f, unit ("%")));
    // v2.10.0 距離ならし（近接効果の自動補正）。初期値0% = 従来と完全に同じ音。
    // 「その人のいつもの距離」を40秒かけて覚え、そこからのずれだけを打ち消す。
    // 入力のいちばん手前に置く＝マイク音量の直後、原音コピーより前。
    layout.add (std::make_unique<P>(pid ("prox_amt"), "Proximity Evener", R (0.0f, 100.0f, 1.0f), 0.0f, unit ("%")));
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
    // ---- v2.10.0 #77/#78 音源モード ----
    //  プリセットではない。**同じツマミが見る場所を持ち替える**。
    //  「こもり」「かたさ」「ぬけ」「きらめき」の中心も、ディエッサーが探す帯域も、
    //  なめらかが見張る範囲も、音源ごとに正しい場所が違う。歌の場所のまま
    //  アコギに使っても、ツマミは効いているのに狙いが外れる。
    //  ★うた(0)は既定。うたの数値は今までと1つも変えていないので、
    //    既存のプロジェクトは音が変わらない（dsp_srcmode でビット比較して確認）。
    //  ★v3.1「使いかた4種」: 弾き語りを**末尾に足した**。番号は 0=うた 1=アコギだけ
    //    2=しゃべり 3=弾き語り。**並べ替えていない**のがここの肝で、APVTS は選択肢を
    //    番号(非正規化値)で保存するため、末尾に足すぶんには古いプロジェクトが
    //    そのまま開く。1 の表示名だけ「アコギ」→「アコギだけ」に変えたが、
    //    保存されるのは番号なので、これも古い曲に影響しない。
    layout.add (std::make_unique<juce::AudioParameterChoice>(pid ("src_mode"), "Source Mode",
                    juce::StringArray { juce::String::fromUTF8 ("\xe3\x81\x86\xe3\x81\x9f"),                    // うた
                                        juce::String::fromUTF8 ("\xe3\x82\xa2\xe3\x82\xb3\xe3\x82\xae\xe3\x81\xa0\xe3\x81\x91"),  // アコギだけ
                                        juce::String::fromUTF8 ("\xe3\x81\x97\xe3\x82\x83\xe3\x81\xb9\xe3\x82\x8a"),              // しゃべり
                                        juce::String::fromUTF8 ("\xe5\xa3\xb0\xe3\x81\xa8\xe3\x82\xae\xe3\x82\xbf\xe3\x83\xbc") }, 0));   // 声とギター(弾き語り用)

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
    // v3.0 自動学びなおし。しゃべっていない間に部屋のノイズを測り直す。
    //  既定 OFF（既存プロジェクトの音は変わらない）。トーク自動が ON にする。
    // v4.0.0 ★既定を ON にした（2026-08-31 の実測にもとづく）。
    //  tools/dsp_dnslow で 30分ぶんを実際に流して比べた:
    //   ・部屋が静かなまま  … ON -96.28 / OFF -96.22 dBFS ＝ ちがいなし
    //   ・部屋が30分で+10dB … ON +9.6dB(部屋どおり) / OFF +18.2dB(効かなくなる)
    //  OFF のままだと、学習した床が凍っているので、PCのファンやエアコンで
    //  部屋が上がるとゲートが閉じきれず、喋りはじめにサーッと出る。
    //  静かな部屋では音が変わらないので、入れない理由が無い。
    layout.add (std::make_unique<B>(pid ("dn_relearn"), "De-Noise Auto Relearn", true));
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
                            juce::String::fromUTF8 ("\xe3\x82\xb7\xe3\x83\x9e\xe3\x83\xbc"),
                            // v4.0.0 へや: ここから先は物理で作った本物の部屋の畳み込み。
                            // 7 以上 = heya::Convolver を通す（従来の番号は動かさない）。
                            juce::String::fromUTF8 ("\xe3\x81\xb8\xe3\x82\x84\xef\xbc\x9a\xe3\x81\x8a\xe3\x81\xb5\xe3\x82\x8d"),
                            juce::String::fromUTF8 ("\xe3\x81\xb8\xe3\x82\x84\xef\xbc\x9a\xe3\x82\xab\xe3\x83\xa9\xe3\x82\xaa\xe3\x82\xb1\xe7\xae\xb1"),
                            juce::String::fromUTF8 ("\xe3\x81\xb8\xe3\x82\x84\xef\xbc\x9a\xe9\x8c\xb2\xe9\x9f\xb3\xe3\x82\xb9\xe3\x82\xbf\xe3\x82\xb8\xe3\x82\xaa"),
                            juce::String::fromUTF8 ("\xe3\x81\xb8\xe3\x82\x84\xef\xbc\x9a\xe3\x83\xa9\xe3\x82\xa4\xe3\x83\x96\xe3\x83\x8f\xe3\x82\xa6\xe3\x82\xb9"),
                            juce::String::fromUTF8 ("\xe3\x81\xb8\xe3\x82\x84\xef\xbc\x9a\xe3\x82\xb3\xe3\x83\xb3\xe3\x82\xb5\xe3\x83\xbc\xe3\x83\x88\xe3\x83\x9b\xe3\x83\xbc\xe3\x83\xab"),
                            juce::String::fromUTF8 ("\xe3\x81\xb8\xe3\x82\x84\xef\xbc\x9a\xe3\x83\x97\xe3\x83\xac\xe3\x83\xbc\xe3\x83\x88") }, 0));

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

    // ---- v3.0「つぶさない」（音量が上がっても潰れないモード）------------------
    // 既定は OFF。ONにしたときだけ音が変わる（上げただけでは今までどおり）。
    layout.add (std::make_unique<juce::AudioParameterBool>(
        pid ("crush_on"), "Crush Guard", false));

    // ---- v3.0 モジュールのON/OFF（8個）----------------------------------------
    // 「De-noiseだけ使いたいのに他がかかる」への答え。**既定はすべてON**なので、
    // 上げただけでは音は一切変わらない。切ったところだけ素通しになる。
    // 自動化にも載る（ホストのオートメーションで曲中に切り替えられる）。
    for (int m = 0; m < gz::ModuleChain::Count; ++m)
        layout.add (std::make_unique<juce::AudioParameterBool>(
            pid (gz::ModuleChain::paramId (m)), gz::ModuleChain::paramName (m), true));

    // ---- v3.0-c 順番の分岐（案C）------------------------------------------------
    // 「起動順を選べるように」への答え。ただし8つを自由に並べ替えるのではなく、
    // **実際に要望が出る分岐だけ**を用意する。
    //
    //  なぜ自由な並べ替えにしないか:
    //   8つのうち3つ（おそうじ・音量そろえ・音色づくり）は、いまのコードで
    //   1か所にまとまっていない。しかもそれぞれ理由があって分かれている
    //   （例: 音量キープがサ行おさえの後にあるのは、サ行を削った後の音量で
    //    合わせないと「サ行のせいで音量が下がる」動きになるから）。
    //   1か所にまとめた瞬間、**いま保存してある設定の音が全部変わる**。
    //   それは受け入れられないので、分岐だけを足す形にした。
    //
    //  ★どれも既定は false ＝ いままでと**1サンプルも変わらない**。
    //    選んだ人だけ順番が変わる。（dsp_order がそれを毎回確かめている）
    layout.add (std::make_unique<juce::AudioParameterBool>(
        pid ("ord_deess"), "Order: De-Ess Before Comp", false));
    layout.add (std::make_unique<juce::AudioParameterBool>(
        pid ("ord_eq"),    "Order: Shape After Comp",   false));
    layout.add (std::make_unique<juce::AudioParameterBool>(
        pid ("ord_space"), "Order: Space Before Character", false));

    // v3.0-c 互換スイッチ。
    //  ON にすると、サビリフト/エモの検出を**キャラ声がONのときだけ**行う
    //  v2.12.0 以前の動きに戻る。既定は OFF（＝直った動き）だが、
    //  古いプロジェクトを開いたときは自動で ON になる（setStateInformation）。
    layout.add (std::make_unique<juce::AudioParameterBool>(
        pid ("lift_legacy"), "Legacy: Lift Needs Character", false));

    return layout;
}

//==============================================================================
// v3.0-c 分岐3「ひろがりを キャラ声の前へ」用に、キャラ声を関数へ出した。
//
//  ここは他の2つ（ととのえ／サ行おさえ）と事情が違う。
//  キャラ声は **2つの仕事**を持っている:
//    ・音を変える  … ロボ声・メガホン
//    ・音を測る    … エモ（ロングトーン）・サビリフト（サビ検出）
//  そして**ひろがりは「測った結果」だけを使う**（残響と広がりの送り量に掛ける）。
//
//  だから順番を入れ替えるときは、丸ごと後ろに回すのではなく
//  「測る所だけ先に済ませ、音を変える所を後ろに回す」のが正しい。
//  そうしないと、ひろがりが 0 を読んでサビリフトが効かなくなる。
//
//  doFx / doDetect の両方が true のときは、切り出す前と**1行も変わらない**。
//  （dsp_order [1] が既定の bit 一致を毎回確かめている）
//==============================================================================
void VocalGzzioProcessor::applyChara (juce::AudioBuffer<float>& buffer, bool doFx, bool doDetect)
{
    const int numCh      = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();

    // ===== モジュール7「キャラ声」（ロボ声・メガホン・エモ・サビリフト）=====
    // v3.0-b ひろがりと同じ。OFF なら丸ごと飛ばす。
    const bool charaRun = ! mods.isOff (gz::ModuleChain::Chara);
    if (doFx && charaRun) mods.save (gz::ModuleChain::Chara, buffer);

    // ---- v1.4.0 character FX: robot voice (ring mod) then megaphone ----
    if (doFx && charaRun)
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
    // v3.0-c ★ここは `if (charaRun)` の中にあった。つまり
    //  **「キャラ声」を切ると、サビリフトが残響に効かなくなっていた。**
    //  サビリフトは「サビを見つけて残響を少し増やす」機能で、ロボ声やメガホンとは
    //  何の関係もない。キャラ声を使わない人ほど、切ったまま気づかない。
    //
    //  直したが、直した時点で**キャラ声を切っている人の音は変わる**（サビで残響が
    //  増えるようになる）。そこで、
    //   ・新しく置いたときは **直った動き**（既定）
    //   ・**v2.12.0 以前に保存されたプロジェクトを開いたときは、昔の動きのまま**
    //     （setStateInformation が ver<3 を見て lift_legacy を立てる）
    //   ・手で戻すスイッチも用意する（lift_legacy）
    //  としてある。「更新したら昔の曲の音が変わった」は起こさない。
    //
    //  ★検出を呼ぶ位置は1行も動かしていない（キャラ声の処理のあと・渡しの前）。
    //   ここを動かすと charaRun が true のときの音まで変わってしまう。
    const bool liftLegacy = apvts.getRawParameterValue ("lift_legacy")->load() > 0.5f;
    float& emoBloomNow = emoBloomNowV;   // v3.0-c 受け渡し用のメンバー（中身は同じ）
    float& liftNow     = liftNowV;
    if (doDetect) { emoBloomNow = 0.0f; liftNow = 0.0f; }
    if (doDetect && (charaRun || ! liftLegacy))
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

    if (doFx && charaRun) mods.restore (gz::ModuleChain::Chara, buffer);
    // ===== モジュール7「キャラ声」ここまで =====
}

//==============================================================================
// v3.0-c 順番の分岐（案C）用に、2つのモジュールを関数へ出した。
//
//  ここに出したのは「呼ぶ場所を2択にしたい」からで、中身は**1行も変えていない**。
//  切り出しても既定の音が変わらないことは dsp_order が毎回確かめる。
//
//  出せた理由: どちらの区間も、使っているものがすべてプロセッサの持ち物で、
//  processBlock の途中で作られた一時変数に依存していなかった。
//  （「ひろがり」は liftNow / emoBloomNow というキャラ声の中で計算される値に
//    依存しているので、そのままでは前に出せなかった。v3.0-c 第8歩で、
//    キャラ声を「測る所」と「音を変える所」に分けて解いた。上の applyChara 参照）
//==============================================================================
void VocalGzzioProcessor::applyTotonoe (juce::AudioBuffer<float>& buffer)
{
    const int numCh      = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();
    juce::dsp::AudioBlock<float> block (buffer);
    juce::dsp::ProcessContextReplacing<float> ctx (block);
    juce::ignoreUnused (numCh);

    // ---- subtractive EQ ----
    hpf.process (ctx);
    mud.process (ctx);
    if (mud2On) mud2.process (ctx);   // v3.1 弾き語りだけ。胴鳴り 220Hz の2点目
    harsh.process (ctx);

    // ---- v2.4.0 なめらか(動的レゾナンス抑制) ----
    // 「こもり」「キンキン」(固定EQ)のあと・コンプの前。コンプより前に刺さりを
    // 削っておかないと、コンプが刺さりごと持ち上げてしまうため。ゼロ遅延。
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

    // ---- v3.1「ピックおさえ」(アコギだけ) ----
    //  置き場所は なめらか の直後・コンプの前。なめらか と同じ理由で、
    //  頭を先に整えておかないとコンプが頭ごと持ち上げて余計に暴れる。
    //
    //  作り: 速い包絡(0.5ms) ÷ 遅い包絡(30ms) の比を見る。
    //   ・比が 1.4倍(約+3dB)を超えたら「いつもより飛び出した頭」
    //   ・2.8倍(約+9dB)で下げ量いっぱい
    //   ・下げるのは 1ms、戻すのは 60ms（下げ遅れは頭を素通しさせるので速く、
    //     戻しは音がしゃくり上がって聞こえないようゆっくり）
    //  ★先読みはしない。だから頭のいちばん先の数百usはすり抜ける。
    //   そこは意図どおりで、全部潰すと「弾いた感じ」が消える。
    //  ★dB へ直す log は使わない。1サンプルに2回 log10 を呼ぶと
    //   ここだけで無視できない負荷になるので、比のまま線で扱う。
    const float pickAmt = srcPickOk
                        ? apvts.getRawParameterValue ("pick_amt")->load() * 0.01f : 0.0f;
    if (pickAmt > 0.001f)
    {
        const float minG = juce::Decibels::decibelsToGain (-9.0f * pickAmt);  // ブロックに1回
        float* L = buffer.getWritePointer (0);
        float* R = numCh > 1 ? buffer.getWritePointer (1) : nullptr;
        float peak = 0.0f;
        for (int n = 0; n < numSamples; ++n)
        {
            const float x = juce::jmax (std::abs (L[n]), R != nullptr ? std::abs (R[n]) : 0.0f);
            pickFast += (x > pickFast ? pickFastAtk : pickFastRel) * (x - pickFast);
            pickSlow += (x > pickSlow ? pickSlowAtk : pickSlowRel) * (x - pickSlow);

            //  (a)「いま頭か」… 速い包絡が遅い包絡の 1.41倍(+3dB)〜2.82倍(+9dB)
            const float ratio = pickFast / (pickSlow + 1.0e-9f);
            const float t = juce::jlimit (0.0f, 1.0f, (ratio - 1.41f) / (2.82f - 1.41f));

            //  (b)「その頭はいつもより大きいか」… ゆっくりした基準からの飛び出し。
            //   ★これが「そろえ」の本体。(a) だけだと強い頭も弱い頭も同じだけ下がる。
            //   基準は最近の頭の大きさ。無音で 0 へ落ちないよう、音があるときだけ動かす。
            if (pickFast > 1.0e-4f)                        // 約 -80dBFS 以上
            {
                if (! pickRefPrimed) { pickRef = pickFast; pickRefPrimed = true; }
                else pickRef += (pickFast > pickRef ? pickRefUp : pickRefDn) * (pickFast - pickRef);
            }
            const float over = pickFast / (pickRef + 1.0e-9f);
            //  基準どおり(1.0)で 0.4、2倍(+6dB)で 1.0。弱い頭は浅く、強い頭は深く。
            const float lvl = juce::jlimit (0.0f, 1.0f, (over - 1.0f));
            const float depth = t * (0.40f + 0.60f * lvl);

            const float target = 1.0f - depth * (1.0f - minG);
            const float c = (target < pickGain) ? pickDownCoef : pickUpCoef;
            pickGain += c * (target - pickGain);

            L[n] *= pickGain;
            if (R != nullptr) R[n] *= pickGain;
            peak = juce::jmax (peak, 1.0f - pickGain);
        }
        pickMeter.store (juce::jlimit (0.0f, 1.0f, peak * 2.5f));
    }
    else
    {
        //  0% のときは1サンプルも触らない。包絡だけ静かに戻しておく
        //  （次に上げた瞬間に古い値で暴れないように）。
        pickGain = 1.0f;
        pickFast *= 0.5f; pickSlow *= 0.5f;
        if (pickMeter.load() != 0.0f) pickMeter.store (0.0f);
    }
}

void VocalGzzioProcessor::applyDeEsser (juce::AudioBuffer<float>& buffer)
{
    const int numCh      = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();

    // ---- de-esser (split-band style: detect >5.2k, duck a 6.5k high shelf) ----
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

            // v2.12.0 張り保護(§6-2): しきい値を声全体の音量に追従させる。
            // 今までは絶対値(-30〜-18dBFS)だったので、張って歌う=全帯域が
            // 大きくなるだけで「サ行が出た」と誤認して高域を削っていた。
            // 全体包絡の30%(約-10dB)を下回らないしきい値にすると、判定が
            // 「サ行の割合」になり、声量では動かなくなる。小さい声のときは
            // 絶対値side が勝つので、今までの動きと同じ。
            const float bb = juce::jmax (std::abs (bL[n]),
                                         bR != nullptr ? std::abs (bR[n]) : 0.0f);
            dsBroadEnv += (bb > dsBroadEnv ? dsBbAtk : dsBbRel) * (bb - dsBroadEnv);
            const float thrEff = juce::jmax (thr, dsBroadEnv * 0.30f);

            // desired shelf cut in dB when sibilance exceeds threshold
            float wantDb = 0.0f;
            if (dsEnv > thrEff)
                wantDb = juce::jlimit (0.0f, maxCutDb,
                                       20.0f * std::log10 (dsEnv / thrEff) * 1.5f);
            dsCurrentReduction += 0.02f * (wantDb - dsCurrentReduction);

            // update shelf every 32 samples (cheap enough, smooth enough)
            if ((n & 31) == 0)
            {
                // v2.8.0: 32サンプルごとに new していた。確保なしの形へ。
                // v2.10.0 棚の位置も音源モードで持ち替える(アコギは 4kHz)。
                const auto co = ACoefs::makeHighShelf (currentSampleRate, dsShelfHzNow, 0.8f,
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

    // ---- v3.1 2点目のサ行おさえ（弾き語りのフレット/ピックの音）----
    //  dsDetect2Hz == 0 の使いかた（うた・しゃべり・アコギだけ）では
    //  この中へ一度も入らない ＝ 今までと1サンプルも変わらない。
    if (dsDetect2Hz > 1.0f && amount > 0.001f && dsOn)
    {
        scratch2.makeCopyOf (buffer, true);
        juce::dsp::AudioBlock<float> s2block (scratch2);
        juce::dsp::ProcessContextReplacing<float> s2ctx (s2block);
        deessDetectBP2.process (s2ctx);      // 3kHz 前後だけを取り出す

        //  しきい値は1点目と同じ考え方（絶対値と、全体包絡の割合の大きい方）。
        //  下げ量は1点目の半分まで。フレットの音はサ行ほど耳につかないので、
        //  同じだけ下げると今度は「ギターが引っ込んだ」と聞こえる。
        const float thr      = juce::Decibels::decibelsToGain (-30.0f + (1.0f - amount) * 12.0f);
        const float maxCutDb = (12.0f * amount + 4.0f) * 0.5f;

        auto* sL = scratch2.getReadPointer (0);
        auto* sR = scratch2.getReadPointer (juce::jmin (1, scratch2.getNumChannels() - 1));
        auto* bL = buffer.getWritePointer (0);
        auto* bR = numCh > 1 ? buffer.getWritePointer (1) : nullptr;

        for (int n = 0; n < numSamples; ++n)
        {
            const float det = juce::jmax (std::abs (sL[n]), std::abs (sR[n]));
            dsEnv2 += (det > dsEnv2 ? dsEnvAtk : dsEnvRel) * (det - dsEnv2);

            //  全体包絡(dsBroadEnv)は1点目が同じブロックで更新済み。ここでは
            //  読むだけにして二重に進めない（進めると時定数が実質2倍速になる）。
            const float thrEff = juce::jmax (thr, dsBroadEnv * 0.30f);

            float wantDb = 0.0f;
            if (dsEnv2 > thrEff)
                wantDb = juce::jlimit (0.0f, maxCutDb,
                                       20.0f * std::log10 (dsEnv2 / thrEff) * 1.5f);
            dsCurrentReduction2 += 0.02f * (wantDb - dsCurrentReduction2);

            if ((n & 31) == 0)
            {
                const auto co = ACoefs::makePeakFilter (currentSampleRate, dsDipHzNow, dsDipQNow,
                                    juce::Decibels::decibelsToGain (-dsCurrentReduction2));
                *dsDipL.coefficients = co;
                *dsDipR.coefficients = co;
            }
            bL[n] = dsDipL.processSample (bL[n]);
            if (bR) bR[n] = dsDipR.processSample (bR[n]);
        }
        //  画面のメーターは1点目と2点目の大きい方（「いまサ行を抑えている」の表示）
        meterDS.store (juce::jlimit (0.0f, 1.0f,
                        juce::jmax (dsCurrentReduction, dsCurrentReduction2 * 2.0f) / 16.0f));
    }
    else
        dsCurrentReduction2 *= 0.9f;
}

//==============================================================================
void VocalGzzioProcessor::resetReverbState() noexcept
{
    reverb.reset();
    if (heyaReady.load()) heyaRev.reset();
    revHPF.reset(); revLPF.reset();
    for (auto* ring : { &preBufL, &preBufR, &springBufL, &springBufR, &shimBufL, &shimBufR })
        std::fill (ring->begin(), ring->end(), 0.0f);
    preWrite = springW = shimW = 0;
    springLpL = springLpR = shimPhase = 0.0f;
    shimLpL = shimLpR = shimHpL = shimHpR = 0.0f;
    shimFb.clear();
    revBypassGain = 0.0f;
    revWasRunning = false;
}

void VocalGzzioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;

    // v4.0.0 へや: **いつも用意する**（約9MB・約180ms）。
    // ------------------------------------------------------------------
    // もとは「初めて選ばれたときだけ用意する」倹約版だった。
    // が、用意はメッセージスレッド(handleAsyncUpdate)まかせで、
    // それが走らなければ heyaReady は false のまま。
    // その間は従来のタンク式が鳴るので、**部屋を選んでも音が変わらない**
    // ように見える（＝機能が丸ごと死んでいるのと同じ）。
    // 起動時に180ms払うほうが、静かに効かないより百倍まし。
    heyaRev.prepare (sampleRate, samplesPerBlock);
    heyaReady.store (true);

    // ---- v1.8.0 voice changer + unison ----
    for (auto& s : vcSh) s.prepare (sampleRate);
    for (auto& s : unSh) s.prepare (sampleRate);
    // v2.12.0 無声ガード(§6-3): 本人の声(ボイス変換・ピッチ補正)は子音を素通し、
    // ユニゾン/ハモリの分身は子音を黙らせる(4人分の「サッ」が重ならないように)。
    for (auto& s : vcSh) s.setUnvoicedGuard (1);
    for (auto& s : unSh) s.setUnvoicedGuard (2);
    pitchDet.prepare (sampleRate);            // v1.9.0 auto-tune F0 detector
    pitchCorrection.reset();
    pitchWasTracking = false;
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
    mud2.prepare (spec);                                // v3.1 弾き語りの2点目
    *mud2.state = ACoefs::makePeakFilter (sampleRate, 220.0f, 1.3f, 1.0f);
    presence.prepare (spec); air.prepare (spec);
    ringL.prepare (spec); ringR.prepare (spec);          // v1.9.5 艶
    ringDet.prepare (spec); sibDet.prepare (spec);
    ringDet.coefficients = Coefficients::makeBandPass (sampleRate, 3000.0, 1.2f);
    sibDet .coefficients = Coefficients::makeBandPass (sampleRate, 7500.0, 0.9f);
    ringL.coefficients = ringR.coefficients = Coefficients::makePeakFilter (sampleRate, 3000.0, 1.4f, 1.0f);
    ringEnv = sibEnv = 0.0f; ringGainDb = 0.0f; ringApplied = -99.0f;
    deessDetectHP.prepare (spec);
    deessDetectBP2.prepare (spec);                      // v3.1 2点目の側鎖
    *deessDetectBP2.state = ACoefs::makeBandPass (sampleRate, 3000.0f, 1.1f);

    juce::dsp::ProcessSpec mono = spec; mono.numChannels = 1;
    dsShelfL.prepare (mono); dsShelfR.prepare (mono);
    dsDipL.prepare (mono);   dsDipR.prepare (mono);     // v3.1 2点目で下げる山
    //  ここで先に2次の係数を入れておく理由は dsShelf と同じ（音声側で確保しない）。
    dsDipL.coefficients = Coefficients::makePeakFilter (sampleRate, 3500.0, 1.2f, 1.0f);
    dsDipR.coefficients = Coefficients::makePeakFilter (sampleRate, 3500.0, 1.2f, 1.0f);
    dsEnv2 = 0.0f; dsCurrentReduction2 = 0.0f;
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
    scratch2.setSize  (2, samplesPerBlock, false, false, true);   // v3.1 2点目の側鎖
    // v3.0 モジュールの退避先。音声コールバックでは一切確保しないので、ここで取る。
    mods.prepare (sampleRate, samplesPerBlock, 2);

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
    for (auto* f : { &dnPhaseLow2, &dnPhaseLow3, &dnPhaseMid3 })
    {
        f->prepare (spec);
        f->setType (juce::dsp::LinkwitzRileyFilterType::allpass);
    }
    dnPhaseLow2.setCutoffFrequency (1200.0f);
    dnPhaseLow3.setCutoffFrequency (5000.0f);
    dnPhaseMid3.setCutoffFrequency (5000.0f);
    for (auto* f : { &dnDryPhase1, &dnDryPhase2, &dnDryPhase3 })
    {
        f->prepare (spec);
        f->setType (juce::dsp::LinkwitzRileyFilterType::allpass);
    }
    dnDryPhase1.setCutoffFrequency (250.0f);
    dnDryPhase2.setCutoffFrequency (1200.0f);
    dnDryPhase3.setCutoffFrequency (5000.0f);
    dnDryPhaseBuffer.setSize (2, samplesPerBlock, false, false, true);
    for (auto& b : bandBuf) b.setSize (2, samplesPerBlock, false, false, true);
    dnEnvAtk    = 1.0f - std::exp (-1.0f / (0.002f * (float) sampleRate));
    dnEnvRel    = 1.0f - std::exp (-1.0f / (0.060f * (float) sampleRate));
    dnOpenCoef  = 1.0f - std::exp (-1.0f / (0.003f * (float) sampleRate));
    dnCloseCoef = 1.0f - std::exp (-1.0f / (0.045f * (float) sampleRate));
    // v2.12.0 サフサフ対策(§6-1)
    dnFastOpen    = 1.0f - std::exp (-1.0f / (0.001f * (float) sampleRate));
    dnHoldSamples = (int) (0.080 * sampleRate);
    dnVoiceHold   = 0;
    dnHfVoiceHold = 0;
    // v2.12.0 張り保護(§6-2)
    beltFastDb = -60.0f; beltSlowDb = -60.0f; beltNow = 0.0f; beltPrimed = false;
    // v3.0「つぶさない」の自動ヘッドルーム: 下げ20ms / 戻し1.5秒（ブロック単位）
    crushGuard = 0.0f; crushHeadDb = 0.0f; crushMeterDb.store (0.0f);
    crushHeadAtk = 1.0f - std::exp (-(float) samplesPerBlock / (0.020f * (float) sampleRate));
    crushHeadRel = 1.0f - std::exp (-(float) samplesPerBlock / (1.500f * (float) sampleRate));
    dsBroadEnv = 0.0f;
    dsBbAtk = 1.0f - std::exp (-1.0f / (0.010f * (float) sampleRate));
    dsBbRel = 1.0f - std::exp (-1.0f / (0.200f * (float) sampleRate));
    dnClassifier.prepare (sampleRate);
    dnInitialFollowCoef = 1.0f - std::exp (-1.0f / (0.35f * (float) sampleRate));
    dnLearnPeriodicFrames = 0;
    dnLearnStationaryFrames = 0;
    // v3.0 自動学びなおし
    dnRelearnCoef = 1.0f - std::exp (-1.0f / (2.0f * (float) sampleRate));   // τ≈2秒
    dnFallMax     = (int) (0.030 * sampleRate);
    for (auto& r : dnFallRun) r = 0;
    dnEnvSlowCoef = 1.0f - std::exp (-1.0f / (0.15f * (float) sampleRate));   // v3.1 さ行の頭
    dnEnvSlow3    = 0.0f;
    dnSoftOpen    = 1.0f - std::exp (-1.0f / (0.25f * (float) sampleRate));   // v3.1 そっと開ける(0.25秒)
    dnHfDuckCoef  = 1.0f - std::exp (-1.0f / (0.08f * (float) sampleRate));   // v3.1 高域抑えを下げる向き(0.08秒)

    // v3.1「ピックおさえ」の時定数
    pickFastAtk  = 1.0f - std::exp (-1.0f / (0.0005f * (float) sampleRate));  // 0.5ms
    pickFastRel  = 1.0f - std::exp (-1.0f / (0.020f  * (float) sampleRate));  // 20ms
    pickSlowAtk  = 1.0f - std::exp (-1.0f / (0.030f  * (float) sampleRate));  // 30ms
    pickSlowRel  = 1.0f - std::exp (-1.0f / (0.150f  * (float) sampleRate));  // 150ms
    pickDownCoef = 1.0f - std::exp (-1.0f / (0.001f  * (float) sampleRate));  // 下げる 1ms
    pickUpCoef   = 1.0f - std::exp (-1.0f / (0.060f  * (float) sampleRate));  // 戻す 60ms
    pickRefUp    = 1.0f - std::exp (-1.0f / (0.80f   * (float) sampleRate));  // 基準の上がり 0.8秒
    pickRefDn    = 1.0f - std::exp (-1.0f / (4.00f   * (float) sampleRate));  // 基準の下がり 4秒
    pickFast = pickSlow = 0.0f; pickGain = 1.0f; pickMeter.store (0.0f);
    pickRef = 0.0f; pickRefPrimed = false;
   #if VOCALGZZIO_TRIAL
    // 体験版のディップは「挿した瞬間」ではなく60秒後から。
    //  カウンタ0開始だと最初の0.6秒がいきなりディップになり、
    //  第一印象が「音が引っ込む壊れたプラグイン」になってしまう。
    trialCounter = (int) (sampleRate * 0.60);
   #endif
    for (int b = 0; b < 4; ++b) { dnEnv[b] = 0; dnGain[b] = 1; }
    // v1.5.0: a learned noise profile is part of the user's settings now, so it
    // survives prepareToPlay (levels are sample-rate independent). Only the
    // adaptive tracker restarts when nothing has been learned yet.
    if (! dnLearned)
        for (int b = 0; b < 4; ++b) dnFloor[b] = 1e-5f;
    learnCountdown.store (0);
    dnLearnCommand.store (0);

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
        resModeNow = -1;                 // v2.10.0 音源モードに合わせて組み直させる
        proxEvener.prepare (sampleRate); // v2.10.0 距離ならし
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
    revBypassGain = 0.0f;
    revBypassStep = (float) (1.0 / (0.012 * sampleRate));
    revWasRunning = false;

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
        // ピッチ窓はサンプルレートに追従する。実際の遅延を原音側にも確保する。
        const int maxLat = vcSh[0].latencySamples();
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

    for (auto& sample : tunerBuf) sample.store (0.0f, std::memory_order_relaxed);
    tunerSequence.store (0);
    tunerPos.store (0);
    tunerChannelEnergy[0] = tunerChannelEnergy[1] = 0.0;
    tunerInputChannel = 0;
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
        // 窓はprepare()がサンプルレートに合わせて選ぶ。
        // 96kHzでも低音の周期数を保ち、原音側の遅延と同じ値を申告する。
        // v2.9.0 セッションモード: ONの間は遅延をふやす3機能を素通しにする。
        const bool sessionOn = apvts.getRawParameterValue ("session")->load() > 0.5f;
        // v2.10.0 音源モードがアコギなら、ピッチ系はそもそも通さない。
        //  ここは updateParameters() より前に走るので、プロファイルを直接読む。
        // v3.1 ★voiceOnly ではなく pitchOk を見るようにした。
        //  「弾き語り」は声があるので ことば・艶 は要る(voiceOnly=true)が、
        //  和音が同時に鳴っているので単音前提のピッチ検出は誤動作する
        //  (pitchOk=false)。ここを voiceOnly のままにすると、申告する遅延と
        //  processBlock が実際に通す顔ぶれが食い違い、DAW がトラックを
        //  17.4ms 前へ引っ張って**歌だけ走る**。下の processBlock 側と必ず同じ条件にする。
        const bool voiceSrc = sourceProfile ((int) apvts.getRawParameterValue ("src_mode")->load()).pitchOk;
        // v3.0 ★モジュール「へんしん」も申告に効かせる。
        //  ここを忘れると、へんしんOFF＝実際は素通しなのに 768 サンプル(17.4ms)
        //  遅れると申告し続け、DAW がその嘘を信じてトラックを 17.4ms 前へ引っ張る
        //  ＝**歌だけ走る**。v2.9.0 のハモリで一度やった失敗と同じ形なので、
        //  processBlock 側の条件と必ず同じ顔ぶれにしておく。
        const bool henshinOn = apvts.getRawParameterValue (
                                   gz::ModuleChain::paramId (gz::ModuleChain::Henshin))->load() > 0.5f;
        const bool vcOn = apvts.getRawParameterValue ("vc_on")->load() > 0.5f && henshinOn && ! sessionOn && voiceSrc;
        const bool atOn = apvts.getRawParameterValue ("at_on")->load() > 0.5f && henshinOn && ! sessionOn && voiceSrc;
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
    // v2.10.0 #77/#78 音源モードで中心を持ち替える（うたは今までと同じ数値）
    const int  srcMode = (int) apvts.getRawParameterValue ("src_mode")->load();
    const auto prof    = sourceProfile (srcMode);

    *hpf.state      = ACoefs::makeHighPass   (sr, p ("lowcut"), 0.707f);
    *mud.state      = ACoefs::makePeakFilter (sr, prof.mudHz,   prof.mudQ,   toGain (p ("mud")));
    *harsh.state    = ACoefs::makePeakFilter (sr, prof.harshHz, prof.harshQ, toGain (p ("harsh")));
    *presence.state = ACoefs::makePeakFilter (sr, prof.presHz,  prof.presQ,  toGain (p ("presence")));
    *air.state      = ACoefs::makeHighShelf  (sr, prof.airHz,   0.707f,      toGain (p ("air")));
    *deessDetectHP.state = ACoefs::makeHighPass (sr, prof.dsDetectHz, 0.9f);

    // v3.1 2点目のこもり（弾き語りだけ）。使わない使いかたでは1回も通さない。
    mud2On = prof.mudHz2 > 1.0f;
    if (mud2On)
        *mud2.state = ACoefs::makePeakFilter (sr, prof.mudHz2, prof.mudQ2, toGain (p ("mud")));

    // v3.1 2点目のサ行おさえ（弾き語りだけ）。検出は帯域通過、下げるのは山。
    dsDetect2Hz = prof.dsDetectHz2;
    if (dsDetect2Hz > 1.0f)
    {
        *deessDetectBP2.state = ACoefs::makeBandPass (sr, dsDetect2Hz, 1.1f);
        dsDipHzNow = prof.dsDipHz2;
        dsDipQNow  = prof.dsDipQ2;
    }

    // なめらか(Resonance)の見張る範囲。モードが変わったときだけ組み直す。
    // Tamer::prepare は固定長配列に係数を書くだけで確保は起きないので、
    // 音声コールバックの中から呼んでも安全（dsp_noalloc で確認している）。
    if (srcMode != resModeNow)
    {
        resModeNow = srcMode;
        resTamer.prepare (sr, prof.resLoHz, prof.resHiHz);
    }
    // ディエッサーの棚と、歌かどうかの判定を音声側へ渡す
    dsShelfHzNow  = prof.dsShelfHz;
    srcVoiceOnly  = prof.voiceOnly;
    srcBreathOk   = prof.breathOk;
    srcSpaceOk    = prof.spaceOk;
    srcPitchOk    = prof.pitchOk;      // v3.1 ピッチ系だけ別扱い
    srcPickOk     = prof.pickOk;       // v3.1 ピックおさえ（アコギだけ）
    srcDnScale    = prof.dnScale;      // v3.1 使いかたごとのノイズ除去の効き

    // ---- v3.0「つぶさない」がONのときだけ効く量（0=いつもの声量、1=張っている）
    // なぜ「しきい値を上げる」のか:
    //   圧縮は「しきい値を超えたぶんを 1/比率 に縮める」。声を張る＝入力が上がる
    //   ＝超える量が増える＝**縮められる量も増える**。だから張るほど詰まる。
    //   張っている間だけしきい値を持ち上げれば、いつもの声量での効き（まとまり）は
    //   そのままに、張ったところだけ縮められずに抜ける。
    //   比率も少し寝かせる（4.0→2.8 / 2.5→1.6）。しきい値だけだと、超えた瞬間に
    //   同じ急さで潰れるので「壁に当たった」感じが残るため。
    const float cg = (apvts.getRawParameterValue ("crush_on")->load() > 0.5f) ? crushGuard : 0.0f;

    // comp1: fast peak catcher; amount maps threshold -8..-30 dB, ratio 4:1
    const float c1 = p ("comp1") * 0.01f;
    comp1.setThreshold (juce::jmap (c1, -8.0f, -30.0f) + 6.0f * cg);
    comp1.setRatio (4.0f - 1.2f * cg);
    comp1.setAttack  (juce::jmax (1.0f, p ("attack") * 0.5f));
    comp1.setRelease (p ("release") * 0.6f);

    // comp2: slow leveller; amount maps threshold -10..-35 dB, ratio 2.5:1
    const float c2 = p ("comp2") * 0.01f;
    comp2.setThreshold (juce::jmap (c2, -10.0f, -35.0f) + 6.0f * cg);
    comp2.setRatio (2.5f - 0.9f * cg);

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
        // v4.0.0: 7以上は「へや」。従来の tank は通さないので defs は使わないが、
        // 出口の HPF/LPF は共通なので 0 番（素通しに近い）の値を当てておく。
        const RevDef& d = defs[type >= kHeyaFirst ? 0 : juce::jlimit (0, 6, type)];
        if (type >= kHeyaFirst)
        {
            if (heyaReady.load())
            {
                // どちらも確保しない・例外を投げない。音声スレッドから呼んでよい。
                heyaRev.setRoom (type - kHeyaFirst);
                heyaRev.setSize (s);
            }
        }

        juce::dsp::Reverb::Parameters rp;
        rp.roomSize = juce::jmap (s, d.roomLo, d.roomHi);
        rp.damping  = d.damp;
        rp.width    = d.width;
        // ON/OFFは共通の出口で12msフェード。ここで即座にゼロへ飛ばさない。
        rp.wetLevel = juce::jlimit (0.0f, 1.0f, p ("revmix") * 0.01f);
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

    // ---- v3.0 モジュールのON/OFF ----
    // ここでこのブロックぶんの「渡し具合」を決めてしまう。以降の save/restore は
    // その値を読むだけなので、同じモジュールが離れた2か所にあっても足並みが揃う。
    // v3.0-c「くらべる」: 押している**間だけ**8つとも素通しにする。
    //  ★パラメータは1つも書き換えない。書き換えると、押した瞬間にホストの
    //   オートメーションが動き、離す前に保存やプリセット切替が起きると
    //   「全部OFFのまま保存された」事故になる。専用のフラグで切るのが安全。
    //   渡しは ModuleChain の 10ms クロスフェードに乗るので、プチッと言わない。
    const bool cmp = compareBypass.load (std::memory_order_relaxed);
    // v3.1「1つずつ」画面の くらべる は、その1枚だけを渡しにする。
    const int  cmpOne = compareOne.load (std::memory_order_relaxed);
    for (int m = 0; m < gz::ModuleChain::Count; ++m)
        mods.setOn (m, ! cmp && m != cmpOne
                       && apvts.getRawParameterValue (gz::ModuleChain::paramId (m))->load() > 0.5f);
    mods.beginBlock (numSamples);

    // ---- v3.0「つぶさない」----
    // 「音量が上がったときに音が潰れないようにするモード」。
    // crushGuard は前のブロックで測った張り具合(0..1)。OFF のときは 0 なので、
    // 掛かる場所すべてが今までと**同じ式**になる（＝1サンプルも変わらない）。
    const bool  crushOn = apvts.getRawParameterValue ("crush_on")->load() > 0.5f;
    const float crushCg = crushOn ? crushGuard : 0.0f;

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

    // 学習の開始・やり直し・解除をブロック境界で反映する。
    // 操作側から集計配列を消すと、音声側の加算と競合して不定な床ができていた。
    const int dnCommand = dnLearnCommand.exchange (0, std::memory_order_acq_rel);
    if (dnCommand != 0)
    {
        for (auto& f : dnFloorLearn) f = 1e-6f;
        for (auto& row : dnHist) for (auto& count : row) count = 0;
        dnHistCount = 0;
        dnHistDecim = 0;
        dnLearnPeriodicFrames = 0;
        dnLearnStationaryFrames = 0;
        dnLearnResult.store (0);
        learnCountdown.store (dnCommand == 1 ? (int) (1.5 * currentSampleRate) : 0);
        if (dnCommand == -1)
        {
            dnLearned = false;
            dnLearnedShared.store (false);
            dnProfileValidation.store (0);
            dnClassifier.prepare (currentSampleRate);
            for (int b = 0; b < 4; ++b)
            {
                dnFloor[b] = 1e-5f;
                dnFloorShared[b].store (dnFloor[b]);
            }
        }
    }

    // ---- v2.10.0 #73 ゼロ遅延の自己証明（入口） ----
    // 「測る」を押している約1秒だけ、入力を**こちらで作った信号に差し替える**。
    // 中身は無音＋インパルス1発だけ。チェーンをそのまま通し、出口で出てきた
    // 位置を見れば、その人の環境での実測遅延が分かる。
    // ※測っている間の出力は出口側で消すので、耳には何も届かない。
    if (selfTestRunning.load (std::memory_order_relaxed))
    {
        const int pos   = selfTestPos.load (std::memory_order_relaxed);
        const int impAt = (int) (currentSampleRate * 0.25);   // 落ち着かせてから入れる
        for (int ch = 0; ch < numCh; ++ch)
        {
            auto* d = buffer.getWritePointer (ch);
            juce::FloatVectorOperations::clear (d, numSamples);
            const int local = impAt - pos;
            if (local >= 0 && local < numSamples) d[local] = 1.0f;
        }
        selfTestImpactAt = impAt;
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

    // ---- v2.10.0 距離ならし（近接効果の自動補正） ----
    // マイク音量の直後・原音コピーより前。ここが v2.8.0 で確定した「いちばん手前」。
    // 原音側にも掛かるので、Mix で混ぜ戻したときに太さが食い違わない。
    // 声が出ているときだけ学習する（無音でノイズの比を「いつも」と覚えないため）。
    {
        const float pa = apvts.getRawParameterValue ("prox_amt")->load() * 0.01f;
        proxEvener.setAmount (pa);
        if (pa > 0.0f)
        {
            const float rms = buffer.getRMSLevel (0, 0, numSamples);
            proxEvener.process (buffer.getWritePointer (0),
                                numCh > 1 ? buffer.getWritePointer (1) : nullptr,
                                numSamples, rms > 0.0006f);   // ざっくり -64dBFS 以上を「声」とみなす
        }
        proxCorrDb.store (proxEvener.currentCorrectionDb(), std::memory_order_relaxed);
    }

    // ---- v2.12.0 張り保護(§6-2): 「いつもの声量」に対して今どれだけ張っているか ----
    // 速い包絡(約60ms)と遅い基準(約4秒・そこそこ鳴っているときだけ動く)の差が
    // +6dB を超えたら「張っている」。0..1 にして「なめらか」へ渡す(2〜4kHzの
    // 削りを最大50%緩める)。張りっぱなしなら基準が追いつき、保護は自然に解ける。
    {
        float pk = 0.0f;
        for (int ch = 0; ch < juce::jmin (numCh, 2); ++ch)
            pk = juce::jmax (pk, buffer.getMagnitude (ch, 0, numSamples));
        const float pkDb = juce::Decibels::gainToDecibels (pk, -80.0f);
        const float aF = 1.0f - std::exp (-(float) numSamples / (0.060f * (float) currentSampleRate));
        const float aS = 1.0f - std::exp (-(float) numSamples / (4.0f   * (float) currentSampleRate));
        beltFastDb += aF * (pkDb - beltFastDb);
        // v3.0 ★最初の1回だけ、基準を「いま出ている声量」に合わせる。
        //  これが無いと、基準は起動時の -60 dB から4秒の時定数で登るので、
        //  **歌い始めの5〜8秒はずっと「張っている」と誤判定**する。
        //  v2.12.0 のなめらか保護でも同じことが起きていた（気づいていなかった）。
        //  「つぶさない」の表示を作って、いつもの声量でも 6.00 dB と出続けたので
        //  発覚した。数字を画面に出す作りにしていなければ、見つからなかった。
        if (beltFastDb > -45.0f)
        {
            if (! beltPrimed) { beltSlowDb = beltFastDb; beltPrimed = true; }
            else              beltSlowDb += aS * (beltFastDb - beltSlowDb);
        }
        beltNow = juce::jlimit (0.0f, 1.0f, (beltFastDb - beltSlowDb - 6.0f) / 6.0f);
        resTamer.setBeltProtect (beltNow);

        // v3.0「つぶさない」用に少しなめらかにする。beltNow はブロックごとに
        // ぱたぱた動くので、そのまま圧縮のしきい値に入れると音量が揺れて聞こえる。
        // 上がるのは速く(守りは遅れさせない)、下がるのはゆっくり(戻りで段差を作らない)。
        crushGuard += (beltNow > crushGuard ? 0.25f : 0.03f) * (beltNow - crushGuard);
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


    // Tuner feed only: summing stereo can cancel opposite-polarity inputs and
    // attenuate a quiet instrument connected to only one input by 6 dB. Keep
    // one channel, switching only when the other has clearly more energy.
    // Smoothed energy plus 6 dB hysteresis avoids sample-by-sample switching.
    {
        const float* dl = dryBuffer.getReadPointer (0);
        const float* dr = dryBuffer.getReadPointer (juce::jmin (1, dryBuffer.getNumChannels() - 1));
        double channelEnergy[2] {};
        for (int n = 0; n < numSamples; ++n)
        {
            channelEnergy[0] += (double) dl[n] * dl[n];
            channelEnergy[1] += (double) dr[n] * dr[n];
        }
        const double energyAlpha = 1.0 - std::exp (-numSamples / (0.030 * currentSampleRate));
        for (int ch = 0; ch < 2; ++ch)
            tunerChannelEnergy[ch] += energyAlpha
                * (channelEnergy[ch] / juce::jmax (1, numSamples) - tunerChannelEnergy[ch]);
        const int other = 1 - tunerInputChannel;
        if (tunerChannelEnergy[other] > 1.0e-10
            && tunerChannelEnergy[other] > 4.0 * tunerChannelEnergy[tunerInputChannel])
            tunerInputChannel = other;
        // v4.3.1 ★いま見ている側が「このブロックで完全に無音」のときは、平滑を
        //  待たずにすぐ乗り換える。
        //  平滑(30ms)＋6dBヒステリシスだけだと、直前が大きい音だった場合に
        //  乗り換えまで約0.3秒かかる。その間チューナーは無音の側を解析し続け、
        //  解析窓(32768サンプル)に無音が残るため音程がずれる。
        //  96kHz/192kHz では解析窓が時間として短く、残った無音の割合が大きい
        //  ので誤差が出た（実測: B0 を右だけに入れて 9.81セント / ui_tuner）。
        //  「完全な無音」(-120dBFS未満)だけを対象にするので、小さい楽器の音で
        //  ばたつくことはない。
        const double instantCurrent = channelEnergy[tunerInputChannel] / juce::jmax (1, numSamples);
        const double instantOther   = channelEnergy[other]             / juce::jmax (1, numSamples);
        if (instantOther > 1.0e-10 && instantCurrent < 1.0e-12)
            tunerInputChannel = other;
        const float* tunerInput = tunerInputChannel == 0 ? dl : dr;
        tunerSequence.fetch_add (1, std::memory_order_acq_rel);
        int pos = tunerPos.load (std::memory_order_relaxed);
        for (int n = 0; n < numSamples; ++n)
        {
            tunerBuf[pos].store (tunerInput[n], std::memory_order_relaxed);
            pos = (pos + 1) % tunerSize;
        }
        tunerPos.store (pos, std::memory_order_release);
        tunerSequence.fetch_add (1, std::memory_order_release);
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

    // ===== モジュール1「おそうじ」ここから（ゲート・ノイズ除去）=====
    // v3.0-b OFF なら丸ごと飛ばす（区間は2か所に分かれるが、フラグは1つ）
    const bool soujiRun = ! mods.isOff (gz::ModuleChain::Souji);
    // 「De-noiseだけ使いたいのに他がかかる」への答え。OFF なら、この区間を
    // 通る前と後で波形が1サンプルも変わらない（tools/dsp_modules で一致確認）。
    if (soujiRun) mods.save (gz::ModuleChain::Souji, buffer);

    // ---- gate ----
    if (soujiRun && apvts.getRawParameterValue ("gate_on")->load() > 0.5f)
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
        const bool  dnRelearn = apvts.getRawParameterValue ("dn_relearn")->load() > 0.5f;
        // v3.1 使いかたごとに効きを変える（設計書§3）。ツマミの数字はそのまま、
        //  同じ 50% でも しゃべり=強め(×1.25) / アコギだけ・弾き語り=ひかえめ(×0.7)。
        //  うたは ×1.0 = 今までと同じ。上限は 1.0（ツマミ100%より強くはしない）。
        const float amount = juce::jlimit (0.0f, 1.0f,
                                apvts.getRawParameterValue ("denoise")->load() * 0.01f * srcDnScale);
        int learn = learnCountdown.load();

        // 切替中にも分割フィルターの履歴を更新する。再び入れた瞬間に
        // 前回の古い音が漏れたり、原音側の位相補償と食い違ったりしない。
        {
            // 直列分割では、下位帯域にも後段の位相回転を補わないと平坦に戻らない。
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
            {
                juce::dsp::AudioBlock<float> b0 (bandBuf[0]);
                juce::dsp::ProcessContextReplacing<float> c0 (b0);
                dnPhaseLow2.process (c0);
                dnPhaseLow3.process (c0);
                juce::dsp::AudioBlock<float> b1 (bandBuf[1]);
                juce::dsp::ProcessContextReplacing<float> c1x (b1);
                dnPhaseMid3.process (c1x);
            }
        }

        // おそうじを切っていても、明示的に始めた学習は最後まで測る。
        // 音を変えるかどうかと測定を分けないと、学習中表示が戻らなくなる。
        if ((soujiRun && amount > 0.001f && dnOn) || learn > 0)
        {

            const float maxAttenDb = dnOn ? 24.0f * amount : 0.0f;
            float attenSum = 0.0f;

            for (int n = 0; n < numSamples; ++n)
            {
                // 音量の大小と関係なく、声・演奏の周期性と部屋の安定性を測る。
                // 左右を足すと逆相の声が消えるので、判定は左右で独立する。
                if (dnClassifier.push (buffer.getSample (0, n),
                                       buffer.getSample (juce::jmin (1, numCh - 1), n),
                                       dnEnv, gateGain > 0.99f)
                    && learn > 0)
                {
                    if (dnClassifier.isPeriodic()) ++dnLearnPeriodicFrames;
                    if (dnClassifier.isStationary()) ++dnLearnStationaryFrames;
                }
                // 高域の子音は高域自身の立ち上がりで保護する。
                // 母音が出たことだけを理由に高域の部屋ノイズを開放しない。
                const bool hfOnset = dnEnv[3] > 2.2e-3f
                                  && dnEnv[3] > dnEnvSlow3 * 1.25f;
                if (hfOnset) dnHfVoiceHold = dnHoldSamples;
                else if (dnHfVoiceHold > 0) --dnHfVoiceHold;
                // ---- v2.12.0 サフサフ対策(§6-1) ----
                // 声の中心帯域(250-1200 / 1200-5000)が床から+12dB(×4)を超えたら
                // 「声が出ている」。その間+80msは**全帯域を一斉に開く**。
                // 今までは帯域ごとに独立して開いていたので、喋りはじめの弱い
                // 子音・息(床に近い)が高域だけ「まだノイズ」と判定されて削られ、
                // サ行・ハ行の頭がサフサフになっていた。判定は1サンプル前の
                // 包絡を見るが、保持が80msあるので問題にならない。
                // ★条件は「2帯域そろって+12dB」または「片帯域が+20dB」。
                //  片帯域+12dBだけにすると、部屋ノイズの包絡の揺れ(床は最小値
                //  追従なので、揺れの上側は床の4倍を超えることがある)で誤発火し、
                //  声のない区間まで開きっぱなしになる(dsp_dnonset[2]で実測)。
                //  さらに絶対レベルの下限(-52dBFS相当)を重ねる。床は最小値追従
                //  なので、静かな部屋ノイズでも包絡の揺れの上側が床の4倍を超えて
                //  「声」と誤認し、開きっぱなしになる(dsp_dnonset[2]で実測)。
                //  声・ささやきは-45dBFSより上に来るので、この下限では切れない。
                // v3.1 ★「喋りはじめのさ行」も声として扱う。
                //  さ・し・す…のエネルギーは5kHzより上（帯域3）に集まるが、
                //  この判定は帯域1・2しか見ていなかった。フレーズの頭が子音だと
                //  母音が来るまで発火せず、その間の子音がエキスパンダーに揉まれて
                //  シャフシャフしていた（文中の子音は80ms保持に守られるので無事＝
                //  「喋りはじめだけ」症状が出る、という報告どおり）。
                //  レベルだけでは騒がしい部屋の床と子音を区別できないので、
                //  **立ち上がりの速さ**で見る: 帯域3の包絡が「ゆっくり平均(0.15s)」を
                //  一気に上回るのは子音の頭だけ。しきい値は実測から決めた:
                //   ・さ行の頭のジャンプ = ×1.38（-48dBの部屋で-45dB相当の弱い子音）
                //   ・定常ノイズの包絡の揺れ = ×1.05〜1.08（release 60msが均すため）
                //  → 中間の ×1.25（+2dB）。絶対の下限(-53dBFS相当)も重ねて、
                //  無音の空騒ぎを弾く（dsp_dnonset[2]が誤発火を毎回見張る）。
                //  ★「床より上」の条件は置かない。学習した床は med×1.4、つまり
                //  包絡の中央値より**上**にあるので、床と比べると弱い子音
                //  （床×1.06 とか）を自分で弾いてしまう（実測でそれが起きた）。
                if (((dnEnv[1] + dnEnv[2] > 2.5e-3f)
                     && ((dnEnv[1] > dnFloor[1] * 4.0f && dnEnv[2] > dnFloor[2] * 4.0f)
                          || dnEnv[1] > dnFloor[1] * 10.0f
                          || dnEnv[2] > dnFloor[2] * 10.0f))
                    || hfOnset)
                    dnVoiceHold = dnHoldSamples;
                else if (dnVoiceHold > 0) --dnVoiceHold;
                const bool voiceOpen = dnVoiceHold > 0;
               #ifdef GZ_DNDEBUG
                { static int dbg = 0; if ((dbg++ & 511) == 0)
                    std::printf ("DBG env3=%.5f slow3=%.5f floor3=%.5f open=%d\n",
                                 dnEnv[3], dnEnvSlow3, dnFloor[3], (int) voiceOpen); }
               #endif

                for (int b = 0; b < 4; ++b)
                {
                    float pk = 0.0f;
                    for (int ch = 0; ch < juce::jmin (numCh, 2); ++ch)
                        pk = juce::jmax (pk, std::abs (bandBuf[b].getSample (ch, n)));

                    dnEnv[b] += (pk > dnEnv[b] ? dnEnvAtk : dnEnvRel) * (pk - dnEnv[b]);

                    // v3.0 包絡が「自由落下」しているかどうか。
                    //  部屋鳴りなら波形の山が数ms毎に包絡へ届く（カウンタは即リセット）。
                    //  ミュート・入力切替の直後は、山が来ないまま包絡だけが release で
                    //  落ち続ける。それを30ms見たら「落下中」＝学びなおしを止める。
                    //  （落下中の包絡を学ぶと、床がミュートのたびに少しずつ沈む）
                    if (pk < dnEnv[b] * 0.7f) { if (dnFallRun[b] <= dnFallMax) ++dnFallRun[b]; }
                    else                        dnFallRun[b] = 0;

                    // v3.1 帯域3のゆっくり平均（喋りはじめのさ行検出の基準線）
                    if (b == 3)
                        dnEnvSlow3 += dnEnvSlowCoef * (dnEnv[3] - dnEnvSlow3);

                    if (learn > 0)
                    {
                        dnFloorLearn[b] = juce::jmax (dnFloorLearn[b], dnEnv[b]);
                        // v2.10.0 中央値を取るための度数分布。16サンプルに1回で足りる
                        // (包絡は 2ms/60ms で均してあるので、それより速くは動かない)。
                        if (dnHistDecim == 0)
                        {
                            const float db = juce::Decibels::gainToDecibels (dnEnv[b], -120.0f);
                            int idx = (int) std::lround (db) + 120;
                            dnHist[b][juce::jlimit (0, dnHistBins - 1, idx)] += 1;
                            if (b == 3) ++dnHistCount;      // 最後の帯域で1回だけ数える
                        }
                    }
                    // v2.10.0 ★ここは else ではない。
                    //   以前は学習中の1.5秒だけ自動追従が止まっていた。採用しなかった
                    //   ときに「押しただけで音が変わる」ことになり（実測 低域 +3.9dB）、
                    //   拒否が無害にならない。学習中も追従は回し続ける。
                    //   採用したときは、下でどうせ床を上書きするので影響しない。
                    // 未学習時も約0.5秒の定常・非周期音を確認してから床へ寄せる。
                    // 初期床から+3dB/秒で這い上がる方式は、大きいノイズを声と
                    // 誤認すると数十秒間まったく働かなかった。声の周期があれば
                    // 時間が経っても更新せず、演奏をノイズへ取り込まない。
                    if ((! dnLearned || dnRelearn) && gateGain > 0.99f
                        && dnClassifier.isStationary() && dnFallRun[b] <= dnFallMax)
                    {
                        const float target = dnClassifier.floorTarget (b);
                        const float follow = dnLearned ? dnRelearnCoef : dnInitialFollowCoef;
                        dnFloor[b] += follow * (target - dnFloor[b]);
                        dnFloor[b] = juce::jlimit (1.0e-6f, 0.5f, dnFloor[b]);
                    }

                    const float openThr = dnFloor[b] * 2.5f;   // ~+8 dB above floor

                    // 高域は声の有無で二択に切り替えず、床からの余裕で連続的に減衰。
                    // 以前は母音中に -6dB まで必ず開いていたため、黙っている間に
                    // 抑えたノイズが声と一緒に戻っていた。子音の立ち上がりだけ保持する。
                    const bool protectBand = b == 3 ? dnHfVoiceHold > 0 : voiceOpen;
                    const float below = juce::jlimit (0.0f, 1.0f,
                                          (openThr - dnEnv[b]) / juce::jmax (openThr, 1e-9f));
                    const float targetGain = protectBand ? 1.0f
                        : juce::Decibels::decibelsToGain (-maxAttenDb * below);
                    const bool bandHasSignal = dnEnv[b] > dnFloor[b] * 1.3f;
                    const float upCoef = protectBand ? dnFastOpen
                                       : (bandHasSignal ? dnOpenCoef : dnSoftOpen);
                    const float coef = targetGain > dnGain[b] ? upCoef
                                     : (b == 3 ? dnHfDuckCoef : dnCloseCoef);
                    dnGain[b] += coef * (targetGain - dnGain[b]);
                    attenSum += 1.0f - dnGain[b];

                    for (int ch = 0; ch < numCh; ++ch)
                        bandBuf[b].setSample (ch, n,
                            bandBuf[b].getSample (juce::jmin (ch, 1), n) * dnGain[b]);
                }

                // 帯域ごとに間引くと1024の倍数が帯域0に偏り、残り3帯域が保存されない。
                if (dnLearned && dnRelearn && (++dnShareDecim & 0x3FF) == 0)
                    for (int b = 0; b < 4; ++b) dnFloorShared[b].store (dnFloor[b]);

                if (learn > 0) { if (++dnHistDecim >= 16) dnHistDecim = 0; --learn; }
            }

            if (learnCountdown.load() > 0)
            {
                learnCountdown.store (learn);
                if (learn <= 0)   // learning just finished: 中身を見てから決める
                {
                    // ---- v2.10.0 ★採用してよい学習かどうかを判定する ----
                    //  中央値 = 部屋のノイズの代表値。一瞬の物音では動かない。
                    //  山と谷の開き = その1.5秒がどれだけ静かだったか。
                    //  部屋のノイズは定常なので開きは小さい。声・息・イスが入ると開く。
                    float medDb[4] = { -120.0f, -120.0f, -120.0f, -120.0f };
                    float p95Db[4] = { -120.0f, -120.0f, -120.0f, -120.0f };
                    const int half = juce::jmax (1, dnHistCount / 2);
                    const int p95  = juce::jmax (1, (dnHistCount * 95) / 100);
                    for (int b = 0; b < 4; ++b)
                    {
                        int acc = 0; bool haveMed = false;
                        for (int i = 0; i < dnHistBins; ++i)
                        {
                            acc += dnHist[b][i];
                            if (! haveMed && acc >= half) { medDb[b] = (float) (i - 120); haveMed = true; }
                            if (acc >= p95) { p95Db[b] = (float) (i - 120); break; }
                        }
                    }
                    // 帯域をまたいだ代表値（いちばん大きい帯域で見る＝いちばん危ない側）
                    float loudestMed = -120.0f, widestSpread = 0.0f;
                    for (int b = 0; b < 4; ++b)
                    {
                        loudestMed   = juce::jmax (loudestMed,   medDb[b]);
                        widestSpread = juce::jmax (widestSpread, p95Db[b] - medDb[b]);
                    }
                    dnLearnLevelDb .store (loudestMed);
                    dnLearnSpreadDb.store (widestSpread);

                    // 大きいホワイトノイズも学習できる。声・演奏は周期性で拒否し、
                    // 変動の大きい音と過大入力も採用しない。
                    const bool tooLoud  = loudestMed > -12.0f;
                    // v4.3.1 ★「静かすぎる」門。これが無かった。
                    //  入力が来ていない状態（I/Fのミュート・入力chの選び間違い・
                    //  ケーブル未接続）で「ノイズを測る」を押すと、中央値が -120dB の
                    //  まま床 1e-6 が採用され、dnLearned=true で保存まで残る。
                    //  以後その床は openThr を本物のノイズより下へ置き続けるので、
                    //  ノイズ除去が完全に無処理になる（学びなおしOFFだと直る道が無い）。
                    //  2026-09-01「v4.0.1 でやる：ノイズ除去の根治」§B の根っこ。
                    const bool tooQuiet = loudestMed < kDenoiseMinLearnedFloorDb;
                    const bool notSteady = widestSpread > 12.0f || dnLearnPeriodicFrames > 0
                                        || dnLearnStationaryFrames == 0;

                    if (tooLoud || tooQuiet || notSteady)
                    {
                        dnLearnResult.store (tooLoud ? 2 : 3);     // 採用しない
                        // ★床には触らない。前の状態のまま（自動追従なら自動追従のまま）。
                    }
                    else
                    {
                        for (int b = 0; b < 4; ++b)
                        {
                            // 中央値 +3dB を床にする（従来は最大値×1.4＝+3dB相当）
                            // ★第2引数は「これ未満のdBは0とみなす」境界。gainではない。
                            const float med = juce::Decibels::decibelsToGain (medDb[b], -200.0f);
                            dnFloor[b] = juce::jmax (med * 1.4f, 1e-6f);
                            dnFloorShared[b].store (dnFloor[b]);   // expose for state save
                        }
                        dnLearned = true;
                        dnLearnedShared.store (true);
                        dnProfileValidation.store (1);
                        dnLearnResult.store (1);
                        markStateDirty();                          // autosave the new profile
                    }
                }
            }

            // recombine bands
            if (soujiRun && dnOn && amount > 0.001f)
            for (int ch = 0; ch < numCh; ++ch)
            {
                auto* out = buffer.getWritePointer (ch);
                for (int n = 0; n < numSamples; ++n)
                    out[n] = bandBuf[0].getSample (ch, n) + bandBuf[1].getSample (ch, n)
                           + bandBuf[2].getSample (ch, n) + bandBuf[3].getSample (ch, n);
            }

            meterDN.store (soujiRun && dnOn ? juce::jlimit (0.0f, 1.0f,
                               attenSum / (4.0f * (float) juce::jmax (1, numSamples))) : 0.0f);
        }
        else
            meterDN.store (meterDN.load() * 0.9f);
    }

    if (soujiRun) mods.restore (gz::ModuleChain::Souji, buffer);
    // ===== モジュール1「おそうじ」前半ここまで（ポップ・リップは後半にある）=====

    juce::dsp::AudioBlock<float> block (buffer);
    juce::dsp::ProcessContextReplacing<float> ctx (block);

    // ---- v1.8.0 voice changer (formant-preserving) + 5-voice unison ----
    // ===== モジュール2「へんしん」=====
    // ★ここだけ save/restore を使わない。この3機能（ボイス変換・ハモリ・ピッチ補正）
    //  だけが遅延(約16ms)を持つので、波形を後から差し替えると
    //  「申告した遅延」と「実際の遅延」がずれ、DAWの遅延補正でトラックが
    //  16ms早くなってしまう。だから**スイッチ側で切る**。切れば遅延も0に戻る。
    //  セッションモードが同じやり方で既に動いていて、実績がある。
    //  v3.0-c: 音には触らない「測るだけ」の対だけ足す（カードのミニメーター用）。
    mods.probeOnlyBegin (gz::ModuleChain::Henshin, buffer);
    {
        // v2.9.0 セッションモード: ONの間、遅延をふやす3機能はここで素通しにする。
        // スイッチ自体は触らない(セッションを抜けたら元どおり鳴る)。
        const bool sessionOn = apvts.getRawParameterValue ("session")->load() > 0.5f;
        sessionActive.store (sessionOn);

        // v2.10.0 ★アコギではピッチ補正・ハモリ・ボイス変換を通さない。
        //  どれも「声のピッチ」を前提にした処理で、和音が鳴るギターに掛けると
        //  検出が別の弦に飛び移って音程がふらつく。切れば遅延も 0 のままになる。
        // v3.0 モジュール「へんしん」。OFF のあいだは3つとも切れる＝遅延も0のまま。
        const bool henshinOn = mods.displayGain (gz::ModuleChain::Henshin) > 0.5f;

        const bool vcOn = (apvts.getRawParameterValue ("vc_on")->load() > 0.5f)
                            && henshinOn && ! sessionOn && srcPitchOk
                          #if VOCALGZZIO_LITE
                            && false   // Lite版は非搭載
                          #endif
                            ;
        const bool jnOn = (apvts.getRawParameterValue ("jn_on")->load() > 0.5f)
                            && henshinOn && ! sessionOn && srcPitchOk
                          #if VOCALGZZIO_LITE
                            && false   // Lite版は非搭載
                          #endif
                            ;
        const bool atOn = (apvts.getRawParameterValue ("at_on")->load() > 0.5f)
                            && henshinOn && ! sessionOn && srcPitchOk
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
            if (! pitchWasTracking)
            {
                pitchDet.reset();
                pitchCorrection.reset();
            }
            pitchWasTracking = true;
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

            // 音符ごとに中心を持ち、新しい音へ古い補正を持ち越さない。
            // 境界には小さな余裕を設け、検出の揺れで隣の音を行き来するのを抑える。
            const float midi = hz > 0.0f ? 69.0f + 12.0f * std::log2 (hz / refA) : 0.0f;
            float target = atOn ? pitchCorrection.target (midi, blockSec, key, scId, speed, amount) : 0.0f;
            if (! atOn) pitchCorrection.reset();
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
            const bool hard = atOn && (speed <= 8.0f) && (amount >= 0.90f);

            // retune-speed glide: tau grows with 'speed' (0 => instant snap,
            // 100 => ~180 ms smooth). Time constant is block-size independent.
            if (! atOn)
                atCorrection = 0.0f;  // ハモリ用の検出だけで補正を有効にしない
            else if (hard)
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
        else { atCorrection = 0.0f; atWetMix = 0.0f; pitchWasTracking = false; pitchCorrection.reset(); atDetectedHz.store (0.0f); atCurrentCorrection.store (0.0f);
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

    mods.probeOnlyEnd (gz::ModuleChain::Henshin, buffer);
    // ===== モジュール2「へんしん」ここまで =====

    // ===== モジュール1「おそうじ」後半（ポップ・リップ）=====
    // コードの並び上、へんしんを挟んで2か所に分かれている。フラグは同じなので、
    // ひとつのスイッチで前半・後半とも切れる（渡し具合もブロック単位でそろう）。
    if (soujiRun) mods.save (gz::ModuleChain::Souji, buffer);

    // ---- v2.3.0 ポップ(破裂音) / リップ(口の粘着音) 除去 ----
    // ローカットの手前で処理する。検出側は加工前の低域を見たほうが確実なため。
    if (soujiRun)
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

    if (soujiRun) mods.restore (gz::ModuleChain::Souji, buffer);
    // ===== モジュール1「おそうじ」ここまで =====

    // ===== モジュール3「ととのえ」（ローカット・こもり・キンキン・なめらか）=====
    // v3.0-c 順番の分岐: 既定は**圧縮の前**（いままでどおり）。
    //  「ととのえを圧縮の後へ」を選ぶと、ここは飛ばして音量そろえの後で呼ぶ。
    const bool totonoeRun = ! mods.isOff (gz::ModuleChain::Totonoe);   // v3.0-b OFFなら飛ばす
    const bool eqLate     = apvts.getRawParameterValue ("ord_eq")->load() > 0.5f;
    if (totonoeRun && ! eqLate)
    {
        mods.save (gz::ModuleChain::Totonoe, buffer);
        applyTotonoe (buffer);
        mods.restore (gz::ModuleChain::Totonoe, buffer);
    }
    // ===== モジュール3「ととのえ」ここまで（既定の場所）=====

    // ===== モジュール5「サ行おさえ」（分岐: 圧縮の前）=====
    //  「サ行おさえを圧縮の前へ」を選んだときだけ、ここで先に削る。
    //  圧縮がサ行に反応して音が波打つのを防ぎたい人向け。
    const bool sagyoRun   = ! mods.isOff (gz::ModuleChain::Sagyo);
    const bool deessEarly = apvts.getRawParameterValue ("ord_deess")->load() > 0.5f;
    if (sagyoRun && deessEarly)
    {
        mods.save (gz::ModuleChain::Sagyo, buffer);
        applyDeEsser (buffer);
        mods.restore (gz::ModuleChain::Sagyo, buffer);
    }


    // ===== モジュール4「音量そろえ」前半（圧縮1・2・SmartEQ）=====
    // v3.0-b OFF なら飛ばす。GRメーターも一緒に止めるので、切ったのに
    //  メーターだけ動いている、という嘘の表示にならない。
    const bool soroeRun = ! mods.isOff (gz::ModuleChain::Soroe);
    if (soroeRun) mods.save (gz::ModuleChain::Soroe, buffer);

    // ---- 2-stage compression with GR metering ----
    if (soroeRun)
    {
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
    }   // ← soroeRun（圧縮＋SmartEQ）

    if (soroeRun) mods.restore (gz::ModuleChain::Soroe, buffer);
    // ===== モジュール4 前半ここまで（音量キープはサ行おさえの後にある）=====

    // ===== モジュール5「サ行おさえ」（既定の場所: 圧縮の後）=====
    if (sagyoRun && ! deessEarly)
    {
        mods.save (gz::ModuleChain::Sagyo, buffer);
        applyDeEsser (buffer);
        mods.restore (gz::ModuleChain::Sagyo, buffer);
    }
    // ===== モジュール5「サ行おさえ」ここまで =====


    // ===== モジュール4「音量そろえ」後半（音量キープ）=====
    if (soroeRun) mods.save (gz::ModuleChain::Soroe, buffer);

    // ---- v2.4.0 音量キープ(自動ゲインライド / Vocal Rider 相当) ----
    // ならし圧縮(コンプ=速い波)とは別系統。300msのラウドネスを見て、目標
    // (-18dBFS RMS)へ±9dBの範囲でゆっくりフェーダーを動かす。無音や息つぎ
    // (-45dBFS未満)ではゲインを凍結するので、ノイズ床を持ち上げない。
    // ただの掛け算なのでゼロ遅延のまま。コンプとディエッサーの後に置く。
    if (soroeRun)
    {
        const float amt = apvts.getRawParameterValue ("ride_amt")->load() * 0.01f;
        if (amt > 0.001f)
        {
            auto* L = buffer.getWritePointer (0);
            auto* R = numCh > 1 ? buffer.getWritePointer (1) : nullptr;
            const float maxDb = 9.0f;
            // v3.0「つぶさない」: 音量キープは大きいところを**下げる**方向にも動く。
            //  張った1音はその曲の山であることが多いので、そこを下げると
            //  「盛り上がらない」＝これも潰れ。**下げる側だけ**上限を絞る。
            //  持ち上げる側（小さい声を聞こえるように）は触らない。
            const float downMax = maxDb * (1.0f - 0.75f * crushCg);
            for (int n = 0; n < numSamples; ++n)
            {
                const float x = (R != nullptr) ? 0.5f * (L[n] + R[n]) : L[n];
                rideEnv2 += rideRmsA * (x * x - rideEnv2);
                const float rmsDb = 10.0f * std::log10 (rideEnv2 + 1.0e-12f);
                if (rmsDb > -45.0f)   // 歌って/話しているときだけ動く
                {
                    const float want = juce::jlimit (-downMax, maxDb, -18.0f - rmsDb) * amt;
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

    if (soroeRun) mods.restore (gz::ModuleChain::Soroe, buffer);
    // ===== モジュール4「音量そろえ」ここまで =====

    // ===== モジュール3「ととのえ」（分岐: 圧縮の後）=====
    //  「削った帯で圧縮を動かしたくない」人向け。既定では通らない。
    if (totonoeRun && eqLate)
    {
        mods.save (gz::ModuleChain::Totonoe, buffer);
        applyTotonoe (buffer);
        mods.restore (gz::ModuleChain::Totonoe, buffer);
    }


    // ===== モジュール6「音色づくり」前半（ことば・ヌケ感・キラキラ・息・艶）=====
    // v3.0-b OFF なら飛ばす（区間は「仕上げ音量」をまたいで2か所に分かれる）
    const bool neiroRun = ! mods.isOff (gz::ModuleChain::Neiro);
    if (neiroRun) mods.save (gz::ModuleChain::Neiro, buffer);

    // ---- v2.6.0 ことば(子音エンハンサー) ----
    // コンプ・ディエッサーの「あと」に置く。コンプは母音を持ち上げて子音を
    // 相対的に埋めてしまうので、埋まった状態を見てから起こしたほうが正確。
    // ディエッサーより後なので、サ行を持ち上げ直してしまう心配もない。
    if (neiroRun)
    {
        // v2.10.0 ことばは「子音」を探す処理。アコギには子音が無いので切る。
        const float consAmt = srcVoiceOnly
                            ? apvts.getRawParameterValue ("cons_amt")->load() * 0.01f : 0.0f;
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
    if (neiroRun)
    {
        presence.process (ctx);
        air.process (ctx);
    }

    // ---- v2.0.0 息(Breath): 小声のときだけ息の帯域を持ち上げる ----
    // バラードの「ささやき」を近くに感じさせる定番処理。大声では何もしないので
    // 歯擦音がきつくならない(ディエッサーの逆向きの動き)。
    if (neiroRun)
    {
        // v2.10.0 息はささやき声のための処理。アコギ/しゃべりでは使わない。
        const float brAmt = srcBreathOk
                          ? apvts.getRawParameterValue ("br_amt")->load() * 0.01f : 0.0f;
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
    if (neiroRun)
    {
        // v2.10.0 艶は「母音のとき」を見て掛ける処理。アコギでは意味が無い。
        const float ringAmt = srcVoiceOnly
                            ? apvts.getRawParameterValue ("ring")->load() * 0.01f : 0.0f;   // 0..1
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

    if (neiroRun) mods.restore (gz::ModuleChain::Neiro, buffer);
    // ===== モジュール6 前半ここまで（あたたかみ・のびは仕上げ音量の後）=====

    makeup.process (ctx);

    // ---- Warmth + Sustain (のび) + dry/wet ----
    // v2.0.0: ハモだけ出力中は、Mixツマミの「原音を混ぜ戻す」側を無効にする。
    // ここで dry(=入力そのまま) が混ざると、せっかく消した原音が復活してしまう。
    const bool hamoDake = apvts.getRawParameterValue ("jn_solo")->load() > 0.5f
                       && apvts.getRawParameterValue ("jn_on")->load()   > 0.5f;
    const float wet = hamoDake ? 1.0f : apvts.getRawParameterValue ("mix")->load() * 0.01f;
    const float dry = 1.0f - wet;
    // v3.0「つぶさない」: あたたかみ(drive)と のび(sustain) は tanh の飽和なので、
    //  入力が大きいほど**波形の頭が丸くなる**＝これも「潰れ」の正体のひとつ。
    //  張っている間だけ量を引く（あたたかみは強く、のびは控えめに）。
    //  0%に落とさないのは、音色が張った瞬間だけ変わってしまうと不自然だから。
    const float driveAmt = apvts.getRawParameterValue ("drive")->load() * 0.01f
                             * (1.0f - 0.70f * crushCg);
    const float k = 1.0f + driveAmt * 5.0f;
    const float susAmt = apvts.getRawParameterValue ("sustain")->load() * 0.01f
                             * (1.0f - 0.50f * crushCg);

    // v2.8.0 ★Mixで混ぜ戻す原音を、加工側と同じだけ遅らせる。
    // ボイス変換/ピッチ補正/ハモリのどれかがONだと加工側は voiceLatency(約16ms)
    // 後ろにずれる。遅れていない原音をそこへ混ぜると 16ms のコムフィルタになり、
    // 62.5Hz おきに音が消える＝Mixを中間にしたときだけ「スカスカ」になっていた。
    // 原音は常にリングへ書き込み、必要なぶんだけ遅らせて読み出す。
    // (voiceLatency は prepareToPlay でしか変わらないので、ここは読むだけで安全)
    // 分割フィルターを足し戻した音は振幅が平坦でも位相が回っている。
    // 元の波形と混ぜると250/1200/5000Hz付近で打ち消しが起こるため、
    // 原音側にも同じ全域通過フィルターを通す。履歴はOFF中も進めておく。
    dnDryPhaseBuffer.makeCopyOf (dryBuffer, true);
    {
        juce::dsp::AudioBlock<float> phaseBlock (dnDryPhaseBuffer);
        juce::dsp::ProcessContextReplacing<float> phaseCtx (phaseBlock);
        dnDryPhase1.process (phaseCtx);
        dnDryPhase2.process (phaseCtx);
        dnDryPhase3.process (phaseCtx);
    }
    const bool dnMixPhase = soujiRun
        && apvts.getRawParameterValue ("dn_on")->load() > 0.5f
        && apvts.getRawParameterValue ("denoise")->load() * 0.01f * srcDnScale > 0.001f;
    // 0%は加工を混ぜないので、位相補償もせず原音の波形をそのまま返す。
    const juce::AudioBuffer<float>* dryMixSrc = dnMixPhase && wet > 0.0f
        ? &dnDryPhaseBuffer : &dryBuffer;
    if (voiceLatency > 0 && dryRing.getNumSamples() > 0
        && numSamples <= dryAligned.getNumSamples())
    {
        const int ringLen = dryRing.getNumSamples();
        const int lat     = juce::jmin (voiceLatency, ringLen - 1);
        for (int ch = 0; ch < juce::jmin (2, numCh); ++ch)
        {
            const float* s = dryMixSrc->getReadPointer (juce::jmin (ch, dryMixSrc->getNumChannels() - 1));
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

    // v3.0 ★ここは元は1つのループで「あたたかみ・のび」と「Mix(原音の混ぜ戻し)」を
    //  まとめてやっていた。Mix は**どのモジュールにも属さない全体設定**なので、
    //  音色づくりを切ったときに一緒に消えてはいけない。2つのループに割った。
    //  （numSamples は 512 以下なので、2周しても実測で差は出なかった）
    // ===== モジュール6「音色づくり」後半（あたたかみ・のび）=====
    if (neiroRun) mods.save (gz::ModuleChain::Neiro, buffer);
    for (int n = 0; neiroRun && n < numSamples; ++n)
    {
        float pk = 0.0f;
        for (int ch = 0; ch < numCh; ++ch)
        {
            float x = buffer.getSample (ch, n);
            if (driveAmt > 0.001f)
                x = std::tanh (x * k) / k;
            buffer.setSample (ch, n, x);
            pk = juce::jmax (pk, std::abs (x));
        }

        // 包絡と持ち上げ量は1サンプルに1回だけ更新し、左右へ同じ値を掛ける。
        // 以前は左を1ブロック処理した後、右全体へ最後のゲインを掛けていたため、
        // 同じ左右入力でも音像が揺れ、バッファを大きくすると差が増えていた。
        // 左右の最大値で検出するので、右だけの入力や逆相の入力も見落とさない。
        if (susAmt > 0.001f)
        {
            susEnv += (pk > susEnv ? susEnvAtk : susEnvRel) * (pk - susEnv);
            const float envDb = 20.0f * std::log10 (juce::jmax (susEnv, 1e-6f));
            float wantLift = 0.0f;
            if (envDb < -18.0f && envDb > -55.0f)
                wantLift = juce::jlimit (0.0f, 7.0f, (-18.0f - envDb) * 0.32f) * susAmt;
            susLift += 0.002f * (wantLift - susLift);

            const float lg = juce::Decibels::decibelsToGain (susLift);
            const float bias = 0.35f * susAmt;
            for (int ch = 0; ch < numCh; ++ch)
            {
                const float x = buffer.getSample (ch, n);
                const float sat  = std::tanh (x * lg * (1.0f + susAmt) + bias) - std::tanh (bias);
                buffer.setSample (ch, n, x * (1.0f - 0.5f * susAmt)
                    + sat * 0.5f * susAmt + x * (lg - 1.0f) * 0.6f);
            }
        }
    }
    if (neiroRun) mods.restore (gz::ModuleChain::Neiro, buffer);
    // ===== モジュール6「音色づくり」ここまで =====

    // ---- Mix(原音の混ぜ戻し) ---- ★モジュールに属さない全体設定
    for (int ch = 0; ch < numCh; ++ch)
    {
        auto* w = buffer.getWritePointer (ch);
        auto* d = dryMixSrc->getReadPointer (juce::jmin (ch, dryMixSrc->getNumChannels() - 1));
        for (int n = 0; n < numSamples; ++n)
            w[n] = w[n] * wet + d[n] * dry;
    }

    // ===== モジュール7「キャラ声」 =====
    // v3.0-c 分岐3: 「ひろがりを キャラ声の前へ」。
    //  既定(false)は いままでどおり キャラ声 → ひろがり。
    //  ON にすると ひろがり → キャラ声 になり、ロボ声やメガホンに
    //  残響が**後がけされなくなる**（＝声そのものが加工され、響きは素のまま）。
    //
    //  入れ替えても サビリフト／エモ が死なないように、
    //  ON のときは「測る所」だけ先に走らせる。ひろがりはその結果を読む。
    const bool spaceEarly = apvts.getRawParameterValue ("ord_space")->load() > 0.5f;
    applyChara (buffer, /*doFx*/ ! spaceEarly, /*doDetect*/ true);

    // 検出の結果。以前はキャラ声の区間で作られるローカル変数だった。
    // 関数へ出したので、ここで受け取ってから下の「ひろがり」で使う（名前も値も同じ）。
    const float emoBloomNow = emoBloomNowV;
    const float liftNow     = liftNowV;

    // ===== モジュール8「ひろがり」（かさね・ひろがり・コーラス・やまびこ・ひびき）=====
    // v3.0-b ★ここから「OFF なら**処理そのものを飛ばす**」。
    //  v3.0-a では走らせて出力を捨てていた（音は素通しだが CPU は減らなかった）。
    //  ひろがりから始めたのは、チェーンでいちばん重いから
    //  （ディレイと ひびき のディレイライン、コーラス4声、ダッキング検出）。
    //  飛ばすので save/restore も要らない ＝ buffer に一切触らない ＝ 完全な素通し。
    //  渡し中（クロスフェード）のあいだは今までどおり走らせて混ぜる。
    const bool spaceRun = ! mods.isOff (gz::ModuleChain::Hirogari);
    if (spaceRun) mods.save (gz::ModuleChain::Hirogari, buffer);

    // ---- Doubler (modulated short delay, mono-safe L/R inversion) + Width ----
    if (spaceRun)
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
    if (spaceRun)
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
    if (spaceRun)
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
    if (spaceRun)
    {
        // v2.10.0 しゃべり配信ではやまびこを切る(聞き取りを妨げるため)。
        const bool  dlyOn = apvts.getRawParameterValue ("dly_on")->load() > 0.5f && srcSpaceOk;
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
    // モジュール/用途のバイパスでも履歴を凍結させない。
    // 再ONの瞬間に以前の歌声が戻ることを防ぐ。
    if ((! spaceRun || ! srcSpaceOk) && revWasRunning)
        resetReverbState();
    if (spaceRun)
    {
        // ON/OFFの選択は、しゃべりを含む全用途で有効。
        const bool  revOn  = apvts.getRawParameterValue ("revon")->load() > 0.5f && srcSpaceOk;
        const float revMix = apvts.getRawParameterValue ("revmix")->load() * 0.01f;
        const bool revEnabled = revOn && revMix > 0.001f;
        if (revEnabled || revBypassGain > 0.0f)
        {
            revWasRunning = true;
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
                    const float inL = wL[n], inR = wR[n];
                    springLpL += lpA * (inL + dL * fb - springLpL);
                    springLpR += lpA * (inR + dR * fb - springLpR);
                    springBufL[(size_t) springW] = juce::jlimit (-1.5f, 1.5f, springLpL);
                    springBufR[(size_t) springW] = juce::jlimit (-1.5f, 1.5f, springLpR);
                    springW = (springW + 1) % sz;
                    wL[n] = inL * 0.45f + dL * 0.95f;   // drips dominate the tank feed
                    if (numCh > 1) wR[n] = inR * 0.45f + dR * 0.95f;
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

            if (rtype >= kHeyaFirst && heyaReady.load())
            {
                // v4.0.0 へや: 物理で作った部屋のインパルス応答をそのまま畳む。
                // 分割コンボリューションなので、ホストのブロック長がいくつでも
                // 追加遅延は0サンプル（tools/dsp_heya.cpp 検査1で直接畳み込みと一致）。
                // updateParameters でも渡しているが、ここでも当て直す。
                // （どちらも確保しない・例外を投げない。音声スレッドから呼んでよい）
                heyaRev.setRoom (rtype - kHeyaFirst);
                heyaRev.setSize (rsz);
                // モノ入力ではConvolver内部で左右の応答を平均する。
                float* wL = revWet.getWritePointer (0);
                float* wR = revWet.getWritePointer (juce::jmin (1, revWet.getNumChannels() - 1));
                heyaRev.process (wL, wR, numSamples);

                // ★「ひびき」の量をここで掛ける。
                //   従来型は juce::dsp::Reverb の wetLevel が中で掛けてくれるが、
                //   へやは tank を通らないので、掛ける人が誰もいなくなる。
                //   これを忘れると、revmix を絞っても響きが減らない（＝つまみが効かない）。
                const float wetG = juce::jlimit (0.0f, 1.0f, revMix);
                for (int ch = 0; ch < numCh; ++ch)
                {
                    float* w = revWet.getWritePointer (juce::jmin (ch, revWet.getNumChannels() - 1));
                    juce::FloatVectorOperations::multiply (w, wetG, numSamples);
                }
            }
            else
            {
                reverb.process (wc);   // dryLevel = 0 -> revWet now holds the wet signal only
            }

            // predelay per type (vocal practice: room 12 / plate 22 / hall 30 /
            // church 50 / spring 8 / shimmer 22 ms; normal keeps legacy = none)
            const int type = rtype;
            static const float preMs[7] = { 0.0f, 12.0f, 22.0f, 30.0f, 50.0f, 8.0f, 22.0f };
            // v4.0.0 へや: 部屋ごとのおすすめプリディレイは Heya.h の RoomSpec が持っている
            // （寸法から決めた値。ここで二重に持たない）。
            const float preThisType = (type >= kHeyaFirst)
                ? heya::rooms()[(size_t) juce::jlimit (0, heya::kNumRooms - 1, type - kHeyaFirst)].preDelayMs
                : preMs[juce::jlimit (0, 6, type)];
            const int preSamps = (int) (preThisType * 0.001f * currentSampleRate);
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
            // フェードは1サンプルにつき1度進め、左右に同じゲインを使う。
            for (int n = 0; n < numSamples; ++n)
            {
                revBypassGain = revEnabled ? juce::jmin (1.0f, revBypassGain + revBypassStep)
                                           : juce::jmax (0.0f, revBypassGain - revBypassStep);
                const float gain = duckGainBuf[(size_t) n] * revLift * revBypassGain;
                for (int ch = 0; ch < numCh; ++ch)
                    buffer.getWritePointer (ch)[n] += revWet.getReadPointer (ch)[n] * gain;
            }
            if (! revEnabled && revBypassGain == 0.0f)
                resetReverbState();
        }
    }

    if (spaceRun) mods.restore (gz::ModuleChain::Hirogari, buffer);
    // ===== モジュール8「ひろがり」ここまで =====

    // v3.0-c 分岐3: ON のときだけ、キャラ声をここで掛ける（ひろがりの**あと**）。
    //  測る所は上で済ませてあるので、ここは音を変える所だけ。
    //  既定(false)ではこの行は何もしない ＝ いままでと1サンプルも変わらない。
    if (spaceEarly) applyChara (buffer, /*doFx*/ true, /*doDetect*/ false);

    // ここから先（メーター・遅延自己証明・配信出力）は「しあげ」＝固定で常に通る。

    // ---- v3.0「つぶさない」の自動ヘッドルーム ----
    // つぶすのをやめれば、当然ピークは伸びる。そのまま 0 dBFS に当たれば
    // **いちばん汚い潰れ方（デジタルクリップ）**になるので、手前で音量を下げる。
    //
    // ★これはリミッタではない。リミッタは「はみ出した頭だけ」を潰す道具で、
    //   それこそが今回いただいた「潰れる」の正体。ここでやるのは
    //   **曲全体をゆっくり下げるフェーダー操作**で、波形の形は変えない。
    //   下げ 20ms / 戻し 1.5秒。この速さだと1音の中では動かないので、
    //   アタックもビブラートもそのまま残る。
    //   代わりに、速い1発の頭は数ms分すり抜ける。そこは意図どおり
    //   （DAW内部は float なので 0dBFS を超えても壊れない。書き出しの前に
    //     この表示を見て仕上げ音量を下げてください、という設計）。
    if (crushOn)
    {
        float pk = 0.0f;
        for (int ch = 0; ch < juce::jmin (numCh, 2); ++ch)
            pk = juce::jmax (pk, buffer.getMagnitude (ch, 0, numSamples));
        const float ceilingDb = -0.5f;                       // ここより上には出さない
        const float pkDb   = juce::Decibels::gainToDecibels (pk, -80.0f);
        const float wantDb = juce::jmin (0.0f, ceilingDb - pkDb);   // 下げるだけ
        const float a = (wantDb < crushHeadDb) ? crushHeadAtk : crushHeadRel;
        crushHeadDb += a * (wantDb - crushHeadDb);
        if (crushHeadDb < -0.05f)
        {
            const float g = juce::Decibels::decibelsToGain (crushHeadDb);
            for (int ch = 0; ch < numCh; ++ch)
                juce::FloatVectorOperations::multiply (buffer.getWritePointer (ch), g, numSamples);
        }
        // 画面に出す「いま守っている量」= 圧縮を緩めたぶん + 下げたぶん
        crushMeterDb.store (6.0f * crushCg - crushHeadDb);
    }
    else
    {
        crushHeadDb = 0.0f;
        crushMeterDb.store (0.0f);
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

    // ---- v2.10.0 #73 ゼロ遅延の自己証明（出口） ----
    // 出てきた山のいちばん高い場所を覚え、インパルスを入れた場所との差を取る。
    // そのあと**出力を消す**ので、測っている間は無音になる。
    if (selfTestRunning.load (std::memory_order_relaxed))
    {
        const int pos = selfTestPos.load (std::memory_order_relaxed);
        const auto* d = buffer.getReadPointer (0);
        for (int n = 0; n < numSamples; ++n)
        {
            const float a = std::abs (d[n]);
            if (a > selfTestBest) { selfTestBest = a; selfTestBestAt = pos + n; }
        }
        for (int ch = 0; ch < numCh; ++ch)
            juce::FloatVectorOperations::clear (buffer.getWritePointer (ch), numSamples);

        const int next = pos + numSamples;
        selfTestPos.store (next, std::memory_order_relaxed);
        if (next >= (int) (currentSampleRate * 1.0))
        {
            selfTestMeasured.store ((selfTestBestAt >= 0 && selfTestBest > 1.0e-6f)
                                        ? (selfTestBestAt - selfTestImpactAt) : -1);
            selfTestBest = 0.0f; selfTestBestAt = -1;
            selfTestRunning.store (false, std::memory_order_relaxed);
        }
    }



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
        // v2.10.0 ★符号が逆だった。mud/harsh のツマミは -12..0 dB(マイナスが「削る」)
        //  なのに、おまかせは 0..8 の**プラス**を書き込んでいた。範囲に丸められて
        //  必ず 0 になるので、**おまかせのこもり取り・かたさ取りは一度も効いていなかった**。
        //  「おまかせを掛けてもシャリつきが取れない」の一因。マイナスで書く。
        setP ("mud",   -juce::jlimit (0.0f, 8.0f, (mudRatio - 0.35f) * 22.0f));
        setP ("harsh", -juce::jlimit (0.0f, 7.0f, (pres / juce::jmax (1e-4f, highSum) - 0.4f) * 20.0f));
        setP ("presence", juce::jlimit (0.5f, 4.0f, (0.30f - pres / juce::jmax (1e-4f, highSum)) * 12.0f + 1.5f));
        setP ("air",   juce::jlimit (1.0f, 6.0f, (0.28f - airE / juce::jmax (1e-4f, highSum)) * 22.0f + 1.0f));
        // the WARMTH knob is the "drive" parameter (soft saturation): thin voices
        // get more body, already-warm voices keep it light
        // v2.12.0 ★「最低でも12%必ず入れる」をやめた(§6-4)。これが
        // 「うた自動があたたかい声しか出ない」の半分だった。連続値にして、
        // 胴の鳴っている声(bodyLow>=0.17)には 0% = 何も足さない。
        setP ("drive", juce::jlimit (0.0f, 28.0f, (0.17f - bodyLow) * 280.0f));

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

        // result: 10..12 = sing done, +100 if LEARN is recommended
        // v2.12.0 ★判定を作り直した(§6-4)。以前は「高域の割合 − 低域の割合」を
        // ±0.15 で3択にしていたが、人の声はエネルギーの大半が低域にあるので、
        // この式ではほぼ全員が「あたたかい」に落ちていた(=あたたかい一辺倒の残り半分)。
        // 声の標準傾斜ぶん(低域優位・実測でおよそ+8dB)を差し引いた対数比較にする。
        const float brightDb = 10.0f * std::log10 (juce::jmax (1.0e-4f, highSum)
                             / juce::jmax (1.0e-4f, rumble + bodyLow + mud));
        // 補正値+12dB: 標準的な声(-6dB/octの倍音列)でこの比が約-12dBになることを
        // dsp_autoset で実測して合わせた。ここが0になる声=ふつう。
        const float delta = brightDb + 12.0f;                    // + = 標準より明るい
        asBrightDb.store (delta);
        asSibPct.store (100.0f * sib / juce::jmax (1.0e-4f, highSum));
        int r = delta > 2.0f ? 10 : delta < -2.0f ? 11 : 12;
        if (dn >= 18.0f) r += 100;                               // noisy room: suggest LEARN
        autoSetupResult.store (r);
        return;
    }

    // low cut: more rumble -> higher HPF (bounded to the musical 70-120 Hz zone)
    setP ("lowcut", juce::jlimit (70.0f, 120.0f, 80.0f + rumble * 400.0f));

    // mud dip: only if 250-500 Hz dominates the low end
    const float mudRatio = mud / juce::jmax (1e-4f, bodyLow + midE);
    setP ("mud", -juce::jlimit (0.0f, 8.0f, (mudRatio - 0.35f) * 22.0f));   // v2.10.0 符号を直した(上記)

    // harshness: strong presence share invites a gentle 3-5 kHz cut
    setP ("harsh", -juce::jlimit (0.0f, 7.0f, (pres / juce::jmax (1e-4f, highSum) - 0.4f) * 20.0f));   // v2.10.0 符号を直した

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
    // v3.0 トークは長丁場。部屋（PCファン・エアコン）が途中で変わっても
    // ついていけるよう、静かな間の自動学びなおしを入れる
    setP ("dn_relearn", 1.0f);

    // pick the nearest mic-preset tilt (bright vs warm vs neutral)
    // v2.12.0 ★うた側と同じ作り直し(§6-4)。生の割合差は声の低域優位で
    // ほぼ常に「あたたかい」判定になっていた。標準傾斜との差で見る。
    int nearest = -1;
    const float brightDb = 10.0f * std::log10 (juce::jmax (1.0e-4f, highSum)
                         / juce::jmax (1.0e-4f, rumble + bodyLow + mud));
    const float delta = brightDb + 12.0f;   // 校正は dsp_autoset(うた側と共通)
    asBrightDb.store (delta);
    asSibPct.store (100.0f * sib / juce::jmax (1.0e-4f, highSum));
    if      (delta >  2.0f) nearest = 0;   // bright/condenser-ish
    else if (delta < -2.0f) nearest = 1;   // warm/dynamic-ish
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
    // 音声処理は待たせない。描画側がコピー中の更新を検知したら取り直す。
    for (int attempt = 0; attempt < 3; ++attempt)
    {
        const unsigned before = tunerSequence.load (std::memory_order_acquire);
        if ((before & 1u) != 0) continue;
        const int pos = tunerPos.load (std::memory_order_acquire);
        for (int i = 0; i < tunerSize; ++i)
            dest[(size_t) i] = tunerBuf[(pos + i) % tunerSize].load (std::memory_order_relaxed);
        std::atomic_thread_fence (std::memory_order_acquire);
        if (before == tunerSequence.load (std::memory_order_acquire)) return;
    }
    std::fill (dest.begin(), dest.end(), 0.0f);
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
            case maRevType: cycleChoice ("rev_type", kRevTypeCount, +1); break;
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
    root->setAttribute ("ver", 4);   // v3.0-c: 3 以降は「直ったサビリフト」
                                     // v4.0.0: 4 以降は「学びなおし既定ON」
    if (auto params = apvts.copyState().createXml())
        root->addChildElement (params.release());
    auto* d = root->createNewChildElement ("DENOISE");
    d->setAttribute ("learned", dnLearnedShared.load());
    d->setAttribute ("validation", dnProfileValidation.load());
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
    // v3.1 ★状態を読み込むときは「使いかたごとの記憶」を必ず1回空振りさせる。
    //  読み込みで src_mode が変わると、次の呼び出しが
    //  「読み込む前の値」で「読み込んだばかりの記録」を上書きしてしまう。
    //  false にしておけば、次の1回は基準を取り直すだけで終わる。
    useModeArmed = false;
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

        // ---- v3.0-c 古いプロジェクトは、昔の音のまま開く -------------------
        //  v3.0-c で「キャラ声を切るとサビリフトが残響に効かない」を直した。
        //  直したこと自体は正しいが、**すでに保存されている曲の音を変えては
        //  いけない**。保存に付いている ver が 3 未満なら、そのインスタンスだけ
        //  昔の動き(lift_legacy = ON)にして開く。
        //  新しく置いたときは既定の OFF ＝ 直った動きになる。
        //  ※ ver は保存時に必ず書かれるので、無いものは古いとみなす。
        if (xml.getIntAttribute ("ver", 0) < 3)
            if (auto* prm = apvts.getParameter ("lift_legacy"))
                prm->setValueNotifyingHost (1.0f);

        // ---- v4.0.0 「自動学びなおし」を既定 ON にした ---------------------
        //  ★既定を変えただけでは、v3.1 から使っている人は直らない。
        //    保存には false が**書いてある**ので、replaceState でそれが戻る。
        //  ver < 4 の保存を読んだときだけ、一度だけ ON へ上げる。
        //  上書きしてよい理由: この項目は v3.0 で足したばかりで、
        //  トーク配信のおまかせを押した人以外は触ったことがない。
        //  そして静かな部屋では ON にしても音は変わらない。
        if (xml.getIntAttribute ("ver", 0) < 4)
            if (auto* prm = apvts.getParameter ("dn_relearn"))
                prm->setValueNotifyingHost (1.0f);
        if (auto* d = xml.getChildByName ("DENOISE"))
        {
            bool  learned = d->getBoolAttribute ("learned", false);
            float f[4];
            for (int b = 0; b < 4; ++b)
                f[b] = (float) d->getDoubleAttribute ("f" + juce::String (b), 1e-5);

            // 新しい測定は音量だけで拒否しない。旧版の危険な学習は従来通り
            // 捨てる。NaN・無限大・範囲外の値は検証方式にかかわらず採用しない。
            const int validation = d->getIntAttribute ("validation", 0);
            bool validFloor = true;
            float loudest = 0.0f;
            for (float floor : f)
            {
                validFloor = validFloor && std::isfinite (floor) && floor >= 1e-6f && floor <= 0.5f;
                loudest = juce::jmax (loudest, floor);
            }
            // v4.3.1 ★静かすぎる床は「学習済み」として採用しない。
            //  うるさすぎる側しか弾いていなかったため、過去に無入力で測って
            //  しまった床がそのまま復元され、ノイズ除去が効かないままになる。
            //  スタンドアロンは設定ファイルから必ずこの道を通るので、
            //  一度書き込まれると再インストールしても直らなかった。
            //  採用しない場合は未学習に戻すだけで、自動追従がすぐ測り直す。
            const float minLearnedFloor =
                juce::Decibels::decibelsToGain (kDenoiseMinLearnedFloorDb) * 1.4f;
            const bool tooQuietFloor = loudest < minLearnedFloor;
            if (! validFloor || tooQuietFloor || (learned && validation < 1
                && juce::Decibels::gainToDecibels (loudest, -120.0f) > -45.0f))
            {
                learned = false;
                for (float& floor : f) floor = 1e-5f;
            }
            dnProfileValidation.store (learned ? validation : 0);

            dnLearnedShared.store (learned);
            for (int b = 0; b < 4; ++b) dnFloorShared[b].store (f[b]);
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

// v2.10.0 #74 報告セット: 版・環境・いまの設定・使われ方を1枚にまとめる。
// 「再現しません」の往復を減らすための道具。**ネットワークは使わない**。
juce::String VocalGzzioProcessor::buildReportText() const
{
    juce::String s;
    s << "VocalGzzio 不具合報告セット\n";
    s << "====================================\n";
    s << "このファイルは自動送信されません。あなたが送ったときだけ作者に届きます。\n";
    s << "中身はぜんぶ文字なので、送る前にそのまま読めます。\n\n";

    s << "[版と環境]\n";
    s << "  VocalGzzio : " << JucePlugin_VersionString << "\n";
    s << "  形式       : " << juce::AudioProcessor::getWrapperTypeDescription (wrapperType) << "\n";
    s << "  ホスト     : " << juce::PluginHostType().getHostDescription() << "\n";
    s << "  OS         : " << juce::SystemStats::getOperatingSystemName() << "\n";
    s << "  CPU        : " << juce::SystemStats::getCpuModel()
      << " (" << juce::SystemStats::getNumCpus() << " コア)\n";
    s << "  メモリ     : " << juce::SystemStats::getMemorySizeInMegabytes() << " MB\n";
    s << "  書き出し日 : " << juce::Time::getCurrentTime().toISO8601 (true) << "\n\n";

    s << "[音の設定]\n";
    s << "  サンプリング周波数 : " << juce::String (getSampleRate(), 0) << " Hz\n";
    s << "  ブロック           : " << getBlockSize() << " サンプル";
    if (getSampleRate() > 0.0)
        s << " (" << juce::String (1000.0 * getBlockSize() / getSampleRate(), 2) << " ms)";
    s << "\n";
    s << "  申告している遅延   : " << reportedLatency.load() << " サンプル\n";
    s << "  セッションモード   : " << (sessionActive.load() ? "入" : "切") << "\n\n";

    s << "[使われ方] ※手元に貯めているだけの数字です\n";
    s << "  はじめて使った日 : " << usageLog.getFirstSeen() << "\n";
    s << "  起動した回数     : " << usageLog.getLaunches() << "\n";
    {
        const auto& all = usageLog.all();
        const auto keys = all.getAllKeys();
        if (keys.isEmpty()) s << "  (まだ記録がありません)\n";
        for (const auto& k : keys)
        {
            const double sec = all[k].getDoubleValue();
            if (sec < 1.0) continue;
            s << "  " << k.paddedRight (' ', 16) << " : "
              << juce::String (sec / 60.0, 1) << " 分\n";
        }
    }
    s << "\n[いまの設定]\n";
    // copyState() は非 const なので、ここでは値ツリーをそのまま複製して書き出す
    // （中身は同じ。const のまま読める形にしている）。
    if (auto xml = apvts.state.createXml())
        s << xml->toString() << "\n";

    return s;
}

juce::File VocalGzzioProcessor::autosaveFile()
{
    return gz::dataDirectory().getChildFile ("autosave.xml");
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
    // v3.1 使いかたが変わっていたら、その使いかたの値を出し入れする。
    //  画面のコンボからも即座に同じものを呼ぶので、ふつうはここは空振りする。
    //  ここが要るのは「画面が開いていないのにホストのオートメーションで
    //  使いかたが動いた」ときだけ。
    applyUseModeMemoryIfChanged();

   #if VOCALGZZIO_TRIAL
    stateDirty.store (false);      // 体験版は autosave を書かない
   #else
    if (stateDirty.load()
        && juce::Time::getMillisecondCounter() - lastDirtyMs.load() > 1200)
        flushAutosaveNow();
   #endif
}

//==============================================================================
// v3.1「使いかた4種」— 使いかたごとにツマミの値を別に覚える（設計書§3）
//
//  記録は apvts.state の子 "usemode_0".."usemode_3" に、
//  パラメータの**正規化値(0..1)**で入れる。正規化値にしておくと、
//  あとで範囲を変えても壊れない（dB や Hz の生値で持つと範囲変更で意味が変わる）。
//  apvts.state はまるごと保存されるので、プロジェクトを開き直しても残る。
//
//  ★覚えない物は useModeMemorySkips() にまとめてある（曲そのものの設定・
//   互換スイッチ・その場のスイッチ）。ここを甘くすると、使いかたを変えただけで
//   「古い曲を昔の音で開く」設定まで動いてしまう。
void VocalGzzioProcessor::snapshotUseMode (int mode)
{
    if (mode < 0 || mode > 9) return;
    auto child = apvts.state.getOrCreateChildWithName ("usemode_" + juce::String (mode), nullptr);
    for (auto* prm : getParameters())
        if (auto* wp = dynamic_cast<juce::AudioProcessorParameterWithID*> (prm))
            if (! useModeMemorySkips (wp->paramID))
                child.setProperty (wp->paramID, wp->getValue(), nullptr);
}

bool VocalGzzioProcessor::restoreUseMode (int mode)
{
    if (mode < 0 || mode > 9) return false;
    auto child = apvts.state.getChildWithName ("usemode_" + juce::String (mode));
    if (! child.isValid()) return false;      // その使いかたは初めて＝いまの音のまま

    for (auto* prm : getParameters())
        if (auto* wp = dynamic_cast<juce::AudioProcessorParameterWithID*> (prm))
        {
            if (useModeMemorySkips (wp->paramID)) continue;
            if (! child.hasProperty (wp->paramID)) continue;
            const float v = juce::jlimit (0.0f, 1.0f, (float) (double) child.getProperty (wp->paramID));
            //  同じ値なら触らない。触るとホストの「変更あり」印が付き、
            //  オートメーションにも無意味な点が並ぶ。
            if (std::abs (v - wp->getValue()) > 1.0e-6f)
            {
                wp->beginChangeGesture();
                wp->setValueNotifyingHost (v);
                wp->endChangeGesture();
            }
        }
    return true;
}

void VocalGzzioProcessor::applyUseModeMemoryIfChanged()
{
    const int cur = (int) apvts.getRawParameterValue ("src_mode")->load();

    //  ★状態を読み込んだ直後の1回は、必ず空振りさせる。
    //   ここで空振りさせないと、「読み込む前の値」で「読み込んだばかりの記録」を
    //   上書きしてしまう（＝プロジェクトを開くたびに設定が壊れる）。
    if (! useModeArmed)
    {
        useModeArmed = true;
        useModeLast  = cur;
        return;
    }
    if (cur == useModeLast) return;

    snapshotUseMode (useModeLast);   // 出ていく使いかたを覚える
    restoreUseMode  (cur);           // 入る使いかたを思い出す（初めてなら何もしない）
    useModeLast = cur;
}

void VocalGzzioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
   #if VOCALGZZIO_TRIAL
    // 体験版は設定を保存しない。空ではなく「体験版でした」だけを書く
    // （製品版がこれを読んでも既定のまま＝事故にならない）。
    juce::XmlElement t ("VOCALGZZIO_TRIAL_NOSAVE");
    copyXmlToBinary (t, destData);
    return;
   #endif
    if (auto xml = makeStateXml())
        copyXmlToBinary (*xml, destData);
}

void VocalGzzioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
   #if VOCALGZZIO_TRIAL
    juce::ignoreUnused (data, sizeInBytes);   // 体験版は読みもしない（毎回まっさら）
    return;
   #endif
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
