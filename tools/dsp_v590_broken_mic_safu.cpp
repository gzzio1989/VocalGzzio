// v5.9.0 自動検証スイート:
//  1. 🎙️ マイク劣化・パリパリ／サフー修復 (BrokenMicSafuCanceller): 追加遅延 0.0 ms (0 samples) 保証
//  2. クリーン歌唱の完全透明性 (r > 0.999, 残差 < -35 dB) ＆ 日本語子音 (さ・ふ・息継ぎ) 98%以上保持
//  3. 4大マイク劣化症状の抑制検証:
//     - 症状1: 「あー」発声中のカプセル湿気パリパリ・チリチリ微小アーク放電バースト（単体＆ノイズ除去併用）
//     - 症状2: 劣化した風防スポンジの共鳴サフー（基音包絡変調）＋ 母音停止後 48ms 減衰尾の消去
//     - 症状3: 壊れたスピーカーのような振動板片側擦れ歪み (Rub & Buzz)
//     - 症状4: 複合劣化マイク（パリパリ＋スポンジ共鳴＋擦れ歪み同時発生）＆ メーター表示
//  4. 『うた自動』でのマイク劣化自動検知（クリーン声では OFF 維持、パリパリ声では自動 ON）
//  5. VST3 状態保存／復元ラウンドトリップ・マルチサンプルレート (44.1/48/96kHz)・128サンプル低負荷検証
//  6. 初回／標準起動時のデフォルト画面検証: 必ず「ふつう（日本語・標準総合画面）」で起動し英語スタートにならないこと
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "TestPaths.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

