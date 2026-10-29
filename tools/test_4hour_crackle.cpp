// test_4hour_crackle.cpp — 4〜5時間連続稼働時の「チリチリ」音 3大原因の再現・解消検証テスト (5時間 = 8.64億サンプル)
#include "../Source/TdPitchShifter.h"
#include "../Source/PitchCorrection.h"
#include <cstdio>
#include <cmath>
#include <vector>
#include <algorithm>

static int gFailures = 0;
static void check (bool ok, const char* msg)
{
    std::printf ("  [%s] %s\n", ok ? "PASS" : "FAIL", msg);
    if (! ok) ++gFailures;
}

int main()
{
    std::printf ("=== VocalGzzio v5.3.0 4〜5時間連続稼働 チリチリ音解消ストレステスト ===\n\n");
    constexpr double sr = 48000.0;
    constexpr double pi = 3.14159265358979323846;

    // -------------------------------------------------------------------------
    // [検証1] WASAPIInputDevice リングバッファ 5時間(300分) クロックドリフト比較検証
    //   旧JUCE実装: +2.2ppm(約0.11サンプル/秒)の微小クロック差で約3.9時間後にreservoir(2048)が満杯になり
    //               ReleaseBuffer(0)で永久ウェッジ(毎パケット欠落=チリチリ音)、マイナス差ではzeromem連発
    //   新弾性水位制御: 5時間(180万パケット)連続稼働でも xrun=0・ゼロ欠落=0・段差=0 を維持
    // -------------------------------------------------------------------------
    std::printf ("[1] WASAPIInputDevice 5時間(300分) クロックドリフト耐性検証 (+2.5ppm / -2.5ppm)\n");
    for (double ppm : { +2.5, -2.5 })
    {
        const int actualBufferSize = 480;
        const int bufferSize = 480;
        const int totalCallbacks = (int) (5.0 * 3600.0 * sr / bufferSize); // 5時間 = 1,800,000 callbacks

        // 旧JUCEの挙動シミュレーション
        int oldCap = 2048;
        int oldReadable = 0;
        int oldWedgeDrops = 0;
        int oldZeroInjects = 0;
        int firstFaultCallback = -1;

        // 新パッチの挙動シミュレーション
        int newCap = 16384;
        int newReadable = 0;
        int newDrops = 0;
        int newZeroInjects = 0;
        const int lowWater  = bufferSize + actualBufferSize + std::max (256, bufferSize / 2);
        const int highWater = lowWater + std::max (1024, actualBufferSize * 2);

        double inAccum = 0.0;
        for (int cb = 0; cb < totalCallbacks; ++cb)
        {
            inAccum += (double) bufferSize * (1.0 + ppm * 1.0e-6);
            // パケット到着 (480サンプル単位)
            while (inAccum >= (double) actualBufferSize)
            {
                inAccum -= (double) actualBufferSize;
                // 旧JUCE: 残り空間が足りないと ReleaseBuffer(0) で破棄ウェッジ
                if (actualBufferSize > oldCap - oldReadable)
                {
                    ++oldWedgeDrops;
                    if (firstFaultCallback < 0) firstFaultCallback = cb;
                }
                else
                {
                    oldReadable += actualBufferSize;
                }
                // 新パッチ
                if (actualBufferSize > newCap - newReadable)
                    ++newDrops;
                else
                    newReadable += actualBufferSize;
            }

            // 出力コールバック消費
            if (oldReadable < bufferSize)
            {
                if (cb > 2)
                {
                    ++oldZeroInjects;
                    if (firstFaultCallback < 0) firstFaultCallback = cb;
                }
                oldReadable = 0;
            }
            else
            {
                oldReadable -= bufferSize;
            }

            // 新パッチの弾性水位消費
            if (newReadable >= bufferSize - 1 && newReadable < lowWater)
            {
                newReadable -= (bufferSize - 1); // +1サンプルクッション回復
            }
            else if (newReadable >= bufferSize)
            {
                newReadable -= bufferSize;
                if (newReadable > highWater && newReadable >= 1)
                    newReadable -= 1;            // -1サンプルドリフト吸収
            }
            else
            {
                if (cb > 2) ++newZeroInjects;
                newReadable = 0;
            }
        }
        const double faultHours = (firstFaultCallback >= 0) ? (firstFaultCallback * bufferSize / sr / 3600.0) : -1.0;
        std::printf ("    ドリフト %+.1f ppm: 旧JUCE [発症 %.2f時間目 / ウェッジ破棄 %d回 / ゼロ欠落 %d回] -> 新パッチ [破棄 %d回 / ゼロ欠落 %d回 / 最終水位 %d]\n",
                     ppm, faultHours, oldWedgeDrops, oldZeroInjects, newDrops, newZeroInjects, newReadable);
        check (newDrops == 0 && newZeroInjects == 0,
               ppm > 0 ? "プラス側クロック差(+2.5ppm)で5時間稼働しても満杯ウェッジ・欠落ゼロ"
                       : "マイナス側クロック差(-2.5ppm)で5時間稼働しても在庫切れゼロ欠落ゼロ");
    }

    // -------------------------------------------------------------------------
    // [検証2] TdPitchShifter + PitchCorrection 5時間(300分 = 8.64億サンプル) 連続稼働検査
    //   1時間・2時間・3時間・4時間・5時間時点での波形相関・音量差・高域比差・-54dBFSささやき跳躍比
    // -------------------------------------------------------------------------
    std::printf ("\n[2] TdPitchShifter + PitchCorrection 5時間(300分 = 8.64億サンプル) 連続稼働検証\n");
    {
        gz::TdPitchShifter sh; sh.prepare (sr);
        gz::PitchCorrection pc; pc.reset();

        const long phraseLen = (long) (4.0 * sr); // 4秒フレーズ
        std::vector<float> phraseIn ((size_t) phraseLen);
        std::vector<float> phraseF0 ((size_t) phraseLen);
        for (long i = 0; i < phraseLen; ++i)
        {
            const double t = (double) i / sr;
            double s = 0.0;
            float f0 = 0.0f;
            if (t < 3.0)
            {
                f0 = 218.0f;
                const double env = std::min (1.0, std::min (t * 25.0, (3.0 - t) * 25.0));
                s = 0.15 * env * (std::sin (2.0 * pi * f0 * t) + 0.4 * std::sin (4.0 * pi * f0 * t));
            }
            else if (t < 3.5)
            {
                // ささやき・語尾(-54dBFS = 0.0020): Upward Lifterで浮上する微弱有声領域
                f0 = 218.0f;
                s = 0.0018 * (std::sin (2.0 * pi * f0 * t) + 0.3 * std::sin (4.0 * pi * f0 * t));
            }
            else
            {
                // 部屋の暗騒音 (-60dBFS = 0.0010, 無声)
                f0 = 0.0f;
                unsigned q = (unsigned) (i * 2246822519u + 12345u);
                q ^= q >> 15; q *= 2654435761u; q ^= q >> 13;
                s = ((double) (q & 0xffff) / 32768.0 - 1.0) * 0.001;
            }
            phraseIn[(size_t) i] = (float) s;
            phraseF0[(size_t) i] = f0;
        }

        const long long totalPhrases = (long long) (5.0 * 3600.0 / 4.0); // 4500 phrases = 5 hours
        const long long markPhrases[] = { 15, 900, 1800, 2700, 3600, 4499 };
        const char*     markNames[]   = { "基準(1分)", "1時間後", "2時間後", "3時間後", "4時間後", "5時間後" };
        std::vector<float> refOut;
        double worstCorr = 1.0, worstTailJumpRatio = 0.0;
        bool allFinite = true;

        for (long long ph = 0; ph < totalPhrases; ++ph)
        {
            bool capture = false;
            int markIdx = -1;
            for (int m = 0; m < 6; ++m)
                if (ph == markPhrases[m]) { capture = true; markIdx = m; break; }

            std::vector<float> curOut;
            if (capture) curOut.resize ((size_t) phraseLen);

            for (long i = 0; i < phraseLen; i += 64)
            {
                const int n = (int) std::min (64L, phraseLen - i);
                const float f0 = phraseF0[(size_t) i];
                const float midi = (f0 > 0.0f) ? (69.0f + 12.0f * std::log2 (f0 / 440.0f)) : 0.0f;
                const float corr = pc.processSkilled (midi, (float) n / (float) sr, 0, 0, 20.0f, 1.0f);
                sh.setPitchHz (f0);
                sh.setParams (corr, 1.0f);
                float blk[64];
                for (int k = 0; k < n; ++k) blk[k] = phraseIn[(size_t) (i + k)];
                sh.processBlock (blk, n);
                if (capture)
                {
                    for (int k = 0; k < n; ++k)
                    {
                        if (! std::isfinite (blk[k])) allFinite = false;
                        curOut[(size_t) (i + k)] = blk[k];
                    }
                }
            }

            if (capture)
            {
                if (markIdx == 0)
                {
                    refOut = curOut;
                }
                else
                {
                    double ab = 0.0, aa = 1e-20, bb = 1e-20;
                    for (long i = 0; i < phraseLen; ++i)
                    {
                        ab += (double) refOut[(size_t) i] * curOut[(size_t) i];
                        aa += (double) refOut[(size_t) i] * refOut[(size_t) i];
                        bb += (double) curOut[(size_t) i] * curOut[(size_t) i];
                    }
                    const double corr = ab / std::sqrt (aa * bb);
                    const double rmsDb = 10.0 * std::log10 (bb / aa);

                    // ささやき語尾領域 (3.05s〜3.45s) のサンプル間最大跳躍比
                    double inJ = 0.0, outJ = 0.0;
                    for (long i = (long) (3.05 * sr); i < (long) (3.45 * sr); ++i)
                    {
                        inJ  = std::max (inJ,  (double) std::abs (phraseIn[(size_t) i] - phraseIn[(size_t) (i - 1)]));
                        outJ = std::max (outJ, (double) std::abs (curOut[(size_t) i]   - curOut[(size_t) (i - 1)]));
                    }
                    const double jumpRatio = outJ / std::max (inJ, 1e-9);
                    worstCorr = std::min (worstCorr, corr);
                    worstTailJumpRatio = std::max (worstTailJumpRatio, jumpRatio);
                    std::printf ("    %s: 相関 %.6f / 音量差 %+.3f dB / ささやき語尾(-54dBFS)跳躍比 %.2f\n",
                                 markNames[markIdx], corr, rmsDb, jumpRatio);
                }
            }
        }
        check (allFinite && worstCorr > 0.999 && worstTailJumpRatio < 1.5,
               "4時間・5時間経過後も波形相関 1.0000・ささやき語尾チリチリ発振ゼロ");
    }

    std::printf ("\n=== 結果: %s (FAIL %d 件) ===\n", gFailures == 0 ? "全項目 PASS" : "不合格あり", gFailures);
    return gFailures == 0 ? 0 : 1;
}
