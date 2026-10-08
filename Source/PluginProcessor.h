#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <atomic>
#include <vector>
#include "VoiceShifter.h"
#include "PitchDetector.h"
#include "PitchCorrection.h"
#include "StreamOut.h"
#include "Heya.h"
#include "DenoiseClassifier.h"

// v4.0.0 リバーブ種別の番号。0..6 は従来のアルゴリズム式、
// 7..12 は「へや」= 計算で生成した空間IRの畳み込み。
// 古いプロジェクトの番号を動かさないため、必ず**末尾に足す**こと。
static constexpr int kHeyaFirst    = 7;
static constexpr int kRevTypeCount = kHeyaFirst + heya::kNumRooms;   // 13
#include "Resonance.h"     // v2.4.0 なめらか(動的レゾナンス抑制)
#include "HumKiller.h"     // v2.6.0 ジー音(電源ハム)の自動除去
#include "Consonant.h"     // v2.6.0 ことば(子音エンハンサー)
#include "Proximity.h"     // v2.10.0 距離ならし(近接効果の自動補正)
#include "Checkup.h"       // v2.10.0 点検(使われ方・名前ごとの設定・報告セット)
#include "ModuleChain.h"   // v3.0   モジュールのON/OFF(OFF=完全素通し・10ms渡し)
#include "Ornament.h"      // v2.7.0 こぶし(しゃくり・こぶし保護)

//==============================================================================
// v2.10.0 #77/#78 音源モード — 同じツマミが「見る場所」を持ち替える
//
//  歌の声・アコギ・しゃべりは、同じ名前の問題でも出る場所が違う。
//   ・こもり: 歌は 300Hz、アコギの胴鳴りは 220Hz、しゃべりは 350Hz
//   ・かたさ: 歌は 3.2kHz、アコギのピックのカリつきは 2.5kHz
//   ・ぬけ  : 歌は 4.2kHz、アコギの弦の輪郭は 3.5kHz、しゃべりの明瞭度は 2.8kHz
//   ・きらめき: 歌は 11kHz の棚、アコギは 9kHz(弦の響き)
//   ・ディエッサー: 歌はサ行 5.2kHz、アコギはフレット/ピックの音 3kHz
//   ・なめらか: 歌は 900Hz-9kHz、アコギは胴のピークが低いので 250Hz-6kHz
//  ツマミの位置は同じまま、狙う場所だけを変える。だから「プリセット」ではない。
//
//  ★うた(0) の数値は v2.9.0 までと1つも変えていない。既定のまま使う限り
//    出音はビット単位で同じ（tools/dsp_srcmode.cpp で確認している）。
struct SourceProfile
{
    float mudHz, mudQ;          // こもり
    float harshHz, harshQ;      // かたさ
    float presHz, presQ;        // ぬけ
    float airHz;                // きらめき(棚)
    float dsDetectHz;           // ディエッサーが探す帯域
    float dsShelfHz;            // ディエッサーが下げる棚
    double resLoHz, resHiHz;    // なめらかが見張る範囲
    bool  voiceOnly;            // ことば・艶を使うか
    bool  breathOk;             // 息(小声のとき息の帯域を持ち上げる)を使うか
    bool  spaceOk;              // ひびき・やまびこを使うか

    // ---- v3.1「使いかた4種」でふえた分（既定値つき。古い3つはそのまま動く）----
    //  ★ここから下は必ず**末尾に足す**こと。上に挿すと下の3つの初期化がずれる。
    bool  pitchOk    = true;    // ピッチ補正・ボイス変換を使うか（和音が入る使いかたは false）
    float dnScale    = 1.0f;    // ノイズ除去の効き（1.0 = ふつう）
    float mudHz2     = 0.0f;    // 2点目のこもり（0 = 使わない）
    float mudQ2      = 1.2f;
    float dsDetectHz2= 0.0f;    // 2点目のディエッサーが探す中心（0 = 使わない）
    float dsDipHz2   = 3500.0f; // 2点目で下げる山（フレット/ピックの音）
    float dsDipQ2    = 1.2f;
    bool  pickOk     = false;   // 「ピックおさえ」を使うか（アコギだけ）
};

// v3.0「つぶさない」の、使いかたごとの既定。
//  うた・アコギ … ON。どちらも「大きく出したところが曲の山」なので、
//                  そこを潰すと盛り上がらない。
//  しゃべり     … OFF。話し声は**音量がそろっている方が聞き取りやすい**ので、
//                  張ったところをそのまま通すのは目的に反する。
//  ※ここは「使いかたを選び直したときの初期値」であって、鍵ではない。
//    選んだあとにスイッチを触れば、その選択が優先される。
inline bool crushRecommendedFor (int srcMode) noexcept
{
    // 0=うた / 1=アコギだけ / 3=弾き語り → ON（大きく出した所が曲の山）
    // 2=しゃべり → OFF（音量がそろっている方が聞き取りやすい）
    return srcMode != 2;
}

inline SourceProfile sourceProfile (int mode) noexcept
{
    switch (mode)
    {
        case 1:  // アコギだけ（v2.10.0 の「アコギ」。名前だけ変えた。番号は同じ1）
        {
            //  こもりは胴鳴りの 220Hz、かたさはピックのカリつき 2.5kHz、
            //  ぬけは弦の輪郭 3.5kHz、きらめきは 9kHz。ディエッサーはサ行ではなく
            //  フレット/ピックの音(3kHz)を探して 4kHz の棚を下げる。
            //  なめらかは胴のピークが低いところに出るので 250Hz-6kHz を見る。
            //  声のための処理(ことば・艶・息・ピッチ系)は全部切る。
            SourceProfile p { 220.0f, 1.4f, 2500.0f, 1.4f, 3500.0f, 0.8f, 9000.0f,
                              3000.0f, 4000.0f, 250.0, 6000.0, false, false, true };
            p.pitchOk = false;      // 和音に声用のピッチ検出は誤動作する（v2.10.0 からの動き）
            // ★v3.1 変更点: ノイズ除去を「ひかえめ」に（設計書§3）。
            //  弦の余韻は減衰がゆるやかなので、床(ノイズ)と間違えて消されやすい。
            p.dnScale = 0.7f;
            // v3.1 設計書§3 の追加ノブ「ピックおさえ」は、この使いかたにだけ出す。
            //  声が入るマイクで頭を丸めると子音まで鈍るので、ここだけ。
            p.pickOk  = true;
            return p;
        }
        case 2:  // しゃべり配信
        {
            //  こもりは 350Hz(マイクに近づくと出る)、ぬけは明瞭度の 2.8kHz。
            //  ことばは残す(聞き取りやすさが最優先)。息は口の音が目立つので切る。
            //  残響の有無はプリセットとON/OFFで選ぶ。用途で操作を無効化しない。
            SourceProfile p { 350.0f, 1.0f, 3200.0f, 1.2f, 2800.0f, 0.8f, 11000.0f,
                              5200.0f, 6500.0f, 900.0, 9000.0, true, false, true };
            // ★v3.1 変更点: ノイズ除去を「強め」に（設計書§3）。
            //  配信は生活音の中で録るのがふつうで、歌より音の隙間も多い。
            p.dnScale = 1.25f;
            // ★v3.1 変更点(2026-08-20 相談で決定): ピッチ補正・ボイス変換・ハモリを
            //  **音の側で止める**。しゃべりに音程を当てにいくと、語尾や抑揚が
            //  勝手に階段状になり、聞き取りにくくなる。画面から消すだけでは
            //  「ぜんぶ表示」で出したときに通ってしまうので、ここで切る。
            //  ★同時に、申告する遅延が 768 → 0 サンプルになる（配信の口パクずれが減る）。
            //   申告と実際が食い違うと DAW がトラックを前に引っ張るので、
            //   条件は必ず pitchOk 一本で見ること（v2.9.0 のハモリの失敗）。
            p.pitchOk = false;
            return p;
        }
        case 3:  // 弾き語り（v3.1 新設）— 1本のマイクに声とギターが同時に入る
        {
            //  ★ここは「声とギターの両方に効く1組の設定」を作る所。
            //   どちらか片方に最適化すると、もう片方が確実に外れる。
            //   ・こもり  … 声の 300Hz と 胴鳴りの 220Hz の**2点がけ**（下の mudHz2）
            //   ・かたさ  … 声の 3.2kHz（声が主役。ギターのカリつきは弱く扱う）
            //   ・ぬけ    … 声 4.2k と 弦の輪郭 3.5k の間で 3.8kHz、Qを 0.7 に広げて両方へ届かせる
            //   ・きらめき… 歌 11k と アコギ 9k の間の 10kHz
            //   ・サ行    … 声のサ行 5.2kHz と フレット 3kHz の**2点**（下の dsDetectHz2）
            //   ・なめらか… 胴のピークが低いので下は 250Hz まで、声のため上は 9kHz まで
            SourceProfile p { 300.0f, 1.0f, 3200.0f, 1.2f, 3800.0f, 0.7f, 10000.0f,
                              5200.0f, 6500.0f, 250.0, 9000.0, true, true, true };
            //  ピッチ補正・ボイス変換は切る。和音が同時に鳴っているマイクに
            //  「単音の声」を前提とした検出をかけると、必ず誤動作する。
            p.pitchOk = false;
            //  ノイズ除去はひかえめ。弦の余韻を床と間違えて消しやすいのはアコギと同じ。
            p.dnScale = 0.7f;
            //  2点目のこもり: 胴鳴り 220Hz。声の 300Hz と両方を同じツマミで下げる。
            p.mudHz2 = 220.0f; p.mudQ2 = 1.3f;
            //  2点目のサ行: フレット/ピックの音。3kHz を中心に**帯域で**見張り、
            //  3.5kHz の山を下げる（棚を下げると声の抜けまで一緒に暗くなるため）。
            p.dsDetectHz2 = 3000.0f; p.dsDipHz2 = 3500.0f; p.dsDipQ2 = 1.2f;
            return p;
        }
        default: // うた（v2.9.0 までと同じ数値。ここを変えると既存の曲の音が変わる）
            return { 300.0f, 1.0f, 3200.0f, 1.2f, 4200.0f, 0.9f, 11000.0f,
                     5200.0f, 6500.0f, 900.0, 9000.0, true, true, true };
    }
}