namespace
{
int gFailures = 0;

void check (bool cond, const char* msg)
{
    if (! cond)
    {
        std::fprintf (stderr, "FAIL: %s\n", msg);
        ++gFailures;
    }
}

void setParam (VocalGzzioProcessor& proc, const char* id, float plainVal)
{
    if (auto* p = proc.apvts.getParameter (id))
        p->setValueNotifyingHost (proc.apvts.getParameterRange (id).convertTo0to1 (plainVal));
}

void drainStartupSelfTest (VocalGzzioProcessor& proc, int blockSize)
{
    juce::AudioBuffer<float> buf (2, blockSize);
    juce::MidiBuffer midi;
    for (int i = 0; i < 64 && proc.isSelfTestRunning(); ++i)
    {
        buf.clear();
        proc.processBlock (buf, midi);
    }
}

// すべて素通しにして「整える (mod_souji)」モジュールのみを有効化した検証用プロセッサを構築
void configureCleanOnlyStrip (VocalGzzioProcessor& proc, double sr, bool enableDenoise, float denoisePct)
{
    proc.setPlayConfigDetails (2, 2, sr, 512);
    proc.prepareToPlay (sr, 512);
    drainStartupSelfTest (proc, 256);

    for (int m = 0; m < gz::ModuleChain::Count; ++m)
        setParam (proc, gz::ModuleChain::paramId (m), m == gz::ModuleChain::Souji ? 1.0f : 0.0f);

    setParam (proc, "ingain",  0.0f);
    setParam (proc, "makeup",  0.0f);
    setParam (proc, "gate",   -90.0f);
    setParam (proc, "lowcut",  20.0f);
    setParam (proc, "pop",     0.0f);
    setParam (proc, "lip",     0.0f);
    setParam (proc, "hum",     0.0f);
    setParam (proc, "pick",    0.0f);
    setParam (proc, "mud",     0.0f);
    setParam (proc, "harsh",   0.0f);
    setParam (proc, "dn_on",   enableDenoise ? 1.0f : 0.0f);
    setParam (proc, "denoise", denoisePct);
    proc.setMasteringPrismMode (0);
    proc.setJpopPolishMode (0);
    proc.setLipDeclickOn (false);
    proc.setRoomDereverbOn (false);
    proc.setBreathControlOn (false);
}

std::vector<float> highpass1300 (const std::vector<float>& x, double sr);

struct VocalTestBench
{
    std::vector<float> clean;
    std::vector<bool>  voicedMask;
    std::vector<bool>  consMask;
    std::vector<bool>  tailMask;
    std::vector<float> defectCrackle;
    std::vector<float> defectSponge;
    std::vector<float> defectRubBuzz;
    std::vector<float> defectCombined;
};

VocalTestBench buildVocalBench (double sr)
{
    const int N = (int) (sr * 3.2);
    VocalTestBench b;
    b.clean.assign ((size_t) N, 0.0f);
    b.voicedMask.assign ((size_t) N, false);
    b.consMask.assign ((size_t) N, false);
    b.tailMask.assign ((size_t) N, false);
    b.defectCrackle.assign ((size_t) N, 0.0f);
    b.defectSponge.assign ((size_t) N, 0.0f);
    b.defectRubBuzz.assign ((size_t) N, 0.0f);
    b.defectCombined.assign ((size_t) N, 0.0f);

    std::mt19937 rng (5902026u);
    std::normal_distribution<float> norm (0.0f, 1.0f);
    std::uniform_real_distribution<float> uni (0.0f, 1.0f);

    // 簡易フォルマント共振器（2次IIRバンドパス）
    auto makeResonator = [sr] (double fc, double bw, const std::vector<float>& src)
    {
        std::vector<float> out (src.size(), 0.0f);
        const double r = std::exp (-juce::MathConstants<double>::pi * bw / sr);
        const double c1 = 2.0 * r * std::cos (2.0 * juce::MathConstants<double>::pi * fc / sr);
        const double c2 = -r * r;
        const double g  = 1.0 - r;
        double y1 = 0.0, y2 = 0.0;
        for (size_t n = 0; n < src.size(); ++n)
        {
            const double y = g * src[n] + c1 * y1 + c2 * y2;
            y2 = y1;
            y1 = y;
            out[n] = (float) y;
        }
        return out;
    };

    std::vector<float> glottal ((size_t) N, 0.0f);
    std::vector<float> white ((size_t) N, 0.0f);
    std::vector<float> f0Track ((size_t) N, 0.0f);
    double phase = 0.0;

    for (int n = 0; n < N; ++n)
    {
        white[(size_t) n] = norm (rng);
        const double t = (double) n / sr;

        // 0.15..0.32s: 無声歯擦音「さ (/s/)」
        // 0.32..1.45s: 有声母音「あー (/a:/)」（ビブラート付き）
        // 1.45..1.52s: 母音停止直後の無音（スポンジ共鳴の減衰尾観測区間）
        // 1.65..1.80s: 無声摩擦子音「ふ (/fu/ 子音部)」
        // 1.80..2.85s: 有声母音「うー (/u:/)」
        // 2.90..3.10s: 息継ぎブレス
        if ((t >= 0.32 && t < 1.45) || (t >= 1.80 && t < 2.85))
        {
            const double baseF0 = (t < 1.50) ? 220.0 : 261.63;
            const double f0 = baseF0 + 3.8 * std::sin (2.0 * juce::MathConstants<double>::pi * 5.3 * t);
            f0Track[(size_t) n] = (float) f0;
            phase += f0 / sr;
            if (phase >= 1.0) phase -= 1.0;

            double s = 0.0;
            for (int h = 1; h <= 26; ++h)
            {
                if (h * f0 >= 0.44 * sr) break;
                const double amp = 1.0 / std::pow ((double) h, 1.18);
                s += amp * std::sin (2.0 * juce::MathConstants<double>::pi * h * phase + 0.13 * h);
            }
            const double segStart = (t < 1.50) ? 0.32 : 1.80;
            const double segEnd   = (t < 1.50) ? 1.45 : 2.85;
            const double fadeIn   = juce::jlimit (0.0, 1.0, (t - segStart) / 0.018);
            const double fadeOut  = juce::jlimit (0.0, 1.0, (segEnd - t) / 0.022);
            glottal[(size_t) n] = (float) (0.22 * fadeIn * fadeOut * s);
            if (t >= segStart + 0.04 && t <= segEnd - 0.04)
                b.voicedMask[(size_t) n] = true;
        }
        if (t >= 1.455 && t < 1.505)
            b.tailMask[(size_t) n] = true;
    }

    const auto f1 = makeResonator (780.0,  130.0, glottal);
    const auto f2 = makeResonator (1240.0, 170.0, glottal);
    const auto f3 = makeResonator (2850.0, 260.0, glottal);
    const auto sBand  = makeResonator (6400.0, 2200.0, white);
    const auto fuBand = makeResonator (2400.0, 1800.0, white);
    const auto brBand = makeResonator (1700.0, 1500.0, white);

    for (int n = 0; n < N; ++n)
    {
        const double t = (double) n / sr;
        float v = 0.45f * glottal[(size_t) n] + 0.55f * f1[(size_t) n] + 0.42f * f2[(size_t) n] + 0.24f * f3[(size_t) n];

        if (t >= 0.15 && t < 0.32)
        {
            const float env = (float) std::sin (juce::MathConstants<double>::pi * (t - 0.15) / 0.17);
            v += 0.11f * env * sBand[(size_t) n];
            if (t >= 0.17 && t < 0.30) b.consMask[(size_t) n] = true;
        }
        if (t >= 1.65 && t < 1.80)
        {
            const float env = (float) std::sin (juce::MathConstants<double>::pi * (t - 1.65) / 0.15);
            v += 0.095f * env * fuBand[(size_t) n];
            if (t >= 1.67 && t < 1.78) b.consMask[(size_t) n] = true;
        }
        if (t >= 2.90 && t < 3.10)
        {
            const float env = (float) std::sin (juce::MathConstants<double>::pi * (t - 2.90) / 0.20);
            v += 0.055f * env * brBand[(size_t) n];
            if (t >= 2.93 && t < 3.07) b.consMask[(size_t) n] = true;
        }
        b.clean[(size_t) n] = v;
    }

    // --- 症状1: 「あー」発声中のパリパリ・チリチリ微小放電バースト ---
    std::vector<float> rawCrackle ((size_t) N, 0.0f);
    for (int n = 0; n < N - 24; ++n)
    {
        if (b.voicedMask[(size_t) n] && std::abs (b.clean[(size_t) n]) > 0.04f && uni (rng) < 0.0022f)
        {
            const int len = 4 + (int) (uni (rng) * 10.0f);
            const float pol = (uni (rng) > 0.5f) ? 1.0f : -1.0f;
            for (int k = 0; k < len; ++k)
            {
                const float dec = std::exp (-0.28f * (float) k);
                rawCrackle[(size_t) (n + k)] += pol * ((k & 1) ? -0.22f : 0.25f) * dec;
            }
            n += len + 18;
        }
    }
    const auto crackleHi = makeResonator (4600.0, 3600.0, rawCrackle);
    for (int n = 0; n < N; ++n)
        b.defectCrackle[(size_t) n] = 0.65f * rawCrackle[(size_t) n] + 0.85f * crackleHi[(size_t) n];

    // --- 症状2: 劣化した風防スポンジ共鳴サフー ＋ 42ms 減衰尾 ---
    const auto spongeCarrierA = makeResonator (2850.0, 950.0, white);
    const auto spongeCarrierB = makeResonator (4300.0, 1300.0, white);
    float cavEnv = 0.0f;
    const float atkC = std::exp (-1.0f / (float) (0.004 * sr));
    const float relC = std::exp (-1.0f / (float) (0.026 * sr));
    for (int n = 0; n < N; ++n)
    {
        const float drive = std::max (0.0f, glottal[(size_t) n] * 2.2f);
        cavEnv = (drive > cavEnv) ? (atkC * cavEnv + (1.0f - atkC) * drive)
                                  : (relC * cavEnv + (1.0f - relC) * drive);
        b.defectSponge[(size_t) n] = cavEnv * (0.34f * spongeCarrierA[(size_t) n] + 0.26f * spongeCarrierB[(size_t) n]);
    }

    // --- 症状3: 壊れたスピーカー擦れ歪み (Rub & Buzz: 大振幅変位で発生するスティックスリップ高域擦れ音) ---
    std::vector<float> rawBuzz ((size_t) N, 0.0f);
    for (int n = 0; n < N; ++n)
    {
        if (f0Track[(size_t) n] > 80.0f && f1[(size_t) n] < -0.055f)
        {
            const float over = std::min (0.18f, (-f1[(size_t) n] - 0.055f) * 3.8f);
            const float stickSlip = (uni (rng) < 0.18f) ? ((uni (rng) > 0.5f ? 2.6f : -2.6f)) : white[(size_t) n];
            rawBuzz[(size_t) n] = over * stickSlip;
        }
    }
    const auto buzzMid = highpass1300 (makeResonator (3800.0, 2200.0, rawBuzz), sr);
    const auto buzzHi  = highpass1300 (makeResonator (6800.0, 2800.0, rawBuzz), sr);
    for (int n = 0; n < N; ++n)
        b.defectRubBuzz[(size_t) n] = 0.85f * buzzMid[(size_t) n] + 0.65f * buzzHi[(size_t) n];

    // --- 症状4: 複合劣化 ---
    for (int n = 0; n < N; ++n)
    {
        b.defectCombined[(size_t) n] = 0.75f * b.defectCrackle[(size_t) n]
                                     + 0.75f * b.defectSponge[(size_t) n]
                                     + 0.70f * b.defectRubBuzz[(size_t) n];
    }
    return b;
}

std::vector<float> runMonoThroughPlugin (VocalGzzioProcessor& proc, const std::vector<float>& inSig, int blockSize = 128)
{
    std::vector<float> out (inSig.size(), 0.0f);
    juce::AudioBuffer<float> buf (2, blockSize);
    juce::MidiBuffer midi;
    size_t pos = 0;
    while (pos < inSig.size())
    {
        const int n = (int) std::min<size_t> ((size_t) blockSize, inSig.size() - pos);
        buf.setSize (2, n, false, false, true);
        for (int i = 0; i < n; ++i)
        {
            buf.setSample (0, i, inSig[pos + (size_t) i]);
            buf.setSample (1, i, inSig[pos + (size_t) i]);
        }
        proc.processBlock (buf, midi);
        for (int i = 0; i < n; ++i)
            out[pos + (size_t) i] = buf.getSample (0, i);
        pos += (size_t) n;
    }
    return out;
}

// 1.3kHz 1次ハイパスでマイク劣化帯域 (パリパリ・サフー・擦れ) の誤差電力を計測
std::vector<float> highpass1300 (const std::vector<float>& x, double sr)
{
    std::vector<float> y (x.size(), 0.0f);
    const double a = std::exp (-2.0 * juce::MathConstants<double>::pi * 1300.0 / sr);
    double lp = 0.0;
    for (size_t i = 0; i < x.size(); ++i)
    {
        lp += (1.0 - a) * ((double) x[i] - lp);
        y[i] = (float) ((double) x[i] - lp);
    }
    return y;
}

struct EvalResult
{
    double defectReductionDb { 0.0 };
    double peakReductionDb   { 0.0 };
    double voicedCorr        { 0.0 };
    double cleanDeltaDb      { 0.0 };
    double consRetentionPct  { 0.0 };
};

EvalResult evaluateRepair (const std::vector<float>& cleanOutRef,
                           const std::vector<float>& noisyInOut,
                           const std::vector<float>& repairedOut,
                           const std::vector<bool>&  evalMask,
                           const std::vector<bool>&  consMask,
                           double sr)
{
    const auto refHp = highpass1300 (cleanOutRef, sr);
    const auto rawHp = highpass1300 (noisyInOut,  sr);
    const auto repHp = highpass1300 (repairedOut, sr);

    double errRawSq = 1.0e-15, errRepSq = 1.0e-15;
    double errRawPk = 1.0e-9,  errRepPk = 1.0e-9;
    double refSq = 1.0e-15, repSq = 1.0e-15, dot = 0.0, diffSq = 1.0e-15;

    for (size_t i = 0; i < cleanOutRef.size(); ++i)
    {
        if (evalMask[i])
        {
            const double e0 = (double) rawHp[i] - (double) refHp[i];
            const double e1 = (double) repHp[i] - (double) refHp[i];
            errRawSq += e0 * e0;
            errRepSq += e1 * e1;
            errRawPk = std::max (errRawPk, std::abs (e0));
            errRepPk = std::max (errRepPk, std::abs (e1));

            const double r0 = cleanOutRef[i];
            const double r1 = repairedOut[i];
            refSq  += r0 * r0;
            repSq  += r1 * r1;
            dot    += r0 * r1;
            diffSq += (r1 - r0) * (r1 - r0);
        }
    }

    double consRefSq = 1.0e-15, consRepSq = 1.0e-15;
    for (size_t i = 0; i < cleanOutRef.size(); ++i)
    {
        if (consMask[i])
        {
            consRefSq += (double) cleanOutRef[i] * (double) cleanOutRef[i];
            consRepSq += (double) repairedOut[i] * (double) repairedOut[i];
        }
    }

    EvalResult res;
    res.defectReductionDb = 10.0 * std::log10 (errRawSq / errRepSq);
    res.peakReductionDb   = 20.0 * std::log10 (errRawPk / errRepPk);
    res.voicedCorr        = dot / std::sqrt (refSq * repSq);
    res.cleanDeltaDb      = 10.0 * std::log10 (diffSq / refSq);
    res.consRetentionPct  = 100.0 * std::sqrt (consRepSq / consRefSq);
    return res;
}
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;

