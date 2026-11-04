#include "TestPaths.h"
#include "PluginProcessor.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

static int gFail = 0;
#define CHECK(cond, ...) do { \
    if (cond) { std::printf ("  PASS: " __VA_ARGS__); std::printf ("\n"); } \
    else      { std::printf ("  FAIL: " __VA_ARGS__); std::printf ("\n"); ++gFail; } } while (0)

static void setParam (VocalGzzioProcessor& p, const char* id, float v)
{
    if (auto* prm = p.apvts.getParameter (id))
        prm->setValueNotifyingHost (p.apvts.getParameterRange (id).convertTo0to1 (v));
}

// 指定周波数の Goertzel 振幅計測
static double measureFreqMag (const float* data, int n, double freqHz, double sr)
{
    if (n <= 0) return 0.0;
    const double omega = juce::MathConstants<double>::twoPi * freqHz / sr;
    const double coeff = 2.0 * std::cos (omega);
    double q1 = 0.0, q2 = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * (double) i / (double) juce::jmax (1, n - 1));
        const double q0 = coeff * q1 - q2 + (double) data[i] * w;
        q2 = q1;
        q1 = q0;
    }
    const double magSq = std::max (0.0, q1 * q1 + q2 * q2 - coeff * q1 * q2);
    return std::sqrt (magSq) / (double) n;
}

// ハモリ声部の人間味デチューン（±15〜16 cents）を含む半音内バンド振幅計測
static double measureNoteBandMag (const float* data, int n, double centerHz, double sr)
{
    static const double kCentOffsets[5] = { -16.0, -8.0, 0.0, +8.0, +16.0 };
    double sumSq = 0.0;
    for (double c : kCentOffsets)
    {
        const double f = centerHz * std::pow (2.0, c / 1200.0);
        const double m = measureFreqMag (data, n, f, sr);
        sumSq += m * m;
    }
    return std::sqrt (sumSq);
}

