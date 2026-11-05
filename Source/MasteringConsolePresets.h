#pragma once

/**
 * @file MasteringConsolePresets.h
 * @brief VocalGzzio 世界最高峰マスタリング＆ボーカルプロデュース 24大シグネチャー・コンソールマトリクス＆自動推薦エンジン
 *
 * 【研究開発の背景と独自技術】
 * 単なる「EQとコンプの数値プリセット」ではなく、世界の音楽史と現代チャートを彩る
 * 24種類の象徴的なボーカル・シグナルチェイン（マイクカプセル物理特性、トランスフォーマー飽和、
 * Pultec インダクタEQ、並列バスコンプレッション、空間初期反射、配信プラットフォーム天井設定）を
 * 構造化データとして完全網羅し、さらに任意の4コンソール間を連続モーフィングできる
 * 双線形コンソール補間エンジン (Bilinear Console Morphing Matrix) を提供します。
 */

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include "MasteringProEngine.h"
#include "StudioAcousticRoomEngine.h"
#include "MasteringLoudnessAndDelivery.h"

namespace vg::console_presets
{

//==============================================================================
// 1. シグネチャー・ボーカルコンソール完全パラメータ構造体
//==============================================================================
struct SignatureVocalConsoleSpec
{
    int id = 0;
    const char* codeName = "SERBAN_POP_AUTHORITY";
    const char* titleJa = "01. モダンポップ・チャート王道 (Serban Parallel Bus)";
    const char* subtitleJa = "C800G + 1073 + 並列FET/Opto + Oxford Inflator による圧倒的抜けと透明感";

    // マイクカプセル＆プリアンプ段
    vg::studio_acoustics::VintageMicrophoneCapsuleSimulator::CapsuleModel capsuleModel =
        vg::studio_acoustics::VintageMicrophoneCapsuleSimulator::CapsuleModel::ModernC800G;
    float capsuleCharacter01 = 0.65f;
    float marinairTransformerDrive01 = 0.28f;
    float dynamicProximityGuard01 = 0.55f;

    // 音素・ディエッシング・部屋鳴り除去段
    float zeroLagDeReverb01 = 0.35f;
    float weissRelativeDeEss01 = 0.58f;
    float nasalHonkSuppress01 = 0.45f;
    float harshPharynxTame01 = 0.52f;

    // Pultec EQP-1A / MEQ-5 & シンガーズフォルマント段
    float pultecLowTrick01 = 0.42f;
    float pultecMeqSweetMid01 = 0.38f;
    float pultecInductorAir01 = 0.64f;
    float sundbergSingersFormant01 = 0.55f;
    float passaggioBridge01 = 0.50f;

    // 並列ダイナミクス・テープ・倍音段
    float serbanParallelBus01 = 0.68f;
    float oxfordInflatorDensity01 = 0.54f;
    float sslBusGlue01 = 0.48f;
    float studer30IpsTape01 = 0.35f;
    float aphexPhaseExciter01 = 0.42f;
    float missingFundamentalBass01 = 0.25f;

    // 空間ポケット・純正律・配信ラウドネス段
    float h3000MicroPitchWidth01 = 0.44f;
    float bricastiEarlyPocket01 = 0.32f;
    float hadamardVelvetReverb01 = 0.22f;
    float justIntonationPurity01 = 0.65f;
    float targetLufs = -14.0f;
    float truePeakCeilingDbTp = -1.0f;
};

//==============================================================================
// 2. 世界最高峰 24大ボーカル＆マスタリング・コンソールカタログ
//==============================================================================
class WorldClassConsoleCatalog
{
public:
    static constexpr int kNumConsoles = 24;

