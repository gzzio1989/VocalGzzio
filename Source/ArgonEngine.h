#pragma once

#include <cmath>
#include <vector>
#include <algorithm>
#include <juce_core/juce_core.h>

namespace gz { namespace argon
{

//==============================================================================
// Argon Velvet Engine (アルゴン・ベルベット音響機関)
// -----------------------------------------------------------------------------
// 既存の「全帯域 tanh サチュレーション」「固定シェルフEQ」「単純ダブラー」では
// 原理的に解決できなかった人間の声の3大課題を、声の物理構造（母音周期・子音摩擦・
// 両耳間直交位相）に基づいてゼロ遅延・ゼロ確保で解決する独自DSPエンジン。
//
//  1. Voiced-Gated Asymmetric Triode & Chest Sub-Anchor (ベルベット倍音 / velvet)
//     母音帯域(150-2200Hz)と摩擦子音帯域(4.8kHz+)の瞬時エネルギー比から「母音度」を
//     サンプル単位で追跡。母音が鳴っている瞬間だけ、胸鳴り基音(135Hz)の偶次倍音補強と
//     非対称三極管(Triode)2次倍音を注入し、サ行・息が来た瞬間(1.2ms)に自動で素通しへ
//     逃がす。これにより「太く温かいのにサ行が一切ザラつかない」音質を実現する。
//
//  2. Crest-Protected Dynamic 14kHz Silk Air Exciter (シルク空気感 / silk)
//     8.5kHz以上の成分から軟飽和で超高域倍音を生成し、11.5kHz以上のみを抽出。
//     小声やささやき・語尾ではふわっと開き、強声や強い歯擦音では自動で引っ込む
//     クレスト保護エンベロープにより、耳に刺さらないシルクの空気感を付与する。
//
//  3. Mono-Safe Quadrature All-Pass Binaural Halo (立体ハロー空間 / halo)
//     11.7ms / 19.3ms の黄金比マイクロ反射を直交 LFO (sin/cos) で変調し、2段の
//     シュレーダー全域通過拡散器(All-Pass Diffuser)を通して Side 成分(L+, R-)へ
//     直交注入する。スマホ等のモノラル再生(L+R)では完全相殺されてコムフィルタが
//     一切起きず、ステレオ再生では声の周囲に立体的な光の輪(Halo)が浮かぶ。
//  4. Dynamic Singer's Formant Focus & Anti-Harshness Guard (声芯フォーカス / focus) [v4.5.0]
//     歌詞の明瞭度と伴奏抜けを決定づけるシンガーズ・フォルマント帯域(1.85kHz-3.65kHz)を抽出し、
//     小〜中声量のフレーズでは上向きハーモニック圧縮で声の芯を前へ押し出しつつ、張り上げ(ベルティング)や
//     強い歯擦音では自動で軟飽和クランプをかけて3kHz帯の耳刺さりを防ぐ。
//==============================================================================
class VelvetEngine
{
public:
    void prepare (double sampleRate) noexcept
    {
        sr = sampleRate > 8000.0 ? sampleRate : 48000.0;
        const float fs = (float) sr;

        // 1-pole filter coefficients
        auto pole = [fs] (float hz) noexcept
        {
            return 1.0f - std::exp (-2.0f * juce::MathConstants<float>::pi * hz / fs);
        };

        cVoicedLp  = pole (1800.0f);
        cVoicedHp  = pole (130.0f);
        cSibHp     = pole (4800.0f);
        cChestHp   = pole (90.0f);
        cChestLp   = pole (215.0f);
        cChestDc   = pole (32.0f);
        cTriodeLp  = pole (3600.0f);
        cTriodeDc  = pole (28.0f);
        cFocusHp   = pole (1850.0f);
        cFocusLp   = pole (3650.0f);
        cAirHp1    = pole (8200.0f);
        cAirHp2    = pole (11200.0f);
        cHaloHp    = pole (260.0f);
        cHaloLp    = pole (7200.0f);

        // Envelope time constants
        vAtk   = 1.0f - std::exp (-1.0f / (0.0025f * fs));
        vRel   = 1.0f - std::exp (-1.0f / (0.0250f * fs));
        sAtk   = 1.0f - std::exp (-1.0f / (0.0008f * fs));
        sRel   = 1.0f - std::exp (-1.0f / (0.0180f * fs));
        fAtk   = 1.0f - std::exp (-1.0f / (0.0018f * fs));
        fRel   = 1.0f - std::exp (-1.0f / (0.0220f * fs));
        ratioFall = 1.0f - std::exp (-1.0f / (0.0012f * fs));  // instant sibilant guard
        ratioRise = 1.0f - std::exp (-1.0f / (0.0160f * fs));
        paramSlew = 1.0f - std::exp (-1.0f / (0.0080f * fs));

        // Halo ring & all-pass buffers (~45 ms max at 192 kHz = 8640 -> 16384 power of 2)
        haloMask = 16383;
        haloBuf.assign ((size_t) (haloMask + 1), 0.0f);
        ap1Len = std::max (8, (int) std::lround (0.0031 * sr));
        ap2Len = std::max (8, (int) std::lround (0.0047 * sr));
        ap1Buf.assign ((size_t) ap1Len, 0.0f);
        ap2Buf.assign ((size_t) ap2Len, 0.0f);

        reset();
    }

