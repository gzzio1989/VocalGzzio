#pragma once

/**
 * @file AnalogTubeAndOptoMasteringChain.h
 * @brief VocalGzzio v5.5.0 真空管Vari-Mu (Fairchild 670)・光学式T4Bセル (Teletronix LA-2A)・並列パッシブLCシェルフ (Manley Massive Passive) モデリング
 *
 * 世界最高峰のボーカル録音・マスタリングスタジオで「最後の魔法」として必ず通される3大アナログ名機：
 *   1. Fairchild 670 (6386 リモートカットオフ三極管プッシュプル Vari-Mu コンプレッサー ＋ TC1〜TC6 プログラム依存時定数 ＋ Lat/Vert M/S 結合)
 *   2. Teletronix LA-2A (T4B エレクトロルミネッセンス発光パネル ＋ CdS 光導電セル 2段階フォトメモリ減衰器)
 *   3. Manley Massive Passive (並列パッシブLCインダクタ・シェルフ＆ベルEQ ＋ 真空管メイクアップアンプ)
 * の物理回路特性を、オーディオスレッド確保ゼロ・ゼロ遅延TPT/非線形微分方程式で忠実に再現するモジュール。
 */

#include <algorithm>
#include <array>
#include <cmath>

namespace gzzio
{
namespace analog_mastering
{

// =============================================================================
// 1. Fairchild 670 リモートカットオフ 6386 Vari-Mu 真空管コンプレッサー
//    - 圧縮比が固定ではなく、ゲインリダクションが深くなるほど 1.5:1 → 8:1 へ連続変化する Vari-Mu 特性
//    - 6ポジション時定数ネットワーク (TC1〜TC6: TC5/TC6 は高速トランジェント回路＋低速プログラム依存回路の二重時定数)
//    - Lateral / Vertical (Mid / Side) モード対応
// =============================================================================
class Fairchild670VariMuTubeCompressor
{
public:
    enum class TimeConstantPreset : int
    {
        TC1_Fast60ms       = 0, // Atk 0.2ms / Rel 150ms
        TC2_VocalStandard  = 1, // Atk 0.2ms / Rel 300ms
        TC3_SmoothBallad   = 2, // Atk 0.4ms / Rel 800ms
        TC4_SlowMajestic   = 3, // Atk 0.8ms / Rel 2000ms
        TC5_ProgramDualA   = 4, // 二重時定数: 単発ピーク 200ms ＋ 持続平均 2.5s
        TC6_ProgramDualB   = 5  // 二重時定数: 単発ピーク 300ms ＋ 持続平均 5.0s
    };

    void prepare(double newSampleRate) noexcept
    {
        sampleRate = std::max(8000.0, newSampleRate);
        setTimeConstant(TimeConstantPreset::TC2_VocalStandard);
        reset();
    }

    void reset() noexcept
    {
        fastEnvMid  = 0.0f;
        slowEnvMid  = 0.0f;
        fastEnvSide = 0.0f;
        slowEnvSide = 0.0f;
        dcMidIn = dcMidOut = 0.0f;
        dcSideIn = dcSideOut = 0.0f;
        lastGrMidDb  = 0.0f;
        lastGrSideDb = 0.0f;
    }