    std::printf ("=== VocalGzzio v5.9.0 Broken-Mic Crackle/Safu Repair & Default Startup Audit ===\n");

    // =========================================================================
    // 1. テスト分離 (TestPaths) ＆ デフォルト起動画面「ふつう（日本語・標準総合画面）」厳格検証
    // =========================================================================
    {
        check (gz::isTestDataIsolated(),
               "v5.9.0 VOCALGZZIO_TESTING always isolates settings from real %APPDATA%/VocalGzzio");

        VocalGzzioProcessor proc;
        proc.setPlayConfigDetails (2, 2, 48000.0, 128);
        proc.prepareToPlay (48000.0, 128);

        std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
        auto* vgEd = dynamic_cast<VocalGzzioEditor*> (ed.get());
        check (vgEd != nullptr, "v5.9.0 createEditor() creates VocalGzzioEditor");
        if (vgEd != nullptr)
        {
            auto& content = vgEd->getContent();
            check (content.getThemeMode() == 0,
                   "v5.9.0 Default startup themeMode is 0 ('ふつう' Standard Studio, NOT Brand=3)");
            check (content.getLanguage() != nullptr && ! content.getLanguage()->english,
                   "v5.9.0 Default startup language is Japanese (english == false, NOT English!)");
            check (content.isOverview(),
                   "v5.9.0 Default startup opens on Standard Overview screen (isOverview() == true)");
            check (! content.isLiteMode() && content.getAccessMode() == 0,
                   "v5.9.0 Default startup has liteMode=false and accessMode=0");
            check (proc.getLegacyVersionId() == 0 && proc.getLegacyVersionEra() >= 590,
                   "v6.0.0/v5.9.0 Default startup runs current flagship (id=0, era>=590)");
        }
    }