    // 1次 ADAA (Antiderivative Anti-Aliasing) tanh:
    // F1(u) = ln(cosh(u)) = |u| + ln(1 + exp(-2|u|)) - ln(2) により折り返し歪みをゼロ遅延で抑制
    static inline float adaaTanhF1 (float u) noexcept
    {
        const float au = std::abs (u);
        return au + std::log (1.0f + std::exp (-2.0f * au)) - 0.69314718f;
    }

    static inline float adaaTanh (float u, float& uPrev) noexcept
    {
        const float du = u - uPrev;
        const float y = (std::abs (du) > 1.0e-4f)
            ? ((adaaTanhF1 (u) - adaaTanhF1 (uPrev)) / du)
            : std::tanh (0.5f * (u + uPrev));
        uPrev = u;
        return y;
    }

    void reset() noexcept
    {
        vLp = vHp = sHp = 0.0f;
        vEnv = sEnv = fEnv = voicedRatio = 0.0f;
        for (int ch = 0; ch < 2; ++ch)
        {
            chestHp[ch] = chestLp[ch] = chestDc[ch] = 0.0f;
            triLp[ch] = triDc[ch] = 0.0f;
            triPrevU[ch] = 0.24f;
            focHp[ch] = focLp[ch] = 0.0f;
            airHp1[ch] = airHp2[ch] = 0.0f;
        }
        haloHpState = haloLpState = 0.0f;
        haloWrite = ap1Idx = ap2Idx = 0;
        lfoPhase = 0.0f;
        velvetSm = focusSm = silkSm = haloSm = 0.0f;
        lastVelvetMeter = lastFocusMeter = lastSilkMeter = lastHaloMeter = lastVoicedMeter = 0.0f;
        std::fill (haloBuf.begin(), haloBuf.end(), 0.0f);
        std::fill (ap1Buf.begin(), ap1Buf.end(), 0.0f);
        std::fill (ap2Buf.begin(), ap2Buf.end(), 0.0f);
    }

    void setParams (float velvet01, float silk01, float halo01,
                    float crushGuard01, bool voiceOnly, float focus01 = 0.0f) noexcept
    {
        velvetTarget = voiceOnly ? juce::jlimit (0.0f, 1.0f, velvet01) : 0.0f;
        focusTarget  = voiceOnly ? juce::jlimit (0.0f, 1.0f, focus01)  : 0.0f;
        silkTarget   = juce::jlimit (0.0f, 1.0f, silk01);
        haloTarget   = juce::jlimit (0.0f, 1.0f, halo01);
        crushGuard   = juce::jlimit (0.0f, 1.0f, crushGuard01);
    }