    static const std::array<SignatureVocalConsoleSpec, kNumConsoles>& getCatalog() noexcept
    {
        using Cap = vg::studio_acoustics::VintageMicrophoneCapsuleSimulator::CapsuleModel;
        static const std::array<SignatureVocalConsoleSpec, kNumConsoles> kSpecs = {{
            // 01: Serban Ghenea Modern Pop Authority
            {
                0, "SERBAN_POP_AUTHORITY",
                "01. モダンポップ・チャート王道 (Serban Parallel Bus)",
                "C800G + 1073 + 並列FET/Opto + Oxford Inflator による圧倒的抜けと透明感",
                Cap::ModernC800G, 0.68f, 0.28f, 0.55f,
                0.35f, 0.62f, 0.45f, 0.52f,
                0.42f, 0.38f, 0.66f, 0.58f, 0.50f,
                0.72f, 0.58f, 0.52f, 0.32f, 0.45f, 0.25f,
                0.46f, 0.32f, 0.20f, 0.65f, -14.0f, -1.0f
            },
            // 02: Manny Marroquin 4-Band Soul & R&B
            {
                1, "MANNY_SOUL_RNB",
                "02. エモーショナル R&B・ソウル (Manny 4-Band Exciter)",
                "U47真空管の濃厚な中低域とドルビーA方式シルキーエアの芳醇なブレンド",
                Cap::GermanTubeU47, 0.74f, 0.42f, 0.50f,
                0.30f, 0.54f, 0.40f, 0.48f,
                0.58f, 0.52f, 0.56f, 0.48f, 0.55f,
                0.60f, 0.50f, 0.55f, 0.52f, 0.48f, 0.38f,
                0.52f, 0.40f, 0.32f, 0.70f, -14.0f, -1.0f
            },
            // 03: Jaycen Joshua Trap & Hip-Hop Pocket
            {
                2, "JAYCEN_URBAN_POCKET",
                "03. アーバン・トラップ＆ヒップホップ (Jaycen Soothe Pocket)",
                "808ベースと干渉しないタイトな低域位相と耳に刺さらない高密度プレゼンス",
                Cap::ModernC800G, 0.72f, 0.36f, 0.68f,
                0.48f, 0.70f, 0.58f, 0.72f,
                0.36f, 0.44f, 0.68f, 0.62f, 0.42f,
                0.75f, 0.64f, 0.58f, 0.40f, 0.52f, 0.30f,
                0.38f, 0.28f, 0.16f, 0.50f, -12.0f, -0.8f
            },
            // 04: Andrew Scheps Parallel Rear-Bus Rock
            {
                3, "SCHEPS_REARBUS_ROCK",
                "04. オルタナティブ・ロック存在感 (Scheps Parallel Rear-Bus)",
                "ラウドなギターの壁を突き抜ける1176リアバス並列圧縮とNeveトランスの咆哮",
                Cap::BroadcastDynamic7B, 0.70f, 0.58f, 0.60f,
                0.40f, 0.52f, 0.48f, 0.56f,
                0.50f, 0.62f, 0.45f, 0.68f, 0.58f,
                0.82f, 0.66f, 0.62f, 0.58f, 0.38f, 0.42f,
                0.34f, 0.36f, 0.18f, 0.45f, -12.0f, -0.8f
            },
            // 05: Bob Katz K-14 Audiophile Realism
            {
                4, "KATZ_K14_AUDIOPHILE",
                "05. オーディオファイル・アコースティック (Bob Katz K-14)",
                "クレストファクター14dBの自然な呼吸感と位相歪みゼロの純粋TPTマスタリング",
                Cap::TransparentReference, 0.20f, 0.14f, 0.42f,
                0.22f, 0.40f, 0.32f, 0.35f,
                0.28f, 0.30f, 0.42f, 0.35f, 0.35f,
                0.32f, 0.24f, 0.30f, 0.20f, 0.22f, 0.15f,
                0.24f, 0.38f, 0.35f, 0.85f, -16.0f, -1.0f
            },
            // 06: J-Pop / Anisong High-Density Chorus Cut-Through
            {
                5, "JPOP_ANISONG_PRISM",
                "06. J-POP・アニソン高密度サビ抜け (Anisong Prism Cut)",
                "音数の多い高速バンド＋シンセ伴奏の真ん中に日本語の母音と子音をくっきり定位",
                Cap::ModernC800G, 0.76f, 0.32f, 0.62f,
                0.45f, 0.66f, 0.55f, 0.60f,
                0.45f, 0.54f, 0.74f, 0.70f, 0.68f,
                0.76f, 0.65f, 0.56f, 0.36f, 0.58f, 0.28f,
                0.55f, 0.34f, 0.26f, 0.72f, -12.0f, -0.8f
            },
            // 07: VTuber / Utaite Live Stream Zero-Latency Guard
            {
                6, "UTAITE_STREAM_GUARD",
                "07. 歌ってみた・配信特化 (Utaite Live Stream Guard)",
                "自宅部屋の反響消し・打鍵クリック遮断・骨伝導モニター補正を備えた歌枠完成形",
                Cap::ModernC800G, 0.60f, 0.26f, 0.72f,
                0.75f, 0.64f, 0.52f, 0.58f,
                0.40f, 0.46f, 0.62f, 0.60f, 0.62f,
                0.68f, 0.55f, 0.50f, 0.28f, 0.46f, 0.32f,
                0.42f, 0.30f, 0.24f, 0.68f, -14.0f, -1.0f
            },
            // 08: Whisper & Intimate Close-Mic Velvet
            {
                7, "WHISPER_VELVET_AIR",
                "08. ウィスパー・吐息ベルベット (Intimate Upward Velvet)",
                "ISO 226 等ラウドネス補償と上向きコンプでささやき声の繊細な息遣いを目前に再現",
                Cap::GermanTubeU47, 0.66f, 0.24f, 0.78f,
                0.52f, 0.68f, 0.42f, 0.46f,
                0.52f, 0.36f, 0.72f, 0.42f, 0.48f,
                0.64f, 0.60f, 0.42f, 0.38f, 0.54f, 0.36f,
                0.48f, 0.42f, 0.28f, 0.75f, -14.0f, -1.0f
            },
            // 09: Abbey Road Studio Two Steed Chamber
            {
                8, "ABBEY_STEED_CHAMBER",
                "09. アビーロード・ブリティッシュ・シルク (Steed Chamber & TG)",
                "30msテーププリディレイ＋15IPSヘッドバンプ＋温かなチェンバー初期反射",
                Cap::GermanTubeU47, 0.78f, 0.48f, 0.46f,
                0.25f, 0.48f, 0.38f, 0.44f,
                0.56f, 0.50f, 0.48f, 0.52f, 0.45f,
                0.58f, 0.46f, 0.52f, 0.64f, 0.34f, 0.30f,
                0.36f, 0.56f, 0.42f, 0.60f, -14.0f, -1.0f
            },
            // 10: Jacob Collier / Pentatonix Pure Just-Intonation Harmony
            {
                9, "PURE_JUST_HARMONY",
                "10. 純正律アカペラ・コーラス (Pure 5:4 / 3:2 Lock)",
                "和音ホールド時に純粋長3度(-13.7c)と完全5度(+1.96c)へロックしうなりを完全解消",
                Cap::TransparentReference, 0.45f, 0.20f, 0.52f,
                0.42f, 0.56f, 0.46f, 0.48f,
                0.38f, 0.44f, 0.58f, 0.54f, 0.56f,
                0.54f, 0.48f, 0.50f, 0.24f, 0.38f, 0.22f,
                0.58f, 0.44f, 0.36f, 1.00f, -14.0f, -1.0f
            },
            // 11: Sundberg Epilarynx Stage Belting
            {
                10, "SUNDBERG_STAGE_BELT",
                "11. ハイトーン・ベルティング (Sundberg 3kHz Epilarynx)",
                "絶唱時にも潰れず2.8kHz〜3.3kHzのシンガーズフォルマントがホール全体に響き渡る設計",
                Cap::GermanTubeU47, 0.64f, 0.38f, 0.58f,
                0.32f, 0.58f, 0.54f, 0.62f,
                0.46f, 0.48f, 0.56f, 0.82f, 0.76f,
                0.62f, 0.52f, 0.48f, 0.42f, 0.44f, 0.34f,
                0.40f, 0.46f, 0.38f, 0.72f, -14.0f, -1.0f
            },
            // 12: City Pop & 80s Sunset Analog Tape
            {
                11, "CITY_POP_30IPS_GOLD",
                "12. シティポップ・アナログテープ (30IPS Studer & H3000)",
                "68Hzヘッドバンプの弾むローエンドとEventide H3000マイクロピッチの煌めくステレオ感",
                Cap::GermanTubeU47, 0.72f, 0.44f, 0.48f,
                0.28f, 0.50f, 0.40f, 0.46f,
                0.54f, 0.46f, 0.60f, 0.50f, 0.48f,
                0.60f, 0.52f, 0.58f, 0.72f, 0.48f, 0.28f,
                0.66f, 0.45f, 0.34f, 0.62f, -14.0f, -1.0f
            },
            // 13: Emotional Piano & Strings Ballad
            {
                12, "CINEMATIC_BALLAD_HALO",
                "13. シネマティック・バラード (8x8 Hadamard Velvet Halo)",
                "歌唱中はリバーブが自動で引き下がり、語尾の余韻で8x8アダマール空間が美しく開花",
                Cap::GermanTubeU47, 0.70f, 0.30f, 0.56f,
                0.36f, 0.56f, 0.44f, 0.50f,
                0.48f, 0.42f, 0.64f, 0.56f, 0.60f,
                0.56f, 0.48f, 0.46f, 0.36f, 0.42f, 0.26f,
                0.48f, 0.50f, 0.58f, 0.78f, -14.0f, -1.0f
            },
            // 14: Vocaloid / Synth-Pop Humanizer
            {
                13, "SYNTHPOP_HUMANIZER",
                "14. ボカロ・エレクトロポップ親和 (Kinematic Humanizer)",
                "ADAA真空管の偶数次倍音と輪状甲状筋ビブラートで無機質な高域に人間的な体温を宿す",
                Cap::ModernC800G, 0.75f, 0.46f, 0.52f,
                0.38f, 0.64f, 0.50f, 0.58f,
                0.44f, 0.52f, 0.70f, 0.64f, 0.65f,
                0.70f, 0.62f, 0.54f, 0.48f, 0.56f, 0.32f,
                0.56f, 0.36f, 0.28f, 0.68f, -12.0f, -0.8f
            },
            // 15: Baritone & Deep Voice Smartphone Translator
            {
                14, "BARITONE_VIRTUAL_BASS",
                "15. 低音イケボ・スマホ再生最適化 (Chebyshev Virtual Pitch)",
                "チェビシェフT2/T3欠落基音倍音により小型スマホスピーカーでも胸に響く重低音ボイス",
                Cap::BroadcastDynamic7B, 0.68f, 0.40f, 0.70f,
                0.46f, 0.54f, 0.48f, 0.46f,
                0.62f, 0.44f, 0.52f, 0.56f, 0.42f,
                0.66f, 0.58f, 0.52f, 0.44f, 0.46f, 0.76f,
                0.32f, 0.28f, 0.18f, 0.55f, -14.0f, -1.0f
            },
            // 16: High-Tone Passaggio Seamless Bridge
            {
                15, "PASSAGGIO_MIX_BRIDGE",
                "16. ミックスボイス・パッサージョ無縫製ブリッジ",
                "裏返りやすいC4〜F5の声区転換点を検知し、地声の厚みと頭声の輝きを自動架橋",
                Cap::ModernC800G, 0.66f, 0.34f, 0.58f,
                0.38f, 0.60f, 0.58f, 0.62f,
                0.42f, 0.56f, 0.64f, 0.68f, 0.88f,
                0.66f, 0.56f, 0.50f, 0.36f, 0.48f, 0.34f,
                0.46f, 0.36f, 0.26f, 0.70f, -14.0f, -1.0f
            },
            // 17: Bedroom Booth De-Reverb & Condenser Upgrade
            {
                16, "BEDROOM_TO_ABBEY_BOOTH",
                "17. 宅録部屋鳴り消去＆高級コンデンサー化",
                "6帯域DRRデリバーブで反響を剥ぎ取り、12kHzコンデンサーシルクを新たに再合成",
                Cap::ModernC800G, 0.78f, 0.32f, 0.74f,
                0.86f, 0.66f, 0.62f, 0.64f,
                0.46f, 0.48f, 0.68f, 0.58f, 0.52f,
                0.68f, 0.58f, 0.52f, 0.34f, 0.55f, 0.30f,
                0.40f, 0.32f, 0.20f, 0.60f, -14.0f, -1.0f
            },
            // 18: Acoustic Guitar & Singer-Songwriter Warmth
            {
                17, "SINGER_SONGWRITER_WOOD",
                "18. 弾き語り・アコースティック木質感 (Warm Inductor & U47)",
                "アコギの倍音とぶつからない中域ポケットとPultecインダクタの優しい空気感",
                Cap::GermanTubeU47, 0.64f, 0.36f, 0.54f,
                0.34f, 0.52f, 0.44f, 0.48f,
                0.48f, 0.46f, 0.54f, 0.48f, 0.46f,
                0.54f, 0.44f, 0.46f, 0.48f, 0.36f, 0.22f,
                0.38f, 0.44f, 0.32f, 0.74f, -14.0f, -1.0f
            },
            // 19: Fast Rap & Syllabic Articulation Snap
            {
                18, "SYLLABIC_RAP_SNAP",
                "19. 高速ラップ・早口歌詞スナップ (STI Articulation Lifter)",
                "子音の立ち上がり35msだけを瞬間強調し、マシンガンフロウの一言一句を鮮明に伝達",
                Cap::BroadcastDynamic7B, 0.72f, 0.42f, 0.66f,
                0.55f, 0.68f, 0.56f, 0.60f,
                0.44f, 0.52f, 0.58f, 0.64f, 0.44f,
                0.76f, 0.62f, 0.58f, 0.42f, 0.52f, 0.36f,
                0.28f, 0.22f, 0.12f, 0.45f, -12.0f, -0.8f
            },
            // 20: Gospel & Choir Wide Symphonic Stage
            {
                19, "SYMPHONIC_CHOIR_STAGE",
                "20. ゴスペル・シンフォニック大聖堂 (Wide M/S & Golden ER)",
                "モノラル互換性を100%保ったまま左右180度へ広がる壮麗なコーラスステージ",
                Cap::GermanTubeU47, 0.68f, 0.38f, 0.48f,
                0.26f, 0.50f, 0.42f, 0.46f,
                0.52f, 0.48f, 0.62f, 0.62f, 0.58f,
                0.60f, 0.54f, 0.56f, 0.46f, 0.44f, 0.30f,
                0.74f, 0.58f, 0.52f, 0.88f, -14.0f, -1.0f
            },
            // 21: Podcast / Radio FM Broadcast Authority
            {
                20, "BROADCAST_FM_AUTHORITY",
                "21. ラジオ・雑談MC・ナレーション王道 (7B Broadcast Glue)",
                "長時間聴いても一切疲れない温かいチェストと、リップノイズ・打鍵音の完全遮断",
                Cap::BroadcastDynamic7B, 0.80f, 0.34f, 0.78f,
                0.68f, 0.64f, 0.54f, 0.56f,
                0.56f, 0.50f, 0.42f, 0.48f, 0.36f,
                0.70f, 0.56f, 0.60f, 0.38f, 0.34f, 0.48f,
                0.16f, 0.18f, 0.08f, 0.40f, -14.0f, -1.0f
            },
            // 22: Shorts / TikTok Loudness Winner (-12 LUFS)
            {
                21, "SHORTS_LOUDNESS_WINNER",
                "22. YouTube Shorts / TikTok スマホ爆音映え (-12 LUFS)",
                "スマホ縦型動画で最初の0.5秒で耳を奪う高密度インフレーター＆ロッシーコーデック保護",
                Cap::ModernC800G, 0.76f, 0.38f, 0.64f,
                0.48f, 0.68f, 0.56f, 0.64f,
                0.48f, 0.56f, 0.72f, 0.68f, 0.62f,
                0.80f, 0.72f, 0.62f, 0.42f, 0.60f, 0.52f,
                0.48f, 0.32f, 0.22f, 0.65f, -12.0f, -0.8f
            },
            // 23: Apple Music Spatial & Dolby Atmos Ready (-16 LUFS)
            {
                22, "APPLE_MUSIC_SPATIAL",
                "23. Apple Music SoundCheck ＆ 空間オーディオ基準 (-16 LUFS)",
                "過度なリミッティングを避け、立体的な前後奥行きと微小なブレス表現を100%温存",
                Cap::TransparentReference, 0.48f, 0.22f, 0.50f,
                0.30f, 0.52f, 0.40f, 0.44f,
                0.38f, 0.42f, 0.58f, 0.52f, 0.50f,
                0.46f, 0.38f, 0.42f, 0.28f, 0.36f, 0.20f,
                0.44f, 0.48f, 0.38f, 0.78f, -16.0f, -1.0f
            },
            // 24: Yoimatsuri Grand Finale (VocalGzzio Flagship Masterpiece)
            {
                23, "YOIMATSURI_GRAND_FINALE",
                "24. 宵祭グランドフィナーレ (VocalGzzio 旗艦マスタリング)",
                "全14種の科学DSPエンジンが黄金比で協調動作するVocalGzzio最高峰フラッグシップ設定",
                Cap::ModernC800G, 0.72f, 0.35f, 0.60f,
                0.42f, 0.62f, 0.50f, 0.56f,
                0.48f, 0.48f, 0.68f, 0.64f, 0.62f,
                0.72f, 0.60f, 0.54f, 0.40f, 0.50f, 0.34f,
                0.50f, 0.38f, 0.28f, 0.75f, -14.0f, -1.0f
            }
        }};
        return kSpecs;
    }

