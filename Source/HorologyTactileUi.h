#pragma once

// =============================================================================
//  HorologyTactileUi.h — VocalGzzio v5.5.0
//  高級機械式時計工芸（Haute Horlogerie）× 宵闇ランタン＆花火ボケ光学UI ×
//  物理慣性・磁気デテント操作エンジン
// =============================================================================
//  【デザイン哲学・人間による工芸設計の追求】
//  AI生成特有の「均一なネオン枠線・平板なグラデーション・無機質なグリッド」を完全排除し、
//  1. スイス／ドイツ高級時計工芸（Breguet ギョーシェ彫り・A. Lange & Söhne 面取りアングラージュ・
//     サファイアクリスタル無反射ドーム屈折・ブルースティール＆ローズゴールド指針）
//  2. ドイツ光学機器（Leica M 真鍮鏡筒のローレット加工・被写界深度ボケ表現）
//  3. ユーザー指定リファレンス配色（深い宵闇のインディゴ林冠 `#040B1E`、温かな琥珀ランタン
//     `#FFAA2B` / `#FFD875`、夜空に咲くコーラル花火 `#FF6E8E`、星明かりのシアン `#3CE8D5`）
//  4. 触っているだけでワクワクする物理キネティック操作（慣性モーメント・磁気デテント吸着・
//     臨界減衰スプリング復帰・24バンド心理音響バーク地平線レーダー）
//  を統合した、唯一無二のビスポークUI/UXレンダリング＆物理インタラクション基盤です。
// =============================================================================

#include <juce_gui_basics/juce_gui_basics.h>
#include "MasteringProEngine.h"
#include "VocalScienceLab.h"
#include <array>
#include <cmath>
#include <algorithm>

namespace gzzio
{
namespace ui_horology
{

// =============================================================================
//  1. 宵闇ランタン＆花火ボケ 色彩体系 (Midnight Lantern & Firework Bokeh Palette)
// =============================================================================
//  リファレンス画像（宵闇の森と琥珀ランタン・花火の多重ボケ）から抽出した
//  3ゾーン黄金比カラーハーモニー（明度対比・彩度階層・色温度グラデーション）。
// =============================================================================
struct MidnightLanternPalette
{
    // Zone 1: 宵闇の天蓋・深藍シルエット (Deep Canopy & Indigo Velvet)
    static inline juce::Colour canopyAbyss()       noexcept { return juce::Colour(0xff030712); }
    static inline juce::Colour midnightIndigo()    noexcept { return juce::Colour(0xff071026); }
    static inline juce::Colour twilightNavy()      noexcept { return juce::Colour(0xff0d1c3d); }
    static inline juce::Colour panelObsidian()     noexcept { return juce::Colour(0xe8081228); }
    static inline juce::Colour cardVelvet()        noexcept { return juce::Colour(0xf20b1733); }

    // Zone 2: 琥珀ランタン・白熱フィラメント (Warm Amber Lantern & Incandescent Gold)
    static inline juce::Colour filamentCore()      noexcept { return juce::Colour(0xfffff8e2); }
    static inline juce::Colour champagneGold()     noexcept { return juce::Colour(0xffffdc82); }
    static inline juce::Colour lanternAmber()      noexcept { return juce::Colour(0xffffaa2b); }
    static inline juce::Colour antiqueBrass()      noexcept { return juce::Colour(0xffc89646); }
    static inline juce::Colour bronzeShadow()      noexcept { return juce::Colour(0xff6b481c); }

    // Zone 3: 夜空の花火コーラル＆星明かりシアン (Firework Coral & Starlight Cyan)
    static inline juce::Colour fireworkRose()      noexcept { return juce::Colour(0xffff6e8e); }
    static inline juce::Colour fireworkBlossom()   noexcept { return juce::Colour(0xffffa4bc); }
    static inline juce::Colour starlightCyan()     noexcept { return juce::Colour(0xff3ce8d5); }
    static inline juce::Colour celestialAzure()    noexcept { return juce::Colour(0xff52b6ff); }
    static inline juce::Colour violetNebula()      noexcept { return juce::Colour(0xff9c7cff); }

    // タイポグラフィ階層（目の疲れを防ぎつつ一瞬で読める暖白〜亜麻色）
    static inline juce::Colour textIvory()         noexcept { return juce::Colour(0xfffaf5eb); }
    static inline juce::Colour textWarmSilver()    noexcept { return juce::Colour(0xffd0dae8); }
    static inline juce::Colour textMutedSlate()    noexcept { return juce::Colour(0xff8a9bb5); }

    // 互換エイリアス（HorologyInteractiveDeck / VocalistPerformanceCoach 用）
    static inline juce::Colour lanternGold()        noexcept { return champagneGold(); }
    static inline juce::Colour champagneHighlight() noexcept { return filamentCore(); }
    static inline juce::Colour hairlineSlate()      noexcept { return textMutedSlate(); }
    static inline juce::Colour obsidianRecess()     noexcept { return panelObsidian(); }
    static inline juce::Colour brushedBrass()       noexcept { return antiqueBrass(); }
    static inline juce::Colour fireworkCoral()      noexcept { return fireworkRose(); }
    static inline juce::Colour warmIvoryText()      noexcept { return textIvory(); }

    // 値 (0.0 .. 1.0) に応じて「星明かりシアン -> 琥珀ランタン -> コーラル花火」へ連続遷移する有機色補間
    static inline juce::Colour temperatureGradient(float t01) noexcept
    {
        const float t = std::clamp(t01, 0.0f, 1.0f);
        if (t < 0.5f)
            return starlightCyan().interpolatedWith(lanternAmber(), t * 2.0f);
        return lanternAmber().interpolatedWith(fireworkRose(), (t - 0.5f) * 2.0f);
    }
};

// =============================================================================
//  2. 物理キネティック・ダイヤル力学＆磁気デテントエンジン (KineticDialPhysics)
// =============================================================================
//  ノブや2Dオーブを回した際の「重み・慣性・音楽的スイートスポットへの磁気吸着（カチッという
//  手応えの視覚化）・ダブルクリック時のバネ復帰」をリアルタイム物理演算します。
// =============================================================================
class KineticDialPhysics
{
public:
    struct DetentPoint
    {
        float normalizedPos = 0.5f;   // [0..1]
        float magneticRadius = 0.018f;// 吸着半径
        const char* label = "";       // スイートスポット名（例: "0 dB 標準", "黄金比 61.8%"）
    };

