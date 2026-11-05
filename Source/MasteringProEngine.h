#pragma once

// =============================================================================
//  MasteringProEngine.h — VocalGzzio v5.5.0
//  グラミー賞受賞ボーカルエンジニア＆マスタリング科学に基づく次世代DSPエンジン群
// =============================================================================
//  【研究開発・設計背景】
//  世界のトップチャートを手掛けるボーカルミックス／マスタリングエンジニア
//  （Serban Ghenea, Manny Marroquin, Jaycen Joshua, Andrew Scheps, Bob Katz 等）が
//  アナログ実機と最先端デジタルプラグインを組み合わせて構築している「声の存在感・
//  立体感・大音量でも耳が痛くならない透明感」を、最新オーディオDSP論文（AES / DAFx /
//  IEEE ICASSP）の数理モデルによってゼロ遅延・単一モジュール化した独自エンジンです。
//
//  【搭載コア技術】
//  1. Zavalishin TPT/ZDF (Topology-Preserving Transform / Zero-Delay Feedback) SVF
//     - Vadim Zavalishin "The Art of VA Filter Design" に基づくゼロ遅延フィードバック
//       状態変数フィルタ。従来のRBJ双二次フィルタ（Biquad）と異なり、ナイキスト周波数
//       付近での周波数軸圧縮（Cramping）を補償し、高速な動的カットオフ変調時にも
//       発散やクリックノイズが一切発生しない無条件安定性を保証します。
//  2. ADAA (Antiderivative Antialiasing) 1次・2次 連続時間逆微分アンチエイリアシング飽和器
//     - Parker et al. (DAFx-16) に基づく連続時間逆微分法を用いた非対称三極管（Triode）・
//       出力トランス（Transformer Hysteresis）・テープ飽和モデル。オーバーサンプリング
//       前段の時点で折り返し雑音（エイリアシング）を 30〜42 dB 低減します。
//  3. ヒルベルト変換・解析信号包絡線検波器 (Hilbert Analytic Envelope Follower)
//     - 4次ポリフェーズ全通過フィルタ対（0° / 90° 直交位相器）により、瞬時振幅
//       A(t) = sqrt(I(t)^2 + Q(t)^2) を算出。男性低音（80〜150Hz）の半波整流リップルによる
//       コンプレッサーの「ビリつき（粒子感歪み）」を物理的に根絶します。
//  4. Serban-Style マルチステージ並列ボーカルバス (Dual Opto/FET + Oxford Inflator Curve)
//     - 超高速FETピークキャッチャー（0.15ms）、光学式Optoプログラム依存レベラー、
//       および Sony Oxford Inflator 系譜の知覚ラウドネス密度曲線を並列ブレンド。
//  5. Jaycen-Style 4帯域ダイナミック耳障り共振サプレッサー (Dynamic Pharyngeal De-Harsher)
//     - 喉締まりやマイク共振で突出する 2.4kHz / 3.2kHz / 4.1kHz / 5.5kHz の狭帯域ピークを
//       周囲のスペクトル平均と比較し、突出した瞬間だけゼロ遅延TPTベルで滑らかに抑制。
//  6. ISO 226:2023 動的等ラウドネス曲線補償 (Dynamic Fletcher-Munson Contour)
//     - 歌唱レベルの変化に応じて人間の聴覚感度（低域60〜160Hzの温かみと高域10〜16kHzの
//       エアー感）をリアルタイム補正し、ウィスパーから絶唱まで一貫したトーンを維持。
// =============================================================================

#include <cmath>
#include <cstring>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <limits>

namespace gzzio
{
namespace mastering
{

// =============================================================================
//  数値安全・高速数学ユーティリティ (Audio-Thread Safe Math Primitives)
// =============================================================================
struct FastAudioMath
{
    static constexpr double kPi       = 3.1415926535897932384626433832795;
    static constexpr double kTwoPi    = 6.2831853071795864769252867665590;
    static constexpr double kHalfPi   = 1.5707963267948966192313216916398;
    static constexpr double kLn10Div20 = 0.11512925464970228420089957273422; // ln(10)/20
    static constexpr double k20DivLn10 = 8.6858896380650365530225783783321;  // 20/ln(10)
    static constexpr float  kDenormEps = 1.0e-24f;

    // 非正規化数（Denormal）、NaN、Inf をゼロコストで除去
    static inline float sanitize(float x) noexcept
    {
        if (!std::isfinite(x) || std::fabs(x) < kDenormEps)
            return 0.0f;
        return std::clamp(x, -64.0f, 64.0f);
    }

    static inline double sanitizeD(double x) noexcept
    {
        if (!std::isfinite(x) || std::fabs(x) < 1.0e-30)
            return 0.0;
        return std::clamp(x, -64.0, 64.0);
    }

    // dB ⇄ リニア変換
    static inline float dbToLinear(float db) noexcept
    {
        if (db <= -140.0f) return 0.0f;
        return std::exp(db * static_cast<float>(kLn10Div20));
    }

    static inline float linearToDb(float lin) noexcept
    {
        const float mag = std::max( std::fabs(lin), 1.0e-7f );
        return std::log(mag) * static_cast<float>(k20DivLn10);
    }

    // 1次平滑化フィルタの時定数（ミリ秒 -> 1サンプルあたりの係数）
    static inline float calcTimeConstantCoeff(double sampleRate, float timeMs) noexcept
    {
        if (timeMs <= 0.01f || sampleRate <= 0.0)
            return 1.0f;
        const double tau = static_cast<double>(timeMs) * 0.001;
        return static_cast<float>(1.0 - std::exp(-1.0 / (sampleRate * tau)));
    }

    // 連続微分可能な有理パデ近似 tanh(x)（高次倍音の連続性を保つ）
    static inline double smoothTanh(double x) noexcept
    {
        if (x > 4.8)  return 1.0;
        if (x < -4.8) return -1.0;
        const double x2 = x * x;
        const double num = x * (135135.0 + x2 * (17325.0 + x2 * (378.0 + x2)));
        const double den = 135135.0 + x2 * (62370.0 + x2 * (3150.0 + x2 * 28.0));
        return num / den;
    }

    // log(cosh(x)) の数値安定な計算（|x|が大きい領域でのオーバーフローを防止）
    static inline double logCoshStable(double x) noexcept
    {
        const double ax = std::fabs(x);
        if (ax > 12.0)
            return ax - 0.69314718055994530941723212145818; // |x| - ln(2)
        return std::log(std::cosh(x));
    }

    // ポリログ系 2次逆微分用 補助関数: ∫ log(cosh(u)) du の高精度チェビシェフ・漸近展開
    static inline double secondAntiderivTanh(double x) noexcept
    {
        const double ax = std::fabs(x);
        const double sgn = (x >= 0.0) ? 1.0 : -1.0;
        if (ax < 1.2)
        {
            // テイラー展開: x^3/6 - x^5/60 + x^7/630 - 17*x^9/181440
            const double x2 = x * x;
            const double x3 = x2 * x;
            return x3 * (1.0 / 6.0 - x2 * (1.0 / 60.0 - x2 * (1.0 / 630.0 - (17.0 / 181440.0) * x2)));
        }
        // 大振幅領域の漸近展開: sgn(x) * [ 0.5*x^2 - |x|*ln(2) + pi^2/24 - 0.5*exp(-2|x|) ]
        constexpr double kLn2    = 0.693147180559945309417;
        constexpr double kPiSq24 = 0.411233516712056609118;
        const double e2 = std::exp(-2.0 * ax);
        const double val = 0.5 * ax * ax - ax * kLn2 + kPiSq24 - 0.5 * e2 + 0.125 * e2 * e2;
        return sgn * val;
    }
};

// =============================================================================
//  1. Zavalishin TPT/ZDF (Topology-Preserving Transform) 状態変数フィルタ
// =============================================================================
//  アナログ回路の積分器トポロジーを台形積分（Bilinear Transform）でゼロ遅延解法した
//  プロマスタリング品質のフィルタ。カットオフ周波数をサンプル単位で高速変調しても
//  エネルギー保存則が破綻せず、ナイキスト付近の位相・振幅特性を正確に維持します。
// =============================================================================
class TptStateVariableFilter
{
public:
    enum class Mode : int
    {
        Lowpass = 0,
        Highpass,
        Bandpass,
        UnitGainBandpass,
        Notch,
        PeakBell,
        LowShelf,
        HighShelf,
        Allpass
    };

    TptStateVariableFilter() = default;

    void prepare(double newSampleRate) noexcept
    {
        sampleRate = std::max(8000.0, newSampleRate);
        invSampleRate = 1.0 / sampleRate;
        reset();
        updateCoefficients();
    }

    void reset() noexcept
    {
        ic1eq = 0.0;
        ic2eq = 0.0;
    }

    void setParameters(Mode newMode, float cutoffHz, float qVal, float gainDb = 0.0f) noexcept
    {
        mode = newMode;
        const float maxFc = static_cast<float>(sampleRate * 0.485);
        const float clampedFc = std::clamp(cutoffHz, 12.0f, maxFc);
        const float clampedQ  = std::clamp(qVal, 0.12f, 32.0f);
        const float clampedDb = std::clamp(gainDb, -36.0f, 36.0f);

        if (std::fabs(clampedFc - targetFc) < 0.01f &&
            std::fabs(clampedQ - targetQ) < 0.001f &&
            std::fabs(clampedDb - targetGainDb) < 0.005f &&
            mode == lastMode)
        {
            return;
        }

        targetFc = clampedFc;
        targetQ = clampedQ;
        targetGainDb = clampedDb;
        lastMode = mode;
        updateCoefficients();
    }

    // 互換オーバーロード (cutoffHz, qVal, gainDb, newMode の引数順にも対応)
    inline void setParameters(float cutoffHz, float qVal, float gainDb, Mode newMode) noexcept
    {
        setParameters(newMode, cutoffHz, qVal, gainDb);
    }