    void setTimeConstant(TimeConstantPreset preset) noexcept
    {
        currentPreset = preset;
        double atkMs = 0.25, fastRelMs = 300.0, slowRelMs = 1500.0;
        switch (preset)
        {
            case TimeConstantPreset::TC1_Fast60ms:      atkMs = 0.20; fastRelMs = 150.0; slowRelMs = 400.0;  dualWeight = 0.10f; break;
            case TimeConstantPreset::TC2_VocalStandard: atkMs = 0.25; fastRelMs = 320.0; slowRelMs = 900.0;  dualWeight = 0.22f; break;
            case TimeConstantPreset::TC3_SmoothBallad:  atkMs = 0.40; fastRelMs = 780.0; slowRelMs = 1800.0; dualWeight = 0.30f; break;
            case TimeConstantPreset::TC4_SlowMajestic:  atkMs = 0.80; fastRelMs = 1600.0;slowRelMs = 3200.0; dualWeight = 0.35f; break;
            case TimeConstantPreset::TC5_ProgramDualA:  atkMs = 0.22; fastRelMs = 220.0; slowRelMs = 2400.0; dualWeight = 0.52f; break;
            case TimeConstantPreset::TC6_ProgramDualB:  atkMs = 0.30; fastRelMs = 300.0; slowRelMs = 4500.0; dualWeight = 0.62f; break;
        }
        attCoeff     = static_cast<float>(1.0 - std::exp(-1.0 / (sampleRate * (atkMs * 0.001))));
        fastRelCoeff = static_cast<float>(1.0 - std::exp(-1.0 / (sampleRate * (fastRelMs * 0.001))));
        slowRelCoeff = static_cast<float>(1.0 - std::exp(-1.0 / (sampleRate * (slowRelMs * 0.001))));
    }

    // Lat/Vert (Mid/Side) モードで処理: Mid のボーカル芯は滑らかに抱擁し、Side の残響は息づくように浮かび上がらせる
    inline void processSamplePair(float& ioL, float& ioR, float drive01, bool latVertMode = true) noexcept
    {
        const float drv = std::clamp(drive01, 0.0f, 1.0f);
        if (drv <= 0.001f)
        {
            lastGrMidDb  = 0.0f;
            lastGrSideDb = 0.0f;
            return;
        }

        const float midIn  = latVertMode ? (0.5f * (ioL + ioR)) : ioL;
        const float sideIn = latVertMode ? (0.5f * (ioL - ioR)) : ioR;

        const float midOut  = processVariMuChannel(midIn,  drv,        fastEnvMid,  slowEnvMid,  dcMidIn,  dcMidOut,  lastGrMidDb);
        const float sideOut = processVariMuChannel(sideIn, drv * 0.72f, fastEnvSide, slowEnvSide, dcSideIn, dcSideOut, lastGrSideDb);

        if (latVertMode)
        {
            ioL = midOut + sideOut;
            ioR = midOut - sideOut;
        }
        else
        {
            ioL = midOut;
            ioR = sideOut;
        }
    }

    inline float getGainReductionMidDb() const noexcept  { return lastGrMidDb; }
    inline float getGainReductionSideDb() const noexcept { return lastGrSideDb; }

private:
    inline float processVariMuChannel(float x,
                                      float drive01,
                                      float& fastEnv,
                                      float& slowEnv,
                                      float& dcIn,
                                      float& dcOut,
                                      float& outGrDb) const noexcept
    {
        // 入力トランス昇圧ドライブ
        const float preGain = 1.0f + drive01 * 2.1f;
        const float driven  = x * preGain;
        const float absX    = std::fabs(driven);

        // 二重時定数カソードバイアス制御電圧の追跡
        fastEnv += ((absX > fastEnv) ? attCoeff : fastRelCoeff) * (absX - fastEnv);
        slowEnv += ((absX > slowEnv) ? (attCoeff * 0.18f) : slowRelCoeff) * (absX - slowEnv);

        const float biasControl = (1.0f - dualWeight) * fastEnv + dualWeight * slowEnv;

        // 6386 リモートカットオフ三極管の相互コンダクタンス gm(V_bias) 曲線
        // 小信号ではほぼ 1:1、信号が大きくなるにつれてソフトニーで滑らかに Vari-Mu 圧縮が深まる
        const float threshold = 0.16f;
        float gainLin = 1.0f;
        if (biasControl > threshold)
        {
            const float excess = (biasControl - threshold) / threshold;
            // Vari-Mu 逆双曲線ゲイン法則
            gainLin = 1.0f / std::pow(1.0f + 0.55f * excess * (1.0f + 0.35f * drive01), 0.68f);
        }

        outGrDb = -20.0f * std::log10(std::max(0.05f, gainLin));

        // プッシュプル三極管ステージ（偶数次歪みは打ち消し合い、わずかな2次非対称と豊かな3次真空管倍音が付与される）
        const float compressed = driven * gainLin;
        const float tubeSat    = std::tanh(compressed * 0.92f + 0.025f * compressed * compressed) * 1.05f;

        // 出力結合コンデンサ DC ブロッカー
        const float clean = tubeSat - dcIn + 0.9982f * dcOut;
        dcIn  = tubeSat;
        dcOut = clean;

        // オートメイクアップ補正
        const float makeup = 1.0f / (1.0f + drive01 * 0.52f);
        return clean * makeup;
    }