    KineticDialPhysics()
    {
        setStandardMusicalDetents();
    }

    void setStandardMusicalDetents() noexcept
    {
        numDetents = 5;
        detents[0] = { 0.000f, 0.012f, "MIN" };
        detents[1] = { 0.250f, 0.014f, "25% 控えめ" };
        detents[2] = { 0.500f, 0.020f, "50% センター基準" };
        detents[3] = { 0.618f, 0.016f, "61.8% 黄金比スイートスポット" };
        detents[4] = { 1.000f, 0.012f, "MAX" };
    }

    // ユーザーのドラッグ入力値 rawTarget01 に対して磁気デテント吸着とリップル発生を処理
    float applyMagneticDetent(float rawTarget01, bool fineModeActive = false) noexcept
    {
        const float clamped = std::clamp(rawTarget01, 0.0f, 1.0f);
        if (fineModeActive)
        {
            targetValue = clamped;
            activeDetentIndex = -1;
            return clamped;
        }

        int foundIdx = -1;
        float snapped = clamped;
        for (int i = 0; i < numDetents; ++i)
        {
            const float dist = std::fabs(clamped - detents[static_cast<size_t>(i)].normalizedPos);
            const float r    = detents[static_cast<size_t>(i)].magneticRadius;
            if (dist < r)
            {
                // 中心に近いほど強く引き寄せる3次エルミート磁気井戸ポテンシャル
                const float u = dist / r;
                const float pull = u * u * (3.0f - 2.0f * u);
                snapped = detents[static_cast<size_t>(i)].normalizedPos +
                          (clamped - detents[static_cast<size_t>(i)].normalizedPos) * pull * 0.25f;
                if (dist < r * 0.45f)
                    snapped = detents[static_cast<size_t>(i)].normalizedPos;
                foundIdx = i;
                break;
            }
        }

        if (foundIdx != -1 && foundIdx != activeDetentIndex)
        {
            // 新たにデテントに吸着した瞬間、金色のマイクロ波紋（ハプティック・リップル）を発火
            detentRippleProgress = 0.01f;
        }
        activeDetentIndex = foundIdx;
        targetValue = snapped;
        return snapped;
    }

    // 60fps タイマーで呼び出し：不足減衰バネ・ダンパ方程式による滑らかな指針追従
    void stepPhysics(float dtSeconds = 1.0f / 60.0f) noexcept
    {
        const float dt = std::clamp(dtSeconds, 0.001f, 0.050f);
        constexpr float kSpringStiffness = 210.0f; // ばね定数 k
        constexpr float kDamping         = 24.5f;  // 減衰係数 c（わずかに心地よいオーバーシュートを残す）

        const float displacement = displayValue - targetValue;
        const float accel = -kSpringStiffness * displacement - kDamping * velocity;
        velocity     += accel * dt;
        displayValue += velocity * dt;
        displayValue  = std::clamp(displayValue, -0.02f, 1.02f);

        if (std::fabs(displacement) < 0.0002f && std::fabs(velocity) < 0.001f)
        {
            displayValue = std::clamp(targetValue, 0.0f, 1.0f);
            velocity = 0.0f;
        }

        if (detentRippleProgress > 0.0f)
        {
            detentRippleProgress += dt * 2.8f;
            if (detentRippleProgress >= 1.0f)
                detentRippleProgress = 0.0f;
        }
    }

    void triggerSpringReturnTo(float defaultVal01) noexcept
    {
        targetValue = std::clamp(defaultVal01, 0.0f, 1.0f);
        velocity += (targetValue > displayValue) ? 1.8f : -1.8f;
        detentRippleProgress = 0.01f;
    }

    float getDisplayValue() const noexcept { return std::clamp(displayValue, 0.0f, 1.0f); }
    float getVelocity() const noexcept { return velocity; }
    float getDetentRippleProgress() const noexcept { return detentRippleProgress; }
    bool  isSnappedToDetent() const noexcept { return activeDetentIndex >= 0; }
    const char* getActiveDetentLabel() const noexcept
    {
        if (activeDetentIndex >= 0 && activeDetentIndex < numDetents)
            return detents[static_cast<size_t>(activeDetentIndex)].label;
        return "";
    }

private:
    std::array<DetentPoint, 8> detents {};
    int numDetents = 0;
    int activeDetentIndex = -1;