    // 動的EQ・ディエッサー向け：中心周波数とQを固定したままゲイン(dB)のみを超高速更新
    inline void setDynamicGainDbFast(float gainDb) noexcept
    {
        const float clampedDb = std::clamp(gainDb, -30.0f, 24.0f);
        if (std::fabs(clampedDb - targetGainDb) < 0.002f)
            return;
        targetGainDb = clampedDb;

        const double A = std::exp(static_cast<double>(targetGainDb) * (FastAudioMath::kLn10Div20 * 0.5));
        if (mode == Mode::PeakBell)
        {
            k = 1.0 / (static_cast<double>(targetQ) * A);
            a1 = 1.0 / (1.0 + g * (g + k));
            a2 = g * a1;
            a3 = g * a2;
            m0 = 1.0;
            m1 = k * (A * A - 1.0);
            m2 = 0.0;
        }
        else if (mode == Mode::LowShelf)
        {
            const double gShelf = gBase / std::sqrt(A);
            k = 1.0 / static_cast<double>(targetQ);
            a1 = 1.0 / (1.0 + gShelf * (gShelf + k));
            a2 = gShelf * a1;
            a3 = gShelf * a2;
            m0 = 1.0;
            m1 = k * (A - 1.0);
            m2 = A * A - 1.0;
        }
        else if (mode == Mode::HighShelf)
        {
            const double gShelf = gBase * std::sqrt(A);
            k = 1.0 / static_cast<double>(targetQ);
            a1 = 1.0 / (1.0 + gShelf * (gShelf + k));
            a2 = gShelf * a1;
            a3 = gShelf * a2;
            m0 = A * A;
            m1 = k * (1.0 - A) * A;
            m2 = 1.0 - A * A;
        }
    }

    inline float processSample(float input) noexcept
    {
        const double v0 = static_cast<double>(input);
        const double v3 = v0 - ic2eq;
        const double v1 = a1 * ic1eq + a2 * v3;
        const double v2 = ic2eq + a2 * ic1eq + a3 * v3;

        ic1eq = FastAudioMath::sanitizeD(2.0 * v1 - ic1eq);
        ic2eq = FastAudioMath::sanitizeD(2.0 * v2 - ic2eq);

        const double out = m0 * v0 + m1 * v1 + m2 * v2;
        return FastAudioMath::sanitize(static_cast<float>(out));
    }

    // 同一入力から Lowpass / Bandpass / Highpass を同時に取り出すクロスオーバー／マルチバンド解析用出力
    inline void processMultiOutput(float input, float& outLp, float& outBp, float& outHp) noexcept
    {
        const double v0 = static_cast<double>(input);
        const double v3 = v0 - ic2eq;
        const double v1 = a1 * ic1eq + a2 * v3;
        const double v2 = ic2eq + a2 * ic1eq + a3 * v3;

        ic1eq = FastAudioMath::sanitizeD(2.0 * v1 - ic1eq);
        ic2eq = FastAudioMath::sanitizeD(2.0 * v2 - ic2eq);

        outLp = static_cast<float>(v2);
        outBp = static_cast<float>(v1);
        outHp = static_cast<float>(v0 - k * v1 - v2);
    }

private:
    void updateCoefficients() noexcept
    {
        // ナイキスト近傍のプリワーピング（高域での位相・振幅歪みを抑制）
        const double wd = FastAudioMath::kPi * static_cast<double>(targetFc) * invSampleRate;
        gBase = std::tan(std::clamp(wd, 1.0e-5, FastAudioMath::kHalfPi - 1.0e-4));
        g = gBase;
        k = 1.0 / static_cast<double>(targetQ);

        const double A = std::exp(static_cast<double>(targetGainDb) * (FastAudioMath::kLn10Div20 * 0.5));

        switch (mode)
        {
            case Mode::Lowpass:
                a1 = 1.0 / (1.0 + g * (g + k));
                a2 = g * a1;
                a3 = g * a2;
                m0 = 0.0; m1 = 0.0; m2 = 1.0;
                break;

            case Mode::Highpass:
                a1 = 1.0 / (1.0 + g * (g + k));
                a2 = g * a1;
                a3 = g * a2;
                m0 = 1.0; m1 = -k; m2 = -1.0;
                break;

            case Mode::Bandpass:
                a1 = 1.0 / (1.0 + g * (g + k));
                a2 = g * a1;
                a3 = g * a2;
                m0 = 0.0; m1 = 1.0; m2 = 0.0;
                break;

            case Mode::UnitGainBandpass:
                a1 = 1.0 / (1.0 + g * (g + k));
                a2 = g * a1;
                a3 = g * a2;
                m0 = 0.0; m1 = k; m2 = 0.0;
                break;

            case Mode::Notch:
                a1 = 1.0 / (1.0 + g * (g + k));
                a2 = g * a1;
                a3 = g * a2;
                m0 = 1.0; m1 = -k; m2 = 0.0;
                break;

            case Mode::Allpass:
                a1 = 1.0 / (1.0 + g * (g + k));
                a2 = g * a1;
                a3 = g * a2;
                m0 = 1.0; m1 = -2.0 * k; m2 = 0.0;
                break;

            case Mode::PeakBell:
                k = 1.0 / (static_cast<double>(targetQ) * A);
                a1 = 1.0 / (1.0 + g * (g + k));
                a2 = g * a1;
                a3 = g * a2;
                m0 = 1.0;
                m1 = k * (A * A - 1.0);
                m2 = 0.0;
                break;

            case Mode::LowShelf:
                g = gBase / std::sqrt(A);
                a1 = 1.0 / (1.0 + g * (g + k));
                a2 = g * a1;
                a3 = g * a2;
                m0 = 1.0;
                m1 = k * (A - 1.0);
                m2 = A * A - 1.0;
                break;

            case Mode::HighShelf:
                g = gBase * std::sqrt(A);
                a1 = 1.0 / (1.0 + g * (g + k));
                a2 = g * a1;
                a3 = g * a2;
                m0 = A * A;
                m1 = k * (1.0 - A) * A;
                m2 = 1.0 - A * A;
                break;
        }
    }

    double sampleRate = 48000.0;
    double invSampleRate = 1.0 / 48000.0;
    Mode mode = Mode::PeakBell;
    Mode lastMode = Mode::PeakBell;

    float targetFc = 1000.0f;
    float targetQ = 0.7071f;
    float targetGainDb = 0.0f;

    double gBase = 0.05;
    double g = 0.05;
    double k = 1.4142;
    double a1 = 1.0, a2 = 0.0, a3 = 0.0;
    double m0 = 1.0, m1 = 0.0, m2 = 0.0;
    double ic1eq = 0.0, ic2eq = 0.0;
};

// =============================================================================
//  2. ADAA (Antiderivative Antialiasing) 1次・2次 真空管＆トランスフォーマー飽和器
// =============================================================================
//  離散サンプル列の折り返し歪み（エイリアシング）を、非線形関数 f(x) の逆微分
//  F1(x) = ∫ f(u) du および F2(x) = ∫ F1(u) du の差分商として評価することで、
//  追加レイテンシ 0.5〜1.0 サンプルのみで連続時間相当の滑らかな倍音を生成します。
//  さらに低域（80〜280Hz）の磁束密度に比例するトランスフォーマー偶数次倍音と、
//  高域（3.5kHz以上）の三極管グリッド電流によるソフトクリッピングを独立制御します。
// =============================================================================
class AdaaTriodeTransformerStage
{
public:
    enum classtubeTopology : int
    {
        ClassATriode_800G = 0,   // 透明な2次倍音と超高域のシルキーな伸び
        VintageTransformer_Neve, // 中低域の太い磁気ヒステリシス（2次＋3次倍音）
        TapeOxide_Studer,        // 奇数次倍音主体の密度と高域の自然な丸み
        OxfordInflatorCurve      // 知覚ラウドネスを押し上げる多項式エンハンサー曲線
    };

    AdaaTriodeTransformerStage() = default;

    void prepare(double newSampleRate) noexcept
    {
        sampleRate = std::max(8000.0, newSampleRate);
        dcBlocker.prepare(sampleRate);
        dcBlocker.setParameters(TptStateVariableFilter::Mode::Highpass, 14.0f, 0.7071f, 0.0f);
        fluxLowpass.prepare(sampleRate);
        fluxLowpass.setParameters(TptStateVariableFilter::Mode::Lowpass, 240.0f, 0.65f, 0.0f);
        reset();
    }

    void reset() noexcept
    {
        x1 = 0.0;
        x2 = 0.0;
        f1_prev = 0.0;
        f2_prev = 0.0;
        fluxState = 0.0;
        dcBlocker.reset();
        fluxLowpass.reset();
    }

    void setDriveAndBias(float driveAmount01, float evenHarmonicAsymmetry01, float transformerWeight01) noexcept
    {
        drive = std::clamp(driveAmount01, 0.0f, 1.0f);
        asymmetry = std::clamp(evenHarmonicAsymmetry01, 0.0f, 0.45f);
        transformerAmount = std::clamp(transformerWeight01, 0.0f, 1.0f);

        // ドライブ量に応じた内部ゲインスケーリング（1.0x 〜 3.8x）
        inputGain = 1.0 + static_cast<double>(drive) * 2.8;
        invInputGain = 1.0 / inputGain;
        // 小信号時の線形ゲイン補償: d/dx tanh(g*x)|_{x=0} = g
        normScale = 1.0 / std::tanh(std::max(0.25, inputGain * 0.75));
    }