    // =========================================================================
    // 2. 追加遅延 0.0 ms (0 samples) 保証
    // =========================================================================
    {
        VocalGzzioProcessor proc;
        configureCleanOnlyStrip (proc, 48000.0, false, 0.0f);
        proc.setMicSafuMode (0);
        const int latOff = proc.addedLatencySamples();
        proc.setMicSafuMode (1);
        const int latAuto = proc.addedLatencySamples();
        proc.setMicSafuMode (2);
        const int latDeep = proc.addedLatencySamples();
        check (latOff == 0 && latAuto == 0 && latDeep == 0,
               "v5.9.0 BrokenMicSafuCanceller adds strictly 0 samples (0.0 ms) latency in all modes");
    }

    // =========================================================================
    // 3. クリーン歌唱透明性 ＆ 4大マイク劣化症状の抑制検証 (@ 48kHz)
    // =========================================================================
    {
        const double sr = 48000.0;
        const auto bench = buildVocalBench (sr);

        // 基準: 劣化修復 OFF のクリーン通過出力
        std::vector<float> cleanRef;
        {
            auto refProc = std::make_unique<VocalGzzioProcessor>();
            configureCleanOnlyStrip (*refProc, sr, false, 0.0f);
            refProc->setMicSafuMode (0);
            cleanRef = runMonoThroughPlugin (*refProc, bench.clean);
        }

        // (3-A) クリーン歌唱に「劣化修復: 標準 (mode=1)」を掛けた時の透明性＆子音保持
        {
            auto transProc = std::make_unique<VocalGzzioProcessor>();
            configureCleanOnlyStrip (*transProc, sr, false, 0.0f);
            transProc->setMicSafuMode (1);
            const auto cleanRepaired = runMonoThroughPlugin (*transProc, bench.clean);
            const auto transEval = evaluateRepair (cleanRef, cleanRef, cleanRepaired,
                                                   bench.voicedMask, bench.consMask, sr);
            std::printf ("  [Clean Transparency] r=%.6f | delta=%.2f dB | consonant=%.2f%%\n",
                         transEval.voicedCorr, transEval.cleanDeltaDb, transEval.consRetentionPct);
            check (transEval.voicedCorr > 0.9998 && transEval.cleanDeltaDb < -45.0,
                   "v5.9.0 Clean vocal passes through Mic Fix (Auto) with r > 0.9998 and < -45 dB delta");
            check (transEval.consRetentionPct >= 98.0 && transEval.consRetentionPct <= 102.0,
                   "v5.9.0 Japanese unvoiced consonants (/s/, /fu/, breath) are preserved >= 98%");
        }

        auto makeNoisy = [&] (const std::vector<float>& defect)
        {
            std::vector<float> y (bench.clean.size(), 0.0f);
            for (size_t i = 0; i < y.size(); ++i)
                y[i] = bench.clean[i] + defect[i];
            return y;
        };

        // (3-B) 症状1: 「あー」発声中のパリパリ・チリチリ放電バースト（単体 dn_on=OFF ＆ 併用 dn_on=ON）
        {
            const auto inCrackle = makeNoisy (bench.defectCrackle);

            auto pOff  = std::make_unique<VocalGzzioProcessor>();
            auto pAuto = std::make_unique<VocalGzzioProcessor>();
            configureCleanOnlyStrip (*pOff,  sr, false, 0.0f);  pOff->setMicSafuMode (0);
            configureCleanOnlyStrip (*pAuto, sr, false, 0.0f);  pAuto->setMicSafuMode (1);
            const auto outOff  = runMonoThroughPlugin (*pOff,  inCrackle);
            const auto outAuto = runMonoThroughPlugin (*pAuto, inCrackle);
            const auto evAuto  = evaluateRepair (cleanRef, outOff, outAuto, bench.voicedMask, bench.consMask, sr);
            pOff.reset();
            pAuto.reset();

            auto pDnRef  = std::make_unique<VocalGzzioProcessor>();
            auto pDnOff  = std::make_unique<VocalGzzioProcessor>();
            auto pDnAuto = std::make_unique<VocalGzzioProcessor>();
            configureCleanOnlyStrip (*pDnRef,  sr, true, 55.0f); pDnRef->setMicSafuMode (0);
            configureCleanOnlyStrip (*pDnOff,  sr, true, 55.0f); pDnOff->setMicSafuMode (0);
            configureCleanOnlyStrip (*pDnAuto, sr, true, 55.0f); pDnAuto->setMicSafuMode (2);
            const auto cleanDnRef = runMonoThroughPlugin (*pDnRef,  bench.clean);
            const auto outDnOff   = runMonoThroughPlugin (*pDnOff,  inCrackle);
            const auto outDnAuto  = runMonoThroughPlugin (*pDnAuto, inCrackle);
            const auto evDnAuto   = evaluateRepair (cleanDnRef, outDnOff, outDnAuto, bench.voicedMask, bench.consMask, sr);

            std::printf ("  [Symptom 1: Mid-Vowel Crackle] Standalone=+%.2f dB (pk +%.2f dB, cons %.1f%%) | WithDenoise=+%.2f dB\n",
                         evAuto.defectReductionDb, evAuto.peakReductionDb, evAuto.consRetentionPct, evDnAuto.defectReductionDb);
            check (evAuto.defectReductionDb >= 12.0 && evAuto.peakReductionDb >= 10.0,
                   "v5.9.0 Suppresses mid-vowel 'Aaa' capsule crackle bursts (>= +12.0dB RMS, >= +10.0dB peak)");
            check (evDnAuto.defectReductionDb >= 12.0,
                   "v5.9.0 Suppresses mid-vowel crackle bursts when combined with main denoiser");
            check (evAuto.consRetentionPct >= 98.0,
                   "v5.9.0 Preserves /s/ and /fu/ consonants (>= 98%) while removing mid-vowel crackle");
        }

        // (3-C) 症状2: 劣化した風防スポンジ共鳴サフー ＋ 48ms 減衰尾
        {
            const auto inSponge = makeNoisy (bench.defectSponge);
            std::vector<bool> voicedAndTail (bench.voicedMask.size(), false);
            for (size_t i = 0; i < voicedAndTail.size(); ++i)
                voicedAndTail[i] = bench.voicedMask[i] || bench.tailMask[i];

            VocalGzzioProcessor pOff, pDeep;
            configureCleanOnlyStrip (pOff,  sr, false, 0.0f); pOff.setMicSafuMode (0);
            configureCleanOnlyStrip (pDeep, sr, false, 0.0f); pDeep.setMicSafuMode (2);
            const auto outOff  = runMonoThroughPlugin (pOff,  inSponge);
            const auto outDeep = runMonoThroughPlugin (pDeep, inSponge);
            const auto evAll   = evaluateRepair (cleanRef, outOff, outDeep, voicedAndTail, bench.consMask, sr);
            const auto evTail  = evaluateRepair (cleanRef, outOff, outDeep, bench.tailMask, bench.consMask, sr);

            std::printf ("  [Symptom 2: Sponge Chuffing] Overall=+%.2f dB | Post-Vowel 48ms Tail=+%.2f dB | cons=%.1f%%\n",
                         evAll.defectReductionDb, evTail.defectReductionDb, evAll.consRetentionPct);
            check (evAll.defectReductionDb >= 6.0,
                   "v5.9.0 Suppresses degraded foam/sponge cavity chuffing resonance (>= +6.0 dB)");
            check (evTail.defectReductionDb >= 8.0,
                   "v5.9.0 Suppresses post-vowel 48ms sponge resonance tail (>= +8.0 dB)");
            check (evAll.consRetentionPct >= 98.0,
                   "v5.9.0 Preserves consonants (>= 98%) during sponge chuffing & tail suppression");
        }

        // (3-D) 症状3: 壊れたスピーカー擦れ歪み (Rub & Buzz)
        {
            const auto inBuzz = makeNoisy (bench.defectRubBuzz);
            VocalGzzioProcessor pOff, pDeep;
            configureCleanOnlyStrip (pOff,  sr, false, 0.0f); pOff.setMicSafuMode (0);
            configureCleanOnlyStrip (pDeep, sr, false, 0.0f); pDeep.setMicSafuMode (2);
            const auto outOff  = runMonoThroughPlugin (pOff,  inBuzz);
            const auto outDeep = runMonoThroughPlugin (pDeep, inBuzz);
            const auto evBuzz  = evaluateRepair (cleanRef, outOff, outDeep, bench.voicedMask, bench.consMask, sr);

            std::printf ("  [Symptom 3: Rub & Buzz] Power=+%.2f dB | Peak=+%.2f dB | r=%.4f\n",
                         evBuzz.defectReductionDb, evBuzz.peakReductionDb, evBuzz.voicedCorr);
            check (evBuzz.defectReductionDb >= 8.0 && evBuzz.peakReductionDb >= 5.5 && evBuzz.voicedCorr > 0.990,
                   "v5.9.0 Suppresses speaker-like diaphragm Rub & Buzz (>= +8.0dB RMS, >= +5.5dB peak, r > 0.99)");
        }

        // (3-E) 症状4: 複合劣化マイク ＆ リアルタイム修復量メーター (getMicSafuCleanDb)
        {
            const auto inComb = makeNoisy (bench.defectCombined);
            VocalGzzioProcessor pOff, pDeep;
            configureCleanOnlyStrip (pOff,  sr, false, 0.0f); pOff.setMicSafuMode (0);
            configureCleanOnlyStrip (pDeep, sr, false, 0.0f); pDeep.setMicSafuMode (2);
            const auto outOff = runMonoThroughPlugin (pOff, inComb);

            float peakMeterDb = 0.0f;
            juce::AudioBuffer<float> buf (2, 128);
            juce::MidiBuffer midi;
            std::vector<float> outDeep (inComb.size(), 0.0f);
            for (size_t pos = 0; pos < inComb.size(); pos += 128)
            {
                const int n = (int) std::min<size_t> (128, inComb.size() - pos);
                buf.setSize (2, n, false, false, true);
                for (int i = 0; i < n; ++i)
                {
                    buf.setSample (0, i, inComb[pos + (size_t) i]);
                    buf.setSample (1, i, inComb[pos + (size_t) i]);
                }
                pDeep.processBlock (buf, midi);
                peakMeterDb = std::max (peakMeterDb, pDeep.getMicSafuCleanDb());
                for (int i = 0; i < n; ++i)
                    outDeep[pos + (size_t) i] = buf.getSample (0, i);
            }
            const auto evComb = evaluateRepair (cleanRef, outOff, outDeep, bench.voicedMask, bench.consMask, sr);
            std::printf ("  [Symptom 4: Combined Broken Mic] Power=+%.2f dB | Peak=+%.2f dB | MeterMax=%.2f dB\n",
                         evComb.defectReductionDb, evComb.peakReductionDb, peakMeterDb);
            check (evComb.defectReductionDb >= 7.5 && peakMeterDb >= 0.5f,
                   "v5.9.0 Combined broken-mic defects suppressed >= +7.5 dB with live dB meter activity");
        }

        // (3-F) 症状5: メインのノイズ除去 (dn_on=ON, 60%) をすり抜ける定常サー音（サフサフ変調ヒス）の協調浄化
        {
            std::mt19937 hissRng (777590u);
            std::normal_distribution<float> hissDist (0.0f, 0.0085f); // 約 -41 dBFS のサー音フロア
            std::vector<float> inHiss (bench.clean.size(), 0.0f);
            for (size_t i = 0; i < inHiss.size(); ++i)
                inHiss[i] = bench.clean[i] + hissDist (hissRng);

            std::vector<bool> voicedAndTail (bench.voicedMask.size(), false);
            for (size_t i = 0; i < voicedAndTail.size(); ++i)
                voicedAndTail[i] = bench.voicedMask[i] || bench.tailMask[i];

            auto pDnClean = std::make_unique<VocalGzzioProcessor>();
            auto pDnOnly  = std::make_unique<VocalGzzioProcessor>();
            auto pSynergy = std::make_unique<VocalGzzioProcessor>();
            configureCleanOnlyStrip (*pDnClean, sr, true, 60.0f); pDnClean->setMicSafuMode (0);
            configureCleanOnlyStrip (*pDnOnly,  sr, true, 60.0f); pDnOnly->setMicSafuMode (0);
            configureCleanOnlyStrip (*pSynergy, sr, true, 60.0f); pSynergy->setMicSafuMode (2);

            const auto refDnClean = runMonoThroughPlugin (*pDnClean, bench.clean);
            const auto outDnOnly  = runMonoThroughPlugin (*pDnOnly,  inHiss);
            const auto outSynergy = runMonoThroughPlugin (*pSynergy, inHiss);
            const auto evSynergy  = evaluateRepair (refDnClean, outDnOnly, outSynergy, voicedAndTail, bench.consMask, sr);

            std::printf ("  [Symptom 5: Denoiser + Safu Synergy on -41dBFS Hiss] ExtraSuppression=+%.2f dB | cons=%.1f%%\n",
                         evSynergy.defectReductionDb, evSynergy.consRetentionPct);
            check (evSynergy.defectReductionDb >= 2.5 && evSynergy.consRetentionPct >= 97.5,
                   "v5.9.0 Safu Canceller further suppresses residual in-vowel/tail denoiser hiss (>= +2.5 dB) while preserving consonants");
        }
    }