    float targetValue = 0.5f;
    float displayValue = 0.5f;
    float velocity = 0.0f;
    float detentRippleProgress = 0.0f;
};

// =============================================================================
//  3. 高級時計ギョーシェ彫り＆面取りアングラージュ描画器 (HorologyGuillocheRenderer)
// =============================================================================
//  スイス機械式時計の文字盤・ベゼル工芸をベクターグラフィックスで精密に再現：
//  - Soleil (サンレイ放射筋目仕上げ) ＆ Barleycorn (麦粒ギョーシェ波紋)
//  - Anglage (45度鏡面面取りベゼル：左上135°主光源＋右下ランタン反射光)
//  - Breguet Moon-Hand (ブレゲ針：中抜き三日月ポインター)
//  - Dandelion Filament Arc (たんぽぽ綿毛・白熱フィラメント発光アーク)
// =============================================================================
class HorologyGuillocheRenderer
{
public:
    // 背景またはカード上に、宵闇の多層ランタン玉ボケ（Depth-of-Field Bokeh）を描画
    static void drawAtmosphericLanternBokeh(juce::Graphics& g,
                                            juce::Rectangle<float> bounds,
                                            float animPhaseRad = 0.0f,
                                            int bokehDensity = 18)
    {
        juce::Graphics::ScopedSaveState state(g);
        g.reduceClipRegion(bounds.toNearestInt());

        // 1. 宵闇インディゴ天蓋から地平線の琥珀グローへの垂直大気グラデーション
        juce::ColourGradient skyGrad(
            MidnightLanternPalette::canopyAbyss(), bounds.getX(), bounds.getY(),
            MidnightLanternPalette::twilightNavy(), bounds.getX(), bounds.getBottom(), false);
        skyGrad.addColour(0.55, MidnightLanternPalette::midnightIndigo());
        skyGrad.addColour(0.88, juce::Colour(0xff141c38));
        g.setGradientFill(skyGrad);
        g.fillRect(bounds);

        // 2. 下部地平線から立ち昇るランタンの暖気グロー
        juce::ColourGradient horizonGlow(
            MidnightLanternPalette::lanternAmber().withAlpha(0.16f),
            bounds.getCentreX(), bounds.getBottom(),
            juce::Colours::transparentBlack,
            bounds.getCentreX(), bounds.getY() + bounds.getHeight() * 0.25f, true);
        horizonGlow.addColour(0.45, MidnightLanternPalette::fireworkRose().withAlpha(0.08f));
        g.setGradientFill(horizonGlow);
        g.fillRect(bounds);

        // 3. 黄金角 (137.508°) 配置による自然で非周期的な八角絞り玉ボケ（Octagonal Lens Aperture Bokeh）
        const int count = std::clamp(bokehDensity, 4, 36);
        for (int i = 0; i < count; ++i)
        {
            const float fi = static_cast<float>(i);
            const float goldenX = std::fmod(fi * 0.6180339887f + 0.13f, 1.0f);
            const float goldenY = std::fmod(fi * 0.3819660112f + 0.29f, 1.0f);

            const float driftX = std::sin(animPhaseRad * 0.5f + fi * 1.1f) * 6.0f;
            const float driftY = std::cos(animPhaseRad * 0.4f + fi * 0.9f) * 4.5f;

            const float cx = bounds.getX() + goldenX * bounds.getWidth() + driftX;
            // 下半分ほどランタン密度が高く、上半分は星明かりと花火が舞う分布
            const float biasedY = std::pow(goldenY, 0.78f);
            const float cy = bounds.getY() + (0.18f + 0.76f * biasedY) * bounds.getHeight() + driftY;

            const float radius = 14.0f + std::fmod(fi * 17.3f, 42.0f);
            const int colorType = i % 4;
            juce::Colour baseCol = (colorType == 0) ? MidnightLanternPalette::lanternAmber()
                                 : (colorType == 1) ? MidnightLanternPalette::champagneGold()
                                 : (colorType == 2) ? MidnightLanternPalette::fireworkRose()
                                                    : MidnightLanternPalette::starlightCyan();

            const float alpha = 0.045f + 0.035f * (1.0f - radius / 58.0f);

            // レンズの球面収差を模した「中心が柔らかく縁がわずかに明るい」ソフト玉ボケ
            juce::ColourGradient bokehGrad(
                baseCol.withAlpha(alpha * 0.75f), cx, cy,
                baseCol.withAlpha(0.0f), cx + radius, cy, true);
            bokehGrad.addColour(0.72, baseCol.withAlpha(alpha * 1.15f));
            g.setGradientFill(bokehGrad);
            g.fillEllipse(cx - radius, cy - radius, radius * 2.0f, radius * 2.0f);
        }
    }