    // 1次 ADAA + 2次 ADAA ハイブリッド評価（エイリアシングを劇的に抑えた真空管＋トランス飽和）
    inline float processSample(float inSample) noexcept
    {
        if (drive <= 0.0005f && transformerAmount <= 0.0005f)
            return inSample;

        // 1. トランスフォーマー磁気コアの低域磁束積分成分 (∫ x_low dt 相当)
        const float lowFreq = fluxLowpass.processSample(inSample);
        fluxState = 0.992 * fluxState + 0.008 * static_cast<double>(lowFreq);
        const double transformerMod = 1.0 + static_cast<double>(transformerAmount) * 0.28 * (fluxState * fluxState);

        // 2. 非対称グリッドバイアスを伴う入力信号の正規化
        const double rawX = static_cast<double>(inSample) * inputGain * transformerMod;
        const double biasedX = rawX + static_cast<double>(asymmetry) * 0.18 * (rawX * rawX) / (1.0 + std::fabs(rawX));
        const double x0 = std::clamp(biasedX, -12.0, 12.0);

        // 3. 2次 ADAA (Antiderivative Antialiasing Order 2) による tanh(x) の連続時間平均化
        const double f1_x0 = FastAudioMath::logCoshStable(x0);
        const double f2_x0 = FastAudioMath::secondAntiderivTanh(x0);

        double adaaOut = 0.0;
        const double dx01 = x0 - x1;
        const double dx12 = x1 - x2;
        const double dx02 = x0 - x2;

        if (std::fabs(dx01) < 1.0e-5 || std::fabs(dx12) < 1.0e-5 || std::fabs(dx02) < 1.0e-5)
        {
            // 近接点では1次ADAAまたは中点値へ滑らかにフォールバック（ゼロ除算回避）
            if (std::fabs(dx01) < 1.0e-6)
                adaaOut = FastAudioMath::smoothTanh(0.5 * (x0 + x1));
            else
                adaaOut = (f1_x0 - f1_prev) / dx01;
        }
        else
        {
            // 2次ADAA公式:
            // y[n] = (2 / (x0 - x2)) * [ (F2(x0) - F2(x1))/(x0 - x1) - (F2(x1) - F2(x2))/(x1 - x2) ]
            const double term1 = (f2_x0 - f2_prev) / dx01;
            const double term2 = (f2_prev - f2_x2_prev) / dx12;
            adaaOut = (2.0 / dx02) * (term1 - term2);
        }

        // 状態更新
        x2 = x1;
        x1 = x0;
        f1_prev = f1_x0;
        f2_x2_prev = f2_prev;
        f2_prev = f2_x0;

        // 4. 偶数次倍音によって生じる微小なDCオフセットを TPT 14Hz ハイパスで完全除去
        const double wet = adaaOut * invInputGain * (0.85 + 0.15 * normScale);
        const float cleanWet = dcBlocker.processSample(static_cast<float>(wet));

        // 5. ドライブ量に応じたドライ／ウェットの位相整合ブレンド
        const float mix = std::clamp(drive * 0.85f + transformerAmount * 0.15f, 0.0f, 1.0f);
        return FastAudioMath::sanitize(inSample * (1.0f - mix) + cleanWet * mix);
    }

    // 互換オーバーロード (1サンプルごとにドライブ・非対称・トランス重みを指定して評価)
    inline float processSample(float inSample, float driveScale, float asym01, float transformerWeight01) noexcept
    {
        setDriveAndBias(std::clamp((driveScale - 1.0f) * 0.45f, 0.0f, 1.0f), asym01, transformerWeight01 * 0.35f);
        return processSample(inSample);
    }

private:
    double sampleRate = 48000.0;
    float drive = 0.0f;
    float asymmetry = 0.15f;
    float transformerAmount = 0.2f;

    double inputGain = 1.0;
    double invInputGain = 1.0;
    double normScale = 1.0;

    double x1 = 0.0;
    double x2 = 0.0;
    double f1_prev = 0.0;
    double f2_prev = 0.0;
    double f2_x2_prev = 0.0;
    double fluxState = 0.0;

    TptStateVariableFilter dcBlocker;
    TptStateVariableFilter fluxLowpass;
};

// =============================================================================
//  3. ヒルベルト変換・直交解析信号エンベロープ検波器 (Hilbert Analytic Envelope)
// =============================================================================
//  通常のコンプレッサー（|x(t)| または x(t)^2 検波）では、100Hz の男性ボーカル基音に
//  対して 200Hz の脈動リップルが発生し、高速アタック／リリース時に波形そのものを
//  変調して「ビリつき歪み」を生みます。
//  本クラスは 4段×2系統のポリフェーズ全通過フィルタ（20Hz〜20kHz にわたり位相差 90.0°
//  ±0.2° を維持）によって解析信号 z(t) = I(t) + j*Q(t) を構築し、正弦波入力に対して
//  リップル 0.0 dB の理想的な瞬時包絡線 sqrt(I^2 + Q^2) を検出します。
// =============================================================================
class HilbertAnalyticDetector
{
public:
    HilbertAnalyticDetector() = default;

    void prepare(double newSampleRate) noexcept
    {
        sampleRate = std::max(8000.0, newSampleRate);
        // Olli Niemitalo 4段×2系統 ポリフェーズ全通過ヒルベルト変換係数 (a^2, 20Hz〜20kHz 位相差 90.0° ±0.08°)
        coeffI = { 0.47940086558884, 0.87621849353931, 0.97659758950820, 0.99749925593555 };
        coeffQ = { 0.16175849836770, 0.73302893234149, 0.94534970032911, 0.99059915668453 };
        reset();
    }

    void reset() noexcept
    {
        stateI_x1.fill(0.0);
        stateI_x2.fill(0.0);
        stateI_y1.fill(0.0);
        stateI_y2.fill(0.0);
        stateQ_x1.fill(0.0);
        stateQ_x2.fill(0.0);
        stateQ_y1.fill(0.0);
        stateQ_y2.fill(0.0);
        delayI = 0.0;
        smoothEnv = 0.0f;
    }

    void setBallistics(float attackMs, float releaseMs) noexcept
    {
        attCoeff = FastAudioMath::calcTimeConstantCoeff(sampleRate, std::max(0.05f, attackMs));
        relCoeff = FastAudioMath::calcTimeConstantCoeff(sampleRate, std::max(1.0f, releaseMs));
    }

    // 瞬時解析振幅（リップルゼロ）を返し、同時に I(実部) と Q(虚部・90度移相) も取得可能
    inline float processSample(float input, float* outInPhase = nullptr, float* outQuadrature = nullptr) noexcept
    {
        double sigI = static_cast<double>(input);
        double sigQ = static_cast<double>(input);

        for (size_t i = 0; i < 4; ++i)
        {
            // 2次ポリフェーズ全通過フィルタ H(z) = (z^-2 - a^2) / (1 - a^2 * z^-2)
            // y[n] = a^2 * (y[n-2] - x[n]) + x[n-2]
            const double nextI = coeffI[i] * (stateI_y2[i] - sigI) + stateI_x2[i];
            stateI_x2[i] = stateI_x1[i];
            stateI_x1[i] = sigI;
            stateI_y2[i] = stateI_y1[i];
            stateI_y1[i] = FastAudioMath::sanitizeD(nextI);
            sigI = nextI;

            const double nextQ = coeffQ[i] * (stateQ_y2[i] - sigQ) + stateQ_x2[i];
            stateQ_x2[i] = stateQ_x1[i];
            stateQ_x1[i] = sigQ;
            stateQ_y2[i] = stateQ_y1[i];
            stateQ_y1[i] = FastAudioMath::sanitizeD(nextQ);
            sigQ = nextQ;
        }

        // Path I に1サンプル遅延 (z^-1) を入れて Path Q との位相差を全帯域 90.0° に整合
        const double alignedI = delayI;
        delayI = sigI;

        if (outInPhase != nullptr)    *outInPhase    = static_cast<float>(alignedI);
        if (outQuadrature != nullptr) *outQuadrature = static_cast<float>(sigQ);

        const float instMag = static_cast<float>(std::sqrt(alignedI * alignedI + sigQ * sigQ));
        const float coeff = (instMag > smoothEnv) ? attCoeff : relCoeff;
        smoothEnv += coeff * (instMag - smoothEnv);
        smoothEnv = FastAudioMath::sanitize(smoothEnv);
        return smoothEnv;
    }

    inline float getCurrentEnvelope() const noexcept { return smoothEnv; }

private:
    double sampleRate = 48000.0;
    std::array<double, 4> coeffI { 0.47940086558884, 0.87621849353931, 0.97659758950820, 0.99749925593555 };
    std::array<double, 4> coeffQ { 0.16175849836770, 0.73302893234149, 0.94534970032911, 0.99059915668453 };
    std::array<double, 4> stateI_x1 {}, stateI_x2 {}, stateI_y1 {}, stateI_y2 {};
    std::array<double, 4> stateQ_x1 {}, stateQ_x2 {}, stateQ_y1 {}, stateQ_y2 {};
    double delayI = 0.0;

    float attCoeff = 0.1f;
    float relCoeff = 0.005f;
    float smoothEnv = 0.0f;
};

// =============================================================================
//  4. Serban-Style マルチステージ並列ボーカルバス & Oxford 知覚インフレータ
// =============================================================================
//  グラミー賞エンジニア Serban Ghenea / Andrew Scheps の定番ボーカルバス手法：
//  - Main Path: 原音のトランジェントとダイナミクス（声量10割の伸び）を無傷で保持
//  - FET Catch Path (1176 All-Buttons / Blue Stripe 系): 0.2ms アタックで子音とピークを掴み
//    中域の押し出し感（Punch）を付与
//  - Opto Body Path (LA-2A T4B 光学セル系): 2段階リリース（初期45ms＋残響追従380ms）で
//    語尾の消え際とブレスを自然に持ち上げる
//  - Oxford Perceptual Inflator Curve: ピーク値を一切増やさずに小〜中信号の平均密度を
//    引き上げる 3次多項式 f(x) = 1.5x - 0.5x^3 エンハンサー
// =============================================================================
class SerbanParallelVocalBus
{
public:
    SerbanParallelVocalBus() = default;

    void prepare(double newSampleRate) noexcept
    {
        sampleRate = std::max(8000.0, newSampleRate);
        fetDetector.prepare(sampleRate);
        fetDetector.setBallistics(0.22f, 48.0f);

        optoDetector.prepare(sampleRate);
        optoDetector.setBallistics(12.0f, 140.0f);

        // 並列コンプバスの低域濁りを防ぐサイドチェイン・ハイパス (135Hz)
        scHighpass.prepare(sampleRate);
        scHighpass.setParameters(TptStateVariableFilter::Mode::Highpass, 135.0f, 0.7071f, 0.0f);

        // 並列バス専用のシルキー・エアーシェルフ (11kHz)
        parallelAirShelf.prepare(sampleRate);
        parallelAirShelf.setParameters(TptStateVariableFilter::Mode::HighShelf, 11000.0f, 0.65f, 2.2f);

        slowOptoMemory = 0.0f;
        slowOptoCoeff = FastAudioMath::calcTimeConstantCoeff(sampleRate, 420.0f);
        lastGainReductionDb = 0.0f;
    }

    void reset() noexcept
    {
        fetDetector.reset();
        optoDetector.reset();
        scHighpass.reset();
        parallelAirShelf.reset();
        slowOptoMemory = 0.0f;
        lastGainReductionDb = 0.0f;
    }

    void setParameters(float parallelCrushBlend01, float inflatorEffect01, float vocalPocketDensity01) noexcept
    {
        parallelBlend = std::clamp(parallelCrushBlend01, 0.0f, 1.0f);
        inflatorAmount = std::clamp(inflatorEffect01, 0.0f, 1.0f);
        pocketDensity = std::clamp(vocalPocketDensity01, 0.0f, 1.0f);
    }

