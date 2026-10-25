#pragma once

#include <initializer_list>

// Preset definition tables for VocalGzzio v5.0.0.
// 3軸直交プリセット体系:
//  ① kVoicePresets[20] : 声質・発声・悩み補正（男女共通・発声科学ベース4カテゴリ）
//  ② kMicPresets[20]   : マイク方式・設置・部屋環境補正（3カテゴリ）
//  ③ kEqPresets[20]    : 音楽ジャンル・サブジャンル・曲調レシピ（4カテゴリ）

namespace gzzio
{
// 主画面の目的別6種仕上がり（finishPresetBox 用・互換維持）
struct FinishPresetDef
{
    const char* name;
    const char* nameEn;
    const char* desc;
    const char* descEn;
    float lowcut, mud, harsh, presence, air, warmth;
    float peaks, level, attack, release, deess, sustain, consonants;
    int reverbType;
    float reverb, size, delay, delayMs, feedback, duck;
};

inline constexpr FinishPresetDef kFinishPresets[] =
{
    { "自然に整える", "Natural",
      "声や楽器の抑揚を残して、低い振動と小さな音量差を整えます。響きは短く控えめ。まず原音に近い仕上がりから始めたいときに。",
      "Keeps the expression of your voice or instrument, gently controlling rumble and peaks with a small room. Start here for a natural finish.",
      75, -1, -0.5f, 0, 0, 0, 12, 10, 24, 180, 15, 0, 0,
      1, 9, 20, 0, 240, 15, 0 },
    { "歌詞を前へ", "Vocal Forward",
      "低いこもりを引いて言葉の輪郭と明るさを足します。大小をそろえ、短い響きで歌を伴奏の前へ。硬く感じたら「明るさ」を下げてください。",
      "Clears low mids, adds presence and air, and evens out phrases. A short plate keeps the vocal in front of the backing. Reduce brightness if it sounds sharp.",
      120, -4.5f, -1, 4.5f, 3, 12, 45, 52, 8, 100, 40, 0, 15,
      2, 15, 35, 0, 240, 15, 30 },
    { "丸く親密に", "Warm & Intimate",
      "低音の厚みを残し、耳に当たる高音を丸くします。遅めの音量調整で出だしを残す、近く落ち着いた声。明るすぎる録音や静かな曲に。",
      "Retains body while softening the upper mids. Slower dynamics preserve the start of each phrase, with a close room for intimate vocals or bright recordings.",
      65, 0, -4, -2, 0, 32, 20, 34, 25, 220, 30, 8, 0,
      1, 14, 30, 0, 240, 15, 0 },
    { "太く力強く", "Dense & Driven",
      "大きな音を速く受け止め、音量差を強めにそろえて密度を出します。中高音の芯と軽い歪みを足す、ロックや力強い歌の仕上がり。",
      "Catches loud peaks quickly and reduces the gap between loud and quiet phrases. Adds midrange weight and gentle saturation for dense, driven vocals.",
      90, -2, -1.5f, 3, 1, 55, 65, 60, 4, 90, 45, 12, 5,
      2, 10, 20, 0, 240, 15, 40 },
    { "語りをくっきり", "Speech Focus",
      "低音の膨らみを削り、言葉の出だしと小さな声をそろえます。残響とやまびこは切り、言葉を近くはっきり届けます。歌では乾いた質感に。",
      "Reduces low-end buildup and brings quiet words forward. Reverb and delay are off for close, intelligible speech or a deliberately dry vocal.",
      145, -5, -2, 2.5f, 0, 0, 35, 68, 3, 80, 50, 0, 30,
      1, 0, 0, 0, 240, 15, 0 },
    { "広く響かせる", "Wide Ballad",
      "明るさを少し足し、長いホールと薄いやまびこで余韻を広げます。歌っている間は響きを抑え、語尾から広がるバラード向け。響きが多ければ「残響量」を下げてください。",
      "Adds a long hall and a quiet echo, opening up the ends of phrases. Ducking keeps the words clear while you sing. Reduce reverb for a closer sound.",
      90, -2.5f, -1, 1, 3.5f, 10, 28, 28, 18, 180, 28, 0, 0,
      3, 32, 70, 12, 320, 18, 35 }
};
inline constexpr int kNumFinishPresets = (int) (sizeof (kFinishPresets) / sizeof (kFinishPresets[0]));

// setParameter(const char* id, float unnormalisedValue) をUIと検査で共用。
// 入力音量・ノイズ測定・用途・音程設定・出力音量を保持する。
// 一方、音色/音量/空間の設定は毎回確定し、直前の補助設定に依存させない。
// 音程補正やハモリを保ったまま、仕上がりだけを比較できる。
template <typename SetParameter>
bool applyFinishPreset (int index, SetParameter&& setParameter)
{
    if (index < 0 || index >= kNumFinishPresets) return false;
    const auto& p = kFinishPresets[index];
    for (const char* id : { "mod_totonoe", "mod_soroe", "mod_sagyo", "mod_neiro", "mod_hirogari" })
        setParameter (id, 1.0f);
    for (const char* id : { "res_amt", "ride_amt", "ring", "br_amt", "emo_amt", "lift_amt",
                           "robo_mix", "mega_amt", "cho_amt", "doubler", "width", "seq_on",
                           "crush_on", "ord_eq", "ord_space", "jn_solo" })
        setParameter (id, 0.0f);
    setParameter ("mix", 100.0f);
    setParameter ("ord_deess", 1.0f);
    setParameter ("ds_on", 1.0f);
    setParameter ("lowcut", p.lowcut);
    setParameter ("mud", p.mud);
    setParameter ("harsh", p.harsh);
    setParameter ("presence", p.presence);
    setParameter ("air", p.air);
    setParameter ("drive", p.warmth);
    setParameter ("comp1", p.peaks);
    setParameter ("comp2", p.level);
    setParameter ("attack", p.attack);
    setParameter ("release", p.release);
    setParameter ("deess", p.deess);
    setParameter ("sustain", p.sustain);
    setParameter ("cons_amt", p.consonants);
    setParameter ("rev_type", (float) p.reverbType);
    setParameter ("revon", p.reverb > 0.0f ? 1.0f : 0.0f);
    setParameter ("revmix", p.reverb);
    setParameter ("revsize", p.size);
    setParameter ("dly_on", p.delay > 0.0f ? 1.0f : 0.0f);
    setParameter ("delay", p.delay);
    setParameter ("dly_sync", 0.0f);
    setParameter ("dly_ms", p.delayMs);
    setParameter ("dly_fb", p.feedback);
    setParameter ("dly_hc", 5000.0f);
    setParameter ("duck", p.duck);
    return true;
}

// v5.0.0: 3軸直交プリセット体系
//  ① kVoicePresets[20] : 声質・発声・悩み補正（男女共通・発声科学ベース4カテゴリ）
//  ② kMicPresets[20]   : マイク方式・設置・部屋環境補正（3カテゴリ）
//  ③ kEqPresets[20]    : 音楽ジャンル・サブジャンル・曲調レシピ（4カテゴリ）
struct VoicePresetDef
{
    const char* name;
    const char* nameEn;
    const char* desc;
    const char* descEn;
    float c1, c2, atk, rel, pres, drv, sus;
    // v5.0 拡張: 発声・声帯特性・弱点カバーに直結するDSP＆Argonパラメータ
    float ride, res, cons, br, ring, ds;
    bool crush;
    float velvet, focus, silk;
};
inline constexpr VoicePresetDef kVoicePresets[] =
{
    // ── 【1. 声の重心・音域タイプ（男女共通 4種）】(0..3) ──
    { "ハイトーン・クリア（高音域・透明感）", "High Register / Clear",
      "男女問わず高音域が得意で澄んだ声向け。高域の刺さりを防ぎつつ透明感と伸びを引き出します",
      "For clear high-register voices (any gender). Preserves crystalline air while taming upper-mid harshness.",
      30.0f, 30.0f, 10.0f, 140.0f, 2.0f,  8.0f, 26.0f, 35.0f, 35.0f, 20.0f, 18.0f, 28.0f, 32.0f, false, 28.0f, 56.0f, 74.0f },
    { "ミッド・ナチュラル（中音域・標準バランス）", "Mid Register / Natural",
      "最も多い標準的な中音域の声向け。自然な声色を崩さず、歌でもトークでも聴きやすく整えます",
      "Balanced mid-register vocal profile. Keeps your natural timbre intact with effortless clarity.",
      34.0f, 34.0f,  9.0f, 145.0f, 1.8f, 14.0f, 24.0f, 35.0f, 30.0f, 22.0f, 12.0f, 25.0f, 28.0f, false, 42.0f, 54.0f, 58.0f },
    { "ローミッド・リッチ（中低音域・艶と包容力）", "Low-Mid / Rich & Warm",
      "落ち着いた中低音域の魅力を活かす設定。胸の温かみと滑らかな倍音の艶を豊かに引き出します",
      "For warm low-mid voices. Enhances chest resonance and silky harmonic richness without mud.",
      32.0f, 38.0f, 12.0f, 175.0f, 1.2f, 22.0f, 30.0f, 38.0f, 32.0f, 20.0f, 14.0f, 24.0f, 26.0f, false, 64.0f, 52.0f, 54.0f },
    { "ディープ・チェスト（低音・重低音・太い胸声）", "Deep / Chest Voice",
      "深く響く低音ボイス向け。低域のボワつきだけを整理し、芯のある重厚な存在感を前に出します",
      "For deep chest-dominant voices. Controls sub-bass bloat while locking in authoritative low-end weight.",
      32.0f, 40.0f, 12.0f, 190.0f, 1.5f, 28.0f, 22.0f, 40.0f, 35.0f, 25.0f, 10.0f, 22.0f, 24.0f, false, 72.0f, 58.0f, 46.0f },

    // ── 【2. 発声・声帯の鳴り方タイプ（6種）】(4..9) ──
    { "ウィスパー・エアリー（吐息多め・繊細発声）", "Whisper / Airy Breath",
      "息を多く含んだ繊細な発声向け。微細なブレスの粒立ちを潰さず、耳元で鳴る空気感を持ち上げます",
      "For breathy, airy phonation. Gently lifts delicate breath textures without pumping or harsh sibilance.",
      22.0f, 46.0f, 15.0f, 220.0f, 1.2f, 12.0f, 40.0f, 48.0f, 28.0f, 18.0f, 38.0f, 18.0f, 36.0f, false, 48.0f, 44.0f, 82.0f },
    { "ソフト・シルキー（優しい丸い声・声門閉鎖弱め）", "Soft / Silky Tone",
      "力みのない優しい声質向け。アタックを柔らかく包み込み、シルクのように滑らかな質感に整えます",
      "For gentle, light-closure voices. Smooths transients and wraps the vocal in a silky, intimate sheen.",
      26.0f, 42.0f, 14.0f, 210.0f, 1.0f, 16.0f, 36.0f, 45.0f, 32.0f, 18.0f, 24.0f, 20.0f, 30.0f, false, 56.0f, 48.0f, 72.0f },
    { "ミックスボイス・ファルセット（裏声・換声点を繋ぐ）", "Mix Voice / Falsetto",
      "地声と裏声を行き来する歌唱向け。換声点での音量差や声の細りを支え、高音まで滑らかに繋ぎます",
      "For mix-voice and falsetto transitions. Bridges register breaks and keeps head voice full and connected.",
      42.0f, 38.0f,  8.0f, 135.0f, 2.5f, 14.0f, 30.0f, 52.0f, 38.0f, 24.0f, 18.0f, 34.0f, 32.0f, false, 44.0f, 62.0f, 68.0f },
    { "パワフル・ベルティング（強声量・地声張り上げ）", "Powerful / Belting",
      "声量が大きく地声で力強く張り上げる歌唱向け。突発的なピークを素早く受け止め、耳痛を防ぎます",
      "For high-output belting. Catches sudden peaks transparently while suppressing upper-mid glare.",
      54.0f, 40.0f,  5.0f, 105.0f, 2.2f, 16.0f, 18.0f, 42.0f, 48.0f, 22.0f,  8.0f, 32.0f, 34.0f, false, 46.0f, 64.0f, 52.0f },
    { "ハスキー・スモーキー（かすれ声・倍音と息の渋み）", "Husky / Smoky",
      "かすれ成分や独特の渋みを持つ声向け。ザラつきの美味しい倍音を残し、痛くなる帯域だけを整えます",
      "For husky, smoky voices. Preserves textured harmonic rasp while smoothing fatiguing harshness.",
      42.0f, 36.0f,  7.0f, 125.0f, 1.8f, 32.0f, 24.0f, 38.0f, 42.0f, 24.0f, 22.0f, 28.0f, 34.0f, false, 68.0f, 58.0f, 60.0f },
    { "エッジ・シャウト・がなり（ロック発声・強い声門閉鎖）", "Grit / Edge / Shout",
      "がなりやシャウトを含む激しい発声向け。歪みの熱量とアタック感を保ったまま音割れと暴れを抑えます",
      "For aggressive rock grit and shouts. Locks in raw harmonic drive and punch while preventing digital clip.",
      58.0f, 42.0f,  4.0f,  90.0f, 2.8f, 34.0f, 15.0f, 40.0f, 50.0f, 30.0f,  6.0f, 36.0f, 36.0f, true,  62.0f, 72.0f, 46.0f },

    // ── 【3. 歌唱表現・テクニック特性（4種）】(10..13) ──
    { "ロングトーン・ビブラート重視（伸びと余韻の表現）", "Long Tone & Vibrato",
      "伸びやかなロングトーンや美しいビブラートを聴かせる歌唱向け。語尾の減衰を自然に支えて余韻を保ちます",
      "Tailored for sustained notes and expressive vibrato. Holds note tails gracefully without unnatural pumping.",
      34.0f, 42.0f, 11.0f, 185.0f, 2.0f, 16.0f, 38.0f, 48.0f, 34.0f, 18.0f, 16.0f, 32.0f, 28.0f, false, 52.0f, 56.0f, 66.0f },
    { "高速フロウ・早口・子音重視（速い言葉立ち・ボカロ・ラップ）", "Fast Flow & Articulation",
      "早口フレーズや高速ボカロ曲・ラップ向け。子音の立ち上がりを鋭く際立たせ、一音一音を鮮明に届けます",
      "For rapid-fire lyrics, Vocaloid covers, and rap flows. Fast transient recovery and crisp consonant definition.",
      46.0f, 30.0f,  4.0f,  80.0f, 2.8f, 18.0f, 12.0f, 40.0f, 32.0f, 48.0f, 10.0f, 30.0f, 34.0f, false, 36.0f, 74.0f, 58.0f },
    { "こぶし・しゃくり・抑揚表現（ダイナミクス・感情優先）", "Expressive Inflection",
      "こぶし・しゃくり・フォールなど細かい表情付けを多用する歌唱向け。過度な圧縮を避けて抑揚を活かします",
      "For richly inflected singing (kobushi, scoops, falls). Preserves micro-dynamics and emotional phrasing.",
      32.0f, 36.0f, 12.0f, 175.0f, 2.0f, 20.0f, 30.0f, 36.0f, 34.0f, 24.0f, 14.0f, 30.0f, 28.0f, false, 56.0f, 58.0f, 58.0f },
    { "静と動・広ダイナミクス（Aメロ吐息〜強サビの落差大）", "Wide Dynamic Contrast",
      "ささやくAメロから力強いサビまで声量差が大きい歌唱向け。自動レベラーと2段コンプで落差を綺麗に収めます",
      "For dramatic quiet-verse to belted-chorus vocals. Dual-stage leveling keeps both whispers and belts in pocket.",
      44.0f, 44.0f,  7.0f, 140.0f, 2.2f, 18.0f, 32.0f, 65.0f, 42.0f, 24.0f, 16.0f, 30.0f, 32.0f, false, 48.0f, 60.0f, 64.0f },

    // ── 【4. 声の悩み・弱点お助けカバー（6種）】(14..19) ──
    { "悩み対策：声が細く伴奏に埋もれやすい（太さ・声芯補強）", "Fix: Thin Voice in Mix",
      "「オケに負ける・声が細い」時の処方箋。倍音密度と3kHzの歌手フォルマントを補強し、前に出る太い声にします",
      "Remedy for thin vocals buried in the mix. Reinforces harmonic body, vocal core, and 3 kHz singer's formant.",
      46.0f, 42.0f,  7.0f, 130.0f, 3.2f, 26.0f, 32.0f, 55.0f, 32.0f, 36.0f, 12.0f, 46.0f, 28.0f, true,  60.0f, 72.0f, 56.0f },
    { "悩み対策：鼻声・こもりやすく歌詞が不明瞭（スッキリ明瞭化）", "Fix: Nasal / Muffled Voice",
      "「鼻にかかってモコモコする・言葉が聴き取りづらい」時の処方箋。共鳴を抑えて子音と輪郭をスッキリ立たせます",
      "Remedy for nasal or muffled vocals. Clears honky resonance and boosts consonant articulation.",
      38.0f, 34.0f,  6.0f, 115.0f, 3.0f, 10.0f, 20.0f, 40.0f, 55.0f, 45.0f, 16.0f, 34.0f, 32.0f, false, 26.0f, 70.0f, 68.0f },
    { "悩み対策：サ行や高音がキンキン刺さる（耳障り・歯擦音抑制）", "Fix: Harsh / Piercing Highs",
      "「高音やサ行が耳に刺さって痛い」時の処方箋。動的共振おさえとディエッサーを強化し、丸く聴きやすい高域にします",
      "Remedy for piercing highs and sharp sibilance. Engages deep dynamic resonance smoothing and de-essing.",
      38.0f, 38.0f,  8.0f, 145.0f, 0.0f, 22.0f, 26.0f, 38.0f, 68.0f, 10.0f,  8.0f, 15.0f, 52.0f, false, 64.0f, 42.0f, 46.0f },
    { "悩み対策：声量差が激しい・語尾が消える（自動レベリング）", "Fix: Uneven Volume & Tails",
      "「小さい声が聞こえず、大きい声で割れる・語尾が消える」時の処方箋。自動音量揃えとサステインで安定させます",
      "Remedy for inconsistent volume and vanishing phrase endings. Maximizes auto-rider leveling and tail sustain.",
      48.0f, 46.0f,  6.0f, 150.0f, 2.0f, 16.0f, 44.0f, 75.0f, 36.0f, 26.0f, 14.0f, 28.0f, 30.0f, false, 48.0f, 62.0f, 58.0f },
    { "悩み対策：ブレス・ツヤ・色気が足りない（息づかい＆空気感補強）", "Fix: Add Breath & Gloss",
      "「声がのっぺりして色気や空気感が足りない」時の処方箋。ブレス表現とシルキーな超高域エアーを華やかに足します",
      "Remedy for dry, flat-sounding vocals. Injects intimate breath detail, 3 kHz ring, and silky top-end air.",
      28.0f, 42.0f, 12.0f, 190.0f, 1.8f, 16.0f, 36.0f, 42.0f, 30.0f, 22.0f, 46.0f, 38.0f, 36.0f, false, 50.0f, 52.0f, 86.0f },
    { "悩み対策：高音で声が裏返る・急に細くなる（換声点サポート）", "Fix: High-Note Break Support",
      "「高音になると急に声が細くなる・ひっくり返りそうになる」時の処方箋。高域の密度と伸びを厚くサポートします",
      "Remedy for thin or cracking high notes. Cushions register transitions with dense sustain and harmonic support.",
      44.0f, 44.0f,  7.0f, 140.0f, 2.4f, 20.0f, 40.0f, 60.0f, 46.0f, 24.0f, 18.0f, 40.0f, 34.0f, true,  54.0f, 64.0f, 66.0f },
};

// v5.0.0: マイク方式・設置・部屋環境補正（3カテゴリ 20種）
struct MicPresetDef
{
    const char* name;
    const char* nameJa;
    const char* desc;
    const char* descEn;
    float lc, mud, harsh, air, ds;
    // v5.0 拡張: 近接効果自動補正(prox_amt) / ポップガード(pop_amt)
    float prox, pop;
};
inline constexpr MicPresetDef kMicPresets[] =
{
    // ── 【1. ダイナミックマイク（配信・宅録・ステージ定番 5種）】(0..4) ──
    { "Dynamic: Classic Vocal (58-style)", "ダイナミック：王道ボーカル（58系・近接こもり整理＆高域抜け）",
      "定番ボーカルダイナミック機向け。近接時の200〜300Hzの膨らみを整理し、不足しがちな10kHz以上のエアーを補います",
      "Classic stage/home dynamic mic. Clears 200-300 Hz proximity mud and restores top-end air.",
      100.0f, -3.2f, -1.0f, 3.5f, 28.0f, 52.0f, 45.0f },
    { "Dynamic: Supercardioid Bright (Beta/V7)", "ダイナミック：超単一指向・高抜け（Beta/V7/e945系・硬さケア）",
      "抜けの良い超単一指向性ダイナミック機向け。強い中高域の張り出しの角を少し丸め、滑らかな抜けに整えます",
      "Bright supercardioid dynamic mic. Tames upper-mid bite while keeping crisp vocal projection.",
      100.0f, -2.4f, -2.0f, 1.8f, 32.0f, 48.0f, 42.0f },
    { "Dynamic: Broadcast Warm (7B/MV7-style)", "ダイナミック：ブロードキャスト太め（7B/MV7/PodMic系・高域補給）",
      "配信・実況で人気の太くダークな放送用ダイナミック機向け。豊かな低域を活かしつつ高域の輝きと明瞭度を足します",
      "Warm broadcast dynamic mic. Retains rich low-mid authority while opening up high-frequency sparkle.",
       90.0f, -2.2f, -0.5f, 4.2f, 24.0f, 55.0f, 50.0f },
    { "Dynamic: Large Broadcast Flat (RE20-style)", "ダイナミック：大口径フラット放送局（RE20/PR40系・素直な帯域維持）",
      "近接効果が少なくフラットな大口径ダイナミック機向け。素直な周波数特性を活かし、最小限の整理と艶だけを足します",
      "Large-diaphragm broadcast dynamic with minimal proximity bloat. Gentle polish preserving flat response.",
       85.0f, -1.5f, -1.0f, 2.2f, 26.0f, 25.0f, 38.0f },
    { "Dynamic: Entry & Handheld", "ダイナミック：エントリー＆カラオケマイク（箱鳴り・サ行ケア）",
      "入門用ダイナミックやハンドヘルド機向け。中低域の箱鳴り感と高域のザラつき・ハンドリングノイズをまとめて抑えます",
      "Entry-level handheld dynamic mic. Cleans up boxy low-mids, handling rumble, and grainy highs.",
      110.0f, -3.8f, -2.2f, 2.5f, 34.0f, 55.0f, 52.0f },

    // ── 【2. コンデンサーマイク（歌ってみた・スタジオ定番 5種）】(5..9) ──
    { "Condenser: Entry Bright (2020/NT1-A)", "コンデンサー：入門ブライト・高域華やか（2020/NT1-A/C214系）",
      "宅録・歌ってみた定番の入門コンデンサー機向け。華やかですが硬くなりやすい高域とサ行の刺さりを重点的にケアします",
      "Entry-level bright condenser mic. Smooths out metallic upper-highs and controls sibilance.",
      100.0f, -2.2f, -2.8f, 0.5f, 42.0f, 40.0f, 45.0f },
    { "Condenser: Natural Balanced (2035/NT1)", "コンデンサー：標準ナチュラル均衡（2035/2050/NT1 5th/MK4系）",
      "フラットで癖の少ない標準コンデンサー機向け。軽いローカットと中低域の整理だけで、マイク本来の透明感を活かします",
      "Balanced mid-tier condenser mic. Light corrective EQ preserving natural transient detail.",
       95.0f, -2.0f, -1.5f, 1.2f, 36.0f, 38.0f, 42.0f },
    { "Condenser: Modern Mid-Rich (4040/TLM)", "コンデンサー：上位中域高密度・モダン（4040/TLM102/103/Blue系）",
      "中域の密度が高くモダンな上位コンデンサー機向け。近接時の低域の厚みとサ行のエネルギーを上品にコントロールします",
      "Modern mid-dense condenser mic. Balances proximity warmth and high-shelf sheen for studio polish.",
       95.0f, -2.5f, -1.8f, 1.0f, 38.0f, 42.0f, 44.0f },
    { "Condenser: Studio Flagship / Tube (87-style)", "コンデンサー：スタジオ大口径・真空管＆ビンテージ系（87/414/Tube系）",
      "情報量が多く中域に独特の張りがあるスタジオ大口径・真空管機向け。濃厚な倍音を活かしつつ中低域をタイトに整えます",
      "Large-diaphragm studio flagship or tube condenser. Keeps lush midrange density while tightening low-mids.",
       90.0f, -3.0f, -1.2f, 1.5f, 35.0f, 45.0f, 42.0f },
    { "Condenser: Stage Handheld (87A/e965)", "コンデンサー：ステージ用ハンド型（Beta87/e965/KMS105系）",
      "ライブ・配信で使われるハンドヘルド型コンデンサー機向け。口元の吹かれとサ行を抑え、繊細な高域を綺麗に届けます",
      "Stage handheld condenser mic. Guards against plosives and sibilance while keeping condenser clarity.",
      105.0f, -2.2f, -1.5f, 1.5f, 38.0f, 50.0f, 55.0f },

    // ── 【3. USBマイク・ヘッドセット・VR・部屋環境＆距離対策（10種）】(10..19) ──
    { "USB Condenser Desk Stand (Yeti/QuadCast)", "USBコンデンサー机置き（Yeti/QuadCast/AT2020USB系・机反射除去）",
      "デスクスタンド置きのUSBコンデンサー機向け。机からの反射による中低域の濁りとPC振動ノイズを強力にカットします",
      "Desk-mounted USB condenser mic. Removes desk-reflection comb mud and low-frequency desk vibration.",
      115.0f, -4.0f, -2.2f, 1.5f, 38.0f, 35.0f, 45.0f },
    { "USB/XLR Streamer Mic (Wave/MV7+)", "USB・XLR配信ダイナミック（MV7+/Wave:3/Wave DX系）",
      "現代の配信者向けUSB/XLRマイク用。まとまりのある中域を活かし、BGMの上でも声が埋もれないエアー感を足します",
      "Modern USB/XLR streamer mic. Adds open top-end air and clean low-mid separation over background music.",
       95.0f, -2.2f, -1.0f, 2.8f, 30.0f, 45.0f, 45.0f },
    { "Gaming Headset Mic (Bandwidth Fix)", "ゲーミングヘッドセットマイク（狭帯域・鼻づまり感＆刺さり補正）",
      "ヘッドセットブームマイク向け。特有の鼻づまり感と中高域の硬いピークを削り、自然な高域の広がりを補います",
      "Gaming headset boom mic. Reduces boxy nasal peaks, harsh 3-4 kHz glare, and restores missing air.",
      125.0f, -3.5f, -3.2f, 3.2f, 34.0f, 60.0f, 65.0f },
    { "VR Headset Built-in Mic (Quest/Index)", "VRヘッドセット内蔵マイク（Quest 3/Index/VIVE/PICO系・温かみ補給）",
      "VRゴーグル内蔵マイクでのVRSNS・VR歌枠向け。鼻元の吹かれと軽い音質を補正し、自然で温かみのある声に整えます",
      "VR headset built-in mic. Tames nasal plosives and thin midrange for fuller VRChat/virtual singing.",
      115.0f, -2.8f, -2.5f, 2.8f, 36.0f, 50.0f, 68.0f },
    { "Lavalier / Earbud Mic (Chest Mud Cut)", "ピンマイク・ラベリア・イヤホンマイク（胸元こもり＆衣擦れカット）",
      "胸元のピンマイクやイヤホンマイク向け。胸腔の反響による強いこもりと衣擦れ低音をカットし、明瞭な声を引き出します",
      "Lavalier or earbud mic. Cuts chest-cavity mud and clothing rustle while restoring upper-frequency clarity.",
      135.0f, -4.8f, -1.5f, 3.8f, 30.0f, 30.0f, 40.0f },
    { "Room Fix: Untreated / Echoey Room", "環境対策：反響しやすい部屋・防音なし（部屋鳴り・お風呂場感除去）",
      "吸音材のない一般的なお部屋での収録・配信向け。壁や天井の反響が溜まりやすい300〜500Hzの箱鳴りをスッキリ除去します",
      "For untreated, reflective rooms. Scoops out 300-500 Hz room boxiness and tightens low-end decay.",
      115.0f, -4.5f, -2.0f, 2.0f, 35.0f, 45.0f, 45.0f },
    { "Room Fix: Dead Booth / Closet", "環境対策：吸音ブース・デッドな防音室（詰まり解消＆エアー開放）",
      "狭い防音室やクローゼット・吸音フォーム多めの環境向け。吸音されすぎた高域の空気感を開放し、低域の詰まりを抜きます",
      "For heavily damped vocal booths or closets. Opens up absorbed top-end air and relieves low-mid congestion.",
      100.0f, -3.2f, -0.8f, 4.0f, 28.0f, 52.0f, 42.0f },
    { "Distance: Ultra-Close On-Mic (1-5cm)", "距離対策：オンマイク超近接（口元1〜5cm・近接低音膨張を強力抑制）",
      "マイクに唇が触れるほど近づいて歌う・囁く方向け。近接効果による低音のボワつきとポップノイズを強力に抑え込みます",
      "For ultra-close mic technique (1-5 cm). Maximizes proximity-effect bass control and plosive protection.",
      120.0f, -4.2f, -1.5f, 2.2f, 34.0f, 80.0f, 75.0f },
    { "Distance: Far Off-Mic (20-40cm)", "距離対策：オフマイク遠め（20〜40cm離し・声芯と厚みを補強）",
      "マイクを画面外や少し離れた位置に置く方向け。距離で薄くなった中低域の厚みと輪郭を補い、部屋鳴りを整理します",
      "For off-axis or distant mic placement (20-40 cm). Restores vocal body and focus while trimming room mud.",
       90.0f, -1.2f, -1.5f, 2.5f, 30.0f, 15.0f, 20.0f },
    { "Flat / No Mic Compensation", "マイク補正なし（フラット・現在のマイク特性をそのまま通す）",
      "マイク固有の補正EQを行わずフラットに通します。すでに外部機材で音作り済みの場合や素の音を確認したい時に",
      "Neutral / flat mic response. Use when your hardware chain is already tailored or for pure reference.",
       80.0f,  0.0f,  0.0f, 0.0f, 25.0f, 20.0f, 25.0f },
};

// v5.0.0: 音楽ジャンル・サブジャンル・曲調レシピ（男女共通 4カテゴリ 20種）
struct EqPresetDef
{
    const char* name;
    const char* nameEn;
    const char* desc;
    const char* descEn;
    float lc, mud, harsh, pres, air, drv;
    // v5.0 拡張: 曲調・オケ編成に合わせたArgon 4軸 & 子音・息・艶・伸び連携
    float velvet, focus, silk, halo;
    float cons, br, ring, sus;
};
inline constexpr EqPresetDef kEqPresets[] =
{
    // ── 【1. アニソン・ボカロ・J-POP（編成・サブジャンル別 7種）】(0..6) ──
    { "アニソン：疾走ギターロック（歪みギターの壁を突き抜ける主役声）", "Anison: High-Speed Guitar Rock",
      "男女問わず激しいバンド系アニソン向け。歪みギターと重なる中低域を整理し、2〜4kHzの輪郭と高域の突破力を最大化します",
      "For fast guitar-rock anime songs (any gender). Cuts through dense distorted guitars with bold 3 kHz ring and focus.",
      110.0f, -3.8f, -1.5f, 3.5f, 3.0f, 18.0f, 44.0f, 74.0f, 64.0f, 34.0f, 34.0f, 14.0f, 38.0f, 24.0f },
    { "アニソン：シンフォニック・劇伴・壮大系（ストリングスに映える伸びと艶）", "Anison: Symphonic & Epic Strings",
      "ストリングスやピアノが響く壮大な劇伴・ファンタジー系アニソン向け。豊かな倍音の艶と気品あるホール感を両立します",
      "For symphonic, orchestral anime themes. Blends silky upper harmonics, vocal ring, and majestic spatial bloom.",
       95.0f, -2.8f, -2.0f, 2.2f, 3.5f, 16.0f, 54.0f, 58.0f, 76.0f, 58.0f, 22.0f, 20.0f, 36.0f, 36.0f },
    { "アニソン：王道ヒーロー＆アッパーブラス（ホーンとバンドに映える明快さ）", "Anison: Heroic Brass & Upbeat",
      "ブラスやシンセが鳴り響く明るい王道OPテーマ向け。中域の押し出しと太い声芯で、元気よく最前列に飛び出す音像にします",
      "For upbeat, brass-driven heroic anime openings. Punchy midrange projection and vibrant, front-row energy.",
      105.0f, -3.2f, -1.2f, 3.2f, 2.8f, 20.0f, 48.0f, 70.0f, 62.0f, 36.0f, 30.0f, 12.0f, 34.0f, 26.0f },
    { "アニソン・アイドル：キラキラ高彩度・キャラソン＆電波（弾ける子音と吐息）", "Anison / Idol: Sparkling Cute & Denpa",
      "アイドル曲・キャラソン・ポップな電波ソング向け。子音の弾けるリズム感とキラキラした超高域の彩度を華やかに盛ります",
      "For idol pop, character songs, and hyper-cute tracks. Sparkling top-end air and bouncy consonant articulation.",
      115.0f, -3.5f, -2.2f, 3.0f, 4.2f, 10.0f, 32.0f, 68.0f, 84.0f, 42.0f, 38.0f, 28.0f, 32.0f, 22.0f },
    { "ボカロ・ネット系：高速エレクトロ＆高密度シンセ（早口でも歌詞が埋もれない）", "Vocaloid / Net: Hyper Electro & Fast Lyric",
      "音数の多いボカロ曲・エレクトロ歌ってみた向け。早口でも歌詞が一文字残らず聴き取れる子音強調とモダンな高域設計です",
      "For dense Vocaloid & electro covers. Ultra-clear consonant definition and modern sheen that never gets buried.",
      120.0f, -4.2f, -2.0f, 3.5f, 3.8f, 14.0f, 34.0f, 76.0f, 74.0f, 32.0f, 44.0f, 20.0f, 34.0f, 20.0f },
    { "ボカロ・ネット系：ダーク＆エモーショナル（艶やかな中低域と鋭いエッジ）", "Vocaloid / Net: Dark & Emotional",
      "ダークな世界観のボカロ・ネット発楽曲向け。吐息の冷たい空気感と、サビで感情が爆発するエッジの効いた倍音を両立します",
      "For dark, emotional net-music tracks. Combines intimate breath detail with edgy harmonic saturation.",
      105.0f, -3.2f, -1.8f, 2.6f, 3.2f, 24.0f, 58.0f, 66.0f, 68.0f, 38.0f, 32.0f, 26.0f, 32.0f, 26.0f },
    { "J-POP：王道ブライト＆モダンポップス（どんな編成でも聴きやすい主役感）", "J-Pop: Modern Bright Chart",
      "現代J-POPのスタンダードな完成形。不要なこもりだけを綺麗に抜き、明るさと温かみのバランスが取れた王道の主役ボーカルに",
      "Modern J-Pop chart standard. Clean low-mids, polished presence, and effortless radio-ready balance.",
      105.0f, -3.2f, -1.5f, 2.5f, 3.0f, 15.0f, 44.0f, 62.0f, 68.0f, 40.0f, 26.0f, 18.0f, 30.0f, 26.0f },

    // ── 【2. ロック・バンド・ラウド（3種）】(7..9) ──
    { "ロック：ギターロック＆オルタナ（中域の太い芯と自然なドライブ感）", "Rock: Guitar Rock & Alternative",
      "バンドサウンドの真ん中に立つギターロック向け。高域をチャラつかせず、中域の太い肉厚感とアナログな倍音で聴かせます",
      "For guitar rock and alternative bands. Thick midrange punch and warm analog drive that holds center stage.",
      100.0f, -2.8f, -1.2f, 3.0f, 2.0f, 26.0f, 58.0f, 68.0f, 50.0f, 28.0f, 28.0f, 12.0f, 32.0f, 22.0f },
    { "ロック：ハードロック・ラウド・V系（シャウトとハイトーンの突破力）", "Rock: Hard Rock / Loud / Visual-Kei",
      "重厚なリフやツーバスが鳴るラウド・V系・メタル向け。シャウトやハイトーンが分厚い音壁を貫くアグレッシブな設定です",
      "For hard rock, metal, loud rock, and Visual-Kei. Aggressive presence and harmonic grit that pierces heavy walls.",
      110.0f, -3.5f, -1.8f, 3.8f, 2.5f, 32.0f, 62.0f, 76.0f, 54.0f, 30.0f, 34.0f, 10.0f, 38.0f, 20.0f },
    { "ロック：パンク・メロコア・ライブハウス（ドライで荒々しい生ライブ感）", "Rock: Punk / Live House Raw",
      "パンク・メロコア・青春ロック向け。余計な化粧を抑えて言葉の勢いと生々しい熱量をダイレクトにぶつけるライブ仕様です",
      "For punk, melodic hardcore, and raw live-house vibes. Direct, dry, punchy vocal energy with immediate attack.",
      105.0f, -2.5f, -1.0f, 3.2f, 1.5f, 28.0f, 54.0f, 72.0f, 44.0f, 18.0f, 32.0f,  8.0f, 28.0f, 16.0f },

    // ── 【3. バラード・アコースティック・R&B・シティポップ（6種）】(10..15) ──
    { "バラード：ピアノ＆ストリングス王道（吐息のニュアンスと豊かなホール余韻）", "Ballad: Piano & Strings Grand",
      "しっとり聴かせる王道バラード向け。耳あたりの硬さを丁寧に取り除き、ブレスの温もりと美しい余韻で空間を満たします",
      "For grand piano & string ballads. Softens upper-mid harshness while enveloping every breath in lush hall bloom.",
       90.0f, -2.5f, -2.5f, 1.2f, 2.8f, 20.0f, 56.0f, 50.0f, 72.0f, 62.0f, 18.0f, 26.0f, 28.0f, 38.0f },
    { "アコースティック：弾き語り・カフェライブ（生楽器と自然に溶け合う温かい声）", "Acoustic: Unplugged & Cafe Live",
      "アコギやピアノの弾き語り・アンプラグド編成向け。加工感を抑えたナチュラルな肌触りで、すぐ目の前で歌う親密さを作ります",
      "For acoustic guitar/piano singer-songwriters. Natural, uncolored warmth that blends organically with wood instruments.",
       88.0f, -2.2f, -2.0f, 1.5f, 2.2f, 18.0f, 52.0f, 54.0f, 60.0f, 36.0f, 20.0f, 20.0f, 22.0f, 28.0f },
    { "R&B・ネオソウル・チルグルーヴ（滑らかなシルキー高域と太い胸声）", "R&B / Neo-Soul / Smooth Groove",
      "R&Bやソウル、グルーヴ重視の楽曲向け。太く艶やかなチェストの響きと、ベルベットのように滑らかな高域を両立します",
      "For R&B, Neo-Soul, and groove tracks. Rich chest warmth paired with velvety-smooth, non-fatiguing top-end silk.",
       85.0f, -2.4f, -2.2f, 1.5f, 3.2f, 24.0f, 66.0f, 56.0f, 74.0f, 44.0f, 22.0f, 24.0f, 26.0f, 32.0f },
    { "シティポップ・80s/90sレトロモダン（甘くメロウな倍音と軽やかな広がり）", "City Pop / 80s-90s Retro Modern",
      "都会的なシティポップやレトロモダンな楽曲向け。甘くメロウな真空管風の倍音と、爽やかに抜けるエアーをブレンドします",
      "For City Pop and retro-modern grooves. Sweet analog-style harmonic warmth with breezy, sophisticated stereo air.",
       92.0f, -2.4f, -1.8f, 1.8f, 3.0f, 22.0f, 58.0f, 56.0f, 70.0f, 48.0f, 22.0f, 20.0f, 26.0f, 28.0f },
    { "ジャズ・ボサノヴァ・大人スロー（息の温度感と深いベルベット倍音）", "Jazz / Bossa Nova / Velvet Lounge",
      "ジャズやボサノヴァ、夜の大人なスロー曲向け。高域の派手さを抑え、息の温度感と深いベルベット倍音でしっとりと包みます",
      "For Jazz, Bossa Nova, and late-night lounge vocals. Deep velvet harmonics and intimate breath warmth.",
       80.0f, -1.8f, -2.8f, 0.8f, 1.8f, 30.0f, 74.0f, 48.0f, 56.0f, 42.0f, 16.0f, 24.0f, 20.0f, 30.0f },
    { "Lo-Fi・チル・ベッドルームポップ（耳元で囁くような親密さと丸い高域）", "Lo-Fi / Chill / Bedroom Intimate",
      "Lo-Fiビートやチル系ベッドルームポップ向け。刺さる帯域を大きく丸め、すぐ隣で歌っているような柔らかい親密さを作ります",
      "For Lo-Fi beats and bedroom pop. Gently rolled-off harshness with warm, close-up, pillowy vocal intimacy.",
      100.0f, -2.5f, -3.5f, 1.0f, 2.0f, 26.0f, 68.0f, 46.0f, 58.0f, 34.0f, 18.0f, 28.0f, 18.0f, 30.0f },

    // ── 【4. ラップ・伝統歌唱・配信トーク・ASMR（4種）】(16..19) ──
    { "ヒップホップ・ラップ・メロディックフロウ（言葉のパンチと太い最前列定位）", "Hip-Hop / Rap / Melodic Flow",
      "ヒップホップ・ラップ・リズム重視の歌唱向け。808ベースと被らないよう超低域を切りつつ、言葉のパンチを最前列に出します",
      "For Hip-Hop, Rap, and melodic flows. Tight low-cut clears 808 sub-bass while locking punchy consonants up front.",
      115.0f, -3.8f, -1.5f, 3.2f, 2.8f, 22.0f, 52.0f, 78.0f, 58.0f, 20.0f, 46.0f, 14.0f, 30.0f, 16.0f },
    { "演歌・民謡・和風楽曲（こぶし・ビブラートの抑揚と深いホール残響）", "Enka / Traditional Japanese Vocal",
      "演歌・民謡・和楽器編成の楽曲向け。男女問わず、こぶしやビブラートの力強い芯と艶やかなホール残響を格調高く響かせます",
      "For Enka, folk, and traditional Japanese arrangements (any gender). Strong vocal core and noble hall resonance.",
       90.0f, -2.5f, -1.5f, 2.5f, 2.0f, 22.0f, 58.0f, 62.0f, 58.0f, 56.0f, 24.0f, 14.0f, 38.0f, 34.0f },
    { "雑談配信・ラジオ・ゲーム実況（BGMの上でも長時間聴き疲れしない明瞭トーク）", "Stream Talk / Radio / Gaming",
      "雑談・ラジオ・ゲーム実況向け。部屋のこもりと耳障りな帯域を両方カットし、何時間聴いても疲れないクリアな話し声にします",
      "For chatting streams, radio, and gaming commentary. Crisp, fatigue-free speech intelligibility over BGM.",
      120.0f, -4.0f, -2.0f, 2.5f, 1.8f, 14.0f, 42.0f, 68.0f, 52.0f, 12.0f, 34.0f, 10.0f, 22.0f, 18.0f },
    { "ASMR・ささやき・朗読・寝かしつけ（耳元の吐息と温かみを極限まで近づける）", "ASMR / Whisper / Bedtime Reading",
      "ASMR・ささやき・朗読・睡眠導入配信向け。低域の温もりと耳元の微細な吐息を残し、刺さりだけを優しく取り除きます",
      "For ASMR, whispering, and bedtime reading. Preserves close-up warmth and delicate breath while removing harsh glare.",
       70.0f, -1.2f, -3.2f, 0.5f, 3.8f, 14.0f, 60.0f, 42.0f, 84.0f, 26.0f, 16.0f, 42.0f, 14.0f, 36.0f },
};

inline constexpr int kNumVoicePresets = (int) (sizeof (kVoicePresets) / sizeof (kVoicePresets[0]));
inline constexpr int kNumMicPresets   = (int) (sizeof (kMicPresets)   / sizeof (kMicPresets[0]));
inline constexpr int kNumEqPresets    = (int) (sizeof (kEqPresets)    / sizeof (kEqPresets[0]));

// ---- v1.4.0 / v5.1.0 Voice Changer & Character-Voice presets (pitch/formant + robot + megaphone) ----
struct CharPresetDef
{
    const char* name;
    const char* nameEn;
    const char* desc;
    float roboFreq, roboMix;
    int   megaType;
    float megaAmt;
    bool  vcOn    = false;
    float vcPitch = 0.0f;
    float vcForm  = 0.0f;
    float vcMix   = 100.0f;
};
static const CharPresetDef kCharPresets[] = {
    { "もどす (ボイチェン・演出 解除)", "Off (Reset All)",
      "ボイスチェンジャー（ピッチ・声質変換）とロボ声・メガホン演出をすべてオフ（原音）に戻します。",
      80.0f, 0.0f, 0, 0.0f, false, 0.0f, 0.0f, 100.0f },
    { "🎀 カワボ・美少女 (男→女・高め愛され声)", "Kawaii Girl (M-to-F)",
      "男性地声や低めの声を、自然で可愛らしい高めの美少女ボイスへ変換します（ピッチ+5.0半音・声質フォルマント+3.2半音）。バ美肉・VTuber配信・VRChatに最適。",
      80.0f, 0.0f, 0, 0.0f, true, +5.0f, +3.2f, 100.0f },
    { "🌸 ナチュラル女性・お姉さん (自然で艶やかな高め声)", "Natural Female (Soft Sister)",
      "やりすぎ感のない自然な女性声・綺麗なお姉さん声へ変換します（ピッチ+3.0半音・声質フォルマント+2.0半音）。歌枠でも雑談でも違和感がありません。",
      80.0f, 0.0f, 0, 0.0f, true, +3.0f, +2.0f, 100.0f },
    { "👑 イケボ・低音男子 (女→男・深みのある低音)", "Deep Male (F-to-M)",
      "女性声や高めの声を、胸の響きが太く落ち着いたイケボ・低音男性ボイスへ変換します（ピッチ-5.0半音・声質フォルマント-2.8半音）。",
      80.0f, 0.0f, 0, 0.0f, true, -5.0f, -2.8f, 100.0f },
    { "🎧 少年・中性ボイス (爽やかボーイッシュ)", "Boyish / Neutral Voice",
      "爽やかで中性的な少年・ボーイッシュ声へ変換します（ピッチ-2.5半音・声質フォルマント-1.5半音）。少年役やカッコいい歌ってみたに最適。",
      80.0f, 0.0f, 0, 0.0f, true, -2.5f, -1.5f, 100.0f },
    { "🧚 小動物・マスコット妖精 (ハイトーン・デフォルメ)", "Mascot / Fairy Voice",
      "アニメのマスコットや妖精のような愛くるしいハイトーン声へ変換します（ピッチ+8.0半音・声質フォルマント+4.8半音）。",
      80.0f, 0.0f, 0, 0.0f, true, +8.0f, +4.8f, 100.0f },
    { "😈 魔王・ダークボス (重低音ボイス変換＋威圧感)", "Dark Boss (Giant Low Voice)",
      "地の底から響くような超重低音モンスター・魔王ボイスへ変換します（ピッチ-7.5半音・声質フォルマント-4.2半音）。",
      80.0f, 0.0f, 0, 0.0f, true, -7.5f, -4.2f, 100.0f },
    { "🤖 ロボット (定番ロボボイス)", "Robot",
      "定番のロボット変調ボイス。コメント読みや自己紹介・SF演出のネタに。",
      60.0f, 85.0f, 1, 25.0f, false, 0.0f, 0.0f, 100.0f },
    { "👽 宇宙人 (金属変調エイリアン)", "Alien",
      "金属的な高い変調の宇宙人ボイス。SF演出やボケ担当に。",
      320.0f, 70.0f, 0, 0.0f, true, +2.0f, +3.5f, 100.0f },
    { "📞 電話のむこう (電話越し・無線風)", "Far-away Phone",
      "電話越し・トランシーバー無線風の声。回想シーンや実況ごっこ・歌のイントロ演出に。",
      80.0f, 0.0f, 1, 70.0f, false, 0.0f, 0.0f, 100.0f },
    { "📢 悪役メガホン (低音変調＋拡声器)", "Villain Voice",
      "低い変調＋拡声器の威圧ボイス。ラスボス登場シーンやアジ演説風に。",
      35.0f, 60.0f, 0, 30.0f, true, -3.5f, -2.0f, 100.0f },
    { "📻 こわれたスピーカー (Lo-Fiレトロ割れ)", "Broken Speaker",
      "ローファイで割れたレトロスピーカー音。ボカロ曲の間奏演出フレーズ再現に。",
      80.0f, 0.0f, 2, 85.0f, false, 0.0f, 0.0f, 100.0f },
};
static constexpr int kNumCharPresets = (int) (sizeof (kCharPresets) / sizeof (kCharPresets[0]));
} // namespace gzzio
