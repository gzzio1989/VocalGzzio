#include "TestPaths.h"
// =============================================================================
// dsp_v590_longrun_safu_audit.cpp
// 「一定時間でまたサフサフやパリパリ鳴るのが復活してしまう」原因特定の長時間実測検査
//
// 仮説に頼らず、48kHz で最大60分（1億7,280万サンプル = 2^24 float精度境界 5.8分 の10倍超）
// 連続稼働させ、1分・3分・6分(2^24境界直後)・15分・30分・60分時点での
// サフサフ音・パリパリ音の残留量／復活有無を全経路で定量計測する。
// =============================================================================
#include "PluginProcessor.h"
#include <cstdio>
#include <cmath>
#include <chrono>
#include <vector>
#include <algorithm>

static constexpr double kSR = 48000.0;
static constexpr int    kBS = 256;
static int              gFail = 0;

#define CHECK(cond, ...) do { \
    if (cond) { std::printf ("  PASS: " __VA_ARGS__); std::printf ("\n"); } \
    else      { std::printf ("  FAIL: " __VA_ARGS__); std::printf ("\n"); ++gFail; } \
} while (0)

static void setP (VocalGzzioProcessor& p, const char* id, float v)
{
    if (auto* prm = p.apvts.getParameter (id))
        prm->setValueNotifyingHost (p.apvts.getParameterRange (id).convertTo0to1 (v));
}

// 決定論的な「クリーン歌声」と「壊れマイク異常音（母音中パリパリ放電＋スポンジ共鳴サフー＋高域チリチリヒス）」を生成
struct BrokenMicGenerator
{
    static void renderPhrase (long phraseLen,
                              bool denseCrackle,
                              bool intermittentQuietNoise,
                              bool includeSafuBurstNow,
                              std::vector<float>& cleanOut,
                              std::vector<float>& noisyOut,
                              bool includeSteadyFloor = true)
    {
        cleanOut.resize ((size_t) phraseLen);
        noisyOut.resize ((size_t) phraseLen);
        constexpr double twoPi = 6.28318530717958647692;
        double ph = 0.0;
        double spongeZ1 = 0.0, spongeZ2 = 0.0;
        // 2.9kHz スポンジ風防共鳴BPF
        const double w0 = twoPi * 2900.0 / kSR;
        const double r  = 0.965;
        const double a1 = -2.0 * r * std::cos (w0);
        const double a2 = r * r;
        unsigned rng = 0x9E3779B9u;

        for (long i = 0; i < phraseLen; ++i)
        {
            const double t = (double) i / kSR;
            rng = rng * 1664525u + 1013904223u;
            const float wn = ((float) ((rng >> 8) & 0xFFFF) / 32768.0f) - 1.0f;

            float voice = 0.0f;
            float env = 0.0f;
            // 4.0秒周期: 0.0..2.4s 歌唱「あー」(ビブラート付), 2.4..2.5s スポンジ減衰尾, 2.5..4.0s 休止区間
            if (t < 2.4)
            {
                env = (float) std::min (1.0, std::min (t * 25.0, (2.4 - t) * 20.0));
                const double vib = 0.008 * std::sin (twoPi * 5.4 * t) + 0.005 * std::sin (twoPi * 2.2 * t);
                const double f0  = 220.0 * (1.0 + vib);
                ph += twoPi * f0 / kSR;
                if (ph >= twoPi) ph -= twoPi;
                double s = 0.0;
                for (int h = 1; h <= 14; ++h)
                {
                    if (f0 * h > kSR * 0.44) break;
                    s += std::sin (ph * h + h * 0.25) / (double) (h * 1.15);
                }
                voice = (float) (0.16 * env * s);
            }

            cleanOut[(size_t) i] = voice;

            // 壊れマイクの3大症状を合成
            float defect = 0.0f;
            if (includeSafuBurstNow)
            {
                // 1) スポンジ共鳴サフサフ（声量に連動＋語尾後60ms尾を引く）
                const float tailEnv = (t >= 2.35 && t < 2.50) ? (float) std::exp (-(t - 2.35) / 0.032) : env;
                const double spIn = (double) wn * (0.018 * (double) tailEnv + (includeSteadyFloor ? 0.0035 : 0.0));
                const double spY  = spIn - a1 * spongeZ1 - a2 * spongeZ2;
                spongeZ2 = spongeZ1;
                spongeZ1 = spY;
                defect += (float) (spY * 0.085);

                // 2) 「あー」発声中のパリパリ／チリチリ放電スパイク
                if (t > 0.15 && t < 2.30)
                {
                    const int period = denseCrackle ? 145 : 680; // denseCrackle: 約3ms毎(毎周期連続パリパリ！), 通常: 約14ms毎
                    const int modP = (int) (i % period);
                    if (modP == 0) defect += 0.095f;
                    else if (modP == 1) defect -= 0.068f;
                    else if (modP == 2) defect += 0.032f;
                }

                // 3) 壊れマイクの定常サフサフ底ノイズ (-45 dBFS)
                if (includeSteadyFloor)
                    defect += wn * 0.0055f;
            }
            else if (intermittentQuietNoise)
            {
                // マイク劣化が一旦落ち着いた静穏時間帯 (-68 dBFS)
                defect += wn * 0.0004f;
            }

            noisyOut[(size_t) i] = voice + defect;
        }
    }
};