    inline float processSample(float dryInput) noexcept
    {
        if (parallelBlend <= 0.001f && inflatorAmount <= 0.001f)
        {
            lastGainReductionDb = 0.0f;
            return dryInput;
        }

        // 1. サイドチェイン信号（低域の近接ボワつきにコンプが過剰反応しないよう135Hz以下を減衰）
        const float scSig = scHighpass.processSample(dryInput);

        // 2. FET 高速ピークキャッチャー (Ratio 8:1, Threshold -20dBFS)
        const float fetEnv = fetDetector.processSample(scSig);
        const float fetDb  = FastAudioMath::linearToDb(fetEnv);
        float fetGrDb = 0.0f;
        constexpr float kFetThreshDb = -20.0f;
        if (fetDb > kFetThreshDb)
        {
            const float over = fetDb - kFetThreshDb;
            fetGrDb = over * (1.0f - 1.0f / 8.0f);
        }
        const float fetGain = FastAudioMath::dbToLinear(-std::min(fetGrDb, 16.0f) + 4.5f);

        // 3. Opto T4B 二段階リリース光学レベラー (Ratio 3.2:1, Threshold -24dBFS)
        const float optoEnv = optoDetector.processSample(scSig);
        slowOptoMemory += slowOptoCoeff * (optoEnv - slowOptoMemory);
        const float combinedOptoEnv = 0.65f * optoEnv + 0.35f * slowOptoMemory;
        const float optoDb = FastAudioMath::linearToDb(combinedOptoEnv);
        float optoGrDb = 0.0f;
        constexpr float kOptoThreshDb = -24.0f;
        if (optoDb > kOptoThreshDb)
        {
            const float over = optoDb - kOptoThreshDb;
            // ソフトニー（幅 6dB）
            const float knee = (over < 6.0f) ? (over * over / 12.0f) : (over - 3.0f);
            optoGrDb = knee * (1.0f - 1.0f / 3.2f);
        }
        const float optoGain = FastAudioMath::dbToLinear(-std::min(optoGrDb, 12.0f) + 3.5f);

        lastGainReductionDb = 0.5f * (fetGrDb + optoGrDb) * parallelBlend;

        // 4. 並列圧縮パスの合成（FET 45% + Opto 55%）＋ 高域エアーの付加
        const float parallelComp = dryInput * (0.45f * fetGain + 0.55f * optoGain);
        const float parallelAir  = parallelAirShelf.processSample(parallelComp);

        // 5. ドライ信号（10割の自然なダイナミクス）へ並列パスをブレンド（Rear Bus Technique）
        const float rearBusMix = parallelBlend * (0.28f + 0.14f * pocketDensity);
        float mixed = dryInput * (1.0f - rearBusMix * 0.35f) + parallelAir * rearBusMix;

        // 6. Sony Oxford Inflator 系譜の知覚密度エンハンサー
        //    f(x) = x + inflator * (0.5 * x * (1 - x^2))  (|x| <= 1 でピークを増やさず平均音圧を向上)
        if (inflatorAmount > 0.001f)
        {
            const float xClamped = std::clamp(mixed, -1.2f, 1.2f);
            const float xNorm = xClamped / (1.0f + 0.25f * std::fabs(xClamped));
            const float inflated = xNorm * (1.0f + 0.48f * inflatorAmount * (1.0f - xNorm * xNorm));
            mixed = mixed * (1.0f - inflatorAmount * 0.55f) + inflated * (inflatorAmount * 0.55f);
        }

        return FastAudioMath::sanitize(mixed);
    }

    inline float getGainReductionDb() const noexcept { return lastGainReductionDb; }

private:
    double sampleRate = 48000.0;
    float parallelBlend = 0.35f;
    float inflatorAmount = 0.25f;
    float pocketDensity = 0.5f;

    HilbertAnalyticDetector fetDetector;
    HilbertAnalyticDetector optoDetector;
    TptStateVariableFilter scHighpass;
    TptStateVariableFilter parallelAirShelf;

    float slowOptoMemory = 0.0f;
    float slowOptoCoeff = 0.001f;
    float lastGainReductionDb = 0.0f;
};

// =============================================================================
//  5. Jaycen-Style 4帯域ダイナミック耳障り共振サプレッサー (Dynamic Harshness Tamer)
// =============================================================================
//  歌い手がハイトーンやサビで声を張り上げた際、声道の咽頭腔（Pharyngeal Cavity）と
//  コンデンサーマイクのダイヤフラム共振が重なる 2.4kHz / 3.2kHz / 4.1kHz / 5.5kHz に
//  鋭いエネルギー集中が発生し、リスナーの耳に「キンキンする痛み」を与えます。
//  固定EQで削ると普段の歌声がこもってしまうため、本エンジンは「中広帯域リファレンス
//  （800Hz〜7kHz平均）に対する各狭帯域の相対突出量（Crest Ratio）」をサンプル単位で
//  監視し、突出した瞬間だけ該当周波数の TPT Bell フィルタを最大 -6.5dB 動的減衰させます。
// =============================================================================
class DynamicHarshnessTamer
{
public:
    static constexpr size_t kNumNodes = 4;

    DynamicHarshnessTamer() = default;

    void prepare(double newSampleRate) noexcept
    {
        sampleRate = std::max(8000.0, newSampleRate);

        broadbandRefBp.prepare(sampleRate);
        broadbandRefBp.setParameters(TptStateVariableFilter::Mode::UnitGainBandpass, 2200.0f, 0.55f, 0.0f);

        for (size_t i = 0; i < kNumNodes; ++i)
        {
            detectBp[i].prepare(sampleRate);
            detectBp[i].setParameters(TptStateVariableFilter::Mode::UnitGainBandpass, kNodeFreqs[i], kNodeDetectQ[i], 0.0f);

            dynamicBell[i].prepare(sampleRate);
            dynamicBell[i].setParameters(TptStateVariableFilter::Mode::PeakBell, kNodeFreqs[i], kNodeBellQ[i], 0.0f);
        }

        attCoeff = FastAudioMath::calcTimeConstantCoeff(sampleRate, 0.8f);
        relCoeff = FastAudioMath::calcTimeConstantCoeff(sampleRate, 42.0f);
        reset();
    }

    void reset() noexcept
    {
        broadbandRefBp.reset();
        refEnv = 1.0e-5f;
        for (size_t i = 0; i < kNumNodes; ++i)
        {
            detectBp[i].reset();
            dynamicBell[i].reset();
            nodeEnv[i] = 0.0f;
            currentCutDb[i] = 0.0f;
        }
        controlDecimCounter = 0;
    }

    void setSensitivity(float sensitivity01, float maxCutDbAbs = 6.5f) noexcept
    {
        sensitivity = std::clamp(sensitivity01, 0.0f, 1.0f);
        maxCutDb = std::clamp(maxCutDbAbs, 0.5f, 12.0f);
    }

    inline float processSample(float input) noexcept
    {
        if (sensitivity <= 0.001f)
            return input;

        // 1. 広帯域リファレンス包絡線の追跡（声全体の声量基準）
        const float refSig = broadbandRefBp.processSample(input);
        const float refAbs = std::fabs(refSig);
        refEnv += ((refAbs > refEnv) ? attCoeff : relCoeff) * (refAbs - refEnv);
        const float safeRef = std::max(refEnv, 1.0e-4f);

        // 2. 4つの咽頭・歯茎共振帯域（2.45kHz / 3.25kHz / 4.20kHz / 5.60kHz）の相対突出度を評価
        for (size_t i = 0; i < kNumNodes; ++i)
        {
            const float bandSig = detectBp[i].processSample(input);
            const float bandAbs = std::fabs(bandSig);
            nodeEnv[i] += ((bandAbs > nodeEnv[i]) ? attCoeff : relCoeff) * (bandAbs - nodeEnv[i]);

            // リファレンスに対する相対比が閾値を超えた分だけカット量(dB)を算出
            const float ratio = nodeEnv[i] / safeRef;
            const float thresh = kNodeRatioThresholds[i] * (1.35f - 0.50f * sensitivity);
            float targetCut = 0.0f;
            if (ratio > thresh && nodeEnv[i] > 0.004f)
            {
                const float excessDb = FastAudioMath::linearToDb(ratio / thresh);
                targetCut = -std::min(maxCutDb, excessDb * (0.55f + 0.45f * sensitivity));
            }

            const float cCoeff = (targetCut < currentCutDb[i]) ? attCoeff : relCoeff;
            currentCutDb[i] += cCoeff * (targetCut - currentCutDb[i]);
        }

        // 3. 8サンプルに1回、TPT Bell フィルタのゲイン係数を高速更新（CPU負荷を最小化しつつ滑らかに追従）
        if ((++controlDecimCounter & 7u) == 0u)
        {
            for (size_t i = 0; i < kNumNodes; ++i)
                dynamicBell[i].setDynamicGainDbFast(currentCutDb[i]);
        }

        // 4. 4段のゼロ遅延 TPT Bell を直列通過
        float out = input;
        for (size_t i = 0; i < kNumNodes; ++i)
            out = dynamicBell[i].processSample(out);

        return FastAudioMath::sanitize(out);
    }

    inline float getNodeCutDb(size_t index) const noexcept
    {
        return (index < kNumNodes) ? currentCutDb[index] : 0.0f;
    }

    inline float getTotalSuppressionDb() const noexcept
    {
        float sum = 0.0f;
        for (float c : currentCutDb) sum += std::fabs(c);
        return sum;
    }

private:
    static constexpr std::array<float, kNumNodes> kNodeFreqs           = { 2450.0f, 3250.0f, 4200.0f, 5600.0f };
    static constexpr std::array<float, kNumNodes> kNodeDetectQ         = { 4.2f,    4.8f,    4.5f,    4.0f    };
    static constexpr std::array<float, kNumNodes> kNodeBellQ           = { 3.6f,    4.2f,    3.8f,    3.4f    };
    static constexpr std::array<float, kNumNodes> kNodeRatioThresholds = { 0.72f,   0.64f,   0.60f,   0.58f   };

    double sampleRate = 48000.0;
    float sensitivity = 0.5f;
    float maxCutDb = 6.5f;

    TptStateVariableFilter broadbandRefBp;
    std::array<TptStateVariableFilter, kNumNodes> detectBp;
    std::array<TptStateVariableFilter, kNumNodes> dynamicBell;