    double sampleRate = 48000.0;
    TimeConstantPreset currentPreset = TimeConstantPreset::TC2_VocalStandard;
    float attCoeff     = 0.05f;
    float fastRelCoeff = 0.001f;
    float slowRelCoeff = 0.0002f;
    float dualWeight   = 0.25f;

    float fastEnvMid  = 0.0f, slowEnvMid  = 0.0f;
    float fastEnvSide = 0.0f, slowEnvSide = 0.0f;
    float dcMidIn = 0.0f, dcMidOut = 0.0f;
    float dcSideIn = 0.0f, dcSideOut = 0.0f;
    float lastGrMidDb = 0.0f, lastGrSideDb = 0.0f;
};

// =============================================================================
// 2. Teletronix LA-2A (T4B 光学式 EL発光パネル ＋ CdS 二段階フォトメモリ減衰器)
// =============================================================================
class TeletronixLa2aT4bOptoAttenuator
{
public:
    void prepare(double newSampleRate) noexcept
    {
        sampleRate = std::max(8000.0, newSampleRate);
        // サイドチェイン HF エンファシス用1次ハイパス (1.2kHz 以上の歯擦音・高域張り上げに敏感に反応させる R37 Pre-emphasis)
        const double rc = 1.0 / (2.0 * 3.141592653589793 * 1250.0);
        const double dt = 1.0 / sampleRate;
        scHpAlpha = static_cast<float>(rc / (rc + dt));

        // T4B CdS セル時定数:
        //   Attack: 約 10ms (発光強度が大きいほど速くなる非線形アタック)
        //   Fast Release: 約 60ms (最初の 50% の減衰がスッと戻る)
        //   Slow Memory Tail: 約 1.8s (長時間圧縮された分だけ CdS セルにメモリが残り、歌の余韻を優しく支える)
        baseAtkCoeff  = static_cast<float>(1.0 - std::exp(-1.0 / (sampleRate * 0.0095)));
        fastRelCoeff  = static_cast<float>(1.0 - std::exp(-1.0 / (sampleRate * 0.062)));
        memoryAccCoeff= static_cast<float>(1.0 - std::exp(-1.0 / (sampleRate * 0.450)));
        memoryRelCoeff= static_cast<float>(1.0 - std::exp(-1.0 / (sampleRate * 1.850)));
        reset();
    }

    void reset() noexcept
    {
        scPrevIn    = 0.0f;
        scPrevHp    = 0.0f;
        fastCellEnv = 0.0f;
        slowMemEnv  = 0.0f;
        lastOptoGrDb= 0.0f;
    }

