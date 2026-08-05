// dsp_voiceshift_stress.cpp — VoiceShifter 長時間ストレステスト (v2.6.0)
//
// ユーザー報告「Cubase 12 AI + UR12 + バッファ124 でボイス変換を押すと
// 最初しか音が出ない」の再現条件に合わせ、実物の VoiceShifter.h を
// 44.1kHz / 124サンプルブロックで叩き続ける。
//
//   A: 3分間の歌い込み(フレーズ+無音の繰り返し)      … NaN無し・出力が生きている
//   B: 完全無音30秒 → 歌が戻る                        … 無音で壊れない・復帰する
//   C: デノーマル級の極小入力5秒 → 歌                  … 爆発しない・復帰する
//   D: フルスケール矩形波5秒                           … 有限・過大出力しない
//   E: ON/OFFトグル×20回(プラグインと同じ reset 運用)  … 毎回復帰する
//   F: 低遅延切替(setWindow 10↔9)×10回                … 毎回復帰する
//   G: NaN注入 → 汚染を確認 → v2.6.0安全弁で自己回復    … 弁が実際に効く証明
//
// ビルド:  g++ -O2 -std=c++17 -I../Source dsp_voiceshift_stress.cpp -o dsp_stress
#include "../Source/VoiceShifter.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <vector>
#include <algorithm>

static constexpr double kSR    = 44100.0;
static constexpr int    kBlock = 124;          // ユーザーの実バッファサイズ
static int gFail = 0;

#define CHECK(cond, ...) do { \
    if (cond) { std::printf("  PASS: " __VA_ARGS__); std::printf("\n"); } \
    else      { std::printf("  FAIL: " __VA_ARGS__); std::printf("\n"); ++gFail; } } while (0)

// ---- 歌声っぽい音源: 倍音20本のノコギリ+ビブラート。位相連続で生成 ----
struct Voice
{
    double phase[20] = {};
    double vibPhase = 0.0;
    // f0 [Hz], amp 0..1
    float sample (double f0, double amp)
    {
        const double vib = 1.0 + 0.017 * std::sin (vibPhase);       // ±30セント
        vibPhase += 2.0 * M_PI * 5.5 / kSR;
        double s = 0.0;
        for (int h = 0; h < 20; ++h)
        {
            const double fh = f0 * vib * (h + 1);
            if (fh > kSR * 0.45) break;
            s += std::sin (phase[h]) / (h + 1);
            phase[h] += 2.0 * M_PI * fh / kSR;
            if (phase[h] > 2.0 * M_PI) phase[h] -= 2.0 * M_PI;
        }
        return (float) (amp * 0.55 * s / 1.7);                       // ~ -6dBFS ピーク
    }
};

static bool finiteBlock (const float* p, int n)
{
    float acc = 0.0f;
    for (int i = 0; i < n; ++i) acc += std::abs (p[i]);
    return std::isfinite (acc);
}

// 直近ウィンドウのRMS(dBFS)を測る小さなメーター
struct Meter
{
    std::vector<float> buf; size_t w = 0; bool full = false;
    explicit Meter (int n) : buf ((size_t) n, 0.0f) {}
    void push (const float* p, int n)
    { for (int i = 0; i < n; ++i) { buf[w] = p[i]; if (++w >= buf.size()) { w = 0; full = true; } } }
    double rmsDb() const
    {
        const size_t n = full ? buf.size() : w; if (!n) return -200.0;
        double s = 0.0; for (size_t i = 0; i < n; ++i) s += (double) buf[i] * buf[i];
        return 10.0 * std::log10 (s / (double) n + 1e-30);
    }
};