int main()
{
    std::setvbuf (stdout, nullptr, _IONBF, 0);
    juce::ScopedJuceInitialiser_GUI juceInit;
    const double sr = 48000.0;

    std::printf ("=== v5.5.0 統合機能検査 (dsp_v550_features) ===\n");

    // ------------------------------------------------------------------
    // 1. MultiVoiceHarmonyShifter: 1回共通FFT解析・複数声部合成の正確性
    // ------------------------------------------------------------------
    {
        gz::MultiVoiceHarmonyShifter mh;
        mh.prepare (sr);
        mh.setWindow (10);
        mh.setVoiceParams (0, true,  +4.0f, 0.0f);  // 長3度上 (C4 -> E4 ~329.63Hz)
        mh.setVoiceParams (1, true,  +7.0f, 0.0f);  // 完全5度上 (C4 -> G4 ~392.00Hz)
        mh.setVoiceParams (2, false, -5.0f, 0.0f);  // 休止声部 (0出力)
        mh.setVoiceParams (3, true,  -12.0f, 0.0f); // 1オクターブ下 (C4 -> C3 ~130.81Hz)

        const int total = 24000;
        std::vector<float> inSig ((size_t) total), v0 ((size_t) total), v1 ((size_t) total), v2 ((size_t) total), v3 ((size_t) total);
        for (int i = 0; i < total; ++i)
        {
            const double t = (double) i / sr;
            double s = 0.0;
            for (int h = 1; h <= 5; ++h)
                s += std::sin (juce::MathConstants<double>::twoPi * 261.6256 * (double) h * t) / (double) h;
            inSig[(size_t) i] = (float) (0.25 * s);
        }

        for (int pos = 0; pos < total; pos += 256)
        {
            const int blk = juce::jmin (256, total - pos);
            float* outs[4] = { v0.data() + pos, v1.data() + pos, v2.data() + pos, v3.data() + pos };
            mh.processBlock (inSig.data() + pos, outs, blk);
        }

        const int tailOff = 6000;
        const int tailLen = total - tailOff;
        bool allFinite = true;
        float maxInactive = 0.0f;
        for (int i = 0; i < total; ++i)
        {
            if (! std::isfinite (v0[(size_t) i]) || ! std::isfinite (v1[(size_t) i])
                || ! std::isfinite (v2[(size_t) i]) || ! std::isfinite (v3[(size_t) i]))
                allFinite = false;
            maxInactive = juce::jmax (maxInactive, std::abs (v2[(size_t) i]));
        }

        const double e4OnV0 = measureFreqMag (v0.data() + tailOff, tailLen, 329.63, sr);
        const double c4OnV0 = measureFreqMag (v0.data() + tailOff, tailLen, 261.63, sr);
        const double g4OnV1 = measureFreqMag (v1.data() + tailOff, tailLen, 392.00, sr);
        const double c3OnV3 = measureFreqMag (v3.data() + tailOff, tailLen, 130.81, sr);

        CHECK (allFinite && maxInactive == 0.0f,
               "MultiVoiceHarmonyShifter: 全出力が有限値かつ非稼働声部は完全ゼロ出力 (maxInactive=%.6f)", maxInactive);
        CHECK (e4OnV0 > c4OnV0 * 4.0 && g4OnV1 > 0.01 && c3OnV3 > 0.01,
               "MultiVoiceHarmonyShifter: +4st(E4=%.4f vs C4=%.4f), +7st(G4=%.4f), -12st(C3=%.4f)",
               e4OnV0, c4OnV0, g4OnV1, c3OnV3);
    }

    // ------------------------------------------------------------------
    // 2. AppAudioCapture: ロックフリーFIFO＋異サンプルレート間ドリフト補正リサンプラ
    // ------------------------------------------------------------------
    {
        gz::AppAudioCapture cap;

        const int pushFrames = 4800; // 100ms @ 48kHz
        std::vector<float> inL ((size_t) pushFrames), inR ((size_t) pushFrames);
        for (int i = 0; i < pushFrames; ++i)
        {
            const double t = (double) i / 48000.0;
            inL[(size_t) i] = 0.4f * (float) std::sin (juce::MathConstants<double>::twoPi * 440.0 * t);
            inR[(size_t) i] = 0.4f * (float) std::sin (juce::MathConstants<double>::twoPi * 880.0 * t);
        }
        cap.pushCapturedStereo (inL.data(), inR.data(), pushFrames, 48000.0);

        const int pullFrames = 2400; // ~54.4ms @ 44.1kHz (28msプリバッファ突破後に安定プル)
        std::vector<float> outL ((size_t) pullFrames, 0.0f), outR ((size_t) pullFrames, 0.0f);
        const bool pulled = cap.pullStereo (outL.data(), outR.data(), pullFrames, 44100.0);

        const double mag440L = measureFreqMag (outL.data() + 200, pullFrames - 200, 440.0, 44100.0);
        const double mag880R = measureFreqMag (outR.data() + 200, pullFrames - 200, 880.0, 44100.0);
        CHECK (pulled && mag440L > 0.08 && mag880R > 0.08,
               "AppAudioCapture: 48kHz->44.1kHz ロックフリーリサンプル正常 (L440=%.4f, R880=%.4f)", mag440L, mag880R);
    }

    // ------------------------------------------------------------------
    // 3. 伴奏コード追従スマートハモリ: C長調の借用和音 E7 (E-G#-B-D) で E4 の上3度ハモが G4 ではなく G#4 に自動吸着
    // ------------------------------------------------------------------
    {
        // E7 和音の一時伴奏 WAV を生成して BGM プレイヤーへ読み込ませる
        auto testDir = gz::dataDirectory();
        testDir.createDirectory();
        auto tmpBgm = testDir.getChildFile ("e7_chord_test.wav");
        {
            const int bgmSamples = 48000;
            juce::AudioBuffer<float> chordBuf (2, bgmSamples);
            const double freqs[5] = { 164.81, 207.65, 246.94, 293.66, 415.30 }; // E3, G#3, B3, D4, G#4
            for (int i = 0; i < bgmSamples; ++i)
            {
                const double t = (double) i / sr;
                double s = 0.0;
                for (double f : freqs)
                    s += 0.14 * std::sin (juce::MathConstants<double>::twoPi * f * t);
                chordBuf.setSample (0, i, (float) s);
                chordBuf.setSample (1, i, (float) s);
            }
            tmpBgm.deleteFile();
            juce::WavAudioFormat wavFmt;
            if (auto out = tmpBgm.createOutputStream())
            {
                if (auto w = std::unique_ptr<juce::AudioFormatWriter> (wavFmt.createWriterFor (out.get(), sr, 2, 16, {}, 0)))
                {
                    out.release();
                    w->writeFromAudioSampleBuffer (chordBuf, 0, bgmSamples);
                }
            }
        }

        auto runHarmonyWithChordFollow = [&] (bool followOn, juce::String& outChordName) -> std::pair<double, double>
        {
            VocalGzzioProcessor proc;
            proc.prepareToPlay (sr, 256);
            setParam (proc, "mod_henshin", 1.0f);
            setParam (proc, "mod_space",   0.0f);
            setParam (proc, "revon",       0.0f);
            setParam (proc, "at_key",      0.0f);   // Key = C
            setParam (proc, "at_scale",    1.0f);   // 1 = Major (0 = Chromatic)
            setParam (proc, "jn_on",       1.0f);
            setParam (proc, "jn_harm",     1.0f);   // +3度 (上ハモ)
            setParam (proc, "jn_mix",      100.0f);
            setParam (proc, "jn_solo",     1.0f);   // ハモリ声部のみ出力（主旋律と伴奏を含めない）
            proc.setBgmSyncKey (false);             // キー自動変更は切り、純粋にコード追従のみ検証
            proc.setChordFollowHarmOn (followOn);
            proc.loadBgmFile (tmpBgm);
            proc.setBgmLooping (true);
            proc.setBgmToStream (false);            // buffer(録音/配信本線)へ伴奏を混ぜずハモリ純音だけを観測
            proc.setBgmPlaying (true);

            const int blocks = 140; // ~0.75秒
            std::vector<float> captured;
            captured.reserve ((size_t) blocks * 256u);
            juce::AudioBuffer<float> buf (2, 256);
            juce::MidiBuffer midi;
            double phase = 0.0;

            for (int b = 0; b < blocks; ++b)
            {
                for (int i = 0; i < 256; ++i)
                {
                    // 主旋律: E4 (329.628 Hz) の倍音付き歌声
                    double v = 0.0;
                    for (int h = 1; h <= 4; ++h)
                        v += std::sin (phase * (double) h) / (double) h;
                    phase += juce::MathConstants<double>::twoPi * 329.6276 / sr;
                    const float sample = (float) (0.22 * v);
                    buf.setSample (0, i, sample);
                    buf.setSample (1, i, sample);
                }
                proc.processBlock (buf, midi);
                if (b >= 60)
                {
                    const float* rL = buf.getReadPointer (0);
                    for (int i = 0; i < 256; ++i)
                        captured.push_back (rL[i]);
                }
            }
            outChordName = proc.getDetectedBgmChordName();
            proc.releaseResources();

            const double magG4  = measureNoteBandMag (captured.data(), (int) captured.size(), 391.995, sr); // G4 (濁る音)
            const double magGs4 = measureNoteBandMag (captured.data(), (int) captured.size(), 415.305, sr); // G#4 (E7長3度・協和音)
            return { magG4, magGs4 };
        };

        juce::String chordOff, chordOn;
        const auto [gOff, gsOff] = runHarmonyWithChordFollow (false, chordOff);
        const auto [gOn,  gsOn]  = runHarmonyWithChordFollow (true,  chordOn);
        tmpBgm.deleteFile();

        CHECK (chordOn.startsWith ("E"),
               "伴奏コードリアルタイム検出: E7和音から '%s' を検出", chordOn.toRawUTF8());
        CHECK (gOff > gsOff * 2.5,
               "コード追従OFF時: 従来のC長調ダイアトニック G4(%.4f) > G#4(%.4f)", gOff, gsOff);
        CHECK (gsOn > gOn * 2.5,
               "コード追従ON時 : 借用和音 E7 に合わせて G#4(%.4f) > G4(%.4f) へスマート吸着！", gsOn, gOn);
    }

    // ------------------------------------------------------------------
    // 4. 音ズレ1クリック自動計測 (startSyncAutoCalibration): スピーカー→マイク往復 35ms の自動検出
    // ------------------------------------------------------------------
    {
        VocalGzzioProcessor proc;
        proc.prepareToPlay (sr, 256);
        setParam (proc, "vc_on", 0.0f);
        setParam (proc, "at_on", 0.0f);

        const int simRoundTripSamples = 1680; // 35.0 ms @ 48 kHz
        std::vector<float> acousticLoop ((size_t) (simRoundTripSamples + 4096), 0.0f);
        int loopW = simRoundTripSamples;
        int loopR = 0;

        proc.startSyncAutoCalibration();
        juce::AudioBuffer<float> buf (2, 256);
        juce::MidiBuffer midi;

        for (int b = 0; b < 130 && proc.isSyncCalibrating(); ++b)
        {
            for (int i = 0; i < 256; ++i)
            {
                const float micIn = acousticLoop[(size_t) loopR] * 0.45f;
                loopR = (loopR + 1) % (int) acousticLoop.size();
                buf.setSample (0, i, micIn);
                buf.setSample (1, i, micIn);
            }
            proc.processBlock (buf, midi);
            const float* spkOut = buf.getReadPointer (0);
            for (int i = 0; i < 256; ++i)
            {
                acousticLoop[(size_t) loopW] = spkOut[i];
                loopW = (loopW + 1) % (int) acousticLoop.size();
            }
        }

        const int resCode = proc.getSyncCalibResultCode();
        const float rtMs  = proc.getSyncCalibMeasuredRtMs();
        const int offMs   = proc.getSyncOffsetMs();
        proc.releaseResources();

        CHECK (resCode == 1 && std::abs (rtMs - 35.0f) <= 2.5f && std::abs (offMs - (-35)) <= 3,
               "音ズレ1クリック自動計測: 結果=%d, 実測往復=%.2f ms (期待35.0ms), 自動補正値=%d ms",
               resCode, rtMs, offMs);
    }

    // ------------------------------------------------------------------
    // 5. SnsVideoExporter: H.264 + AAC MP4 (9:16 縦型 Before/After 比較モード) 実ファイル生成検査
    // ------------------------------------------------------------------
   #if JUCE_WINDOWS
    {
        const int clipSamples = 48000; // 1.0秒
        juce::AudioBuffer<float> dry (2, clipSamples), wet (2, clipSamples);
        for (int i = 0; i < clipSamples; ++i)
        {
            const double t = (double) i / sr;
            const float d = 0.15f * (float) std::sin (juce::MathConstants<double>::twoPi * 220.0 * t);
            const float w = 0.38f * (float) (std::sin (juce::MathConstants<double>::twoPi * 220.0 * t)
                                           + 0.4 * std::sin (juce::MathConstants<double>::twoPi * 440.0 * t));
            dry.setSample (0, i, d); dry.setSample (1, i, d);
            wet.setSample (0, i, w); wet.setSample (1, i, w);
        }

        auto testDir = gz::dataDirectory();
        testDir.createDirectory();
        auto mp4File = testDir.getChildFile ("sns_test_9x16_compare.mp4");
        mp4File.deleteFile();

        gz::SnsVideoExporter::Options opt;
        opt.width              = 720;
        opt.height             = 1280;
        opt.fps                = 30;
        opt.compareBeforeAfter = true;
        opt.showWatermark      = true;
        opt.titleText          = "VocalGzzio v5.5.0 Test";
        opt.subTitleText       = "-14 LUFS Auto-Mastered";

        std::atomic<float> progress { 0.0f };
        juce::String err;
        const bool ok = gz::SnsVideoExporter::exportMp4 (mp4File, dry, wet, sr, opt, &progress, nullptr, &err);

        char boxTag[5] = {};
        const juce::int64 fsize = mp4File.existsAsFile() ? mp4File.getSize() : 0;
        if (fsize >= 12)
        {
            if (auto in = mp4File.createInputStream())
            {
                in->setPosition (4);
                in->read (boxTag, 4);
            }
        }
        mp4File.deleteFile();

        CHECK (ok && fsize > 4096 && std::strcmp (boxTag, "ftyp") == 0,
               "SnsVideoExporter: 9:16 Before/After H.264+AAC MP4 生成成功 (size=%lld bytes, box='%s', err='%s')",
               (long long) fsize, boxTag, err.toRawUTF8());
    }
   #endif

    // ------------------------------------------------------------------
    // 6. 最終音量 out_vol (0〜250%) 直感リニアゲイン ＆ 大音量ブースト時の低歪み音質検査
    // ------------------------------------------------------------------
    {
        auto measureOutVolTone = [&] (float outVolPct, float makeupDb, float drivePct, float inAmp,
                                      double& outH1, double& outOddThdRatio) -> float
        {
            auto proc = std::make_unique<VocalGzzioProcessor>();
            proc->prepareToPlay (sr, 256);
            for (int m = 0; m < gz::ModuleChain::Count; ++m)
                setParam (*proc, gz::ModuleChain::paramId (m), 0.0f);
            setParam (*proc, "mod_neiro",    1.0f); // drive (warmth) を通す
            setParam (*proc, "argon_velvet", 0.0f);
            setParam (*proc, "argon_focus",  0.0f);
            setParam (*proc, "argon_silk",   0.0f);
            setParam (*proc, "argon_halo",   0.0f);
            setParam (*proc, "presence",     0.0f);
            setParam (*proc, "air",          0.0f);
            setParam (*proc, "sustain",      0.0f);
            setParam (*proc, "ring",         0.0f);
            setParam (*proc, "cons",         0.0f);
            setParam (*proc, "drive",        drivePct);
            setParam (*proc, "makeup",       makeupDb);
            proc->setOutputVolumePct (outVolPct);
            proc->setScreamGuardOn (true);

            const int blocks = 80;
            std::vector<float> tail;
            tail.reserve (40 * 256);
            juce::AudioBuffer<float> buf (2, 256);
            juce::MidiBuffer midi;
            double ph = 0.0;
            float peak = 0.0f;
            for (int b = 0; b < blocks; ++b)
            {
                for (int i = 0; i < 256; ++i)
                {
                    const float s = inAmp * (float) std::sin (ph);
                    ph += juce::MathConstants<double>::twoPi * 220.0 / sr;
                    buf.setSample (0, i, s);
                    buf.setSample (1, i, s);
                }
                proc->processBlock (buf, midi);
                if (b >= 40)
                {
                    const float* r = buf.getReadPointer (0);
                    for (int i = 0; i < 256; ++i)
                    {
                        tail.push_back (r[i]);
                        peak = juce::jmax (peak, std::abs (r[i]));
                    }
                }
            }
            proc->releaseResources();
            outH1 = measureFreqMag (tail.data(), (int) tail.size(), 220.0, sr);
            const double h3 = measureFreqMag (tail.data(), (int) tail.size(), 660.0, sr);
            const double h5 = measureFreqMag (tail.data(), (int) tail.size(), 1100.0, sr);
            outOddThdRatio = (outH1 > 1e-6) ? std::sqrt (h3 * h3 + h5 * h5) / outH1 : 0.0;
            return peak;
        };

        double h1Zero = 0.0, thdZero = 0.0;
        double h1Unit = 0.0, thdUnit = 0.0;
        double h1Dbl  = 0.0, thdDbl  = 0.0;
        double h1Loud = 0.0, thdLoud = 0.0;
        const float pkZero = measureOutVolTone (0.0f,   0.0f,  0.0f,  0.20f, h1Zero, thdZero);
        const float pkUnit = measureOutVolTone (100.0f, 0.0f,  0.0f,  0.20f, h1Unit, thdUnit);
        const float pkDbl  = measureOutVolTone (200.0f, 0.0f,  0.0f,  0.20f, h1Dbl,  thdDbl);
        const float pkLoud = measureOutVolTone (160.0f, 12.0f, 35.0f, 0.12f, h1Loud, thdLoud);

        CHECK (pkZero < 1e-4f && std::abs (pkDbl / juce::jmax (1e-6f, pkUnit) - 2.0f) < 0.04f,
               "最終音量 out_vol: 0%%=無音(%.6f), 100%%=%.4f, 200%%=%.4f (正確2.0倍)", pkZero, pkUnit, pkDbl);
        CHECK (pkLoud > 0.55f && pkLoud <= 0.995f && thdLoud < 0.08,
               "大音量ブースト時の高音質維持 (drive=35%%, makeup=+12dB, out_vol=160%%): peak=%.4f, 奇数次歪み率=%.4f (<0.08)",
               pkLoud, thdLoud);
    }

    // ------------------------------------------------------------------
    // 7. 声量ダイナミクス3モード (vocalExprMode): 🎤10割声量ダイレクト開放 vs 📻一定キープ
    // ------------------------------------------------------------------
    {
        auto measureDynamicRiseDb = [&] (int exprMode) -> float
        {
            auto proc = std::make_unique<VocalGzzioProcessor>();
            proc->prepareToPlay (sr, 256);
            for (int m = 0; m < gz::ModuleChain::Count; ++m)
                setParam (*proc, gz::ModuleChain::paramId (m), 0.0f);
            setParam (*proc, "mod_soroe",    1.0f); // ダイナミクス段を稼働
            setParam (*proc, "argon_velvet", 0.0f);
            setParam (*proc, "argon_focus",  0.0f);
            setParam (*proc, "argon_silk",   0.0f);
            setParam (*proc, "argon_halo",   0.0f);
            setParam (*proc, "src_mode",     0.0f); // うた
            setParam (*proc, "crush_on",     0.0f); // モード単体の効きを比較
            setParam (*proc, "comp1",        55.0f);
            setParam (*proc, "comp2",        65.0f);
            setParam (*proc, "ride_amt",     55.0f);
            setParam (*proc, "makeup",       0.0f);
            proc->setOutputVolumePct (100.0f);
            proc->setVocalExprMode (exprMode);

            auto runLevelRmsDb = [&] (float amp) -> float
            {
                juce::AudioBuffer<float> buf (2, 256);
                juce::MidiBuffer midi;
                double ph = 0.0;
                double sumSq = 0.0;
                int count = 0;
                for (int b = 0; b < 90; ++b)
                {
                    for (int i = 0; i < 256; ++i)
                    {
                        const float s = amp * (float) (0.75 * std::sin (ph) + 0.25 * std::sin (ph * 2.0));
                        ph += juce::MathConstants<double>::twoPi * 261.63 / sr;
                        buf.setSample (0, i, s);
                        buf.setSample (1, i, s);
                    }
                    proc->processBlock (buf, midi);
                    if (b >= 45)
                    {
                        const float* r = buf.getReadPointer (0);
                        for (int i = 0; i < 256; ++i)
                        {
                            sumSq += (double) r[i] * (double) r[i];
                            ++count;
                        }
                    }
                }
                const float rms = count > 0 ? (float) std::sqrt (sumSq / (double) count) : 1e-6f;
                return juce::Decibels::gainToDecibels (rms, -96.0f);
            };

            const float midDb  = runLevelRmsDb (0.08f);  // 中くらいの歌声 (~-22 dBFS)
            const float loudDb = runLevelRmsDb (0.63f);  // 10割フル声量のサビ (~-4 dBFS, +17.9 dB入力差)
            proc->releaseResources();
            return loudDb - midDb;
        };

        const float riseBalanced = measureDynamicRiseDb (0);
        const float riseDirect10 = measureDynamicRiseDb (1);
        const float riseSteady   = measureDynamicRiseDb (2);

        CHECK (riseDirect10 >= 12.0f && riseDirect10 > riseBalanced + 1.5f && riseBalanced > riseSteady + 1.0f,
               "声量幅3モード抑揚差 (+17.9dB入力変化に対し): 🎤10割ダイレクト=+%.2fdB > 標準=+%.2fdB > 📻一定キープ=+%.2fdB",
               riseDirect10, riseBalanced, riseSteady);
    }

    // ------------------------------------------------------------------
    // 8. 次世代進化①：🎧 歌い手専用「裏ガイドボーカル分離」 (bgmGuideMonitorOn)
    //    - 配信/録音側からは中央ボーカル(1000Hz Mid)を除去しつつ、ヘッドホン出力(buf)にだけ裏ガイドボーカルを返す
    // ------------------------------------------------------------------
    {
        auto testDir = gz::dataDirectory();
        testDir.createDirectory();
        auto guideBgmFile = testDir.getChildFile ("guide_vocal_sep_test.wav");
        {
            const int bgmSamples = 24000;
            juce::AudioBuffer<float> testBgm (2, bgmSamples);
            for (int i = 0; i < bgmSamples; ++i)
            {
                const double t = (double) i / sr;
                const float centerVoc = 0.40f * (float) std::sin (juce::MathConstants<double>::twoPi * 1000.0 * t);
                const float sideInst  = 0.30f * (float) std::sin (juce::MathConstants<double>::twoPi * 320.0 * t);
                testBgm.setSample (0, i, centerVoc + sideInst);
                testBgm.setSample (1, i, centerVoc - sideInst);
            }
            guideBgmFile.deleteFile();
            juce::WavAudioFormat wavFmt;
            if (auto out = guideBgmFile.createOutputStream())
            {
                if (auto w = std::unique_ptr<juce::AudioFormatWriter> (wavFmt.createWriterFor (out.get(), sr, 2, 16, {}, 0)))
                {
                    out.release();
                    w->writeFromAudioSampleBuffer (testBgm, 0, bgmSamples);
                }
            }
        }

        auto measureHeadphoneGuide = [&] (bool guideOn, float& outGuideMeterDb) -> double
        {
            auto proc = std::make_unique<VocalGzzioProcessor>();
            proc->prepareToPlay (sr, 256);
            proc->loadBgmFile (guideBgmFile);
            proc->setBgmGainDb (0.0f);
            proc->setBgmVocalCancel (true); // 配信・録音側はボーカル除去ON
            proc->setBgmToStream (true);
            proc->setBgmGuideMonitorLevel (1.0f);
            proc->setBgmGuideMonitorOn (guideOn);
            proc->setBgmPlaying (true);

            std::vector<float> hpOut;
            hpOut.reserve (40 * 256);
            juce::AudioBuffer<float> buf (2, 256);
            juce::MidiBuffer midi;
            for (int b = 0; b < 60; ++b)
            {
                buf.clear();
                proc->processBlock (buf, midi);
                if (b >= 20)
                {
                    const float* rL = buf.getReadPointer (0);
                    for (int i = 0; i < 256; ++i)
                        hpOut.push_back (rL[i]);
                }
            }
            outGuideMeterDb = proc->getBgmGuideMonitorMeterDb();
            proc->releaseResources();
            return measureFreqMag (hpOut.data(), (int) hpOut.size(), 1000.0, sr);
        };

        float meterOff = -60.0f, meterOn = -60.0f;
        const double magGuideOff = measureHeadphoneGuide (false, meterOff);
        const double magGuideOn  = measureHeadphoneGuide (true,  meterOn);
        guideBgmFile.deleteFile();

        CHECK (magGuideOff < 0.020 && magGuideOn > 0.085 && magGuideOn > magGuideOff * 5.0 && meterOn > -20.0f,
               "裏ガイドボーカル分離: ボーカル除去のみ1kHz=%.4f (配信/録音側) vs 裏ガイドON時ヘッドホン1kHz=%.4f (meter=%.1f dB)",
               magGuideOff, magGuideOn, meterOn);
    }

    // ------------------------------------------------------------------
    // 9. 次世代進化②：🎭 ゼロ遅延・1人デュエット「声キャラ瞬間モーフィング」 (vocalCharPreset)
    // ------------------------------------------------------------------
    {
        auto measureCharRatio = [&] (int preset, int& outLatencySamples) -> double
        {
            auto proc = std::make_unique<VocalGzzioProcessor>();
            proc->prepareToPlay (sr, 256);
            for (int m = 0; m < gz::ModuleChain::Count; ++m)
                setParam (*proc, gz::ModuleChain::paramId (m), 0.0f);
            setParam (*proc, "argon_velvet", 0.0f);
            setParam (*proc, "argon_focus",  0.0f);
            setParam (*proc, "argon_silk",   0.0f);
            setParam (*proc, "argon_halo",   0.0f);
            proc->setVocalCharPreset (preset);

            std::vector<float> sig;
            sig.reserve (30 * 256);
            juce::AudioBuffer<float> buf (2, 256);
            juce::MidiBuffer midi;
            double p1 = 0.0, p2 = 0.0;
            for (int b = 0; b < 50; ++b)
            {
                for (int i = 0; i < 256; ++i)
                {
                    const float s = 0.15f * (float) (std::sin (p1) + std::sin (p2));
                    p1 += juce::MathConstants<double>::twoPi * 210.0  / sr;
                    p2 += juce::MathConstants<double>::twoPi * 2700.0 / sr;
                    buf.setSample (0, i, s);
                    buf.setSample (1, i, s);
                }
                proc->processBlock (buf, midi);
                if (b >= 20)
                {
                    const float* r = buf.getReadPointer (0);
                    for (int i = 0; i < 256; ++i)
                        sig.push_back (r[i]);
                }
            }
            outLatencySamples = proc->getLatencySamples();
            proc->releaseResources();
            const double lowMag  = measureFreqMag (sig.data(), (int) sig.size(), 210.0,  sr);
            const double highMag = measureFreqMag (sig.data(), (int) sig.size(), 2700.0, sr);
            return (lowMag > 1e-6) ? (highMag / lowMag) : 0.0;
        };

        int lat0 = 0, latCool = 0, latSweet = 0;
        const double ratioOff   = measureCharRatio (0, lat0);
        const double ratioCool  = measureCharRatio (1, latCool);  // 💎 クール低音 (210Hz増強・2.6kHz減衰)
        const double ratioSweet = measureCharRatio (2, latSweet); // 🌸 スイート高音 (210Hz減衰・2.7kHz増強)

        CHECK (latCool == lat0 && latSweet == lat0 && ratioSweet > ratioOff * 1.8 && ratioCool < ratioOff * 0.65,
               "ゼロ遅延・声キャラ瞬間モーフィング: 追加遅延0サンプル (lat=%d), 高低比 OFF=%.2f / Cool=%.2f / Sweet=%.2f",
               latCool - lat0, ratioOff, ratioCool, ratioSweet);
    }

    // ------------------------------------------------------------------
    // 10. 次世代進化④：📺 OBSブラウザソース用「Now Playing オーバーレイ (.html)」自動生成検査
    // ------------------------------------------------------------------
    {
        auto proc = std::make_unique<VocalGzzioProcessor>();
        proc->prepareToPlay (sr, 256);
        proc->loadBuiltinDemoBgm();
        proc->setVocalCharPreset (2); // 🌸 スイート高音
        proc->addCurrentSongToSetlist();
        const bool okHtml = proc->updateObsNowPlayingOverlayFile();
        const auto htmlFile = proc->getObsOverlayHtmlFile();
        const juce::String content = htmlFile.existsAsFile() ? htmlFile.loadFileAsString() : juce::String();
        htmlFile.deleteFile();
        proc->releaseResources();

        CHECK (okHtml && content.contains ("VocalGzzio OBS Now Playing Overlay") && content.contains ("BPM"),
               "OBS Now Playing オーバーレイ自動生成: HTML生成成功 (%d bytes)", (int) content.getNumBytesAsUTF8());
    }

    std::printf ("=== dsp_v550_features 完了: FAIL=%d ===\n", gFail);
    return gFail == 0 ? 0 : 1;
}
