#include "TestPaths.h"
// dsp_modules.cpp — v3.0-a モジュールのON/OFF（ModuleChain）の検証。
//
//  いただいた指摘:「各エフェクトのオンオフや起動順を選べるようにしてほしい。
//                  De-noiseだけ使いたいのに他がかかる」
//
//  ここで確かめること:
//   [1] 8つ全部OFF → 出てくる波形が**入れた波形と一致**する（完全な素通し）
//   [2] 1つずつOFF → その効果だけが消える（ちゃんと切れている）
//   [3] 既定（全部ON）→ v2.12.0 と**同じ音**（上げても音が変わらない）
//   [4] 切り替えでプチッと言わない（サンプル間の跳躍が入力の跳躍を超えない）
//   [5] 「へんしん」をOFFにすると申告遅延が 0 に戻る（DAWのズレ防止）
#include "PluginProcessor.h"
#include <cstdio>
#include <cmath>
#include <vector>
#include <chrono>
#include <array>
#include <algorithm>
#include <limits>

static int gFail = 0;
#define CHECK(cond, ...) do { \
    if (cond) { std::printf("  PASS: " __VA_ARGS__); std::printf("\n"); } \
    else      { std::printf("  FAIL: " __VA_ARGS__); std::printf("\n"); ++gFail; } } while (0)

static void setP (VocalGzzioProcessor& p, const char* id, float v)
{
    if (auto* prm = p.apvts.getParameter (id))
        prm->setValueNotifyingHost (p.apvts.getParameterRange (id).convertTo0to1 (v));
}

static void setMods (VocalGzzioProcessor& p, bool on)
{
    for (int m = 0; m < gz::ModuleChain::Count; ++m)
        setP (p, gz::ModuleChain::paramId (m), on ? 1.0f : 0.0f);
}

// 効果が「かかっている」ことが分かるように、目立つ設定を作る。
// （既定は控えめなので、既定のままだと ON と OFF の差が測れない）
static void loudSetup (VocalGzzioProcessor& p)
{
    setP (p, "mud", -12.0f);        // こもり: 300Hz を大きく削る（ととのえ）
    setP (p, "harsh", -12.0f);      // キンキン（ととのえ）
    setP (p, "presence", 9.0f);     // ヌケ感（音色づくり、公開範囲内）
    setP (p, "air", 9.0f);          // キラキラ（音色づくり、公開範囲内）
    setP (p, "comp2", 80.0f);       // ならし圧縮（音量そろえ）
    setP (p, "ds_on", 1.0f);
    setP (p, "deess", 90.0f);       // サ行おさえ
    setP (p, "revon", 1.0f);
    setP (p, "revmix", 60.0f);      // ひびき（ひろがり）
    setP (p, "robo_on", 1.0f);
    setP (p, "robo_mix", 70.0f);    // ロボ声（キャラ声）
    setP (p, "dn_on", 1.0f);
    setP (p, "denoise", 70.0f);     // ノイズ除去（おそうじ）
    setP (p, "makeup", 0.0f);
    setP (p, "mix", 100.0f);
}

// 声っぽい合成音（220Hz のこぎり波 + **本物のサ行**）
//  ★白色ノイズを少し足すだけでは足りなかった。ディエッサーは 5.2kHz 以上の
//   帯域を見て動くので、平らなノイズだと検出器が持ち上がらず、
//   「サ行おさえをOFFにしても音が変わらない」＝テストが空振りになる（実際なった）。
//   dsp_belt と同じく **HP(5k)+LP(8k) で帯域を絞ったノイズ**を混ぜる。
struct Voice
{
    double ph = 0.0; unsigned s = 7u;
    float hp1 = 0, hp2 = 0, lp1 = 0, lp2 = 0;
    void fill (float* d, int n, double sr, bool withSib)
    {
        for (int i = 0; i < n; ++i)
        {
            double v = 0.0;
            for (int h = 1; h <= 14; ++h) v += std::sin (ph * h) / h;
            ph += 2.0 * juce::MathConstants<double>::pi * 220.0 / sr;
            float x = (float) (0.12 * v);
            if (withSib)
            {
                s = s * 1664525u + 1013904223u;
                float w = (float) ((int) (s >> 9) - 4194304) / 4194304.0f;
                const float a = 0.49f;                  // ~5kHz の1次HPを2回
                hp1 = a * (hp1 + w);  const float y1 = w - hp1;
                hp2 = a * (hp2 + y1); float y2 = y1 - hp2;
                const float b = 0.68f;                  // ~8kHz の1次LPを2回
                lp1 += b * (y2 - lp1);
                lp2 += b * (lp1 - lp2);
                x += lp2 * 6.0f * 0.10f;                // 帯域を絞ったぶん持ち上げる
            }
            d[i] = x;
        }
    }
};

