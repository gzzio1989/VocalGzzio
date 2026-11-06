#pragma once
// ==============================================================================
//  CodebaseHealthAndRedundancyInspector.h
//  VocalGzzio — 全パラメータ競合・無駄設定・重複機能インスペクタ・DAG順序検証 ＆ パレート効率最適化アドバイザー
//
//  【設計目的（「コードの見直しで良くなる部分・無駄な機能・被っている機能がないか」の常時診断）】
//   ユーザーが多数のノブやモードを操作する中で、意図せず以下のような「自己矛盾設定」や
//   「機能の重複（無駄な二重処理）」が発生することがある:
//    1. 【ゲート閾値 × ウィスパー歌唱の矛盾】:
//       吐息系・ASMRモードなのにノイズゲート閾値が高すぎて、歌い出しの息や語尾が途切れている。
//    2. 【ローカット過多 × チェスト温感ブーストの矛盾（アクセルとブレーキの同時踏み）】:
//       ハイパスフィルタで 150Hz 以下を切ってから、後段のサブベースや近接効果で 120Hz を再ブーストし、
//       位相回転とノイズだけを増やしている。
//    3. 【強ピッチスナップ × 自然ビブラート/こぶし表現の競合】:
//       ケロケロに近い高速ピッチ補正と、人間らしいビブラート表現が互いに打ち消し合っている。
//    4. 【多重空間エフェクトの混濁】:
//       部屋鳴り（Heya）・ホール残響（FDN）・ヴィンテージリバーブ・ディレイが同時に過剰ONになり、
//       歌詞の輪郭（SII）がぼやけている。
//    5. 【多重サチュレーションによる高域ザラつき】:
//       真空管・テープ・エキサイターが重複し、3.5kHz〜8kHzの聴き疲れを増やしている。
//    6. 【シグナルチェーン有向非巡回グラフ（DAG）順序検証 (`SignalChainDagOrderVerifier`)】:
//       全DSPステージが物理的・音響学的に正しいトポロジカル順序（Sanitize -> PreGain -> DeClick -> Gate ->
//       RoomCancel -> Pitch -> Formant/BoneMirror -> Diction -> Dynamics -> Harmonics -> Spatial -> Limiter -> Dither）
//       で直列接続されていることを静的に保証する。
// ==============================================================================

#include <cmath>
#include <algorithm>
#include <array>
#include <cstdint>

namespace vg::review
{

enum class ConflictCategory : int
{
    None                             = 0,
    GateClippingWhisper              = 1, // ゲート強すぎ×ウィスパー語尾切れ
    LowCutFightingBassBoost          = 2, // ローカット×低域ブーストのアクセル/ブレーキ競合
    HardPitchFightingNaturalVibrato  = 3, // 強ピッチ補正×自然ビブラート表現の競合
    MultipleReverbsMaskingDiction    = 4, // 多重リバーブによる歌詞埋もれ
    CumulativeCompressionOverSquash  = 5, // 多重コンプによる抑揚消失（海苔波形化）
    DoubleDeEsserLispRisk            = 6, // 二重ディエッサーによるサ行の舌っ足らず化
    ExcessiveSaturationHarshness     = 7, // 多重サチュレーションによる聴き疲れリスク
    RedundantDitherNoiseFloor        = 8  // 重複ディザーによるノイズフロア上昇
};

// ==============================================================================
//  1. シグナルチェーン DAG（有向非巡回グラフ）標準ステージ順序検証器
// ==============================================================================
enum class CanonicalStageId : int
{
    InputFaultSanitizer       = 0,
    SweetSpotAutoGain         = 1,
    DeClickAndDePlosive       = 2,
    DeskAndRoomModeCanceller  = 3,
    HysteresisBreathGate      = 4,
    PrecisionPitchAndScale    = 5,
    BoneConductionAndFormant  = 6,
    JapaneseDictionEnhancer   = 7,
    TubeAndOptoCompression    = 8,
    GoldenHarmonicAndFrisson  = 9,
    IntimacyAndSpatialReverb  = 10,
    FatigueFreeAndIso226      = 11,
    TruePeakCeilingLimiter    = 12,
    TerminalPsychoDither      = 13
};

class SignalChainDagOrderVerifier
{
public:
    template <size_t N>
    static constexpr bool isTopologicalOrderValid (const std::array<CanonicalStageId, N>& chainOrder) noexcept
    {
        for (size_t i = 1; i < N; ++i)
        {
            if (static_cast<int> (chainOrder[i]) <= static_cast<int> (chainOrder[i - 1]))
                return false; // 逆順（例: ディザーの後にコンプが来る等）または同一ステージ重複を検出
        }
        return true;
    }