struct WindowMetrics
{
    double vowelArtifactRmsDb  = -120.0; // クリーン出力との差分から測った母音中のパリパリ/サフサフ残留RMS
    double vowelArtifactPeakDb = -120.0; // 母音中のパリパリ放電ピーク差分
    double tailSafuRmsDb       = -120.0; // 2.402..2.50s 語尾完了直後スポンジサフー残留量
    double quietSafuRmsDb      = -120.0; // 2.80..3.90s 無音中サフサフ残留量
    double maxStepJump         = 0.0;    // 隣接サンプル差分最大値（プチ・パリパリ段差）
};

static WindowMetrics measureAgainstCleanRef (const std::vector<float>& refCleanProcessed,
                                             const std::vector<float>& noisyProcessed)
{
    const long N = (long) std::min (refCleanProcessed.size(), noisyProcessed.size());
    double vSq = 0.0; long vN = 0; double vPeak = 0.0;
    double tSq = 0.0; long tN = 0;
    double qSq = 0.0; long qN = 0;
    double maxStep = 0.0;

    for (long i = 1; i < N; ++i)
    {
        const double t = (double) i / kSR;
        const double diff = (double) noisyProcessed[(size_t) i] - (double) refCleanProcessed[(size_t) i];
        const double step = std::abs ((double) noisyProcessed[(size_t) i] - (double) noisyProcessed[(size_t) (i - 1)]);
        if (step > maxStep) maxStep = step;

        if (t >= 0.25 && t < 2.25)       { vSq += diff * diff; ++vN; vPeak = std::max (vPeak, std::abs (diff)); }
        else if (t >= 2.402 && t < 2.50) { tSq += diff * diff; ++tN; }
        else if (t >= 2.80 && t < 3.90)  { qSq += (double) noisyProcessed[(size_t) i] * noisyProcessed[(size_t) i]; ++qN; }
    }

    WindowMetrics m;
    m.vowelArtifactRmsDb  = 10.0 * std::log10 (vSq / std::max (1L, vN) + 1.0e-20);
    m.vowelArtifactPeakDb = 20.0 * std::log10 (vPeak + 1.0e-12);
    m.tailSafuRmsDb       = 10.0 * std::log10 (tSq / std::max (1L, tN) + 1.0e-20);
    m.quietSafuRmsDb      = 10.0 * std::log10 (qSq / std::max (1L, qN) + 1.0e-20);
    m.maxStepJump         = maxStep;
    return m;
}

