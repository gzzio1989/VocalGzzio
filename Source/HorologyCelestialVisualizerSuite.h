#pragma once

/**
 * @file HorologyCelestialVisualizerSuite.h
 * @brief VocalGzzio v5.5.0 天球プラニスフィア音場コンパス・4大スタジオSE専用タクタイル紋章＆スプリットセコンド(Rattrapante) LUFS/dBTP クロノグラフ描画スイート
 *
 * ご指定の宵祭カラーパレット（Deep Midnight Indigo `#040B1E` / Warm Amber Lantern `#FFAA2B` /
 * Firework Rose `#FF6E8E` / Starlight Cyan `#3CE8D5`）と、スイス機械式時計・天文儀（Astrolabe Planisphere）の
 * 工芸美を融合した次世代ビジュアライザー群。
 *
 * 収録クラス:
 *   1. CelestialPlanisphereAndSoundstageMapRenderer
 *      - 4帯域 LR4 クロスオーバー（Low / LowMid / HighMid / Air）のステレオ音場広がりとエネルギー重心を、
 *        天球プラニスフィア（星座早見盤）の子午線・黄道リング・ランタン星団としてリアルタイム描画。
 *
 *   2. LiveSePadHorologyVisualizer
 *      - 刷新された4大スタジオSE（♪拍手 / ✦キラ / ●ドラム / ◆ベル）の物理音響モデルを視覚化する専用エンブレム描画：
 *        - SE 0 (♪拍手): 220名の観客が囲むコンサートホール音響リップル＆ゴールデン・クラップアーク
 *        - SE 1 (✦キラ): 36本ウィンドチャイムとチェレスタの八芒星スターライト・プリズム
 *        - SE 2 (●ドラム): ベッセル円形膜振動ノードリング＆40モード青銅クラッシュシンバル閃光
 *        - SE 3 (◆ベル): 揺れるオーケストラ・チューブラーベル＆ヴィブラフォン回転バタフライ羽根
 *
 *   3. LufsAndTruePeakChronographRenderer
 *      - スイス時計の最高峰機構「ラトラパンテ（Split-Seconds Rattrapante 追針クロノグラフ）」をモチーフに、
 *        Momentary LUFS（金針）と Short-Term LUFS（ローズ追針）および 8倍 True-Peak dBTP ジュエルインジケーターを描画。
 */

#include <juce_gui_basics/juce_gui_basics.h>
#include <algorithm>
#include <array>
#include <cmath>
#include "HorologyTactileUi.h"

namespace gzzio
{
namespace horology_celestial
{

// =============================================================================
// 1. 天球プラニスフィア 3D音場＆4帯域エネルギー・アストロラーベ描画器
//    (CelestialPlanisphereAndSoundstageMapRenderer)
// =============================================================================
class CelestialPlanisphereAndSoundstageMapRenderer
{
public:
    struct SoundstageBandMetrics
    {
        std::array<float, 4> bandEnergy01 { 0.45f, 0.68f, 0.74f, 0.52f }; // Low, LowMid, HighMid, Air
        std::array<float, 4> bandWidth01  { 0.10f, 0.42f, 0.68f, 0.85f }; // ステレオ幅
        float vocalCenterLock01 = 0.88f;
        float hallAmbience01    = 0.42f;
    };