    inline void processSamplePair(float& ioL,
                                  float& ioR,
                                  float peakReduction01,
                                  bool limitMode = false,
                                  float hfEmphasis01 = 0.35f) noexcept
    {
        const float pr = std::clamp(peakReduction01, 0.0f, 1.0f);
        if (pr <= 0.001f)
        {
            lastOptoGrDb = 0.0f;
            return;
        }

        const float mono = 0.5f * (ioL + ioR);
        // R37 サイドチェイン高域エンファシス（フラット〜高域重視）
        const float hp = scHpAlpha * (scPrevHp + mono - scPrevIn);
        scPrevIn = mono;
        scPrevHp = hp;

        const float scSig = mono + std::clamp(hfEmphasis01, 0.0f, 1.0f) * 1.45f * hp;

        // EL エレクトロルミネッセンスパネルの発光輝度（閾値を超えると滑らかに発光）
        const float lumDrive = std::fabs(scSig) * (1.0f + pr * 3.4f);
        const float panelLum = (lumDrive * lumDrive) / (0.12f + lumDrive);

        // CdS セルは光が強いほどアタックが速くなる光導電特性を持つ
        const float dynAtk = std::min(0.25f, baseAtkCoeff * (1.0f + 1.8f * panelLum));
        fastCellEnv += ((panelLum > fastCellEnv) ? dynAtk : fastRelCoeff) * (panelLum - fastCellEnv);

        // 長時間の発光で CdS セル内部に蓄積されるフォトメモリ層
        slowMemEnv  += ((fastCellEnv > slowMemEnv) ? memoryAccCoeff : memoryRelCoeff) * (fastCellEnv - slowMemEnv);

        // 最初の半分は Fast Cell (60ms) で素早く戻り、残りは Slow Memory (1.85s) で音楽的に余韻を残す
        const float combinedCell = std::max(fastCellEnv, slowMemEnv * 0.78f);

        // Compress (約 3:1) vs Limit (約 10:1)
        const float ratioExp = limitMode ? 0.88f : 0.64f;
        const float gainLin  = 1.0f / std::pow(1.0f + combinedCell * 2.2f, ratioExp);
        lastOptoGrDb         = -20.0f * std::log10(std::max(0.05f, gainLin));

        // 12AX7 / 12BH7 真空管メイクアップアンプのウォームな偶数次倍音とトランス飽和
        const float makeup = 1.0f + pr * 0.68f;
        auto applyTubeStage = [gainLin, makeup](float s) noexcept -> float
        {
            const float x = s * gainLin * makeup;
            return std::tanh(x + 0.035f * x * x) * 0.96f;
        };

        ioL = applyTubeStage(ioL);
        ioR = applyTubeStage(ioR);
    }

    inline float getOptoGainReductionDb() const noexcept { return lastOptoGrDb; }

private:
    double sampleRate    = 48000.0;
    float  scHpAlpha     = 0.85f;
    float  baseAtkCoeff  = 0.002f;
    float  fastRelCoeff  = 0.0003f;
    float  memoryAccCoeff= 0.00005f;
    float  memoryRelCoeff= 0.00001f;

    float  scPrevIn     = 0.0f;
    float  scPrevHp     = 0.0f;
    float  fastCellEnv  = 0.0f;
    float  slowMemEnv   = 0.0f;
    float  lastOptoGrDb = 0.0f;
};

// =============================================================================
// 3. Manley Massive Passive 風 並列パッシブLCインダクタ・トーンシェイパー
//    直列EQのように位相歪みが累積せず、4つのパッシブLC共振ブランチが並列加算されることで
//    どれだけブーストしても自然で立体的な空気感を保つ
// =============================================================================
class ManleyMassivePassiveInductorShelf
{
public:
    void prepare(double newSampleRate) noexcept
    {
        sampleRate = std::max(8000.0, newSampleRate);
        updateFilterBranches();
        reset();
    }

    void reset() noexcept
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            warmthIc1[ch] = warmthIc2[ch] = 0.0;
            mudCutIc1[ch] = mudCutIc2[ch] = 0.0;
            presenceIc1[ch] = presenceIc2[ch] = 0.0;
            silkAirIc1[ch] = silkAirIc2[ch] = 0.0;
        }
    }