    static const SignatureVocalConsoleSpec& getSpecByIndex(int index) noexcept
    {
        const auto& cat = getCatalog();
        const int clamped = std::clamp(index, 0, kNumConsoles - 1);
        return cat[static_cast<size_t>(clamped)];
    }

    /**
     * @brief 4つのコンソール仕様 (topLeft, topRight, bottomLeft, bottomRight) を
     *        2D座標 (x01, y01) で双線形モーフィング補間した新しいコンソール仕様を生成
     */
    static SignatureVocalConsoleSpec interpolateFourConsoles(
        const SignatureVocalConsoleSpec& topLeft,
        const SignatureVocalConsoleSpec& topRight,
        const SignatureVocalConsoleSpec& bottomLeft,
        const SignatureVocalConsoleSpec& bottomRight,
        float x01,
        float y01) noexcept
    {
        const float tx = std::clamp(x01, 0.0f, 1.0f);
        const float ty = std::clamp(y01, 0.0f, 1.0f);
        const float wTL = (1.0f - tx) * (1.0f - ty);
        const float wTR = tx * (1.0f - ty);
        const float wBL = (1.0f - tx) * ty;
        const float wBR = tx * ty;

        auto bilerp = [&](float a, float b, float c, float d) noexcept -> float
        {
            return wTL * a + wTR * b + wBL * c + wBR * d;
        };

        SignatureVocalConsoleSpec out = topLeft;
        out.capsuleCharacter01         = bilerp(topLeft.capsuleCharacter01,         topRight.capsuleCharacter01,         bottomLeft.capsuleCharacter01,         bottomRight.capsuleCharacter01);
        out.marinairTransformerDrive01 = bilerp(topLeft.marinairTransformerDrive01, topRight.marinairTransformerDrive01, bottomLeft.marinairTransformerDrive01, bottomRight.marinairTransformerDrive01);
        out.dynamicProximityGuard01    = bilerp(topLeft.dynamicProximityGuard01,    topRight.dynamicProximityGuard01,    bottomLeft.dynamicProximityGuard01,    bottomRight.dynamicProximityGuard01);
        out.zeroLagDeReverb01          = bilerp(topLeft.zeroLagDeReverb01,          topRight.zeroLagDeReverb01,          bottomLeft.zeroLagDeReverb01,          bottomRight.zeroLagDeReverb01);
        out.weissRelativeDeEss01       = bilerp(topLeft.weissRelativeDeEss01,       topRight.weissRelativeDeEss01,       bottomLeft.weissRelativeDeEss01,       bottomRight.weissRelativeDeEss01);
        out.nasalHonkSuppress01        = bilerp(topLeft.nasalHonkSuppress01,        topRight.nasalHonkSuppress01,        bottomLeft.nasalHonkSuppress01,        bottomRight.nasalHonkSuppress01);
        out.harshPharynxTame01         = bilerp(topLeft.harshPharynxTame01,         topRight.harshPharynxTame01,         bottomLeft.harshPharynxTame01,         bottomRight.harshPharynxTame01);
        out.pultecLowTrick01           = bilerp(topLeft.pultecLowTrick01,           topRight.pultecLowTrick01,           bottomLeft.pultecLowTrick01,           bottomRight.pultecLowTrick01);
        out.pultecMeqSweetMid01        = bilerp(topLeft.pultecMeqSweetMid01,        topRight.pultecMeqSweetMid01,        bottomLeft.pultecMeqSweetMid01,        bottomRight.pultecMeqSweetMid01);
        out.pultecInductorAir01        = bilerp(topLeft.pultecInductorAir01,        topRight.pultecInductorAir01,        bottomLeft.pultecInductorAir01,        bottomRight.pultecInductorAir01);
        out.sundbergSingersFormant01   = bilerp(topLeft.sundbergSingersFormant01,   topRight.sundbergSingersFormant01,   bottomLeft.sundbergSingersFormant01,   bottomRight.sundbergSingersFormant01);
        out.passaggioBridge01          = bilerp(topLeft.passaggioBridge01,          topRight.passaggioBridge01,          bottomLeft.passaggioBridge01,          bottomRight.passaggioBridge01);
        out.serbanParallelBus01        = bilerp(topLeft.serbanParallelBus01,        topRight.serbanParallelBus01,        bottomLeft.serbanParallelBus01,        bottomRight.serbanParallelBus01);
        out.oxfordInflatorDensity01    = bilerp(topLeft.oxfordInflatorDensity01,    topRight.oxfordInflatorDensity01,    bottomLeft.oxfordInflatorDensity01,    bottomRight.oxfordInflatorDensity01);
        out.sslBusGlue01               = bilerp(topLeft.sslBusGlue01,               topRight.sslBusGlue01,               bottomLeft.sslBusGlue01,               bottomRight.sslBusGlue01);
        out.studer30IpsTape01          = bilerp(topLeft.studer30IpsTape01,          topRight.studer30IpsTape01,          bottomLeft.studer30IpsTape01,          bottomRight.studer30IpsTape01);
        out.aphexPhaseExciter01        = bilerp(topLeft.aphexPhaseExciter01,        topRight.aphexPhaseExciter01,        bottomLeft.aphexPhaseExciter01,        bottomRight.aphexPhaseExciter01);
        out.missingFundamentalBass01   = bilerp(topLeft.missingFundamentalBass01,   topRight.missingFundamentalBass01,   bottomLeft.missingFundamentalBass01,   bottomRight.missingFundamentalBass01);
        out.h3000MicroPitchWidth01     = bilerp(topLeft.h3000MicroPitchWidth01,     topRight.h3000MicroPitchWidth01,     bottomLeft.h3000MicroPitchWidth01,     bottomRight.h3000MicroPitchWidth01);
        out.bricastiEarlyPocket01      = bilerp(topLeft.bricastiEarlyPocket01,      topRight.bricastiEarlyPocket01,      bottomLeft.bricastiEarlyPocket01,      bottomRight.bricastiEarlyPocket01);
        out.hadamardVelvetReverb01     = bilerp(topLeft.hadamardVelvetReverb01,     topRight.hadamardVelvetReverb01,     bottomLeft.hadamardVelvetReverb01,     bottomRight.hadamardVelvetReverb01);
        out.justIntonationPurity01     = bilerp(topLeft.justIntonationPurity01,     topRight.justIntonationPurity01,     bottomLeft.justIntonationPurity01,     bottomRight.justIntonationPurity01);
        out.targetLufs                 = bilerp(topLeft.targetLufs,                 topRight.targetLufs,                 bottomLeft.targetLufs,                 bottomRight.targetLufs);
        out.truePeakCeilingDbTp        = bilerp(topLeft.truePeakCeilingDbTp,        topRight.truePeakCeilingDbTp,        bottomLeft.truePeakCeilingDbTp,        bottomRight.truePeakCeilingDbTp);
        return out;
    }

    /**
     * @brief 歌声の基本周波数 (meanF0Hz)・クレストファクター (crestDb)・歯擦音比 (sibilance01) から
     *        最適なシグネチャーコンソール番号 (0..23) を自動推薦
     */
    static int recommendOptimalConsoleIndex(float meanF0Hz, float crestDb, float sibilance01, bool isLiveStreaming) noexcept
    {
        if (isLiveStreaming)
            return 6; // 07: Utaite Live Stream Guard
        if (meanF0Hz > 0.0f && meanF0Hz < 145.0f)
            return 14; // 15: Baritone Virtual Bass
        if (meanF0Hz > 330.0f && crestDb < 9.5f)
            return 5; // 06: J-Pop / Anisong Prism Cut
        if (crestDb > 14.5f && sibilance01 > 0.35f)
            return 7; // 08: Whisper Velvet Air
        if (meanF0Hz >= 280.0f && crestDb >= 11.5f)
            return 10; // 11: Sundberg Stage Belt
        return 23; // 24: Yoimatsuri Grand Finale
    }
};

} // namespace vg::console_presets