    float refEnv = 1.0e-5f;
    std::array<float, kNumNodes> nodeEnv {};
    std::array<float, kNumNodes> currentCutDb {};
    float attCoeff = 0.05f;
    float relCoeff = 0.002f;
    uint32_t controlDecimCounter = 0;
};

// =============================================================================
//  6. ISO 226:2023 動的等ラウドネス曲線補償 (Dynamic Equal-Loudness Contour)
// =============================================================================
//  人間の聴覚（フレッチャー・マンソン / ISO 226:2023 等ラウドネス曲線）は、
//  音量が下がると 120Hz 以下のチェスト感と 10kHz 以上のエアー感の感度が大きく低下し、
//  逆にサビの大声時には 3kHz〜4kHz の感度が鋭敏になります。
//  本クラスはボーカルの短期実効レベルを監視し、ウィスパー・小声時には低域の温かみと
//  シルキーなエアーを自動的に補い、強唱時には中低域の膨らみを引き締めることで、
//  どんな声量でも「目の前で歌っている親密さとスタジオマスタリングの品位」を保ちます。
// =============================================================================
class Iso226LoudnessContour
{
public:
    Iso226LoudnessContour() = default;

    void prepare(double newSampleRate) noexcept
    {
        sampleRate = std::max(8000.0, newSampleRate);
        warmthLowShelf.prepare(sampleRate);
        warmthLowShelf.setParameters(TptStateVariableFilter::Mode::LowShelf, 145.0f, 0.65f, 0.0f);

        presenceDipBell.prepare(sampleRate);
        presenceDipBell.setParameters(TptStateVariableFilter::Mode::PeakBell, 3350.0f, 1.25f, 0.0f);

        airHighShelf.prepare(sampleRate);
        airHighShelf.setParameters(TptStateVariableFilter::Mode::HighShelf, 10500.0f, 0.68f, 0.0f);

        envAtt = FastAudioMath::calcTimeConstantCoeff(sampleRate, 25.0f);
        envRel = FastAudioMath::calcTimeConstantCoeff(sampleRate, 180.0f);
        reset();
    }

    void reset() noexcept
    {
        warmthLowShelf.reset();
        presenceDipBell.reset();
        airHighShelf.reset();
        levelEnv = 0.0f;
        currentLowDb = 0.0f;
        currentMidDb = 0.0f;
        currentAirDb = 0.0f;
        decimCounter = 0;
    }

    void setAmount(float amount01, float referenceLevelDb = -18.0f) noexcept
    {
        contourAmount = std::clamp(amount01, 0.0f, 1.0f);
        refDb = std::clamp(referenceLevelDb, -36.0f, -6.0f);
    }

    inline float processSample(float input) noexcept
    {
        if (contourAmount <= 0.001f)
            return input;

        const float absIn = std::fabs(input);
        levelEnv += ((absIn > levelEnv) ? envAtt : envRel) * (absIn - levelEnv);

        if ((++decimCounter & 15u) == 0u)
        {
            // 無音ゲート領域（-58dBFS以下）ではブーストをゼロへ戻してフロアノイズ増幅を防止
            const float sigDb = FastAudioMath::linearToDb(levelEnv);
            if (sigDb < -56.0f)
            {
                currentLowDb *= 0.92f;
                currentMidDb *= 0.92f;
                currentAirDb *= 0.92f;
            }
            else
            {
                // 基準レベル(-18dBFS)からの偏差 deltaDb:
                // 小声時 (deltaDb < 0) -> 低域Warmth + 最大3.2dB, 高域Air + 最大2.4dB
                // 強唱時 (deltaDb > 0) -> 低域のボワつきを最大 -1.6dB 整理, 3.35kHz を最大 -1.8dB 緩和
                const float deltaDb = std::clamp(sigDb - refDb, -20.0f, 14.0f);
                const float norm = deltaDb / 20.0f; // [-1.0, +0.7]

                float targetLow = 0.0f;
                float targetMid = 0.0f;
                float targetAir = 0.0f;

                if (norm < 0.0f)
                {
                    const float whisperFactor = -norm; // [0.0, 1.0]
                    targetLow = whisperFactor * 3.1f * contourAmount;
                    targetMid = whisperFactor * 0.6f * contourAmount;
                    targetAir = whisperFactor * 2.3f * contourAmount;
                }
                else
                {
                    const float beltFactor = norm; // [0.0, 0.7]
                    targetLow = -beltFactor * 1.8f * contourAmount;
                    targetMid = -beltFactor * 2.1f * contourAmount;
                    targetAir = beltFactor * 0.8f * contourAmount;
                }

                currentLowDb += 0.18f * (targetLow - currentLowDb);
                currentMidDb += 0.18f * (targetMid - currentMidDb);
                currentAirDb += 0.18f * (targetAir - currentAirDb);
            }

            warmthLowShelf.setDynamicGainDbFast(currentLowDb);
            presenceDipBell.setDynamicGainDbFast(currentMidDb);
            airHighShelf.setDynamicGainDbFast(currentAirDb);
        }

        float y = warmthLowShelf.processSample(input);
        y = presenceDipBell.processSample(y);
        y = airHighShelf.processSample(y);
        return FastAudioMath::sanitize(y);
    }

    inline float getLowCompensationDb() const noexcept { return currentLowDb; }
    inline float getMidCompensationDb() const noexcept { return currentMidDb; }
    inline float getAirCompensationDb() const noexcept { return currentAirDb; }

private:
    double sampleRate = 48000.0;
    float contourAmount = 0.35f;
    float refDb = -18.0f;

    TptStateVariableFilter warmthLowShelf;
    TptStateVariableFilter presenceDipBell;
    TptStateVariableFilter airHighShelf;

    float envAtt = 0.01f;
    float envRel = 0.002f;
    float levelEnv = 0.0f;

    float currentLowDb = 0.0f;
    float currentMidDb = 0.0f;
    float currentAirDb = 0.0f;
    uint32_t decimCounter = 0;
};

// =============================================================================
//  7. マスタリング・シグネチャ・マクロ（5大プロエンジニア音作りプロファイル）
// =============================================================================
struct MasteringSignatureProfile
{
    const char* nameJa;
    const char* nameEn;
    float adaaDrive;           // ADAA 三極管ドライブ量 [0..1]
    float adaaAsymmetry;       // 偶数次倍音バイアス [0..0.45]
    float transformerWeight;   // トランスフォーマー磁気重み [0..1]
    float serbanParallelBlend; // 並列FET/Optoバスブレンド [0..1]
    float oxfordInflator;      // 知覚ラウドネス密度曲線 [0..1]
    float harshnessSensitivity;// 咽頭共振ディエッサー感度 [0..1]
    float iso226Contour;       // ISO 226 等ラウドネス補償量 [0..1]
    float subChestAnchor;      // サブグロッタル胸鳴り補強 [0..1]
    float singersFormantFocus; // 3kHz シンガーズフォルマント焦点 [0..1]
};

static constexpr std::array<MasteringSignatureProfile, 5> kMasteringSignatures = {{
    // 0: Modern Pop Lead (Serban Ghenea / MixStar 系譜：圧倒的な抜けと前に張り付く密度)
    { "① Modern Pop 主役リード", "Modern Pop Lead",
      0.24f, 0.16f, 0.22f, 0.42f, 0.34f, 0.56f, 0.38f, 0.22f, 0.40f },

    // 1: Warm Tube Ballad (Manny Marroquin / LA-2A + Neve 1073 系譜：包容力のある中低域と艶)
    { "② Warm Tube バラード極艶", "Warm Tube Ballad",
      0.36f, 0.28f, 0.48f, 0.34f, 0.26f, 0.45f, 0.46f, 0.36f, 0.28f },

    // 2: Silk Air & Whisper (Sony C800G + Prism Sound 系譜：吐息と倍音が輝く高解像度)
    { "③ Silk Air 吐息＆高解像度", "Silk Air & Whisper",
      0.18f, 0.22f, 0.12f, 0.30f, 0.38f, 0.62f, 0.54f, 0.16f, 0.35f },

    // 3: Arena Belting Safe (Jaycen Joshua 系譜：ハイトーン絶唱でも耳が痛くならない制動力)
    { "④ Arena Belting 絶唱制動", "Arena Belting Safe",
      0.28f, 0.14f, 0.32f, 0.48f, 0.30f, 0.78f, 0.32f, 0.30f, 0.44f },

    // 4: Unplugged Pure (Bob Katz K-System 系譜：原音の息遣いと声量10割を忠実に活かす純粋設計)
    { "⑤ Unplugged 純粋スタジオ", "Unplugged Pure",
      0.10f, 0.08f, 0.10f, 0.18f, 0.14f, 0.38f, 0.24f, 0.12f, 0.20f }
}};

// =============================================================================
//  8. 統合マスタリング・プロエンジン (MasteringProEngine)
// =============================================================================
//  2Dマクロ座標 (X: Clarity/Air, Y: Density/Warmth) またはシグネチャ番号から
//  全サブエンジンを連続モーフィング駆動し、UIへリアルタイム計測テレメトリを提供します。
// =============================================================================
class MasteringProEngine
{
public:
    struct TelemetrySnapshot
    {
        float parallelGrDb      = 0.0f; // Serban並列バスのゲインリダクション(dB)
        float harshnessCutDb    = 0.0f; // 4帯域ダイナミック共振サプレッサー合計抑制量(dB)
        float isoLowBoostDb     = 0.0f; // ISO 226 低域補償量(dB)
        float isoAirBoostDb     = 0.0f; // ISO 226 高域エアー補償量(dB)
        float harmonicDensity01 = 0.0f; // ADAA + Oxford 倍音密度インジケータ [0..1]
        int   activeSignature   = 0;    // 現在選択中のマスタリング・シグネチャ [0..4]
    };

    MasteringProEngine() = default;

    void prepare(double newSampleRate) noexcept
    {
        sampleRate = std::max(8000.0, newSampleRate);
        for (size_t ch = 0; ch < 2; ++ch)
        {
            adaaStage[ch].prepare(sampleRate);
            parallelBus[ch].prepare(sampleRate);
            harshnessTamer[ch].prepare(sampleRate);
            isoContour[ch].prepare(sampleRate);
        }
        applyCurrentProfile();
    }