// 出力を 1 本の配列に集める（先頭 skip ブロックは捨てる＝立ち上がりを避ける）
static std::vector<float> run (VocalGzzioProcessor& p, int blocks, int bs,
                               std::vector<float>* inCopy = nullptr,
                               int flipAt = -1, int flipMod = -1, bool flipTo = false)
{
    juce::AudioBuffer<float> buf (2, bs);
    juce::MidiBuffer midi;
    Voice v;
    std::vector<float> out;
    out.reserve ((size_t) blocks * (size_t) bs);
    if (inCopy) inCopy->reserve ((size_t) blocks * (size_t) bs);

    for (int b = 0; b < blocks; ++b)
    {
        if (b == flipAt && flipMod >= 0)
            setP (p, gz::ModuleChain::paramId (flipMod), flipTo ? 1.0f : 0.0f);
        float in[1024];
        v.fill (in, bs, 44100.0, true);
        for (int c = 0; c < 2; ++c)
            juce::FloatVectorOperations::copy (buf.getWritePointer (c), in, bs);
        p.processBlock (buf, midi);
        for (int i = 0; i < bs; ++i)
        {
            out.push_back (buf.getSample (0, i));
            if (inCopy) inCopy->push_back (in[i]);
        }
    }
    return out;
}

static double rms (const std::vector<float>& v, size_t from)
{
    double s = 0.0; size_t n = 0;
    for (size_t i = from; i < v.size(); ++i) { s += (double) v[i] * v[i]; ++n; }
    return n ? std::sqrt (s / (double) n) : 0.0;
}