    // 機械式時計のギョーシェ彫り＆アングラージュ面取りクロノ・ダイヤルを描画
    static void drawMasterChronoDial(juce::Graphics& g,
                                     juce::Rectangle<float> bounds,
                                     float normalizedValue01,
                                     const juce::String& titleText,
                                     const juce::String& formattedValueText,
                                     juce::Colour primaryAccent,
                                     bool isHovered = false,
                                     bool isDragging = false,
                                     float detentRipple01 = 0.0f)
    {
        const float val = std::clamp(normalizedValue01, 0.0f, 1.0f);
        const float cx  = bounds.getCentreX();
        const float cy  = bounds.getCentreY();
        const float outerR = std::min(bounds.getWidth(), bounds.getHeight()) * 0.44f;
        if (outerR < 10.0f)
            return;

        // 角度範囲: 7時半 (-135° = -2.356 rad) から 4時半 (+135° = +2.356 rad)
        constexpr float kStartAng = -2.35619449f;
        constexpr float kEndAng   =  2.35619449f;
        const float currentAng    = kStartAng + val * (kEndAng - kStartAng);

        // 1. 外部ドロップシャドウ（左上光源に対する右下への柔らかい投影）
        for (int s = 3; s >= 1; --s)
        {
            const float expand = static_cast<float>(s) * 2.2f;
            const float offset = static_cast<float>(s) * 1.4f;
            g.setColour(juce::Colours::black.withAlpha(0.18f / static_cast<float>(s)));
            g.fillEllipse(cx - outerR - expand + offset * 0.6f,
                          cy - outerR - expand + offset,
                          (outerR + expand) * 2.0f,
                          (outerR + expand) * 2.0f);
        }

        // 2. デテント吸着時のゴールドハプティック波紋
        if (detentRipple01 > 0.001f && detentRipple01 < 1.0f)
        {
            const float ripR = outerR * (1.02f + 0.28f * detentRipple01);
            const float ripAlpha = (1.0f - detentRipple01) * 0.65f;
            g.setColour(MidnightLanternPalette::champagneGold().withAlpha(ripAlpha));
            g.drawEllipse(cx - ripR, cy - ripR, ripR * 2.0f, ripR * 2.0f, 2.2f);
        }

        // 3. 外周チャプターリング（分目盛り＆12箇所の植字アプライド・ゴールドインデックス）
        const float tickOuterR = outerR * 1.08f;
        const float tickInnerR = outerR * 0.98f;
        constexpr int kNumTicks = 36;
        for (int i = 0; i <= kNumTicks; ++i)
        {
            const float t = static_cast<float>(i) / static_cast<float>(kNumTicks);
            const float ang = kStartAng + t * (kEndAng - kStartAng) - juce::MathConstants<float>::halfPi;
            const bool isMajor = (i % 6 == 0);
            const bool isLit   = (t <= val + 0.005f);

            const float r1 = isMajor ? (tickInnerR - 2.0f) : tickInnerR;
            const float r2 = isMajor ? (tickOuterR + 1.5f) : tickOuterR;
            const float x1 = cx + std::cos(ang) * r1;
            const float y1 = cy + std::sin(ang) * r1;
            const float x2 = cx + std::cos(ang) * r2;
            const float y2 = cy + std::sin(ang) * r2;

            juce::Colour tickCol = isLit
                ? primaryAccent.interpolatedWith(MidnightLanternPalette::filamentCore(), isMajor ? 0.45f : 0.15f)
                : MidnightLanternPalette::textMutedSlate().withAlpha(isMajor ? 0.55f : 0.24f);

            g.setColour(tickCol);
            g.drawLine(x1, y1, x2, y2, isMajor ? 2.0f : 1.0f);
        }

        // 4. 白熱フィラメント・プログレスアーク（背景溝＋多重グロー発光アーク）
        const float arcR = outerR * 0.91f;
        juce::Path trackPath;
        trackPath.addCentredArc(cx, cy, arcR, arcR, 0.0f, kStartAng, kEndAng, true);
        g.setColour(juce::Colour(0xff050b1a).withAlpha(0.90f));
        g.strokePath(trackPath, juce::PathStrokeType(4.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        if (val > 0.003f)
        {
            juce::Path valPath;
            valPath.addCentredArc(cx, cy, arcR, arcR, 0.0f, kStartAng, currentAng, true);

            // 外側のソフトランタンブルーム
            g.setColour(primaryAccent.withAlpha(isDragging ? 0.42f : 0.26f));
            g.strokePath(valPath, juce::PathStrokeType(8.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            // メインの金〜アクセントグラデーションアーク
            juce::ColourGradient arcGrad(
                MidnightLanternPalette::starlightCyan(), cx - arcR, cy + arcR,
                primaryAccent, cx + arcR, cy - arcR, false);
            arcGrad.addColour(0.55, MidnightLanternPalette::lanternAmber());
            g.setGradientFill(arcGrad);
            g.strokePath(valPath, juce::PathStrokeType(3.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            // 中心の白熱フィラメント芯線
            g.setColour(MidnightLanternPalette::filamentCore().withAlpha(0.85f));
            g.strokePath(valPath, juce::PathStrokeType(1.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // 5. 45度鏡面面取りアングラージュ真鍮ベゼル (Chamfered Brass Bezel)
        const float bezelR = outerR * 0.81f;
        juce::ColourGradient bezelGrad(
            MidnightLanternPalette::champagneGold().withAlpha(isHovered ? 0.95f : 0.80f),
            cx - bezelR * 0.7f, cy - bezelR * 0.7f,
            MidnightLanternPalette::bronzeShadow().withAlpha(0.90f),
            cx + bezelR * 0.7f, cy + bezelR * 0.7f, false);
        bezelGrad.addColour(0.50, MidnightLanternPalette::antiqueBrass().withAlpha(0.75f));
        g.setGradientFill(bezelGrad);
        g.fillEllipse(cx - bezelR, cy - bezelR, bezelR * 2.0f, bezelR * 2.0f);

        // 6. 文字盤インナープレート（深藍ソレイユ＆ギョーシェ彫り模様）
        const float faceR = bezelR - 2.6f;
        juce::ColourGradient faceGrad(
            juce::Colour(0xff10224c), cx - faceR * 0.35f, cy - faceR * 0.35f,
            juce::Colour(0xff040a18), cx + faceR * 0.75f, cy + faceR * 0.75f, true);
        g.setGradientFill(faceGrad);
        g.fillEllipse(cx - faceR, cy - faceR, faceR * 2.0f, faceR * 2.0f);

        // ギョーシェ同心波紋リング（Engine-Turned Guilloché Rings）
        g.setColour(MidnightLanternPalette::champagneGold().withAlpha(0.09f));
        for (float rr = faceR * 0.32f; rr < faceR * 0.92f; rr += faceR * 0.18f)
            g.drawEllipse(cx - rr, cy - rr, rr * 2.0f, rr * 2.0f, 0.75f);

        // アストロラーベ子午線・天球赤道クロスヘア
        g.setColour(MidnightLanternPalette::starlightCyan().withAlpha(0.11f));
        g.drawLine(cx - faceR * 0.82f, cy, cx + faceR * 0.82f, cy, 0.7f);
        g.drawLine(cx, cy - faceR * 0.82f, cx, cy + faceR * 0.82f, 0.7f);

        // 7. ブレゲ風ポインター針（Breguet Eccentric Moon Hand）
        const float handAngle = currentAng - juce::MathConstants<float>::halfPi;
        const float cosA = std::cos(handAngle);
        const float sinA = std::sin(handAngle);

        const float tailLen = faceR * 0.22f;
        const float tipLen  = faceR * 0.88f;
        const float eyeDist = faceR * 0.62f;

        const float tx0 = cx - cosA * tailLen;
        const float ty0 = cy - sinA * tailLen;
        const float tx1 = cx + cosA * tipLen;
        const float ty1 = cy + sinA * tipLen;
        const float ex  = cx + cosA * eyeDist;
        const float ey  = cy + sinA * eyeDist;

        // 針のドロップシャドウ
        g.setColour(juce::Colours::black.withAlpha(0.55f));
        g.drawLine(tx0 + 1.2f, ty0 + 1.8f, tx1 + 1.2f, ty1 + 1.8f, 2.6f);

        // 針本体（シャンパンゴールド〜先端白熱コア）
        g.setColour(MidnightLanternPalette::champagneGold());
        g.drawLine(tx0, ty0, tx1, ty1, 2.2f);
        g.fillEllipse(ex - 3.6f, ey - 3.6f, 7.2f, 7.2f);
        g.setColour(juce::Colour(0xff061024));
        g.fillEllipse(ex - 1.8f, ey - 1.8f, 3.6f, 3.6f);

        // 先端ルミナス・ジュエルドット
        g.setColour(primaryAccent.withAlpha(0.55f));
        g.fillEllipse(tx1 - 4.5f, ty1 - 4.5f, 9.0f, 9.0f);
        g.setColour(MidnightLanternPalette::filamentCore());
        g.fillEllipse(tx1 - 2.2f, ty1 - 2.2f, 4.4f, 4.4f);

        // 中心ピボット・シャトン（金枠ルビー軸受）
        g.setColour(MidnightLanternPalette::champagneGold());
        g.fillEllipse(cx - 4.5f, cy - 4.5f, 9.0f, 9.0f);
        g.setColour(MidnightLanternPalette::fireworkRose());
        g.fillEllipse(cx - 2.3f, cy - 2.3f, 4.6f, 4.6f);

        // 8. サファイアクリスタル・ドーム反射ハイライト（左上三日月グレア）
        juce::Path glassGlare;
        glassGlare.addCentredArc(cx, cy, faceR * 0.88f, faceR * 0.88f, 0.0f, -0.95f, 0.25f, true);
        g.setColour(juce::Colours::white.withAlpha(isHovered ? 0.22f : 0.14f));
        g.strokePath(glassGlare, juce::PathStrokeType(2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // 9. 下部ラベル＆数値リードアウト（高視認性エディトリアル・タイポグラフィ）
        if (titleText.isNotEmpty())
        {
            g.setColour(MidnightLanternPalette::textIvory());
            g.setFont(juce::Font(juce::FontOptions(12.5f, juce::Font::bold)));
            g.drawText(titleText,
                       juce::Rectangle<float>(bounds.getX() - 4.0f, cy + outerR + 3.0f, bounds.getWidth() + 8.0f, 16.0f),
                       juce::Justification::centred, false);
        }
        if (formattedValueText.isNotEmpty())
        {
            g.setColour(isDragging ? MidnightLanternPalette::filamentCore() : MidnightLanternPalette::champagneGold());
            g.setFont(juce::Font(juce::FontOptions(11.5f, juce::Font::bold)));
            g.drawText(formattedValueText,
                       juce::Rectangle<float>(bounds.getX() - 4.0f, cy + outerR + 18.0f, bounds.getWidth() + 8.0f, 15.0f),
                       juce::Justification::centred, false);
        }
    }
};

// =============================================================================
//  4. 16臨界帯域バーク心理音響ホライゾン＆マスタリング・テレメトリ HUD
// =============================================================================
//  `VocalScienceLab::BarkMaskingAnalyzer` と `MasteringProEngine` から送られる
//  リアルタイム音響科学テレメトリを、宵闇のランタン地平線＆星座レーダーとして可視化。
//  AI的な単調な棒グラフではなく、連続スプライン曲線・シンガーズフォルマント天頂グロー・
//  歌声明瞭度バッジ（VAI: Vocal Audibility Index）として美しく描画します。
// =============================================================================
class BarkAcousticRadarHud
{
public:
    static void drawPsychoacousticHorizon(
        juce::Graphics& g,
        juce::Rectangle<float> bounds,
        const gzzio::science::BarkMaskingAnalyzer::BarkTelemetry& bark,
        const gzzio::mastering::MasteringProEngine::TelemetrySnapshot& masteringSnap,
        float animPhaseRad = 0.0f)
    {
        if (bounds.getWidth() < 80.0f || bounds.getHeight() < 36.0f)
            return;

        juce::Graphics::ScopedSaveState state(g);

        // 1. ガラスフレーム背景（深藍ベルベット＋真鍮ヘアライン枠）
        g.setColour(MidnightLanternPalette::cardVelvet());
        g.fillRoundedRectangle(bounds, 10.0f);

        juce::ColourGradient borderGrad(
            MidnightLanternPalette::champagneGold().withAlpha(0.55f), bounds.getX(), bounds.getY(),
            MidnightLanternPalette::starlightCyan().withAlpha(0.35f), bounds.getRight(), bounds.getBottom(), false);
        g.setGradientFill(borderGrad);
        g.drawRoundedRectangle(bounds.reduced(0.5f), 10.0f, 1.2f);

        const auto plotArea = bounds.reduced(10.0f, 22.0f).withTrimmedBottom(4.0f);
        if (plotArea.getWidth() <= 20.0f || plotArea.getHeight() <= 12.0f)
            return;

        // 2. 4大音響音声学ゾーンの背景ガイド（F1 Warmth / F2 Clarity / Singer's Formant / Silk Air）
        const float w = plotArea.getWidth();
        const float x0 = plotArea.getX();
        const float yTop = plotArea.getY();
        const float yBot = plotArea.getBottom();

        // Singer's Formant (2.6kHz〜3.8kHz = Band 9..11) の黄金スポットライト
        const float sfX1 = x0 + (9.0f / 15.0f) * w;
        const float sfX2 = x0 + (11.5f / 15.0f) * w;
        juce::ColourGradient sfSpot(
            MidnightLanternPalette::lanternAmber().withAlpha(0.14f), (sfX1 + sfX2) * 0.5f, yTop,
            juce::Colours::transparentBlack, (sfX1 + sfX2) * 0.5f, yBot, false);
        g.setGradientFill(sfSpot);
        g.fillRect(sfX1, yTop, sfX2 - sfX1, plotArea.getHeight());

        // 水平デシベル目盛り線
        g.setColour(MidnightLanternPalette::textMutedSlate().withAlpha(0.14f));
        for (float frac : { 0.25f, 0.50f, 0.75f })
        {
            const float yy = yTop + frac * plotArea.getHeight();
            g.drawHorizontalLine(static_cast<int>(yy), x0, plotArea.getRight());
        }

        // 3. 伴奏マスキング曲線（Starlight Cyan / Indigo Veil）とボーカルスペクトル曲線（Amber / Rose Glow）
        constexpr size_t N = gzzio::science::BarkMaskingAnalyzer::kNumBarkBands;
        juce::Path bgmPath, bgmFill, vocalPath, vocalFill;

        auto dbToY = [&](float dbVal, size_t idx) noexcept -> float
        {
            // 無音時でも美しい待機呼吸アニメーションを表示
            const float idleWave = -46.0f + 6.0f * std::sin(animPhaseRad + static_cast<float>(idx) * 0.45f);
            const float effectiveDb = std::max(dbVal, idleWave);
            const float norm = std::clamp((effectiveDb + 62.0f) / 56.0f, 0.04f, 0.96f);
            return yBot - norm * plotArea.getHeight();
        };

        for (size_t i = 0; i < N; ++i)
        {
            const float px = x0 + (static_cast<float>(i) / static_cast<float>(N - 1)) * w;
            const float vy = dbToY(bark.vocalDb[i], i);
            const float my = dbToY(bark.bgmMaskDb[i] - 4.0f, i + 3);

            if (i == 0)
            {
                vocalPath.startNewSubPath(px, vy);
                vocalFill.startNewSubPath(px, yBot);
                vocalFill.lineTo(px, vy);

                bgmPath.startNewSubPath(px, my);
                bgmFill.startNewSubPath(px, yBot);
                bgmFill.lineTo(px, my);
            }
            else
            {
                vocalPath.lineTo(px, vy);
                vocalFill.lineTo(px, vy);
                bgmPath.lineTo(px, my);
                bgmFill.lineTo(px, my);
            }
        }
        vocalFill.lineTo(plotArea.getRight(), yBot);
        vocalFill.closeSubPath();
        bgmFill.lineTo(plotArea.getRight(), yBot);
        bgmFill.closeSubPath();

        // 伴奏マスキング層の描画
        juce::ColourGradient bgmGrad(
            MidnightLanternPalette::starlightCyan().withAlpha(0.20f), x0, yTop,
            MidnightLanternPalette::midnightIndigo().withAlpha(0.02f), x0, yBot, false);
        g.setGradientFill(bgmGrad);
        g.fillPath(bgmFill);
        g.setColour(MidnightLanternPalette::starlightCyan().withAlpha(0.45f));
        g.strokePath(bgmPath, juce::PathStrokeType(1.3f));

        // ボーカル主役層の描画（琥珀ランタン〜コーラル花火のグラデーション）
        juce::ColourGradient vocGrad(
            MidnightLanternPalette::lanternAmber().withAlpha(0.42f), x0, yTop,
            MidnightLanternPalette::fireworkRose().withAlpha(0.04f), x0, yBot, false);
        g.setGradientFill(vocGrad);
        g.fillPath(vocalFill);

        g.setColour(MidnightLanternPalette::champagneGold());
        g.strokePath(vocalPath, juce::PathStrokeType(2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // 4. 上部ヘッダー：マスタリング・シグネチャ名 ＆ 歌声明瞭度指数 (VAI) ＆ ISO 226 / ADAA バッジ
        const auto& sig = gzzio::mastering::kMasteringSignatures[
            static_cast<size_t>(std::clamp(masteringSnap.activeSignature, 0, 4))];

        g.setColour(MidnightLanternPalette::champagneGold());
        g.setFont(juce::Font(juce::FontOptions(11.5f, juce::Font::bold)));
        g.drawText(juce::String::fromUTF8("✦ 音響解析 & マスタリング: ") + juce::String::fromUTF8(sig.nameJa),
                   bounds.reduced(10.0f, 3.0f).removeFromTop(18.0f),
                   juce::Justification::centredLeft, false);

        const juce::String vaiText = juce::String::fromUTF8("歌声抜け指数 ")
            + juce::String(bark.vocalAudibilityIndex, 0) + "%"
            + juce::String::fromUTF8(" ｜ 共振抑制 -")
            + juce::String(masteringSnap.harshnessCutDb, 1) + "dB";

        g.setColour(MidnightLanternPalette::starlightCyan());
        g.drawText(vaiText,
                   bounds.reduced(10.0f, 3.0f).removeFromTop(18.0f),
                   juce::Justification::centredRight, false);
    }
};

// =============================================================================
//  5. 天球アストロラーベ 2D マスタリング・コンステレーション・オーブ描画器
// =============================================================================
//  2D XY パッド（横軸: 透明感・シルク高域、縦軸: 真空管倍音密度・声芯の押し出し）を
//  中世の天体観測儀（Astrolabe）と宵闇のたんぽぽ花火（Dandelion Firework Filament）の
//  融合として描画。4隅のプロエンジニア・シグネチャ星座と現在位置を黄金の軌跡で結びます。
// =============================================================================
class AstrolabeConstellationOrbRenderer
{
public:
    static void drawConstellationOrb(
        juce::Graphics& g,
        juce::Rectangle<float> bounds,
        float normX01,
        float normY01,
        float vocalEnergy01,
        float sibilantProb01,
        float animPhaseRad = 0.0f)
    {
        const float cx = bounds.getCentreX();
        const float cy = bounds.getCentreY();
        const float radius = std::min(bounds.getWidth(), bounds.getHeight()) * 0.46f;
        if (radius < 16.0f)
            return;

        juce::Graphics::ScopedSaveState state(g);

        const float x = std::clamp(normX01, 0.0f, 1.0f);
        const float y = std::clamp(normY01, 0.0f, 1.0f);
        const float energy = std::clamp(vocalEnergy01, 0.0f, 1.0f);

        // 1. 外周アングラージュ真鍮ベゼルリング
        juce::ColourGradient outerRim(
            MidnightLanternPalette::champagneGold().withAlpha(0.85f), cx - radius, cy - radius,
            MidnightLanternPalette::bronzeShadow().withAlpha(0.90f),  cx + radius, cy + radius, false);
        outerRim.addColour(0.5, MidnightLanternPalette::antiqueBrass().withAlpha(0.70f));
        g.setGradientFill(outerRim);
        g.fillEllipse(cx - radius, cy - radius, radius * 2.0f, radius * 2.0f);

        // 2. 深藍天球ガラス内部
        const float innerR = radius - 3.0f;
        juce::ColourGradient domeBg(
            juce::Colour(0xff112450), cx, cy - innerR * 0.35f,
            juce::Colour(0xff030816), cx, cy + innerR * 0.90f, true);
        domeBg.addColour(0.65, MidnightLanternPalette::midnightIndigo());
        g.setGradientFill(domeBg);
        g.fillEllipse(cx - innerR, cy - innerR, innerR * 2.0f, innerR * 2.0f);

        // 3. アストロラーベ偏心天球リング（黄道・赤道・水平線）
        g.setColour(MidnightLanternPalette::champagneGold().withAlpha(0.14f));
        for (float frac : { 0.38f, 0.62f, 0.84f })
        {
            const float rr = innerR * frac;
            g.drawEllipse(cx - rr, cy - rr, rr * 2.0f, rr * 2.0f, 0.85f);
        }
        g.drawEllipse(cx - innerR * 0.55f, cy - innerR * 0.72f, innerR * 1.10f, innerR * 1.10f, 0.75f);

        // 4. 4象限の星座アンカーポイントと現在のターゲット座標 (px, py)
        const float px = cx + (x - 0.5f) * 2.0f * (innerR * 0.72f);
        const float py = cy - (y - 0.5f) * 2.0f * (innerR * 0.72f);

        const std::array<juce::Point<float>, 4> anchors = {{
            { cx - innerR * 0.54f, cy + innerR * 0.54f }, // 左下: Unplugged Pure
            { cx + innerR * 0.54f, cy + innerR * 0.54f }, // 右下: Silk Air
            { cx - innerR * 0.54f, cy - innerR * 0.54f }, // 左上: Warm Tube
            { cx + innerR * 0.54f, cy - innerR * 0.54f }  // 右上: Modern Pop
        }};

        for (size_t i = 0; i < anchors.size(); ++i)
        {
            const auto& pt = anchors[i];
            const float dist = pt.getDistanceFrom({ px, py });
            const float proximity = std::clamp(1.0f - dist / (innerR * 1.3f), 0.08f, 0.85f);

            g.setColour(MidnightLanternPalette::champagneGold().withAlpha(proximity * 0.35f));
            g.drawLine(pt.x, pt.y, px, py, 1.0f);

            g.setColour(MidnightLanternPalette::starlightCyan().withAlpha(0.25f + proximity * 0.50f));
            g.fillEllipse(pt.x - 3.0f, pt.y - 3.0f, 6.0f, 6.0f);
        }

        // 5. たんぽぽ綿毛・花火フィラメント放射（歌声エネルギーに連動して開花）
        const float bloomR = 14.0f + energy * 24.0f;
        constexpr int kNumFilaments = 24;
        for (int i = 0; i < kNumFilaments; ++i)
        {
            const float ang = (static_cast<float>(i) / static_cast<float>(kNumFilaments))
                            * juce::MathConstants<float>::twoPi + animPhaseRad * 0.25f;
            const float len = bloomR * (0.72f + 0.28f * std::sin(static_cast<float>(i) * 2.7f + animPhaseRad));
            const float x2  = px + std::cos(ang) * len;
            const float y2  = py + std::sin(ang) * len;

            juce::Colour rayCol = (i % 3 == 0) ? MidnightLanternPalette::fireworkRose()
                                : (i % 3 == 1) ? MidnightLanternPalette::lanternAmber()
                                               : MidnightLanternPalette::starlightCyan();

            g.setColour(rayCol.withAlpha(0.32f + 0.35f * energy));
            g.drawLine(px, py, x2, y2, 1.15f);
            g.fillEllipse(x2 - 1.6f, y2 - 1.6f, 3.2f, 3.2f);
        }

        // 6. 中心の白熱ランタンコア
        juce::ColourGradient coreGlow(
            MidnightLanternPalette::filamentCore(), px, py,
            MidnightLanternPalette::lanternAmber().withAlpha(0.0f), px + bloomR * 1.1f, py, true);
        coreGlow.addColour(0.35, MidnightLanternPalette::champagneGold().withAlpha(0.75f));
        coreGlow.addColour(0.70, MidnightLanternPalette::fireworkRose().withAlpha(0.28f * (1.0f - sibilantProb01)));
        g.setGradientFill(coreGlow);
        g.fillEllipse(px - bloomR * 1.1f, py - bloomR * 1.1f, bloomR * 2.2f, bloomR * 2.2f);

        g.setColour(MidnightLanternPalette::filamentCore());
        g.fillEllipse(px - 4.2f, py - 4.2f, 8.4f, 8.4f);
    }
};

// =============================================================================
//  6. エディトリアル・面取りオブシディアンカード＆機械式トグル描画器
// =============================================================================
//  AIデザインにありがちな「太いネオン枠」ではなく、Leicaカメラの真鍮トッププレートや
//  高級マスタリングコンソールの彫り込みベゼルを範とした立体パネル＆ピルスイッチ描画。
// =============================================================================
class EditorialBeveledCardRenderer
{
public:
    static void drawCard(
        juce::Graphics& g,
        juce::Rectangle<float> bounds,
        juce::Colour accentTone,
        bool highlighted = false,
        float cornerRadius = 10.0f)
    {
        drawInstrumentCard(g, bounds, {}, accentTone, cornerRadius, highlighted);
    }

    static void drawInstrumentCard(
        juce::Graphics& g,
        juce::Rectangle<float> bounds,
        const juce::String& headerTitle,
        juce::Colour accentTone,
        float cornerRadius = 12.0f,
        bool highlighted = false)
    {
        if (bounds.isEmpty())
            return;

        // 1. ソフトドロップシャドウ
        g.setColour(juce::Colours::black.withAlpha(0.38f));
        g.fillRoundedRectangle(bounds.translated(1.5f, 3.0f), cornerRadius);

        // 2. パネル本体（上端がわずかに明るい深藍ベルベット）
        juce::ColourGradient bodyGrad(
            juce::Colour(0xf40d1b3a), bounds.getX(), bounds.getY(),
            juce::Colour(0xf6060d20), bounds.getX(), bounds.getBottom(), false);
        g.setGradientFill(bodyGrad);
        g.fillRoundedRectangle(bounds, cornerRadius);

        // 3. 外周1pxアングラージュ（左上シャンパンゴールド反射＋右下インディゴ影）
        juce::ColourGradient rimGrad(
            (highlighted ? accentTone : MidnightLanternPalette::champagneGold()).withAlpha(highlighted ? 0.78f : 0.42f),
            bounds.getX(), bounds.getY(),
            MidnightLanternPalette::bronzeShadow().withAlpha(0.25f),
            bounds.getRight(), bounds.getBottom(), false);
        g.setGradientFill(rimGrad);
        g.drawRoundedRectangle(bounds.reduced(0.5f), cornerRadius, highlighted ? 1.5f : 1.0f);

        // 4. 上部エッジの微細スペキュラハイライト線
        g.setColour(MidnightLanternPalette::filamentCore().withAlpha(0.10f));
        g.drawHorizontalLine(static_cast<int>(bounds.getY() + 1.0f),
                             bounds.getX() + cornerRadius,
                             bounds.getRight() - cornerRadius);

        // 5. セクションヘッダーリボン（指定時）
        if (headerTitle.isNotEmpty())
        {
            const auto headerRect = bounds.reduced(12.0f, 8.0f).removeFromTop(20.0f);
            g.setColour(accentTone);
            g.fillRoundedRectangle(headerRect.getX(), headerRect.getY() + 3.0f, 3.5f, 14.0f, 1.5f);

            g.setColour(MidnightLanternPalette::textIvory());
            g.setFont(juce::Font(juce::FontOptions(13.5f, juce::Font::bold)));
            g.drawText(headerTitle, headerRect.withTrimmedLeft(10.0f), juce::Justification::centredLeft, false);
        }
    }

    // 機械式ジュエル・ピルスイッチ（ON時に琥珀／シアン宝石インジケータが点灯する物理スイッチ）
    static void drawTactileJewelSwitch(
        juce::Graphics& g,
        juce::Rectangle<float> bounds,
        bool isOn,
        const juce::String& labelText,
        juce::Colour activeJewelColour = MidnightLanternPalette::lanternAmber(),
        bool isHovered = false)
    {
        const float r = bounds.getHeight() * 0.5f;

        // 彫り込みトラック溝
        g.setColour(juce::Colour(0xff040916));
        g.fillRoundedRectangle(bounds, r);

        juce::Colour rimCol = isOn
            ? activeJewelColour.withAlpha(isHovered ? 0.95f : 0.78f)
            : MidnightLanternPalette::textMutedSlate().withAlpha(isHovered ? 0.55f : 0.32f);
        g.setColour(rimCol);
        g.drawRoundedRectangle(bounds.reduced(0.6f), r, 1.3f);

        // ON時の内部ランタングロー
        if (isOn)
        {
            juce::ColourGradient glow(
                activeJewelColour.withAlpha(0.28f), bounds.getX(), bounds.getY(),
                activeJewelColour.withAlpha(0.08f), bounds.getRight(), bounds.getBottom(), false);
            g.setGradientFill(glow);
            g.fillRoundedRectangle(bounds.reduced(1.2f), r);
        }

        // 左側のジュエル・パイロットランプ
        const float dotR = 4.2f;
        const float dotX = bounds.getX() + 12.0f;
        const float dotY = bounds.getCentreY();
        if (isOn)
        {
            g.setColour(activeJewelColour.withAlpha(0.45f));
            g.fillEllipse(dotX - dotR * 1.7f, dotY - dotR * 1.7f, dotR * 3.4f, dotR * 3.4f);
            g.setColour(MidnightLanternPalette::filamentCore());
            g.fillEllipse(dotX - dotR, dotY - dotR, dotR * 2.0f, dotR * 2.0f);
        }
        else
        {
            g.setColour(MidnightLanternPalette::textMutedSlate().withAlpha(0.35f));
            g.fillEllipse(dotX - dotR * 0.8f, dotY - dotR * 0.8f, dotR * 1.6f, dotR * 1.6f);
        }

        // ラベルテキスト
        g.setColour(isOn ? MidnightLanternPalette::textIvory() : MidnightLanternPalette::textWarmSilver());
        g.setFont(juce::Font(juce::FontOptions(12.5f, juce::Font::bold)));
        g.drawText(labelText, bounds.withTrimmedLeft(22.0f).withTrimmedRight(6.0f),
                   juce::Justification::centred, false);
    }
};

// =============================================================================
//  7. Bob Katz K-14 マスタリング・ラウドネス＆トゥルーピーク・クレストリボン
// =============================================================================
class BobKatzK14CrestRibbonRenderer
{
public:
    static void drawK14MasteringRibbon(
        juce::Graphics& g,
        juce::Rectangle<float> bounds,
        const gzzio::mastering::BobKatzKSystemAnalyzer::KSystemMetrics& metrics)
    {
        if (bounds.getWidth() < 100.0f || bounds.getHeight() < 16.0f)
            return;

        juce::Graphics::ScopedSaveState state(g);
        const float r = bounds.getHeight() * 0.45f;

        g.setColour(juce::Colour(0xff050b1a));
        g.fillRoundedRectangle(bounds, r);
        g.setColour(MidnightLanternPalette::champagneGold().withAlpha(0.35f));
        g.drawRoundedRectangle(bounds.reduced(0.5f), r, 1.0f);

        // -50 LUFS .. 0 LUFS を横幅へマッピング（K-14 基準点 -14 LUFS は 72% 位置）
        const float innerX = bounds.getX() + 4.0f;
        const float innerW = bounds.getWidth() - 8.0f;
        const float lufsNorm = std::clamp((metrics.momentaryLufs + 50.0f) / 50.0f, 0.02f, 1.0f);
        const float tpNorm   = std::clamp((metrics.truePeakDbtp + 50.0f) / 50.0f, 0.02f, 1.0f);
        const float k14Pos   = innerX + ((50.0f - 14.0f) / 50.0f) * innerW;

        // K-14 ラウドネスバー
        const float barW = lufsNorm * innerW;
        juce::ColourGradient barGrad(
            MidnightLanternPalette::starlightCyan(), innerX, bounds.getCentreY(),
            MidnightLanternPalette::fireworkRose(),  innerX + innerW, bounds.getCentreY(), false);
        barGrad.addColour(0.72, MidnightLanternPalette::lanternAmber());
        g.setGradientFill(barGrad);
        g.fillRoundedRectangle(innerX, bounds.getY() + 3.0f, barW, bounds.getHeight() - 6.0f, r * 0.7f);

        // K-14 (-14 LUFS) 基準ターゲット刻印ライン
        g.setColour(MidnightLanternPalette::filamentCore().withAlpha(0.85f));
        g.drawVerticalLine(static_cast<int>(k14Pos), bounds.getY() + 1.0f, bounds.getBottom() - 1.0f);

        // 4x Inter-Sample True Peak マーカー
        const float tpX = innerX + tpNorm * innerW;
        g.setColour(metrics.truePeakDbtp > -0.3f ? MidnightLanternPalette::fireworkRose() : MidnightLanternPalette::champagneGold());
        g.fillRect(tpX - 1.5f, bounds.getY() + 2.0f, 3.0f, bounds.getHeight() - 4.0f);

        // 数値オーバーレイ
        const juce::String info = "K-14: " + juce::String(metrics.shortTermLufs, 1) + " LUFS  |  TP: "
                                + juce::String(metrics.truePeakDbtp, 1) + " dBTP  |  Crest: "
                                + juce::String(metrics.crestFactorDb, 1) + " dB";
        g.setColour(MidnightLanternPalette::textIvory());
        g.setFont(juce::Font(juce::FontOptions(10.5f, juce::Font::bold)));
        g.drawText(info, bounds.reduced(8.0f, 0.0f), juce::Justification::centred, false);
    }
};

} // namespace ui_horology
} // namespace gzzio

namespace vg
{
namespace horology_ui = ::gzzio::ui_horology;
} // namespace vg