    void updateFilterBranches() noexcept
    {
        // Branch 1: 120Hz Passive Inductor Warmth Shelf/Bell (Q = 0.75)
        setupSvf(120.0, 0.75, wA1, wA2, wA3);
        // Branch 2: 340Hz Boxiness / Mud Passive Dip (Q = 1.45)
        setupSvf(340.0, 1.45, mA1, mA2, mA3);
        // Branch 3: 2.7kHz Vocal Forwardness Bell (Q = 0.90)
        setupSvf(2700.0, 0.90, pA1, pA2, pA3);
        // Branch 4: 16kHz Massive Passive Inductor Air Shelf (Q = 0.68)
        setupSvf(std::min(16000.0, sampleRate * 0.43), 0.68, sA1, sA2, sA3);
    }

    inline void processSamplePair(float& ioL,
                                  float& ioR,
                                  float warmthDb,
                                  float mudCutDb,
                                  float presenceDb,
                                  float airShelfDb) noexcept
    {
        // パッシブ並列ブランチの重み係数（直列ではなく並列で元信号に加算・減算される）
        const double wGain = (std::pow(10.0, std::clamp(static_cast<double>(warmthDb),   -8.0, 8.0) / 20.0) - 1.0);
        const double mGain = (std::pow(10.0, std::clamp(static_cast<double>(mudCutDb),   -8.0, 4.0) / 20.0) - 1.0);
        const double pGain = (std::pow(10.0, std::clamp(static_cast<double>(presenceDb), -6.0, 8.0) / 20.0) - 1.0);
        const double sGain = (std::pow(10.0, std::clamp(static_cast<double>(airShelfDb), -6.0,10.0) / 20.0) - 1.0);

        ioL = processChannel(0, ioL, wGain, mGain, pGain, sGain);
        ioR = processChannel(1, ioR, wGain, mGain, pGain, sGain);
    }

private:
    void setupSvf(double freqHz, double q, double& a1, double& a2, double& a3) const noexcept
    {
        const double g = std::tan(3.14159265358979323846 * std::clamp(freqHz, 20.0, sampleRate * 0.45) / sampleRate);
        const double k = 1.0 / std::max(0.25, q);
        a1 = 1.0 / (1.0 + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }

    inline float processChannel(int ch,
                                float in,
                                double wGain,
                                double mGain,
                                double pGain,
                                double sGain) noexcept
    {
        const double x = static_cast<double>(in);

        // 1. 120Hz Warmth (Lowpass + Bandpass Inductor Contour)
        const double wv3 = x - warmthIc2[ch];
        const double wv1 = wA1 * warmthIc1[ch] + wA2 * wv3;
        const double wv2 = warmthIc2[ch] + wA2 * warmthIc1[ch] + wA3 * wv3;
        warmthIc1[ch] = 2.0 * wv1 - warmthIc1[ch];
        warmthIc2[ch] = 2.0 * wv2 - warmthIc2[ch];
        const double branchWarmth = (wv2 * 0.70 + wv1 * 0.55) * wGain;

        // 2. 340Hz Mud Dip (Bandpass)
        const double mv3 = x - mudCutIc2[ch];
        const double mv1 = mA1 * mudCutIc1[ch] + mA2 * mv3;
        const double mv2 = mudCutIc2[ch] + mA2 * mudCutIc1[ch] + mA3 * mv3;
        mudCutIc1[ch] = 2.0 * mv1 - mudCutIc1[ch];
        mudCutIc2[ch] = 2.0 * mv2 - mudCutIc2[ch];
        const double branchMud = (mv1 * 0.689) * mGain;

        // 3. 2.7kHz Presence Bell (Bandpass)
        const double pv3 = x - presenceIc2[ch];
        const double pv1 = pA1 * presenceIc1[ch] + pA2 * pv3;
        const double pv2 = presenceIc2[ch] + pA2 * presenceIc1[ch] + pA3 * pv3;
        presenceIc1[ch] = 2.0 * pv1 - presenceIc1[ch];
        presenceIc2[ch] = 2.0 * pv2 - presenceIc2[ch];
        const double branchPres = (pv1 * 1.11) * pGain;

        // 4. 16kHz Inductor Air Shelf (Highpass + Resonant Bell)
        const double sv3 = x - silkAirIc2[ch];
        const double sv1 = sA1 * silkAirIc1[ch] + sA2 * sv3;
        const double sv2 = silkAirIc2[ch] + sA2 * silkAirIc1[ch] + sA3 * sv3;
        silkAirIc1[ch] = 2.0 * sv1 - silkAirIc1[ch];
        silkAirIc2[ch] = 2.0 * sv2 - silkAirIc2[ch];
        const double hpAir = x - (1.0 / 0.68) * sv1 - sv2;
        // インダクタコアの微細な高域倍音飽和
        const double branchAir = std::tanh((hpAir * 0.75 + sv1 * 0.65) * 1.15) * sGain;

        const double sum = x + branchWarmth + branchMud + branchPres + branchAir;
        return static_cast<float>(sum);
    }

    double sampleRate = 48000.0;
    double wA1 = 0.0, wA2 = 0.0, wA3 = 0.0;
    double mA1 = 0.0, mA2 = 0.0, mA3 = 0.0;
    double pA1 = 0.0, pA2 = 0.0, pA3 = 0.0;
    double sA1 = 0.0, sA2 = 0.0, sA3 = 0.0;

    double warmthIc1[2]{}, warmthIc2[2]{};
    double mudCutIc1[2]{}, mudCutIc2[2]{};
    double presenceIc1[2]{}, presenceIc2[2]{};
    double silkAirIc1[2]{}, silkAirIc2[2]{};
};

// =============================================================================
// 4. 統合アナログ真空管＆光学マスタリングスイート (AnalogTubeAndOptoMasteringSuite)
// =============================================================================
class AnalogTubeAndOptoMasteringSuite
{
public:
    void prepare(double newSampleRate) noexcept
    {
        la2aOpto.prepare(newSampleRate);
        fairchild670.prepare(newSampleRate);
        massivePassive.prepare(newSampleRate);
    }