int main()
{
    std::printf ("VoiceShifter stress @ %.0f Hz, block=%d (ユーザー実機条件)\n", kSR, kBlock);

    // ============ A: 3分間の歌い込み ============
    {
        gz::VoiceShifter sh; sh.prepare (kSR, 10, 4);
        sh.setParams (4.0f, 2.0f, 1.0f);                 // ボイス変換らしい設定
        Voice v; Meter m ((int)(kSR * 0.05));
        const long total = (long) (kSR * 180.0);
        long bad = 0, deadSing = 0; float peak = 0.0f;
        float blk[kBlock];
        long pos = 0;
        while (pos < total)
        {
            // フレーズ: 2.2秒歌って0.6秒休む。f0は220→330Hzをゆっくり往復
            for (int i = 0; i < kBlock; ++i, ++pos)
            {
                const double t = pos / kSR;
                const bool sing = std::fmod (t, 2.8) < 2.2;
                const double f0 = 275.0 + 55.0 * std::sin (2.0 * M_PI * t / 23.0);
                blk[i] = sing ? v.sample (f0, 0.8) : 0.0f;
            }
            const double t0 = (pos - kBlock) / kSR;
            const bool midPhrase = (std::fmod (t0, 2.8) > 0.25) && (std::fmod (t0, 2.8) < 2.0);
            sh.processBlock (blk, kBlock);
            if (! finiteBlock (blk, kBlock)) ++bad;
            m.push (blk, kBlock);
            for (int i = 0; i < kBlock; ++i) peak = std::max (peak, std::abs (blk[i]));
            if (midPhrase && t0 > 1.0 && m.rmsDb() < -60.0) ++deadSing;
        }
        CHECK (bad == 0, "A 3分間: 非有限ブロック %ld 個 (0が正)", bad);
        CHECK (deadSing == 0, "A 3分間: 歌唱中に出力が死んだブロック %ld 個 (0が正)", deadSing);
        CHECK (peak < 2.0f, "A 3分間: ピーク %.2f (<2.0)", peak);
    }

    // ============ B: 完全無音30秒 → 歌が戻る ============
    {
        gz::VoiceShifter sh; sh.prepare (kSR, 10, 4);
        sh.setParams (4.0f, 2.0f, 1.0f);
        Voice v; float blk[kBlock];
        long bad = 0;
        for (long pos = 0; pos < (long)(kSR * 30.0); pos += kBlock)
        {
            std::memset (blk, 0, sizeof blk);
            sh.processBlock (blk, kBlock);
            if (! finiteBlock (blk, kBlock)) ++bad;
        }
        CHECK (bad == 0, "B 無音30秒: 非有限ブロック %ld 個 (0が正)", bad);
        // 歌を戻して 150ms 後に生きているか
        Meter m ((int)(kSR * 0.05));
        for (long pos = 0; pos < (long)(kSR * 0.5); pos += kBlock)
        {
            for (int i = 0; i < kBlock; ++i) blk[i] = v.sample (260.0, 0.8);
            sh.processBlock (blk, kBlock);
            if (pos > (long)(kSR * 0.15)) m.push (blk, kBlock);
        }
        CHECK (m.rmsDb() > -40.0, "B 復帰: 歌再開150ms後のRMS %.1f dBFS (>-40)", m.rmsDb());
    }

    // ============ C: デノーマル級の極小入力 ============
    {
        gz::VoiceShifter sh; sh.prepare (kSR, 10, 4);
        sh.setParams (4.0f, 2.0f, 1.0f);
        Voice v; float blk[kBlock]; long bad = 0; float peak = 0.0f;
        for (long pos = 0; pos < (long)(kSR * 5.0); pos += kBlock)
        {
            for (int i = 0; i < kBlock; ++i)
                blk[i] = (float) (1e-30 * std::sin (2.0 * M_PI * 200.0 * (pos + i) / kSR));
            sh.processBlock (blk, kBlock);
            if (! finiteBlock (blk, kBlock)) ++bad;
            for (int i = 0; i < kBlock; ++i) peak = std::max (peak, std::abs (blk[i]));
        }
        CHECK (bad == 0 && peak < 1e-3f, "C 極小入力5秒: 非有限%ld・ピーク%.2g (爆発しない)", bad, peak);
        Meter m ((int)(kSR * 0.05));
        for (long pos = 0; pos < (long)(kSR * 0.5); pos += kBlock)
        {
            for (int i = 0; i < kBlock; ++i) blk[i] = v.sample (260.0, 0.8);
            sh.processBlock (blk, kBlock);
            if (pos > (long)(kSR * 0.15)) m.push (blk, kBlock);
        }
        CHECK (m.rmsDb() > -40.0, "C 復帰: RMS %.1f dBFS (>-40)", m.rmsDb());
    }

    // ============ D: フルスケール矩形波 ============
    {
        gz::VoiceShifter sh; sh.prepare (kSR, 10, 4);
        sh.setParams (4.0f, 2.0f, 1.0f);
        float blk[kBlock]; long bad = 0; float peak = 0.0f;
        for (long pos = 0; pos < (long)(kSR * 5.0); pos += kBlock)
        {
            for (int i = 0; i < kBlock; ++i)
                blk[i] = (std::fmod ((pos + i) / kSR * 150.0, 1.0) < 0.5) ? 1.0f : -1.0f;
            sh.processBlock (blk, kBlock);
            if (! finiteBlock (blk, kBlock)) ++bad;
            for (int i = 0; i < kBlock; ++i) peak = std::max (peak, std::abs (blk[i]));
        }
        CHECK (bad == 0, "D 矩形波5秒: 非有限ブロック %ld 個 (0が正)", bad);
        CHECK (peak < 8.0f, "D 矩形波5秒: ピーク %.2f (<8, 安全弁≤4×の範囲)", peak);
    }

    // ============ E: ON/OFFトグル×20回 (プラグインと同じ reset 運用) ============
    {
        gz::VoiceShifter sh; sh.prepare (kSR, 10, 4);
        sh.setParams (4.0f, 2.0f, 1.0f);
        Voice v; float blk[kBlock]; long bad = 0; int deadCycles = 0;
        for (int cyc = 0; cyc < 20; ++cyc)
        {
            sh.reset();                                   // プラグインはON化の瞬間にreset
            Meter m ((int)(kSR * 0.05));
            for (long pos = 0; pos < (long)(kSR * 1.0); pos += kBlock)   // 1秒ON
            {
                for (int i = 0; i < kBlock; ++i) blk[i] = v.sample (250.0 + 3 * cyc, 0.8);
                sh.processBlock (blk, kBlock);
                if (! finiteBlock (blk, kBlock)) ++bad;
                if (pos > (long)(kSR * 0.15)) m.push (blk, kBlock);
            }
            if (m.rmsDb() < -40.0) ++deadCycles;
            for (long pos = 0; pos < (long)(kSR * 0.3); pos += kBlock)   // 0.3秒OFF(素通し=呼ばない)
                for (int i = 0; i < kBlock; ++i) (void) v.sample (250.0, 0.8);
        }
        CHECK (bad == 0, "E トグル20回: 非有限ブロック %ld 個 (0が正)", bad);
        CHECK (deadCycles == 0, "E トグル20回: 復帰しなかった回 %d (0が正)", deadCycles);
    }

    // ============ F: 低遅延切替 (setWindow 10↔9) ×10回 ============
    {
        gz::VoiceShifter sh; sh.prepare (kSR, 10, 4);
        sh.setParams (4.0f, 2.0f, 1.0f);
        Voice v; float blk[kBlock]; long bad = 0; int deadCycles = 0;
        for (int cyc = 0; cyc < 10; ++cyc)
        {
            sh.setWindow ((cyc & 1) ? 10 : 9);
            Meter m ((int)(kSR * 0.05));
            for (long pos = 0; pos < (long)(kSR * 1.0); pos += kBlock)
            {
                for (int i = 0; i < kBlock; ++i) blk[i] = v.sample (270.0, 0.8);
                sh.processBlock (blk, kBlock);
                if (! finiteBlock (blk, kBlock)) ++bad;
                if (pos > (long)(kSR * 0.15)) m.push (blk, kBlock);
            }
            if (m.rmsDb() < -40.0) ++deadCycles;
        }
        CHECK (bad == 0, "F 低遅延切替10回: 非有限ブロック %ld 個 (0が正)", bad);
        CHECK (deadCycles == 0, "F 低遅延切替10回: 復帰しなかった回 %d (0が正)", deadCycles);
    }

    // ============ G: NaN / Inf 注入 → 落ちない・汚染されない・自力で戻る ============
    // v2.6.0 以前はここで (a) 配列外アクセスによるクラッシュ (ASan で実測) と
    // (b) 位相メモリの永久汚染 = 以後ずっと無音、が起きていた。
    // これがユーザー報告「ボイス変換を押すと最初しか音が出ない」の正体。
    {
        const float poison[3] = { std::nanf (""), INFINITY, -INFINITY };
        const char* pname[3]  = { "NaN", "+Inf", "-Inf" };

        for (int p = 0; p < 3; ++p)
        {
            gz::VoiceShifter sh; sh.prepare (kSR, 10, 4);
            sh.setParams (4.0f, 2.0f, 1.0f);
            Voice v; float blk[kBlock];
            for (long pos = 0; pos < (long)(kSR * 1.0); pos += kBlock)   // まず1秒歌う
            { for (int i = 0; i < kBlock; ++i) blk[i] = v.sample (260.0, 0.8);
              sh.processBlock (blk, kBlock); }

            // 汚染を1ブロックまるごと流し込む(落ちなければ第一関門クリア)
            for (int i = 0; i < kBlock; ++i) blk[i] = poison[p];
            sh.processBlock (blk, kBlock);
            CHECK (finiteBlock (blk, kBlock), "G(%s) 汚染ブロック自体の出力が有限", pname[p]);

            // 以後きれいな入力に戻したとき、外側の助けなしで戻るか
            long bad = 0; Meter m ((int)(kSR * 0.05));
            for (long pos = 0; pos < (long)(kSR * 1.0); pos += kBlock)
            {
                for (int i = 0; i < kBlock; ++i) blk[i] = v.sample (260.0, 0.8);
                sh.processBlock (blk, kBlock);
                if (! finiteBlock (blk, kBlock)) ++bad;
                if (pos > (long)(kSR * 0.5)) m.push (blk, kBlock);
            }
            CHECK (bad == 0, "G(%s) 復帰後: 非有限ブロック %ld 個 (0が正)", pname[p], bad);
            CHECK (m.rmsDb() > -40.0, "G(%s) 復帰後: RMS %.1f dBFS (>-40 = 音が戻っている)",
                   pname[p], m.rmsDb());
        }

        // 1サンプルだけ紛れ込む現実的なケース(バッファの継ぎ目の壊れ)も確認
        {
            gz::VoiceShifter sh; sh.prepare (kSR, 10, 4);
            sh.setParams (4.0f, 2.0f, 1.0f);
            Voice v; float blk[kBlock]; long bad = 0;
            for (int cyc = 0; cyc < 200; ++cyc)
            {
                for (int i = 0; i < kBlock; ++i) blk[i] = v.sample (260.0, 0.8);
                if (cyc % 7 == 0) blk[cyc % kBlock] = std::nanf ("");
                sh.processBlock (blk, kBlock);
                if (! finiteBlock (blk, kBlock)) ++bad;
            }
            Meter m ((int)(kSR * 0.05));
            for (long pos = 0; pos < (long)(kSR * 0.5); pos += kBlock)
            {
                for (int i = 0; i < kBlock; ++i) blk[i] = v.sample (260.0, 0.8);
                sh.processBlock (blk, kBlock);
                if (pos > (long)(kSR * 0.2)) m.push (blk, kBlock);
            }
            CHECK (bad == 0, "G(散発NaN) 200ブロック中の非有限出力 %ld 個 (0が正)", bad);
            CHECK (m.rmsDb() > -40.0, "G(散発NaN) 最終RMS %.1f dBFS (>-40)", m.rmsDb());
        }
    }

    std::printf (gFail ? "\n== %d 件 FAIL ==\n" : "\n== 全テスト PASS ==\n", gFail);
    return gFail ? 1 : 0;
}
