#include <cmath>
#include <juce_audio_formats/juce_audio_formats.h>
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
    {
        auto f = autosaveFile();
        bool loadedFromStandaloneSettings = false;
        if (! gz::isTestDataIsolated())
        {
            const auto sf = gz::dataDirectory().getChildFile ("VocalGzzio.settings");
            if (sf.existsAsFile() && (! f.existsAsFile() || sf.getLastModificationTime() > f.getLastModificationTime()))
            {
                if (auto sxml = juce::XmlDocument::parse (sf))
                {
                    for (auto* v = sxml->getFirstChildElement(); v != nullptr; v = v->getNextElement())
                    {
                        if (v->hasTagName ("VALUE") && v->getStringAttribute ("name") == "filterState")
                        {
                            juce::MemoryBlock mb;
                            if (mb.fromBase64Encoding (v->getStringAttribute ("val")) && mb.getSize() > 0)
                            {
                                if (auto stateXml = getXmlFromBinary (mb.getData(), (int) mb.getSize()))
                                {
                                    applyStateXml (*stateXml);
                                    flushAutosaveNow();
                                    loadedFromStandaloneSettings = true;
                                }
                            }
                            break;
                        }
                    }
                }
            }
        }
        if (! loadedFromStandaloneSettings && f.existsAsFile())
            if (auto xml = juce::XmlDocument::parse (f))
                applyStateXml (*xml);

        if (gz::isTestDataIsolated())
        {
            legacyVersionId.store  (0,    std::memory_order_relaxed);
            legacyVersionEra.store (510,  std::memory_order_relaxed);
            legacyEcoGate.store    (true, std::memory_order_relaxed);
        }
    }
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
    // v5.1.1: 18項目へ倍増（9..17を末尾に追加し、既存の0..8のインデックス互換を完全維持）。
    layout.add (std::make_unique<juce::AudioParameterChoice>(pid ("jn_harm"), "Unison Harmony",
                    juce::StringArray { "Unison", "3rd up", "3rd down", "6th up",
                                        "6th down", "5th up", "3rd + 5th", "Auto flip",
                                        "Contrary", "Octave down", "Octave up", "Octave W",
                                        "3rd W (up+down)", "4th up", "5th down",
                                        "3-voice Choir", "4-voice Gospel", "Pedal Drone" }, 0));
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

    // ---- v4.4.0/v4.5.0 Argon Velvet Engine (母音感応2次倍音・声芯フォーカス・シルク空気感・モノラル互換バイノーラルHalo) ----
    // 既定値は 0% ＝ 既存のプリセットや回帰検査はビット単位で完全に同じ音を保つ。
    // 2D Voice Gravity Orb や Argon Target・自動設定を動かしたときに滑らかに働く。
    layout.add (std::make_unique<P>(pid ("argon_velvet"), "Argon Velvet",   R (0.0f, 100.0f, 1.0f), 0.0f, unit ("%")));
    layout.add (std::make_unique<P>(pid ("argon_silk"),   "Argon Silk Air", R (0.0f, 100.0f, 1.0f), 0.0f, unit ("%")));
    layout.add (std::make_unique<P>(pid ("argon_halo"),   "Argon 3D Halo",  R (0.0f, 100.0f, 1.0f), 0.0f, unit ("%")));
    // 既存パラメーターの順序と AU オートメーションを保つため末尾に追加し、
    // 新規パラメーターだけ版ヒントを上げる（既存は 1 のまま）。
    layout.add (std::make_unique<P>(juce::ParameterID { "argon_focus", 2 },
                                  "Argon Focus", R (0.0f, 100.0f, 1.0f), 0.0f, unit ("%")));

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
            if (rmsDb > -35.0f) emoHoldSec = juce::jmin (emoHoldSec + blockSec, 60.0f);   // v5.1.3 長時間でも飽和しない上限
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

    const int eraNow = legacyEcoGate.load (std::memory_order_relaxed)
                           ? legacyVersionEra.load (std::memory_order_relaxed) : 500;

    // ---- v2.4.0 なめらか(動的レゾナンス抑制) ----
    // 「こもり」「キンキン」(固定EQ)のあと・コンプの前。コンプより前に刺さりを
    // 削っておかないと、コンプが刺さりごと持ち上げてしまうため。ゼロ遅延。
    const float resAmt = (eraNow >= 240)
                       ? apvts.getRawParameterValue ("res_amt")->load() * 0.01f : 0.0f;
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
    const float pickAmt = (srcPickOk && eraNow >= 310)
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
    // v4.0.0 へや + 遅延構築: バッファ領域だけ prepare で一括確保し、
    // 6部屋の重いIR合成は「実際に選ばれた部屋だけ」オンデマンドで1部屋ずつ構築する。
    // 初期状態（アルゴリズム式リバーブ）では起動時の6部屋一括生成コストをゼロにする。
    heyaRev.prepare (sampleRate, samplesPerBlock, true);
    {
        const int initRevType = (int) apvts.getRawParameterValue ("rev_type")->load();
        if (initRevType >= kHeyaFirst)
            heyaRev.ensureRoomBuilt (initRevType - kHeyaFirst);
    }
    heyaReady.store (true);

    // ---- v1.8.0 voice changer + unison ----
    const int initWinOrd = gz::VoiceShifter::windowOrderForLatencyMode (sampleRate, pitchLatMode.load (std::memory_order_relaxed));
    const int initUnOrd  = std::max (initWinOrd, gz::VoiceShifter::windowOrderForLatencyMode (sampleRate, 1));
    for (auto& s : vcSh) { s.prepare (sampleRate); s.setWindow (initWinOrd); }
    for (auto& s : unSh) { s.prepare (sampleRate); s.setWindow (initUnOrd); }
    for (auto& s : tdSh) s.prepare (sampleRate);   // v5.1.4 ピッチ補正専用の超低遅延エンジン(4サンプル=0.08ms)
    tdWasActive = false;
    tdBodyLp[0] = tdBodyLp[1] = 0.0f;
    // v2.12.0 無声ガード(§6-3): 本人の声(ボイス変換・ピッチ補正)は子音を素通し、
    // ユニゾン/ハモリの分身は子音を黙らせる(4人分の「サッ」が重ならないように)。
    for (auto& s : vcSh) s.setUnvoicedGuard (1);
    for (auto& s : unSh) s.setUnvoicedGuard (2);
    for (int ch = 0; ch < 2; ++ch)
    {
        vcNatHp[ch].reset();
        vcNatBody[ch].reset();
        vcNatDeHarsh[ch].reset();
    }
    vcNatLastPitch = -999.0f;
    vcNatLastForm  = -999.0f;
    vcNatLastSr    = sampleRate;
    for (float& hs : jnHeldSemi) hs = 0.0f;
    jnPedalMidi = 60.0f;
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

    const int maxCh = juce::jmax (2, getTotalNumInputChannels(), getTotalNumOutputChannels());
    dryBuffer.setSize (maxCh, samplesPerBlock, false, false, true);
    scratch.setSize   (maxCh, samplesPerBlock, false, false, true);
    scratch2.setSize  (maxCh, samplesPerBlock, false, false, true);   // v3.1 2点目の側鎖
    // v3.0 モジュールの退避先。音声コールバックでは一切確保しないので、ここで取る。
    mods.prepare (sampleRate, samplesPerBlock, maxCh);

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
    dnDryPhaseBuffer.setSize (maxCh, samplesPerBlock, false, false, true);
    for (auto& b : bandBuf) b.setSize (maxCh, samplesPerBlock, false, false, true);
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
    // survives prepareToPlay (levels are sample-rate independent).
    // ASIO (FireFace UC等) や SONAR 等の再初期化で prepareToPlay が先に呼ばれた場合も、
    // 保留中のプロファイル復元や解除コマンドをここで確実に反映する。
    if (dnProfilePending.exchange (false, std::memory_order_acquire))
    {
        dnLearned = dnLearnedShared.load();
        for (int b = 0; b < 4; ++b) dnFloor[b] = dnFloorShared[b].load();
    }
    int pendingClear = -1;
    if (dnLearnCommand.compare_exchange_strong (pendingClear, 0, std::memory_order_acq_rel))
    {
        dnLearned = false;
        dnLearnedShared.store (false);
        dnProfileValidation.store (0);
        for (int b = 0; b < 4; ++b) { dnFloor[b] = 1e-5f; dnFloorShared[b].store (1e-5f); }
    }
    if (! dnLearned)
        for (int b = 0; b < 4; ++b) dnFloor[b] = 1e-5f;
    learnCountdown.store (0);
    // 操作側が新しく送った学習／解除は消さず、次の音声ブロックで処理する。

    // ---- sustain constants ----
    susEnvAtk = 1.0f - std::exp (-1.0f / (0.005f * (float) sampleRate));
    susEnvRel = 1.0f - std::exp (-1.0f / (0.180f * (float) sampleRate));
    susEnv = 0; susLift = 0;
    warmPrevU[0] = warmPrevU[1] = 0.0f;
    susPrevU[0] = susPrevU[1] = 0.0f;

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
    argonEngine.prepare (sampleRate);
    meterArgonVelvet.store (0.0f); meterArgonFocus.store (0.0f); meterArgonSilk.store (0.0f);
    meterArgonHalo.store (0.0f);   meterArgonVoiced.store (0.0f);

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
        // v5.1.1: 実行中に 💎最高音質モード(N=2048) へ切り替えても再確保不要なよう、最大窓サイズで確保する。
        const int maxLat = (1 << gz::VoiceShifter::recommendedWindowOrder (sampleRate));
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
    meterDuckDb.store (0.0f, std::memory_order_relaxed);

    // v4.7.0 5秒試聴ルーパーのステレオバッファを事前確保（音声スレッドでの確保ゼロ）
    auditionMaxSamples = juce::jmax (4096, (int) std::lround (5.0 * sampleRate));
    auditionBuf.setSize (2, auditionMaxSamples, false, true, true);
    auditionBuf.clear();
    auditionState.store (AuditionOff, std::memory_order_relaxed);
    auditionPos.store (0, std::memory_order_relaxed);
    auditionLen.store (0, std::memory_order_relaxed);

    // v4.8.0 咳払い瞬間ミュート ＆ 絶叫セーフティガード ＆ カラオケBGM ＆ 30秒タイムマシン録音バッファ
    coughMuteGain = coughMute.load (std::memory_order_relaxed) ? 0.0f : 1.0f;
    coughMuteStep = (float) (1.0 / juce::jmax (32.0, 0.008 * sampleRate));
    screamGuardEnv = 0.0f;
    meterScreamGuardDb.store (0.0f, std::memory_order_relaxed);
    floatPadGain = 1.0f;
    feedbackHoldSamples = 0;
    for (auto& e : inRouteEnergy) e = 0.0;

    bgmPitchRingL.assign ((size_t) ((int) (0.120 * sampleRate) + 16), 0.0f);
    bgmPitchRingR.assign (bgmPitchRingL.size(), 0.0f);
    bgmPitchRingW = 0;
    bgmPitchPhase = 0.0f;
    bgmDuckEnv = 0.0f;
    bgmDuckGain = 1.0f;
    meterBgmDuckDb.store (0.0f, std::memory_order_relaxed);
    bgmBlockScratch.setSize (2, juce::jmax (samplesPerBlock * 2, 8192), false, true, true);

    retroMaxSamples = juce::jlimit (48000, 48000 * 30, (int) std::lround (30.0 * sampleRate));
    if (retroDryBuf.getNumSamples() != retroMaxSamples)
    {
        retroDryBuf.setSize (2, retroMaxSamples, false, true, true);
        retroWetBuf.setSize (2, retroMaxSamples, false, true, true);
        retroBgmBuf.setSize (2, retroMaxSamples, false, true, true);
        retroDryBuf.clear();
        retroWetBuf.clear();
        retroBgmBuf.clear();
    }
    retroWritePos.store (0, std::memory_order_relaxed);
    retroFilled.store (0, std::memory_order_relaxed);

    wavRecMaxSamples = juce::jmax (96000, (int) std::lround (120.0 * sampleRate));
    if (wavRecording.load (std::memory_order_relaxed) && wavRecDryBuf.getNumSamples() < wavRecMaxSamples)
    {
        wavRecDryBuf.setSize (2, wavRecMaxSamples, true, true, true);
        wavRecWetBuf.setSize (2, wavRecMaxSamples, true, true, true);
        wavRecBgmBuf.setSize (2, wavRecMaxSamples, true, true, true);
    }

    // v5.0.0 伴奏ボーカル除去フィルタ状態 ＆ サビ盛り上げエンベロープ ＆ 4種ライブSE波形（遅延合成）
    bgmVcLp160L = bgmVcLp160R = bgmVcLp8500L = bgmVcLp8500R = 0.0f;
    chorusBoostEnv = chorusBoostMode.load (std::memory_order_relaxed) ? 1.0f : 0.0f;
    {
        const double seSr = juce::jmax (8000.0, sampleRate);
        const int len0 = (int) std::lround (2.2 * seSr); // 0: 拍手歓声 (Applause)
        const int len1 = (int) std::lround (1.8 * seSr); // 1: キラキラ (Starlight Chime)
        const int len2 = (int) std::lround (2.1 * seSr); // 2: ドラムロール→ジャン! (Drumroll & Ta-Da)
        const int len3 = (int) std::lround (1.6 * seSr); // 3: チャイム (Broadcast Bell)
        if (liveSeBufs[0].getNumSamples() != len0)
        {
            // 起動時はバッファ枠だけ事前確保（ヒープ再確保ゼロ）し、波形合成はボタン押下時に1個ずつ遅延生成する
            liveSeBufs[0].setSize (2, len0, false, true, true);
            liveSeBufs[1].setSize (2, len1, false, true, true);
            liveSeBufs[2].setSize (2, len2, false, true, true);
            liveSeBufs[3].setSize (2, len3, false, true, true);
            liveSeBuilt = { false, false, false, false };
        }
    }

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
        voiceLatency = mainShifted ? mainPitchLatency (vcOn) : 0;
        setLatencySamples (voiceLatency);
        reportedLatency.store (voiceLatency);
    }
    updateParameters();

    // v2.2.0 配信出力: 本線のサンプリングレートが変わったら開き直す
    // (レート比が変わるため。止まっているときは何もしない)
    streamOut.setHostSampleRate (sampleRate);
    if (isStandalone())
    {
        if (streamWanted && streamDevWanted.isNotEmpty())
            streamOut.start (streamDevWanted, sampleRate);
        else if (! streamUserConfigured && ! streamAutoChecked)
        {
            streamAutoChecked = true;
            juce::MessageManager::callAsync ([this] { ensureStandaloneVirtualDriverActive(); });
        }
    }
}

void VocalGzzioProcessor::ensureLiveSeBuilt (int index)
{
    if (index < 0 || index >= 4) return;
    if (liveSeBuilt[(size_t) index]) return;
    const double seSr = juce::jmax (8000.0, currentSampleRate);
    const int nSamples = liveSeBufs[index].getNumSamples();
    if (nSamples <= 0 || liveSeBufs[index].getNumChannels() < 2) return;

    auto* L = liveSeBufs[index].getWritePointer (0);
    auto* R = liveSeBufs[index].getWritePointer (1);
    juce::FloatVectorOperations::clear (L, nSamples);
    juce::FloatVectorOperations::clear (R, nSamples);

    const float twoPi = juce::MathConstants<float>::twoPi;
    auto softAtk = [] (float dt, float atkSec) noexcept -> float
    {
        if (dt <= 0.0f) return 0.0f;
        if (dt >= atkSec) return 1.0f;
        return 0.5f * (1.0f - std::cos (juce::MathConstants<float>::pi * (dt / atkSec)));
    };

    if (index == 0)
    {
        // SE 0: ♪拍手（ホワイトノイズを一切使わない物理モデリング・パームクラップ群 ＋ 祝福マリンバ和音）
        //   旧版の連続フィルタノイズ（「サー／ザー」という安っぽいノイズ感）を完全撤廃し、
        //   手のひら空洞共鳴（Helmholtz 6モード減衰正弦波）の離散パチパチ音と、温かい木琴マリンバの祝福コードを合成。
        static const float palmModes[6] = { 690.0f, 940.0f, 1210.0f, 1490.0f, 1780.0f, 2120.0f };
        static const float palmWeights[6] = { 0.30f, 0.42f, 0.36f, 0.24f, 0.15f, 0.09f };
        struct ClapEvent { float t0; float pan; float pitchScale; float amp; };
        ClapEvent claps[32];
        for (int c = 0; c < 32; ++c)
        {
            const float u = (float) c / 31.0f;
            const float jitter = (c == 0) ? 0.0f : (0.018f * std::sin ((float) (c * 17 + 5) * 1.37f));
            claps[c].t0 = (c == 0) ? 0.0f : juce::jlimit (0.01f, 1.65f, 0.015f + u * 1.52f + jitter);
            claps[c].pan = 0.18f + 0.64f * (0.5f + 0.5f * std::sin ((float) (c * 11 + 3) * 1.91f));
            claps[c].pitchScale = 0.88f + 0.24f * (0.5f + 0.5f * std::cos ((float) (c * 7 + 2) * 1.63f));
            const float macro = (u < 0.15f ? (0.55f + 0.45f * (u / 0.15f)) : std::exp (-1.45f * (u - 0.15f)));
            claps[c].amp = macro;
        }
        static const float marimbaNotes[6] = { 523.25f, 659.25f, 783.99f, 1046.50f, 1174.66f, 1318.51f };
        static const float marimbaTimes[6] = { 0.00f,   0.08f,   0.16f,   0.24f,    0.34f,    0.45f    };

        for (int i = 0; i < nSamples; ++i)
        {
            const float t = (float) i / (float) seSr;
            float sL = 0.0f, sR = 0.0f;

            // 離散アコースティック・パームクラップ（各打撃は35msで完全減衰し、打撃間のノイズフロアは完全ゼロ）
            for (int c = 0; c < 32; ++c)
            {
                const float dt = t - claps[c].t0;
                if (dt > 0.0f && dt < 0.045f)
                {
                    const float env = softAtk (dt, 0.0022f) * std::exp (-92.0f * dt) * claps[c].amp;
                    const float bend = 1.0f + 0.14f * std::exp (-140.0f * dt);
                    float palm = 0.0f;
                    for (int m = 0; m < 6; ++m)
                        palm += palmWeights[m] * std::sin (twoPi * palmModes[m] * claps[c].pitchScale * bend * dt + (float) (c + m));
                    sL += palm * env * (1.0f - claps[c].pan);
                    sR += palm * env * claps[c].pan;
                }
            }

            // 温かい木琴マリンバ＆ローズの祝福アルペジオ（心地よい音楽的余韻）
            for (int n = 0; n < 6; ++n)
            {
                const float dt = t - marimbaTimes[n];
                if (dt > 0.0f && dt < 1.65f)
                {
                    const float atk = softAtk (dt, 0.006f);
                    const float f0  = marimbaNotes[n];
                    const float body = std::sin (twoPi * f0 * dt) * std::exp (-3.6f * dt)
                                     + 0.18f * std::sin (twoPi * f0 * 4.0f * dt) * std::exp (-26.0f * dt);
                    const float pan = 0.25f + 0.50f * ((float) n / 5.0f);
                    sL += body * atk * 0.55f * (1.0f - pan);
                    sR += body * atk * 0.55f * pan;
                }
            }
            L[i] = sL;
            R[i] = sR;
        }
    }
    else if (index == 1)
    {
        // SE 1: ✦キラ（アコースティック・セレスタ＆ハープの透明感グリッサンド）
        //   高次倍音は打鍵直後40msだけ繊細にきらめいて急速減衰させ、余韻は純粋で澄んだサイン波のみを残す。
        static const float freqs[10] = {
            523.25f, 587.33f, 659.25f, 783.99f, 880.00f,
            1046.50f, 1174.66f, 1318.51f, 1567.98f, 2093.00f
        };
        for (int i = 0; i < nSamples; ++i)
        {
            const float t = (float) i / (float) seSr;
            float sL = 0.0f, sR = 0.0f;
            for (int n = 0; n < 10; ++n)
            {
                const float dt = t - (float) n * 0.072f;
                if (dt > 0.0f)
                {
                    const float atk = softAtk (dt, 0.007f);
                    const float w = twoPi * freqs[n] * dt;
                    const float fund = std::sin (w) * std::exp (-3.4f * dt);
                    const float bell = (0.16f * std::sin (w * 2.0f) + 0.06f * std::sin (w * 4.0f)) * std::exp (-24.0f * dt);
                    const float tone = (fund + bell) * atk;
                    const float pan = 0.22f + 0.56f * ((n % 2 == 0) ? ((float) n / 9.0f) : (1.0f - (float) n / 9.0f));
                    sL += tone * (1.0f - pan);
                    sR += tone * pan;
                }
            }
            L[i] = sL;
            R[i] = sR;
        }
    }
    else if (index == 2)
    {
        // SE 2: ●ドラム（ホワイトノイズ不使用のアコースティック・ティンパニロール → 温かいブラス＆ローズのファンファーレ和音）
        //   旧版のホワイトノイズ連打＋波形クリップ（「壊れたスピーカーのバリバリ音」）を完全撤廃し、
        //   丸く温かいティンパニ膜モード共鳴のロールから、澄んだメジャー9thファンファーレ和音へ解決する。
        const int numHits = 18;
        const float rollDur = 1.02f;
        for (int i = 0; i < nSamples; ++i)
        {
            const float t = (float) i / (float) seSr;
            float sL = 0.0f, sR = 0.0f;

            // 0.00s .. 1.02s: ティンパニ／タムのマレット連打ロール（ノイズ一切なし）
            if (t < rollDur + 0.18f)
            {
                for (int h = 0; h < numHits; ++h)
                {
                    const float u = (float) h / (float) numHits;
                    const float hitTime = u * rollDur;
                    const float dt = t - hitTime;
                    if (dt > 0.0f && dt < 0.14f)
                    {
                        const float f0 = (h % 2 == 0) ? (130.81f + u * 18.0f) : (164.81f + u * 22.0f);
                        const float pitchEnv = 1.0f + 0.24f * std::exp (-48.0f * dt);
                        const float w = twoPi * f0 * pitchEnv * dt;
                        const float membrane = std::sin (w) * std::exp (-22.0f * dt)
                                             + 0.22f * std::sin (w * 1.59f) * std::exp (-45.0f * dt);
                        const float vel = 0.35f + 0.65f * u;
                        const float hit = membrane * softAtk (dt, 0.004f) * vel;
                        const float pan = (h % 2 == 0) ? 0.35f : 0.65f;
                        sL += hit * (1.0f - pan);
                        sR += hit * pan;
                    }
                }
            }

            // 1.05s .. 2.10s: 温かいファンファーレ和音（C4 - G4 - B4 - D5 - E5）
            if (t >= 1.05f)
            {
                const float dt = t - 1.05f;
                const float atk = softAtk (dt, 0.012f);
                static const float chord[5] = { 261.63f, 392.00f, 493.88f, 587.33f, 659.25f };
                static const float pans[5]  = { 0.50f,   0.30f,   0.70f,   0.38f,   0.62f   };
                const float envFund = std::exp (-2.6f * dt);
                const float envHarm = std::exp (-12.0f * dt);
                for (int k = 0; k < 5; ++k)
                {
                    const float w = twoPi * chord[k] * dt;
                    const float wDetune = twoPi * (chord[k] * 1.0015f) * dt;
                    const float voice = (0.55f * (std::sin (w) + std::sin (wDetune)) * envFund
                                       + 0.22f * std::sin (w * 2.0f) * envHarm
                                       + 0.07f * std::sin (w * 3.0f) * envHarm) * atk;
                    sL += voice * (1.0f - pans[k]);
                    sR += voice * pans[k];
                }
            }
            L[i] = sL;
            R[i] = sR;
        }
    }
    else if (index == 3)
    {
        // SE 3: ◆ベル（上品で丸みのあるビブラフォン・2音ピンポンチャイム）
        for (int i = 0; i < nSamples; ++i)
        {
            const float t = (float) i / (float) seSr;
            float sL = 0.0f, sR = 0.0f;
            const float tremL = 0.88f + 0.12f * std::sin (twoPi * 5.2f * t);
            const float tremR = 0.88f + 0.12f * std::cos (twoPi * 5.2f * t);

            // 第1音: A4 + E5 (温かい5度ハーモニー)
            {
                const float atk1 = softAtk (t, 0.008f);
                const float w1a = twoPi * 440.00f * t;
                const float w1b = twoPi * 659.25f * t;
                const float tone1 = (0.45f * std::sin (w1a) + 0.55f * std::sin (w1b)) * std::exp (-3.2f * t)
                                  + 0.14f * std::sin (w1b * 2.0f) * std::exp (-24.0f * t);
                sL += tone1 * atk1 * tremL * 0.62f;
                sR += tone1 * atk1 * tremR * 0.42f;
            }
            // 第2音: D4 + F#4 + D5 (柔らかく落ち着く解決和音)
            if (t >= 0.36f)
            {
                const float dt = t - 0.36f;
                const float atk2 = softAtk (dt, 0.009f);
                const float w2a = twoPi * 293.66f * dt;
                const float w2b = twoPi * 440.00f * dt;
                const float w2c = twoPi * 587.33f * dt;
                const float tone2 = (0.35f * std::sin (w2a) + 0.30f * std::sin (w2b) + 0.45f * std::sin (w2c)) * std::exp (-2.5f * dt)
                                  + 0.12f * std::sin (w2c * 2.0f) * std::exp (-22.0f * dt);
                sL += tone2 * atk2 * tremL * 0.44f;
                sR += tone2 * atk2 * tremR * 0.64f;
            }
            L[i] = sL;
            R[i] = sR;
        }
    }

    // ★ 全4種共通：ピーク正規化＆静音シーリング（最大ピーク 0.12f ≈ -18.4 dBFS に統一し、耳障りな爆音やクリップ歪みを100%防止）
    float maxPk = 0.0f;
    for (int i = 0; i < nSamples; ++i)
        maxPk = juce::jmax (maxPk, juce::jmax (std::abs (L[i]), std::abs (R[i])));

    const float targetPeak = (index == 0) ? 0.11f : 0.12f; // -19.1 dBFS ~ -18.4 dBFS（歌声の邪魔をしない上品なBGM音量）
    const float scale = (maxPk > 1.0e-5f) ? (targetPeak / maxPk) : 0.0f;
    const int fadeOutLen = juce::jmin (nSamples / 4, (int) (0.065 * seSr));
    const int fadeStart  = nSamples - fadeOutLen;

    for (int i = 0; i < nSamples; ++i)
    {
        float envOut = scale;
        if (i >= fadeStart && fadeOutLen > 0)
        {
            const float u = (float) (i - fadeStart) / (float) fadeOutLen;
            envOut *= 0.5f * (1.0f + std::cos (juce::MathConstants<float>::pi * u));
        }
        L[i] *= envOut;
        R[i] *= envOut;
    }

    liveSeBuilt[(size_t) index] = true;
}