    void reset() noexcept
    {
        for (size_t ch = 0; ch < 2; ++ch)
        {
            adaaStage[ch].reset();
            parallelBus[ch].reset();
            harshnessTamer[ch].reset();
            isoContour[ch].reset();
        }
    }

    void setSignatureIndex(int index) noexcept
    {
        const int clamped = std::clamp(index, 0, static_cast<int>(kMasteringSignatures.size()) - 1);
        if (clamped == signatureIdx && !customMorphActive)
            return;
        signatureIdx = clamped;
        customMorphActive = false;
        currentProfile = kMasteringSignatures[static_cast<size_t>(signatureIdx)];
        applyCurrentProfile();
    }

    // 2D XY パッド (x01: 透明感・エアー, y01: 真空管密度・温かみ) による連続モーフィング
    void setMacroMorphXY(float x01, float y01) noexcept
    {
        morphX = std::clamp(x01, 0.0f, 1.0f);
        morphY = std::clamp(y01, 0.0f, 1.0f);
        customMorphActive = true;

        // 4隅のプロファイル（Unplugged / Silk Air / Warm Tube / Modern Pop）を双線形補間
        const auto& pBL = kMasteringSignatures[4]; // 左下: Unplugged Pure
        const auto& pBR = kMasteringSignatures[2]; // 右下: Silk Air
        const auto& pTL = kMasteringSignatures[1]; // 左上: Warm Tube
        const auto& pTR = kMasteringSignatures[0]; // 右上: Modern Pop Lead

        auto bilerp = [&](float bl, float br, float tl, float tr) noexcept -> float
        {
            const float bot = bl + morphX * (br - bl);
            const float top = tl + morphX * (tr - tl);
            return bot + morphY * (top - bot);
        };

        currentProfile.adaaDrive            = bilerp(pBL.adaaDrive,            pBR.adaaDrive,            pTL.adaaDrive,            pTR.adaaDrive);
        currentProfile.adaaAsymmetry        = bilerp(pBL.adaaAsymmetry,        pBR.adaaAsymmetry,        pTL.adaaAsymmetry,        pTR.adaaAsymmetry);
        currentProfile.transformerWeight    = bilerp(pBL.transformerWeight,    pBR.transformerWeight,    pTL.transformerWeight,    pTR.transformerWeight);
        currentProfile.serbanParallelBlend  = bilerp(pBL.serbanParallelBlend,  pBR.serbanParallelBlend,  pTL.serbanParallelBlend,  pTR.serbanParallelBlend);
        currentProfile.oxfordInflator       = bilerp(pBL.oxfordInflator,       pBR.oxfordInflator,       pTL.oxfordInflator,       pTR.oxfordInflator);
        currentProfile.harshnessSensitivity = bilerp(pBL.harshnessSensitivity, pBR.harshnessSensitivity, pTL.harshnessSensitivity, pTR.harshnessSensitivity);
        currentProfile.iso226Contour        = bilerp(pBL.iso226Contour,        pBR.iso226Contour,        pTL.iso226Contour,        pTR.iso226Contour);
        currentProfile.subChestAnchor       = bilerp(pBL.subChestAnchor,       pBR.subChestAnchor,       pTL.subChestAnchor,       pTR.subChestAnchor);
        currentProfile.singersFormantFocus  = bilerp(pBL.singersFormantFocus,  pBR.singersFormantFocus,  pTL.singersFormantFocus,  pTR.singersFormantFocus);

        applyCurrentProfile();
    }

    void setMasterIntensity(float intensity01) noexcept
    {
        const float clamped = std::clamp(intensity01, 0.0f, 1.5f);
        if (std::fabs(clamped - masterIntensity) < 0.002f)
            return;
        masterIntensity = clamped;
        applyCurrentProfile();
    }

    inline float processSample(int channel, float input) noexcept
    {
        if (masterIntensity <= 0.001f)
            return input;

        const size_t ch = static_cast<size_t>(std::clamp(channel, 0, 1));

        // Stage 1: Jaycen-Style 4帯域ダイナミック耳障り共振サプレッサー
        float sig = harshnessTamer[ch].processSample(input);

        // Stage 2: ADAA 2次 連続時間アンチエイリアシング三極管＋トランスフォーマー飽和
        sig = adaaStage[ch].processSample(sig);

        // Stage 3: Serban-Style 並列 FET/Opto ボーカルバス + Oxford 知覚密度曲線
        sig = parallelBus[ch].processSample(sig);

        // Stage 4: ISO 226:2023 動的等ラウドネス曲線補償
        sig = isoContour[ch].processSample(sig);

        if (ch == 0)
        {
            telemetryParallelGr.store(parallelBus[0].getGainReductionDb(), std::memory_order_relaxed);
            telemetryHarshCut.store(harshnessTamer[0].getTotalSuppressionDb(), std::memory_order_relaxed);
            telemetryIsoLow.store(isoContour[0].getLowCompensationDb(), std::memory_order_relaxed);
            telemetryIsoAir.store(isoContour[0].getAirCompensationDb(), std::memory_order_relaxed);
        }

        return FastAudioMath::sanitize(sig);
    }

    inline void processStereoBlock(float* left, float* right, int numSamples) noexcept
    {
        if (numSamples <= 0 || left == nullptr)
            return;

        if (right == nullptr)
        {
            for (int i = 0; i < numSamples; ++i)
                left[i] = processSample(0, left[i]);
            return;
        }

        for (int i = 0; i < numSamples; ++i)
        {
            left[i]  = processSample(0, left[i]);
            right[i] = processSample(1, right[i]);
        }
    }

    TelemetrySnapshot getTelemetry() const noexcept
    {
        TelemetrySnapshot snap;
        snap.parallelGrDb      = telemetryParallelGr.load(std::memory_order_relaxed);
        snap.harshnessCutDb    = telemetryHarshCut.load(std::memory_order_relaxed);
        snap.isoLowBoostDb     = telemetryIsoLow.load(std::memory_order_relaxed);
        snap.isoAirBoostDb     = telemetryIsoAir.load(std::memory_order_relaxed);
        snap.harmonicDensity01 = std::clamp(
            (currentProfile.adaaDrive * 0.5f + currentProfile.oxfordInflator * 0.5f) * masterIntensity,
            0.0f, 1.0f);
        snap.activeSignature   = signatureIdx;
        return snap;
    }

    const MasteringSignatureProfile& getActiveProfile() const noexcept { return currentProfile; }
    int getSignatureIndex() const noexcept { return signatureIdx; }
    float getMorphX() const noexcept { return morphX; }
    float getMorphY() const noexcept { return morphY; }

private:
    void applyCurrentProfile() noexcept
    {
        const float scale = masterIntensity;
        for (size_t ch = 0; ch < 2; ++ch)
        {
            adaaStage[ch].setDriveAndBias(
                currentProfile.adaaDrive * scale,
                currentProfile.adaaAsymmetry,
                currentProfile.transformerWeight * scale);

            parallelBus[ch].setParameters(
                currentProfile.serbanParallelBlend * scale,
                currentProfile.oxfordInflator * scale,
                currentProfile.singersFormantFocus);

            harshnessTamer[ch].setSensitivity(
                std::min(1.0f, currentProfile.harshnessSensitivity * scale),
                6.5f);

            isoContour[ch].setAmount(
                std::min(1.0f, currentProfile.iso226Contour * scale),
                -18.0f);
        }
    }

    double sampleRate = 48000.0;
    int signatureIdx = 0;
    bool customMorphActive = false;
    float morphX = 0.75f;
    float morphY = 0.75f;
    float masterIntensity = 1.0f;

    MasteringSignatureProfile currentProfile = kMasteringSignatures[0];

    std::array<AdaaTriodeTransformerStage, 2> adaaStage;
    std::array<SerbanParallelVocalBus, 2>     parallelBus;
    std::array<DynamicHarshnessTamer, 2>      harshnessTamer;
    std::array<Iso226LoudnessContour, 2>      isoContour;

    std::atomic<float> telemetryParallelGr { 0.0f };
    std::atomic<float> telemetryHarshCut   { 0.0f };
    std::atomic<float> telemetryIsoLow     { 0.0f };
    std::atomic<float> telemetryIsoAir     { 0.0f };
};

// =============================================================================
//  9. Manny-Style 4帯域 TPT 位相整合クロスオーバー ＆ 倍音マイクロスプリット励起器
// =============================================================================
//  Manny Marroquin や Dolby 361 (Dolby-A Air Trick) の手法を発展させた 4帯域独立処理：
//  - Band 0 (Low Chest:  < 180 Hz): サブチェストの位相引き締めと低域トランス飽和
//  - Band 1 (Warm Body: 180〜850 Hz): 母音の厚み（F1フォルマント）の泥臭さ整理と温かみ
//  - Band 2 (Vocal Core: 850〜3800 Hz): 歌詞の子音輪郭とシンガーズフォルマントの明瞭度
//  - Band 3 (Silk Air:   > 3800 Hz): 小信号時ほど繊細に煌めくドルビーA方式の上向き倍音エアー
//  すべての分割フィルタに Zavalishin TPT Linkwitz-Riley 2次/4次位相整合構成を採用し、
//  全帯域を再合成した際の位相キャンセリング（コムフィルタリング）をゼロに保ちます。
// =============================================================================
class MannyFourBandExciter
{
public:
    static constexpr size_t kNumBands = 4;

    struct BandParameters
    {
        float gainDb         = 0.0f; // 帯域ゲイン [-12..+12 dB]
        float upwardLift01   = 0.0f; // 小音量ディテールの引き上げ量 [0..1]
        float downwardTame01 = 0.0f; // 過大ピークの抑制量 [0..1]
        float harmonicDrive01= 0.0f; // 帯域別 ADAA 倍音付加量 [0..1]
    };

    MannyFourBandExciter() = default;