    void process (float* L, float* R, int numSamples) noexcept
    {
        if (L == nullptr || numSamples <= 0) return;

        // Fast bypass when all Argon parameters are at 0 (bit-identical pass-through)
        if (velvetTarget <= 1.0e-4f && focusTarget <= 1.0e-4f && silkTarget <= 1.0e-4f && haloTarget <= 1.0e-4f
            && velvetSm <= 1.0e-4f && focusSm <= 1.0e-4f && silkSm <= 1.0e-4f && haloSm <= 1.0e-4f)
        {
            lastVelvetMeter = lastFocusMeter = lastSilkMeter = lastHaloMeter = 0.0f;
            return;
        }

        const float fs = (float) sr;
        const float lfoInc = juce::MathConstants<float>::twoPi * 0.58f / fs;
        const float tap1Base = 0.0117f * fs;   // 11.7 ms
        const float tap2Base = 0.0193f * fs;   // 19.3 ms
        const float tapMod   = 0.0011f * fs;   // +-1.1 ms quadrature excursion
        const float crushScale = 1.0f - 0.55f * crushGuard;

        float vMeterAcc = 0.0f, fMeterAcc = 0.0f, sMeterAcc = 0.0f, hMeterAcc = 0.0f, vrAcc = 0.0f;
        const bool hasHaloRing = ! haloBuf.empty() && ! ap1Buf.empty() && ! ap2Buf.empty();

        for (int n = 0; n < numSamples; ++n)
        {
            velvetSm += paramSlew * (velvetTarget - velvetSm);
            focusSm  += paramSlew * (focusTarget  - focusSm);
            silkSm   += paramSlew * (silkTarget   - silkSm);
            haloSm   += paramSlew * (haloTarget   - haloSm);

            const float inL = L[n];
            const float inR = (R != nullptr) ? R[n] : inL;
            const float mono = 0.5f * (inL + inR);

            // ---- 1. Voiced vs Sibilant Discrimination ----
            vHp += cVoicedHp * (mono - vHp);
            const float bandV = mono - vHp;
            vLp += cVoicedLp * (bandV - vLp);
            const float absV = std::abs (vLp);

            sHp += cSibHp * (mono - sHp);
            const float absS = std::abs (mono - sHp);

            vEnv += (absV > vEnv ? vAtk : vRel) * (absV - vEnv);
            sEnv += (absS > sEnv ? sAtk : sRel) * (absS - sEnv);

            const float rawRatio = (vEnv > 0.0003f)
                ? juce::jlimit (0.0f, 1.0f, (vEnv - 2.1f * sEnv) / (vEnv + sEnv + 1.0e-6f))
                : 0.0f;
            voicedRatio += (rawRatio < voicedRatio ? ratioFall : ratioRise) * (rawRatio - voicedRatio);
            vrAcc += voicedRatio;

            float outL = inL;
            float outR = inR;

            // ---- 2. Voiced-Gated Asymmetric Triode & Chest Sub-Anchor (VELVET) ----
            if (velvetSm > 1.0e-4f)
            {
                const float effVelvet = velvetSm * voicedRatio * crushScale;
                const float bias = 0.24f;
                const float tanhBias = 0.2354957f; // std::tanh(0.24f)
                const float drive = 1.0f + 1.65f * effVelvet;

                auto applyVelvetCh = [&] (float x, int ch) noexcept
                {
                    // Sub-fundamental chest anchor (90..215 Hz even-order reinforcement)
                    chestHp[ch] += cChestHp * (x - chestHp[ch]);
                    const float cBand = x - chestHp[ch];
                    chestLp[ch] += cChestLp * (cBand - chestLp[ch]);
                    const float c = chestLp[ch];
                    const float evenRect = (c * c) / (std::abs (c) + 0.06f);
                    chestDc[ch] += cChestDc * (evenRect - chestDc[ch]);
                    const float chestHarm = (evenRect - chestDc[ch]) * (0.42f * effVelvet);

                    // Mid-band asymmetric triode 2nd harmonic (1st-order ADAA anti-aliased)
                    triLp[ch] += cTriodeLp * (x - triLp[ch]);
                    const float mid = triLp[ch];
                    const float tri = (adaaTanh (mid * drive + bias, triPrevU[ch]) - tanhBias) / drive;
                    triDc[ch] += cTriodeDc * (tri - triDc[ch]);
                    const float triClean = tri - triDc[ch];

                    return x + chestHarm + (triClean - mid) * (0.58f * effVelvet);
                };

                outL = applyVelvetCh (outL, 0);
                if (R != nullptr) outR = applyVelvetCh (outR, 1);
                vMeterAcc += effVelvet * juce::jlimit (0.0f, 1.0f, vEnv * 6.0f);
            }

            // ---- 3. Dynamic Singer's Formant Focus & Articulation (FOCUS: 1.85-3.65 kHz) [v4.5.0] ----
            if (focusSm > 1.0e-4f)
            {
                focHp[0] += cFocusHp * (outL - focHp[0]);
                const float fBandL = outL - focHp[0];
                focLp[0] += cFocusLp * (fBandL - focLp[0]);
                const float fCoreL = focLp[0];

                float fCoreR = fCoreL;
                if (R != nullptr)
                {
                    focHp[1] += cFocusHp * (outR - focHp[1]);
                    const float fBandR = outR - focHp[1];
                    focLp[1] += cFocusLp * (fBandR - focLp[1]);
                    fCoreR = focLp[1];
                }

                const float absF = 0.5f * (std::abs (fCoreL) + std::abs (fCoreR));
                fEnv += (absF > fEnv ? fAtk : fRel) * (absF - fEnv);

                // Upward articulation lift for quiet/mid phrases + anti-harshness clamp on loud belts
                const float sibGuard   = 1.0f / (1.0f + 12.0f * sEnv);
                const float upwardLift = juce::jlimit (0.45f, 1.45f, 1.38f - 2.4f * fEnv);
                const float voiceGate  = 0.25f + 0.75f * voicedRatio;
                const float effFocus   = focusSm * sibGuard * upwardLift * voiceGate * crushScale;

                auto shapeFocus = [effFocus] (float fCore) noexcept
                {
                    const float shaped = (fCore * (1.0f + 1.5f * effFocus)) / (1.0f + 2.2f * std::abs (fCore));
                    return shaped * (0.68f * effFocus);
                };

                outL += shapeFocus (fCoreL);
                if (R != nullptr) outR += shapeFocus (fCoreR);
                fMeterAcc += effFocus * juce::jlimit (0.0f, 1.0f, (absF + absV) * 5.2f);
            }

            // ---- 4. Crest-Protected Dynamic 14kHz Silk Air Exciter (SILK) ----
            if (silkSm > 1.0e-4f)
            {
                // Whisper/soft boost & loud sibilant guard
                const float sibilantGuard = 1.0f / (1.0f + 18.0f * sEnv);
                const float whisperBloom  = juce::jlimit (0.65f, 1.40f, 1.35f - 1.8f * vEnv);
                const float effSilk = silkSm * sibilantGuard * whisperBloom;

                auto applySilkCh = [&] (float x, int ch) noexcept
                {
                    airHp1[ch] += cAirHp1 * (x - airHp1[ch]);
                    const float hf = x - airHp1[ch];
                    // Soft algebraic harmonic exciter (no hard clipping)
                    const float ex = (hf * (1.0f + 1.8f * std::abs (hf))) / (1.0f + 2.4f * hf * hf);
                    airHp2[ch] += cAirHp2 * (ex - airHp2[ch]);
                    const float ultraAir = ex - airHp2[ch];
                    return x + ultraAir * (0.85f * effSilk);
                };

                outL = applySilkCh (outL, 0);
                if (R != nullptr) outR = applySilkCh (outR, 1);
                sMeterAcc += effSilk * juce::jlimit (0.0f, 1.0f, (absS + absV) * 5.0f);
            }

            // ---- 5. Mono-Safe Quadrature All-Pass Binaural Halo (HALO) ----
            if (hasHaloRing && (haloSm > 1.0e-4f || haloWrite != 0))
            {
                haloHpState += cHaloHp * (mono - haloHpState);
                const float hBand = mono - haloHpState;
                haloLpState += cHaloLp * (hBand - haloLpState);
                haloBuf[(size_t) haloWrite] = haloLpState;

                if (haloSm > 1.0e-4f && R != nullptr)
                {
                    lfoPhase += lfoInc;
                    if (lfoPhase > juce::MathConstants<float>::twoPi)
                        lfoPhase -= juce::MathConstants<float>::twoPi;

                    const auto readInterp = [&] (float delaySamples) noexcept
                    {
                        float rp = (float) haloWrite - delaySamples;
                        while (rp < 0.0f) rp += (float) (haloMask + 1);
                        const int i0 = ((int) rp) & haloMask;
                        const int i1 = (i0 + 1) & haloMask;
                        const float frac = rp - std::floor (rp);
                        return haloBuf[(size_t) i0] * (1.0f - frac) + haloBuf[(size_t) i1] * frac;
                    };

                    const float t1 = readInterp (tap1Base + tapMod * std::sin (lfoPhase));
                    const float t2 = readInterp (tap2Base + tapMod * std::cos (lfoPhase));
                    const float rawHalo = 0.55f * (t1 - t2);

                    // 2-stage Schroeder All-Pass Diffuser
                    const float gAp = 0.618f;
                    const float d1 = ap1Buf[(size_t) ap1Idx];
                    const float v1 = rawHalo - gAp * d1;
                    ap1Buf[(size_t) ap1Idx] = v1;
                    const float y1 = d1 + gAp * v1;
                    if (++ap1Idx >= ap1Len) ap1Idx = 0;

                    const float d2 = ap2Buf[(size_t) ap2Idx];
                    const float v2 = y1 - gAp * d2;
                    ap2Buf[(size_t) ap2Idx] = v2;
                    const float y2 = d2 + gAp * v2;
                    if (++ap2Idx >= ap2Len) ap2Idx = 0;

                    // Auto-duck while center vocal is strong so lead stays up-front
                    const float vocalDuck = 1.0f / (1.0f + 3.2f * vEnv);
                    const float sideHalo = y2 * (0.48f * haloSm * vocalDuck);

                    // Pure orthogonal Side injection: L + R cancels 100% in mono!
                    outL += sideHalo;
                    outR -= sideHalo;
                    hMeterAcc += haloSm * vocalDuck * juce::jlimit (0.0f, 1.0f, (absV + absS) * 4.5f);
                }

                haloWrite = (haloWrite + 1) & haloMask;
            }

            // HALO の後で左右を別々に飽和させると、モノ合算時の相殺が崩れる。
            // 音量は本体の出力調整と既存の自動ヘッドルームで扱う。
            L[n] = outL;
            if (R != nullptr) R[n] = outR;
        }

        const float invN = 1.0f / (float) numSamples;
        lastVelvetMeter = juce::jlimit (0.0f, 1.0f, vMeterAcc * invN);
        lastFocusMeter  = juce::jlimit (0.0f, 1.0f, fMeterAcc * invN);
        lastSilkMeter   = juce::jlimit (0.0f, 1.0f, sMeterAcc * invN);
        lastHaloMeter   = juce::jlimit (0.0f, 1.0f, hMeterAcc * invN);
        lastVoicedMeter = juce::jlimit (0.0f, 1.0f, vrAcc * invN);
    }