void VocalGzzioProcessor::updateParameters()
{
    const auto sr = currentSampleRate;
    auto p = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };
    auto toGain = [] (float db) { return juce::Decibels::decibelsToGain (db); };

    // v2.8.0: ここは processBlock から毎ブロック呼ばれる。makeXxx (ヒープ確保)
    // ではなく ArrayCoefficients (確保なし) を使う。→ PluginProcessor.h の ACoefs 参照
    // v2.10.0 #77/#78 音源モードで中心を持ち替える（うたは今までと同じ数値）
    const int  eraNow  = legacyEcoGate.load (std::memory_order_relaxed)
                             ? legacyVersionEra.load (std::memory_order_relaxed) : 510;
    const int  srcMode = (eraNow >= 210) ? (int) apvts.getRawParameterValue ("src_mode")->load() : 0;
    const auto prof    = sourceProfile (srcMode);

    *hpf.state      = ACoefs::makeHighPass   (sr, p ("lowcut"), 0.707f);
    *mud.state      = ACoefs::makePeakFilter (sr, prof.mudHz,   prof.mudQ,   toGain (p ("mud")));
    *harsh.state    = ACoefs::makePeakFilter (sr, prof.harshHz, prof.harshQ, toGain (p ("harsh")));
    *presence.state = ACoefs::makePeakFilter (sr, prof.presHz,  prof.presQ,  toGain (p ("presence")));
    *air.state      = ACoefs::makeHighShelf  (sr, prof.airHz,   0.707f,      toGain (p ("air")));
    *deessDetectHP.state = ACoefs::makeHighPass (sr, prof.dsDetectHz, 0.9f);

    // v3.1 2点目のこもり（弾き語りだけ）。使わない使いかた・v3.1未満世代では1回も通さない。
    mud2On = (eraNow >= 310) && (prof.mudHz2 > 1.0f);
    if (mud2On)
        *mud2.state = ACoefs::makePeakFilter (sr, prof.mudHz2, prof.mudQ2, toGain (p ("mud")));

    // v3.1 2点目のサ行おさえ（弾き語りだけ）。検出は帯域通過、下げるのは山。
    dsDetect2Hz = (eraNow >= 310) ? prof.dsDetectHz2 : 0.0f;
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
        if (type >= kHeyaFirst && eraNow >= 400)
        {
            if (heyaReady.load())
            {
                // 実際に選ばれた部屋だけをオンデマンド構築（事前確保済みのためヒープ確保ゼロ）
                heyaRev.ensureRoomBuilt (type - kHeyaFirst);
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

    // ---- v5.1.0 柱③：🎙️ ゼロ遅延・名機マイク・モデリング（0=OFF, 1=800G真空管, 2=U87王道, 3=SM7B配信, 4=ASMR極艶） ----
    {
        const int mm = (eraNow >= 510) ? micModelMode.load (std::memory_order_relaxed) : 0;
        if (mm > 0 && (mm != micModLastMode || std::abs (sr - micModLastSr) > 1.0))
        {
            micModLastMode = mm;
            micModLastSr   = sr;
            const double safeSr = juce::jmax (16000.0, sr);
            const float maxF    = (float) (safeSr * 0.44);
            struct MicProfile { float hpHz, bodyHz, bodyQ, bodyDb, presHz, presQ, presDb, airHz, airDb; };
            static const MicProfile kProfiles[4] = {
                // 1: ✨ 800G 真空管 (Sony C-800G 風 — 10.5kHz シルクエアー＋すっきり中低域＋真空管2次倍音)
                { 65.0f, 255.0f, 1.15f, -1.6f, 3800.0f, 0.90f, +1.8f, 10500.0f, +4.2f },
                // 2: 👑 U87 王道 (Neumann U87 Ai 風 — 185Hz 胸声の厚み＋2.6kHz 王道ボーカルの芯)
                { 75.0f, 185.0f, 0.90f, +1.6f, 2600.0f, 0.85f, +2.4f,  9500.0f, +1.5f },
                // 3: 📻 SM7B 配信 (Shure SM7B 風 — タイトな低域＋放送局の温かみ＋4kHz耳痛緩和)
                { 85.0f, 155.0f, 1.00f, +2.2f, 2850.0f, 1.05f, +2.1f,  7500.0f, -1.2f },
                // 4: 💎 ASMR 極艶 (耳元リボン＆超高域ブレス — 140Hz 近接感＋3.2kHz 刺さり緩和＋11.5kHz 極艶エアー)
                { 55.0f, 140.0f, 0.90f, +2.0f, 3200.0f, 1.30f, -2.0f, 11500.0f, +4.8f }
            };
            const auto& mp = kProfiles[juce::jlimit (0, 3, mm - 1)];
            const auto cSub  = ACoefs::makeHighPass   (safeSr, juce::jmin (mp.hpHz,   maxF), 0.707f);
            const auto cBody = ACoefs::makePeakFilter (safeSr, juce::jmin (mp.bodyHz, maxF), mp.bodyQ, toGain (mp.bodyDb));
            const auto cPres = ACoefs::makePeakFilter (safeSr, juce::jmin (mp.presHz, maxF), mp.presQ, toGain (mp.presDb));
            const auto cAir  = ACoefs::makeHighShelf  (safeSr, juce::jmin (mp.airHz,  maxF), 0.707f,   toGain (mp.airDb));
            for (int ch = 0; ch < 2; ++ch)
            {
                *micModSub[ch].coefficients  = cSub;
                *micModBody[ch].coefficients = cBody;
                *micModPres[ch].coefficients = cPres;
                *micModAir[ch].coefficients  = cAir;
            }
        }
        else if (mm == 0 && micModLastMode != 0)
        {
            micModLastMode = 0;
            for (int ch = 0; ch < 2; ++ch)
            {
                micModSub[ch].reset();
                micModBody[ch].reset();
                micModPres[ch].reset();
                micModAir[ch].reset();
                micModDc[ch] = 0.0f;
            }
        }
    }
}

//==============================================================================
bool VocalGzzioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (isStandalone())
        return out.size() >= 1 && out.size() <= 16 && layouts.getMainInputChannelSet().size() <= 16;
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

void VocalGzzioProcessor::processBlock (juce::AudioBuffer<float>& fullBuffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int totalCh = fullBuffer.getNumChannels();
    const int numCh = juce::jmin (totalCh, 2);
    const int numSamples = fullBuffer.getNumSamples();
    juce::AudioBuffer<float> buffer (fullBuffer.getArrayOfWritePointers(), numCh, numSamples);

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
    // 🕰️ 世代タイムマシン・省負荷ゲート: 旧バージョンが選ばれているときは、
    // その世代以降に追加されたDSPステージを丸ごと休止させて当時の軽快なCPU負荷を再現する。
    const int   eraNow  = legacyEcoGate.load (std::memory_order_relaxed)
                              ? legacyVersionEra.load (std::memory_order_relaxed) : 510;
    const bool  crushOn = (eraNow >= 300) && (apvts.getRawParameterValue ("crush_on")->load() > 0.5f);
    const float crushCg = crushOn ? crushGuard : 0.0f;

    // ---- v2.6.0 入力の防火壁 ＋ v4.8.1 32bitフロート機(ZOOM UAC-232/432・Steinberg URX等)の0dBFS超え自動ヘッドルーム制御 ----
    // ホストや他のプラグインから NaN/Inf が一度でも流れ込むと、内部の再帰状態が汚染されるため入口で 0 に置き換える。
    // また、32bitフロート対応インターフェース(ZOOM UAC-232/432等)でゲイン調整なしに大声を出して |x| > 1.0f (+0dBFS超え) の
    // 浮動小数点信号がそのまま入ってきても絶対にクリップしないよう、0dBFS超えを検知した瞬間だけ滑らかに自動パッドを入れる。
    // (通常の16/24bit整数インターフェースでは |x| <= 1.0f のため 1サンプルも変化しない)
    float blockMaxIn = 0.0f;
    for (int ch = 0; ch < totalCh; ++ch)
    {
        float* p = fullBuffer.getWritePointer (ch);
        if (! gzBlockFinite (p, numSamples))
        {
            for (int i = 0; i < numSamples; ++i)
                if (! std::isfinite (p[i])) p[i] = 0.0f;
        }
        for (int i = 0; i < numSamples; ++i)
        {
            const float a = std::abs (p[i]);
            if (a > blockMaxIn) blockMaxIn = a;
        }
    }
    if (blockMaxIn > 1.0f || floatPadGain < 0.9999f)
    {
        const float targetPad = (blockMaxIn > 1.0f) ? juce::jlimit (0.05f, 1.0f, 0.92f / blockMaxIn) : 1.0f;
        if (targetPad < floatPadGain)
            floatPadGain = targetPad; // 0dBFS超えの突発入力には即時追従してクリップを完全阻止
        else
            floatPadGain += 0.04f * (targetPad - floatPadGain); // 通常域へ戻ったら滑らかに復帰
        if (floatPadGain > 0.9995f) floatPadGain = 1.0f;

        for (int ch = 0; ch < totalCh; ++ch)
            juce::FloatVectorOperations::multiply (fullBuffer.getWritePointer (ch), floatPadGain, numSamples);
    }

    // ---- v4.8.0 マイク入力の中央寄せルーティング（全インターフェース IN1〜IN4・多ch完全対応） ----
    // Roland Rubix 22/24/44、Yamaha AG03/06、Steinberg UR22C/UR44C、Focusrite Scarlett Solo/2i2/4i4、
    // MOTU M2/M4、SSL 2、Universal Audio Volt、RME Fireface 等を単体起動版(Standalone)で使う際、
    // 入力1〜4のどこにXLRマイクが挿さっていても、有効なマイク入力を両耳の中央(L=R)へ揃え、
    // 空いている反対chのプリアンプ熱雑音やDCオフセットの混入を入口で遮断する。
    if (numCh >= 2 && ! selfTestRunning.load (std::memory_order_relaxed))
    {
        const int availIn = juce::jmin (totalCh, juce::jmax (1, getTotalNumInputChannels()));
        if (availIn == 1)
        {
            autoCenterCh.store (0, std::memory_order_relaxed);
            juce::FloatVectorOperations::copy (buffer.getWritePointer (1), buffer.getReadPointer (0), numSamples);
        }
        else
        {
            const int routeMode = inputRouteMode.load (std::memory_order_relaxed);
            if (routeMode == 1) // IN 1 (左マイク固定)
            {
                autoCenterCh.store (0, std::memory_order_relaxed);
                juce::FloatVectorOperations::copy (buffer.getWritePointer (1), buffer.getReadPointer (0), numSamples);
            }
            else if (routeMode == 2) // IN 2 (右マイク固定 / Scarlett Solo 4th Gen XLR等)
            {
                autoCenterCh.store (1, std::memory_order_relaxed);
                juce::FloatVectorOperations::copy (buffer.getWritePointer (0), buffer.getReadPointer (1), numSamples);
            }
            else if (routeMode == 5) // IN 3 (Rubix 44 / UR44C / Scarlett 4i4 / MOTU M4 等の3ch目マイク)
            {
                const int srcCh = (availIn > 2) ? 2 : 0;
                autoCenterCh.store (srcCh, std::memory_order_relaxed);
                if (srcCh != 0)
                    juce::FloatVectorOperations::copy (buffer.getWritePointer (0), fullBuffer.getReadPointer (srcCh), numSamples);
                juce::FloatVectorOperations::copy (buffer.getWritePointer (1), buffer.getReadPointer (0), numSamples);
            }
            else if (routeMode == 6) // IN 4 (Rubix 44 / UR44C / Scarlett 4i4 / MOTU M4 等の4ch目マイク)
            {
                const int srcCh = (availIn > 3) ? 3 : 1;
                autoCenterCh.store (srcCh, std::memory_order_relaxed);
                juce::FloatVectorOperations::copy (buffer.getWritePointer (0), fullBuffer.getReadPointer (srcCh), numSamples);
                juce::FloatVectorOperations::copy (buffer.getWritePointer (1), buffer.getReadPointer (0), numSamples);
            }
            else if (routeMode == 3) // 全マイク合成 (IN 1+2 または IN 1〜4 のうち信号のあるchを合成)
            {
                const int scanCh = juce::jmin (availIn, 4);
                double e[4] = { 0.0, 0.0, 0.0, 0.0 };
                double maxE = 0.0;
                for (int c = 0; c < scanCh; ++c)
                {
                    const float* rc = fullBuffer.getReadPointer (c);
                    double s = 0.0;
                    for (int i = 0; i < numSamples; ++i)
                        s += (double) rc[i] * (double) rc[i];
                    e[c] = s / (double) juce::jmax (1, numSamples);
                    if (e[c] > maxE) maxE = e[c];
                }
                int activeMask = 0;
                int activeCount = 0;
                for (int c = 0; c < scanCh; ++c)
                {
                    if (e[c] > 1.0e-7 && e[c] >= maxE * 0.0625)
                    {
                        activeMask |= (1 << c);
                        ++activeCount;
                    }
                }
                if (activeCount <= 0)
                {
                    activeMask = 3; // IN 1 + IN 2
                    activeCount = 2;
                }
                const float mixScale = (activeCount == 1) ? 1.0f : (1.0f / (float) activeCount);
                float* L = buffer.getWritePointer (0);
                float* R = buffer.getWritePointer (1);
                for (int i = 0; i < numSamples; ++i)
                {
                    float sum = 0.0f;
                    for (int c = 0; c < scanCh; ++c)
                        if ((activeMask & (1 << c)) != 0)
                            sum += fullBuffer.getReadPointer (c)[i];
                    const float m = sum * mixScale;
                    L[i] = m;
                    R[i] = m;
                }
            }
            else if (routeMode == 0 && (isStandalone() || outputMonoMode.load (std::memory_order_relaxed)))
            {
                // 自動モノラル (IN 1〜4 のうち声が入っている端子を自動追従して中央 L=R へ配置)
                // 隣接サンプル差分 dx = rc[i] - rc[i-1] によるAC音声帯域エネルギーで判定するため、
                // 未接続端子のDCオフセットや50/60Hzハムで空き端子へ誤切り替えすることがない。
                const int scanCh = juce::jmin (availIn, 4);
                double e[4] = { 0.0, 0.0, 0.0, 0.0 };
                const double alpha = 1.0 - std::exp (-numSamples / (0.025 * juce::jmax (8000.0, currentSampleRate)));
                for (int c = 0; c < scanCh; ++c)
                {
                    const float* rc = fullBuffer.getReadPointer (c);
                    double acSum = 0.0;
                    float prev = numSamples > 0 ? rc[0] : 0.0f;
                    for (int i = 1; i < numSamples; ++i)
                    {
                        const float dx = rc[i] - prev;
                        prev = rc[i];
                        acSum += (double) dx * (double) dx;
                    }
                    e[c] = acSum / (double) juce::jmax (1, numSamples - 1);
                    inRouteEnergy[c] += alpha * (e[c] - inRouteEnergy[c]);
                }

                int activeCh = juce::jlimit (0, scanCh - 1, autoCenterCh.load (std::memory_order_relaxed));
                int bestCh = activeCh;
                double bestE = e[activeCh];

                // 1. まず IN 1 / IN 2 を優先判定（明確な声レベルのAC変動 > 1.6e-5 があるときのみ切り替え）
                for (int c = 0; c < juce::jmin (scanCh, 2); ++c)
                {
                    if (e[c] > 1.6e-5 && e[c] > bestE * 3.5 && inRouteEnergy[c] > inRouteEnergy[bestCh] * 2.2)
                    {
                        bestCh = c;
                        bestE = e[c];
                    }
                }
                // 2. IN 1 / IN 2 が両方無音(< 1.0e-6)で、IN 3 または IN 4 に単一マイク音声(> 5.0e-5)がある場合のみ追従
                // （MOTU M2/M4 や EVO 4/8 の IN 3-4 ステレオループバック e[2]≒e[3] は除外）
                if (scanCh > 2 && inRouteEnergy[0] < 1.0e-6 && inRouteEnergy[1] < 1.0e-6)
                {
                    for (int c = 2; c < scanCh; ++c)
                    {
                        const int otherLoopCh = (c == 2) ? 3 : 2;
                        const bool looksLikeStereoLoopback = (scanCh >= 4 && e[otherLoopCh] > e[c] * 0.45 && e[c] > e[otherLoopCh] * 0.45);
                        if (! looksLikeStereoLoopback && e[c] > 5.0e-5 && e[c] > bestE * 4.0 && inRouteEnergy[c] > inRouteEnergy[bestCh] * 2.5)
                        {
                            bestCh = c;
                            bestE = e[c];
                        }
                    }
                }
                if (bestCh != activeCh)
                {
                    activeCh = bestCh;
                    autoCenterCh.store (activeCh, std::memory_order_relaxed);
                }

                if (activeCh != 0)
                    juce::FloatVectorOperations::copy (buffer.getWritePointer (0), fullBuffer.getReadPointer (activeCh), numSamples);
                juce::FloatVectorOperations::copy (buffer.getWritePointer (1), buffer.getReadPointer (0), numSamples);
            }
        }
    }

    // ---- v2.6.0 ジー音(電源ハム)の自動除去 ----
    // 何よりも先に消す。理由は3つ。
    //  ・ローカット(既定100Hz)より前なので、ハムの基本波まで丸ごと見える
    //  ・ゲートやコンプがジーに反応して呼吸するのを防げる
    //  ・ボイス変換に入る前に消しておかないと、ピッチシフトでジーが
    //    ぐしゃぐしゃに散らばって、あとからでは絶対に取れなくなる
    {
        const float humAmt = (eraNow >= 260)
                           ? apvts.getRawParameterValue ("hum_amt")->load() * 0.01f : 0.0f;
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

    // ---- v4.7.0 5秒録って手放し聴き比べ「試聴ルーパー（Quick Audition Looper）」 ----
    // 歌いながら骨伝導が混ざった状態では、エフェクトの効き具合やテーマごとの声色を
    // 客観的に判断しづらい。ここ（入力トリム直後・原音コピー dryBuffer の直前）で
    // 5秒間の素の声を録音し、録音完了後は自動で手放しループ再生に切り替える。
    // 原音コピーより手前で差し替えるため、ループ再生中に 2D オーブや仕上がりプリセットを
    // 動かしてリアルタイムに音色を作れるだけでなく、「押して原音と比較」も同じ録音フレーズの
    // 未加工音と加工音を正確に比較できる。
    if (! selfTestRunning.load (std::memory_order_relaxed) && auditionBuf.getNumSamples() > 0)
    {
        const int audSt = auditionState.load (std::memory_order_acquire);
        const int maxCap = auditionBuf.getNumSamples();
        if (audSt == AuditionRecording)
        {
            int pos = auditionPos.load (std::memory_order_relaxed);
            const float* inL = buffer.getReadPointer (0);
            const float* inR = buffer.getReadPointer (numCh > 1 ? 1 : 0);
            float* recL = auditionBuf.getWritePointer (0);
            float* recR = auditionBuf.getWritePointer (1);
            for (int n = 0; n < numSamples; ++n)
            {
                if (pos < maxCap)
                {
                    recL[pos] = inL[n];
                    recR[pos] = inR[n];
                    ++pos;
                }
            }
            if (pos >= maxCap)
            {
                auditionLen.store (maxCap, std::memory_order_relaxed);
                auditionPos.store (0, std::memory_order_relaxed);
                auditionState.store (AuditionPlaying, std::memory_order_release);
            }
            else
            {
                auditionPos.store (pos, std::memory_order_relaxed);
            }
        }
        else if (audSt == AuditionPlaying)
        {
            const int loopLen = juce::jlimit (1, maxCap, auditionLen.load (std::memory_order_relaxed));
            int pos = auditionPos.load (std::memory_order_relaxed) % loopLen;
            const int xfadeSamples = juce::jmin (loopLen / 4, juce::jmax (32, (int) (0.012 * currentSampleRate)));
            const float* recL = auditionBuf.getReadPointer (0);
            const float* recR = auditionBuf.getReadPointer (1);
            float* outL = buffer.getWritePointer (0);
            float* outR = numCh > 1 ? buffer.getWritePointer (1) : nullptr;
            for (int n = 0; n < numSamples; ++n)
            {
                float sL = recL[pos];
                float sR = recR[pos];
                // ループ終端の12msだけ先頭と滑らかにクロスフェードし、継ぎ目のプチノイズを防ぐ
                if (xfadeSamples > 0 && pos >= loopLen - xfadeSamples)
                {
                    const int headIdx = pos - (loopLen - xfadeSamples);
                    const float alpha = (float) headIdx / (float) xfadeSamples;
                    sL = sL * (1.0f - alpha) + recL[headIdx] * alpha;
                    sR = sR * (1.0f - alpha) + recR[headIdx] * alpha;
                }
                outL[n] = sL;
                if (outR != nullptr) outR[n] = sR;
                if (++pos >= loopLen) pos = 0;
            }
            auditionPos.store (pos, std::memory_order_relaxed);
        }
    }

    // ---- v4.8.0 柱④：😷 咳払い・くしゃみ瞬間ミュート（8msポップノイズゼロ遮断） ----
    // 原音コピー(dryBuffer)・リバーブ・ディレイ・コンプより手前で 8ms スムーズフェードにより遮断する。
    // これにより咳払いやくしゃみがリバーブの残響タンクに飛び込むのを防ぎつつ、直前の歌声の余韻は自然に消える。
    if (! selfTestRunning.load (std::memory_order_relaxed))
    {
        const bool wantMute = coughMute.load (std::memory_order_relaxed);
        const float targetG = wantMute ? 0.0f : 1.0f;
        if (std::abs (coughMuteGain - targetG) > 1.0e-5f || coughMuteGain < 0.9999f)
        {
            float* outL = buffer.getWritePointer (0);
            float* outR = numCh > 1 ? buffer.getWritePointer (1) : nullptr;
            for (int n = 0; n < numSamples; ++n)
            {
                if (coughMuteGain < targetG)
                    coughMuteGain = juce::jmin (targetG, coughMuteGain + coughMuteStep);
                else if (coughMuteGain > targetG)
                    coughMuteGain = juce::jmax (targetG, coughMuteGain - coughMuteStep);
                outL[n] *= coughMuteGain;
                if (outR != nullptr) outR[n] *= coughMuteGain;
            }
        }
    }

    // ---- v2.10.0 距離ならし（近接効果の自動補正） ----
    // マイク音量の直後・原音コピーより前。ここが v2.8.0 で確定した「いちばん手前」。
    // 原音側にも掛かるので、Mix で混ぜ戻したときに太さが食い違わない。
    // 声が出ているときだけ学習する（無音でノイズの比を「いつも」と覚えないため）。
    {
        const float pa = (eraNow >= 240)
                       ? apvts.getRawParameterValue ("prox_amt")->load() * 0.01f : 0.0f;
        proxEvener.setAmount (pa);
        if (pa > 0.0f)
        {
            float rms = buffer.getRMSLevel (0, 0, numSamples);
            if (numCh > 1) rms = juce::jmax (rms, buffer.getRMSLevel (1, 0, numSamples));
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

    // ---- v5.1.0 柱③：🎙️ ゼロ遅延・名機マイク・モデリング（0=OFF, 1=800G真空管, 2=U87王道, 3=SM7B配信, 4=ASMR極艶） ----
    // 原音コピー(dryBuffer)の直後で適用するため、「押して原音と比較」や Mix=0% では素のマイク音と正確に聴き比べられる。
    {
        const int mm = (eraNow >= 510 && ! cmp && ! selfTestRunning.load (std::memory_order_relaxed))
                           ? micModelMode.load (std::memory_order_relaxed) : 0;
        if (mm > 0 && micModLastMode == mm)
        {
            const int chCount = juce::jmin (numCh, 2);
            for (int ch = 0; ch < chCount; ++ch)
            {
                float* p = buffer.getWritePointer (ch);
                auto& fSub  = micModSub[ch];
                auto& fBody = micModBody[ch];
                auto& fPres = micModPres[ch];
                auto& fAir  = micModAir[ch];
                float dc    = micModDc[ch];
                for (int n = 0; n < numSamples; ++n)
                {
                    float x = fAir.processSample (fPres.processSample (fBody.processSample (fSub.processSample (p[n]))));
                    if (mm == 1) // ✨ 800G 真空管: 偶数次2次倍音の艶やかな三極管サチュレーション
                    {
                        const float sq = x * x;
                        dc += 0.002f * (sq - dc);
                        x = x + 0.075f * (sq - dc);
                    }
                    else if (mm == 2) // 👑 U87 王道: トランスフォーマーの温かみある奇数次ソフト飽和
                    {
                        x = std::tanh (1.18f * x) * 0.8474576f;
                    }
                    p[n] = x;
                }
                micModDc[ch] = dc;
            }
        }
    }

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
        // FireFace UC 等の多入力インターフェースで Mic 2 (ch 1) だけにマイクを挿した
        // 場合でも無音(-120dBFS)と誤測定しないよう、有効チャンネルの最大エネルギーを使う。
        {
            double ss = 0.0;
            for (int ch = 0; ch < juce::jmin (2, dryBuffer.getNumChannels()); ++ch)
            {
                const float* r = dryBuffer.getReadPointer (ch);
                double chSs = 0.0;
                for (int n = 0; n < numSamples; ++n) chSs += (double) r[n] * r[n];
                ss = juce::jmax (ss, chSs);
            }
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
        const int keyCh = juce::jlimit (0, juce::jmax (0, dryBuffer.getNumChannels() - 1), tunerInputChannel);
        const float* in = dryBuffer.getReadPointer (keyCh);
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

    // ---- de-noise (4-band adaptive downward expander, RX-style Learn) ----
    // ゲートより先に入力を測る。ゲートが閉じていても環境音を学習し、
    // 古い学習済みの床が固定されたままになることを防ぐ。
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
                                       dnEnv, true)
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
                // 未学習直後や低い保存床からの復帰中、dnClassifier がすでに「定常・非周期の
                // 部屋ノイズ」と確認している間は、現在のノイズ床推定値(floorTarget)も
                // 基準の下限に入れて「定常ノイズで voiceOpen がロックする」デッドロックを防ぐ。
                const float vFloor1 = (dnClassifier.isStationary() && ! dnClassifier.isPeriodic())
                                    ? juce::jmax (dnFloor[1], dnClassifier.floorTarget (1))
                                    : dnFloor[1];
                const float vFloor2 = (dnClassifier.isStationary() && ! dnClassifier.isPeriodic())
                                    ? juce::jmax (dnFloor[2], dnClassifier.floorTarget (2))
                                    : dnFloor[2];
                if (((dnEnv[1] + dnEnv[2] > 2.5e-3f)
                     && ((dnEnv[1] > vFloor1 * 4.0f && dnEnv[2] > vFloor2 * 4.0f)
                          || dnEnv[1] > vFloor1 * 10.0f
                          || dnEnv[2] > vFloor2 * 10.0f))
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
                    // 未学習時および自動学習時は約0.5秒の定常・非周期音を確認してから床へ寄せる。
                    // 保存された床が現在の定常ノイズより大幅に低い場合(dnFloor < target * 0.25)も
                    // 速い時定数で追いつかせ、エキスパンダーが開きっぱなしになるのを防ぐ。
                    if ((! dnLearned || dnRelearn)
                        && dnClassifier.isStationary() && dnFallRun[b] <= dnFallMax)
                    {
                        const float target = dnClassifier.floorTarget (b);
                        const float follow = (! dnLearned || dnFloor[b] < target * 0.25f)
                                           ? dnInitialFollowCoef : dnRelearnCoef;
                        dnFloor[b] += follow * (target - dnFloor[b]);
                        dnFloor[b] = juce::jlimit (1.0e-6f, 0.5f, dnFloor[b]);
                    }

                    const float openThr = dnFloor[b] * 2.5f;   // ~+8 dB above floor

                    // 高域は声の有無で二択に切り替えず、床からの余裕で連続的に減衰。
                    // ノイズ包絡の中央値付近(dnFloor/1.4)では最大減衰(below≈1.0)がしっかり掛かり、
                    // ノイズのランダムな小ピークで高速開放(dnOpenCoef)が誤発火しないようにする。
                    const bool protectBand = b == 3 ? dnHfVoiceHold > 0 : voiceOpen;
                    const float below = juce::jlimit (0.0f, 1.0f,
                                          (openThr - dnEnv[b]) / juce::jmax (openThr - dnFloor[b] * 0.5f, 1e-9f));
                    // v4.6.0/v4.7.0 深層フロア抑制: 低〜中低域(b < 2: 0〜1200Hz)の包絡が学習床(dnFloor)以下に沈んでいる
                    // 定常ノイズ区間では追加の深掘り減衰(最大+18dB)を滑らかに重ね、I/F固有の複合ノイズ(-68dBFS)を
                    // 無音域まで沈める。中高域〜高域(b >= 2)は喋り出しのノイズ跳ね(サフサフ)を防ぐため従来カーブを維持する。
                    const float deepFloor = (b < 2)
                        ? juce::jlimit (0.0f, 1.0f, (dnFloor[b] * 1.25f - dnEnv[b]) / juce::jmax (dnFloor[b] * 0.50f, 1e-9f))
                        : 0.0f;
                    const float attenDb = maxAttenDb * below + (dnOn ? 18.0f * amount * deepFloor : 0.0f);
                    const float targetGain = protectBand ? 1.0f
                        : juce::Decibels::decibelsToGain (-attenDb);
                    const bool bandHasSignal = dnEnv[b] > dnFloor[b] * 1.6f;
                    const float upCoef = protectBand ? dnFastOpen
                                       : (bandHasSignal ? dnOpenCoef : dnSoftOpen);
                    const float coef = targetGain > dnGain[b] ? upCoef
                                     : (b == 3 ? dnHfDuckCoef : dnCloseCoef);
                    dnGain[b] += coef * (targetGain - dnGain[b]);
                    attenSum += 1.0f - dnGain[b];

                    for (int ch = 0; ch < numCh; ++ch)
                        bandBuf[b].setSample (ch, n,
                            bandBuf[b].getSample (ch, n) * dnGain[b]);
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
                    const int q25  = juce::jmax (1, dnHistCount / 4);
                    const int q75  = juce::jmax (q25, (dnHistCount * 3) / 4);
                    const int half = juce::jmax (1, dnHistCount / 2);
                    const int p95  = juce::jmax (1, (dnHistCount * 95) / 100);
                    for (int b = 0; b < 4; ++b)
                    {
                        int acc = 0; bool haveMed = false;
                        double iqrSum = 0.0; int iqrWeight = 0;
                        for (int i = 0; i < dnHistBins; ++i)
                        {
                            const int prevAcc = acc;
                            acc += dnHist[b][i];
                            // 四分位範囲(25%〜75%)の重み付き平均で1dB刻みの量子化揺れを抑える
                            const int lo = juce::jmax (prevAcc, q25 - 1);
                            const int hi = juce::jmin (acc, q75);
                            if (hi > lo)
                            {
                                iqrSum    += (double) (hi - lo) * (double) (i - 120);
                                iqrWeight += (hi - lo);
                            }
                            if (! haveMed && acc >= half) { medDb[b] = (float) (i - 120); haveMed = true; }
                            if (acc >= p95) { p95Db[b] = (float) (i - 120); break; }
                        }
                        if (iqrWeight > 0)
                            medDb[b] = (float) (iqrSum / (double) iqrWeight);
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

    // ---- gate ----
    // 判定には除去前の入力を使い、減衰だけをノイズ除去後へ適用する。
    // これにより学習を妨げず、既存のゲートしきい値と開閉速度を保つ。
    if (soujiRun && apvts.getRawParameterValue ("gate_on")->load() > 0.5f)
    {
        const float thr = juce::Decibels::decibelsToGain (apvts.getRawParameterValue ("gate")->load());
        for (int n = 0; n < numSamples; ++n)
        {
            float inPk = 0.0f;
            for (int ch = 0; ch < numCh; ++ch)
                inPk = juce::jmax (inPk, std::abs (dryBuffer.getSample (ch, n)));
            gateEnv += (inPk > gateEnv ? gateEnvAtk : gateEnvRel) * (inPk - gateEnv);
            const float target = gateEnv >= thr ? 1.0f : 0.0f;
            gateGain += (target > gateGain ? gateOpenCoef : gateCloseCoef) * (target - gateGain);
            for (int ch = 0; ch < numCh; ++ch)
                buffer.setSample (ch, n, buffer.getSample (ch, n) * gateGain);
        }
    }
    else gateGain = 1.0f;

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
        const bool talkMc    = liveTalkMode.load (std::memory_order_relaxed)
                            && ! selfTestRunning.load (std::memory_order_relaxed);
        sessionActive.store (sessionOn || talkMc);

        // v2.10.0 ★アコギではピッチ補正・ハモリ・ボイス変換を通さない。
        //  どれも「声のピッチ」を前提にした処理で、和音が鳴るギターに掛けると
        //  検出が別の弦に飛び移って音程がふらつく。切れば遅延も 0 のままになる。
        // v3.0 モジュール「へんしん」。OFF のあいだは3つとも切れる＝遅延も0のまま。
        // v4.7.0 雑談MC切替(talkMc)中は、歌枠のパラメータ値を保ったままピッチ補正(atOn)とハモリ(jnOn)のみを素通しにする。
        // ★ 重要：ボイス変換(vcOn)は雑談MC中も絶対に維持する（雑談MC中にボイチェンが切れて地声が露出するのを防ぐ）。
        const bool henshinOn = (eraNow >= 180) && (mods.displayGain (gz::ModuleChain::Henshin) > 0.5f);

        const bool vcOn = (apvts.getRawParameterValue ("vc_on")->load() > 0.5f)
                            && henshinOn && ! sessionOn && srcPitchOk
                          #if VOCALGZZIO_LITE
                            && false   // Lite版は非搭載
                          #endif
                            ;
        const bool jnOn = (apvts.getRawParameterValue ("jn_on")->load() > 0.5f)
                            && henshinOn && ! sessionOn && ! talkMc && srcPitchOk
                          #if VOCALGZZIO_LITE
                            && false   // Lite版は非搭載
                          #endif
                            ;
        const bool atOn = (apvts.getRawParameterValue ("at_on")->load() > 0.5f)
                            && henshinOn && ! sessionOn && ! talkMc && srcPitchOk
                          #if VOCALGZZIO_LITE
                            && false   // Lite版は非搭載
                          #endif
                            ;

        const bool pitchActive = vcOn || atOn;   // vcSh runs for the voice-changer AND/OR auto-tune
        // v5.1.2 ピッチ補正だけのとき(ボイス変換OFF・💎以外)は時間領域ピッチ同期エンジン(約2.9ms)を使う
        const bool useTd = pitchActive && ! vcOn && pitchLatMode.load (std::memory_order_relaxed) < 3;

        // v1.9.0 修正: 停止中のシフターには前回の音・位相・OLA が残っている。
        // そのまま再開すると 1 窓ぶん(約17ms)、入力の10倍を超える音が飛び出す
        // ("押した瞬間に爆音" の原因)。有効化の瞬間に必ず初期化する。
        if (pitchActive && (! vcWasActive || useTd != tdWasActive))
        {
            if (useTd) { for (auto& s : tdSh) s.reset(); tdBodyLp[0] = tdBodyLp[1] = 0.0f; }
            else       { for (auto& s : vcSh) s.reset(); }
        }
        tdWasActive = useTd;
        if (jnOn && ! jnWasActive)
        {
            for (auto& s : unSh) s.reset();
            jnContra.reset();                            // v2.0.0 反行の状態も初期化
            for (float& hs : jnHeldSemi) hs = 0.0f;
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
        // v5.1.1: 3段階遅延モード（0:⚡超低遅延 ~10.6ms / 1:⭐標準低遅延 ~21.3ms / 2:💎最高音質 ~42.6ms）の動的反映（確保ゼロ）
        {
            const int wantOrd = gz::VoiceShifter::windowOrderForLatencyMode (currentSampleRate, pitchLatMode.load (std::memory_order_relaxed));
            const int wantUn  = std::max (wantOrd, gz::VoiceShifter::windowOrderForLatencyMode (currentSampleRate, 1));
            if (vcSh[0].currentWindow() != (1 << wantOrd))
                for (auto& s : vcSh) s.setWindow (wantOrd);
            // ハモリ声部は主旋律の横に足す並列処理なので、極限モードでも512点以上を保って音質を守る
            if (unSh[0].currentWindow() != (1 << wantUn))
                for (auto& s : unSh) s.setWindow (wantUn);
        }
        voiceLatency = pitchActive ? (useTd ? tdSh[0].latencySamples() : vcSh[0].latencySamples()) : 0;
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
            atDetectedHz.store (hz);

            // ---- v2.7.0 こぶし(しゃくり・こぶし保護) & v5.1.4 規則的ビブラート確信度 ----
            // 音程の「動き方」だけを見て、しゃくり／こぶし／音の渡りの最中だけ
            // 補正を引っ込め、規則的なビブラートと不規則なふらつきを分離する。
            // 音そのものには一切触らないのでゼロ遅延のまま。詳細は Source/Ornament.h。
            float keep    = 1.0f;
            float vibConf = 0.0f;
            {
                const float ornAmt = (eraNow >= 270)
                                   ? apvts.getRawParameterValue ("orn_amt")->load() * 0.01f : 0.0f;
                ornGuard.setAmount (ornAmt);
                // 半音単位の音高(無声は0以下)を渡す。基準ピッチは補正側と同じもの。
                keep    = ornGuard.process (midi, blockSec);
                vibConf = ornGuard.vibratoConfidence();
                if (ornAmt > 0.001f)
                {
                    meterOrn.store (ornGuard.lastProtect());
                    meterOrnKind.store (ornGuard.lastKind());
                }
                else if (meterOrn.load() != 0.0f) { meterOrn.store (0.0f); meterOrnKind.store (0); }
            }

            // ケロケロ判定: 速さがほぼ0で、補正量も強い設定のとき
            const bool hard = atOn && (speed <= 8.0f) && (amount >= 0.90f);

            // v5.1.4: 歌を上手く聴かせるゼロ遅延軌道エンジン (processSkilled)
            //  - ノート間レガート軌跡(smoothedNote)とノート内即時制振(directCorr)を分離し、
            //    従来の160ms中心フィルタ遅れ＋補正ローパス遅れによる「遅れて動くラグ感・うねり」を根絶。
            //  - 規則的ビブラート中はビブラートをそのまま残し、ロングトーンや語尾のフラット垂れは先回りして支える。
            if (! atOn)
            {
                pitchCorrection.reset();
                atCorrection = 0.0f;  // ハモリ用の検出だけで補正を有効にしない
            }
            else
            {
                atCorrection = pitchCorrection.processSkilled (midi, blockSec, key, scId, speed, amount, keep, vibConf);
            }
            atCorr = atCorrection;
            atCurrentCorrection.store (atCorr);

            // v1.9.0: 音程が合っているときはボコーダを通さず素の声のまま出す。
            // v5.1.1: 旧版(0.05半音〜0.02半音のヒステリシス＋15ms遷移)では、微修正中に
            //         atWetMix が中間値になって「補正前の原音」と「数セントずれた補正音」が混ざり、
            //         フランジャー/コーラス状の『うなり・二重声』が発生していた。
            //         0.002半音(0.2セント)以上の実補正がある間は 1.5ms で即座に 100% Wet へ振り切り、
            //         二重声のうなりを完全に根絶する（完全ゼロ時のインパルス自己診断は 0.0f のまま通過）。
            if (hard)
            {
                atWetMix = 1.0f;
            }
            else
            {
                const float mag = std::abs (atCorr);
                const float want = (mag > 0.002f) ? 1.0f : (mag < 0.0008f ? 0.0f : atWetMix);
                const float tauMix = (want > atWetMix) ? 0.0015f : 0.012f;
                const float aMix = 1.0f - std::exp (-blockSec / tauMix);
                atWetMix += (want - atWetMix) * aMix;
            }
        }
        else { atCorrection = 0.0f; atWetMix = 0.0f; pitchWasTracking = false; pitchCorrection.reset(); atDetectedHz.store (0.0f); atCurrentCorrection.store (0.0f);
               if (meterOrn.load() != 0.0f) { meterOrn.store (0.0f); meterOrnKind.store (0); } }

        if (pitchActive || jnOn)
        {
            const float vcPitRaw = vcOn ? apvts.getRawParameterValue ("vc_pitch")->load() : 0.0f;
            const float pit  = vcPitRaw + atCorr;
            const float frm  = vcOn ? apvts.getRawParameterValue ("vc_form")->load() : 0.0f;
            // 声変え時はユーザーのミックス。ピッチ補正だけのときは「補正が要る間だけ」通す。
            const float vmix = vcOn ? apvts.getRawParameterValue ("vc_mix")->load() * 0.01f : atWetMix;

            if (pitchActive)
            {
                const bool applyVcNat = vcOn
                                     && (std::abs (vcPitRaw) > 0.15f || std::abs (frm) > 0.15f)
                                     && vmix > 0.01f
                                     && ! selfTestRunning.load (std::memory_order_relaxed);
                if (applyVcNat)
                {
                    if (std::abs (vcPitRaw - vcNatLastPitch) > 0.05f
                        || std::abs (frm - vcNatLastForm) > 0.05f
                        || std::abs (currentSampleRate - vcNatLastSr) > 1.0)
                    {
                        vcNatLastPitch = vcPitRaw;
                        vcNatLastForm  = frm;
                        vcNatLastSr    = currentSampleRate;
                        const float hpHz = juce::jlimit (65.0f, 115.0f,
                            72.0f + juce::jmax (0.0f, -vcPitRaw * 3.0f - frm * 1.8f));
                        const float bodyDb = juce::jlimit (-2.4f, 2.6f,
                            vcPitRaw * 0.20f + frm * 0.16f);
                        const float deHarshDb = juce::jlimit (-3.2f, 1.6f,
                            -juce::jmax (0.0f, vcPitRaw * 0.25f + frm * 0.20f)
                            + juce::jmax (0.0f, -vcPitRaw * 0.12f));
                        const auto cHp   = ACoefs::makeHighPass (currentSampleRate, hpHz, 0.707f);
                        const auto cBody = ACoefs::makePeakFilter (currentSampleRate, 215.0f, 0.85f,
                                               juce::Decibels::decibelsToGain (bodyDb * vmix));
                        const auto cDeH  = ACoefs::makePeakFilter (currentSampleRate, 6200.0f, 1.35f,
                                               juce::Decibels::decibelsToGain (deHarshDb * vmix));
                        for (int ch = 0; ch < 2; ++ch)
                        {
                            *vcNatHp[ch].coefficients      = cHp;
                            *vcNatBody[ch].coefficients    = cBody;
                            *vcNatDeHarsh[ch].coefficients = cDeH;
                        }
                    }
                }

                for (int ch = 0; ch < juce::jmin (numCh, 2); ++ch)
                {
                    float* p = buffer.getWritePointer (ch);
                    // v2.6.0 安全弁: 位相メモリ(再帰状態)は一度 NaN/Inf が入ると
                    // 自己回復せず永久に無音を出し続ける。加工前の音を取っておき、
                    // 壊れたブロックは元の音に差し戻してシフターを初期化する。
                    // 次のブロックからは何事もなく続く(欠けは最大1ブロック=数ms)。
                    const int nn = juce::jmin (numSamples, (int) vcDry[ch].size());
                    juce::FloatVectorOperations::copy (vcDry[ch].data(), p, nn);
                    if (useTd)
                    {
                        // v5.1.4 ピッチ補正専用: 検出した周期でつなぐ時間領域エンジン(4サンプル=0.08ms、FFT窓の待ち時間なし)
                        tdSh[ch].setPitchHz (atDetectedHz.load (std::memory_order_relaxed));
                        tdSh[ch].setParams (atCorr, vmix);
                        tdSh[ch].processBlock (p, numSamples);

                        // v5.1.4 ピッチ補正連動・声芯フォルマント傾き補正(ゼロ遅延):
                        // フラットした高音を上へ引き上げた際(atCorr > 0)に時間領域シフトで声道共鳴が細く薄くなるのを防ぐため、
                        // シフト量に比例して 240Hz 帯の声芯(チェスト感)をわずかに補い、常に太く余裕のある歌声に保つ。
                        // atCorr == 0 のときは tiltGain == 0 で完全に恒等(ビット単位で素通し)。
                        const float tiltGain = (! selfTestRunning.load (std::memory_order_relaxed))
                                             ? juce::jlimit (-0.08f, 0.14f, atCorr * 0.09f * vmix) : 0.0f;
                        const float lpAlpha  = (float) (1.0 - std::exp (-2.0 * 3.14159265358979323846 * 240.0 / currentSampleRate));
                        float lpState = tdBodyLp[ch];
                        for (int i = 0; i < numSamples; ++i)
                        {
                            const float s = p[i];
                            if (std::isfinite (s))
                            {
                                lpState += lpAlpha * (s - lpState);
                                p[i] = s + tiltGain * lpState;
                            }
                        }
                        tdBodyLp[ch] = lpState;
                    }
                    else
                    {
                        vcSh[ch].setParams (pit, frm, vmix);
                        vcSh[ch].processBlock (p, numSamples);
                    }
                    if (applyVcNat)
                    {
                        const float warmDrive = 0.032f * vmix;
                        for (int i = 0; i < numSamples; ++i)
                        {
                            float x = vcNatHp[ch].processSample (p[i]);
                            x = vcNatBody[ch].processSample (x);
                            x = vcNatDeHarsh[ch].processSample (x);
                            // 声帯の非対称振動に近い微細な偶数次倍音でボイチェン特有の人工的な薄さを補う
                            x += warmDrive * (x * std::abs (x) - 0.30f * x * x * x);
                            p[i] = x;
                        }
                    }
                    if (! gzBlockFinite (p, numSamples))
                    {
                        juce::FloatVectorOperations::copy (p, vcDry[ch].data(), nn);
                        vcSh[ch].reset();
                        tdSh[ch].reset();
                        vcNatHp[ch].reset();
                        vcNatBody[ch].reset();
                        vcNatDeHarsh[ch].reset();
                    }
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
                const int   harm = juce::jlimit (0, 17, (int) apvts.getRawParameterValue ("jn_harm")->load());
                // per-member character: detune (cents), formant (st), pan
                static const float dCents[4] = { +9.0f, -11.0f, +16.0f, -15.0f };
                static const float dForm [4] = { +1.5f,  -1.5f,  +3.2f,  -2.8f };
                static const float pan   [4] = { -0.6f,  +0.6f,  -0.3f,  +0.3f };
                // v1.9.8: ハモリを「半音固定」から「音階の度数」へ。
                // v5.1.1: 9種→18種へ倍増（0..8のインデックス互換は完全維持）。
                //   9=オク下, 10=オク上, 11=オクW(上下同時), 12=3度W(上3+下3),
                //   13=4度上(モダン), 14=5度下(重厚), 15=3声コーラス(下3+上3+上5),
                //   16=4声ゴスペル(下6+下3+上3+上5), 17=ペダル・ドローン(主音+5度保持)
                static const int hDeg[18][4] = {
                    {  0,  0,  0,  0 },   // 0  ユニゾン
                    {  0,  0, +2, +2 },   // 1  3度上
                    {  0,  0, -2, -2 },   // 2  3度下
                    {  0,  0, +5, +5 },   // 3  6度上(3度のオクターブ違い・柔らかい)
                    {  0,  0, -5, -5 },   // 4  6度下
                    {  0,  0, +4, +4 },   // 5  5度上(硬く澄んだ響き)
                    {  0,  0, +2, +4 },   // 6  3度＋5度
                    {  0,  0, +2, +2 },   // 7  おまかせ(上昇フレーズは下・下降は上)
                    {  0,  0,  0,  0 },   // 8  反行(メロディと逆に動く対旋律)
                    {  0,  0, -7, -7 },   // 9  オクターブ下(-12st)
                    {  0,  0, +7, +7 },   // 10 オクターブ上(+12st)
                    { -7, -7, +7, +7 },   // 11 オクターブW(上下オクターブ同時重ね)
                    {  0,  0, +2, -2 },   // 12 3度W(3度上＋3度下の同時重ね)
                    {  0,  0, +3, +3 },   // 13 4度上(都会的・モダンな4度ハモ)
                    {  0,  0, -4, -4 },   // 14 5度下(重厚な下5度パワーハモ)
                    {  0, -2, +2, +4 },   // 15 3声コーラス(上3度＋上5度＋下3度)
                    { -5, -2, +2, +4 },   // 16 4声ゴスペル(下6度＋下3度＋上3度＋上5度)
                    {  0,  0,  0,  0 },   // 17 ペダル・ドローン(主音＋5度を持続保持)
                };
                const int   hKey  = (int) apvts.getRawParameterValue ("at_key")->load();
                const int   hScId = (int) apvts.getRawParameterValue ("at_scale")->load();
                const float hzH   = atDetectedHz.load();

                // モードが変わったら、保持していた音程と反行の状態を捨てる
                if (harm != jnLastHarm)
                {
                    jnLastHarm = harm;
                    for (float& hs : jnHeldSemi) hs = 0.0f;
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
                    else if (harm == 17) // ペダル・ドローン: 歌い手の音域に近い主音(Root)と完全5度(+7st)を持続保持
                    {
                        const int oct = (int) std::round ((pH - (float) hKey) / 12.0f);
                        const float rootMidi = (float) (hKey + oct * 12);
                        jnPedalMidi = rootMidi;
                        hSemi[0] = hSemi[2] = juce::jlimit (-12.0f, 12.0f, rootMidi - pH);
                        hSemi[1] = hSemi[3] = juce::jlimit (-12.0f, 12.0f, (rootMidi + 7.0f) - pH);
                    }
                    else
                    {
                        for (int v = 0; v < 4; ++v)
                        {
                            const int deg = hDeg[harm][v] * flip;
                            if (deg == 0) continue;
                            if (deg == 7)       { hSemi[v] = +12.0f; continue; }
                            else if (deg == -7) { hSemi[v] = -12.0f; continue; }
                            float note = gz::scale::step (pH, hKey, hScId, deg);
                            // トライトーン(6半音)は強い濁り。1度ずらして避ける
                            if (std::abs ((int) std::lround (note - pH)) == 6)
                                note = gz::scale::step (pH, hKey, hScId, deg + (deg > 0 ? 1 : -1));
                            hSemi[v] = note - pH;      // 歌い手の抑揚に寄り添う
                        }
                    }

                    for (int v = 0; v < 4; ++v)
                        jnHeldSemi[v] = hSemi[v];
                }
                else if (harm > 0)
                {
                    // v2.0.0/v5.1.1: 無声区間(子音・ブレス)は直前の4声すべてのハモリ音程を保持
                    for (int v = 0; v < 4; ++v)
                        hSemi[v] = jnHeldSemi[v];
                }

                float* outL = buffer.getWritePointer (0);
                float* outR = numCh > 1 ? buffer.getWritePointer (1) : outL;

                // v1.9.9「ハモだけ」: 原音を消してハモリ声部だけを残す。
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

                    // ハモだけ出力時、ユニゾンパート(度数0)の声部はミュートし、実際に和音を歌っている声部のみ鳴らす
                    const bool isUnisonVoice = (harm > 0 && harm != 17 && hDeg[harm][v] == 0);
                    const float g  = (skipUnisonPair && isUnisonVoice) ? 0.0f
                                   : (skipUnisonPair ? 0.64f : 0.45f) * jmix;
                    const bool monoOpt = outputMonoMode.load (std::memory_order_relaxed)
                                      && ! selfTestRunning.load (std::memory_order_relaxed);
                    const float panV = monoOpt ? 0.0f : pan[v];
                    const float gL = g * 0.5f * (1.0f - panV);
                    const float gR = g * 0.5f * (1.0f + panV);
                    // v5.1.1: ハモリ選択時(harm > 0)はアンサンブル遅延を約半分(5〜14ms)に引き締め、主メロへの追従感を高める
                    float* dl = unDelay[v]; int w = unDelayW[v];
                    const int d = (harm > 0) ? juce::jmax (1, (unDelaySmp[v] * 45) / 100) : unDelaySmp[v];
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
        const float popAmt = (eraNow >= 230)
                           ? apvts.getRawParameterValue ("pop_amt")->load() * 0.01f : 0.0f;
        const bool  declickSw = (eraNow >= 510) && lipDeclickOn.load (std::memory_order_relaxed);
        const float lipAmt = (eraNow >= 230)
                           ? juce::jmax (declickSw ? 0.65f : 0.0f,
                                         apvts.getRawParameterValue ("lip_amt")->load() * 0.01f) : 0.0f;

        if (popAmt > 0.001f || lipAmt > 0.001f || declickSw)
        {
            auto* L = buffer.getWritePointer (0);
            auto* R = numCh > 1 ? buffer.getWritePointer (1) : nullptr;
            float popPk = 0.0f, lipPk = 0.0f;

            for (int n = 0; n < numSamples; ++n)
            {
                // v5.1.0 💧 2階差分トランジェント・ディクリッカー（口のペチャ音・舌打ちの瞬間パルス補間）
                if (declickSw)
                {
                    for (int ch = 0; ch < juce::jmin (numCh, 2); ++ch)
                    {
                        float* p = (ch == 0) ? L : R;
                        if (p == nullptr) break;
                        const float x0 = p[n];
                        const float d1 = std::abs (x0 - lipPrev1[ch]);
                        const float d2 = std::abs (x0 - 2.0f * lipPrev1[ch] + lipPrev2[ch]);
                        lipSlewEnv += 0.004f * (d1 - lipSlewEnv);
                        const float thr = juce::jmax (0.0025f, lipSlewEnv * 6.5f);
                        if (d2 > thr)
                        {
                            const float pred = 1.5f * lipPrev1[ch] - 0.5f * lipPrev2[ch];
                            const float atten = juce::jlimit (0.0f, 0.85f, (d2 - thr) / (thr * 3.0f + 1.0e-5f));
                            p[n] = x0 + atten * (pred - x0);
                            lipPk = juce::jmax (lipPk, atten);
                        }
                        lipPrev2[ch] = lipPrev1[ch];
                        lipPrev1[ch] = p[n];
                    }
                }

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
        const float amt = (eraNow >= 240)
                        ? apvts.getRawParameterValue ("ride_amt")->load() * 0.01f : 0.0f;
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
        const float consAmt = (srcVoiceOnly && eraNow >= 260)
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
        const float brAmt = (srcBreathOk && eraNow >= 200)
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
        const float ringAmt = (srcVoiceOnly && eraNow >= 200)
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
            const int chIdx = juce::jlimit (0, 1, ch);
            if (driveAmt > 0.001f)
                x = gz::argon::VelvetEngine::adaaTanh (x * k, warmPrevU[chIdx]) / k;
            else
                warmPrevU[chIdx] = x * k;
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
            const float tanhBias = std::tanh (bias);
            for (int ch = 0; ch < numCh; ++ch)
            {
                const float x = buffer.getSample (ch, n);
                const int chIdx = juce::jlimit (0, 1, ch);
                const float u = x * lg * (1.0f + susAmt) + bias;
                const float sat  = gz::argon::VelvetEngine::adaaTanh (u, susPrevU[chIdx]) - tanhBias;
                buffer.setSample (ch, n, x * (1.0f - 0.5f * susAmt)
                    + sat * 0.5f * susAmt + x * (lg - 1.0f) * 0.6f);
            }
        }
        else
        {
            susPrevU[0] = susPrevU[1] = 0.0f;
        }
    }
    // v5.0.0 柱④ ＆ v5.1.0 柱②：サビ1クリック華やか演出（Chorus Boost）＋ 🤖 サビ自動検知オートブースト（Auto-Chorus Director）
    if (eraNow >= 510 && autoChorusMode.load (std::memory_order_relaxed)
        && ! liveTalkMode.load (std::memory_order_relaxed)
        && ! selfTestRunning.load (std::memory_order_relaxed))
    {
        const float blockSec = (float) numSamples / (float) juce::jmax (8000.0, currentSampleRate);
        const bool highVocalEnergy = (beltFastDb > -28.0f && (beltFastDb - beltSlowDb > 2.4f || beltNow > 0.15f))
                                  || (beltFastDb > -16.5f);
        if (highVocalEnergy)
        {
            autoChorusHoldSec = 2.4f; // サビ中の短いブレス（息継ぎ）でもブーストが途切れないよう2.4秒ホールド
            autoChorusTriggered.store (true, std::memory_order_relaxed);
        }
        else
        {
            autoChorusHoldSec = juce::jmax (0.0f, autoChorusHoldSec - blockSec);
            if (autoChorusHoldSec <= 0.0f || beltFastDb < -44.0f)
                autoChorusTriggered.store (false, std::memory_order_relaxed);
        }
    }
    else if (autoChorusTriggered.load (std::memory_order_relaxed))
    {
        autoChorusHoldSec = 0.0f;
        autoChorusTriggered.store (false, std::memory_order_relaxed);
    }

    const bool wantChorusBoost = (eraNow >= 500)
                              && (chorusBoostMode.load (std::memory_order_relaxed)
                                  || (eraNow >= 510 && autoChorusMode.load (std::memory_order_relaxed)
                                      && autoChorusTriggered.load (std::memory_order_relaxed)))
                              && ! liveTalkMode.load (std::memory_order_relaxed)
                              && ! selfTestRunning.load (std::memory_order_relaxed);
    const float cbAlpha = 1.0f - std::exp (-(float) numSamples / juce::jmax (1.0f, 0.025f * (float) currentSampleRate));
    chorusBoostEnv += cbAlpha * ((wantChorusBoost ? 1.0f : 0.0f) - chorusBoostEnv);

    if (neiroRun)
    {
        if (eraNow >= 440)
        {
            const bool monoOpt = (outputMonoMode.load (std::memory_order_relaxed)
                               || liveTalkMode.load (std::memory_order_relaxed))
                              && ! selfTestRunning.load (std::memory_order_relaxed);
            const float arVelvet = apvts.getRawParameterValue ("argon_velvet")->load() * 0.01f;
            const float arFocus  = juce::jlimit (0.0f, 1.0f, apvts.getRawParameterValue ("argon_focus")->load() * 0.01f + 0.12f * chorusBoostEnv);
            const float arSilk   = juce::jlimit (0.0f, 1.0f, apvts.getRawParameterValue ("argon_silk")->load()  * 0.01f + 0.18f * chorusBoostEnv);
            const float arHalo   = monoOpt ? 0.0f : juce::jlimit (0.0f, 1.0f, apvts.getRawParameterValue ("argon_halo")->load() * 0.01f + 0.22f * chorusBoostEnv);
            argonEngine.setParams (arVelvet, arSilk, arHalo, crushCg, srcVoiceOnly, arFocus);
            argonEngine.process (buffer.getWritePointer (0),
                                 numCh > 1 ? buffer.getWritePointer (1) : nullptr,
                                 numSamples);
            meterArgonVelvet.store (argonEngine.velvetActivity(), std::memory_order_relaxed);
            meterArgonFocus.store  (argonEngine.focusActivity(),  std::memory_order_relaxed);
            meterArgonSilk.store   (argonEngine.silkActivity(),   std::memory_order_relaxed);
            meterArgonHalo.store   (argonEngine.haloActivity(),   std::memory_order_relaxed);
            meterArgonVoiced.store (argonEngine.voicedRatioNow(), std::memory_order_relaxed);
        }
        else
        {
            meterArgonVelvet.store (0.0f, std::memory_order_relaxed);
            meterArgonFocus.store  (0.0f, std::memory_order_relaxed);
            meterArgonSilk.store   (0.0f, std::memory_order_relaxed);
            meterArgonHalo.store   (0.0f, std::memory_order_relaxed);
            meterArgonVoiced.store (0.0f, std::memory_order_relaxed);
        }
        mods.restore (gz::ModuleChain::Neiro, buffer);
    }
    else
    {
        // 音色づくりを切った後も、最後に処理した値が画面に残らないようにする。
        meterArgonVelvet.store (0.0f, std::memory_order_relaxed);
        meterArgonFocus.store  (0.0f, std::memory_order_relaxed);
        meterArgonSilk.store   (0.0f, std::memory_order_relaxed);
        meterArgonHalo.store   (0.0f, std::memory_order_relaxed);
        meterArgonVoiced.store (0.0f, std::memory_order_relaxed);
    }
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
    const float liftNow     = juce::jlimit (0.0f, 1.5f, liftNowV + 0.65f * chorusBoostEnv);

    // ===== モジュール8「ひろがり」（かさね・ひろがり・コーラス・やまびこ・ひびき）=====
    // v3.0-b ★ここから「OFF なら**処理そのものを飛ばす**」。
    //  v3.0-a では走らせて出力を捨てていた（音は素通しだが CPU は減らなかった）。
    //  ひろがりから始めたのは、チェーンでいちばん重いから
    //  （ディレイと ひびき のディレイライン、コーラス4声、ダッキング検出）。
    //  飛ばすので save/restore も要らない ＝ buffer に一切触らない ＝ 完全な素通し。
    //  渡し中（クロスフェード）のあいだは今までどおり走らせて混ぜる。
    const bool spaceRun = ! mods.isOff (gz::ModuleChain::Hirogari);
    const bool talkMcSpace = liveTalkMode.load (std::memory_order_relaxed)
                          && ! selfTestRunning.load (std::memory_order_relaxed);
    if (spaceRun) mods.save (gz::ModuleChain::Hirogari, buffer);

    // ---- Doubler (modulated short delay, mono-safe L/R inversion) + Width ----
    if (spaceRun)
    {
        const bool monoOpt = outputMonoMode.load (std::memory_order_relaxed)
                          && ! selfTestRunning.load (std::memory_order_relaxed);
        const bool  dblOn    = apvts.getRawParameterValue ("dbl_on")->load() > 0.5f && ! talkMcSpace;
        const float dblAmt   = juce::jlimit (0.0f, 1.0f,
                                   (dblOn ? apvts.getRawParameterValue ("doubler")->load() * 0.01f : 0.0f)
                                   * (1.0f + 0.35f * liftNow) + (talkMcSpace ? 0.0f : 0.22f * chorusBoostEnv));
        const float widthAmt = (monoOpt || talkMcSpace) ? 0.0f : juce::jlimit (0.0f, 1.0f,
                                   apvts.getRawParameterValue ("width")->load() * 0.01f
                                   * (1.0f + 0.30f * emoBloomNow + 0.45f * liftNow)
                                   + 0.06f * emoBloomNow + 0.08f * liftNow + 0.20f * chorusBoostEnv);
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

                if (monoOpt)
                {
                    // cluster / VRChat 等のモノラル最適化モード:
                    // 逆相(±side)によるコムフィルタ干渉や L+R 加算時の消失を防ぎ、
                    // 同相センターの揺らぎダブリングとして L/R 両方へ同一波形を重ねる。
                    const float m = mid + dblAmt * 0.25f * tap;
                    L[n] = m;
                    R[n] = m;
                }
                else
                {
                    // v2.8.0: もとの左右差(side0)を捨てないようにした。
                    const float side0 = 0.5f * (L[n] - R[n]);
                    const float side  = dblAmt * 0.5f * tap + widthAmt * 0.8f * wtap;
                    L[n] = mid + side0 + side;
                    R[n] = mid - side0 - side;
                }
            }
        }
    }

    // ---- v1.4.0 Chorus (wet-only voices; dry path untouched -> zero latency) ----
    if (spaceRun)
    {
        const bool  choOn  = apvts.getRawParameterValue ("cho_on")->load() > 0.5f && ! talkMcSpace;
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
            meterDuckDb.store (juce::jmax (0.0f, -juce::Decibels::gainToDecibels (duckGain, -24.0f)),
                               std::memory_order_relaxed);
        }
        else
        {
            duckGain = 1.0f;
            std::fill (duckGainBuf.begin(), duckGainBuf.begin() + numSamples, 1.0f);
            meterDuckDb.store (0.0f, std::memory_order_relaxed);
        }
    }
    else
    {
        meterDuckDb.store (0.0f, std::memory_order_relaxed);
    }

    // ---- Delay (tempo-syncable echo, feedback highcut, ducked wet) ----
    if (spaceRun)
    {
        // v2.10.0 しゃべり配信ではやまびこを切る(聞き取りを妨げるため)。
        // v4.7.0 雑談MCモード(talkMcSpace)でも歌設定を壊さずやまびこを素通しにする。
        const bool  dlyOn = apvts.getRawParameterValue ("dly_on")->load() > 0.5f && srcSpaceOk && ! talkMcSpace;
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
        // v4.7.0 雑談MCモード(talkMcSpace)中は revOn=false として 12ms フェードアウトで残響をスッと消す。
        const bool  revOn  = apvts.getRawParameterValue ("revon")->load() > 0.5f && srcSpaceOk && ! talkMcSpace;
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

            if (rtype >= kHeyaFirst && eraNow >= 400 && heyaReady.load())
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

    // ---- v4.8.0 柱④：🛡 絶叫セーフティガード（突発爆音・台パンの耳テロ防止ソフトクリップ＆リミッター） ----
    // 普段の歌声やトーク（-0.8 dBFS = 0.912 以下）には 1ビットも触れず、突発的な絶叫・くしゃみ・台パンが
    // 来た瞬間だけ 0.2ms の超高速アタックで捕まえ、-0.3 dBFS (0.966) 天井で歪みなく受け止める。
    const bool anyModActive = ! (mods.isOff (0) && mods.isOff (1) && mods.isOff (2) && mods.isOff (3)
                              && mods.isOff (4) && mods.isOff (5) && mods.isOff (6) && mods.isOff (7));
    if (! selfTestRunning.load (std::memory_order_relaxed)
        && eraNow >= 480
        && screamGuardOn.load (std::memory_order_relaxed)
        && ! compareBypass.load (std::memory_order_relaxed)
        && anyModActive
        && ! (apvts.getRawParameterValue ("crush_on")->load() > 0.5f && mods.displayGain (gz::ModuleChain::Soroe) > 0.5f))
    {
        const float ceiling = 0.96605f; // -0.3 dBFS
        const float kneeThr = 0.91201f; // -0.8 dBFS
        const float atkA = 1.0f - std::exp (-1.0f / juce::jmax (1.0f, 0.0002f * (float) currentSampleRate));
        const float relA = 1.0f - std::exp (-1.0f / juce::jmax (1.0f, 0.0850f * (float) currentSampleRate));
        float* L = buffer.getWritePointer (0);
        float* R = numCh > 1 ? buffer.getWritePointer (1) : nullptr;
        float maxRedDb = 0.0f;
        for (int n = 0; n < numSamples; ++n)
        {
            const float pk = R ? juce::jmax (std::abs (L[n]), std::abs (R[n])) : std::abs (L[n]);
            screamGuardEnv += (pk > screamGuardEnv ? atkA : relA) * (pk - screamGuardEnv);
            if (screamGuardEnv > kneeThr)
            {
                const float gainLim = juce::jlimit (0.05f, 1.0f, ceiling / juce::jmax (ceiling, screamGuardEnv));
                L[n] *= gainLim;
                if (R) R[n] *= gainLim;
                const float redDb = -juce::Decibels::gainToDecibels (gainLim, -36.0f);
                if (redDb > maxRedDb) maxRedDb = redDb;
            }
            // ソフトサチュレーション天井で万が一の1サンプル突発ピークも -0.3dBFS 以内に収める
            if (std::abs (L[n]) > kneeThr)
                L[n] = std::copysign (kneeThr + (ceiling - kneeThr) * std::tanh ((std::abs (L[n]) - kneeThr) / (ceiling - kneeThr)), L[n]);
            if (R && std::abs (R[n]) > kneeThr)
                R[n] = std::copysign (kneeThr + (ceiling - kneeThr) * std::tanh ((std::abs (R[n]) - kneeThr) / (ceiling - kneeThr)), R[n]);
        }
        const float prevDb = meterScreamGuardDb.load (std::memory_order_relaxed);
        meterScreamGuardDb.store (maxRedDb > prevDb ? maxRedDb : prevDb * 0.92f, std::memory_order_relaxed);
    }
    else
    {
        screamGuardEnv = 0.0f;
        meterScreamGuardDb.store (0.0f, std::memory_order_relaxed);
    }

    // ---- v4.8.0 柱①：🎵 カラオケ・BGMプレイヤー生成（キー変更 ±6半音 ＆ 声優先オートBGMダッキング） ----
    bool bgmMixedNow = false;
    const bool bgmSendToStream = bgmToStream.load (std::memory_order_relaxed);
    if (! selfTestRunning.load (std::memory_order_relaxed)
        && bgmPlaying.load (std::memory_order_relaxed)
        && bgmLoadedSamples.load (std::memory_order_relaxed) > 0
        && bgmBlockScratch.getNumSamples() >= numSamples)
    {
        const juce::SpinLock::ScopedTryLockType tl (bgmAudioLock);
        if (tl.isLocked() && bgmAudioBuf != nullptr && bgmAudioBuf->getNumSamples() > 0)
        {
            const int bgmLen = bgmAudioBuf->getNumSamples();
            const float* srcL = bgmAudioBuf->getReadPointer (0);
            const float* srcR = bgmAudioBuf->getReadPointer (bgmAudioBuf->getNumChannels() > 1 ? 1 : 0);
            float* dstL = bgmBlockScratch.getWritePointer (0);
            float* dstR = bgmBlockScratch.getWritePointer (1);

            double rpos = bgmReadPos.load (std::memory_order_relaxed);
            const bool loop = bgmLooping.load (std::memory_order_relaxed);
            const float bgmLinGain = juce::Decibels::decibelsToGain (bgmGainDb.load (std::memory_order_relaxed), -48.0f);
            const int semi = juce::jlimit (-6, 6, bgmPitchSemi.load (std::memory_order_relaxed));
            const float pitchRatio = semi == 0 ? 1.0f : std::pow (2.0f, (float) semi / 12.0f);
            const float duckAmt = bgmDuckAmt.load (std::memory_order_relaxed);
            const float duckThr = juce::Decibels::decibelsToGain (-38.0f);
            const float duckFloor = juce::Decibels::decibelsToGain (-14.0f * duckAmt);
            const float dAtk = 1.0f - std::exp (-1.0f / juce::jmax (1.0f, 0.015f * (float) currentSampleRate));
            const float dRel = 1.0f - std::exp (-1.0f / juce::jmax (1.0f, 0.350f * (float) currentSampleRate));

            const float* vocL = buffer.getReadPointer (0);
            const float* vocR = numCh > 1 ? buffer.getReadPointer (1) : vocL;
            const int ringSz = (int) bgmPitchRingL.size();
            const float grainW = (float) juce::jlimit (256, juce::jmax (512, ringSz / 3), (int) (0.036 * currentSampleRate));
            const bool vocalCancel = bgmVocalCancel.load (std::memory_order_relaxed);
            const float vcA160  = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * 160.0f  / (float) juce::jmax (8000.0, currentSampleRate));
            const float vcA8500 = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * 8500.0f / (float) juce::jmax (18000.0, currentSampleRate));

            for (int n = 0; n < numSamples; ++n)
            {
                float sL = 0.0f, sR = 0.0f;
                int idx = (int) rpos;
                if (idx >= bgmLen)
                {
                    if (loop) { idx %= bgmLen; rpos = (double) idx; }
                    else { bgmPlaying.store (false, std::memory_order_relaxed); rpos = 0.0; idx = -1; }
                }
                if (idx >= 0 && idx < bgmLen)
                {
                    sL = srcL[idx];
                    sR = srcR[idx];
                    rpos += 1.0;
                }

                // v5.0.0 柱①：伴奏ボーカル除去（1クリック原曲カラオケ化 — Mid/Side Spectral Center Canceller）
                // 160Hz以下のベース・キックと8.5kHz以上のステレオ空気感はそのまま保ち、
                // 160Hz〜8.5kHzの中央定位ボーカル(Mid)だけを88%減衰させて原曲を1クリックでカラオケ伴奏化する。
                if (vocalCancel)
                {
                    bgmVcLp160L  += vcA160  * (sL - bgmVcLp160L);
                    bgmVcLp160R  += vcA160  * (sR - bgmVcLp160R);
                    bgmVcLp8500L += vcA8500 * (sL - bgmVcLp8500L);
                    bgmVcLp8500R += vcA8500 * (sR - bgmVcLp8500R);
                    const float midBandL = bgmVcLp8500L - bgmVcLp160L;
                    const float midBandR = bgmVcLp8500R - bgmVcLp160R;
                    const float centerVocal = 0.5f * (midBandL + midBandR);
                    sL -= centerVocal * 0.88f;
                    sR -= centerVocal * 0.88f;
                }

                // キー変更（±6半音・テンポ維持デュアルグレイン・ピッチシフター）
                if (semi != 0 && ringSz > 512)
                {
                    bgmPitchRingL[(size_t) bgmPitchRingW] = sL;
                    bgmPitchRingR[(size_t) bgmPitchRingW] = sR;
                    const float p1 = bgmPitchPhase;
                    const float p2 = (p1 + 0.5f < 1.0f) ? (p1 + 0.5f) : (p1 - 0.5f);
                    const float w1 = 0.5f * (1.0f - std::cos (juce::MathConstants<float>::twoPi * p1));
                    const float w2 = 0.5f * (1.0f - std::cos (juce::MathConstants<float>::twoPi * p2));
                    const float d1 = (pitchRatio >= 1.0f) ? ((1.0f - p1) * grainW) : (p1 * grainW);
                    const float d2 = (pitchRatio >= 1.0f) ? ((1.0f - p2) * grainW) : (p2 * grainW);
                    auto readTap = [ringSz, this] (float delaySamp, const std::vector<float>& rb) noexcept -> float
                    {
                        float rp = (float) bgmPitchRingW - delaySamp;
                        while (rp < 0.0f) rp += (float) ringSz;
                        const int i0 = ((int) rp) % ringSz;
                        const int i1 = (i0 + 1) % ringSz;
                        const float fr = rp - std::floor (rp);
                        return rb[(size_t) i0] * (1.0f - fr) + rb[(size_t) i1] * fr;
                    };
                    sL = readTap (d1, bgmPitchRingL) * w1 + readTap (d2, bgmPitchRingL) * w2;
                    sR = readTap (d1, bgmPitchRingR) * w1 + readTap (d2, bgmPitchRingR) * w2;
                    bgmPitchRingW = (bgmPitchRingW + 1) % ringSz;
                    bgmPitchPhase += std::abs (1.0f - pitchRatio) / grainW;
                    while (bgmPitchPhase >= 1.0f) bgmPitchPhase -= 1.0f;
                }

                // 声優先オートBGMダッキング（声が出ている間だけBGMを自動で下げる）
                const float vPk = juce::jmax (std::abs (vocL[n]), std::abs (vocR[n]));
                bgmDuckEnv += (vPk > bgmDuckEnv ? dAtk : dRel) * (vPk - bgmDuckEnv);
                const float wantDuck = (duckAmt > 0.001f && bgmDuckEnv > duckThr) ? duckFloor : 1.0f;
                bgmDuckGain += (wantDuck < bgmDuckGain ? dAtk : dRel) * (wantDuck - bgmDuckGain);

                const float g = bgmLinGain * bgmDuckGain;
                dstL[n] = sL * g;
                dstR[n] = sR * g;
            }
            bgmReadPos.store (rpos, std::memory_order_relaxed);
            meterBgmDuckDb.store (juce::jmax (0.0f, -juce::Decibels::gainToDecibels (bgmDuckGain, -24.0f)),
                                  std::memory_order_relaxed);
            bgmMixedNow = true;
        }
    }
    else
    {
        meterBgmDuckDb.store (0.0f, std::memory_order_relaxed);
    }

    // ---- v4.6.0 最終モノラル出力 (MONO OUT) ＆ マルチ出力ミラーリング ----
    if (! selfTestRunning.load (std::memory_order_relaxed))
    {
        if (outputMonoMode.load (std::memory_order_relaxed) && numCh >= 2)
        {
            float* L = buffer.getWritePointer (0);
            float* R = buffer.getWritePointer (1);
            for (int n = 0; n < numSamples; ++n)
            {
                const float m = 0.5f * (L[n] + R[n]);
                L[n] = m;
                R[n] = m;
            }
        }
    }

    // ---- v4.8.0 柱② ＆ v5.0.0 柱②：1クリックWAV録音・30秒さかのぼり保存（純粋なWet歌声＋Dry生声＋BGM伴奏を分離キャプチャ） ----
    if (! selfTestRunning.load (std::memory_order_relaxed))
    {
        const float* dryL = dryBuffer.getReadPointer (0);
        const float* dryR = dryBuffer.getReadPointer (dryBuffer.getNumChannels() > 1 ? 1 : 0);
        const float* wetL = buffer.getReadPointer (0);
        const float* wetR = buffer.getReadPointer (numCh > 1 ? 1 : 0);
        const float* bgmL = bgmMixedNow ? bgmBlockScratch.getReadPointer (0) : nullptr;
        const float* bgmR = bgmMixedNow ? bgmBlockScratch.getReadPointer (1) : nullptr;

        if (anyModActive
            && eraNow >= 480
            && ! compareBypass.load (std::memory_order_relaxed)
            && ! retroFreeze.load (std::memory_order_acquire)
            && retroDryBuf.getNumSamples() >= retroMaxSamples
            && retroWetBuf.getNumSamples() >= retroMaxSamples
            && retroBgmBuf.getNumSamples() >= retroMaxSamples
            && retroMaxSamples > 0)
        {
            int wp = retroWritePos.load (std::memory_order_relaxed);
            int fl = retroFilled.load (std::memory_order_relaxed);
            float* rdL = retroDryBuf.getWritePointer (0);
            float* rdR = retroDryBuf.getWritePointer (1);
            float* rwL = retroWetBuf.getWritePointer (0);
            float* rwR = retroWetBuf.getWritePointer (1);
            float* rbL = retroBgmBuf.getWritePointer (0);
            float* rbR = retroBgmBuf.getWritePointer (1);
            for (int n = 0; n < numSamples; ++n)
            {
                rdL[wp] = dryL[n];
                rdR[wp] = dryR[n];
                rwL[wp] = wetL[n];
                rwR[wp] = wetR[n];
                rbL[wp] = bgmL ? bgmL[n] : 0.0f;
                rbR[wp] = bgmR ? bgmR[n] : 0.0f;
                if (++wp >= retroMaxSamples) wp = 0;
            }
            retroWritePos.store (wp, std::memory_order_relaxed);
            retroFilled.store (juce::jmin (retroMaxSamples, fl + numSamples), std::memory_order_relaxed);
        }

        if (wavRecording.load (std::memory_order_acquire)
            && wavRecDryBuf.getNumSamples() >= wavRecMaxSamples
            && wavRecWetBuf.getNumSamples() >= wavRecMaxSamples
            && wavRecBgmBuf.getNumSamples() >= wavRecMaxSamples)
        {
            int rp = wavRecPos.load (std::memory_order_relaxed);
            float* wdL = wavRecDryBuf.getWritePointer (0);
            float* wdR = wavRecDryBuf.getWritePointer (1);
            float* wwL = wavRecWetBuf.getWritePointer (0);
            float* wwR = wavRecWetBuf.getWritePointer (1);
            float* wbL = wavRecBgmBuf.getWritePointer (0);
            float* wbR = wavRecBgmBuf.getWritePointer (1);
            for (int n = 0; n < numSamples && rp < wavRecMaxSamples; ++n, ++rp)
            {
                wdL[rp] = dryL[n];
                wdR[rp] = dryR[n];
                wwL[rp] = wetL[n];
                wwR[rp] = wetR[n];
                wbL[rp] = bgmL ? bgmL[n] : 0.0f;
                wbR[rp] = bgmR ? bgmR[n] : 0.0f;
            }
            wavRecPos.store (rp, std::memory_order_relaxed);
        }

        // BGMの配信出力混音設定がONのときは、録音用Wetステム分離後にメイン出力＆StreamOutへミックスする
        if (bgmMixedNow && bgmSendToStream)
        {
            buffer.addFrom (0, 0, bgmBlockScratch, 0, 0, numSamples);
            if (numCh > 1) buffer.addFrom (1, 0, bgmBlockScratch, 1, 0, numSamples);
        }

        // v5.0.0 柱③：4種ライブSEポン出しパッドのミキシング（自分の耳＆配信StreamOutの両方へ送出）
        const int seIdx = activeLiveSe.load (std::memory_order_acquire);
        if (seIdx >= 0 && seIdx < 4)
        {
            const auto& seBuf = liveSeBufs[seIdx];
            const int seLen = seBuf.getNumSamples();
            int sePos = liveSeReadPos.load (std::memory_order_relaxed);
            if (sePos < seLen && seLen > 0)
            {
                const int toMix = juce::jmin (numSamples, seLen - sePos);
                buffer.addFrom (0, 0, seBuf, 0, sePos, toMix);
                if (numCh > 1)
                    buffer.addFrom (1, 0, seBuf, seBuf.getNumChannels() > 1 ? 1 : 0, sePos, toMix);
                sePos += toMix;
                liveSeReadPos.store (sePos, std::memory_order_relaxed);
                if (sePos >= seLen)
                    activeLiveSe.store (-1, std::memory_order_release);
            }
            else
            {
                activeLiveSe.store (-1, std::memory_order_release);
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

    // ---- v2.10.0 #73 ゼロ遅延の自己証明（出口） ----
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

    // 🛡️ ハードウェアループバック／スピーカー発振（ハウリング）自動遮断ガード
    // AG03/AG06の「LOOPBACK」やRubix 44の「LOOPBACKスイッチ」がONのまま返しミュートがOFFだと、
    // 出力がそのまま入力へ戻って無限増幅（ピーッという発振）を起こす。
    // 入出力が同時に高レベルで0.35秒以上張り付いた場合はループバック発振と判定し、
    // 即座に「自分の耳への返しミュート(monitorMute)」を自動ONにして耳と機材を守る（配信出力StreamOutは維持）。
    if (isStandalone() && ! monitorMute.load (std::memory_order_relaxed)
        && auditionState.load (std::memory_order_relaxed) != AuditionPlaying
        && ! selfTestRunning.load (std::memory_order_relaxed))
    {
        const float inPkNow  = meterIn.load (std::memory_order_relaxed);
        const float outPkNow = buffer.getMagnitude (0, numSamples);
        if (inPkNow > 0.85f && outPkNow > 0.90f)
        {
            feedbackHoldSamples += numSamples;
            if (feedbackHoldSamples > (int) (0.35 * juce::jmax (8000.0, currentSampleRate)))
            {
                monitorMute.store (true, std::memory_order_relaxed);
                feedbackGuardTripped.store (true, std::memory_order_relaxed);
                feedbackHoldSamples = 0;
            }
        }
        else
        {
            feedbackHoldSamples = juce::jmax (0, feedbackHoldSamples - numSamples * 2);
        }
    }
    else
    {
        feedbackHoldSamples = 0;
    }

    // 🔇 自分の耳への返しミュート（cluster・通話・ボイチャ中の二重聴こえ防止）
    // 配信出力(StreamOut)へ声を送ったあと、メインのスピーカー/ヘッドホン出力から生声の返しだけを消す。
    if (isStandalone() && monitorMute.load (std::memory_order_relaxed)
        && auditionState.load (std::memory_order_relaxed) != AuditionPlaying
        && ! selfTestRunning.load (std::memory_order_relaxed))
    {
        fullBuffer.clear();
        if (bgmMixedNow)
        {
            buffer.copyFrom (0, 0, bgmBlockScratch, 0, 0, numSamples);
            if (numCh > 1) buffer.copyFrom (1, 0, bgmBlockScratch, 1, 0, numSamples);
        }
    }
    else if (bgmMixedNow && ! bgmSendToStream)
    {
        // 🎧 自分の耳だけ（練習モード）のときは、WDM配信出力へ声だけを送ったあとにヘッドホン側へBGMを足す
        buffer.addFrom (0, 0, bgmBlockScratch, 0, 0, numSamples);
        if (numCh > 1) buffer.addFrom (1, 0, bgmBlockScratch, 1, 0, numSamples);
    }

    if (! selfTestRunning.load (std::memory_order_relaxed) && totalCh > 2 && numCh >= 2)
    {
        const float* L = fullBuffer.getReadPointer (0);
        const float* R = fullBuffer.getReadPointer (1);
        for (int ch = 2; ch < totalCh; ++ch)
            juce::FloatVectorOperations::copy (fullBuffer.getWritePointer (ch), (ch & 1) == 0 ? L : R, numSamples);
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
juce::String VocalGzzioProcessor::getAutoVoiceProfileSummary (bool english) const
{
    const int reg  = juce::jlimit (0, 3, asVoiceRegIdx.load (std::memory_order_relaxed));
    const int phon = juce::jlimit (0, 4, asVoicePhonIdx.load (std::memory_order_relaxed));
    static const char* const regJa[4]  = { "ハイトーン・クリア", "ミッド・ナチュラル", "ローミッド・リッチ", "ディープ・チェスト" };
    static const char* const regEn[4]  = { "High-Clear", "Mid-Natural", "Low-Mid Rich", "Deep-Chest" };
    static const char* const phonJa[5] = { "ウィスパー・繊細発声", "ナチュラル発声", "ミックス・伸び重視", "パワフル・ベルティング", "高速子音・エッジ発声" };
    static const char* const phonEn[5] = { "Whisper/Airy", "Natural", "Mix/Sustained", "Powerful Belt", "Fast/Edgy" };
    if (english)
        return juce::String (" [Profile: ") + regEn[reg] + " x " + phonEn[phon] + "]";
    return juce::String::fromUTF8 (" 【声質診断: ")
         + juce::String::fromUTF8 (regJa[reg])
         + juce::String::fromUTF8 (" × ")
         + juce::String::fromUTF8 (phonJa[phon])
         + juce::String::fromUTF8 ("】");
}

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

    const double n64   = (double) juce::jmax ((int64_t) 1, asSampleCount.load());
    const float  rmsDb = (float) (10.0 * std::log10 (asSumSq.load() / n64 + 1e-12));
    const float  pkDb  = juce::Decibels::gainToDecibels (juce::jmax (asPeak.load(), 1e-6f));
    const float  crest = pkDb - rmsDb;                       // transient-ness
    const int    bc    = juce::jmax (1, asBlockCount.load());
    const float  mDb   = (float) (asBlockDbSum.load() / bc);
    const float  varDb = (float) juce::jmax (0.0, asBlockDbSqSum.load() / bc - (double) mDb * mDb);
    const float  sdDb  = std::sqrt (varDb);                  // phrase-to-phrase dynamics
    const float  floorDb = asMinBlockDb.load();              // quietest moment ~ room noise

    // v5.0: 多次元声質プロファイリング（声の重心 × 発声・ダイナミクス特性）
    const float lowShare = rumble + bodyLow + mud;
    const float brightDb = 10.0f * std::log10 (juce::jmax (1.0e-4f, highSum)
                         / juce::jmax (1.0e-4f, lowShare));
    const float delta = brightDb + 12.0f;                    // + = 標準より明るい
    const float mudRatio  = mud / juce::jmax (1e-4f, bodyLow + midE);
    const float presShare = pres / juce::jmax (1e-4f, highSum);
    const float airShare  = airE / juce::jmax (1e-4f, highSum);
    const float sibShare  = sib  / juce::jmax (1e-4f, highSum);

    int regIdx = 1; // 0:High-Clear, 1:Mid-Natural, 2:LowMid-Rich, 3:Deep-Chest
    if      (delta >  2.5f && bodyLow < 0.22f) regIdx = 0;
    else if (bodyLow > 0.36f && delta < -3.0f) regIdx = 3;
    else if (delta < -1.8f || bodyLow > 0.26f) regIdx = 2;

    int phonIdx = 1; // 0:Whisper/Airy, 1:Natural, 2:Mix/Sustained, 3:Powerful Belt, 4:Fast/Edgy
    if      (airShare > 0.32f && crest < 11.5f)        phonIdx = 0;
    else if (crest > 15.2f && presShare > 0.42f)       phonIdx = 4;
    else if (crest > 13.8f || sdDb > 5.5f)             phonIdx = 3;
    else if (midE > 0.34f && sdDb < 4.2f)              phonIdx = 2;

    asVoiceRegIdx.store  (regIdx,  std::memory_order_relaxed);
    asVoicePhonIdx.store (phonIdx, std::memory_order_relaxed);

    // ---- v5.0 SING mode: 多次元声質プロファイルに基づく適応型ボーカルチェーン構築 ----
    if (asMode.load() == 1)
    {
        // 1) level: aim the average at about -16 dBFS for streaming
        setP ("makeup", juce::jlimit (0.0f, 12.0f, -16.0f - rmsDb));

        // 2) 発声タイプとクレストファクターに応じたコンプ＆自動音量揃え(ride_amt)
        float c1 = crest > 15.0f ? 55.0f : crest > 11.0f ? 45.0f : 34.0f;
        float c2 = crest > 15.0f ? 40.0f : crest > 11.0f ? 34.0f : 26.0f;
        if (sdDb > 5.0f) c2 += 8.0f;                             // uneven phrases: more levelling
        setP ("comp1", juce::jlimit (0.0f, 70.0f, c1));
        setP ("comp2", juce::jlimit (0.0f, 70.0f, c2));

        const float atkMs = (phonIdx == 0) ? 13.0f : (phonIdx >= 3) ? 5.5f : 8.5f;
        const float relMs = (phonIdx == 0) ? 190.0f : (phonIdx == 4) ? 95.0f : (phonIdx == 3) ? 115.0f : 145.0f;
        setP ("attack",  atkMs);
        setP ("release", relMs);
        setP ("ride_amt", juce::jlimit (25.0f, 70.0f, 28.0f + sdDb * 5.5f));

        // 3) corrective + colour EQ from the spectral shares
        setP ("lowcut", juce::jlimit (70.0f, 120.0f, 80.0f + rumble * 400.0f));
        setP ("mud",   -juce::jlimit (0.0f, 8.0f, (mudRatio - 0.35f) * 22.0f));
        setP ("harsh", -juce::jlimit (0.0f, 7.0f, (presShare - 0.4f) * 20.0f));
        setP ("presence", juce::jlimit (0.5f, 4.0f, (0.30f - presShare) * 12.0f + 1.5f));
        setP ("air",   juce::jlimit (1.0f, 6.0f, (0.28f - airShare) * 22.0f + 1.0f));
        // 胴の鳴っている声(bodyLow>=0.17)には 0% = 何も足さない（dsp_autoset 整合）
        setP ("drive", juce::jlimit (0.0f, 28.0f, (0.17f - bodyLow) * 280.0f));

        // 4) 動的共振おさえ(res_amt) & スマートEQ自動連動
        const float resTarget = juce::jlimit (20.0f, 62.0f,
            22.0f + juce::jmax (0.0f, mudRatio - 0.32f) * 45.0f
                  + juce::jmax (0.0f, presShare - 0.38f) * 55.0f);
        setP ("res_amt", resTarget);
        if (mudRatio > 0.42f || presShare > 0.45f)
        {
            setP ("seq_on",     1.0f);
            setP ("seq_amount", juce::jlimit (25.0f, 55.0f, 28.0f + (mudRatio + presShare - 0.75f) * 60.0f));
        }

        // 5) sibilance + noise + articulation
        setP ("deess", juce::jlimit (20.0f, 65.0f, sibShare * 140.0f));
        const float dn = floorDb > -50.0f ? 42.0f : floorDb > -65.0f ? 32.0f : 25.0f;
        setP ("denoise", dn);
        setP ("dn_relearn", 1.0f);
        setP ("hum_amt",  100.0f);
        setP ("cons_amt", juce::jlimit (20.0f, 48.0f, 34.0f + (0.28f - presShare) * 35.0f));
        setP ("br_amt",   (phonIdx == 0) ? 28.0f : juce::jlimit (8.0f, 24.0f, (0.26f - airShare) * 45.0f + 10.0f));
        setP ("ring",     juce::jlimit (15.0f, 40.0f, 24.0f + (0.30f - presShare) * 40.0f));

        // 6) singing feel: sustain, pleasant space, and Argon Velvet Engine calibration
        setP ("sustain", (phonIdx == 0 || phonIdx == 2) ? 35.0f : (phonIdx == 4) ? 20.0f : 30.0f);
        setP ("width",   28.0f);
        setP ("doubler", 0.0f);
        setP ("delay",   0.0f);
        setP ("revsize", (regIdx == 0 || phonIdx == 0) ? 46.0f : 42.0f);
        setP ("revmix",  (phonIdx == 4) ? 14.0f : 18.0f);
        setP ("mix",     100.0f);
        setP ("argon_velvet", juce::jlimit (18.0f, 68.0f, 34.0f + (0.17f - bodyLow) * 210.0f));
        setP ("argon_focus",  juce::jlimit (22.0f, 66.0f, 38.0f + (0.30f - presShare) * 110.0f));
        setP ("argon_silk",   juce::jlimit (20.0f, 74.0f, 36.0f + (0.28f - airShare) * 130.0f));
        setP ("argon_halo",   (phonIdx == 0 || phonIdx == 2) ? 42.0f : 36.0f);

        asBrightDb.store (delta);
        asSibPct.store (100.0f * sibShare);
        int r = delta > 2.0f ? 10 : delta < -2.0f ? 11 : 12;
        if (floorDb > -62.0f) r += 100;                          // noisy room: suggest LEARN
        autoSetupResult.store (r);
        return;
    }

    // ---- TALK mode: 話し声・雑談・実況向けの適応型明瞭化 ----
    setP ("lowcut", juce::jlimit (70.0f, 120.0f, 80.0f + rumble * 400.0f));
    setP ("mud",   -juce::jlimit (0.0f, 8.0f, (mudRatio - 0.35f) * 22.0f));
    setP ("harsh", -juce::jlimit (0.0f, 7.0f, (presShare - 0.4f) * 20.0f));
    setP ("deess",  juce::jlimit (0.0f, 70.0f, sibShare * 140.0f));
    setP ("air",    juce::jlimit (0.0f, 6.0f, (0.28f - airShare) * 22.0f));
    setP ("res_amt", juce::jlimit (20.0f, 58.0f, 24.0f + juce::jmax (0.0f, mudRatio - 0.32f) * 45.0f));
    setP ("ride_amt", juce::jlimit (30.0f, 68.0f, 34.0f + sdDb * 5.0f));

    setP ("denoise", juce::jlimit (0.0f, 45.0f, asPeak.load() < 0.2f ? 35.0f : 25.0f));
    setP ("hum_amt",  100.0f);
    setP ("cons_amt", juce::jlimit (38.0f, 60.0f, 48.0f + (0.28f - presShare) * 30.0f));

    setP ("revmix", 0.0f);
    setP ("delay",  0.0f);
    setP ("dn_relearn", 1.0f);

    setP ("argon_velvet", juce::jlimit (12.0f, 52.0f, 26.0f + (0.17f - bodyLow) * 160.0f));
    setP ("argon_focus",  juce::jlimit (28.0f, 68.0f, 44.0f + (0.30f - presShare) * 100.0f));
    setP ("argon_silk",   juce::jlimit (14.0f, 54.0f, 24.0f + (0.28f - airShare) * 110.0f));
    setP ("argon_halo",   10.0f);

    int nearest = -1;
    asBrightDb.store (delta);
    asSibPct.store (100.0f * sibShare);
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
            case maJnHarm:  cycleChoice ("jn_harm", 18, +1);          break;
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
    root->setAttribute ("savedAtMs", juce::String (juce::Time::currentTimeMillis()));
    if (auto params = apvts.copyState().createXml())
        root->addChildElement (params.release());
    auto* d = root->createNewChildElement ("DENOISE");
    d->setAttribute ("learned", dnLearnedShared.load());
    d->setAttribute ("validation", dnProfileValidation.load());
    for (int b = 0; b < 4; ++b)
        d->setAttribute ("f" + juce::String (b), (double) dnFloorShared[b].load());

    // v2.2.0/v4.6.0 配信出力 ＆ 入出力ルーティングの設定
    auto* so = root->createNewChildElement ("STREAMOUT");
    so->setAttribute ("on", streamWanted ? 1 : 0);
    so->setAttribute ("dev", streamDevWanted);
    so->setAttribute ("user_cfg", streamUserConfigured ? 1 : 0);
    so->setAttribute ("route", inputRouteMode.load (std::memory_order_relaxed));
    so->setAttribute ("mono", outputMonoMode.load (std::memory_order_relaxed) ? 1 : 0);
    so->setAttribute ("smono", streamOut.isForceMono() ? 1 : 0);
    so->setAttribute ("mon_mute", monitorMute.load (std::memory_order_relaxed) ? 1 : 0);

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

    // v4.6.0 へやカスタムIRパス保存
    const auto irPath = getCustomIrPath();
    if (hasCustomIr() && irPath.isNotEmpty())
    {
        auto* h = root->createNewChildElement ("HEYA");
        h->setAttribute ("customIr", irPath);
    }

    // 🕰️ 歴代バージョン呼び出し状態（世代タイムマシン＆省負荷ゲート）
    {
        auto* lv = root->createNewChildElement ("LEGACY_VER");
        lv->setAttribute ("id",  legacyVersionId.load (std::memory_order_relaxed));
        lv->setAttribute ("era", legacyVersionEra.load (std::memory_order_relaxed));
        lv->setAttribute ("eco", legacyEcoGate.load (std::memory_order_relaxed) ? 1 : 0);
    }

    // v5.1.0 サビ自動検知・名機マイクモデリング・リップノイズ除去・歌枠セットリスト状態
    {
        auto* s51 = root->createNewChildElement ("STAGE_V51");
        s51->setAttribute ("autoChorus", autoChorusMode.load (std::memory_order_relaxed) ? 1 : 0);
        s51->setAttribute ("micModel",   micModelMode.load (std::memory_order_relaxed));
        s51->setAttribute ("lipDeclick", lipDeclickOn.load (std::memory_order_relaxed) ? 1 : 0);
        s51->setAttribute ("pitchLatMode4", pitchLatMode.load (std::memory_order_relaxed));
        s51->setAttribute ("vcFormLink", vcFormAutoLink.load (std::memory_order_relaxed) ? 1 : 0);
        s51->setAttribute ("setlistCur", currentSetlistIdx.load (std::memory_order_relaxed));
        const juce::ScopedLock sl (bgmMetaLock);
        for (const auto& item : setlistItems)
        {
            auto* el = s51->createNewChildElement ("SONG");
            el->setAttribute ("path",   item.filePath);
            el->setAttribute ("title",  item.title);
            el->setAttribute ("semi",   item.pitchSemi);
            el->setAttribute ("vc",     item.vocalCancel ? 1 : 0);
            el->setAttribute ("gain",   (double) item.bgmGainDb);
            el->setAttribute ("key",    item.detKey);
            el->setAttribute ("minor",  item.detMinor ? 1 : 0);
            el->setAttribute ("bpm",    item.detBpm);
            el->setAttribute ("velvet", (double) item.velvet);
            el->setAttribute ("focus",  (double) item.focus);
            el->setAttribute ("silk",   (double) item.silk);
            el->setAttribute ("halo",   (double) item.halo);
            el->setAttribute ("mic",    item.micModel);
            el->setAttribute ("revType", item.revType);
            el->setAttribute ("revMix", (double) item.revMix);
        }
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
            streamDevWanted      = so->getStringAttribute ("dev");
            streamWanted         = so->getIntAttribute ("on", 0) != 0;
            streamUserConfigured = so->getIntAttribute ("user_cfg", 0) != 0;
            inputRouteMode.store (juce::jlimit (0, 6, so->getIntAttribute ("route", 0)), std::memory_order_relaxed);
            outputMonoMode.store (so->getIntAttribute ("mono", 0) != 0, std::memory_order_relaxed);
            streamOut.setForceMono (so->getIntAttribute ("smono", 0) != 0);
            monitorMute.store (so->getIntAttribute ("mon_mute", 0) != 0, std::memory_order_relaxed);
            // 単体起動版のときだけ、保存されていた配信先を開き直す（未設定時や通常スピーカーが誤って入っていた場合は VocalGzzio Driver (WDM) を自動接続）
            if (isStandalone())
            {
                if (! streamUserConfigured && ! gz::StreamOut::isVirtualLoopbackName (streamDevWanted))
                    streamDevWanted.clear();

                if (streamWanted && streamDevWanted.isNotEmpty())
                    streamOut.start (streamDevWanted, currentSampleRate);
                else if (! streamUserConfigured)
                    ensureStandaloneVirtualDriverActive();
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
        if (! suppressDeviceRestore)
        {
            if (auto* h = xml.getChildByName ("HEYA"))
            {
                const auto irPath = h->getStringAttribute ("customIr");
                if (irPath.isNotEmpty())
                    loadCustomIrFile (juce::File (irPath));
                else
                    clearCustomIrFile();
            }
            else
            {
                clearCustomIrFile();
            }

            if (auto* lv = xml.getChildByName ("LEGACY_VER"))
            {
                const int loadedId  = juce::jlimit (0, 13,    lv->getIntAttribute ("id",  0));
                int       loadedEra = juce::jlimit (100, 510, lv->getIntAttribute ("era", 510));
                if (loadedId == 0 && loadedEra == 500)
                    loadedEra = 510; // 最新版(id=0)が選ばれていた場合は v5.1.0 (era=510) へ自然昇格
                legacyVersionId.store  (loadedId,  std::memory_order_relaxed);
                legacyVersionEra.store (loadedEra, std::memory_order_relaxed);
                legacyEcoGate.store    (lv->getIntAttribute ("eco", 1) != 0, std::memory_order_relaxed);
            }
            else if (gz::isTestDataIsolated())
            {
                legacyVersionId.store  (0,    std::memory_order_relaxed);
                legacyVersionEra.store (510,  std::memory_order_relaxed);
                legacyEcoGate.store    (true, std::memory_order_relaxed);
            }

            if (auto* s51 = xml.getChildByName ("STAGE_V51"))
            {
                autoChorusMode.store (s51->getIntAttribute ("autoChorus", 0) != 0, std::memory_order_relaxed);
                micModelMode.store   (juce::jlimit (0, 4, s51->getIntAttribute ("micModel", 0)), std::memory_order_relaxed);
                lipDeclickOn.store   (s51->getIntAttribute ("lipDeclick", 0) != 0, std::memory_order_relaxed);
                // v5.1.2: 4段階化。旧3段階の保存値(0:10.6ms/1:21.3ms/2:42.6ms)は1つずらして同じ遅延へ移す
                if (s51->hasAttribute ("pitchLatMode4"))
                    pitchLatMode.store (juce::jlimit (0, kPitchLatModes - 1, s51->getIntAttribute ("pitchLatMode4", 2)), std::memory_order_relaxed);
                else if (s51->hasAttribute ("pitchLatMode"))
                    pitchLatMode.store (juce::jlimit (1, kPitchLatModes - 1, s51->getIntAttribute ("pitchLatMode", 1) + 1), std::memory_order_relaxed);
                else
                    pitchLatMode.store (2, std::memory_order_relaxed);
                vcFormAutoLink.store (s51->getIntAttribute ("vcFormLink", 0) != 0, std::memory_order_relaxed);
                currentSetlistIdx.store (s51->getIntAttribute ("setlistCur", -1), std::memory_order_relaxed);
                const juce::ScopedLock sl (bgmMetaLock);
                setlistItems.clear();
                for (auto* el : s51->getChildWithTagNameIterator ("SONG"))
                {
                    if ((int) setlistItems.size() >= 20) break;
                    SetlistItem item;
                    item.filePath    = el->getStringAttribute ("path");
                    item.title       = el->getStringAttribute ("title", "Song");
                    item.pitchSemi   = juce::jlimit (-6, 6, el->getIntAttribute ("semi", 0));
                    item.vocalCancel = el->getIntAttribute ("vc", 0) != 0;
                    item.bgmGainDb   = (float) el->getDoubleAttribute ("gain", -6.0);
                    item.detKey      = juce::jlimit (0, 11, el->getIntAttribute ("key", 0));
                    item.detMinor    = el->getIntAttribute ("minor", 0) != 0;
                    item.detBpm      = juce::jlimit (60, 240, el->getIntAttribute ("bpm", 120));
                    item.velvet      = (float) el->getDoubleAttribute ("velvet", 55.0);
                    item.focus       = (float) el->getDoubleAttribute ("focus",  62.0);
                    item.silk        = (float) el->getDoubleAttribute ("silk",   58.0);
                    item.halo        = (float) el->getDoubleAttribute ("halo",   45.0);
                    item.micModel    = juce::jlimit (0, 4, el->getIntAttribute ("mic", 0));
                    item.revType     = el->getIntAttribute ("revType", 7);
                    item.revMix      = (float) el->getDoubleAttribute ("revMix", 22.0);
                    setlistItems.push_back (std::move (item));
                }
            }
        }
    }
    else if (xml.hasTagName (apvts.state.getType()))   // legacy v1.4.x chunk
    {
        swapParams (xml);
    }
    restoringState = false;
}

bool VocalGzzioProcessor::loadCustomIrFile (const juce::File& file)
{
    if (! file.existsAsFile()) return false;
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (file));
    if (reader == nullptr || reader->lengthInSamples <= 0 || reader->sampleRate <= 0.0)
        return false;

    const double targetSr = (currentSampleRate > 8000.0) ? currentSampleRate : 48000.0;
    const int maxReadSamples = (int) std::min<int64_t> (reader->lengthInSamples,
                                                        (int64_t) std::lround (4.0 * reader->sampleRate));
    if (maxReadSamples <= 0) return false;

    juce::AudioBuffer<float> rawBuf ((int) juce::jmin (2u, reader->numChannels), maxReadSamples);
    if (! reader->read (&rawBuf, 0, maxReadSamples, 0, true, true))
        return false;

    const float* inL = rawBuf.getReadPointer (0);
    const float* inR = rawBuf.getReadPointer (rawBuf.getNumChannels() > 1 ? 1 : 0);

    const double ratio = reader->sampleRate / targetSr;
    const int outLen = std::max (64, std::min ((int) std::lround (3.0 * targetSr),
                                               (int) std::lround ((double) maxReadSamples / ratio)));
    std::vector<float> resL ((size_t) outLen, 0.0f), resR ((size_t) outLen, 0.0f);

    for (int i = 0; i < outLen; ++i)
    {
        const double srcPos = (double) i * ratio;
        const int i0 = juce::jlimit (0, maxReadSamples - 1, (int) srcPos);
        const int i1 = juce::jlimit (0, maxReadSamples - 1, i0 + 1);
        const float frac = (float) (srcPos - (double) i0);
        resL[(size_t) i] = inL[i0] * (1.0f - frac) + inL[i1] * frac;
        resR[(size_t) i] = inR[i0] * (1.0f - frac) + inR[i1] * frac;
    }

    // 65Hz ハイパスで外部IRのDCオフセットと超低域の濁りを除去＋末尾20msフェードアウト
    heya::GenBiquad hpL, hpR;
    hpL.highpass (65.0, 0.707, targetSr);
    hpR.highpass (65.0, 0.707, targetSr);
    const int fadeLen = std::min (outLen / 4, std::max (16, (int) (0.020 * targetSr)));
    for (int i = 0; i < outLen; ++i)
    {
        float w = 1.0f;
        if (i >= outLen - fadeLen)
            w = (float) (outLen - 1 - i) / (float) std::max (1, fadeLen);
        resL[(size_t) i] = hpL.tick (resL[(size_t) i]) * w;
        resR[(size_t) i] = hpR.tick (resR[(size_t) i]) * w;
    }

    if (! heyaRev.loadCustomIr (resL.data(), resR.data(), outLen))
        return false;

    {
        const juce::ScopedLock sl (customIrLock);
        customIrPath = file.getFullPathName();
        customIrName = file.getFileNameWithoutExtension();
    }

    if (! restoringState)
    {
        const int curType = (int) apvts.getRawParameterValue ("rev_type")->load();
        if (curType < kHeyaFirst)
        {
            if (auto* p = apvts.getParameter ("rev_type"))
                p->setValueNotifyingHost (p->convertTo0to1 ((float) (kHeyaFirst + 2)));
        }
        if (auto* pOn = apvts.getParameter ("revon"))
            if (apvts.getRawParameterValue ("revon")->load() < 0.5f)
                pOn->setValueNotifyingHost (1.0f);
        markStateDirty();
    }
    return true;
}

void VocalGzzioProcessor::clearCustomIrFile()
{
    heyaRev.clearCustomIr();
    {
        const juce::ScopedLock sl (customIrLock);
        customIrPath.clear();
        customIrName.clear();
    }
    if (! restoringState)
        markStateDirty();
}

juce::String VocalGzzioProcessor::getCustomIrName() const
{
    const juce::ScopedLock sl (customIrLock);
    return customIrName;
}

juce::String VocalGzzioProcessor::getCustomIrPath() const
{
    const juce::ScopedLock sl (customIrLock);
    return customIrPath;
}

void VocalGzzioProcessor::getReverbIrCurve (float* dest, int nPoints) const noexcept
{
    if (dest == nullptr || nPoints <= 0) return;
    const int rtype = (int) apvts.getRawParameterValue ("rev_type")->load();
    const int room = (rtype >= kHeyaFirst) ? (rtype - kHeyaFirst) : 2;
    heyaRev.getDecayCurve (room, dest, nPoints);
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

    s << "[ノイズ除去の診断]\n";
    auto value = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };
    s << "  おそうじ区画 : " << (value ("mod_souji") > 0.5f ? "入" : "切") << "\n";
    s << "  ノイズ除去   : " << (value ("dn_on") > 0.5f ? "入" : "切")
      << " / " << juce::String (value ("denoise"), 0) << "%\n";
    s << "  加工音の割合 : " << juce::String (value ("mix"), 0) << "%\n";
    s << "  原音比較中   : " << (compareBypass.load() ? "はい" : "いいえ")
      << " / 区画比較番号 " << compareOne.load() << "（-1は比較なし）\n";
    s << "  ノイズ学習   : " << (isDenoiseLearning() ? "測定中" : isDenoiseLearned() ? "学習済み" : "自動推定")
      << " / 自動学習 " << (value ("dn_relearn") > 0.5f ? "入" : "切") << "\n";
    s << "  無音時カット : " << (value ("gate_on") > 0.5f ? "入" : "切")
      << " / " << juce::String (value ("gate"), 1) << " dB\n";
    s << "  保存用のノイズ基準（低域から4帯域、dBFS） :";
    for (const auto& floor : dnFloorShared)
        s << " " << juce::String (juce::Decibels::gainToDecibels (floor.load(), -120.0f), 1);
    s << "\n  内部の除去動作指標 : " << juce::String (getDenoiseActivity(), 3)
      << "（0は減衰なし。機器の直接モニター音は測定対象外）\n\n";

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
        && juce::Time::getMillisecondCounter() - lastDirtyMs.load() > 400)
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
        if (! gz::isTestDataIsolated())
        {
            // スタンドアローン版とVST3版の間で直近に更新された設定・歴代バージョンを引き継ぐ：
            // ホスト(DAW/OBS/StandaloneFilterWindow)が渡してきた保存チャンクよりも
            // autosave.xml の方が新しい場合は、最新の autosave.xml 側を優先して適用する。
            const auto chunkMs = xml->getStringAttribute ("savedAtMs").getLargeIntValue();
            if (auto f = autosaveFile(); f.existsAsFile())
            {
                if (auto autoXml = juce::XmlDocument::parse (f))
                {
                    const auto autoMs = juce::jmax<juce::int64> (
                        autoXml->getStringAttribute ("savedAtMs").getLargeIntValue(),
                        f.getLastModificationTime().toMilliseconds());
                    if (autoMs > chunkMs + 1500)
                    {
                        applyStateXml (*autoXml);
                        return;
                    }
                }
            }
        }
        applyStateXml (*xml);
        markStateDirty();   // keep the autosave in sync with the restored project
    }
}

//==============================================================================
// v4.8.0 "Stage & Studio Complete Edition" — 4大機能（BGMプレイヤー / WAV録音＆30秒さかのぼり保存 / 5秒声質ターゲット診断）
//==============================================================================
bool VocalGzzioProcessor::loadBgmFile (const juce::File& file)
{
    if (! file.existsAsFile()) return false;
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (file));
    if (reader == nullptr || reader->lengthInSamples <= 0 || reader->sampleRate <= 0.0)
        return false;

    const double targetSr = currentSampleRate > 1000.0 ? currentSampleRate : 48000.0;
    const int maxInSamples = (int) juce::jmin<juce::int64> (reader->lengthInSamples, (juce::int64) (reader->sampleRate * 600.0)); // 最大10分
    juce::AudioBuffer<float> rawBuf (2, maxInSamples);
    rawBuf.clear();
    reader->read (&rawBuf, 0, maxInSamples, 0, true, reader->numChannels > 1);
    if (reader->numChannels == 1)
        rawBuf.copyFrom (1, 0, rawBuf, 0, 0, maxInSamples);

    auto finalBuf = std::make_shared<juce::AudioBuffer<float>>();
    if (std::abs (reader->sampleRate - targetSr) < 1.0)
    {
        *finalBuf = std::move (rawBuf);
    }
    else
    {
        const double ratio = reader->sampleRate / targetSr;
        const int outSamples = juce::jmax (1, (int) ((double) maxInSamples / ratio));
        finalBuf->setSize (2, outSamples);
        for (int ch = 0; ch < 2; ++ch)
        {
            juce::LagrangeInterpolator interp;
            interp.process (ratio, rawBuf.getReadPointer (ch), finalBuf->getWritePointer (ch), outSamples);
        }
    }

    {
        const juce::SpinLock::ScopedLockType sl (bgmAudioLock);
        bgmAudioBuf = finalBuf;
        bgmLoadedSamples.store (finalBuf->getNumSamples(), std::memory_order_release);
        bgmReadPos.store (0.0, std::memory_order_relaxed);
    }
    {
        const juce::ScopedLock sl (bgmMetaLock);
        bgmTitle    = file.getFileNameWithoutExtension();
        bgmFilePath = file.getFullPathName();
    }
    bgmPitchSemi.store (0, std::memory_order_relaxed);
    analyzeBgmKeyAndBpm();
    bgmPlaying.store (true, std::memory_order_release);
    return true;
}

void VocalGzzioProcessor::loadBuiltinDemoBgm()
{
    const double sr = currentSampleRate > 1000.0 ? currentSampleRate : 48000.0;
    // 120 BPM, 8小節（16秒）の王道カノン進行（C -> G -> Am -> Em -> F -> C -> F -> G）エレクトリックピアノ＆ウォームパッド伴奏
    const int totalSamples = (int) (16.0 * sr);
    auto buf = std::make_shared<juce::AudioBuffer<float>> (2, totalSamples);
    buf->clear();
    float* L = buf->getWritePointer (0);
    float* R = buf->getWritePointer (1);

    struct Chord { int notes[4]; };
    static const Chord chords[8] = {
        { { 48, 60, 64, 67 } }, // C
        { { 43, 59, 62, 67 } }, // G
        { { 45, 60, 64, 69 } }, // Am
        { { 40, 59, 64, 67 } }, // Em
        { { 41, 57, 60, 65 } }, // F
        { { 48, 55, 60, 64 } }, // C
        { { 41, 57, 60, 65 } }, // F
        { { 43, 59, 62, 67 } }  // G
    };

    const int chordLen = totalSamples / 8;
    for (int c = 0; c < 8; ++c)
    {
        const int start = c * chordLen;
        const int end   = (c == 7) ? totalSamples : (c + 1) * chordLen;
        double freqs[4];
        for (int k = 0; k < 4; ++k)
            freqs[k] = 440.0 * std::pow (2.0, (chords[c].notes[k] - 69) / 12.0);

        for (int i = start; i < end; ++i)
        {
            const double t = (double) (i - start) / sr;
            const double beat = std::fmod (t, 0.5); // 四分音符パルス
            const float envPad = (float) (std::min (1.0, t / 0.08) * std::min (1.0, (2.0 - t) / 0.12));
            const float envEp  = (float) (std::exp (-beat * 4.2) * std::min (1.0, beat / 0.008));
            float sL = 0.0f, sR = 0.0f;
            for (int k = 0; k < 4; ++k)
            {
                const double ph = juce::MathConstants<double>::twoPi * freqs[k] * ((double) i / sr);
                const float pad = (float) (std::sin (ph) + 0.25 * std::sin (ph * 2.0 + 0.3));
                const float ep  = (float) (std::sin (ph + 0.6 * std::sin (ph * 2.0) * envEp));
                const float v   = (pad * 0.45f * envPad + ep * 0.55f * envEp) * (k == 0 ? 0.12f : 0.08f);
                const float panL = (k & 1) ? 0.82f : 1.0f;
                const float panR = (k & 1) ? 1.0f  : 0.82f;
                sL += v * panL;
                sR += v * panR;
            }
            L[i] = sL;
            R[i] = sR;
        }
    }

    {
        const juce::SpinLock::ScopedLockType sl (bgmAudioLock);
        bgmAudioBuf = buf;
        bgmLoadedSamples.store (totalSamples, std::memory_order_release);
        bgmReadPos.store (0.0, std::memory_order_relaxed);
    }
    {
        const juce::ScopedLock sl (bgmMetaLock);
        bgmTitle = juce::String::fromUTF8 ("🎹 内蔵デモ伴奏（J-POPカノン進行 / Key: C / 120BPM）");
        bgmFilePath.clear();
    }
    bgmPitchSemi.store (0, std::memory_order_relaxed);
    bgmDetectedKey.store (0, std::memory_order_relaxed);     // C
    bgmDetectedMinor.store (false, std::memory_order_relaxed); // Major
    bgmDetectedBpm.store (120, std::memory_order_relaxed);
    bgmBaseKey = 0; // C
    if (bgmSyncKey.load (std::memory_order_relaxed))
        applyBgmAutoKeyBpm();
}

void VocalGzzioProcessor::analyzeBgmKeyAndBpm()
{
    std::shared_ptr<const juce::AudioBuffer<float>> buf;
    {
        const juce::SpinLock::ScopedLockType sl (bgmAudioLock);
        buf = bgmAudioBuf;
    }
    if (buf == nullptr || buf->getNumSamples() < 4096)
        return;

    const double sr = currentSampleRate > 1000.0 ? currentSampleRate : 48000.0;
    const int totalSamples = buf->getNumSamples();
    // 曲のイントロ無音を避け、盛り上がりやすい前半〜中盤の最大18秒間を高速解析
    const int startSample = (totalSamples > (int) (24.0 * sr)) ? (int) (4.0 * sr) : 0;
    const int analyzeLen  = juce::jmin (totalSamples - startSample, (int) (18.0 * sr));
    if (analyzeLen < 2048)
        return;

    const float* L = buf->getReadPointer (0) + startSample;
    const float* R = buf->getReadPointer (buf->getNumChannels() > 1 ? 1 : 0) + startSample;

    // 1. 12音クロマ（C..B）解析：約6kHzへデシメーションし、MIDI 48(C3)〜83(B5) の36音で直交相関を取る
    const int decim = juce::jmax (1, (int) std::round (sr / 6000.0));
    const double decSr = sr / (double) decim;
    const int decLen = analyzeLen / decim;
    std::vector<float> mono ((size_t) decLen, 0.0f);
    for (int i = 0; i < decLen; ++i)
    {
        const int idx = i * decim;
        mono[(size_t) i] = 0.5f * (L[idx] + R[idx]);
    }

    float chroma[12] = {};
    for (int note = 48; note < 84; ++note)
    {
        const double freq = 440.0 * std::pow (2.0, (note - 69) / 12.0);
        if (freq >= decSr * 0.45) continue;
        const double w = juce::MathConstants<double>::twoPi * freq / decSr;
        const float coeff = (float) (2.0 * std::cos (w));
        // 0.25秒窓ごとにGoertzelエネルギーを積算
        const int winLen = juce::jmax (256, (int) (0.25 * decSr));
        double noteEnergy = 0.0;
        int winCount = 0;
        for (int w0 = 0; w0 + winLen <= decLen; w0 += winLen)
        {
            float s1 = 0.0f, s2 = 0.0f;
            for (int n = 0; n < winLen; ++n)
            {
                const float s0 = mono[(size_t) (w0 + n)] + coeff * s1 - s2;
                s2 = s1;
                s1 = s0;
            }
            const float power = s1 * s1 + s2 * s2 - coeff * s1 * s2;
            noteEnergy += (double) juce::jmax (0.0f, power);
            ++winCount;
        }
        if (winCount > 0)
            chroma[note % 12] += (float) std::sqrt (noteEnergy / (double) winCount);
    }

    // Krumhansl-Kessler 24調（12長調・12短調）プロファイル相関で主音と長調/短調を判定
    static const float kMajProf[12] = { 6.35f, 2.23f, 3.48f, 2.33f, 4.38f, 4.09f, 2.52f, 5.19f, 2.39f, 3.66f, 2.29f, 2.88f };
    static const float kMinProf[12] = { 6.33f, 2.68f, 3.52f, 5.38f, 2.60f, 3.53f, 2.54f, 4.75f, 3.98f, 2.69f, 3.34f, 3.17f };
    int bestKey = 0;
    bool bestMinor = false;
    float bestScore = -1.0e9f;
    for (int root = 0; root < 12; ++root)
    {
        float majScore = 0.0f, minScore = 0.0f;
        for (int pc = 0; pc < 12; ++pc)
        {
            const float c = chroma[(root + pc) % 12];
            majScore += c * kMajProf[pc];
            minScore += c * kMinProf[pc];
        }
        if (majScore > bestScore) { bestScore = majScore; bestKey = root; bestMinor = false; }
        if (minScore > bestScore) { bestScore = minScore; bestKey = root; bestMinor = true;  }
    }

    // 2. テンポ（BPM 75..175）自動解析：100Hzオンセット包絡の自己相関
    const int hop = juce::jmax (1, (int) std::round (decSr / 100.0));
    const double envRate = decSr / (double) hop;
    const int numFrames = decLen / hop;
    int bestBpm = 120;
    if (numFrames >= 240)
    {
        std::vector<float> onset ((size_t) numFrames, 0.0f);
        float prevE = 0.0f;
        for (int f = 0; f < numFrames; ++f)
        {
            float e = 0.0f;
            const int base = f * hop;
            for (int n = 0; n < hop; ++n)
            {
                const float v = mono[(size_t) (base + n)];
                e += v * v;
            }
            e = std::sqrt (e / (float) hop);
            onset[(size_t) f] = juce::jmax (0.0f, e - prevE);
            prevE = e;
        }
        float maxBpmScore = -1.0f;
        for (int bpm = 78; bpm <= 172; ++bpm)
        {
            const int lag1 = juce::jmax (1, (int) std::round (envRate * 60.0 / (double) bpm));
            const int lag2 = lag1 * 2;
            if (lag2 >= numFrames) continue;
            float ac1 = 0.0f, ac2 = 0.0f;
            for (int f = 0; f + lag2 < numFrames; ++f)
            {
                ac1 += onset[(size_t) f] * onset[(size_t) (f + lag1)];
                ac2 += onset[(size_t) f] * onset[(size_t) (f + lag2)];
            }
            // 115〜135 BPM 付近のJ-POP標準テンポ帯に緩やかな事前重みを与えて倍テンポ誤判定を防ぐ
            const float prior = 1.0f - 0.0012f * std::abs ((float) bpm - 124.0f);
            const float score = (ac1 + 0.55f * ac2) * prior;
            if (score > maxBpmScore)
            {
                maxBpmScore = score;
                bestBpm = bpm;
            }
        }
    }

    bgmDetectedKey.store (bestKey, std::memory_order_relaxed);
    bgmDetectedMinor.store (bestMinor, std::memory_order_relaxed);
    bgmDetectedBpm.store (bestBpm, std::memory_order_relaxed);
    bgmBaseKey = bestKey;

    if (bgmSyncKey.load (std::memory_order_relaxed))
        applyBgmAutoKeyBpm();
}

void VocalGzzioProcessor::applyBgmAutoKeyBpm()
{
    const int detKey = juce::jlimit (0, 11, bgmDetectedKey.load (std::memory_order_relaxed));
    const bool detMin = bgmDetectedMinor.load (std::memory_order_relaxed);
    const int detBpm = juce::jlimit (60, 240, bgmDetectedBpm.load (std::memory_order_relaxed));
    const int semi   = juce::jlimit (-6, 6, bgmPitchSemi.load (std::memory_order_relaxed));
    const int effKey = (detKey + semi + 120) % 12;

    if (auto* pKey = apvts.getParameter ("at_key"))
        pKey->setValueNotifyingHost (pKey->convertTo0to1 ((float) effKey));
    if (auto* pScale = apvts.getParameter ("at_scale"))
        pScale->setValueNotifyingHost (pScale->convertTo0to1 (detMin ? 2.0f : 1.0f)); // 1=Major, 2=Minor
    if (auto* pBpm = apvts.getParameter ("bpm"))
        pBpm->setValueNotifyingHost (pBpm->convertTo0to1 ((float) detBpm));
    if (auto* pDlyMs = apvts.getParameter ("dly_ms"))
    {
        const float eighthMs = juce::jlimit (60.0f, 900.0f, 30000.0f / (float) detBpm);
        pDlyMs->setValueNotifyingHost (pDlyMs->convertTo0to1 (eighthMs));
    }
    markStateDirty();
}

juce::String VocalGzzioProcessor::getBgmKeyBpmSummary (bool english) const
{
    static const char* const kKeyNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const int detKey = juce::jlimit (0, 11, bgmDetectedKey.load (std::memory_order_relaxed));
    const bool detMin = bgmDetectedMinor.load (std::memory_order_relaxed);
    const int detBpm = juce::jlimit (60, 240, bgmDetectedBpm.load (std::memory_order_relaxed));
    const int semi   = juce::jlimit (-6, 6, bgmPitchSemi.load (std::memory_order_relaxed));
    const int effKey = (detKey + semi + 120) % 12;

    juce::String s;
    s << (english ? "🎼 Auto-Detected: " : "🎼 自動解析: ")
      << kKeyNames[effKey] << (detMin ? "m" : " Maj")
      << " / " << detBpm << " BPM";
    return s;
}

const char* VocalGzzioProcessor::getMicModelName (int m, bool english) noexcept
{
    static const char* const kJa[5] = {
        "🎙 素のマイク (OFF)",
        "✨ 800G 真空管",
        "👑 U87 王道",
        "📻 SM7B 配信",
        "💎 ASMR 極艶"
    };
    static const char* const kEn[5] = {
        "🎙 Raw Mic (OFF)",
        "✨ 800G Tube",
        "👑 U87 Studio",
        "📻 SM7B Broadcast",
        "💎 ASMR Ribbon"
    };
    const int idx = juce::jlimit (0, 4, m);
    return english ? kEn[idx] : kJa[idx];
}

void VocalGzzioProcessor::clearBgm()
{
    bgmPlaying.store (false, std::memory_order_release);
    {
        const juce::SpinLock::ScopedLockType sl (bgmAudioLock);
        bgmAudioBuf.reset();
        bgmLoadedSamples.store (0, std::memory_order_release);
        bgmReadPos.store (0.0, std::memory_order_relaxed);
    }
    {
        const juce::ScopedLock sl (bgmMetaLock);
        bgmTitle.clear();
        bgmFilePath.clear();
    }
}

void VocalGzzioProcessor::setLipDeclickOn (bool b) noexcept
{
    lipDeclickOn.store (b, std::memory_order_relaxed);
    if (auto* p = apvts.getParameter ("lip_amt"))
    {
        const float cur = apvts.getRawParameterValue ("lip_amt")->load();
        if (b && cur < 55.0f)
            p->setValueNotifyingHost (p->convertTo0to1 (65.0f));
        else if (! b && std::abs (cur - 65.0f) < 1.0f)
            p->setValueNotifyingHost (p->convertTo0to1 (0.0f));
    }
    markStateDirty();
}

int VocalGzzioProcessor::getSetlistCount() const noexcept
{
    const juce::ScopedLock sl (bgmMetaLock);
    return (int) setlistItems.size();
}

VocalGzzioProcessor::SetlistItem VocalGzzioProcessor::getSetlistItem (int index) const
{
    const juce::ScopedLock sl (bgmMetaLock);
    if (index >= 0 && index < (int) setlistItems.size())
        return setlistItems[(size_t) index];
    return {};
}

int VocalGzzioProcessor::addCurrentSongToSetlist()
{
    if (! hasBgmLoaded())
        loadBuiltinDemoBgm();

    SetlistItem item;
    {
        const juce::ScopedLock sl (bgmMetaLock);
        item.filePath = bgmFilePath;
        item.title    = bgmTitle.isNotEmpty() ? bgmTitle : juce::String::fromUTF8 ("曲 ") + juce::String ((int) setlistItems.size() + 1);
    }
    item.pitchSemi   = bgmPitchSemi.load (std::memory_order_relaxed);
    item.vocalCancel = bgmVocalCancel.load (std::memory_order_relaxed);
    item.bgmGainDb   = bgmGainDb.load (std::memory_order_relaxed);
    item.detKey      = bgmDetectedKey.load (std::memory_order_relaxed);
    item.detMinor    = bgmDetectedMinor.load (std::memory_order_relaxed);
    item.detBpm      = bgmDetectedBpm.load (std::memory_order_relaxed);
    item.velvet      = apvts.getRawParameterValue ("argon_velvet")->load();
    item.focus       = apvts.getRawParameterValue ("argon_focus")->load();
    item.silk        = apvts.getRawParameterValue ("argon_silk")->load();
    item.halo        = apvts.getRawParameterValue ("argon_halo")->load();
    item.micModel    = micModelMode.load (std::memory_order_relaxed);
    item.revType     = (int) apvts.getRawParameterValue ("rev_type")->load();
    item.revMix      = apvts.getRawParameterValue ("revmix")->load();

    int newIdx = 0;
    {
        const juce::ScopedLock sl (bgmMetaLock);
        // 同じ曲名/パスがあれば上書き更新、無ければ追加（最大20曲）
        int found = -1;
        for (int i = 0; i < (int) setlistItems.size(); ++i)
        {
            if (setlistItems[(size_t) i].title == item.title && setlistItems[(size_t) i].filePath == item.filePath)
            {
                found = i;
                break;
            }
        }
        if (found >= 0)
        {
            setlistItems[(size_t) found] = item;
            newIdx = found;
        }
        else
        {
            if ((int) setlistItems.size() >= 20)
                setlistItems.erase (setlistItems.begin());
            setlistItems.push_back (item);
            newIdx = (int) setlistItems.size() - 1;
        }
    }
    currentSetlistIdx.store (newIdx, std::memory_order_relaxed);
    markStateDirty();
    return newIdx;
}

bool VocalGzzioProcessor::loadSetlistSong (int index, bool autoPlay)
{
    SetlistItem item;
    {
        const juce::ScopedLock sl (bgmMetaLock);
        if (index < 0 || index >= (int) setlistItems.size())
            return false;
        item = setlistItems[(size_t) index];
    }
    currentSetlistIdx.store (index, std::memory_order_relaxed);

    if (item.filePath.isNotEmpty() && juce::File (item.filePath).existsAsFile())
        loadBgmFile (juce::File (item.filePath));
    else
        loadBuiltinDemoBgm();

    bgmVocalCancel.store (item.vocalCancel, std::memory_order_relaxed);
    bgmGainDb.store (item.bgmGainDb, std::memory_order_relaxed);
    bgmDetectedKey.store (item.detKey, std::memory_order_relaxed);
    bgmDetectedMinor.store (item.detMinor, std::memory_order_relaxed);
    bgmDetectedBpm.store (item.detBpm, std::memory_order_relaxed);
    bgmBaseKey = item.detKey;
    setBgmPitchSemi (item.pitchSemi);
    if (bgmSyncKey.load (std::memory_order_relaxed))
        applyBgmAutoKeyBpm();

    auto setP = [this] (const char* id, float val)
    {
        if (auto* p = apvts.getParameter (id))
            p->setValueNotifyingHost (p->convertTo0to1 (val));
    };
    setP ("argon_velvet", item.velvet);
    setP ("argon_focus",  item.focus);
    setP ("argon_silk",   item.silk);
    setP ("argon_halo",   item.halo);
    setP ("rev_type",     (float) item.revType);
    setP ("revmix",       item.revMix);
    micModelMode.store (juce::jlimit (0, 4, item.micModel), std::memory_order_relaxed);
    bgmPlaying.store (autoPlay, std::memory_order_release);
    markStateDirty();
    return true;
}

bool VocalGzzioProcessor::nextSetlistSong()
{
    const int count = getSetlistCount();
    if (count <= 0) return false;
    const int cur = currentSetlistIdx.load (std::memory_order_relaxed);
    const int next = (cur + 1) % count;
    return loadSetlistSong (next, true);
}

bool VocalGzzioProcessor::prevSetlistSong()
{
    const int count = getSetlistCount();
    if (count <= 0) return false;
    const int cur = currentSetlistIdx.load (std::memory_order_relaxed);
    const int prev = (cur <= 0) ? (count - 1) : (cur - 1);
    return loadSetlistSong (prev, true);
}

void VocalGzzioProcessor::removeSetlistSong (int index)
{
    {
        const juce::ScopedLock sl (bgmMetaLock);
        if (index < 0 || index >= (int) setlistItems.size())
            return;
        setlistItems.erase (setlistItems.begin() + index);
        const int cur = currentSetlistIdx.load (std::memory_order_relaxed);
        if (setlistItems.empty())
            currentSetlistIdx.store (-1, std::memory_order_relaxed);
        else if (cur >= (int) setlistItems.size())
            currentSetlistIdx.store ((int) setlistItems.size() - 1, std::memory_order_relaxed);
    }
    markStateDirty();
}

void VocalGzzioProcessor::clearSetlist()
{
    {
        const juce::ScopedLock sl (bgmMetaLock);
        setlistItems.clear();
        currentSetlistIdx.store (-1, std::memory_order_relaxed);
    }
    markStateDirty();
}

void VocalGzzioProcessor::setBgmPitchSemi (int semi)
{
    const int clamped = juce::jlimit (-6, 6, semi);
    const int prev = bgmPitchSemi.exchange (clamped, std::memory_order_relaxed);
    if (bgmSyncKey.load (std::memory_order_relaxed))
    {
        if (bgmBaseKey < 0)
            bgmBaseKey = (int) apvts.getRawParameterValue ("at_key")->load();
        else if (prev != clamped)
        {
            const int curKey = (int) apvts.getRawParameterValue ("at_key")->load();
            bgmBaseKey = (curKey - prev + 120) % 12;
        }
        const int newKey = (bgmBaseKey + clamped + 120) % 12;
        if (auto* p = apvts.getParameter ("at_key"))
            p->setValueNotifyingHost (p->convertTo0to1 ((float) newKey));
    }
}

juce::File VocalGzzioProcessor::getRecordingsDirectory() const
{
   #if VOCALGZZIO_TESTING
    auto dir = gz::dataDirectory().getChildFile ("VocalGzzio Recordings");
   #else
    auto dir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                   .getChildFile ("VocalGzzio Recordings");
   #endif
    if (! dir.isDirectory())
        dir.createDirectory();
    return dir;
}

bool VocalGzzioProcessor::writeDryWetWavPair (const juce::AudioBuffer<float>& drySrc,
                                              const juce::AudioBuffer<float>& wetSrc,
                                              int numFrames,
                                              const juce::String& prefixTag,
                                              const juce::AudioBuffer<float>* bgmSrc)
{
    if (numFrames <= 0) return false;
    const double sr = currentSampleRate > 1000.0 ? currentSampleRate : 48000.0;
    auto dir = getRecordingsDirectory();
    const auto stamp = juce::Time::getCurrentTime().formatted ("%Y%m%d_%H%M%S");
    auto wetFile = dir.getNonexistentChildFile ("VocalGzzio_" + prefixTag + "_" + stamp + juce::String::fromUTF8 ("_完成音(Wet)"), ".wav", false);
    auto dryFile = dir.getNonexistentChildFile ("VocalGzzio_" + prefixTag + "_" + stamp + juce::String::fromUTF8 ("_素の声(Dry)"), ".wav", false);

    // v5.0.0 柱②：-14.0 LUFS 自動音圧マスタリング ＆ -1.0 dBTP ソフトニー・ブリックウォールリミッターヘルパー
    auto applyAutoMaster14Lufs = [] (juce::AudioBuffer<float>& buf, int frames) -> float
    {
        double sumSq = 0.0;
        int gatedCount = 0;
        const int blockLen = 1024;
        const float gateThrSq = juce::Decibels::decibelsToGain (-52.0f) * juce::Decibels::decibelsToGain (-52.0f);
        const float* rL = buf.getReadPointer (0);
        const float* rR = buf.getReadPointer (buf.getNumChannels() > 1 ? 1 : 0);
        for (int b = 0; b < frames; b += blockLen)
        {
            const int n = juce::jmin (blockLen, frames - b);
            double blkSq = 0.0;
            for (int i = 0; i < n; ++i)
            {
                const float xL = rL[b + i], xR = rR[b + i];
                blkSq += 0.5 * ((double) xL * xL + (double) xR * xR);
            }
            if (blkSq / (double) juce::jmax (1, n) > (double) gateThrSq)
            {
                sumSq += blkSq;
                gatedCount += n;
            }
        }
        float boostDb = 0.0f;
        if (gatedCount > 0)
        {
            const float curLufs = 10.0f * std::log10 ((float) (sumSq / (double) gatedCount) + 1.0e-12f);
            boostDb = juce::jlimit (-8.0f, 14.0f, -14.0f - curLufs);
        }
        const float linGain = juce::Decibels::decibelsToGain (boostDb);
        const float ceiling = 0.8912509f; // -1.0 dBTP
        const float knee    = 0.7079458f; // -3.0 dBFS
        for (int ch = 0; ch < buf.getNumChannels(); ++ch)
        {
            float* w = buf.getWritePointer (ch);
            for (int i = 0; i < frames; ++i)
            {
                float x = w[i] * linGain;
                const float ax = std::abs (x);
                if (ax > knee)
                    x = std::copysign (knee + (ceiling - knee) * std::tanh ((ax - knee) / (ceiling - knee)), x);
                w[i] = x;
            }
        }
        return boostDb;
    };

    const bool autoMaster = recAutoMaster.load (std::memory_order_relaxed);
    juce::AudioBuffer<float> wetOut (2, numFrames);
    for (int ch = 0; ch < 2; ++ch)
        wetOut.copyFrom (ch, 0, wetSrc, juce::jmin (ch, wetSrc.getNumChannels() - 1), 0, numFrames);
    float appliedGainDb = 0.0f;
    if (autoMaster)
        appliedGainDb = applyAutoMaster14Lufs (wetOut, numFrames);
    lastMasterGainDb.store (appliedGainDb, std::memory_order_relaxed);

    // BGMが鳴っていた場合は「歌声(Wet)＋伴奏(BGM)」を合成して -14 LUFS マスタリングした『完パケMIX(-14LUFS).wav』も同時書き出し
    bool hasBgmSignal = false;
    if (bgmSrc != nullptr && bgmSrc->getNumSamples() >= numFrames)
        hasBgmSignal = (bgmSrc->getMagnitude (0, numFrames) > 0.0005f);

    juce::WavAudioFormat wavFormat;
    bool okWet = false, okDry = false, okMix = false;
    {
        std::unique_ptr<juce::FileOutputStream> outWet (wetFile.createOutputStream());
        if (outWet != nullptr)
        {
            std::unique_ptr<juce::AudioFormatWriter> writer (wavFormat.createWriterFor (outWet.get(), sr, 2, 24, {}, 0));
            if (writer != nullptr)
            {
                outWet.release();
                okWet = writer->writeFromAudioSampleBuffer (wetOut, 0, numFrames);
            }
        }
    }
    {
        std::unique_ptr<juce::FileOutputStream> outDry (dryFile.createOutputStream());
        if (outDry != nullptr)
        {
            std::unique_ptr<juce::AudioFormatWriter> writer (wavFormat.createWriterFor (outDry.get(), sr, 2, 24, {}, 0));
            if (writer != nullptr)
            {
                outDry.release();
                okDry = writer->writeFromAudioSampleBuffer (drySrc, 0, numFrames);
            }
        }
    }
    if (hasBgmSignal && bgmSrc != nullptr)
    {
        auto mixFile = dir.getNonexistentChildFile ("VocalGzzio_" + prefixTag + "_" + stamp + juce::String::fromUTF8 ("_完パケMIX(-14LUFS)"), ".wav", false);
        juce::AudioBuffer<float> mixBuf (2, numFrames);
        for (int ch = 0; ch < 2; ++ch)
        {
            mixBuf.copyFrom (ch, 0, wetSrc, juce::jmin (ch, wetSrc.getNumChannels() - 1), 0, numFrames);
            mixBuf.addFrom  (ch, 0, *bgmSrc, juce::jmin (ch, bgmSrc->getNumChannels() - 1), 0, numFrames);
        }
        const float mixGainDb = applyAutoMaster14Lufs (mixBuf, numFrames);
        if (autoMaster)
            lastMasterGainDb.store (mixGainDb, std::memory_order_relaxed);
        std::unique_ptr<juce::FileOutputStream> outMix (mixFile.createOutputStream());
        if (outMix != nullptr)
        {
            std::unique_ptr<juce::AudioFormatWriter> writer (wavFormat.createWriterFor (outMix.get(), sr, 2, 24, {}, 0));
            if (writer != nullptr)
            {
                outMix.release();
                okMix = writer->writeFromAudioSampleBuffer (mixBuf, 0, numFrames);
            }
        }
    }
    if (okWet)
    {
        const float sec = (float) ((double) numFrames / sr);
        const juce::ScopedLock sl (bgmMetaLock);
        lastSavedWavStatus = juce::String::fromUTF8 ("✅ 保存完了 (")
                           + juce::String (sec, 1) + juce::String::fromUTF8 ("秒 / 24bit")
                           + (autoMaster ? juce::String::fromUTF8 (" / -14LUFS音圧最適化済") : juce::String())
                           + juce::String::fromUTF8 ("): ")
                           + wetFile.getFileName()
                           + (okMix ? juce::String::fromUTF8 (" ＋ 完パケMIX ＋ 素の声(Dry)") : juce::String::fromUTF8 (" ＋ 素の声(Dry)"));
    }
    return okWet && okDry;
}

void VocalGzzioProcessor::startWavRecording() noexcept
{
    if (wavRecMaxSamples <= 0)
        wavRecMaxSamples = juce::jmax (96000, (int) std::lround (120.0 * (currentSampleRate > 1000.0 ? currentSampleRate : 48000.0)));
    if (wavRecDryBuf.getNumSamples() < wavRecMaxSamples)
    {
        wavRecDryBuf.setSize (2, wavRecMaxSamples, false, true, true);
        wavRecWetBuf.setSize (2, wavRecMaxSamples, false, true, true);
        wavRecBgmBuf.setSize (2, wavRecMaxSamples, false, true, true);
    }
    wavRecPos.store (0, std::memory_order_relaxed);
    wavRecording.store (true, std::memory_order_release);
}

bool VocalGzzioProcessor::stopAndSaveWavRecording()
{
    if (! wavRecording.exchange (false, std::memory_order_acq_rel))
        return false;
    const int n = juce::jlimit (0, wavRecMaxSamples, wavRecPos.load (std::memory_order_relaxed));
    if (n < 1024)
    {
        const juce::ScopedLock sl (bgmMetaLock);
        lastSavedWavStatus = juce::String::fromUTF8 ("⚠ 録音時間が短すぎるため保存をキャンセルしました");
        return false;
    }
    return writeDryWetWavPair (wavRecDryBuf, wavRecWetBuf, n, juce::String::fromUTF8 ("録音"), &wavRecBgmBuf);
}

void VocalGzzioProcessor::toggleWavRecording()
{
    if (isWavRecording()) stopAndSaveWavRecording();
    else                  startWavRecording();
}

bool VocalGzzioProcessor::saveRetroWavRecording()
{
    const int filled = juce::jlimit (0, retroMaxSamples, retroFilled.load (std::memory_order_relaxed));
    if (filled < 1024 || retroMaxSamples <= 0)
    {
        const juce::ScopedLock sl (bgmMetaLock);
        lastSavedWavStatus = juce::String::fromUTF8 ("⚠ まだ直前の音声が貯まっていません（数秒歌ってから押してください）");
        return false;
    }

    retroFreeze.store (true, std::memory_order_release);
    const int wp = retroWritePos.load (std::memory_order_relaxed);
    juce::AudioBuffer<float> orderedDry (2, filled);
    juce::AudioBuffer<float> orderedWet (2, filled);
    juce::AudioBuffer<float> orderedBgm (2, filled);
    const int startIdx = (wp - filled + retroMaxSamples) % retroMaxSamples;
    const bool haveBgmBuf = (retroBgmBuf.getNumSamples() >= retroMaxSamples);
    for (int ch = 0; ch < 2; ++ch)
    {
        const float* sD = retroDryBuf.getReadPointer (ch);
        const float* sW = retroWetBuf.getReadPointer (ch);
        const float* sB = haveBgmBuf ? retroBgmBuf.getReadPointer (ch) : nullptr;
        float* dD = orderedDry.getWritePointer (ch);
        float* dW = orderedWet.getWritePointer (ch);
        float* dB = orderedBgm.getWritePointer (ch);
        for (int i = 0; i < filled; ++i)
        {
            const int idx = (startIdx + i) % retroMaxSamples;
            dD[i] = sD[idx];
            dW[i] = sW[idx];
            dB[i] = sB ? sB[idx] : 0.0f;
        }
    }
    retroFreeze.store (false, std::memory_order_release);
    return writeDryWetWavPair (orderedDry, orderedWet, filled, juce::String::fromUTF8 ("直前30秒さかのぼり"), &orderedBgm);
}

bool VocalGzzioProcessor::runVoiceTargetDiagnosis (int targetStyle)
{
    targetStyle = juce::jlimit (0, 4, targetStyle);
    lastVoiceTargetStyle.store (targetStyle, std::memory_order_relaxed);

    const double sr = currentSampleRate > 1000.0 ? currentSampleRate : 48000.0;
    // 1) 5秒試聴ルーパーの録音があればそれを使い、無ければ直前30秒リングバッファの最新5秒を使う
    std::vector<float> mono;
    const int audLen = auditionLen.load (std::memory_order_relaxed);
    if (audLen >= (int) (0.5 * sr) && auditionBuf.getNumSamples() >= audLen)
    {
        mono.resize ((size_t) audLen);
        const float* L = auditionBuf.getReadPointer (0);
        const float* R = auditionBuf.getReadPointer (1);
        for (int i = 0; i < audLen; ++i) mono[(size_t) i] = 0.5f * (L[i] + R[i]);
    }
    else
    {
        const int filled = juce::jlimit (0, retroMaxSamples, retroFilled.load (std::memory_order_relaxed));
        const int analyzeLen = juce::jmin (filled, (int) (5.0 * sr));
        if (analyzeLen > 1024 && retroDryBuf.getNumSamples() >= retroMaxSamples)
        {
            mono.resize ((size_t) analyzeLen);
            const int wp = retroWritePos.load (std::memory_order_relaxed);
            const int startIdx = (wp - analyzeLen + retroMaxSamples) % retroMaxSamples;
            const float* L = retroDryBuf.getReadPointer (0);
            const float* R = retroDryBuf.getReadPointer (1);
            for (int i = 0; i < analyzeLen; ++i)
            {
                const int idx = (startIdx + i) % retroMaxSamples;
                mono[(size_t) i] = 0.5f * (L[idx] + R[idx]);
            }
        }
    }

    // 4帯域エネルギー（0:低中域温かみ 240Hz, 1:声芯 2000Hz, 2:サ行・輪郭 5200Hz, 3:空気感 10500Hz）を算出
    // ※16kHz / 22.05kHz (Bluetooth HFP / MME) 等の低サンプルレート環境でもナイキスト周波数超えアサートが起きないよう 0.44*sr 上限で安全クランプ
    float userNorm[4] = { 0.50f, 0.52f, 0.48f, 0.44f };
    bool measuredFromVoice = false;
    if (mono.size() >= 2048)
    {
        double eBand[4] = { 1e-9, 1e-9, 1e-9, 1e-9 };
        const float freqs[4] = { 240.0f, 2000.0f, 5200.0f, 10500.0f };
        const float qs[4]    = { 0.8f,   0.9f,    1.1f,    0.8f     };
        double totalVoiced = 0.0;
        const float maxSafeFreq = juce::jmax (100.0f, (float) (sr * 0.44));
        for (int b = 0; b < 4; ++b)
        {
            juce::dsp::IIR::Filter<float> bp;
            const float fClamped = juce::jlimit (40.0f, maxSafeFreq, freqs[b]);
            bp.coefficients = juce::dsp::IIR::Coefficients<float>::makeBandPass (sr, fClamped, qs[b]);
            if (bp.coefficients == nullptr) continue;
            double s = 0.0;
            for (size_t i = 0; i < mono.size(); ++i)
            {
                const float y = bp.processSample (mono[i]);
                s += (double) y * y;
                if (b == 0) totalVoiced += (double) mono[i] * mono[i];
            }
            eBand[b] = s / (double) mono.size();
        }
        const double rms = std::sqrt (totalVoiced / (double) mono.size());
        if (rms > 0.001)
        {
            measuredFromVoice = true;
            const double tot = eBand[0] + eBand[1] + eBand[2] + eBand[3] + 1e-9;
            const float dbRel[4] = {
                10.0f * std::log10 ((float) (eBand[0] / tot) + 1e-6f),
                10.0f * std::log10 ((float) (eBand[1] / tot) + 1e-6f),
                10.0f * std::log10 ((float) (eBand[2] / tot) + 1e-6f),
                10.0f * std::log10 ((float) (eBand[3] / tot) + 1e-6f)
            };
            userNorm[0] = juce::jlimit (0.15f, 0.92f, 0.65f + (dbRel[0] + 4.0f) * 0.05f);
            userNorm[1] = juce::jlimit (0.15f, 0.92f, 0.65f + (dbRel[1] + 5.0f) * 0.05f);
            userNorm[2] = juce::jlimit (0.15f, 0.92f, 0.60f + (dbRel[2] + 9.0f) * 0.05f);
            userNorm[3] = juce::jlimit (0.15f, 0.92f, 0.60f + (dbRel[3] + 12.0f) * 0.05f);
        }
    }

    struct TargetSpec
    {
        const char* name;
        float bands[4]; // 倍音VELVET, 声芯FOCUS, 空気SILK, 立体HALO の目標プロファイル
        float mudDb, presDb, airDb, deessPct, drivePct;
        float consPct, brPct, ringPct, susPct, resPct, ridePct;
    };
    static const TargetSpec specs[5] = {
        { "✨ 透明感ウィスパー（バラード・ASMR歌枠）",     { 0.42f, 0.56f, 0.88f, 0.68f }, -2.2f, 3.0f, 7.5f, 34.0f, 16.0f, 18.0f, 34.0f, 22.0f, 36.0f, 30.0f, 46.0f },
        { "🔥 芯のある歌い手リード（王道主役ボーカル）",   { 0.68f, 0.86f, 0.64f, 0.44f }, -1.8f, 4.8f, 5.5f, 26.0f, 32.0f, 30.0f, 16.0f, 36.0f, 28.0f, 38.0f, 44.0f },
        { "💎 アイドル・キラ艶ポップ（華やか高域＆立体）", { 0.54f, 0.78f, 0.84f, 0.62f }, -2.0f, 4.5f, 7.0f, 30.0f, 22.0f, 36.0f, 26.0f, 32.0f, 24.0f, 34.0f, 42.0f },
        { "🎙 深みのあるラジオ・雑談低音（温かい包容力）", { 0.84f, 0.70f, 0.42f, 0.24f }, -0.8f, 2.6f, 3.0f, 36.0f, 38.0f, 32.0f, 12.0f, 22.0f, 20.0f, 32.0f, 48.0f },
        { "🎸 アコギ・バンド弾き語り（中域の輪郭重視）",   { 0.60f, 0.82f, 0.58f, 0.36f }, -2.6f, 4.2f, 4.8f, 28.0f, 26.0f, 28.0f, 16.0f, 28.0f, 28.0f, 36.0f, 40.0f }
    };
    const auto& sp = specs[targetStyle];

    for (int b = 0; b < 4; ++b)
    {
        voiceDiagUserBands[b].store (userNorm[b], std::memory_order_relaxed);
        voiceDiagTargetBands[b].store (sp.bands[b], std::memory_order_relaxed);
    }

    // ユーザーの声の帯域バランスとの差分を加味して2Dオーブ4軸＋EQ＋声質補正DSPを自動調律
    const float diffBody  = userNorm[0] - 0.52f;
    const float diffFocus = 0.55f - userNorm[1];
    const float diffSib   = userNorm[2] - 0.50f;
    const float diffAir   = 0.48f - userNorm[3];

    const float outVelvet = juce::jlimit (10.0f, 95.0f, (sp.bands[0] - diffBody * 0.25f) * 100.0f);
    const float outFocus  = juce::jlimit (15.0f, 95.0f, (sp.bands[1] + diffFocus * 0.30f) * 100.0f);
    const float outSilk   = juce::jlimit (15.0f, 95.0f, (sp.bands[2] + diffAir * 0.30f) * 100.0f);
    const float outHalo   = juce::jlimit (5.0f,  90.0f, sp.bands[3] * 100.0f);
    const float outMud    = juce::jlimit (-6.0f, 0.0f,  sp.mudDb - juce::jmax (0.0f, diffBody * 4.5f));
    const float outPres   = juce::jlimit (0.0f,  8.0f,  sp.presDb + diffFocus * 3.0f);
    const float outAir    = juce::jlimit (0.0f,  9.0f,  sp.airDb  + diffAir * 3.5f);
    const float outDeess  = juce::jlimit (15.0f, 65.0f, sp.deessPct + diffSib * 30.0f);
    const float outRes    = juce::jlimit (20.0f, 65.0f, sp.resPct + juce::jmax (0.0f, diffBody) * 28.0f + juce::jmax (0.0f, -diffFocus) * 24.0f);
    const float outCons   = juce::jlimit (12.0f, 55.0f, sp.consPct + diffFocus * 26.0f);
    const float outBr     = juce::jlimit (6.0f,  48.0f, sp.brPct + diffAir * 24.0f);
    const float outRing   = juce::jlimit (12.0f, 48.0f, sp.ringPct + diffFocus * 22.0f);

    auto setP = [this] (const char* id, float val)
    {
        if (auto* p = apvts.getParameter (id))
            p->setValueNotifyingHost (p->convertTo0to1 (val));
    };
    setP ("argon_velvet", outVelvet);
    setP ("argon_focus",  outFocus);
    setP ("argon_silk",   outSilk);
    setP ("argon_halo",   outHalo);
    setP ("mud",          outMud);
    setP ("presence",     outPres);
    setP ("air",          outAir);
    setP ("deess",        outDeess);
    setP ("drive",        sp.drivePct);
    setP ("res_amt",      outRes);
    setP ("ride_amt",     sp.ridePct);
    setP ("cons_amt",     outCons);
    setP ("br_amt",       outBr);
    setP ("ring",         outRing);
    setP ("sustain",      sp.susPct);

    const char* voiceTypeTag = "中音域・ナチュラル型";
    if      (userNorm[0] > 0.62f && userNorm[3] < 0.45f) voiceTypeTag = "中低音リッチ・チェスト優位型";
    else if (userNorm[3] > 0.58f && userNorm[0] < 0.46f) voiceTypeTag = "ハイトーン・クリア＆ブレス型";
    else if (userNorm[1] > 0.62f)                        voiceTypeTag = "声芯・ベルティング優位型";
    else if (userNorm[0] > 0.58f && userNorm[1] < 0.48f) voiceTypeTag = "ウォーム・ソフト発声型";

    {
        const juce::ScopedLock sl (bgmMetaLock);
        voiceTargetSummary = (measuredFromVoice
            ? (juce::String::fromUTF8 ("🎯 声質診断【") + juce::String::fromUTF8 (voiceTypeTag) + juce::String::fromUTF8 ("】→「"))
            : juce::String::fromUTF8 ("🎯 推奨プロファイルに基づき、「"))
            + juce::String::fromUTF8 (sp.name)
            + juce::String::fromUTF8 ("」へ2Dオーブ4軸（倍音 ") + juce::String ((int) outVelvet)
            + juce::String::fromUTF8 ("% / 声芯 ") + juce::String ((int) outFocus)
            + juce::String::fromUTF8 ("% / 空気 ") + juce::String ((int) outSilk)
            + juce::String::fromUTF8 ("% / 立体 ") + juce::String ((int) outHalo)
            + juce::String::fromUTF8 ("%）＆声質補正DSPを自動調律しました！");
    }
    markStateDirty();
    return true;
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new VocalGzzioProcessor();
}

#if JUCE_WINDOWS
#ifndef NOMINMAX
 #define NOMINMAX
#endif
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>

namespace
{
    struct GzPolicyConfigWin10 : public IUnknown
    {
        virtual HRESULT STDMETHODCALLTYPE GetMixFormat (PCWSTR, WAVEFORMATEX**) = 0;
        virtual HRESULT STDMETHODCALLTYPE GetDeviceFormat (PCWSTR, INT, WAVEFORMATEX**) = 0;
        virtual HRESULT STDMETHODCALLTYPE ResetDeviceFormat (PCWSTR) = 0;
        virtual HRESULT STDMETHODCALLTYPE SetDeviceFormat (PCWSTR, WAVEFORMATEX*, WAVEFORMATEX*) = 0;
        virtual HRESULT STDMETHODCALLTYPE GetProcessingPeriod (PCWSTR, INT, PINT64, PINT64) = 0;
        virtual HRESULT STDMETHODCALLTYPE SetProcessingPeriod (PCWSTR, PINT64) = 0;
        virtual HRESULT STDMETHODCALLTYPE GetShareMode (PCWSTR, void*) = 0;
        virtual HRESULT STDMETHODCALLTYPE SetShareMode (PCWSTR, void*) = 0;
        virtual HRESULT STDMETHODCALLTYPE GetPropertyValue (PCWSTR, const PROPERTYKEY&, PROPVARIANT*) = 0;
        virtual HRESULT STDMETHODCALLTYPE SetPropertyValue (PCWSTR, const PROPERTYKEY&, PROPVARIANT*) = 0;
        virtual HRESULT STDMETHODCALLTYPE SetDefaultEndpoint (PCWSTR wszDeviceId, ERole eRole) = 0;
        virtual HRESULT STDMETHODCALLTYPE SetEndpointVisibility (PCWSTR, INT) = 0;
    };

    static GzPolicyConfigWin10* createPolicyConfig()
    {
        static const GUID clsidPolicyConfig = { 0x870af99c, 0x171d, 0x4f9e, { 0xaf, 0x0d, 0xe6, 0x3d, 0xf4, 0x0c, 0x2b, 0xc9 } };
        static const GUID iidPolicyConfig10 = { 0xf8679f50, 0x850a, 0x41cf, { 0x9c, 0x72, 0x43, 0x0f, 0x29, 0x02, 0x90, 0xc8 } };
        static const GUID iidPolicyConfig7  = { 0x568b9108, 0x44bf, 0x40b4, { 0x90, 0x06, 0x86, 0xaf, 0xe5, 0xb5, 0xa6, 0x20 } };

        GzPolicyConfigWin10* pConfig = nullptr;
        HRESULT hr = CoCreateInstance (clsidPolicyConfig, nullptr, CLSCTX_ALL, iidPolicyConfig10, (void**) &pConfig);
        if (FAILED (hr) || pConfig == nullptr)
            hr = CoCreateInstance (clsidPolicyConfig, nullptr, CLSCTX_ALL, iidPolicyConfig7, (void**) &pConfig);
        return SUCCEEDED (hr) ? pConfig : nullptr;
    }

    static void setWindowsEndpointDefaultAllRoles (PCWSTR devId)
    {
        if (devId == nullptr || devId[0] == L'\0') return;
        if (auto* pConfig = createPolicyConfig())
        {
            pConfig->SetDefaultEndpoint (devId, eConsole);
            pConfig->SetDefaultEndpoint (devId, eMultimedia);
            pConfig->SetDefaultEndpoint (devId, eCommunications);
            pConfig->Release();
        }
    }

    static bool trySetMmDeviceSampleRate (IMMDevice* dev, DWORD targetSampleRate)
    {
        if (dev == nullptr || targetSampleRate < 8000) return false;
        LPWSTR wstrId = nullptr;
        if (FAILED (dev->GetId (&wstrId)) || wstrId == nullptr) return false;

        bool changed = false;
        if (auto* pConfig = createPolicyConfig())
        {
            WAVEFORMATEX* pwfx = nullptr;
            if (SUCCEEDED (pConfig->GetDeviceFormat (wstrId, 0, &pwfx)) && pwfx != nullptr)
            {
                if (pwfx->nSamplesPerSec != targetSampleRate && pwfx->nBlockAlign > 0)
                {
                    pwfx->nSamplesPerSec = targetSampleRate;
                    pwfx->nAvgBytesPerSec = pwfx->nSamplesPerSec * pwfx->nBlockAlign;
                    if (SUCCEEDED (pConfig->SetDeviceFormat (wstrId, pwfx, nullptr)))
                        changed = true;
                }
                CoTaskMemFree (pwfx);
            }
            pConfig->Release();
        }
        CoTaskMemFree (wstrId);
        return changed;
    }

    static juce::String getMmDeviceFriendlyName (IMMDevice* dev)
    {
        if (dev == nullptr) return {};
        IPropertyStore* props = nullptr;
        juce::String result;
        if (SUCCEEDED (dev->OpenPropertyStore (STGM_READ, &props)) && props != nullptr)
        {
            PROPVARIANT varName;
            PropVariantInit (&varName);
            if (SUCCEEDED (props->GetValue (PKEY_Device_FriendlyName, &varName)) && varName.vt == VT_LPWSTR && varName.pwszVal != nullptr)
                result = juce::String (varName.pwszVal);
            PropVariantClear (&varName);
            props->Release();
        }
        return result;
    }

    static double getMmDeviceSampleRate (IMMDevice* dev)
    {
        if (dev == nullptr) return 0.0;
        double sr = 0.0;
        IAudioClient* client = nullptr;
        if (SUCCEEDED (dev->Activate (__uuidof (IAudioClient), CLSCTX_ALL, nullptr, (void**) &client)) && client != nullptr)
        {
            WAVEFORMATEX* pwfx = nullptr;
            if (SUCCEEDED (client->GetMixFormat (&pwfx)) && pwfx != nullptr)
            {
                sr = (double) pwfx->nSamplesPerSec;
                CoTaskMemFree (pwfx);
            }
            client->Release();
        }
        return sr;
    }

    // サウンドハウス・Amazonで流通する主要60ブランド・160機種以上のインターフェース／配信ミキサー／USBマイク／VR機器を網羅
    static bool doesEndpointMatchHardware (const juce::String& wdmName, const juce::String& activeHwName)
    {
        if (wdmName.isEmpty() || activeHwName.isEmpty()) return false;
        static const char* const kBrandTokens[] = {
            // YAMAHA / Steinberg
            "AG03", "AG06", "AG08", "ZG01", "ZG02", "YH-G01", "UR12", "UR22", "UR24", "UR44", "UR816", "URX", "UR-RT", "IXO12", "IXO22", "IXO", "Steinberg", "Yamaha",
            // Roland / BOSS
            "Rubix", "BRIDGE CAST", "GO:MIXER", "VT-4", "E-4", "GIGCASTER", "Roland", "BOSS",
            // Focusrite
            "Focusrite", "Scarlett", "Vocaster", "Clarett",
            // Solid State Logic (SSL)
            "SSL", "Solid State Logic",
            // MOTU
            "MOTU", "M2", "M4", "M6", "UltraLite", "828", "Track16", "MicroBook",
            // Universal Audio
            "Volt", "Apollo", "Arrow", "Universal Audio",
            // RME
            "RME", "Babyface", "Fireface", "Digiface", "Madiface",
            // Audient
            "Audient", "iD4", "iD14", "iD24", "iD44", "EVO 4", "EVO 8", "EVO 16", "EVO",
            // audio-technica
            "AT-UMX3", "AT2020", "AT2040", "AT-BHA", "audio-technica", "Audio-Technica",
            // ZOOM (32bit float / Streaming / Podcast)
            "ZOOM", "UAC-232", "UAC-432", "UAC-", "AMS-22", "AMS-24", "AMS-44", "AMS-", "PodTrak", "LiveTrak",
            // TASCAM
            "TASCAM", "US-1x2", "US-2x2", "US-4x4", "US-32", "US-42", "MiNiSTUDIO", "Series 102", "Series 208", "Portacapture", "Mixcast", "FR-AV2",
            // PreSonus
            "PreSonus", "AudioBox", "Revelator", "Studio 24", "Studio 26", "Studio 68", "Studio 18", "Quantum",
            // Arturia & Native Instruments
            "MiniFuse", "AudioFuse", "Arturia", "Komplete", "Native Instruments",
            // Lewitt & Topping
            "Lewitt", "CONNECT 2", "CONNECT 6", "Topping", "E2x2", "E4x4",
            // RODE / Elgato / TC Helicon (Multi-bus Streaming Mixers)
            "RODECaster", "Streamer X", "AI-1", "RODE", "Rode",
            "Wave XLR", "Wave:3", "Wave:1", "Wave Link", "Elgato",
            "GoXLR", "TC-Helicon", "Perform-V",
            // Antelope Audio & Apogee & Neumann
            "Antelope", "Zen Go", "Zen Quadro", "Zen Tour", "Discrete",
            "Apogee", "Boom", "Duet", "Symphony",
            "Neumann", "MT 48",
            // Mackie / Blackstar / IK Multimedia / iConnectivity / Cranborne / Allen & Heath
            "Mackie", "Onyx", "ProFX", "M-Caster", "MainStream", "DLZ",
            "Blackstar", "Polar", "IK Multimedia", "AXE I/O", "iRig",
            "iConnectivity", "AUDIO4c", "Cranborne", "Camden", "Allen & Heath", "ZEDi",
            // Behringer & M-Audio
            "Behringer", "UMC", "UM2", "U-Phoria", "FLOW 8", "Xenyx",
            "M-Audio", "M-Track", "AIR 192",
            // Amazon Best-Selling Gaming Mixers / USB Mics / Headsets / VR
            "FIFINE", "AmpliGame", "SC3", "Maono", "Maonocaster", "AME2", "G1 NEO",
            "Razer", "Seiren", "Sound Blaster", "GC7", "SteelSeries", "GameDAC", "Arctis", "Sonar", "MixAmp", "Astro",
            "HyperX", "QuadCast", "SoloCast", "Yeti", "Blue Microphones", "Shure", "MV7", "MVX2U",
            "INZONE", "Logicool", "Logitech", "Oculus", "Quest", "Steam Streaming", "VIVE", "Index", "PICO"
        };
        for (const char* tok : kBrandTokens)
        {
            const juce::String t (tok);
            if (activeHwName.containsIgnoreCase (t) && wdmName.containsIgnoreCase (t))
                return true;
        }
        return false;
    }
}
#endif

double gz::StreamOut::inspectAndHealWindowsAudioForInterface (const juce::String& activeHardwareName,
                                                              juce::String* outRealSpeaker,
                                                              juce::String* outRealMic,
                                                              juce::String* outDiagReport)
{
   #if JUCE_WINDOWS
    // 1. Windowsの「通信時の音量80%減衰(ダッキング)」を「何もしない(3)」に設定
    {
        HKEY hKey = nullptr;
        if (RegCreateKeyExW (HKEY_CURRENT_USER, L"Software\\Microsoft\\Multimedia\\Audio",
                             0, nullptr, 0, KEY_SET_VALUE, nullptr, &hKey, nullptr) == ERROR_SUCCESS && hKey != nullptr)
        {
            const DWORD pref = 3; // 3 = Do nothing (何もしない)
            RegSetValueExW (hKey, L"UserDuckingPreference", 0, REG_DWORD, reinterpret_cast<const BYTE*> (&pref), sizeof (pref));
            RegCloseKey (hKey);
        }
    }

    double detectedRate = 0.0;
    double hwRenderRate = 0.0;
    juce::String hwRenderName;
    bool restoredDefaultSpk = false;
    bool alignedHwFormat = false;

    const HRESULT hrCom = CoInitializeEx (nullptr, COINIT_APARTMENTTHREADED);
    IMMDeviceEnumerator* pEnum = nullptr;
    if (SUCCEEDED (CoCreateInstance (__uuidof (MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof (IMMDeviceEnumerator), (void**) &pEnum)) && pEnum != nullptr)
    {
        // 2. 既定の再生デバイス(スピーカー/ヘッドホン)が仮想ドライバに奪われていないか点検
        bool needRestoreRender = false;
        for (ERole role : { eConsole, eMultimedia, eCommunications })
        {
            IMMDevice* defDev = nullptr;
            if (SUCCEEDED (pEnum->GetDefaultAudioEndpoint (eRender, role, &defDev)) && defDev != nullptr)
            {
                const auto name = getMmDeviceFriendlyName (defDev);
                if (isVirtualLoopbackName (name))
                    needRestoreRender = true;
                else if (role == eConsole)
                {
                    if (outRealSpeaker != nullptr) *outRealSpeaker = name;
                    detectedRate = getMmDeviceSampleRate (defDev);
                }
                defDev->Release();
            }
        }

        // 3. 全アクティブ再生エンドポイントを走査し、(A) 既定スピーカー救出候補 と (B) 同一ハードウェアの全WDM再生バス(GAME/CHAT/MAIN/AUX等) を特定する
        std::vector<IMMDevice*> matchedRenderDevs;
        {
            IMMDeviceCollection* pCol = nullptr;
            if (SUCCEEDED (pEnum->EnumAudioEndpoints (eRender, DEVICE_STATE_ACTIVE, &pCol)) && pCol != nullptr)
            {
                UINT count = 0;
                pCol->GetCount (&count);
                IMMDevice* bestSpk = nullptr;
                int bestScore = -1000;
                for (UINT i = 0; i < count; ++i)
                {
                    IMMDevice* item = nullptr;
                    if (SUCCEEDED (pCol->Item (i, &item)) && item != nullptr)
                    {
                        const auto name = getMmDeviceFriendlyName (item);
                        if (! isVirtualLoopbackName (name))
                        {
                            int score = 10;
                            if (doesEndpointMatchHardware (name, activeHardwareName))
                            {
                                score += 90;
                                item->AddRef();
                                matchedRenderDevs.push_back (item);
                                if (hwRenderName.isEmpty())
                                {
                                    hwRenderName = name;
                                    hwRenderRate = getMmDeviceSampleRate (item);
                                }
                            }
                            if (name.containsIgnoreCase ("Rubix") || name.containsIgnoreCase ("Focusrite")
                                || name.containsIgnoreCase ("Steinberg") || name.containsIgnoreCase ("MOTU")
                                || name.containsIgnoreCase ("AG03") || name.containsIgnoreCase ("AG06")
                                || name.containsIgnoreCase ("AG08") || name.containsIgnoreCase ("ZG01")
                                || name.containsIgnoreCase ("ZG02") || name.containsIgnoreCase ("BRIDGE CAST")
                                || name.containsIgnoreCase ("AT-UMX3") || name.containsIgnoreCase ("Volt")
                                || name.containsIgnoreCase ("SSL") || name.containsIgnoreCase ("Audient")
                                || name.containsIgnoreCase ("RME") || name.containsIgnoreCase ("MiniFuse")
                                || name.containsIgnoreCase ("UAC-") || name.containsIgnoreCase ("GoXLR")
                                || name.containsIgnoreCase ("RODECaster") || name.containsIgnoreCase ("CONNECT")
                                || name.containsIgnoreCase ("E2x2") || name.containsIgnoreCase ("Zen Go")
                                || name.containsIgnoreCase ("Headphone")
                                || name.containsIgnoreCase (juce::String::fromUTF8 ("スピーカー"))
                                || name.containsIgnoreCase (juce::String::fromUTF8 ("ヘッドホン"))
                                || name.containsIgnoreCase ("OUT"))
                                score += 50;
                            if (name.containsIgnoreCase ("NVIDIA") || name.containsIgnoreCase ("AMD") || name.containsIgnoreCase ("Display"))
                                score -= 20;

                            if (score > bestScore)
                            {
                                if (bestSpk != nullptr) bestSpk->Release();
                                bestSpk = item;
                                bestSpk->AddRef();
                                bestScore = score;
                            }
                        }
                        item->Release();
                    }
                }
                if (bestSpk != nullptr)
                {
                    if (needRestoreRender || detectedRate <= 0.0)
                    {
                        const auto name = getMmDeviceFriendlyName (bestSpk);
                        if (outRealSpeaker != nullptr) *outRealSpeaker = name;
                        detectedRate = getMmDeviceSampleRate (bestSpk);
                        if (needRestoreRender)
                        {
                            LPWSTR wstrId = nullptr;
                            if (SUCCEEDED (bestSpk->GetId (&wstrId)) && wstrId != nullptr)
                            {
                                setWindowsEndpointDefaultAllRoles (wstrId);
                                restoredDefaultSpk = true;
                                CoTaskMemFree (wstrId);
                            }
                        }
                    }
                    bestSpk->Release();
                }
                pCol->Release();
            }
        }

        // 4. 全アクティブ録音エンドポイントを走査し、実マイク名と同一ハードウェアの全WDM録音バス(MAIN/CHAT/STREAM/LOOPBACK等)も特定する
        std::vector<IMMDevice*> matchedCaptureDevs;
        {
            IMMDevice* defCap = nullptr;
            if (SUCCEEDED (pEnum->GetDefaultAudioEndpoint (eCapture, eConsole, &defCap)) && defCap != nullptr)
            {
                const auto name = getMmDeviceFriendlyName (defCap);
                if (! isVirtualLoopbackName (name) && outRealMic != nullptr)
                    *outRealMic = name;
                defCap->Release();
            }

            IMMDeviceCollection* pCol = nullptr;
            if (SUCCEEDED (pEnum->EnumAudioEndpoints (eCapture, DEVICE_STATE_ACTIVE, &pCol)) && pCol != nullptr)
            {
                UINT count = 0;
                pCol->GetCount (&count);
                for (UINT i = 0; i < count; ++i)
                {
                    IMMDevice* item = nullptr;
                    if (SUCCEEDED (pCol->Item (i, &item)) && item != nullptr)
                    {
                        const auto name = getMmDeviceFriendlyName (item);
                        if (! isVirtualLoopbackName (name))
                        {
                            if (outRealMic != nullptr && outRealMic->isEmpty())
                                *outRealMic = name;
                            if (doesEndpointMatchHardware (name, activeHardwareName))
                            {
                                item->AddRef();
                                matchedCaptureDevs.push_back (item);
                            }
                        }
                        item->Release();
                    }
                }
                pCol->Release();
            }
        }

        // 5. 同一オーディオインターフェース（特にBRIDGE CAST / AG08 / ZG01 / GoXLR / RODECaster等のマルチバス機を含む）の
        //    全WDM再生バス・全WDM録音バスでサンプルレートが食い違っている場合、基準レートへ一括統一してクロック反転による音消えを根治する
        const double targetRate = (hwRenderRate >= 44100.0) ? hwRenderRate : ((detectedRate >= 44100.0) ? detectedRate : 48000.0);
        if (targetRate >= 44100.0)
        {
            for (auto* rDev : matchedRenderDevs)
            {
                const double rSr = getMmDeviceSampleRate (rDev);
                if (rSr > 0.0 && std::abs (rSr - targetRate) > 1.0)
                    if (trySetMmDeviceSampleRate (rDev, (DWORD) std::lround (targetRate)))
                        alignedHwFormat = true;
            }
            for (auto* cDev : matchedCaptureDevs)
            {
                const double cSr = getMmDeviceSampleRate (cDev);
                if (cSr > 0.0 && std::abs (cSr - targetRate) > 1.0)
                    if (trySetMmDeviceSampleRate (cDev, (DWORD) std::lround (targetRate)))
                        alignedHwFormat = true;
            }
            if (! matchedRenderDevs.empty())
                detectedRate = targetRate;
        }

        for (auto* rDev : matchedRenderDevs)  rDev->Release();
        for (auto* cDev : matchedCaptureDevs) cDev->Release();
        pEnum->Release();
    }
    if (SUCCEEDED (hrCom))
        CoUninitialize();

    if (outDiagReport != nullptr)
    {
        juce::String rep;
        if (hwRenderName.isNotEmpty())
            rep << juce::String::fromUTF8 ("機材WDM出力: ") << hwRenderName << " (" << (int) hwRenderRate << "Hz)";
        else if (outRealSpeaker != nullptr && outRealSpeaker->isNotEmpty())
            rep << juce::String::fromUTF8 ("既定スピーカー: ") << *outRealSpeaker << " (" << (int) detectedRate << "Hz)";

        if (restoredDefaultSpk)
            rep << juce::String::fromUTF8 (" [既定スピーカー救出済]");
        if (alignedHwFormat)
            rep << juce::String::fromUTF8 (" [全バスクロック統一済]");
        if (isExclusiveWrapperAsioName (activeHardwareName))
            rep << juce::String::fromUTF8 (" ⚠️排他ASIOラッパー検出(共有モード推奨)");
        *outDiagReport = rep;
    }

    return detectedRate;
   #else
    juce::ignoreUnused (activeHardwareName, outRealSpeaker, outRealMic, outDiagReport);
    return 0.0;
   #endif
}

double gz::StreamOut::rescueWindowsDefaultSpeakerAndGetSampleRate (juce::String* outRealSpeaker, juce::String* outRealMic)
{
    return inspectAndHealWindowsAudioForInterface ({}, outRealSpeaker, outRealMic, nullptr);
}



