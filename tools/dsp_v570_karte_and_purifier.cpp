#include "TestPaths.h"
// =============================================================================
//  dsp_v570_karte_and_purifier.cpp — VocalGzzio v5.7.0 統合検証スイート
//  [1] うた自動: 無音・ブレス休止免疫（50%無音混入でもメイクアップ過剰ブーストしない）
//  [2] うた自動: 12帯域音声学＆12タイプJ-Pop声質カルテ精密分類＋補助モジュール処方
//  [3] AIノイズ抑制: 6種ノイズパターン指紋識別（静音 / 空調ファン / ハム / サー音 / 物音 / 複合）
//  [4] AIノイズ抑制: ゼロ遅延ピッチ同期ハーモニック・コム声抽出（倍音損失 <0.15dB ＆ 倍音間雑音 >3.5dB カット）
//      ＋ ピアニッシモ語尾保護ブリッジ ＆ Denoise無稼働Smart Sleep
//  [5] 新機能: J-Pop完パケ艶＆喉締めオートリリーフ（1.08kHzイキミ＆3.88kHz高音耳痛の動的緩和）
//  [6] 統合Prism Mode 4 (Grammy Unified Console) ＆ 状態保存/復元ラウンドトリップ
// =============================================================================

#include "PluginProcessor.h"
#include "VocalAiKarteAndPurifier.h"
#include <cstdio>
#include <cmath>
#include <vector>

static int gFail = 0;
#define CHECK(cond, ...) do { \
    if (cond) { std::printf("  PASS: " __VA_ARGS__); std::printf("\n"); } \
    else      { std::printf("  FAIL: " __VA_ARGS__); std::printf("\n"); ++gFail; } } while (0)