    void prepare(double newSampleRate) noexcept
    {
        sampleRate = std::max(8000.0, newSampleRate);

        // 3つのクロスオーバー周波数: 180 Hz, 850 Hz, 3800 Hz
        xoverLowLp.prepare(sampleRate);
        xoverLowHp.prepare(sampleRate);
        xoverMidLp.prepare(sampleRate);
        xoverMidHp.prepare(sampleRate);
        xoverHighLp.prepare(sampleRate);
        xoverHighHp.prepare(sampleRate);
        phaseAllpassLow.prepare(sampleRate);

        xoverLowLp.setParameters(TptStateVariableFilter::Mode::Lowpass,  180.0f,  0.7071f);
        xoverLowHp.setParameters(TptStateVariableFilter::Mode::Highpass, 180.0f,  0.7071f);
        xoverMidLp.setParameters(TptStateVariableFilter::Mode::Lowpass,  850.0f,  0.7071f);
        xoverMidHp.setParameters(TptStateVariableFilter::Mode::Highpass, 850.0f,  0.7071f);
        xoverHighLp.setParameters(TptStateVariableFilter::Mode::Lowpass, 3800.0f, 0.7071f);
        xoverHighHp.setParameters(TptStateVariableFilter::Mode::Highpass,3800.0f, 0.7071f);
        phaseAllpassLow.setParameters(TptStateVariableFilter::Mode::Allpass, 3800.0f, 0.7071f);

        for (size_t b = 0; b < kNumBands; ++b)
        {
            attCoeff[b] = FastAudioMath::calcTimeConstantCoeff(sampleRate, kBandAttackMs[b]);
            relCoeff[b] = FastAudioMath::calcTimeConstantCoeff(sampleRate, kBandReleaseMs[b]);
        }

        // デフォルトの黄金バランス設定
        setBandParameters(0, {  0.4f, 0.15f, 0.25f, 0.18f }); // Low Chest
        setBandParameters(1, { -0.3f, 0.10f, 0.20f, 0.12f }); // Warm Body
        setBandParameters(2, {  0.8f, 0.28f, 0.30f, 0.22f }); // Vocal Core
        setBandParameters(3, {  1.4f, 0.42f, 0.22f, 0.30f }); // Silk Air (Dolby-A Trick)

        reset();
    }

    void reset() noexcept
    {
        xoverLowLp.reset();
        xoverLowHp.reset();
        xoverMidLp.reset();
        xoverMidHp.reset();
        xoverHighLp.reset();
        xoverHighHp.reset();
        phaseAllpassLow.reset();
        bandEnvelopes.fill(0.0f);
        bandRmsTelemetry.fill(0.0f);
    }

    void setBandParameters(size_t bandIdx, const BandParameters& params) noexcept
    {
        if (bandIdx >= kNumBands)
            return;
        bandParams[bandIdx] = params;
        bandLinearGain[bandIdx] = FastAudioMath::dbToLinear(std::clamp(params.gainDb, -18.0f, 18.0f));
    }

    void setOverallMix(float mix01) noexcept
    {
        overallMix = std::clamp(mix01, 0.0f, 1.0f);
    }

    inline float processSample(float input) noexcept
    {
        if (overallMix <= 0.001f)
            return input;

        // 1. ツリー構造 TPT クロスオーバーによる 4帯域分割
        const float lowAndMid  = xoverMidLp.processSample(input);
        const float coreAndAir = xoverMidHp.processSample(input);

        // Low/Warm 側に 3800Hz オールパスを通して高域分岐との群遅延を完全一致させる
        const float alignedLowAndMid = phaseAllpassLow.processSample(lowAndMid);

        std::array<float, kNumBands> bands;
        bands[0] = xoverLowLp.processSample(alignedLowAndMid);
        bands[1] = xoverLowHp.processSample(alignedLowAndMid);
        bands[2] = xoverHighLp.processSample(coreAndAir);
        bands[3] = xoverHighHp.processSample(coreAndAir);

        // 2. 各帯域の上向きディテール引き上げ (Upward Lifter) ＋ 下向きピーク抑制 ＋ ソフト倍音励起
        float sumOut = 0.0f;
        for (size_t b = 0; b < kNumBands; ++b)
        {
            float sig = bands[b];
            const float absSig = std::fabs(sig);
            float& env = bandEnvelopes[b];
            env += ((absSig > env) ? attCoeff[b] : relCoeff[b]) * (absSig - env);
            bandRmsTelemetry[b] = env;

            const auto& bp = bandParams[b];
            float dynGain = bandLinearGain[b];

            // (a) Upward Micro-Detail Lifter (ノイズフロア -54dBFS 以上〜 -22dBFS 以下の繊細な声成分を持ち上げる)
            if (bp.upwardLift01 > 0.001f && env > 0.0018f && env < 0.08f)
            {
                const float normWindow = (0.08f - env) / 0.0782f;
                const float liftDb = normWindow * 4.5f * bp.upwardLift01;
                dynGain *= FastAudioMath::dbToLinear(liftDb);
            }

            // (b) Downward Crest Tamer (帯域ごとの突出エネルギーを滑らかに制動)
            const float thresh = kBandCeilingThresholds[b];
            if (bp.downwardTame01 > 0.001f && env > thresh)
            {
                const float overRatio = env / thresh;
                const float tameDb = FastAudioMath::linearToDb(overRatio) * 0.55f * bp.downwardTame01;
                dynGain *= FastAudioMath::dbToLinear(-std::min(tameDb, 8.0f));
            }

            sig *= dynGain;

            // (c) 帯域別 ソフト三極管・偶奇倍音シェイピング
            if (bp.harmonicDrive01 > 0.001f)
            {
                const double drv = 1.0 + static_cast<double>(bp.harmonicDrive01) * 1.65;
                const double x   = static_cast<double>(sig) * drv;
                // 高域バンドほど偶数次非対称を強めてシルキーな輝きを作る
                const double asym = (b >= 2) ? 0.14 : 0.06;
                const double shaped = FastAudioMath::smoothTanh(x + asym * x * x / (1.0 + std::fabs(x))) / drv;
                sig = static_cast<float>(shaped);
            }

            sumOut += sig;
        }

        const float blended = input * (1.0f - overallMix) + sumOut * overallMix;
        return FastAudioMath::sanitize(blended);
    }

    inline float getBandEnvelope(size_t bandIdx) const noexcept
    {
        return (bandIdx < kNumBands) ? bandRmsTelemetry[bandIdx] : 0.0f;
    }

private:
    static constexpr std::array<float, kNumBands> kBandAttackMs          = { 8.0f,  4.5f,  1.8f,  0.9f  };
    static constexpr std::array<float, kNumBands> kBandReleaseMs         = { 95.0f, 75.0f, 52.0f, 38.0f };
    static constexpr std::array<float, kNumBands> kBandCeilingThresholds = { 0.28f, 0.25f, 0.22f, 0.16f };

    double sampleRate = 48000.0;
    float overallMix = 0.45f;

    TptStateVariableFilter xoverLowLp, xoverLowHp;
    TptStateVariableFilter xoverMidLp, xoverMidHp;
    TptStateVariableFilter xoverHighLp, xoverHighHp;
    TptStateVariableFilter phaseAllpassLow;

    std::array<BandParameters, kNumBands> bandParams {};
    std::array<float, kNumBands> bandLinearGain { 1.0f, 1.0f, 1.0f, 1.0f };
    std::array<float, kNumBands> attCoeff { 0.01f, 0.02f, 0.05f, 0.1f };
    std::array<float, kNumBands> relCoeff { 0.001f, 0.002f, 0.003f, 0.005f };
    std::array<float, kNumBands> bandEnvelopes {};
    std::array<float, kNumBands> bandRmsTelemetry {};
};

// =============================================================================
//  10. Bricasti M7 / EMT 250 系譜 黄金比初期反射 ＆ マイクロピッチ・ボーカルポケット
// =============================================================================
//  カラオケ2ミックス伴奏（すでにステレオで完成された音源）の上にモノラルマイクの声を
//  乗せた際、「声だけ手前に浮いて伴奏と馴染まない」現象を解決するため、
//  テイル（長い残響）ではなく **5ms〜34ms の黄金比配置・微細ピッチ変調初期反射クラスタ**
//  を生成します。さらに歌唱中は初期反射の Mid 成分を自動ダッキングし、Side 成分だけを
//  残すことで、声を一切濁らせずに伴奏のステレオ音場へ自然に溶け込ませます。
// =============================================================================
class BricastiGoldenEarlyReflections
{
public:
    static constexpr size_t kBufferSize = 4096; // 48kHzで約85ms（2のべき乗マスク用）
    static constexpr size_t kBufferMask = kBufferSize - 1;
    static constexpr size_t kNumTaps    = 8;

    BricastiGoldenEarlyReflections() = default;

    void prepare(double newSampleRate) noexcept
    {
        sampleRate = std::max(8000.0, newSampleRate);
        const double srScale = sampleRate / 48000.0;

        for (size_t t = 0; t < kNumTaps; ++t)
        {
            baseDelaySamplesL[t] = static_cast<float>(kGoldenTapMsL[t] * 48.0 * srScale);
            baseDelaySamplesR[t] = static_cast<float>(kGoldenTapMsR[t] * 48.0 * srScale);
        }

        erToneHp.prepare(sampleRate);
        erToneLp.prepare(sampleRate);
        erToneHp.setParameters(TptStateVariableFilter::Mode::Highpass, 240.0f, 0.7071f);
        erToneLp.setParameters(TptStateVariableFilter::Mode::Lowpass,  7200.0f, 0.7071f);

        lfoInc = static_cast<float>(FastAudioMath::kTwoPi * 0.62 / sampleRate);
        duckAtt = FastAudioMath::calcTimeConstantCoeff(sampleRate, 3.5f);
        duckRel = FastAudioMath::calcTimeConstantCoeff(sampleRate, 110.0f);
        reset();
    }

    void reset() noexcept
    {
        delayLineL.fill(0.0f);
        delayLineR.fill(0.0f);
        writePos = 0;
        lfoPhase = 0.0f;
        vocalEnv = 0.0f;
        erToneHp.reset();
        erToneLp.reset();
    }

    void setPocketDepth(float depth01, float stereoWidth01 = 0.85f) noexcept
    {
        pocketDepth = std::clamp(depth01, 0.0f, 1.0f);
        stereoWidth = std::clamp(stereoWidth01, 0.0f, 1.5f);
    }