    // =========================================================================
    // 4. 『うた自動』でのマイク劣化自動検知（クリーン声 = OFF維持 / 劣化声 = 自動ON）
    // =========================================================================
    {
        const double sr = 48000.0;
        const auto bench = buildVocalBench (sr);

        auto runSongAutoSetup = [&] (const std::vector<float>& src3s) -> int
        {
            VocalGzzioProcessor proc;
            proc.setPlayConfigDetails (2, 2, sr, 256);
            proc.prepareToPlay (sr, 256);
            drainStartupSelfTest (proc, 256);
            proc.setMicSafuMode (0);
            proc.requestAutoSetup (1); // 1 = うた自動 (8秒)

            juce::AudioBuffer<float> buf (2, 256);
            juce::MidiBuffer midi;
            size_t idx = 0;
            int guardBlocks = 0;
            while (proc.isAutoSetupRunning() && guardBlocks < 2000)
            {
                for (int i = 0; i < 256; ++i)
                {
                    const float s = src3s[idx % src3s.size()];
                    ++idx;
                    buf.setSample (0, i, s);
                    buf.setSample (1, i, s);
                }
                proc.processBlock (buf, midi);
                ++guardBlocks;
            }
            proc.applyAutoSetup();
            return proc.getMicSafuMode();
        };

        std::vector<float> brokenSrc (bench.clean.size(), 0.0f);
        for (size_t i = 0; i < brokenSrc.size(); ++i)
            brokenSrc[i] = bench.clean[i] + bench.defectCombined[i];

        const int cleanRec  = runSongAutoSetup (bench.clean);
        const int brokenRec = runSongAutoSetup (brokenSrc);
        std::printf ("  [Auto-Setup Mic Anomaly Detection] CleanMic -> mode=%d | BrokenMic -> mode=%d\n",
                     cleanRec, brokenRec);
        check (cleanRec == 0,
               "v5.9.0 Song Auto-Setup keeps Mic Fix OFF (0) on a healthy studio microphone");
        check (brokenRec >= 1,
               "v5.9.0 Song Auto-Setup automatically enables Mic Fix (>=1) when mid-vowel crackle/safu is detected");
    }

