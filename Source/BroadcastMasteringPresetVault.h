#pragma once

/**
 * @file BroadcastMasteringPresetVault.h
 * @brief VocalGzzio v5.5.0 次世代スタジオSE＆プロマスタリング統合プリセット・ヴォールト
 *
 * 刷新された4大スタジオライブSE（♪拍手 / ✦キラ / ●ドラム / ◆ベル）のホール音響プロファイルと、
 * 20種以上の次世代DSPスイート（Fairchild 670 / LA-2A / Lexicon 224 / Dimension D / Abbey Road ADT /
 * Soothe型16バンド動的レゾナンス抑制 / バイノーラル30°外在化 / 完全モノラル互換ワイドナー）の
 * リファレンス・マスタリングプリセット群を一元管理するモジュール。
 */

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace gzzio
{
namespace preset_vault
{

// =============================================================================
// 1. 次世代マスタリング＆ライブSE統合パラメータ構造体
// =============================================================================
struct NextGenMasteringChainSnapshot
{
    const char* presetId;
    const char* displayNameJa;
    const char* descriptionJa;

    // アナログ真空管＆オプト鎖 (Fairchild 670 / LA-2A / Massive Passive)
    float variMuDrive01;
    float optoSmooth01;
    float inductorAir01;

    // 適応スペクトル＆倍音造形 (16-Band Resonance Tamer / Chebyshev / Sub-Octave)
    float resonanceTamer01;
    float autoSpectralTilt01;
    float chebyshevHarmonics01;
    float subOctaveAnchor01;

    // ヴィンテージ空間＆ダブラー (Abbey Road ADT / H3000 MicroShift / Dimension D / Lexicon 224)
    float abbeyRoadAdt01;
    float microPitchWidth01;
    float dimensionDChorus01;
    float lexiconHallMix01;

    // バイノーラル＆モノラル互換フェーズドクター
    float binauralExternalize01;
    float monoSafeWidener01;
    float transientAttack01;
    float transientSustain01;

    // 4大スタジオSE マスタリング・ゲイン＆ホール残響ブレンド
    float seApplauseHallBloom01;
    float seSparkleCrystalMix01;
    float seDrumFinaleImpact01;
    float seBellCarillonRichness01;
};

// =============================================================================
// 2. プロフェッショナル・ファクトリープリセット・カタログ（全12種）
// =============================================================================
class BroadcastMasteringPresetVault
{
public:
    static constexpr size_t kNumSnapshots = 12;

    static constexpr const std::array<NextGenMasteringChainSnapshot, kNumSnapshots>& getCatalog() noexcept
    {
        return kCatalog;
    }

    static const NextGenMasteringChainSnapshot& getSnapshot(size_t index) noexcept
    {
        return kCatalog[index < kNumSnapshots ? index : 0];
    }

    static NextGenMasteringChainSnapshot interpolateSnapshots(const NextGenMasteringChainSnapshot& a,
                                                              const NextGenMasteringChainSnapshot& b,
                                                              float morph01) noexcept
    {
        const float t = std::clamp(morph01, 0.0f, 1.0f);
        const float inv = 1.0f - t;
        NextGenMasteringChainSnapshot out = (t < 0.5f) ? a : b;

        out.variMuDrive01          = a.variMuDrive01 * inv + b.variMuDrive01 * t;
        out.optoSmooth01           = a.optoSmooth01 * inv + b.optoSmooth01 * t;
        out.inductorAir01          = a.inductorAir01 * inv + b.inductorAir01 * t;
        out.resonanceTamer01       = a.resonanceTamer01 * inv + b.resonanceTamer01 * t;
        out.autoSpectralTilt01     = a.autoSpectralTilt01 * inv + b.autoSpectralTilt01 * t;
        out.chebyshevHarmonics01   = a.chebyshevHarmonics01 * inv + b.chebyshevHarmonics01 * t;
        out.subOctaveAnchor01      = a.subOctaveAnchor01 * inv + b.subOctaveAnchor01 * t;
        out.abbeyRoadAdt01         = a.abbeyRoadAdt01 * inv + b.abbeyRoadAdt01 * t;
        out.microPitchWidth01      = a.microPitchWidth01 * inv + b.microPitchWidth01 * t;
        out.dimensionDChorus01     = a.dimensionDChorus01 * inv + b.dimensionDChorus01 * t;
        out.lexiconHallMix01       = a.lexiconHallMix01 * inv + b.lexiconHallMix01 * t;
        out.binauralExternalize01  = a.binauralExternalize01 * inv + b.binauralExternalize01 * t;
        out.monoSafeWidener01      = a.monoSafeWidener01 * inv + b.monoSafeWidener01 * t;
        out.transientAttack01      = a.transientAttack01 * inv + b.transientAttack01 * t;
        out.transientSustain01     = a.transientSustain01 * inv + b.transientSustain01 * t;
        out.seApplauseHallBloom01  = a.seApplauseHallBloom01 * inv + b.seApplauseHallBloom01 * t;
        out.seSparkleCrystalMix01  = a.seSparkleCrystalMix01 * inv + b.seSparkleCrystalMix01 * t;
        out.seDrumFinaleImpact01   = a.seDrumFinaleImpact01 * inv + b.seDrumFinaleImpact01 * t;
        out.seBellCarillonRichness01 = a.seBellCarillonRichness01 * inv + b.seBellCarillonRichness01 * t;
        return out;
    }

private:
    static constexpr std::array<NextGenMasteringChainSnapshot, kNumSnapshots> kCatalog = {{
        {
            "grammy_golden_vocal",
            "グラミー・ゴールデン・ボーカル鎖",
            "LA-2A オプト減衰＋Fairchild 670 真空管グルー＋Massive Passive 16kHz インダクタ・エアの王道ボーカル鎖",
            0.42f, 0.58f, 0.52f,
            0.45f, 0.38f, 0.35f, 0.14f,
            0.22f, 0.28f, 0.32f, 0.24f,
            0.25f, 0.30f, 0.25f, 0.40f,
            0.85f, 0.85f, 0.85f, 0.85f
        },
        {
            "utawaku_grand_concert_se",
            "歌枠グランド・コンサート＆豪華SE",
            "4大ライブSE（劇場拍手・チェレスタ＆ハープ・シンフォニックスネア＆ドラ・カリヨンベル）が最も華やかに響く歌枠特化設定",
            0.36f, 0.52f, 0.48f,
            0.50f, 0.42f, 0.32f, 0.18f,
            0.28f, 0.34f, 0.40f, 0.35f,
            0.35f, 0.38f, 0.30f, 0.48f,
            1.00f, 1.00f, 1.00f, 1.00f
        },
        {
            "abbey_road_double_lead",
            "アビーロードJ37 ダブルリード",
            "Studer J37 ヴァリスピードADTと H3000 ±8.5cent マイクロピッチで、1人の声を厚みのあるダブルボーカルへ拡張",
            0.48f, 0.44f, 0.38f,
            0.38f, 0.32f, 0.42f, 0.16f,
            0.68f, 0.62f, 0.45f, 0.20f,
            0.20f, 0.45f, 0.35f, 0.35f,
            0.80f, 0.82f, 0.88f, 0.80f
        },
        {
            "soothe_silk_deharsh",
            "16バンド動的シルキー整音（刺さりゼロ）",
            "高音シャウト時や安価なマイクで発生する2.5〜6kHzの耳障りな共振ピークだけを動的ノッチで滑らかに鎮める設定",
            0.30f, 0.60f, 0.42f,
            0.82f, 0.68f, 0.25f, 0.22f,
            0.18f, 0.22f, 0.28f, 0.22f,
            0.30f, 0.25f, 0.15f, 0.45f,
            0.85f, 0.88f, 0.82f, 0.86f
        },
        {
            "binaural_headphone_studio",
            "バイノーラル30°スタジオモニター外在化",
            "ヘッドホン長時間配信での頭内定位と聴覚疲労を解消し、前方のスピーカーで聴いているような自然な音場を構築",
            0.34f, 0.48f, 0.40f,
            0.42f, 0.40f, 0.28f, 0.15f,
            0.20f, 0.24f, 0.30f, 0.25f,
            0.85f, 0.32f, 0.22f, 0.32f,
            0.90f, 0.90f, 0.90f, 0.90f
        },
        {
            "lexicon_224_ballad_hall",
            "Lexicon 224 至高のバラード・ホール",
            "80年代名盤のモジュレーテッド・オールパスタンクと Bass Multiply による、濃密で温かく包み込むバラード残響",
            0.40f, 0.64f, 0.55f,
            0.46f, 0.36f, 0.38f, 0.20f,
            0.24f, 0.30f, 0.48f, 0.68f,
            0.32f, 0.40f, 0.18f, 0.62f,
            0.95f, 0.95f, 0.85f, 0.95f
        },
        {
            "upfront_pop_transient_punch",
            "現代J-POP/K-POP 最前列トランジェント",
            "SPL型差動エンベロープと日本語子音オンセット明瞭化により、高密度なボカロ・EDM伴奏の中でも歌詞が最前列に抜ける設定",
            0.52f, 0.50f, 0.60f,
            0.55f, 0.50f, 0.48f, 0.12f,
            0.32f, 0.42f, 0.38f, 0.18f,
            0.22f, 0.52f, 0.68f, 0.38f,
            0.88f, 0.92f, 0.95f, 0.88f
        },
        {
            "deep_radio_baritone_anchor",
            "シネマティック・深みバリトン＆雑談",
            "位相同期サブオクターブ・アンカーと偶数次チェビシェフ倍音で、細くなりがちな声にラジオDJのような胸の響きを付与",
            0.46f, 0.55f, 0.32f,
            0.40f, 0.45f, 0.58f, 0.64f,
            0.12f, 0.15f, 0.20f, 0.12f,
            0.40f, 0.22f, 0.30f, 0.42f,
            0.82f, 0.80f, 0.86f, 0.85f
        },
        {
            "dimension_d_velvet_chorus",
            "Dimension D ベルベット・空間ワイド",
            "Roland SDD-320 のモード4直交BBD変調とオールパス・ヒルベルトワイドナーによる、スマホ再生でも一切痩せない広大な音像",
            0.38f, 0.52f, 0.46f,
            0.44f, 0.38f, 0.34f, 0.16f,
            0.35f, 0.48f, 0.78f, 0.28f,
            0.30f, 0.72f, 0.25f, 0.40f,
            0.88f, 0.94f, 0.86f, 0.92f
        },
        {
            "whisper_asmr_breath_polish",
            "ウィスパー＆ブレス表現・超高感度ポリッシュ",
            "レベル不変チェビシェフ倍音とAR(2)リップノイズ修復により、ささやき歌唱の吐息の艶だけを残してクリック音を除去",
            0.28f, 0.68f, 0.64f,
            0.62f, 0.52f, 0.46f, 0.24f,
            0.22f, 0.28f, 0.36f, 0.32f,
            0.55f, 0.35f, 0.20f, 0.65f,
            0.78f, 0.92f, 0.75f, 0.88f
        },
        {
            "rock_arena_rmx16_power",
            "ロックアリーナ RMX16 NonLin パワー",
            "AMS RMX16 NonLin2 ゲートアンビエンスと Fairchild 670 ラテラル/バーティカル圧縮で、圧倒的な音圧とキレを両立",
            0.68f, 0.42f, 0.52f,
            0.58f, 0.44f, 0.62f, 0.28f,
            0.42f, 0.38f, 0.30f, 0.34f,
            0.25f, 0.48f, 0.62f, 0.30f,
            0.96f, 0.86f, 1.00f, 0.88f
        },
        {
            "mastering_linear_reference",
            "マスタリング・リニア位相リファレンス",
            "色付けを最小限に抑え、Linkwitz-Riley 4次クロスオーバーと EBU R128 True-Peak 保護のみで原音を磨き上げる基準設定",
            0.20f, 0.32f, 0.28f,
            0.35f, 0.30f, 0.18f, 0.08f,
            0.10f, 0.12f, 0.15f, 0.14f,
            0.18f, 0.20f, 0.15f, 0.20f,
            0.85f, 0.85f, 0.85f, 0.85f
        }
    }};
};

} // namespace preset_vault
} // namespace gzzio

namespace vg
{
namespace preset_vault = ::gzzio::preset_vault;
} // namespace vg
