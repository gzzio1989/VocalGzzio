#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <atomic>
#include <vector>
#include "VoiceShifter.h"
#include "PitchDetector.h"
#include "StreamOut.h"
#include "Resonance.h"     // v2.4.0 なめらか(動的レゾナンス抑制)
#include "HumKiller.h"     // v2.6.0 ジー音(電源ハム)の自動除去
#include "Consonant.h"     // v2.6.0 ことば(子音エンハンサー)
#include "Ornament.h"      // v2.7.0 こぶし(しゃくり・こぶし保護)

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
    static constexpr int tunerSize = 2048;
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
        learnCountdown.store ((int) (1.5 * currentSampleRate));
        for (auto& f : dnFloorLearn) f = 1e-6f;
    }
    bool isDenoiseLearning() const noexcept { return learnCountdown.load() > 0; }

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

    //== v2.9.0 セッションモード: 画面が「いま何サンプル足しているか」を出すための窓口 ==
    // 申告値(ホストに伝えた値)をそのまま返す。0 なら追加遅延ゼロ。
    int  addedLatencySamples() const noexcept { return reportedLatency.load(); }
    bool isSessionActive()     const noexcept { return sessionActive.load(); }

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
    juce::dsp::Compressor<float> comp1, comp2;
    juce::dsp::Gain<float>       makeup;
    juce::dsp::Reverb            reverb;

    // De-esser: side-chain band-pass detector + high-shelf gain reduction
    StereoFilter deessDetectHP;                 // detector isolate (~5 kHz+)
    float dsEnv = 0.0f, dsGain = 1.0f;
    float dsEnvAtk = 0.0f, dsEnvRel = 0.0f;
    juce::dsp::IIR::Filter<float> dsShelfL, dsShelfR;   // applied reduction shelf
    float dsCurrentReduction = 0.0f;            // smoothed dB of shelf cut

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
    float atPitchCenter = 0.0f;               // v1.9.3: 意図した音の中心(MIDI)                    // 補正が要る間だけボコーダを通すためのミックス
    std::atomic<float> atDetectedHz { 0.0f };        // last detected F0 (UI; 0 = unvoiced)
    std::atomic<float> atCurrentCorrection { 0.0f }; // applied correction in semitones (UI)
    float dnEnv[4]   = {};          // per-band envelope
    float dnFloor[4] = { 1e-5f, 1e-5f, 1e-5f, 1e-5f };   // estimated noise floor
    float dnFloorLearn[4] = {};     // capture during learn
    float dnGain[4]  = { 1, 1, 1, 1 };
    float dnEnvAtk = 0, dnEnvRel = 0, dnOpenCoef = 0, dnCloseCoef = 0;
    float dnFloorRise = 1.0f;       // adaptive: slow upward drift per sample
    bool  dnLearned = false;
    std::atomic<int> learnCountdown { 0 };
    // v1.5.0: the learned profile is part of the plugin state now. The audio thread
    // owns dnFloor/dnLearned; these atomics mirror them for save (read on the message
    // thread) and restore (written on the message thread, picked up in processBlock).
    std::atomic<float> dnFloorShared[4] { 1e-5f, 1e-5f, 1e-5f, 1e-5f };
    std::atomic<bool>  dnLearnedShared  { false };
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
    float            tunerBuf[tunerSize] = {};
    std::atomic<int> tunerPos { 0 };

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