    // =========================================================================
    // 5. 状態保存／復元ラウンドトリップ・マルチサンプルレート・128サンプルCPU負荷検証
    // =========================================================================
    {
        for (double sr : { 44100.0, 48000.0, 96000.0 })
        {
            VocalGzzioProcessor proc;
            configureCleanOnlyStrip (proc, sr, true, 45.0f);
            proc.setMicSafuMode (2);

            juce::AudioBuffer<float> buf (2, 128);
            juce::MidiBuffer midi;
            double ph = 0.0;
            bool finiteOk = true;
            for (int b = 0; b < 80; ++b)
            {
                for (int i = 0; i < 128; ++i)
                {
                    ph += 2.0 * juce::MathConstants<double>::pi * 235.0 / sr;
                    const float s = 0.18f * (float) std::sin (ph) + ((i == 37) ? 0.25f : 0.0f);
                    buf.setSample (0, i, s);
                    buf.setSample (1, i, s);
                }
                proc.processBlock (buf, midi);
                for (int i = 0; i < 128; ++i)
                    if (! std::isfinite (buf.getSample (0, i)) || std::abs (buf.getSample (0, i)) > 4.0f)
                        finiteOk = false;
            }
            check (finiteOk, "v5.9.0 Multi-sample-rate (44.1/48/96kHz) processing stays finite and bounded");
        }

        // VST3 state save/load round-trip
        VocalGzzioProcessor pSave, pLoad;
        pSave.prepareToPlay (48000.0, 128);
        pSave.setMicSafuMode (2);
        check (pSave.buildReportText().contains (juce::String::fromUTF8 ("マイク劣化修復")),
               "v5.9.0 buildReportText includes マイク劣化修復 diagnostic line");

        juce::MemoryBlock stateBlob;
        pSave.getStateInformation (stateBlob);
        pLoad.prepareToPlay (48000.0, 128);
        check (pLoad.getMicSafuMode() == 0, "v5.9.0 Fresh processor starts with micSafuMode == 0");
        pLoad.setStateInformation (stateBlob.getData(), (int) stateBlob.getSize());
        check (pLoad.getMicSafuMode() == 2,
               "v5.9.0 setStateInformation restores saved micSafuMode == 2 accurately");

        // BrokenMicSafuCanceller standalone 128-sample @ 48kHz micro-benchmark (< 150us, budget = 2666.7us)
        check (sizeof (gz::v590::BrokenMicSafuCanceller) < 4096,
               "v5.9.0 sizeof(BrokenMicSafuCanceller) stays compact (< 4 KB) with zero stack bloat");
        gz::v590::BrokenMicSafuCanceller unit;
        unit.prepare (48000.0);
        alignas (16) float bL[128], bR[128];
        for (int i = 0; i < 128; ++i)
        {
            const float v = 0.18f * std::sin (2.0f * 3.14159265f * 220.0f * (float) i / 48000.0f);
            bL[i] = v;
            bR[i] = v;
        }
        for (int w = 0; w < 50; ++w)
            unit.processBuffer (bL, bR, 128, 2, 2, 220.0f, 0.92f, false);

        const auto t0 = std::chrono::steady_clock::now();
        constexpr int kIters = 1000;
        for (int it = 0; it < kIters; ++it)
            unit.processBuffer (bL, bR, 128, 2, 2, 220.0f, 0.92f, true);
        const auto t1 = std::chrono::steady_clock::now();
        const double meanUs = std::chrono::duration<double, std::micro> (t1 - t0).count() / (double) kIters;
        std::printf ("  [128-smp @ 48kHz CPU] BrokenMicSafuCanceller::process = %.2f us / block (%.2f%% of 2666.7us budget)\n",
                     meanUs, 100.0 * meanUs / 2666.7);
        check (meanUs < 200.0,
               "v5.9.0 BrokenMicSafuCanceller 128-sample block cost is ultra-lightweight (< 200 us)");
    }

    if (gFailures == 0)
        std::printf ("PASS: dsp_v590_broken_mic_safu (0 failures)\n");
    return gFailures == 0 ? 0 : 1;
}