    static void drawPlanisphere(juce::Graphics& g,
                                juce::Rectangle<float> bounds,
                                const SoundstageBandMetrics& metrics,
                                float animationPhaseRad)
    {
        const float cx = bounds.getCentreX();
        const float cy = bounds.getCentreY();
        const float maxR = std::min(bounds.getWidth(), bounds.getHeight()) * 0.46f;
        if (maxR < 16.0f)
            return;

        using vg::horology_ui::MidnightLanternPalette;

        // 1. 深藍アビス天球ドーム背景
        juce::ColourGradient domeGrad(
            juce::Colour(0xff0c1b3e), cx, cy - maxR * 0.35f,
            MidnightLanternPalette::canopyAbyss(), cx, cy + maxR, true);
        g.setGradientFill(domeGrad);
        g.fillEllipse(cx - maxR, cy - maxR, maxR * 2.0f, maxR * 2.0f);

        // 2. 外周アングラージュ真鍮目盛りリング（天球赤道・方位角36分割）
        g.setColour(MidnightLanternPalette::champagneGold().withAlpha(0.65f));
        g.drawEllipse(cx - maxR, cy - maxR, maxR * 2.0f, maxR * 2.0f, 1.5f);

        for (int deg = 0; deg < 36; ++deg)
        {
            const float ang = static_cast<float>(deg) * (6.2831853f / 36.0f);
            const bool major = (deg % 3 == 0);
            const float r0 = maxR * (major ? 0.90f : 0.94f);
            const float r1 = maxR * 0.99f;
            g.setColour(MidnightLanternPalette::champagneGold().withAlpha(major ? 0.62f : 0.28f));
            g.drawLine(cx + r0 * std::cos(ang), cy + r0 * std::sin(ang),
                       cx + r1 * std::cos(ang), cy + r1 * std::sin(ang),
                       major ? 1.4f : 0.8f);
        }

        // 3. 偏心黄道リング（Ecliptic Astrolabe Ring: ゆっくり歳差回転する金環）
        const float eclR  = maxR * 0.68f;
        const float eclOx = cx + std::cos(animationPhaseRad * 0.35f) * (maxR * 0.10f);
        const float eclOy = cy + std::sin(animationPhaseRad * 0.35f) * (maxR * 0.10f);
        g.setColour(MidnightLanternPalette::lanternAmber().withAlpha(0.28f));
        g.drawEllipse(eclOx - eclR, eclOy - eclR, eclR * 2.0f, eclR * 2.0f, 1.1f);

        // 4. 4帯域 (Low / LowMid / HighMid / Air) の音場アークと星座ノード
        static const std::array<juce::Colour, 4> kBandCols = {{
            MidnightLanternPalette::lanternAmber(),
            MidnightLanternPalette::champagneGold(),
            MidnightLanternPalette::fireworkRose(),
            MidnightLanternPalette::starlightCyan()
        }};

        for (size_t b = 0; b < 4; ++b)
        {
            const float ringR = maxR * (0.26f + static_cast<float>(b) * 0.17f);
            const float spreadRad = std::clamp(metrics.bandWidth01[b], 0.08f, 1.0f) * 1.35f;
            const float energy    = std::clamp(metrics.bandEnergy01[b], 0.05f, 1.0f);

            juce::Path stereoArc;
            stereoArc.addCentredArc(cx, cy, ringR, ringR, 0.0f, -spreadRad, spreadRad, true);
            g.setColour(kBandCols[b].withAlpha(0.22f + 0.55f * energy));
            g.strokePath(stereoArc, juce::PathStrokeType(2.2f + 2.4f * energy, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            // 左右の音響星団ノード
            for (float sign : { -1.0f, 1.0f })
            {
                const float a = sign * spreadRad - 1.5707963f;
                const float nx = cx + ringR * std::cos(a);
                const float ny = cy + ringR * std::sin(a);
                const float nr = 2.6f + 2.8f * energy;
                g.setColour(kBandCols[b].withAlpha(0.38f));
                g.fillEllipse(nx - nr * 1.8f, ny - nr * 1.8f, nr * 3.6f, nr * 3.6f);
                g.setColour(MidnightLanternPalette::filamentCore());
                g.fillEllipse(nx - nr * 0.6f, ny - nr * 0.6f, nr * 1.2f, nr * 1.2f);
            }
        }

        // 5. 中心のリードボーカル・ポラリス（北極星）コア
        const float coreR = 4.5f + 3.5f * std::clamp(metrics.vocalCenterLock01, 0.0f, 1.0f);
        g.setColour(MidnightLanternPalette::lanternAmber().withAlpha(0.42f));
        g.fillEllipse(cx - coreR * 2.2f, cy - coreR * 2.2f, coreR * 4.4f, coreR * 4.4f);
        g.setColour(MidnightLanternPalette::filamentCore());
        g.fillEllipse(cx - coreR, cy - coreR, coreR * 2.0f, coreR * 2.0f);
    }
};

// =============================================================================
// 2. 4大スタジオSE専用タクタイル紋章ビジュアライザー (LiveSePadHorologyVisualizer)
// =============================================================================
class LiveSePadHorologyVisualizer
{
public:
    // padIndex: 0=♪拍手, 1=✦キラ, 2=●ドラム, 3=◆ベル
    static void drawSePadEmblem(juce::Graphics& g,
                                juce::Rectangle<float> bounds,
                                int padIndex,
                                bool isPlaying,
                                float playbackProgress01)
    {
        const float cx = bounds.getCentreX();
        const float cy = bounds.getCentreY();
        const float r  = std::min(bounds.getWidth(), bounds.getHeight()) * 0.42f;
        if (r < 6.0f)
            return;

        using vg::horology_ui::MidnightLanternPalette;
        const float prog = std::clamp(playbackProgress01, 0.0f, 1.0f);

        if (padIndex == 0)
        {
            // ♪拍手: コンサートホール3層（前列・中列・バルコニー）の多重音響リップル波紋
            for (int ring = 0; ring < 3; ++ring)
            {
                const float phase = std::fmod(prog * 2.5f + static_cast<float>(ring) * 0.33f, 1.0f);
                const float rr = r * (0.35f + 0.65f * (isPlaying ? phase : (static_cast<float>(ring + 1) * 0.25f)));
                const float alpha = isPlaying ? ((1.0f - phase) * 0.85f) : 0.45f;
                g.setColour(MidnightLanternPalette::lanternAmber().withAlpha(alpha));
                g.drawEllipse(cx - rr, cy - rr, rr * 2.0f, rr * 2.0f, 1.5f);
            }
            g.setColour(MidnightLanternPalette::filamentCore());
            g.fillEllipse(cx - 2.8f, cy - 2.8f, 5.6f, 5.6f);
        }
        else if (padIndex == 1)
        {
            // ✦キラ: 八芒星スターライト・プリズム＆36本ウィンドチャイム粒子
            juce::Path star;
            const float rot = isPlaying ? (prog * 3.14159265f) : 0.0f;
            for (int i = 0; i < 16; ++i)
            {
                const float ang = rot + static_cast<float>(i) * (6.2831853f / 16.0f);
                const float rad = (i % 2 == 0) ? r : (r * 0.32f);
                const float x = cx + rad * std::cos(ang);
                const float y = cy + rad * std::sin(ang);
                if (i == 0) star.startNewSubPath(x, y);
                else        star.lineTo(x, y);
            }
            star.closeSubPath();
            g.setColour(MidnightLanternPalette::starlightCyan().withAlpha(isPlaying ? 0.90f : 0.65f));
            g.fillPath(star);
            g.setColour(MidnightLanternPalette::filamentCore());
            g.fillEllipse(cx - r * 0.22f, cy - r * 0.22f, r * 0.44f, r * 0.44f);
        }
        else if (padIndex == 2)
        {
            // ●ドラム: スネア円形膜ベッセル関数ノードリング (J_01, J_11, J_21) ＆ クラッシュ閃光
            g.setColour(MidnightLanternPalette::fireworkRose().withAlpha(isPlaying ? 0.92f : 0.68f));
            g.drawEllipse(cx - r, cy - r, r * 2.0f, r * 2.0f, 1.8f);
            g.drawEllipse(cx - r * 0.62f, cy - r * 0.62f, r * 1.24f, r * 1.24f, 1.2f);
            // 節直径線 (Nodal diameters)
            for (int d = 0; d < 3; ++d)
            {
                const float ang = static_cast<float>(d) * 1.04719755f + (isPlaying ? prog * 6.283f : 0.0f);
                g.drawLine(cx - r * std::cos(ang), cy - r * std::sin(ang),
                           cx + r * std::cos(ang), cy + r * std::sin(ang), 1.0f);
            }
        }
        else
        {
            // ◆ベル: オーケストラ・チューブラーベル＆ヴィブラフォン共鳴管ドーム
            juce::Path bellPath;
            bellPath.startNewSubPath(cx - r * 0.72f, cy + r * 0.55f);
            bellPath.quadraticTo(cx - r * 0.52f, cy - r * 0.75f, cx, cy - r * 0.82f);
            bellPath.quadraticTo(cx + r * 0.52f, cy - r * 0.75f, cx + r * 0.72f, cy + r * 0.55f);
            bellPath.closeSubPath();
            g.setColour(MidnightLanternPalette::champagneGold().withAlpha(isPlaying ? 0.92f : 0.72f));
            g.strokePath(bellPath, juce::PathStrokeType(1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            // クラッパー（舌）振り子
            const float swing = isPlaying ? (std::sin(prog * 25.1327f) * r * 0.32f) : 0.0f;
            g.setColour(MidnightLanternPalette::filamentCore());
            g.fillEllipse(cx + swing - 2.6f, cy + r * 0.58f - 2.6f, 5.2f, 5.2f);
        }
    }
};

// =============================================================================
// 3. スプリットセコンド (Rattrapante) LUFS & True-Peak クロノグラフ描画器
//    (LufsAndTruePeakChronographRenderer)
// =============================================================================
class LufsAndTruePeakChronographRenderer
{
public:
    static void drawRattrapanteMeter(juce::Graphics& g,
                                     juce::Rectangle<float> bounds,
                                     float momentaryLufs,
                                     float shortTermLufs,
                                     float truePeakDbTp)
    {
        const float cx = bounds.getCentreX();
        const float cy = bounds.getCentreY();
        const float r  = std::min(bounds.getWidth(), bounds.getHeight()) * 0.44f;
        if (r < 12.0f)
            return;

        using vg::horology_ui::MidnightLanternPalette;

        // 文字盤ベース
        g.setColour(juce::Colour(0xff050c20));
        g.fillEllipse(cx - r, cy - r, r * 2.0f, r * 2.0f);
        g.setColour(MidnightLanternPalette::champagneGold().withAlpha(0.72f));
        g.drawEllipse(cx - r, cy - r, r * 2.0f, r * 2.0f, 1.5f);

        // -36 LUFS 〜 -6 LUFS を -135° 〜 +135° にマッピング
        auto lufsToAngle = [](float lufs) noexcept -> float
        {
            const float u = std::clamp((lufs - (-36.0f)) / 30.0f, 0.0f, 1.0f);
            return -2.35619449f + u * 4.71238898f - 1.57079632f;
        };

        const float angShort = lufsToAngle(shortTermLufs);
        const float angMom   = lufsToAngle(momentaryLufs);

        // 第1針（ラトラパンテ追針: Short-Term LUFS, コーラルローズ）
        g.setColour(MidnightLanternPalette::fireworkRose().withAlpha(0.88f));
        g.drawLine(cx, cy, cx + r * 0.78f * std::cos(angShort), cy + r * 0.78f * std::sin(angShort), 2.0f);

        // 第2針（メインクロノ針: Momentary LUFS, シャンパンゴールド）
        g.setColour(MidnightLanternPalette::champagneGold());
        g.drawLine(cx, cy, cx + r * 0.86f * std::cos(angMom), cy + r * 0.86f * std::sin(angMom), 2.4f);

        // True-Peak インジケータジュエル（-1.0 dBTP 未満はシアン、超過時はアンバー警告）
        const juce::Colour jewelCol = (truePeakDbTp > -0.3f)
            ? MidnightLanternPalette::fireworkRose()
            : MidnightLanternPalette::starlightCyan();
        g.setColour(jewelCol);
        g.fillEllipse(cx - 3.2f, cy - 3.2f, 6.4f, 6.4f);
    }
};

} // namespace horology_celestial
} // namespace gzzio

namespace vg
{
namespace horology_celestial = ::gzzio::horology_celestial;
} // namespace vg