//==============================================================================
// VocalGzzio (ボーカルグッジオ) - real-time vocal channel strip, zero latency.
// Chain: Gate -> HPF -> Mud cut -> Harsh cut -> Comp1 (peaks) -> Comp2 (level)
//        -> De-esser -> Presence -> Air -> Warmth -> Doubler/Width -> Delay -> Reverb
// Tuner + meters are analysis-only (audio is never delayed).
//==============================================================================
class VocalGzzioProcessor : public juce::AudioProcessor,
                            private juce::ValueTree::Listener,
                            private juce::Timer,
                            private juce::AsyncUpdater
{
public:
    VocalGzzioProcessor();
    ~VocalGzzioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    // v2.6.0: ホストがこのプラグインの使用をやめる合図。実行待ちの非同期処理を
    // ここで捨てる。捨てないと、Cubase がチャンネルを片付けている最中(進捗
    // ダイアログがメッセージを回している間)に処理が飛び出して、ホストの解放
    // 処理と取り合いになり固まることがある。
    void releaseResources() override { cancelPendingUpdate(); }
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi()  const override { return true; }   // v2.1.0 MIDIスイッチ用
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.5; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    juce::AudioProcessorValueTreeState apvts { *this, nullptr, "PARAMS", createParameterLayout() };

    //== Tuner (analysis ring, read by editor timer) ==
    // 192 kHzでも95 msの観測窓を確保し、ギター6弦の低い音を測る。
    static constexpr int tunerSize = 32768;
    void   readTunerBuffer (std::vector<float>& dest) const;
    double getTunerSampleRate() const noexcept { return currentSampleRate; }

    //== Spectrum analyzer (analysis ring of the processed output, read by editor) ==
    static constexpr int analyzerSize = 4096;
    void readAnalyzerBuffer (std::vector<float>& dest) const;

    //== Smart EQ band state mirrored for the UI graph (display only) ==
    int   getSeqBandCount() const noexcept        { return seqBandsUI.load(); }
    float getSeqBandCutDb (int b) const noexcept  { return seqCutUI [juce::jlimit (0, seqBands - 1, b)].load(); }
    float getSeqBandFreq  (int b) const noexcept  { return seqFreqUI[juce::jlimit (0, seqBands - 1, b)].load(); }
    float getSeqBandQ     (int b) const noexcept  { return seqQBandUI[juce::jlimit (0, seqBands - 1, b)].load(); }
    float getSeqQ() const noexcept                { return seqQUI.load(); }

    //== Meters ==
    float getInputLevel()  const noexcept { return meterIn.load();  }
    float getOutputLevel() const noexcept { return meterOut.load(); }
    float getOutputRmsDb() const noexcept { return meterRmsDb.load(); }   // stream loudness meter
    float getHostBpm()     const noexcept { return hostBpm.load(); }      // 0 = host gives no tempo
    float getGainReductionDb() const noexcept { return meterGR.load(); }
    float getDeEssActivity()   const noexcept { return meterDS.load(); }   // 0..1
    float getDenoiseActivity() const noexcept { return meterDN.load(); }   // 0..1
    float getSmartEQActivity() const noexcept { return meterSEQ.load(); }  // 0..1
    float getPopActivity() const noexcept { return meterPop.load(); }      // v2.3.0 0..1
    float getResActivity() const noexcept { return meterRes.load(); }      // v2.4.0 最大カット(dB)
    int   getHumHz()      const noexcept { return meterHumHz.load(); }     // v2.6.0 0/50/60
    float getHumLevelDb() const noexcept { return meterHumDb.load(); }     // v2.6.0 見つけたハムの大きさ
    float getConsActivity() const noexcept { return meterCons.load(); }    // v2.6.0 いま持ち上げている量(dB)
    float getOrnProtect() const noexcept { return meterOrn.load(); }       // v2.7.0 いま守っている量 0..1
    int   getOrnKind()    const noexcept { return meterOrnKind.load(); }   // v2.7.0 0=なし 1=しゃくり 2=こぶし
    float getLipActivity() const noexcept { return meterLip.load(); }      // v2.3.0 0..1
    float getAutoTuneHz()         const noexcept { return atDetectedHz.load(); }        // 0 = unvoiced
    float getAutoTuneCorrection() const noexcept { return atCurrentCorrection.load(); } // semitones

    //== De-noise learn (RX-style: capture noise profile while silent) ==
    void requestDenoiseLearn() noexcept
    {
        dnLearnResult.store (0);
        // 集計用の配列は音声スレッドだけが触る。学習中の押し直しも安全にする。
        dnLearnCommand.store (1, std::memory_order_release);
    }
    bool isDenoiseLearning() const noexcept
    {
        const int command = dnLearnCommand.load (std::memory_order_acquire);
        return command == 1 || (command != -1 && learnCountdown.load() > 0);
    }

    // v2.10.0 ★覚えたノイズ床を捨てて、自動追従に戻す。
    //   これまで dnLearned は true になるだけで、false へ戻す道がどこにも無かった。
    //   状態XMLにも保存されるので、汚れた床は再起動しても版を上げても残り続けた。
    //   「急に音が悪くなって、それがずっと直らない」の原因がこれ。
    void clearDenoiseLearn() noexcept
    {
        dnLearnedShared.store (false);
        for (auto& f : dnFloorShared) f.store (1e-5f);
        dnLearnResult.store (0);
        dnLearnCommand.store (-1, std::memory_order_release);
        markStateDirty();
    }
    bool  isDenoiseLearned()    const noexcept { return dnLearnedShared.load(); }
    int   getDenoiseLearnResult() const noexcept { return dnLearnResult.load(); }
    float getDenoiseLearnLevelDb()  const noexcept { return dnLearnLevelDb.load(); }
    float getDenoiseLearnSpreadDb() const noexcept { return dnLearnSpreadDb.load(); }
    void  clearDenoiseLearnResult() noexcept { dnLearnResult.store (0); }

    //== AUTO SETUP: analyse the voice, then set the strip automatically.
    //   mode 0 = talk (5 s, corrective EQ only)
    //   mode 1 = sing (8 s, full strip: level, comp, EQ, de-ess, denoise, space) ==
    void requestAutoSetup (int mode = 0) noexcept
    {
        asMode.store (mode);
        for (auto& b : asBandSum) b.store (0.0);
        asSampleCount.store (0);
        asPeak.store (0.0f);
        asSumSq.store (0.0);
        asBlockDbSum.store (0.0);
        asBlockDbSqSum.store (0.0);
        asBlockCount.store (0);
        asMinBlockDb.store (0.0f);
        asTotalLen.store ((int) ((mode == 1 ? 8.0 : 5.0) * currentSampleRate));
        autoSetupCountdown.store (asTotalLen.load());
    }
    bool  isAutoSetupRunning() const noexcept { return autoSetupCountdown.load() > 0; }
    int   getAutoSetupMode() const noexcept { return asMode.load(); }
    float getAutoSetupProgress() const noexcept                       // 0..1
    {
        const int total = asTotalLen.load();
        const int left  = autoSetupCountdown.load();
        return total > 0 ? juce::jlimit (0.0f, 1.0f, 1.0f - (float) left / (float) total) : 0.0f;
    }
    int  getAutoSetupResult() noexcept { return autoSetupResult.exchange (-1); }  // -1 none; 0..2 talk tilt; 10..12 sing tilt
    void applyAutoSetup();                                            // message-thread apply from captured stats
    // v2.12.0 おまかせの実測値(表示用)。「判断が曖昧で雑」への答えは、判断の
    // 根拠を数字で見せること。brightは声の標準傾斜との差(+=明るい)、sibはサ行の割合。
    float getAutoBrightDb() const noexcept { return asBrightDb.load(); }
    float getAutoSibPct()   const noexcept { return asSibPct.load(); }

    //== KEY / SCALE analysis (v1.4.0 advanced): 8 s chroma capture -> K-S key ==
    void requestKeyScan (double seconds = 8.0) noexcept
    {
        for (auto& c : chromaSum) c.store (0.0);
        keyCaptureWrite.store (0);
        keyCaptureReady.store (false);
        keyAnalyzed = false;
        keyScanCountdown.store ((int) (seconds * currentSampleRate));
        keyScanTotal.store ((int) (seconds * currentSampleRate));
    }
    bool  isKeyScanRunning() const noexcept { return keyScanCountdown.load() > 0; }
    float getKeyScanProgress() const noexcept
    {
        const int t = keyScanTotal.load(), l = keyScanCountdown.load();
        return t > 0 ? juce::jlimit (0.0f, 1.0f, 1.0f - (float) l / (float) t) : 0.0f;
    }
    // Reads the captured chroma, runs Krumhansl-Schmuckler, returns tonic (0-11),
    // isMinor, confidence (0-1). Returns false if not enough data. NOT const: on the
    // first call after a scan it analyses the captured audio (see finalizeKeyScanIfReady).
    bool getKeyResult (int& tonic, bool& isMinor, float& confidence);
    void copyChroma (float out[12]) const { for (int i = 0; i < 12; ++i) out[i] = (float) chromaSum[i].load(); }

    //== v2.1.0 MIDIスイッチ: フットスイッチ/パッドのNote・CCで操作を切り替える ==
    // 割当はUIの「MIDI設定」から。判定はチャンネル不問(表示用に記憶だけする)。
    // 実際の切替は必ずメッセージスレッドで行う(AsyncUpdater経由)ので、
    // オーディオスレッドではフラグを立てるだけ = リアルタイム安全。
    static constexpr int kMidiSlots = 8;
    enum MidiAction { maNone = 0, maAB, maRevOn, maRevType, maJnOn, maJnSolo,
                      maJnHarm, maAtOn, maVcOn, maDlyOn, maKeyUp, maKeyDown, maCount };
    struct MidiMap
    {
        std::atomic<int> act  { 0 };    // MidiAction
        std::atomic<int> type { 0 };    // 0=未割当 1=Note 2=CC
        std::atomic<int> num  { -1 };   // Note/CC番号
        std::atomic<int> ch   { 0 };    // 学習時のチャンネル(表示用)
    };
    MidiMap midiMap[kMidiSlots];
    std::atomic<int> midiLearnArmed { -1 };  // 学習待ちスロット(-1=なし)
    std::atomic<int> midiUiDirty    { 0 };   // 割当変更をUIが拾うためのカウンタ
    void clearMidiSlot (int slot)
    {
        if (slot < 0 || slot >= kMidiSlots) return;
        midiMap[slot].type.store (0); midiMap[slot].num.store (-1);
        midiUiDirty.fetch_add (1); markStateDirtyPublic();
    }

    //== v2.1.0 A/B: プロセッサ所有へ移管(MIDIから、エディタ無しでも切替可能) ==
    int  getAbCurrent() const noexcept { return abCurrent.load(); }
    std::atomic<int> abUiDirty { 0 };        // 切替/コピーをUIが拾うためのカウンタ
    void abSwitch (int target);              // メッセージスレッドから呼ぶこと
    void abCopyToOther();                    // 今の音を反対スロットへ保存

    void markStateDirtyPublic() noexcept { markStateDirty(); }

    //== v2.2.0 配信出力: 単体起動版で「もう1つの出力先」へ同じ音を流す ==
    // ヘッドホン(ASIO)で自分の声を聴きながら、VB-CABLE 等の仮想デバイス経由で
    // OBS へ送るための機能。プラグイン版はホストが配線するので使わない。
    bool isStandalone() const noexcept { return wrapperType == wrapperType_Standalone; }
    gz::StreamOut& getStreamOut() noexcept { return streamOut; }
    juce::String getStreamDeviceWanted() const { return streamDevWanted; }
    void setStreamOutput (bool on, const juce::String& deviceName)
    {
        streamDevWanted = deviceName;
        streamWanted    = on;
        if (on) streamOut.start (deviceName, currentSampleRate);
        else    streamOut.stop();
        markStateDirty();
    }


    //== v2.10.0 点検 =========================================================
    // ★ここから下はどれも音を作らない。音声スレッドからは呼ばない。

    // --- #76 名前ごとの設定: ホストが教えてくれるトラック名 ---
    // 「トラック名が不明なら空」と JUCE が明記しているので、空のときは
    // 画面側でユーザーが打った名前を使う。だから「曲ごと」ではなく「名前ごと」。
    void updateTrackProperties (const TrackProperties& p) override
    {
        const juce::ScopedLock sl (nameLock);
        hostTrackName = p.name.has_value() ? *p.name : juce::String();
    }
    juce::String getHostTrackName() const
    {
        const juce::ScopedLock sl (nameLock);
        return hostTrackName;
    }

    // --- #73 ゼロ遅延の自己証明 ---
    // 「測る」を押すと、約1秒だけ**出力を消して**インパルスを流し、
    // 出てきた場所から実測の遅延を出す。生の音に混ぜたら当然聞こえてしまうので、
    // 測っている間は無音にする。終われば自動で元に戻る。
    void requestLatencySelfTest() noexcept
    {
        selfTestMeasured.store (-1);
        selfTestPos.store (0);
        selfTestRunning.store (true);
    }
    bool isSelfTestRunning() const noexcept { return selfTestRunning.load(); }
    int  getSelfTestMeasured() const noexcept { return selfTestMeasured.load(); }  // -1 = まだ

    // --- #75 使われ方（送信しない） ---
    gz::checkup::Usage& usage() noexcept { return usageLog; }

    // --- #74 報告セットの中身を組み立てる（書き出しは画面側） ---
    juce::String buildReportText() const;

    //== v2.9.0 セッションモード: 画面が「いま何サンプル足しているか」を出すための窓口 ==
    // 申告値(ホストに伝えた値)をそのまま返す。0 なら追加遅延ゼロ。
    int  addedLatencySamples() const noexcept { return reportedLatency.load(); }
    bool isSessionActive()     const noexcept { return sessionActive.load(); }

    //== v3.1「使いかた4種」: 使いかたごとにツマミの値を別に覚える =================
    //  設計書§3「使いかたを変えてもツマミの値は消えない（使いかたごとに別保存）」。
    //
    //  ★出入口はこの1本だけにしてある。画面のコンボからも、500ms のタイマーからも
    //   同じものを呼ぶ。タイマーだけだと最大0.5秒遅れて値が飛ぶので画面から即呼び、
    //   画面が無いとき（ホストのオートメーションで使いかたが動いたとき）は
    //   タイマーが拾う。二重に呼ばれても、使いかたが変わっていなければ何もしない。
    //
    //  ★useModeArmed は「状態を読み込んだ直後の1回だけ、覚え直しをしない」ための鍵。
    //   これが無いと、プロジェクトを開いた瞬間に
    //   「読み込む前の値」で「読み込んだばかりの記録」を上書きしてしまう。
    void applyUseModeMemoryIfChanged();
    void snapshotUseMode (int mode);
    bool restoreUseMode  (int mode);
    // 使いかたごとに覚えない物（曲そのものの設定・互換スイッチ・その場のスイッチ）
    static bool useModeMemorySkips (const juce::String& id) noexcept
    {
        return id == "src_mode"       // 選ぶ本人
            || id == "session"        // セッションモード（その場で切るスイッチ）
            || id == "lift_legacy"    // 古い曲を昔の音で開く互換スイッチ（絶対に自動で動かさない）
            || id == "ord_deess" || id == "ord_eq" || id == "ord_space"  // 順番の分岐（構成の選択）
            || id == "refpitch"       // 基準ピッチ（曲の設定）
            || id == "bpm";           // テンポ（曲の設定）
    }

private:
    using Filter       = juce::dsp::IIR::Filter<float>;
    using Coefficients = juce::dsp::IIR::Coefficients<float>;
    // v2.8.0 ★音声コールバックの中でメモリ確保をしないための型。
    // juce の `Coefficients::makeXxx()` は中身が `return *new Coefficients(...)` で、
    // 呼ぶたびにヒープを触る。フィルタ係数の作り直しは1ブロックに11回＋
    // ディエッサー/スマートEQで32サンプルごとにも走るので、64サンプルの
    // バッファだと 1.3ms ごとに数十回の malloc/free になっていた。
    // これは「配信中つけっぱなし」を売りにしている製品としては致命的で、
    // アロケータの取り合いでコールバックが間に合わずプツッと切れる原因になる。
    // `ArrayCoefficients` は std::array を値で返すだけ（ヒープを触らない）。
    // 既存の Coefficients へ `operator=` で入れれば、2回目以降は確保ゼロ。
    using ACoefs = juce::dsp::IIR::ArrayCoefficients<float>;
    using StereoFilter = juce::dsp::ProcessorDuplicator<Filter, Coefficients>;

    StereoFilter hpf, mud, harsh, presence, air;
    // v3.1「弾き語り」用の2点目のこもり。mud2On が false のときは1回も通さない
    // （通さない＝ビット単位で今までと同じ。0dBのフィルタでも丸め誤差は出るため）。
    StereoFilter mud2;
    bool         mud2On = false;
    juce::dsp::Compressor<float> comp1, comp2;
    juce::dsp::Gain<float>       makeup;
    juce::dsp::Reverb            reverb;
    // v4.0.0 へや: 物理で作った6部屋の畳み込み残響（追加遅延0サンプル）。
    // rev_type が 7 以上のときは reverb ではなくこちらを通す。
    // prepare だけが確保する。process は音声スレッドから呼んでよい。
    heya::Convolver              heyaRev;

    // De-esser: side-chain band-pass detector + high-shelf gain reduction
    StereoFilter deessDetectHP;                 // detector isolate (~5 kHz+)
    float dsEnv = 0.0f, dsGain = 1.0f;
    float dsEnvAtk = 0.0f, dsEnvRel = 0.0f;
    juce::dsp::IIR::Filter<float> dsShelfL, dsShelfR;   // applied reduction shelf
    float dsCurrentReduction = 0.0f;            // smoothed dB of shelf cut

    // ---- v3.1「弾き語り」用の2点目（フレット/ピックの音）----
    //  1点目は「5.2kHz より上」を高域通過で見て 6.5kHz の**棚**を下げる。
    //  2点目は 3kHz を**帯域通過**で見て 3.5kHz の**山**だけ下げる。
    //   ・帯域通過にする理由: 高域通過だと 5.2kHz のサ行まで一緒に拾ってしまい、
    //     「さ」を言うたびに 3.5kHz まで凹んで、声がこもる。
    //   ・棚ではなく山にする理由: 3.5kHz から上を全部下げると、声の抜け(ぬけ)
    //     まで暗くなる。フレットの音がいる所だけ狭く下げる。
    //   ・下げ量は1点目の半分まで。フレット音はサ行ほど耳につかない。
    //  dsDetect2Hz == 0（うた/しゃべり/アコギだけ）のときは 1回も通さない。
    StereoFilter deessDetectBP2;
    juce::dsp::IIR::Filter<float> dsDipL, dsDipR;
    float dsEnv2 = 0.0f, dsCurrentReduction2 = 0.0f;
    float dsDetect2Hz = 0.0f;                   // 0 = 2点目を使わない
    float dsDipHzNow = 3500.0f, dsDipQNow = 1.2f;
    juce::AudioBuffer<float> scratch2;          // 2点目の側鎖(prepareToPlay で確保)

    // ---- v1.9.5 艶 (Ring): 歌手のフォルマント帯を母音のときだけ持ち上げる ----
    juce::dsp::IIR::Filter<float> ringL, ringR;     // 実際にかけるピーキング
    juce::dsp::IIR::Filter<float> ringDet, sibDet;  // 側鎖(モノ): 3kHz帯 と 7.5kHz帯
    float ringEnv = 0.0f, sibEnv = 0.0f;
    float ringGainDb = 0.0f, ringApplied = -99.0f;

    // ---- Smart Dynamic EQ (zero-latency IIR: auto resonance suppression + manual) ----
    static constexpr int seqBands = 6;
    juce::dsp::IIR::Filter<float> seqPeakL[seqBands], seqPeakR[seqBands];  // applied dynamic peaks
    juce::dsp::IIR::Filter<float> seqDet[seqBands];                        // mono side-chain band-pass
    float seqEnv[seqBands]   = {};      // per-band detector envelope (linear)
    float seqCut[seqBands]   = {};      // smoothed current cut (dB, >= 0)
    float seqTarget[seqBands]= {};      // target cut recomputed periodically (dB)
    float seqApplied[seqBands]= {};     // last cut baked into coeffs (dB) -> skip idle rebuilds
    float seqAppliedQ[seqBands]= {};    // last Q baked into coeffs (F6-style per-band Q)
    float seqFreqHz[seqBands]= { 315.f, 630.f, 1250.f, 2500.f, 5000.f, 8000.f };
    float seqEnvAtk = 0.0f, seqEnvRel = 0.0f;   // detector envelope coeffs
    void  processSmartEQ (juce::AudioBuffer<float>&);
    std::atomic<float> meterSEQ { 0 };

    juce::AudioBuffer<float> dryBuffer, scratch;
    // v3.0 モジュールのON/OFF。processBlock の各区間を save/restore で挟む。
    gz::ModuleChain mods;
public:
    const gz::ModuleChain& moduleChain() const noexcept { return mods; }   // 画面から状態を読む

    // v3.0-c 順番の分岐（案C）。呼ぶ場所を2択にするために関数へ出した2つ。
    //  中身は切り出す前と1行も変えていない（既定の音は bit 一致）。
    void applyTotonoe (juce::AudioBuffer<float>& buffer);
    void applyDeEsser (juce::AudioBuffer<float>& buffer);

    // v3.0-c 分岐3「ひろがりをキャラ声の前へ」用。
    //  キャラ声は「音を変える所（ロボ声・メガホン）」と「測る所（エモ・サビリフト）」の
    //  2つでできていて、**ひろがりは測った結果だけを使う**。
    //  なので順番を入れ替えるときは、測る所だけ先に済ませればよい。
    //   doFx     = ロボ声・メガホンを掛ける（save/restore もこちら）
    //   doDetect = エモ・サビリフトを測る（音には触らない）
    //  既定は applyChara (buffer, true, true) ＝ 切り出す前と1行も変わらない。
    void applyChara (juce::AudioBuffer<float>& buffer, bool doFx, bool doDetect);

    // v3.0-c「くらべる」。押している間だけ 8つとも素通し。
    //  パラメータではないので**保存されないし、ホストにも出ない**。
    //  離し忘れ・エディタが閉じたときは画面側が必ず false に戻す。
    std::atomic<bool> compareBypass { false };
    // v3.1 §4「1つずつ」画面の くらべる。押している間だけ**その1枚だけ**素通し。
    //  -1 = 何も止めていない。compareBypass と同じくパラメータではないので、
    //  保存もオートメーションも汚さない。離し忘れは画面側が必ず -1 に戻す。
    std::atomic<int>  compareOne { -1 };
private:

    // ---- v2.8.0: Mixツマミ用「遅らせた原音」 ----
    // ボイス変換/ピッチ補正/ハモリのどれかがONだと、加工側は約16ms後ろにずれる。
    // その状態で遅れていない原音を混ぜると 16ms のコムフィルタになり、
    // Mixを中間にしたときだけ音がスカスカになっていた。原音側も同じだけ
    // 遅らせてから混ぜる。リングバッファは prepareToPlay で確保する。
    juce::AudioBuffer<float> dryRing;            // 原音の遅延リング
    juce::AudioBuffer<float> dryAligned;         // 取り出し先(1ブロック分)
    int dryRingW = 0;
    double currentSampleRate = 44100.0;

    // Noise gate
    float gateEnv = 0.0f, gateGain = 1.0f;
    float gateEnvAtk = 0, gateEnvRel = 0, gateOpenCoef = 0, gateCloseCoef = 0;

    // ---- De-noise: 4-band Linkwitz-Riley split + per-band downward expander ----
    // Crossovers at 250 / 1200 / 5000 Hz. Zero latency (IIR).
    juce::dsp::LinkwitzRileyFilter<float> lrLP1, lrHP1, lrLP2, lrHP2, lrLP3, lrHP3;
    // 後段で分割しない帯域にも同じ位相回転を与え、足し戻すときの打ち消しを防ぐ。
    juce::dsp::LinkwitzRileyFilter<float> dnPhaseLow2, dnPhaseLow3, dnPhaseMid3;
    // 全体の原音混合にも、分割→再合成と同じ位相を通す。
    juce::dsp::LinkwitzRileyFilter<float> dnDryPhase1, dnDryPhase2, dnDryPhase3;
    juce::AudioBuffer<float> dnDryPhaseBuffer;
    juce::AudioBuffer<float> bandBuf[4];

    // ---- v1.8.0 voice changer (formant-preserving pitch shift) + 5-voice unison ----
    gz::VoiceShifter vcSh[2];                 // per-channel voice changer
    gz::VoiceShifter unSh[4];                 // 4 extra "members" for the unison
    std::vector<float> vcMono, vcTmp;         // scratch (sized in prepareToPlay)
    std::vector<float> vcDry[2];              // v2.6.0: 安全弁用の加工前コピー
    float  unDelay[4][4096] = {};             // per-voice ensemble timing delays
    int    unDelayW[4] = {};                  // write heads
    int    unDelaySmp[4] = {};                // delay lengths in samples
    // v2.6.0: 実行中はホストへ遅延変更を通知しない(Cubase が固まる/音が止まる)。
    // 申告は prepareToPlay の一度だけ。ここは内部の記録用。
    int    voiceLatency = 0;

    // ---- v1.9.0 auto-tune (pitch correction): reuses vcSh[] for the actual shift ----
    gz::PitchDetector  pitchDet;              // YIN F0 detector (runs on the audio thread)
    gz::PitchCorrection pitchCorrection;
    bool pitchWasTracking = false;
    float atCorrection = 0.0f;                // smoothed applied correction (semitones)
    // v1.9.0: シフターを前フレームで動かしていたか。OFF→ON の瞬間に古い内部状態を
    //         捨てないと、前回の音が 1 窓ぶん爆音で漏れる。
    bool  vcWasActive = false, jnWasActive = false;
    float atWetMix = 0.0f;
   #if VOCALGZZIO_TRIAL
    int   trialCounter = 0;                   // v1.9.4: 体験版のディップ用
   #endif
    // v2.9.0: 低遅延モード(lowLatActive)は廃止。窓はつねに1024点。
    // 画面が「いま何サンプル足しているか」を出せるように、申告値をここに置く。
    // 音声スレッドからは書かない(prepareToPlay でだけ更新)。
    std::atomic<int>  reportedLatency { 0 };
    std::atomic<bool> sessionActive   { false };
    float jnLastPitch = 60.0f; int jnLastDir = 1;   // v1.9.8: ハモリの自動反転用
    gz::scale::ContraryLine jnContra;         // v2.0.0: 反行ハモリの対旋律
    float jnHeldSemi[2] = { 0.0f, 0.0f };     // v2.0.0: 無声区間はハモリ音程を保持
    int   jnLastHarm = -1;                    //         (モードが変わったら保持を破棄)
    std::atomic<float> atDetectedHz { 0.0f };        // last detected F0 (UI; 0 = unvoiced)
    std::atomic<float> atCurrentCorrection { 0.0f }; // applied correction in semitones (UI)
    float dnEnv[4]   = {};          // per-band envelope
    // v4.3.1 ★「学習した床」として信用できる下限（採用時と復元時の両方で使う）。
    //  これより静かな床は、実在の部屋の音ではなく「入力が無かった」ことを意味する。
    //  床が低すぎると openThr = 床×2.5 が本物のノイズより下に来るため、
    //  エキスパンダーが一度も閉じず、ノイズ除去が完全に無処理になる。
    //  実測: 床 1e-5 を復元＋学びなおしOFF で削れ 0.00dB（tools/dsp_dnrestore）。
    //  -93dBFS は、実在するどんな静かな部屋・機材の床よりも低い。
    static constexpr float kDenoiseMinLearnedFloorDb = -93.0f;
    float dnFloor[4] = { 1e-5f, 1e-5f, 1e-5f, 1e-5f };   // estimated noise floor
    float dnFloorLearn[4] = {};     // capture during learn (peak; v2.10.0 は判定用)
    float dnGain[4]  = { 1, 1, 1, 1 };
    float dnEnvAtk = 0, dnEnvRel = 0, dnOpenCoef = 0, dnCloseCoef = 0;
    gz::DenoiseClassifier dnClassifier;
    float dnInitialFollowCoef = 0.0f;
    int dnLearnPeriodicFrames = 0;
    int dnLearnStationaryFrames = 0;
    // v3.0 自動学びなおし(dn_relearn)と、床の這い上がり対策
    float dnRelearnCoef = 0.0f;     // 学びなおしの速さ (τ≈2秒, サンプル毎)
    int   dnShareDecim = 0;         // dnFloorShared へ書く間引き
    int   dnFallRun[4] = {};        // 包絡が自由落下している連続サンプル数
    int   dnFallMax = 0;            // 30ms（これを超えたら学びなおし停止）
    // v3.1 「喋りはじめのさ行」検出用: 帯域3のゆっくり平均（τ≈0.15s）
    float dnEnvSlow3 = 0.0f;
    float dnEnvSlowCoef = 0.0f;
    // v3.1 「喋り出した瞬間にノイズがバッと出る」対策。
    //  声リンクで開けるだけのバンド（自分では音を持っていないバンド）を
    //  そっと開けるための、ゆっくりした立ち上がり係数。
    float dnSoftOpen = 0.0f;
    // v3.1 母音中の高域抑えは「下げるのは速く・戻すのはゆっくり」
    float dnHfDuckCoef = 0.0f;      // τ≈0.08秒（下げる向きだけ）
    bool  dnLearned = false;
    // ---- v2.12.0 サフサフ対策(§6-1): 声が出たら全帯域を一斉に開けて保持 ----
    int   dnVoiceHold = 0, dnHoldSamples = 0;
    int   dnHfVoiceHold = 0;       // 高域そのものが立ち上がった場合だけ子音を保護
    float dnFastOpen = 0.0f;
    // ---- v2.12.0 張り保護(§6-2): いつもの声量 vs いまの声量 ----
    float beltFastDb = -60.0f, beltSlowDb = -60.0f, beltNow = 0.0f;
    bool  beltPrimed = false;   // v3.0 基準を最初の1回だけ「いまの声量」に合わせたか
    // ---- v3.0「つぶさない」（音量が上がっても潰れないモード）----
    // beltNow(張り具合 0..1)を少しなめらかにしたもの。0=いつもの声量、1=張っている。
    // これを「圧縮のしきい値」「あたたかみ」「のび」「音量キープ」へ配って、
    // **大きく歌ったときだけ**つぶす量を引く。ツマミは増やさない。
    float crushGuard = 0.0f;
    // 出口の自動ヘッドルーム。つぶさないと当然ピークは伸びるので、0dBFSに当たる
    // 手前で**ゆっくり音量を下げる**（速く下げるとそれ自体がつぶし＝リミッタになる）。
    float crushHeadDb = 0.0f, crushHeadAtk = 0.0f, crushHeadRel = 0.0f;
    std::atomic<float> crushMeterDb { 0.0f };   // 画面に「いま何dB守っているか」を出す
public:
    // 画面用: いま「つぶさない」が守っている量(dB)と、張り具合(0..1)
    float getCrushGuardDb() const noexcept { return crushMeterDb.load(); }
    // v3.1「ピックおさえ」のはたらき量（0..1）。画面のミニメーター用。
    float getPickWork() const noexcept { return pickMeter.load(); }
    float getBeltAmount()   const noexcept { return beltNow; }
private:
    float dsBroadEnv = 0.0f, dsBbAtk = 0.0f, dsBbRel = 0.0f;   // サ行おさえ用の全体包絡
    std::atomic<int> learnCountdown { 0 };
    std::atomic<int> dnLearnCommand { 0 };  // 1=学習開始、-1=解除。集計は音声側が所有

    // ---- v2.10.0 ★学習の中身を見て、汚れていたら採用しない ----
    //  これまでは 1.5 秒の**最大値**をそのまま床にしていた。声・息・イスの音が
    //  一瞬でも入れば、そこが床になって「+8dB下」までごっそり削る設定が
    //  でき上がり、しかも二度と自動に戻らないまま保存されていた。
    //  （実測: 歌の最中に押すと 低域 -10.5dB / 空気 +2.7dB / 山谷 +4.0dB。
    //    これが「痩せた・シャリつく・小さい・上げると潰れる」の正体。）
    //  対策は 3 つ:
    //    1. 最大値ではなく**中央値**を床にする（一瞬の物音に引きずられない）
    //    2. 部屋のノイズとして有り得ない大きさなら**採用しない**
    //    3. 山と谷が開いていたら（＝静かではなかった）**採用しない**
    static constexpr int dnHistBins = 121;          // -120..0 dB を 1dB 刻み
    int   dnHist[4][dnHistBins] = {};
    int   dnHistCount = 0;                          // 貯めた個数(帯域共通)
    int   dnHistDecim = 0;                          // 16サンプルに1回だけ数える
    // 判定の結果を画面へ渡す。0=なし 1=採用 2=音が大きすぎた 3=静かではなかった
    std::atomic<int>   dnLearnResult { 0 };
    std::atomic<float> dnLearnLevelDb { -120.0f };  // 測った部屋の大きさ(中央値)
    std::atomic<float> dnLearnSpreadDb { 0.0f };    // 山と谷の開き
    // v1.5.0: the learned profile is part of the plugin state now. The audio thread
    // owns dnFloor/dnLearned; these atomics mirror them for save (read on the message
    // thread) and restore (written on the message thread, picked up in processBlock).
    std::atomic<float> dnFloorShared[4] { 1e-5f, 1e-5f, 1e-5f, 1e-5f };
    std::atomic<bool>  dnLearnedShared  { false };
    std::atomic<int>   dnProfileValidation { 0 }; // 1=周期性と安定性を確認した学習
    std::atomic<bool>  dnProfilePending { false };

    // ---- Sustain (のび): tail lift + even-harmonic generation ----
    float susEnv = 0.0f;
    float susEnvAtk = 0, susEnvRel = 0;
    float susLift = 0.0f;           // smoothed lift gain (dB)

    // ---- v2.3.0 ポップ(破裂音) / リップ(口の粘着音) 除去 ----
    // どちらも「平行して走らせたフィルタへ、必要な瞬間だけ滑らかに寄せる」方式。
    // 係数を作り直さないのでサンプル単位で追従でき、遅延も増えない。
    //   ポップ: 検出したら急峻なハイパス(190Hz/24dB/oct)へ寄せる
    //   リップ: 検出したらローパス(2.2kHz/24dB/oct)へ寄せる(パチッだけ消す)
    juce::dsp::IIR::Filter<float> popHp[2][2];   // [ch][段]
    juce::dsp::IIR::Filter<float> lipLp[2][2];
    juce::dsp::IIR::Filter<float> popDet, lipDet, popMidDet, lipBodyDet;   // 側鎖(モノ)
    float popLfFast = 0.0f, popLfSlow = 0.0f, popMid = 0.0f, popG = 0.0f;
    float lipFast = 0.0f, lipSlow = 0.0f, lipBody = 0.0f, lipG = 0.0f;
    float popLfFastA = 0, popLfFastR = 0, popLfSlowA = 0, popLfSlowR = 0;
    float popMidA = 0, popMidR = 0, popGA = 0, popGR = 0;
    float lipFastA = 0, lipFastR = 0, lipSlowA = 0, lipSlowR = 0;
    float lipBodyA = 0, lipBodyR = 0, lipGA = 0, lipGR = 0;
    std::atomic<float> meterPop { 0 }, meterLip { 0 };   // UI表示用 0..1

    // ---- v2.4.0 なめらか(動的レゾナンス抑制) ----
    // 900Hz〜9kHzの24バンドで「近傍より出っ張った帯域」だけを、出た瞬間だけ削る。
    // ゼロ遅延(オールパス並列ノッチ、係数の作り直しなし)。詳細は Resonance.h。
    gz::res::Tamer resTamer;
    // v2.10.0 音源モードの反映用。updateParameters(毎ブロック)で書き、
    // 同じブロックの中でだけ読む。スレッドをまたがないので atomic は要らない。
    int   resModeNow  = -1;        // いま組んである音源モード(-1 = まだ組んでいない)
    float dsShelfHzNow = 6500.0f;  // ディエッサーが下げる棚の位置
    bool  srcVoiceOnly = true;     // ことば・艶を使うか
    bool  srcBreathOk  = true;     // 息を使うか
    bool  srcSpaceOk   = true;     // ひびき・やまびこを使うか
    // v3.1 ピッチ系(ピッチ補正・ボイス変換)を使うか。voiceOnly から切り離した。
    //  「弾き語り」は声はあるので ことば・艶 は要るが、和音が同時に鳴っているので
    //  単音前提のピッチ検出は誤動作する。＝ voiceOnly=true / pitchOk=false。
    // ---- v3.1「ピックおさえ」(アコギだけ) ----
    //  ゼロ遅延の過渡整え。速い包絡(0.5ms)と遅い包絡(30ms)の比を見て、
    //  「いつもより飛び出した頭」の間だけゲインを下げる。先読みはしないので
    //  頭の最初の数百usはすり抜ける（そこは意図どおり＝弾いた感じは残す）。
    //  0% では 1回も通さない ＝ 今までとビット単位で同じ。
    float pickFast = 0.0f, pickSlow = 0.0f, pickGain = 1.0f;
    float pickFastAtk = 0.0f, pickFastRel = 0.0f;
    float pickSlowAtk = 0.0f, pickSlowRel = 0.0f;
    float pickDownCoef = 0.0f, pickUpCoef = 0.0f;
    //  v3.1 ★「アタックそろえ」に要る、ゆっくりした基準。
    //   これが無いと、下げ量が「頭かどうか」の比だけで決まるので、
    //   強く弾いた頭も軽く弾いた頭も**同じだけ**下がり、開きが縮まらない
    //   （実測: 12.04dB → 12.04dB。1ミリも縮まらなかった）。
    //   最近の頭の大きさを覚えておいて、そこからどれだけ飛び出したかで深さを変える。
    //   上がりは 0.8秒・下がりは 4秒（強い一発で基準が跳ね上がらないように）。
    float pickRef = 0.0f;
    bool  pickRefPrimed = false;
    float pickRefUp = 0.0f, pickRefDn = 0.0f;
    bool  srcPickOk = false;
    std::atomic<float> pickMeter { 0.0f };   // 画面のはたらき量

    // v3.1 使いかたごとの記憶
    int   useModeLast  = -1;       // 直前の使いかた（-1 = まだ見ていない）
    bool  useModeArmed = false;    // 状態を読み込んだ直後は false（1回だけ空振りさせる）

    bool  srcPitchOk   = true;
    float srcDnScale   = 1.0f;     // v3.1 使いかたごとのノイズ除去の効き
    // v2.10.0 距離ならし。入力の直後で動かす。画面へ出す補正量は atomic で渡す。
    gz::prox::Evener  proxEvener;
    std::atomic<float> proxCorrDb { 0.0f };

    // v2.10.0 点検の内部状態
    juce::CriticalSection nameLock;
    juce::String  hostTrackName;
    gz::checkup::Usage usageLog;
    std::atomic<bool> selfTestRunning { false };
    std::atomic<int>  selfTestPos     { 0 };
    std::atomic<int>  selfTestMeasured { -1 };
    int   selfTestImpactAt = -1;      // インパルスを入れた絶対位置
    float selfTestBest = 0.0f;        // これまでの最大振幅
    int   selfTestBestAt = -1;
    std::atomic<float> meterRes { 0 };                   // 直近の最大カット量(dB)

    // ---- v2.6.0 ジー音(電源ハム)の自動除去 / ことば(子音エンハンサー) ----
    // どちらもゼロ遅延。ハムは「同じ波を作って引き算」、子音は Regalia–Mitra の
    // ピークフィルタをサンプル単位で動かすだけ。詳細は HumKiller.h / Consonant.h。
    gz::hum::Killer   humKill;
    gz::cons::Enhancer consEnh;
    std::atomic<int>   meterHumHz { 0 };                 // 0=未検出 / 50 / 60
    std::atomic<float> meterHumDb { -120.0f };
    std::atomic<float> meterCons  { 0 };

    // ---- v2.7.0 こぶし(しゃくり・こぶし保護) ----
    // 音は触らない。ピッチ補正の「効かせる量」に掛ける係数を作るだけ＝ゼロ遅延。
    gz::orn::Guard      ornGuard;
    std::atomic<float>  meterOrn { 0 };
    std::atomic<int>    meterOrnKind { 0 };

    // ---- v2.4.0 マイク音量(入力トリム) & 音量キープ(自動ゲインライド) ----
    float inGainNow = 1.0f, inGainA = 0.0f;              // なめした入力ゲイン
    float rideEnv2 = 0.0f, rideGDb = 0.0f;               // ラウドネス^2 / 現在のゲイン(dB)
    float rideRmsA = 0.0f, rideSlewA = 0.0f;
    std::atomic<float> meterRide { 0 };                  // UI表示用(現在のゲインdB)

    // ---- v2.0.0 エモート(ポップス/バラード用の聴かせ系3機能) ----
    // 息(Breath): 小さい声のときだけ4.5kHz以上を持ち上げる「逆ディエッサー」。
    juce::dsp::IIR::Filter<float> brShelfL, brShelfR;
    float brEnv = 0.0f, brEnvAtk = 0.0f, brEnvRel = 0.0f;
    float brGainDb = 0.0f, brApplied = -99.0f;
    // エモ(Emo Bloom): ロングトーンを検出して、響きと広がりをふわっと開く。
    float emoHoldSec = 0.0f, emoBloom = 0.0f;
    // サビリフト(Chorus Lift): 直近の歌の強さ(速い平均)が曲全体(遅い平均)を
    // 上回る=サビと判定し、広がり・かさね・響きを自動で持ち上げる。
    float liftFastDb = -60.0f, liftSlowDb = -60.0f, liftVal = 0.0f;
    std::atomic<float> liftUI { 0.0f };   // UI表示用 0..1
    // v3.0-c ひろがりが読む「このブロックの検出結果」。
    //  以前は processBlock のローカル変数だったが、キャラ声を関数へ出したので
    //  受け渡し用にここへ置いた。毎ブロック必ず書き直される（持ち越さない）。
    float liftNowV = 0.0f, emoBloomNowV = 0.0f;

    // Doubler / width (mono-safe)
    std::vector<float> dblBuf;
    int dblWrite = 0;
    float dblLfoPhase = 0.0f;

    // Delay (simple echo, zero latency feed)
    std::vector<float> dlyBufL, dlyBufR;
    int dlyWrite = 0;
    float dlyLpL = 0.0f, dlyLpR = 0.0f;         // one-pole highcut in the feedback path
    float dlyTimeSm = 0.34f;                    // smoothed delay time in seconds (avoid zipper on tempo change)

    // ---- v1.4.0: character FX (ring-mod robot voice + megaphone) ----
    StereoFilter megaHP, megaLP, megaPeak;
    float roboPhase = 0.0f;

    // ---- v1.4.0: chorus (wet-only modulated voices, zero latency reported) ----
    juce::dsp::Chorus<float> chorus;

    // ---- v1.4.0: reverb wet path (predelay + tone filter + ducking) ----
    // ---- v1.6.0: spring drip comb + shimmer (+1 oct in the feedback loop) ----
    std::vector<float> springBufL, springBufR;      // ~33 ms drip comb per channel
    int   springW = 0;
    float springLpL = 0.0f, springLpR = 0.0f;       // one-pole LP inside the comb loop
    std::vector<float> shimBufL, shimBufR;          // pitch-shifter ring (reads at 2x)
    int   shimW = 0;
    float shimPhase = 0.0f;                          // shared grain phase (samples)
    juce::AudioBuffer<float> shimFb;                 // last block's +1 oct wet, fed back
    float shimLpL = 0.0f, shimLpR = 0.0f;           // loop conditioning: LP + HP
    float shimHpL = 0.0f, shimHpR = 0.0f;
    juce::AudioBuffer<float> revWet;
    std::vector<float> preBufL, preBufR;        // predelay ring for the wet signal
    int preWrite = 0;
    StereoFilter revHPF, revLPF;
    void resetReverbState() noexcept;
    float revBypassGain = 0.0f;
    float revBypassStep = 1.0f;
    bool revWasRunning = false;

    // ---- v1.4.0: auto-duck (wet of delay+reverb keyed by the vocal) ----
    std::vector<float> duckGainBuf;
    float duckEnv = 0.0f, duckGain = 1.0f;
    float duckEnvAtk = 0.0f, duckEnvRel = 0.0f, duckAtk = 0.0f, duckRel = 0.0f;

    // ---- v1.4.0: host tempo + loudness ----
    std::atomic<float> hostBpm { 0.0f };
    std::atomic<float> meterRmsDb { -60.0f };
    float rmsAccum = 0.0f;

    // ---- v1.4.0: AUTO SETUP capture (5 s band-energy statistics) ----
    // Bands: 0 rumble<80, 1 body 80-250, 2 mud 250-500, 3 mid 500-2k,
    //        4 presence 2k-5k, 5 sibilance 5k-9k, 6 air 9k+
    static constexpr int asBands = 7;
    std::atomic<int>    autoSetupCountdown { 0 };
    std::atomic<int>    autoSetupResult    { -1 };
    std::atomic<float>  asBrightDb { 0.0f }, asSibPct { 0.0f };   // v2.12.0 実測の根拠
    std::atomic<int>    asMode { 0 };                 // 0 talk, 1 sing
    std::atomic<int>    asTotalLen { 1 };             // capture length in samples
    std::atomic<double> asSumSq { 0.0 };              // total energy -> overall RMS
    std::atomic<double> asBlockDbSum { 0.0 };         // per-block RMS dB stats -> dynamics
    std::atomic<double> asBlockDbSqSum { 0.0 };
    std::atomic<int>    asBlockCount { 0 };
    std::atomic<float>  asMinBlockDb { 0.0f };        // quietest block -> noise floor hint
    std::atomic<double> asBandSum[asBands];
    std::atomic<int64_t> asSampleCount { 0 };
    std::atomic<float>  asPeak { 0.0f };
    StereoFilter asBP[asBands];                  // analysis band-pass bank (side chain, no audio effect)
    juce::AudioBuffer<float> asScratch;
    bool asPrepared = false;

    // ---- v1.4.0 KEY/SCALE chroma capture ----
    std::atomic<int>    keyScanCountdown { 0 };
    std::atomic<int>    keyScanTotal     { 0 };
    std::atomic<double> chromaSum[12];
    // v1.4.0 P5: the audio thread only CAPTURES raw voice here (a cheap copy); the
    // heavy autocorrelation + chroma binning runs once on the message thread when the
    // scan ends (finalizeKeyScanIfReady). Previously the autocorrelation ran inside
    // processBlock and its ~1.3M-op burst overran small buffers -> audio crackle.
    std::vector<float>  keyCaptureBuf;             // raw mono capture (sized in prepareToPlay)
    std::atomic<int>    keyCaptureWrite { 0 };     // samples written by the audio thread
    std::atomic<bool>   keyCaptureReady { false }; // set true when the scan window is full
    bool                keyAnalyzed { false };     // message-thread: analysis already done
    void finalizeKeyScanIfReady();                 // message-thread chroma analysis

    // Tuner ring
    static_assert (std::atomic<float>::is_always_lock_free, "Tuner samples must be lock-free");
    std::atomic<float> tunerBuf[tunerSize] {};
    std::atomic<unsigned> tunerSequence { 0 };
    std::atomic<int> tunerPos { 0 };
    // Audio-thread-only selection: never sum opposing stereo waveforms for tuning.
    double tunerChannelEnergy[2] {};
    int tunerInputChannel { 0 };

    // Analyzer ring (post-processing mono mix)
    float            analyzerBuf[analyzerSize] = {};
    std::atomic<int> analyzerPos { 0 };

    // Smart EQ state mirrored for the UI (display only)
    std::atomic<float> seqCutUI [seqBands] = {};
    std::atomic<float> seqFreqUI[seqBands] = {};
    std::atomic<float> seqQBandUI[seqBands] = {};
    std::atomic<float> seqQUI { 1.0f };
    std::atomic<int>   seqBandsUI { seqBands };

    // Meters
    std::atomic<float> meterIn { 0 }, meterOut { 0 }, meterGR { 0 }, meterDS { 0 }, meterDN { 0 };

    void updateParameters();

    // ---- v1.5.0 state persistence: one XML (params + denoise profile) shared by
    //      the host chunk (get/setStateInformation) and the plugin-side autosave.
    //      Autosave: any change marks the state dirty; a message-thread timer writes
    //      the XML ~1.2 s after the last change to <userAppData>/VocalGzzio/autosave.xml.
    //      A fresh instance loads that file, so "last used settings" survive even
    //      when the host never hands back a saved project state.
    std::unique_ptr<juce::XmlElement> makeStateXml();
    void applyStateXml (const juce::XmlElement& xml);
    static juce::File autosaveFile();
    void flushAutosaveNow();
    void markStateDirty() noexcept
    {
        stateDirty.store (true);
        lastDirtyMs.store (juce::Time::getMillisecondCounter());
    }
    std::atomic<bool>         stateDirty  { false };
    std::atomic<juce::uint32> lastDirtyMs { 0 };
    bool restoringState = false;   // message-thread guard: ignore dirty marks mid-restore

    // ---- v2.2.0 配信出力 ----
    gz::StreamOut streamOut;
    juce::String  streamDevWanted;
    bool          streamWanted = false;
    bool          suppressDeviceRestore = false;   // A/B切替中は配信先を触らない
    // v2.8.0: A/Bスロット自体を保存するとき、その中にまたA/Bを入れないための札。
    // 入れてしまうと保存が入れ子に増殖して、プロジェクトが開けなくなる。
    bool          omitAbInState = false;

    // ---- v2.1.0 MIDIスイッチ実装部 ----
    juce::MemoryBlock abSlot[2];                 // A/Bの全状態(メッセージスレッド所有)
    std::atomic<int>  abCurrent { 0 };
    bool midiCcOn[kMidiSlots] = {};              // CCの押下状態(オーディオスレッド専用)
    std::atomic<juce::uint32> midiPending { 0 }; // 実行待ちアクション(スロットbit)

    // v4.0.0 へや: 6部屋ぶんの畳み込みは約9MB・約180ms かかる。
    // 使わない人にまで毎回払わせないよう、**初めて選ばれたときに1回だけ**用意する。
    // 用意が済むまでは従来のタンク式リバーブが鳴る（無音にはならない）。
    std::atomic<bool> heyaReady  { false };
    void processMidiSwitches (juce::MidiBuffer& midi);
    void handleAsyncUpdate() override;           // 切替の実行(メッセージスレッド)

    void timerCallback() override;
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override { if (! restoringState) markStateDirty(); }
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override             { if (! restoringState) markStateDirty(); }
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override      { if (! restoringState) markStateDirty(); }
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override {}
    void valueTreeParentChanged (juce::ValueTree&) override {}

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VocalGzzioProcessor)
};