    float velvetActivity() const noexcept { return lastVelvetMeter; }
    float focusActivity()  const noexcept { return lastFocusMeter; }
    float silkActivity()   const noexcept { return lastSilkMeter; }
    float haloActivity()   const noexcept { return lastHaloMeter; }
    float voicedRatioNow() const noexcept { return lastVoicedMeter; }

private:
    double sr = 48000.0;
    float cVoicedLp = 0.1f, cVoicedHp = 0.01f, cSibHp = 0.3f;
    float cChestHp = 0.01f, cChestLp = 0.03f, cChestDc = 0.004f;
    float cTriodeLp = 0.25f, cTriodeDc = 0.004f;
    float cFocusHp = 0.20f, cFocusLp = 0.35f;
    float cAirHp1 = 0.45f, cAirHp2 = 0.55f;
    float cHaloHp = 0.03f, cHaloLp = 0.40f;
    float vAtk = 0.01f, vRel = 0.001f, sAtk = 0.03f, sRel = 0.002f, fAtk = 0.015f, fRel = 0.0015f;
    float ratioFall = 0.02f, ratioRise = 0.002f, paramSlew = 0.003f;

    float vLp = 0.0f, vHp = 0.0f, sHp = 0.0f;
    float vEnv = 0.0f, sEnv = 0.0f, fEnv = 0.0f, voicedRatio = 0.0f;
    float chestHp[2] {}, chestLp[2] {}, chestDc[2] {};
    float triLp[2] {}, triDc[2] {}, triPrevU[2] { 0.24f, 0.24f };
    float focHp[2] {}, focLp[2] {};
    float airHp1[2] {}, airHp2[2] {};

    int haloMask = 16383, haloWrite = 0;
    std::vector<float> haloBuf;
    float haloHpState = 0.0f, haloLpState = 0.0f, lfoPhase = 0.0f;
    int ap1Len = 148, ap2Len = 225, ap1Idx = 0, ap2Idx = 0;
    std::vector<float> ap1Buf, ap2Buf;

    float velvetTarget = 0.0f, focusTarget = 0.0f, silkTarget = 0.0f, haloTarget = 0.0f, crushGuard = 0.0f;
    float velvetSm = 0.0f, focusSm = 0.0f, silkSm = 0.0f, haloSm = 0.0f;
    float lastVelvetMeter = 0.0f, lastFocusMeter = 0.0f, lastSilkMeter = 0.0f, lastHaloMeter = 0.0f, lastVoicedMeter = 0.0f;
};

}} // namespace gz::argon