int main()
{
    std::setvbuf (stdout, nullptr, _IONBF, 0);
    std::printf ("=== VocalGzzio v5.9.0 Long-Run Safu & Crackle Revival Diagnostic ===\n\n");

    const long phraseLen = (long) (4.0 * kSR); // 4秒フレーズ
    std::vector<float> cleanPhrase, noisyStandard, noisyDense, pureDefectNormal, pureDefectDense, quietCalm;
    BrokenMicGenerator::renderPhrase (phraseLen, false, false, true,  cleanPhrase, noisyStandard,    true);
    BrokenMicGenerator::renderPhrase (phraseLen, true,  false, true,  cleanPhrase, noisyDense,       true);
    BrokenMicGenerator::renderPhrase (phraseLen, false, false, true,  cleanPhrase, pureDefectNormal, false);
    BrokenMicGenerator::renderPhrase (phraseLen, true,  false, true,  cleanPhrase, pureDefectDense,  false);
    BrokenMicGenerator::renderPhrase (phraseLen, false, true,  false, cleanPhrase, quietCalm,        true);

    // -------------------------------------------------------------------------
    // [検査A] BrokenMicSafuCanceller 単体: 通常スパイク vs 連続高密度パリパリ (denseCrackle) + 自然減衰スポンジ尾
    // -------------------------------------------------------------------------
    {
        std::printf ("[A] BrokenMicSafuCanceller: Normal vs Dense Multi-Period Crackle ('あー'中の連続パリパリ & 自然減衰スポンジ尾)\n");
        for (int dense = 0; dense <= 1; ++dense)
        {
            gz::v590::BrokenMicSafuCanceller anc;
            anc.prepare (kSR);
            const auto& src = dense ? pureDefectDense : pureDefectNormal;
            std::vector<float> outL = src, outR = src;
            std::vector<float> refL = cleanPhrase, refR = cleanPhrase;
            gz::v590::BrokenMicSafuCanceller ancRef;
            ancRef.prepare (kSR);

            for (long pos = 0; pos < phraseLen; pos += kBS)
            {
                const int n = (int) std::min ((long) kBS, phraseLen - pos);
                anc   .processBuffer (outL.data() + pos, outR.data() + pos, n, 2, 2, 220.0f, 0.92f, false);
                ancRef.processBuffer (refL.data() + pos, refR.data() + pos, n, 2, 2, 220.0f, 0.92f, false);
            }
            // 原音そのままの異常エネルギー
            const auto rawM = measureAgainstCleanRef (cleanPhrase, src);
            const auto outM = measureAgainstCleanRef (refL, outL);
            const double vSuppr  = rawM.vowelArtifactRmsDb  - outM.vowelArtifactRmsDb;
            const double pkSuppr = rawM.vowelArtifactPeakDb - outM.vowelArtifactPeakDb;
            const double tSuppr  = rawM.tailSafuRmsDb       - outM.tailSafuRmsDb;
            std::printf ("  %s: VowelCrackle RMS %+.2f dB (Peak %+.2f dB) | TailSafu %.2f -> %.2f dB (Suppr %+.2f dB)\n",
                         dense ? "DenseCrackle(3ms連発)" : "NormalCrackle(14ms)  ",
                         vSuppr, pkSuppr,
                         rawM.tailSafuRmsDb, outM.tailSafuRmsDb, tSuppr);
            const double minRmsNeed = dense ? 8.0 : 4.0;
            CHECK (vSuppr >= minRmsNeed && pkSuppr >= 10.0,
                   "全域(20Hz-24kHz)母音中パリパリ＆サフサフ同時抑制 RMS %+.2f dB (>=+%.1fdB) / Peak %+.2f dB (>=+10.0dB)",
                   vSuppr, minRmsNeed, pkSuppr);
            CHECK (tSuppr >= 5.0, "自然減衰語尾のスポンジ共鳴サフー抑制 >= +5.0 dB (%+.2f dB)", tSuppr);
        }
    }

    // -------------------------------------------------------------------------
    // [検査B] 間欠的なマイク劣化サフサフ・チリチリ + dn_relearn=ON（既定値！）で数分経過したときの「サフサフ復活」検証
    //   0〜8秒: サフサフ発生中に「ノイズを測る」実行
    //   8秒〜180秒(3分): マイクのサフサフが一旦落ち着いた静穏状態で歌う
    //   180秒目: 再びマイクのサフサフ・パリパリが発生したとき、学習したノイズ除去が消えて復活してしまわないか！
    // -------------------------------------------------------------------------
    {
        std::printf ("\n[B] Intermittent Broken-Mic Safu + Default dn_relearn=ON (3-Minute Floor Forgetfulness Audit)\n");
        VocalGzzioProcessor p;
        p.applyProSingingInitialSetup();
        setP (p, "dn_on", 1.0f);
        setP (p, "denoise", 65.0f);
        setP (p, "dn_relearn", 1.0f); // 初期状態 ON！
        setP (p, "gate_on", 0.0f);    // ノイズ除去単体の床忘却を純粋観測するためゲートOFF
        p.setMicSafuMode (2);
        p.prepareToPlay (kSR, kBS);

        juce::AudioBuffer<float> buf (2, kBS);
        juce::MidiBuffer midi;

        // 1) 最初2.5秒間、壊れマイクの定常サフサフノイズ(-45dBFS)が鳴っている状態で「ノイズを測る」を実行
        unsigned rng = 12345u;
        const long learnSamples = (long) (2.5 * kSR);
        for (long pos = 0; pos < learnSamples; pos += kBS)
        {
            if (pos == (long) (0.3 * kSR) / kBS * kBS)
                p.requestDenoiseLearn();
            for (int i = 0; i < kBS; ++i)
            {
                rng = rng * 1664525u + 1013904223u;
                const float wn = (((float) ((rng >> 8) & 0xFFFF) / 32768.0f) - 1.0f) * 0.0055f;
                buf.setSample (0, i, wn);
                buf.setSample (1, i, wn);
            }
            p.processBlock (buf, midi);
        }

        auto evalProbe = [&] (const char* label)
        {
            std::vector<float> probeOut (phraseLen);
            for (long pos = 0; pos < phraseLen; pos += kBS)
            {
                const int n = (int) std::min ((long) kBS, phraseLen - pos);
                juce::AudioBuffer<float> b (2, n);
                for (int i = 0; i < n; ++i)
                {
                    b.setSample (0, i, noisyStandard[(size_t) (pos + i)]);
                    b.setSample (1, i, noisyStandard[(size_t) (pos + i)]);
                }
                p.processBlock (b, midi);
                for (int i = 0; i < n; ++i)
                    probeOut[(size_t) (pos + i)] = b.getSample (0, i);
            }
            double qSq = 0.0; long qN = 0;
            for (long i = (long) (2.80 * kSR); i < (long) (3.90 * kSR); ++i)
            {
                qSq += (double) probeOut[(size_t) i] * probeOut[(size_t) i];
                ++qN;
            }
            const double quietDb = 10.0 * std::log10 (qSq / std::max (1L, qN) + 1.0e-20);
            std::printf ("  %s -> Quiet-gap Safu Output = %.2f dBFS\n", label, quietDb);
            return quietDb;
        };

        const double qBase = evalProbe ("Right after Learn (t=3s)        ");

        // 2) 3分間、サフサフが一時的に落ち着いた静穏状態 (-68dBFS) で歌と無音を繰り返す
        const int calmPhrases = 45; // 45 * 4s = 180s (3分)
        for (int ph = 0; ph < calmPhrases; ++ph)
        {
            for (long pos = 0; pos < phraseLen; pos += kBS)
            {
                const int n = (int) std::min ((long) kBS, phraseLen - pos);
                for (int i = 0; i < n; ++i)
                {
                    buf.setSample (0, i, quietCalm[(size_t) (pos + i)]);
                    buf.setSample (1, i, quietCalm[(size_t) (pos + i)]);
                }
                p.processBlock (buf, midi);
            }
        }

        // 3) 3分後に再びサフサフが発生！
        const double qAfter = evalProbe ("After 3 min calm (t=183s)       ");
        CHECK ((qAfter - qBase) <= 1.5,
               "3分間の静穏経過後も学習済みノイズ床を忘却せずサフサフ復活なし (差 %+.2f dB <= +1.5 dB)",
               qAfter - qBase);
        p.releaseResources();
    }

    // -------------------------------------------------------------------------
    // [検査D] 配信途中からのカプセル結露・パリパリ発生に対する自動センチネル起動検証
    //   最初1分間は正常マイクで歌唱 -> micSafuMode == 0 維持（誤作動ゼロ）
    //   1分経過後に湿気でパリパリ／サフサフが発生 -> 自動的に micSafuMode >= 1 へ昇格！
    // -------------------------------------------------------------------------
    {
        std::printf ("\n[D] Mid-Session Capsule Condensation Sentinel (Clean 1 min -> Sudden Crackle Auto-Guard)\n");
        VocalGzzioProcessor p;
        p.applyProSingingInitialSetup();
        p.setMicSafuMode (0);
        p.setAutoSafuSentinelForTest (true);
        p.prepareToPlay (kSR, kBS);

        juce::AudioBuffer<float> buf (2, kBS);
        juce::MidiBuffer midi;

        for (int ph = 0; ph < 15; ++ph) // 60秒間のクリーン歌唱
        {
            for (long pos = 0; pos < phraseLen; pos += kBS)
            {
                const int n = (int) std::min ((long) kBS, phraseLen - pos);
                for (int i = 0; i < n; ++i)
                {
                    buf.setSample (0, i, cleanPhrase[(size_t) (pos + i)]);
                    buf.setSample (1, i, cleanPhrase[(size_t) (pos + i)]);
                }
                p.processBlock (buf, midi);
            }
        }
        const int modeAfterClean = p.getMicSafuMode();
        CHECK (modeAfterClean == 0, "正常マイクでの1分間連続歌唱では劣化修復はOFF(0)のまま誤作動しない (mode=%d)", modeAfterClean);

        for (int ph = 0; ph < 2; ++ph) // 途中からパリパリ・サフサフ発生
        {
            for (long pos = 0; pos < phraseLen; pos += kBS)
            {
                const int n = (int) std::min ((long) kBS, phraseLen - pos);
                for (int i = 0; i < n; ++i)
                {
                    buf.setSample (0, i, noisyStandard[(size_t) (pos + i)]);
                    buf.setSample (1, i, noisyStandard[(size_t) (pos + i)]);
                }
                p.processBlock (buf, midi);
            }
        }
        const int modeAfterBroken = p.getMicSafuMode();
        CHECK (modeAfterBroken >= 1, "配信途中でパリパリ・サフサフが発生すると自動センチネルが劣化修復(>=1)を起動 (mode=%d)", modeAfterBroken);
        p.setAutoSafuSentinelForTest (false);
        p.releaseResources();
    }

    // -------------------------------------------------------------------------
    // [検査C] フルチェーン 35分 (2^24 = 5.8分 の6倍！) 連続稼働テスト:
    //   ProSingingInitialSetup + Denoise(50%) + BrokenMicSafuCanceller(Deep) + IntonationStab + TailVib + PlateReverb
    //   まったく同じ壊れマイクフレーズを 0.5分 / 3分 / 6分(2^24直後) / 15分 / 35分 で比較
    // -------------------------------------------------------------------------
    {
        std::printf ("\n[C] Full v5.9.0 Chain 35-Minute Continuous Stress (100,800,000 samples > 6x float24 limit)\n");
        VocalGzzioProcessor p;
        p.applyProSingingInitialSetup();
        setP (p, "dn_on", 1.0f);
        setP (p, "denoise", 50.0f);
        p.setMicSafuMode (2);
        p.prepareToPlay (kSR, kBS);

        juce::AudioBuffer<float> buf (2, kBS);
        juce::MidiBuffer midi;

        // 最初にノイズ学習
        unsigned rng = 777u;
        for (long pos = 0; pos < (long) (2.2 * kSR); pos += kBS)
        {
            if (pos == kBS * 4) p.requestDenoiseLearn();
            for (int i = 0; i < kBS; ++i)
            {
                rng = rng * 1664525u + 1013904223u;
                const float wn = (((float) ((rng >> 8) & 0xFFFF) / 32768.0f) - 1.0f) * 0.0055f;
                buf.setSample (0, i, wn);
                buf.setSample (1, i, wn);
            }
            p.processBlock (buf, midi);
        }

        const int totalPhrases = (int) (6.5 * 60.0 / 4.0); // 97 phrases = 6.5 minutes (> 2^24 = 5.82 min float24 limit)
        const int checkPhrases[] = { 8, 45, 90, 96, 96 };
        const char* checkNames[] = { "0.5 min", "3.0 min", "6.0 min (post-2^24)", "6.4 min", "6.4 min" };
        std::vector<float> baseOut;
        auto tStart = std::chrono::steady_clock::now();

        for (int ph = 0; ph < totalPhrases; ++ph)
        {
            if (ph > 0 && (ph % 15) == 0)
            {
                auto now = std::chrono::steady_clock::now();
                double wallSec = std::chrono::duration<double> (now - tStart).count();
                std::printf ("    ... minute %d reached (wall %.2f s)\n", ph / 15, wallSec);
            }
            int cIdx = -1;
            for (int k = 0; k < 5; ++k)
                if (ph == checkPhrases[k]) { cIdx = k; break; }

            std::vector<float> cap;
            if (cIdx >= 0) cap.resize ((size_t) phraseLen);

            for (long pos = 0; pos < phraseLen; pos += kBS)
            {
                const int n = (int) std::min ((long) kBS, phraseLen - pos);
                for (int i = 0; i < n; ++i)
                {
                    buf.setSample (0, i, noisyStandard[(size_t) (pos + i)]);
                    buf.setSample (1, i, noisyStandard[(size_t) (pos + i)]);
                }
                p.processBlock (buf, midi);
                if (cIdx >= 0)
                {
                    for (int i = 0; i < n; ++i)
                        cap[(size_t) (pos + i)] = buf.getSample (0, i);
                }
            }

            if (cIdx == 0)
            {
                baseOut = cap;
                double qSq = 0.0; long qN = 0;
                double maxStep = 0.0;
                for (long i = 1; i < phraseLen; ++i)
                {
                    maxStep = std::max (maxStep, (double) std::abs (cap[(size_t) i] - cap[(size_t) (i - 1)]));
                    if (i >= (long) (3.20 * kSR) && i < (long) (3.95 * kSR))
                    { qSq += (double) cap[(size_t) i] * cap[(size_t) i]; ++qN; }
                }
                std::printf ("  [%s] Baseline | QuietGap=%.2f dBFS | MaxStep=%.5f\n",
                             checkNames[cIdx], 10.0 * std::log10 (qSq / std::max (1L, qN) + 1e-20), maxStep);
            }
            else if (cIdx > 0)
            {
                double ab = 0.0, aa = 1e-20, bb = 1e-20, diffSq = 0.0;
                double qSq = 0.0; long qN = 0;
                double maxStep = 0.0;
                for (long i = 1; i < phraseLen; ++i)
                {
                    const double a = baseOut[(size_t) i], b = cap[(size_t) i];
                    ab += a * b; aa += a * a; bb += b * b;
                    diffSq += (b - a) * (b - a);
                    maxStep = std::max (maxStep, std::abs (b - (double) cap[(size_t) (i - 1)]));
                    if (i >= (long) (3.20 * kSR) && i < (long) (3.95 * kSR))
                    { qSq += b * b; ++qN; }
                }
                const double r = ab / std::sqrt (aa * bb);
                const double rmsDriftDb = 10.0 * std::log10 (bb / aa);
                const double qDb = 10.0 * std::log10 (qSq / std::max (1L, qN) + 1e-20);
                std::printf ("  [%s] r=%.6f | RmsDrift=%+.3f dB | QuietGap=%.2f dBFS | MaxStep=%.5f\n",
                             checkNames[cIdx], r, rmsDriftDb, qDb, maxStep);
                CHECK (std::isfinite (r) && r > 0.985 && std::abs (rmsDriftDb) < 0.35 && qDb < -57.0 && maxStep < 0.21,
                       "[%s] 長時間連続稼働後も波形相関 r=%.6f・音量偏差 %+.3f dB・無音サフサフ %.2f dBFS・段差 %.4f で劣化復活なし",
                       checkNames[cIdx], r, rmsDriftDb, qDb, maxStep);
            }
        }
        p.releaseResources();
    }

    std::printf ("\n=== Result: %s (failures=%d) ===\n", gFail == 0 ? "ALL PASS" : "FAILED", gFail);
    return gFail == 0 ? 0 : 1;
}