    void reset() noexcept
    {
        la2aOpto.reset();
        fairchild670.reset();
        massivePassive.reset();
    }

    inline void processSamplePair(float& ioL,
                                  float& ioR,
                                  float optoSmooth01,
                                  float variMuGlue01,
                                  float inductorAir01) noexcept
    {
        // 黄金のボーカルマスタリング順序: LA-2A (光学式レベリング) → Massive Passive (並列パッシブLC造形) → Fairchild 670 (Lat/Vert Vari-Mu グルー)
        la2aOpto.processSamplePair(ioL, ioR, optoSmooth01, false, 0.35f);

        const float eqAmt = std::clamp(inductorAir01, 0.0f, 1.0f);
        if (eqAmt > 0.001f)
        {
            massivePassive.processSamplePair(
                ioL, ioR,
                1.6f * eqAmt,   // +1.6 dB 120Hz Inductor Warmth
               -1.4f * eqAmt,   // -1.4 dB 340Hz Mud Cut
                1.5f * eqAmt,   // +1.5 dB 2.7kHz Forwardness
                2.8f * eqAmt);  // +2.8 dB 16kHz Silk Air
        }

        fairchild670.processSamplePair(ioL, ioR, variMuGlue01, true);
    }

    const TeletronixLa2aT4bOptoAttenuator&    getLa2aOpto() const noexcept       { return la2aOpto; }
    const Fairchild670VariMuTubeCompressor&   getFairchild670() const noexcept   { return fairchild670; }
    const ManleyMassivePassiveInductorShelf&  getMassivePassive() const noexcept { return massivePassive; }

private:
    TeletronixLa2aT4bOptoAttenuator   la2aOpto;
    Fairchild670VariMuTubeCompressor  fairchild670;
    ManleyMassivePassiveInductorShelf massivePassive;
};

} // namespace analog_mastering
} // namespace gzzio

namespace vg
{
namespace analog_mastering = ::gzzio::analog_mastering;
} // namespace vg