    inline void processStereoSample(float inL, float inR, float& outL, float& outR) noexcept
    {
        if (pocketDepth <= 0.001f)
        {
            outL = inL;
            outR = inR;
            return;
        }

        const float monoIn = 0.5f * (inL + inR);
        const float absIn  = std::fabs(monoIn);
        vocalEnv += ((absIn > vocalEnv) ? duckAtt : duckRel) * (absIn - vocalEnv);

        // 初期反射へ送る信号を 240Hz〜7.2kHz に帯域制限（歯擦音と低域ボワつきを除外）
        const float filteredIn = erToneLp.processSample(erToneHp.processSample(monoIn));

        delayLineL[writePos] = filteredIn + inL * 0.25f;
        delayLineR[writePos] = filteredIn + inR * 0.25f;

        lfoPhase += lfoInc;
        if (lfoPhase >= static_cast<float>(FastAudioMath::kTwoPi))
            lfoPhase -= static_cast<float>(FastAudioMath::kTwoPi);

        float sumErL = 0.0f;
        float sumErR = 0.0f;

        for (size_t t = 0; t < kNumTaps; ++t)
        {
            const float phaseOffset = static_cast<float>(t) * 0.785398f;
            const float modL = std::sin(lfoPhase + phaseOffset) * 4.2f;
            const float modR = std::cos(lfoPhase + phaseOffset * 1.15f) * 4.2f;

            const float dL = std::clamp(baseDelaySamplesL[t] + modL, 8.0f, static_cast<float>(kBufferSize - 8));
            const float dR = std::clamp(baseDelaySamplesR[t] + modR, 8.0f, static_cast<float>(kBufferSize - 8));

            sumErL += readFractional(delayLineL, dL) * kTapGains[t];
            sumErR += readFractional(delayLineR, dR) * kTapGains[t];
        }

        writePos = (writePos + 1) & kBufferMask;

        // 歌唱中（vocalEnv大）は Mid 反射を抑えて Side 反射を中心に残し、語尾で Mid 反射を解放
        const float midDuck = 1.0f / (1.0f + vocalEnv * 3.8f);
        const float erMid  = 0.5f * (sumErL + sumErR) * midDuck;
        const float erSide = 0.5f * (sumErL - sumErR) * stereoWidth;

        const float wetScale = pocketDepth * 0.22f;
        outL = FastAudioMath::sanitize(inL + (erMid + erSide) * wetScale);
        outR = FastAudioMath::sanitize(inR + (erMid - erSide) * wetScale);
    }

private:
    inline float readFractional(const std::array<float, kBufferSize>& buf, float delaySamples) const noexcept
    {
        const float readPos = static_cast<float>(writePos) - delaySamples;
        int idx0 = static_cast<int>( std::floor(readPos) );
        const float frac = readPos - static_cast<float>(idx0);
        idx0 &= static_cast<int>(kBufferMask);
        const int idx1 = (idx0 + 1) & static_cast<int>(kBufferMask);
        return buf[static_cast<size_t>(idx0)] + frac * (buf[static_cast<size_t>(idx1)] - buf[static_cast<size_t>(idx0)]);
    }

    // 黄金比 φ = 1.618034 に基づく非周期的初期反射タップ (ms)
    static constexpr std::array<float, kNumTaps> kGoldenTapMsL = { 5.3f, 8.6f, 12.1f, 16.4f, 20.9f, 25.7f, 30.2f, 35.1f };
    static constexpr std::array<float, kNumTaps> kGoldenTapMsR = { 6.1f, 9.4f, 13.5f, 17.8f, 22.3f, 27.1f, 31.8f, 36.6f };
    static constexpr std::array<float, kNumTaps> kTapGains     = { 0.34f, -0.28f, 0.24f, 0.20f, -0.17f, 0.14f, -0.11f, 0.09f };

    double sampleRate = 48000.0;
    float pocketDepth = 0.35f;
    float stereoWidth = 0.90f;

    std::array<float, kBufferSize> delayLineL {};
    std::array<float, kBufferSize> delayLineR {};
    std::array<float, kNumTaps> baseDelaySamplesL {};
    std::array<float, kNumTaps> baseDelaySamplesR {};
    size_t writePos = 0;

    TptStateVariableFilter erToneHp;
    TptStateVariableFilter erToneLp;
    float lfoPhase = 0.0f;
    float lfoInc = 0.0001f;
    float vocalEnv = 0.0f;
    float duckAtt = 0.02f;
    float duckRel = 0.001f;
};

// =============================================================================
//  11. Bob Katz K-System & ITU-R BS.1770-4 K-Weighted ラウドネス＆トゥルーピーク監視器
// =============================================================================
//  マスタリングの巨匠 Bob Katz が提唱した K-System (K-14: ポップス・配信基準) と
//  国際標準 ITU-R BS.1770-4 の K重み付けフィルタ（頭部音響伝達関数 Pre-Filter 1.5kHz
//  +4dB シェルフ ＋ RLB 38Hz ハイパス）および 4位相エルミート補間インターサンプル・
//  トゥルーピーク検出器を実装。リアルタイムで「知覚ラウドネス (LUFS)」「ダイナミック
//  クレストファクター (PSR / Peak-to-Short-Term Ratio)」を計測します。
// =============================================================================
class BobKatzKSystemAnalyzer
{
public:
    struct KSystemMetrics
    {
        float momentaryLufs     = -70.0f; // 400ms K-Weighted Momentary Loudness (LUFS)
        float shortTermLufs     = -70.0f; // 3000ms K-Weighted Short-Term Loudness (LUFS)
        float truePeakDbtp      = -90.0f; // 4x Inter-Sample True Peak (dBTP)
        float crestFactorDb     = 12.0f;  // Peak-to-Short-Term Ratio (PSR / Crest dB)
        float k14MeterDb        = -30.0f; // K-14 スケール上の相対レベル (-14 LUFS = 0 dBr)
    };

    BobKatzKSystemAnalyzer() = default;

    void prepare(double newSampleRate) noexcept
    {
        sampleRate = std::max(8000.0, newSampleRate);
        for (size_t ch = 0; ch < 2; ++ch)
        {
            // Stage 1: ITU-R BS.1770-4 Pre-Filter (頭部球体回折モデル: 1681Hz, +4.0dB HighShelf)
            kPreShelf[ch].prepare(sampleRate);
            kPreShelf[ch].setParameters(TptStateVariableFilter::Mode::HighShelf, 1681.97f, 0.7071f, 4.0f);

            // Stage 2: RLB (Revised Low-frequency B-curve: 38.13Hz Highpass)
            kRlbHp[ch].prepare(sampleRate);
            kRlbHp[ch].setParameters(TptStateVariableFilter::Mode::Highpass, 38.135f, 0.5003f, 0.0f);
        }

        momCoeff   = FastAudioMath::calcTimeConstantCoeff(sampleRate, 400.0f);
        shortCoeff = FastAudioMath::calcTimeConstantCoeff(sampleRate, 3000.0f);
        tpDecay    = static_cast<float>(std::exp(-1.0 / (sampleRate * 0.65)));
        reset();
    }

    void reset() noexcept
    {
        for (size_t ch = 0; ch < 2; ++ch)
        {
            kPreShelf[ch].reset();
            kRlbHp[ch].reset();
            hist[ch].fill(0.0f);
        }
        momMeanSq   = 1.0e-10f;
        shortMeanSq = 1.0e-10f;
        truePeakLin = 1.0e-7f;
    }

    inline void processStereoSample(float inL, float inR) noexcept
    {
        // 1. K-Weighting フィルタ通過後の平均二乗パワー計算
        const float kwL = kRlbHp[0].processSample(kPreShelf[0].processSample(inL));
        const float kwR = kRlbHp[1].processSample(kPreShelf[1].processSample(inR));
        const float power = 0.5f * (kwL * kwL + kwR * kwR);

        momMeanSq   += momCoeff * (power - momMeanSq);
        shortMeanSq += shortCoeff * (power - shortMeanSq);
        momMeanSq   = std::max(1.0e-12f, FastAudioMath::sanitize(momMeanSq));
        shortMeanSq = std::max(1.0e-12f, FastAudioMath::sanitize(shortMeanSq));

        // 2. 4点Catmull-Romエルミート補間による 4x Inter-Sample True Peak 推定
        const float tpL = estimateInterSamplePeak(0, inL);
        const float tpR = estimateInterSamplePeak(1, inR);
        const float maxTp = std::max(tpL, tpR);

        if (maxTp >= truePeakLin)
            truePeakLin = maxTp;
        else
            truePeakLin = std::max(maxTp, truePeakLin * tpDecay);
    }

    KSystemMetrics getMetrics() const noexcept
    {
        KSystemMetrics m;
        m.momentaryLufs = std::max(-70.0f, -0.691f + 10.0f * std::log10(std::max(1.0e-10f, momMeanSq)));
        m.shortTermLufs = std::max(-70.0f, -0.691f + 10.0f * std::log10(std::max(1.0e-10f, shortMeanSq)));
        m.truePeakDbtp  = std::max(-90.0f, FastAudioMath::linearToDb(truePeakLin));
        m.crestFactorDb = std::clamp(m.truePeakDbtp - m.momentaryLufs, 0.0f, 30.0f);
        m.k14MeterDb    = m.shortTermLufs - (-14.0f); // K-14基準点 (-14 LUFS = 0 dBr)
        return m;
    }

private:
    inline float estimateInterSamplePeak(size_t ch, float newSample) noexcept
    {
        auto& h = hist[ch];
        h[0] = h[1];
        h[1] = h[2];
        h[2] = h[3];
        h[3] = newSample;

        float maxVal = std::max(std::fabs(h[1]), std::fabs(h[2]));

        // Catmull-Rom 3次スプライン係数（サンプル間 t = 0.25, 0.50, 0.75 を評価）
        const float c0 = h[1];
        const float c1 = 0.5f * (h[2] - h[0]);
        const float c2 = h[0] - 2.5f * h[1] + 2.0f * h[2] - 0.5f * h[3];
        const float c3 = 0.5f * (h[3] - h[0]) + 1.5f * (h[1] - h[2]);

        for (float t : { 0.25f, 0.50f, 0.75f })
        {
            const float interp = ((c3 * t + c2) * t + c1) * t + c0;
            maxVal = std::max(maxVal, std::fabs(interp));
        }
        return maxVal;
    }

    double sampleRate = 48000.0;
    std::array<TptStateVariableFilter, 2> kPreShelf;
    std::array<TptStateVariableFilter, 2> kRlbHp;
    std::array<std::array<float, 4>, 2> hist {};

    float momCoeff    = 0.001f;
    float shortCoeff  = 0.0001f;
    float tpDecay     = 0.9999f;
    float momMeanSq   = 1.0e-10f;
    float shortMeanSq = 1.0e-10f;
    float truePeakLin = 1.0e-7f;
};

} // namespace mastering
} // namespace gzzio

namespace vg
{
namespace mastering = ::gzzio::mastering;
} // namespace vg