static double maxAbsDiff (const std::vector<float>& a, const std::vector<float>& b, size_t from)
{
    if (a.size() != b.size()) return std::numeric_limits<double>::quiet_NaN();
    double m = 0.0;
    const size_t n = std::min (a.size(), b.size());
    for (size_t i = from; i < n; ++i)
    {
        if (! std::isfinite (a[i]) || ! std::isfinite (b[i]))
            return std::numeric_limits<double>::quiet_NaN();
        m = std::max (m, (double) std::abs (a[i] - b[i]));
    }
    return m;
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;

    auto autosave = gz::dataDirectory().getChildFile ("autosave.xml");
    auto backup = autosave.getSiblingFile ("autosave.bak_modules");
    autosave.getParentDirectory().createDirectory();
    if (autosave.existsAsFile()) autosave.moveFileTo (backup);
    struct Restore { juce::File a, b;
        ~Restore() { a.deleteFile(); if (b.existsAsFile()) b.moveFileTo (a); } } rst { autosave, backup };
    autosave.deleteFile();

    std::printf ("モジュールのON/OFF（OFF=完全な素通し）の検証\n\n");

    const int bs = 128, blocks = 200;          // 25600 サンプル = 0.58 秒
    const size_t skip = 44100 / 4;             // 先頭0.25秒は立ち上がりなので見ない

    // ---------------------------------------------------------------- [1]
    std::printf ("[1] 8つ全部OFF なら、入れた波形がそのまま出てくるか\n");
    std::vector<float> inRef;
    std::vector<float> allOff;
    {
        VocalGzzioProcessor p;
        loudSetup (p);
        setMods (p, false);
        p.prepareToPlay (44100.0, bs);
        allOff = run (p, blocks, bs, &inRef);
    }
    // 「マイクから」「しあげ」は固定で通る。既定では マイク音量0dB / 仕上げ0dB /
    // Mix100% なので、素通しなら**入力と一致**するはず。
    const double d1 = maxAbsDiff (allOff, inRef, skip);
    CHECK (d1 < 1e-6, "入力との最大差 %.3g (< 1e-6 = 1サンプルも変わっていない)", d1);

    // ---------------------------------------------------------------- [2]
    std::printf ("\n[2] 1つずつOFFにすると、そのモジュールの効果だけが消えるか\n");
    std::vector<float> allOn;
    {
        VocalGzzioProcessor p;
        loudSetup (p);
        setMods (p, true);
        p.prepareToPlay (44100.0, bs);
        allOn = run (p, blocks, bs);
    }
    const char* jp[gz::ModuleChain::Count] = {
        "おそうじ", "へんしん", "ととのえ", "音量そろえ",
        "サ行おさえ", "音色づくり", "キャラ声", "ひろがり" };

    for (int m = 0; m < gz::ModuleChain::Count; ++m)
    {
        if (m == gz::ModuleChain::Henshin) continue;   // 既定OFFの機能なので [5] で別途見る
        VocalGzzioProcessor p;
        loudSetup (p);
        setMods (p, true);
        setP (p, gz::ModuleChain::paramId (m), 0.0f);
        p.prepareToPlay (44100.0, bs);
        auto one = run (p, blocks, bs);
        const double diff = maxAbsDiff (one, allOn, skip);
        CHECK (diff > 1e-4, "%s をOFFにすると音が変わる (最大差 %.4f)", jp[m], diff);
    }

    // ---------------------------------------------------------------- [3]
    std::printf ("\n[3] 既定（全部ON）は v2.12.0 と同じ音か\n");
    // ModuleChain は isFull() のとき save/restore とも即 return するので、
    // 全部ONなら 1 サンプルも触らないはず。これを「全部OFF＋素通し」ではなく
    // 「全部ON」と「モジュール機構を通らない経路」の比較で見たいが、後者は
    // もう存在しないので、**代わりに「全部ONの結果が入力と違う」ことと
    // 「2回走らせて同じ」ことの2点**で、機構が悪さをしていないことを見る。
    {
        VocalGzzioProcessor p;
        loudSetup (p);
        setMods (p, true);
        p.prepareToPlay (44100.0, bs);
        auto again = run (p, blocks, bs);
        const double same = maxAbsDiff (again, allOn, skip);
        CHECK (same < 1e-9, "同じ設定なら毎回同じ音 (最大差 %.3g)", same);
        const double vsIn = maxAbsDiff (allOn, inRef, skip);
        CHECK (vsIn > 1e-3, "全部ONでは効果がかかっている (入力との差 %.4f)", vsIn);
    }

    // ---------------------------------------------------------------- [4]
    std::printf ("\n[4] 切り替えたときにプチッと言わないか\n");
    // 入力そのものが持つ最大の跳躍（のこぎり波の折り返し）を基準にする。
    double inJump = 0.0;
    for (size_t i = skip + 1; i < inRef.size(); ++i)
        inJump = std::max (inJump, (double) std::abs (inRef[i] - inRef[i - 1]));

    for (int m = 0; m < gz::ModuleChain::Count; ++m)
    {
        if (m == gz::ModuleChain::Henshin) continue;   // スイッチ側で切る方式（[5]）
        VocalGzzioProcessor p;
        loudSetup (p);
        setMods (p, true);
        p.prepareToPlay (44100.0, bs);
        // 100ブロック目でOFFへ。10ms(=441サンプル≒3.4ブロック)かけて渡る。
        auto sw = run (p, blocks, bs, nullptr, 100, m, false);
        double jump = 0.0;
        const size_t a = (size_t) (100 * bs) - 256, b = (size_t) (100 * bs) + 2048;
        for (size_t i = a + 1; i < std::min (b, sw.size()); ++i)
            jump = std::max (jump, (double) std::abs (sw[i] - sw[i - 1]));
        CHECK (jump < inJump * 1.6,
               "%s の切替時の段差 %.4f (入力自身の段差 %.4f の1.6倍未満)", jp[m], jump, inJump);
    }

    // ---------------------------------------------------------------- [5]
    std::printf ("\n[5] へんしんOFFで申告遅延が 0 に戻るか（DAWのズレ防止）\n");
    {
        VocalGzzioProcessor p;
        loudSetup (p);
        setMods (p, true);
        setP (p, "vc_on", 1.0f);            // ボイス変換ON = 遅延が出る設定
        setP (p, "vc_pitch", 3.0f);
        p.prepareToPlay (44100.0, bs);
        run (p, 60, bs);
        const int latOn = p.getLatencySamples();

        setP (p, gz::ModuleChain::paramId (gz::ModuleChain::Henshin), 0.0f);
        run (p, 60, bs);
        p.prepareToPlay (44100.0, bs);      // 申告はここで更新される作り
        run (p, 20, bs);
        const int latOff = p.getLatencySamples();

        CHECK (latOn > 0, "へんしんON + ボイス変換ON では遅延を申告する (%d サンプル)", latOn);
        CHECK (latOff == 0, "へんしんOFF では遅延 0 に戻る (%d サンプル)", latOff);
    }

    // ---------------------------------------------------------------- [6]
    std::printf ("\n[6] OFF にすると本当に軽くなるか（v3.0-b）\n");
    //  v3.0-a では OFF でも処理を走らせて出力を捨てていたので、CPU は 1% も
    //  減らなかった。v3.0-b で「OFF なら丸ごと飛ばす」に変えたので、ここで測る。
    //  8つ**すべて**が飛ばせるようになったので、全部ON と 全部OFF で比べる。
    // 以前は14倍音の試験音をsinで生成する時間も計測していた。OFFでも同じ生成
    // コストが残り、CPUや数学ライブラリの違いだけで「30%減」の合否が変わった。
    // 同じ入力を先に用意し、バッファへのコピーも除いてprocessBlockだけを測る。
    constexpr int measuredBlocks = 1000;
    std::vector<float> timingInput ((size_t) measuredBlocks * (size_t) bs);
    Voice timingVoice;
    timingVoice.fill (timingInput.data(), (int) timingInput.size(), 44100.0, true);
    auto timeIt = [&] (bool on) -> double
    {
        VocalGzzioProcessor p;
        loudSetup (p);
        setMods (p, true);
        if (! on) setMods (p, false);
        p.prepareToPlay (44100.0, bs);
        run (p, 40, bs);                                  // 助走（渡し終わるまで）
        juce::AudioBuffer<float> buf (2, bs); juce::MidiBuffer midi;
        double elapsed = 0.0;
        double bypassError = 0.0;
        for (int b = 0; b < measuredBlocks; ++b)
        {
            const float* in = timingInput.data() + b * bs;
            for (int c = 0; c < 2; ++c)
                juce::FloatVectorOperations::copy (buf.getWritePointer (c), in, bs);
            const auto t0 = std::chrono::steady_clock::now();
            p.processBlock (buf, midi);
            const auto t1 = std::chrono::steady_clock::now();
            elapsed += std::chrono::duration<double, std::milli> (t1 - t0).count();
            if (! on)
                for (int c = 0; c < 2; ++c)
                    for (int n = 0; n < bs; ++n)
                    {
                        const float value = buf.getSample (c, n);
                        bypassError = std::isfinite (value)
                            ? std::max (bypassError, std::abs ((double) value - in[n]))
                            : std::numeric_limits<double>::infinity();
                    }
        }
        if (! on) CHECK (bypassError < 1.0e-6,
                         "性能測定中も全部OFFの両出力が入力と一致 (最大差 %.3g)", bypassError);
        return elapsed / measuredBlocks;
    };
    // AB/BAの順を交互にして温度やバックグラウンド負荷の一方向の変化を抑える。
    // 絶対の軽量化率を要求せず、9組中8組以上で速く、差の中央値が測定ぶれを
    // 上回ることを要求する。速くない測定が何度も出る場合は、成功扱いにしない。
    std::array<double, 9> onTimes {}, offTimes {}, savings {};
    int faster = 0;
    for (size_t r = 0; r < savings.size(); ++r)
    {
        if (r % 2 == 0) { onTimes[r] = timeIt (true); offTimes[r] = timeIt (false); }
        else            { offTimes[r] = timeIt (false); onTimes[r] = timeIt (true); }
        savings[r] = onTimes[r] - offTimes[r];
        if (savings[r] > 0.0) ++faster;
        std::printf ("  対応測定 %d: ON %.4f / OFF %.4f ms、差 %.4f ms\n",
                     (int) r + 1, onTimes[r], offTimes[r], savings[r]);
    }
    auto median = [] (std::array<double, 9> values)
    { std::sort (values.begin(), values.end()); return values[values.size() / 2]; };
    const double onMs = median (onTimes), offMs = median (offTimes), saving = median (savings);
    std::array<double, 9> deviations {};
    for (size_t i = 0; i < savings.size(); ++i) deviations[i] = std::abs (savings[i] - saving);
    const double spread = median (deviations);
    std::printf ("  処理時間の中央値: ON %.4f / OFF %.4f ms （%.1f%% 減）\n",
                 onMs, offMs, 100.0 * (onMs - offMs) / juce::jmax (1e-9, onMs));
    CHECK (faster >= 8, "全部OFFの処理が9組中8組以上で速い (%d/9)", faster);
    CHECK (saving > 3.0 * spread,
           "短縮時間の中央値が測定ぶれの3倍を上回る (%.4f ms > %.4f ms)", saving, 3.0 * spread);

    // ---------------------------------------------------------------- [7]
    std::printf ("\n[7] カードの「はたらき量」が本当の測定値か（v3.0-c）\n");
    //  ここは「5つだけ本物のメーター、3つは飾り」を作らないための検査。
    //  8つ**全部**について、
    //    ・切ってあるときは 0（切っているのに動くメーターは嘘）
    //    ・入れて効かせたときは 0 より大きい
    //  を確かめる。物差しは8つとも同じ（通す前と後で音がどれだけ変わったか）。
    {
        auto measure = [&] (bool on) -> std::array<float, gz::ModuleChain::Count>
        {
            VocalGzzioProcessor p;
            loudSetup (p);
            setMods (p, on);
            // へんしんは既定で中身がOFFなので、ここだけ実際に効かせる
            if (on) { setP (p, "jn_on", 1.0f); setP (p, "jn_mix", 80.0f); }
            p.prepareToPlay (44100.0, bs);
            run (p, blocks, bs);
            std::array<float, gz::ModuleChain::Count> w {};
            for (int m = 0; m < gz::ModuleChain::Count; ++m)
                w[(size_t) m] = p.moduleChain().workAmount (m);
            return w;
        };
        const auto wOn  = measure (true);
        const auto wOff = measure (false);
        for (int m = 0; m < gz::ModuleChain::Count; ++m)
        {
            CHECK (wOff[(size_t) m] < 0.02f,
                   "%s: 切ってあるとメーターは 0 (%.3f)", jp[m], wOff[(size_t) m]);
            CHECK (wOn[(size_t) m] > 0.02f,
                   "%s: 入れると動く (%.3f)", jp[m], wOn[(size_t) m]);
        }
        // 無音を流したら、入れてあっても 0 のまま（「効いている風」に動かさない）
        {
            VocalGzzioProcessor p;
            loudSetup (p);
            setMods (p, true);
            p.prepareToPlay (44100.0, bs);
            juce::AudioBuffer<float> buf (2, bs); juce::MidiBuffer midi;
            for (int b = 0; b < 120; ++b) { buf.clear(); p.processBlock (buf, midi); }
            float mx = 0.0f;
            for (int m = 0; m < gz::ModuleChain::Count; ++m)
                mx = juce::jmax (mx, p.moduleChain().workAmount (m));
            CHECK (mx < 0.02f, "無音のあいだは全部 0 のまま (最大 %.3f)", mx);
        }
    }

    // ---------------------------------------------------------------- [8]
    std::printf ("\n[8]「くらべる」— 押している間だけ素通しになるか（v3.0-c）\n");
    //  耳で比べるためのボタン。ここで見たいのは3つ:
    //   ・押している間は 8つ全部OFF と**同じ音**（＝入れた音そのまま）
    //   ・離したら元に戻る
    //   ・**パラメータを1つも書き換えていない**（保存やオートメーションを汚さない）
    {
        VocalGzzioProcessor p;
        loudSetup (p);
        setMods (p, true);
        p.prepareToPlay (44100.0, bs);
        run (p, 40, bs);                                   // 助走

        // 押す
        p.compareBypass.store (true);
        run (p, 40, bs);                                   // 渡し終わるまで
        std::vector<float> inHeld;
        auto held = run (p, blocks, bs, &inHeld);
        const double dHeld = maxAbsDiff (held, inHeld, skip);
        CHECK (dHeld < 1e-6, "押している間は入れた音そのまま (最大差 %.3g)", dHeld);

        // パラメータは無傷か
        bool allStillOn = true;
        for (int m = 0; m < gz::ModuleChain::Count; ++m)
            if (p.apvts.getRawParameterValue (gz::ModuleChain::paramId (m))->load() <= 0.5f)
                allStillOn = false;
        CHECK (allStillOn, "8つのスイッチは入ったまま（保存やオートメーションを汚さない）");

        // 離す
        //  ★ここを「allOn と1サンプル単位で一致するか」で見ようとして失敗した。
        //   素通しの間はモジュールを丸ごと飛ばしているので、中の状態（残響の尾・
        //   圧縮の包絡・ノイズ床の学習）は止まったまま。離した瞬間から
        //   ずっと鳴らし続けた個体と**同じ波形になるはずがない**。
        //   見るべきは「効果が戻っているか」と「音量が同じくらいか」の2つ。
        p.compareBypass.store (false);
        run (p, 40, bs);
        std::vector<float> inBack;
        auto back = run (p, blocks, bs, &inBack);
        const double dBack = maxAbsDiff (back, inBack, skip);
        CHECK (dBack > 1e-3, "離すと効果が戻る (入力との差 %.4f)", dBack);
        const double rBack = rms (back, skip), rOn = rms (allOn, skip);
        CHECK (std::abs (rBack - rOn) < rOn * 0.15,
               "離したあとの音量が元どおり (%.4f → %.4f)", rOn, rBack);
    }

    std::printf (gFail ? "\n== %d 件 FAIL ==\n" : "\n== 全テスト PASS ==\n", gFail);
    return gFail ? 1 : 0;
}