static float getP (VocalGzzioProcessor& p, const char* id)
{
    return p.apvts.getRawParameterValue (id)->load();
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;

    auto autosave = gz::dataDirectory().getChildFile ("autosave.xml");
    auto backup   = autosave.getSiblingFile ("autosave.bak_v570");
    autosave.getParentDirectory().createDirectory();
    if (autosave.existsAsFile()) autosave.moveFileTo (backup);
    struct Restore {
        juce::File a, b;
        ~Restore() { a.deleteFile(); if (b.existsAsFile()) b.moveFileTo (a); }
    } restore { autosave, backup };
    autosave.deleteFile();

    std::printf ("=== VocalGzzio v5.7.0 Deep Reconstruction & AI Purifier Audit ===\n\n");

    const double sr = 48000.0;
    const int blk = 128;

    // -------------------------------------------------------------------------
    // [1] 無音・ブレス休止免疫テスト: 連続歌唱 vs 50%休符入り歌唱で makeup が暴走しないこと
    // -------------------------------------------------------------------------
    std::printf ("[1] うた自動：無音・息継ぎ休止免疫（アクティブ歌唱フレーム解析）\n");
    auto runSingMakeup = [&] (bool withBreathPauses, int& outArch) -> float
    {
        VocalGzzioProcessor p;
        p.prepareToPlay (sr, blk);
        p.requestAutoSetup (1);
        juce::AudioBuffer<float> buf (2, blk);
        juce::MidiBuffer midi;
        double ph = 0.0;
        const int total = (int) (8.3 * sr);
        for (int pos = 0; pos < total; pos += blk)
        {
            const double sec = (double) pos / sr;
            // 0.8秒歌って0.8秒休む（50%無音休符）
            const bool singingNow = (! withBreathPauses) || (std::fmod (sec, 1.6) < 0.8);
            auto* L = buf.getWritePointer (0);
            auto* R = buf.getWritePointer (1);
            for (int n = 0; n < blk; ++n)
            {
                float s = 0.0f;
                if (singingNow)
                {
                    double acc = 0.0;
                    for (int h = 1; h <= 18; ++h)
                        acc += std::pow (10.0, -5.5 * std::log2 ((double) h) / 20.0) * std::sin (ph * h);
                    s = (float) (acc * 0.09);
                }
                ph += 2.0 * 3.14159265358979323846 * 220.0 / sr;
                L[n] = s;
                R[n] = s;
            }
            p.processBlock (buf, midi);
        }
        p.applyAutoSetup();
        outArch = p.getVocalKarteArchetype();
        return getP (p, "makeup");
    };

    int archCont = 0, archPause = 0;
    const float mkCont  = runSingMakeup (false, archCont);
    const float mkPause = runSingMakeup (true,  archPause);
    const float mkDiff  = std::abs (mkCont - mkPause);
    CHECK (mkDiff < 1.5f,
           "50%%無音休符混入でもメイクアップゲインが過大化しない (連続 %.2f dB vs 休符混入 %.2f dB, 差 %.2f dB)",
           mkCont, mkPause, mkDiff);
    CHECK (archCont > 0 && archPause > 0,
           "連続歌唱・休符混入歌唱ともに12タイプ声質カルテが有効診断される (arch=%d, %d)", archCont, archPause);

    // -------------------------------------------------------------------------
    // [2] 12タイプ J-Pop 声質カルテ精密分類テスト
    // -------------------------------------------------------------------------
    std::printf ("\n[2] 12タイプ J-Pop 声質カルテ分類（ウィスパー / ハイトーン / 低音イケボ / 箱鳴り / ベルティング）\n");
    {
        // (a) 宅録ボックス鳴り (400Hz 集中)
        gz::v570::JPopVocalKarteAnalyzer kaBox;
        kaBox.prepare (sr);
        std::vector<float> vBuf (512, 0.0f);
        double ph = 0.0;
        for (int bIdx = 0; bIdx < 120; ++bIdx)
        {
            for (int i = 0; i < 512; ++i)
            {
                vBuf[(size_t) i] = 0.10f * (float) std::sin (ph) + 0.015f * (float) std::sin (ph * 2.0);
                ph += 2.0 * 3.14159265358979323846 * 395.0 / sr;
            }
            kaBox.pushBlock (vBuf.data(), nullptr, 512, 198.0f, 0.85f, 1.0e-5f);
        }
        const auto repBox = kaBox.buildReport (-20.0f, 2.5f, false);
        CHECK (repBox.archetype == gz::v570::JPopVocalKarteAnalyzer::HomeBoothBoxyFix && repBox.recRoomDereverb,
               "400Hz箱鳴り突出声を HomeBoothBoxyFix と診断し RoomDereverb を自動処方 (arch=%d)", repBox.archetype);

        // (b) 透明ウィスパー・ASMR系 (小声 + 10kHzブレス豊富 + H1>H2)
        gz::v570::JPopVocalKarteAnalyzer kaWhisp;
        kaWhisp.prepare (sr);
        unsigned rng = 12345u;
        float hp1 = 0.0f;
        ph = 0.0;
        for (int bIdx = 0; bIdx < 120; ++bIdx)
        {
            for (int i = 0; i < 512; ++i)
            {
                rng = rng * 1664525u + 1013904223u;
                const float wn = ((float) ((int) (rng >> 9) - 4194304) / 4194304.0f);
                const float hf = wn - hp1;
                hp1 = wn;
                vBuf[(size_t) i] = 0.022f * (float) std::sin (ph) + 0.016f * hf;
                ph += 2.0 * 3.14159265358979323846 * 240.0 / sr;
            }
            kaWhisp.pushBlock (vBuf.data(), nullptr, 512, 240.0f, 0.55f, 1.0e-5f);
        }
        const auto repWhisp = kaWhisp.buildReport (-28.0f, 2.5f, false);
        CHECK (repWhisp.archetype == gz::v570::JPopVocalKarteAnalyzer::WhisperTransparent && repWhisp.recUpwardAir,
               "吐息豊富な小声を WhisperTransparent と診断し UpwardAir を自動処方 (arch=%d)", repWhisp.archetype);

        // (c) ハイトーン・カワボ系 (高F0=330Hz + 中高域クリア)
        gz::v570::JPopVocalKarteAnalyzer kaHigh;
        kaHigh.prepare (sr);
        ph = 0.0;
        for (int bIdx = 0; bIdx < 120; ++bIdx)
        {
            for (int i = 0; i < 512; ++i)
            {
                double s = 0.0;
                for (int h = 1; h <= 12; ++h)
                    s += (1.0 / (double) h) * std::sin (ph * h);
                vBuf[(size_t) i] = (float) (s * 0.055);
                ph += 2.0 * 3.14159265358979323846 * 330.0 / sr;
            }
            kaHigh.pushBlock (vBuf.data(), nullptr, 512, 330.0f, 0.92f, 1.0e-5f);
        }
        const auto repHigh = kaHigh.buildReport (-21.0f, 2.8f, false);
        CHECK (repHigh.archetype == gz::v570::JPopVocalKarteAnalyzer::HighToneKawaii && repHigh.recPitchSweet,
               "高音域クリア声を HighToneKawaii と診断し PitchSweetSpot を自動処方 (arch=%d)", repHigh.archetype);

        // (d) ウォーム低音・イケボ系 (低F0=115Hz + チェスト豊か)
        gz::v570::JPopVocalKarteAnalyzer kaLow;
        kaLow.prepare (sr);
        ph = 0.0;
        for (int bIdx = 0; bIdx < 120; ++bIdx)
        {
            for (int i = 0; i < 512; ++i)
            {
                const double s = 1.0 * std::sin (ph) + 0.7 * std::sin (ph * 2.0) + 0.25 * std::sin (ph * 5.0);
                vBuf[(size_t) i] = (float) (s * 0.065);
                ph += 2.0 * 3.14159265358979323846 * 115.0 / sr;
            }
            kaLow.pushBlock (vBuf.data(), nullptr, 512, 115.0f, 0.90f, 1.0e-5f);
        }
        const auto repLow = kaLow.buildReport (-21.0f, 2.5f, false);
        CHECK (repLow.archetype == gz::v570::JPopVocalKarteAnalyzer::WarmLowBaritone,
               "低F0チェスト声を WarmLowBaritone と診断 (arch=%d)", repLow.archetype);
    }

    // -------------------------------------------------------------------------
    // [3] AI 6種ノイズパターン指紋識別テスト
    // -------------------------------------------------------------------------
    std::printf ("\n[3] AI 6種ノイズパターン指紋識別（静音 / 空調ファン / 電源ハム / サー音ヒス / 複合騒音）\n");
    {
        gz::v570::AiVoicePurifierAndNoiseClassifier clf;
        clf.prepare (sr);

        const float flQuiet[4] = { 0.00005f, 0.00004f, 0.00004f, 0.00003f };
        const float flHum[4]   = { 0.00400f, 0.00050f, 0.00020f, 0.00015f };
        const float flFan[4]   = { 0.00220f, 0.00200f, 0.00050f, 0.00030f };
        const float flHiss[4]  = { 0.00030f, 0.00040f, 0.00120f, 0.00200f };
        const float flRoom[4]  = { 0.00120f, 0.00110f, 0.00100f, 0.00090f };

        CHECK (clf.classifyProfile (flQuiet) == gz::v570::AiVoicePurifierAndNoiseClassifier::StudioQuiet,
               "極静音フロアを StudioQuiet (0) と識別");
        CHECK (clf.classifyProfile (flHum) == gz::v570::AiVoicePurifierAndNoiseClassifier::PowerHum
               && clf.bandWeight (0) > clf.bandWeight (3),
               "低域集中フロアを PowerHum (2) と識別し低域重みを強化 (w0=%.2f > w3=%.2f)", clf.bandWeight (0), clf.bandWeight (3));
        CHECK (clf.classifyProfile (flFan) == gz::v570::AiVoicePurifierAndNoiseClassifier::HvacFan
               && clf.bandWeight (1) > clf.bandWeight (3),
               "空調・PCファンフロアを HvacFan (1) と識別し高域ブレスを保護 (w1=%.2f > w3=%.2f)", clf.bandWeight (1), clf.bandWeight (3));
        CHECK (clf.classifyProfile (flHiss) == gz::v570::AiVoicePurifierAndNoiseClassifier::PreampHiss
               && clf.bandWeight (3) > clf.bandWeight (0),
               "高域サー音フロアを PreampHiss (3) と識別しチェスト温かみを無傷保護 (w3=%.2f > w0=%.2f)", clf.bandWeight (3), clf.bandWeight (0));
        CHECK (clf.classifyProfile (flRoom) == gz::v570::AiVoicePurifierAndNoiseClassifier::ComplexRoom,
               "広帯域フロアを ComplexRoom (5) と識別");
    }

    // -------------------------------------------------------------------------
    // [4] ピッチ同期ハーモニック・コム声抽出テスト（うたを殺さず倍音間雑音だけカット！）
    // -------------------------------------------------------------------------
    std::printf ("\n[4] ゼロ遅延ピッチ同期ハーモニック声抽出（歌唱中の声無傷 ＆ 倍音間ノイズ減衰）\n");
    {
        gz::v570::AiVoicePurifierAndNoiseClassifier extHarm, extInter;
        extHarm.prepare (sr);
        extInter.prepare (sr);

        // 非整数周期 (237.3 Hz -> 48000 / 237.3 = 202.2756 samples) でもラグランジュ3次補間で無損失を検証
        const float f0Frac = 237.3f;
        const float fl[4]  = { 0.002f, 0.0025f, 0.0025f, 0.0015f };
        const float env[4] = { 0.030f, 0.035f,  0.035f,  0.015f  };

        std::vector<float> b1 (blk, 0.0f), b2 (blk, 0.0f), b3 (blk, 0.0f);
        double inHarmSq = 0.0, outHarmSq = 0.0;
        double inInterSq = 0.0, outInterSq = 0.0;

        // (A) 純粋な声の倍音 (k * f0Frac, k=2..12) -> 損失 < 0.15 dB であること！
        double ph = 0.0;
        for (int bIdx = 0; bIdx < 80; ++bIdx)
        {
            for (int n = 0; n < blk; ++n)
            {
                float s = 0.0f;
                for (int k = 6; k <= 14; ++k)
                    s += (0.02f / (float) (k - 4)) * (float) std::sin (ph * (double) k);
                ph += 2.0 * 3.14159265358979323846 * (double) f0Frac / sr;
                b1[(size_t) n] = 0.0f;
                b2[(size_t) n] = s;
                b3[(size_t) n] = 0.0f;
                if (bIdx >= 20) inHarmSq += (double) s * (double) s;
            }
            extHarm.processHarmonicVoiceExtractor (b1.data(), nullptr, b2.data(), nullptr, b3.data(), nullptr,
                                                   blk, 1, f0Frac, 0.92f, 1.0f, fl, env, false);
            if (bIdx >= 20)
                for (int n = 0; n < blk; ++n)
                    outHarmSq += (double) b2[(size_t) n] * (double) b2[(size_t) n];
        }
        const float harmLossDb = (float) std::abs (10.0 * std::log10 ((outHarmSq + 1e-15) / (inHarmSq + 1e-15)));

        // (B) 倍音と倍音の隙間に居座る非調波ノイズ ((k + 0.5) * f0Frac) -> 歌唱中でも減衰すること！
        ph = 0.0;
        for (int bIdx = 0; bIdx < 80; ++bIdx)
        {
            for (int n = 0; n < blk; ++n)
            {
                float nz = 0.0f;
                for (int k = 6; k <= 14; ++k)
                    nz += (0.02f / (float) (k - 4)) * (float) std::sin (ph * ((double) k + 0.5));
                ph += 2.0 * 3.14159265358979323846 * (double) f0Frac / sr;
                b1[(size_t) n] = 0.0f;
                b2[(size_t) n] = nz;
                b3[(size_t) n] = 0.0f;
                if (bIdx >= 20) inInterSq += (double) nz * (double) nz;
            }
            extInter.processHarmonicVoiceExtractor (b1.data(), nullptr, b2.data(), nullptr, b3.data(), nullptr,
                                                    blk, 1, f0Frac, 0.92f, 1.0f, fl, env, false);
            if (bIdx >= 20)
                for (int n = 0; n < blk; ++n)
                    outInterSq += (double) b2[(size_t) n] * (double) b2[(size_t) n];
        }
        const float interAttenDb = (float) (-10.0 * std::log10 ((outInterSq + 1e-15) / (inInterSq + 1e-15)));

        CHECK (harmLossDb < 0.15f,
               "非整数周期(237.3Hz)でもラグランジュ3次補間により声の倍音損失ほぼゼロ (損失 %.3f dB < 0.15 dB)", harmLossDb);
        CHECK (interAttenDb > 4.5f,
               "歌唱中でも倍音の隙間ノイズ (k+0.5)*F0 をしっかり引き算純化 (減衰 -%.2f dB > 4.5 dB)", interAttenDb);

        const float tailPeriodic = gz::v570::AiVoicePurifierAndNoiseClassifier::computeHarmonicTailBridge01 (true, 0.80f, 0.0030f, 0.0010f);
        const float tailNoise    = gz::v570::AiVoicePurifierAndNoiseClassifier::computeHarmonicTailBridge01 (false, 0.10f, 0.0030f, 0.0010f);
        CHECK (tailPeriodic > 0.5f && tailNoise == 0.0f,
               "ピアニッシモ語尾保護ブリッジが有声ロングトーンのみを通し無声雑音は完全遮断 (periodic=%.2f, noise=%.2f)",
               tailPeriodic, tailNoise);
    }

    // -------------------------------------------------------------------------
    // [5] 新機能: J-Pop完パケ艶＆喉締めオートリリーフ (1.08kHz / 3.88kHz 動的緩和)
    // -------------------------------------------------------------------------
    std::printf ("\n[5] 新機能：J-Pop 完パケ艶＆喉締めオートリリーフ (1.08kHz / 3.88kHz 動的緩和)\n");
    {
        gz::v570::JPopAutoVocalPolishEngine pol;
        pol.prepare (sr);
        std::vector<float> L (blk, 0.0f), R (blk, 0.0f);
        gz::v570::FastTptSvf harshProbe0, harshProbe2;
        harshProbe0.setBandPass (sr, 3880.0, 2.2);
        harshProbe2.setBandPass (sr, 3880.0, 2.2);

        double harshE0 = 0.0, harshE2 = 0.0;
        bool allFinite = true;
        double ph = 0.0;
        for (int bIdx = 0; bIdx < 60; ++bIdx)
        {
            for (int n = 0; n < blk; ++n)
            {
                // 母音680Hzに対して 1080Hzイキミ と 3880Hz喉締め刺さり が強く突出したハイトーン声
                const float s = 0.03f * (float) std::sin (ph * 680.0)
                              + 0.08f * (float) std::sin (ph * 1080.0)
                              + 0.09f * (float) std::sin (ph * 3880.0);
                ph += 2.0 * 3.14159265358979323846 / sr;
                L[(size_t) n] = s;
                R[(size_t) n] = s;
                if (bIdx >= 20)
                {
                    const float p0 = harshProbe0.processSample (s);
                    harshE0 += (double) p0 * (double) p0;
                }
            }
            pol.processBlock (L.data(), R.data(), blk, 2, 0.90f, 340.0f);
            for (int n = 0; n < blk; ++n)
            {
                if (! std::isfinite (L[(size_t) n]) || ! std::isfinite (R[(size_t) n]))
                    allFinite = false;
                if (bIdx >= 20)
                {
                    const float p2 = harshProbe2.processSample (L[(size_t) n]);
                    harshE2 += (double) p2 * (double) p2;
                }
            }
        }
        const float harshCutDb = (float) (-10.0 * std::log10 ((harshE2 + 1e-15) / (harshE0 + 1e-15)));
        CHECK (allFinite && pol.reliefDb() > 1.5f && harshCutDb > 1.5f,
               "喉締め刺さり声(1.08kHz/3.88kHz)を動的検知して滑らかに緩和 (relief=%.2f dB, 3.88kHz実測減衰=-%.2f dB)",
               pol.reliefDb(), harshCutDb);
    }

    // -------------------------------------------------------------------------
    // [6] 統合Prism Mode 4 ＆ Denoise無稼働スリープ ＆ 状態保存/復元ラウンドトリップ
    // -------------------------------------------------------------------------
    std::printf ("\n[6] 単一パス統合Prism Mode 4・Denoise Smart Sleep・状態保存/復元\n");
    {
        VocalGzzioProcessor p1;
        p1.prepareToPlay (sr, blk);
        p1.setMasteringPrismMode (4);
        p1.setJpopPolishMode (2);
        if (auto* mSouji = p1.apvts.getParameter ("mod_souji"))
            mSouji->setValueNotifyingHost (0.0f); // おそうじ完全OFF -> Denoise 4帯域分割スリープ検証

        juce::AudioBuffer<float> buf (2, blk);
        juce::MidiBuffer midi;
        bool finiteOk = true;
        double ph = 0.0;
        for (int bIdx = 0; bIdx < 80; ++bIdx)
        {
            auto* L = buf.getWritePointer (0);
            auto* R = buf.getWritePointer (1);
            for (int n = 0; n < blk; ++n)
            {
                const float s = 0.12f * (float) std::sin (ph);
                ph += 2.0 * 3.14159265358979323846 * 260.0 / sr;
                L[n] = s;
                R[n] = s;
            }
            p1.processBlock (buf, midi);
            for (int n = 0; n < blk; ++n)
                if (! std::isfinite (buf.getSample (0, n)) || ! std::isfinite (buf.getSample (1, n))
                    || std::abs (buf.getSample (0, n)) > 1.05f)
                    finiteOk = false;
        }
        // 再び mod_souji を ON にしてもクリックや発散無く復帰すること
        if (auto* mSouji = p1.apvts.getParameter ("mod_souji"))
            mSouji->setValueNotifyingHost (1.0f);
        for (int bIdx = 0; bIdx < 20; ++bIdx)
        {
            buf.clear();
            p1.processBlock (buf, midi);
            for (int n = 0; n < blk; ++n)
                if (! std::isfinite (buf.getSample (0, n)))
                    finiteOk = false;
        }
        CHECK (finiteOk, "単一パス統合 Prism Mode 4 ＆ mod_souji スリープ復帰が完全に無歪み・有限値で安定動作");

        juce::MemoryBlock stateBlob;
        p1.getStateInformation (stateBlob);

        VocalGzzioProcessor p2;
        p2.prepareToPlay (sr, blk);
        p2.setStateInformation (stateBlob.getData(), (int) stateBlob.getSize());
        CHECK (p2.getMasteringPrismMode() == 4 && p2.getJpopPolishMode() == 2,
               "状態保存・復元で MasteringPrismMode=4 と JpopPolishMode=2 が完全保持される (prism=%d, polish=%d)",
               p2.getMasteringPrismMode(), p2.getJpopPolishMode());
    }

    std::printf (gFail ? "\n== %d 件 FAIL ==\n" : "\n== v5.7.0 全検証 PASS ==\n", gFail);
    return gFail ? 1 : 0;
}
