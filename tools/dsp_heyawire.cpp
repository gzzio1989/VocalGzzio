// v4.0.0 「へや」がプラグイン本体に正しく結線されているかの検査。
// Heya.h 単体の検査(dsp_heya)とは別で、ここでは**プロセッサを実際に作って**
//   ・rev_type=7..12 を選んでも落ちない
//   ・音が出る（無音のままにならない）
//   ・申告遅延が0のまま（へやは追加遅延0の売り）
//   ・processBlock の中でメモリを確保しない
// を見る。
#include <juce_audio_processors/juce_audio_processors.h>
#include "../Source/Heya.h"
#include <cstdio>
#include <cmath>
#include <atomic>

static std::atomic<long long> gAllocs { 0 };
static std::atomic<bool>      gWatch  { false };
void* operator new (size_t s) { if (gWatch.load()) ++gAllocs; return std::malloc (s); }
void  operator delete (void* p) noexcept { std::free (p); }
void  operator delete (void* p, size_t) noexcept { std::free (p); }

static int gFail = 0;
#define CHECK(cond, ...) do { if (cond) { std::printf("  PASS: " __VA_ARGS__); std::printf("\n"); } \
                              else { std::printf("  FAIL: " __VA_ARGS__); std::printf("\n"); ++gFail; } } while(0)

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;   // メッセージループが要る（AsyncUpdater）

    std::unique_ptr<juce::AudioProcessor> proc (createPluginFilter());
    if (proc == nullptr) { std::printf("!! プロセッサを作れませんでした\n"); return 1; }

    const double sr = 48000.0;
    const int    blk = 256;
    proc->setPlayConfigDetails (2, 2, sr, blk);
    proc->prepareToPlay (sr, blk);

    auto setParam = [&] (const juce::String& id, float norm)
    {
        for (auto* p : proc->getParameters())
            if (auto* wp = dynamic_cast<juce::AudioProcessorParameterWithID*> (p))
                if (wp->paramID == id) { wp->setValueNotifyingHost (norm); return true; }
        return false;
    };
    auto choiceCount = [&] (const juce::String& id) -> int
    {
        for (auto* p : proc->getParameters())
            if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (p))
                if (c->paramID == id) return c->choices.size();
        return -1;
    };

    std::printf("\n== 1. rev_type の選択肢がプラグインに載っているか ==\n");
    const int nTypes = choiceCount ("rev_type");
    CHECK (nTypes == 13, "rev_type の選択肢 %d 個（7つの従来型 + 6部屋 = 13）", nTypes);

    // リバーブを聞こえるように、ひびきを上げる
    setParam ("revon", 1.0f);
    setParam ("revmix", 0.60f);
    setParam ("revsize", 0.80f);

    long long allocTotal = 0;
    juce::AudioBuffer<float> buf (2, blk);
    juce::MidiBuffer midi;

    // 「へや」の用意はメッセージスレッドで走る。ヘッドレスの検査ではメッセージ
    // ループを回せない（modal loops 禁止ビルド）ので、もう一本の道である
    // prepareToPlay を叩いて用意させる。本番でもレート変更時に通る道。
    auto readyUp = [&] { proc->prepareToPlay (sr, blk); };

    auto runBlocks = [&] (int n, bool impulseFirst) -> float
    {
        float peak = 0.0f;
        for (int b = 0; b < n; ++b)
        {
            buf.clear();
            if (impulseFirst && b == 0) { buf.setSample (0, 0, 1.0f); buf.setSample (1, 0, 1.0f); }
            proc->processBlock (buf, midi);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < blk; ++i)
                    peak = std::max (peak, std::abs (buf.getSample (ch, i)));
        }
        return peak;
    };

    std::printf("\n== 2. 6部屋: 音が出る・部屋ごとに尾の長さが違う ==\n");
    // インパルス1発では出力が小さすぎて床と紛れる。0.5秒ノイズを鳴らして止め、
    // 「鳴り止んでから 0.5〜0.75 秒」の残り具合を測る。ここが部屋の個性そのもの。
    juce::Random rng (20260827);
    auto burstAndTail = [&] (double fromSec, double toSec) -> double
    {
        const int burst = (int) (sr * 0.5) / blk;
        const int total = (int) (sr * 2.5) / blk;
        double sum = 0.0; int cnt = 0;
        for (int b2 = 0; b2 < total; ++b2)
        {
            buf.clear();
            if (b2 < burst)
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < blk; ++i)
                        buf.setSample (ch, i, (rng.nextFloat() * 2.0f - 1.0f) * 0.25f);
            proc->processBlock (buf, midi);
            const double t = (double) b2 * blk / sr - 0.5;      // 鳴り止みからの秒
            if (t >= fromSec && t < toSec)
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < blk; ++i)
                    { const double v = buf.getSample (ch, i); sum += v * v; ++cnt; }
        }
        return cnt > 0 ? std::sqrt (sum / cnt) : 0.0;
    };

    double tailRms[6] = { 0, 0, 0, 0, 0, 0 };
    for (int room = 0; room < 6; ++room)
    {
        setParam ("rev_type", (float) (7 + room) / (float) (nTypes - 1));
        runBlocks (4, false);
        readyUp();
        runBlocks (24, false);           // 部屋の入れ替えフェードを終わらせる

        const double head = burstAndTail (0.00, 0.25);   // 鳴り止み直後
        tailRms[room]     = burstAndTail (0.50, 0.75);   // 半秒あと

        bool finite = true;
        for (int ch = 0; ch < 2 && finite; ++ch)
            for (int i = 0; i < blk; ++i)
                if (! std::isfinite (buf.getSample (ch, i))) { finite = false; break; }

        CHECK (finite && head > 1e-4 && head < 4.0,
               "%-22s 鳴り止み直後 %.5f / 0.5秒あと %.6f",
               heya::rooms()[(size_t) room].nameJa, head, tailRms[room]);
    }
    {
        // 全部屋が同じ尾なら「部屋を替えているつもりで替わっていない」。
        // 開発中まさにこれが起きた: reset() が入れ替え途中を取り消して固まっていた。
        CHECK (tailRms[2] * 20.0 < tailRms[4],
               "録音スタジオ %.6f << コンサートホール %.6f（20倍以上の差＝別の部屋だと分かる）",
               tailRms[2], tailRms[4]);
        CHECK (tailRms[1] < tailRms[4] && tailRms[3] < tailRms[4],
               "カラオケ箱・ライブハウス < コンサートホール（設計どおりの向き）");
    }

    std::printf("\n== 2b. 「ひびき」つまみがへやにも効くか ==\n");
    // ★この検査が無かったせいで、へやだけ revmix が掛からない不具合を出した。
    //   従来型は juce::dsp::Reverb の wetLevel が中で掛けるが、へやは tank を
    //   通らないので、掛ける人が誰もいなくなっていた。
    {
        setParam ("rev_type", 11.0f / (float) (nTypes - 1));   // コンサートホール
        runBlocks (4, false); readyUp(); runBlocks (24, false);

        setParam ("revmix", 0.80f);
        const double loud = burstAndTail (0.00, 0.25);
        setParam ("revmix", 0.10f);
        runBlocks (16, false);
        const double quiet = burstAndTail (0.00, 0.25);

        CHECK (quiet < loud * 0.5,
               "ひびき80%%→10%% で響きが減る（%.5f → %.5f）", loud, quiet);
        setParam ("revmix", 0.60f);
        runBlocks (8, false);
    }

    std::printf("\n== 2c. prepareToPlay だけで、へやが本当に効くか ==\n");
    // ★メッセージスレッドを一切回さずに確かめる。
    //   用意を非同期まかせにしていたころ、ここが false のままで
    //   **従来のタンク式が代わりに鳴っていた**。音は出るので
    //   「尾があるか」だけ見ても気づけない。
    //   なので**部屋ごとに違うか**を見る。タンク式が鳴っているなら
    //   どの部屋を選んでも defs[0] なので、まったく同じ音になる。
    {
        auto freshTail = [&] (int type) -> double
        {
            std::unique_ptr<juce::AudioProcessor> f2 (createPluginFilter());
            f2->setPlayConfigDetails (2, 2, sr, blk);
            f2->prepareToPlay (sr, blk);          // これ**だけ**。async は回さない
            for (auto* prm : f2->getParameters())
                if (auto* w = dynamic_cast<juce::AudioProcessorParameterWithID*> (prm))
                {
                    if (w->paramID == "revon")    w->setValueNotifyingHost (1.0f);
                    if (w->paramID == "revmix")   w->setValueNotifyingHost (0.60f);
                    if (w->paramID == "revsize")  w->setValueNotifyingHost (0.80f);
                    if (w->paramID == "rev_type") w->setValueNotifyingHost ((float) type / (float) (nTypes - 1));
                }
            juce::AudioBuffer<float> b2 (2, blk); juce::MidiBuffer m2;
            juce::Random rg (7);
            const int burst = (int) (sr * 0.5) / blk, total = (int) (sr * 2.0) / blk;
            double sum = 0.0; int cnt = 0;
            for (int i2 = 0; i2 < total; ++i2)
            {
                b2.clear();
                if (i2 < burst)
                    for (int ch = 0; ch < 2; ++ch) for (int n = 0; n < blk; ++n)
                        b2.setSample (ch, n, (rg.nextFloat() * 2.0f - 1.0f) * 0.25f);
                f2->processBlock (b2, m2);
                const double t = (double) i2 * blk / sr - 0.5;
                if (t >= 0.50 && t < 0.75)
                    for (int n = 0; n < blk; ++n) { const double v = b2.getSample (0, n); sum += v * v; ++cnt; }
            }
            f2->releaseResources();
            return cnt ? std::sqrt (sum / cnt) : 0.0;
        };
        const double studio = freshTail (9);    // へや：録音スタジオ（ほぼ無響）
        const double hall   = freshTail (11);   // へや：コンサートホール（長い）
        CHECK (hall > studio * 20.0,
               "作りたて＋prepareToPlay だけで、スタジオ %.6f << ホール %.6f "
               "（非同期を回さなくても、へやが効いている）", studio, hall);
    }

    std::printf("\n== 3. 申告遅延（へやは追加遅延0が売り） ==\n");
    setParam ("rev_type", 11.0f / (float) (nTypes - 1));   // コンサートホール
    runBlocks (4, false);
    readyUp();
    runBlocks (8, false);
    CHECK (proc->getLatencySamples() == 0,
           "へやを選んでも申告遅延 %d サンプル（0が正）", proc->getLatencySamples());

    std::printf("\n== 4. processBlock の中で確保しないか ==\n");
    // 先に全部屋を用意させてから測る（用意そのものは確保してよい）
    for (int room = 0; room < 6; ++room)
    {
        setParam ("rev_type", (float) (7 + room) / (float) (nTypes - 1));
        runBlocks (2, false);
        readyUp();
        runBlocks (2, false);
    }
    // ★パラメータの設定(setValueNotifyingHost)はホスト通知を伴うので確保する。
    //   測りたいのは processBlock の中だけ。設定は監視の外でやる。
    for (int room = 0; room < 6; ++room)
    {
        setParam ("rev_type", (float) (7 + room) / (float) (nTypes - 1));
        gAllocs.store (0); gWatch.store (true);
        runBlocks (40, false);
        gWatch.store (false);
        if (gAllocs.load() != 0)
            std::printf("      部屋%d で %lld 回\n", room, gAllocs.load());
        if (room == 0) allocTotal = 0;
        allocTotal += gAllocs.load();
    }
    CHECK (allocTotal == 0,
           "6部屋ぶん240ブロック回して processBlock 内の確保 %lld 回（0が正）", allocTotal);

    std::printf("\n== 5. 従来の7種類も壊れていないか ==\n");
    bool allOk = true;
    for (int type = 0; type < 7; ++type)
    {
        setParam ("rev_type", (float) type / (float) (nTypes - 1));
        const float peak = runBlocks (60, true);
        if (! (peak > 1e-5f && peak < 8.0f)) { allOk = false;
            std::printf("      種類%d のピーク %.3f\n", type, peak); }
    }
    CHECK (allOk, "従来の7種類すべて音が出て暴れない");

    std::printf("\n== 6. OFF後に古い歌声が戻らない ==\n");
    for (int type : { 3, 5, 6, 11 })
    {
        setParam ("rev_type", (float) type / (float) (nTypes - 1));
        setParam ("revon", 1.0f); setParam ("revmix", 0.70f);
        readyUp(); runBlocks (24, false);
        for (int b = 0; b < 100; ++b)
        {
            for (int n = 0; n < blk; ++n)
                buf.setSample (0, n, 0.2f * std::sin ((b * blk + n) * 0.032f));
            buf.copyFrom (1, 0, buf, 0, 0, blk);
            proc->processBlock (buf, midi);
        }
        setParam ("revon", 0.0f);
        gAllocs.store (0); gWatch.store (true);
        runBlocks (80, false);
        gWatch.store (false);
        CHECK (gAllocs.load() == 0, "種類%d: OFF処理のメモリ確保 %lld 回", type, gAllocs.load());
        setParam ("revon", 1.0f);
        const float ghost = runBlocks (100, false);
        CHECK (ghost < 1e-6f, "種類%d: 再ON後の残留ピーク %.2e", type, ghost);
    }

    std::printf("\n== 7. 空間モジュールのOFFでも履歴を消去 ==\n");
    setParam ("rev_type", 11.0f / (float) (nTypes - 1));
    readyUp(); runBlocks (24, false); runBlocks (8, true);
    setParam ("mod_hirogari", 0.0f);
    runBlocks (100, false);
    setParam ("mod_hirogari", 1.0f);
    const float moduleGhost = runBlocks (100, false);
    CHECK (moduleGhost < 1e-6f, "モジュール再ON後の残留ピーク %.2e", moduleGhost);

    proc->releaseResources();
    std::printf("\n=======================================\n");
    std::printf(gFail == 0 ? "  dsp_heyawire: ぜんぶ PASS\n" : "  dsp_heyawire: %d 件 FAIL\n", gFail);
    std::printf("=======================================\n");
    return gFail == 0 ? 0 : 1;
}