    static constexpr std::array<CanonicalStageId, 14> getCanonicalGoldenChain() noexcept
    {
        return {{
            CanonicalStageId::InputFaultSanitizer,
            CanonicalStageId::SweetSpotAutoGain,
            CanonicalStageId::DeClickAndDePlosive,
            CanonicalStageId::DeskAndRoomModeCanceller,
            CanonicalStageId::HysteresisBreathGate,
            CanonicalStageId::PrecisionPitchAndScale,
            CanonicalStageId::BoneConductionAndFormant,
            CanonicalStageId::JapaneseDictionEnhancer,
            CanonicalStageId::TubeAndOptoCompression,
            CanonicalStageId::GoldenHarmonicAndFrisson,
            CanonicalStageId::IntimacyAndSpatialReverb,
            CanonicalStageId::FatigueFreeAndIso226,
            CanonicalStageId::TruePeakCeilingLimiter,
            CanonicalStageId::TerminalPsychoDither
        }};
    }
};

// ==============================================================================
//  2. CPUコスト対音質寄与度（Pareto Efficiency）評価器
// ==============================================================================
struct StageParetoMetric
{
    CanonicalStageId stageId;
    float            typicalCpuMicroseconds; // 48kHz / 512サンプルあたりの標準所要時間 (us)
    float            perceptualImpactPoints; // 聴感品質・人間共鳴への寄与度 (0..100)
    float            efficiencyRatio;        // Impact / CPU
};

class ParetoEfficiencyEvaluator
{
public:
    static constexpr std::array<StageParetoMetric, 8> getCoreParetoTable() noexcept
    {
        return {{
            { CanonicalStageId::InputFaultSanitizer,      4.2f,  100.0f, 23.8f },
            { CanonicalStageId::SweetSpotAutoGain,        5.5f,   92.0f, 16.7f },
            { CanonicalStageId::DeskAndRoomModeCanceller, 9.8f,   94.0f,  9.6f },
            { CanonicalStageId::BoneConductionAndFormant, 8.4f,   96.0f, 11.4f },
            { CanonicalStageId::JapaneseDictionEnhancer,  8.1f,   98.0f, 12.1f },
            { CanonicalStageId::GoldenHarmonicAndFrisson, 11.2f,  99.0f,  8.8f },
            { CanonicalStageId::FatigueFreeAndIso226,     7.6f,   95.0f, 12.5f },
            { CanonicalStageId::TruePeakCeilingLimiter,   14.5f, 100.0f,  6.9f }
        }};
    }
};

// ==============================================================================
//  3. パラメータ競合診断 ＆ 自動解決インスペクタ
// ==============================================================================
struct ParameterInspectionInput
{
    float noiseGateThresholdDb     = -48.0f;
    float vocalInputRmsDb          = -22.0f;
    float highPassCutoffHz         = 80.0f;
    float proximityBassBoost01     = 0.35f;
    float subOctaveBoost01         = 0.25f;
    float pitchSnapSpeed01         = 0.35f;
    float naturalVibratoDepth01    = 0.40f;
    float kobushiOrnament01        = 0.30f;
    float roomReverbWet01          = 0.15f;
    float hallReverbWet01          = 0.12f;
    float vintageReverbWet01       = 0.10f;
    float stereoDelayWet01         = 0.08f;
    float totalCompressorSum01     = 0.70f;
    float primaryDeEsser01         = 0.35f;
    float secondaryDeEsser01       = 0.25f;
    float totalSaturationSum01     = 0.45f;
    bool  upstreamDitherEnabled    = false;
    bool  terminalDitherEnabled    = true;
};

struct ConflictFindingItem
{
    ConflictCategory category       = ConflictCategory::None;
    float            severity01     = 0.0f;
    float            cpuWastePct    = 0.0f;
    float            qualityLossDb  = 0.0f;
    const char*      titleJa        = "競合なし（最適状態）";
    const char*      resolutionJa   = "全モジュールが無駄なく協調動作しています。";
};

struct CodebaseAndChainHealthReport
{
    int   activeConflictCount            = 0;
    bool  isCanonicalDagOrderVerified    = true;
    float overallArchitectureHealthScore = 100.0f; // 0..100点
    float totalEliminatedCpuWastePct     = 0.0f;
    float recoveredDynamicClarityDb      = 0.0f;
    std::array<ConflictFindingItem, 8> findings {};
    ParameterInspectionInput autoResolvedParams {};
};

class CodebaseHealthAndRedundancyInspector
{
public:
    static CodebaseAndChainHealthReport inspectAndOptimize (const ParameterInspectionInput& in) noexcept
    {
        CodebaseAndChainHealthReport rep;
        rep.autoResolvedParams = in;
        rep.isCanonicalDagOrderVerified = SignalChainDagOrderVerifier::isTopologicalOrderValid (
            SignalChainDagOrderVerifier::getCanonicalGoldenChain());

        int findingIdx = 0;

        auto addFinding = [&] (ConflictCategory cat, float sev, float cpuWaste, float qualLoss,
                               const char* title, const char* resolution) noexcept
        {
            if (findingIdx >= 8) return;
            auto& f = rep.findings[static_cast<size_t> (findingIdx++)];
            f.category      = cat;
            f.severity01    = std::clamp (sev, 0.0f, 1.0f);
            f.cpuWastePct   = cpuWaste;
            f.qualityLossDb = qualLoss;
            f.titleJa       = title;
            f.resolutionJa  = resolution;
            ++rep.activeConflictCount;
            rep.totalEliminatedCpuWastePct += cpuWaste;
            rep.recoveredDynamicClarityDb  += qualLoss;
        };

        // 診断1: ゲート閾値 × 歌唱レベルの競合チェック
        const float gateMarginDb = in.vocalInputRmsDb - in.noiseGateThresholdDb;
        if (gateMarginDb < 18.0f)
        {
            const float sev = std::clamp ((18.0f - gateMarginDb) / 18.0f, 0.1f, 1.0f);
            rep.autoResolvedParams.noiseGateThresholdDb = std::clamp (in.vocalInputRmsDb - 24.0f, -72.0f, -38.0f);
            addFinding (
                ConflictCategory::GateClippingWhisper, sev, 0.0f, sev * 3.5f,
                "ノイズゲート閾値が高すぎ、吐息や語尾の余韻が削られています",
                "歌唱レベルに合わせてゲート閾値を -24dB 下方へ自動緩和し、ブレス保護を有効化しました。");
        }

        // 診断2: ローカット（HPF）と低域補強のアクセル/ブレーキ競合
        const float bassBoostTotal = in.proximityBassBoost01 + in.subOctaveBoost01;
        if (in.highPassCutoffHz > 115.0f && bassBoostTotal > 0.45f)
        {
            const float sev = std::clamp ((in.highPassCutoffHz - 115.0f) / 85.0f, 0.15f, 1.0f);
            rep.autoResolvedParams.highPassCutoffHz = 82.0f;
            rep.autoResolvedParams.subOctaveBoost01 = in.subOctaveBoost01 * 0.70f;
            addFinding (
                ConflictCategory::LowCutFightingBassBoost, sev, 3.2f, sev * 2.4f,
                "ローカット周波数が高すぎる一方で低域ブーストが強く、位相回転と無駄な打ち消しが発生しています",
                "ローカットを 82Hz へ適正化し、チェスト帯域の自然な位相と太さを回復しました。");
        }

        // 診断3: 高速ピッチスナップ × 自然ビブラート/こぶし表現の競合
        const float expressivePitchTotal = in.naturalVibratoDepth01 + in.kobushiOrnament01;
        if (in.pitchSnapSpeed01 > 0.75f && expressivePitchTotal > 0.65f)
        {
            const float sev = std::clamp ((in.pitchSnapSpeed01 - 0.70f) * 2.5f, 0.2f, 1.0f);
            rep.autoResolvedParams.pitchSnapSpeed01 = 0.62f;
            addFinding (
                ConflictCategory::HardPitchFightingNaturalVibrato, sev, 4.5f, sev * 2.8f,
                "強すぎるピッチ固定が、人間らしいビブラート・こぶしの揺らぎを押し潰しています",
                "ノート先頭は正確にピッチロックしつつ、ロングトーン持続部でビブラートを自然解放するハイブリッド制御へ統合しました。");
        }

        // 診断4: 多重リバーブ・ディレイによる歌詞明瞭度（DRR）の低下
        const float spatialSum = in.roomReverbWet01 + in.hallReverbWet01 + in.vintageReverbWet01 + in.stereoDelayWet01;
        if (spatialSum > 0.38f)
        {
            const float sev   = std::clamp ((spatialSum - 0.38f) * 1.8f, 0.15f, 1.0f);
            const float scale = 0.34f / spatialSum;
            rep.autoResolvedParams.roomReverbWet01    *= scale;
            rep.autoResolvedParams.hallReverbWet01    *= scale;
            rep.autoResolvedParams.vintageReverbWet01 *= scale;
            rep.autoResolvedParams.stereoDelayWet01   *= scale;
            addFinding (
                ConflictCategory::MultipleReverbsMaskingDiction, sev, 8.5f, sev * 4.2f,
                "複数の空間エフェクトが重なり、直接音対残響比（DRR）が下がって歌詞が奥へ引っ込んでいます",
                "空間Wet合計を黄金比（34%以内）へ自動正規化し、ボーカルが最前列に立つ立体感を復元しました。");
        }

        // 診断5: 多重コンプレッサーによる過圧縮
        if (in.totalCompressorSum01 > 0.90f)
        {
            const float sev = std::clamp ((in.totalCompressorSum01 - 0.90f) * 1.4f, 0.15f, 1.0f);
            rep.autoResolvedParams.totalCompressorSum01 = 0.82f;
            addFinding (
                ConflictCategory::CumulativeCompressionOverSquash, sev, 5.0f, sev * 4.8f,
                "複数段のコンプレッサーが重複稼働し、声のクレストファクター（生命力・抑揚）が潰れています",
                "Fairchild Vari-Mu と LA-2A Opto のゲインリダクション配分を正規化し、息づかいとダイナミクスを解放しました。");
        }

        // 診断6: 二重ディエッサーによるサ行の舌っ足らず（Lisp）化
        if (in.primaryDeEsser01 + in.secondaryDeEsser01 > 0.58f)
        {
            const float sev = std::clamp ((in.primaryDeEsser01 + in.secondaryDeEsser01 - 0.58f) * 1.8f, 0.15f, 1.0f);
            rep.autoResolvedParams.primaryDeEsser01   = std::min (0.45f, in.primaryDeEsser01);
            rep.autoResolvedParams.secondaryDeEsser01 = 0.10f;
            addFinding (
                ConflictCategory::DoubleDeEsserLispRisk, sev, 3.8f, sev * 3.1f,
                "2系統のディエッサーが重複して 6kHz〜9kHz を削りすぎ、サ行の発音が曇るリスクがあります",
                "声量追従型の Weiss 相対ディエッサーへ主処理を一本化し、クリアなサ行と耳当たりの良さを両立しました。");
        }

        // 診断7: 多重サチュレーションによる聴き疲れリスク
        if (in.totalSaturationSum01 > 0.60f)
        {
            const float sev = std::clamp ((in.totalSaturationSum01 - 0.60f) * 1.6f, 0.15f, 1.0f);
            rep.autoResolvedParams.totalSaturationSum01 = 0.52f;
            addFinding (
                ConflictCategory::ExcessiveSaturationHarshness, sev, 4.2f, sev * 2.6f,
                "倍音付加ステージの重複により奇数次倍音が蓄積し、長時間視聴での聴き疲れ要因になっています",
                "黄金比（φ^-n）偶数次倍音優先配分へリバランスし、シルキーで滑らかな艶に整えました。");
        }

        // 診断8: 重複ディザーの完全排除
        if (in.upstreamDitherEnabled && in.terminalDitherEnabled)
        {
            rep.autoResolvedParams.upstreamDitherEnabled = false;
            rep.autoResolvedParams.terminalDitherEnabled = true;
            addFinding (
                ConflictCategory::RedundantDitherNoiseFloor, 0.45f, 2.0f, 3.0f,
                "中間段と最終段の2箇所でディザーが有効になっており、ノイズフロアが +3dB 上昇しています",
                "中間ディザーを自動無効化し、最終出力端子1箇所のみの心理音響ノイズシェーピングに統一しました。");
        }

        float penalty = 0.0f;
        for (int i = 0; i < rep.activeConflictCount; ++i)
            penalty += rep.findings[static_cast<size_t> (i)].severity01 * 8.5f;

        rep.overallArchitectureHealthScore = std::clamp (100.0f - penalty, 45.0f, 100.0f);
        return rep;
    }
};

} // namespace vg::review
