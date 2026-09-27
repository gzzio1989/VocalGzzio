#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <cmath>

//==============================================================================
// ModuleChain — v3.0 の土台。「効果を9つの箱にまとめて、箱ごとに切れるようにする」
//
//  いちばん最初にいただいた指摘がこれでした:
//    「各エフェクトのオンオフや起動順を選べるようにしてほしい。
//      De-noiseだけ使いたいのに他がかかる」
//
//  いまの processBlock は 60 個のツマミが**一本の直列**につながっていて、
//  ユーザーからは順番も切れ目も見えません。ここでその直列に**切れ目**を入れます。
//
//  ---- この版(v3.0-a)でやること / やらないこと --------------------------------
//
//  やる: **音としての完全な素通し**。OFF のモジュールは、そこを通る前と後で
//        波形が 1 サンプルも変わりません（テストで一致を確認しています）。
//        切り替えは 10 ms のクロスフェードなので、プチッと言いません。
//
//  やらない: **処理そのものを飛ばすこと**は、まだしていません。
//        OFF でもモジュールは走っていて、出た音を捨てています。理由は2つ。
//         (1) 中の状態（フィルタ・包絡・ディレイライン）が生きたままなので、
//             ONに戻した瞬間から正しい音が出る。飛ばすと数十ms「立ち上がり」が要る。
//         (2) いまの processBlock は 1700 行の一本道で、切り出しは v3.0-b の仕事。
//             音の入口だけ先に用意して、中身の引っ越しはテストを足しながらやる。
//        つまり **CPU はまだ減りません**。減らすのは v3.0-b（関数への切り出しと
//        並べ替えを入れるとき）です。負荷は 99%点 6.7% と余裕があるので、
//        先に「音が変わらないこと」を確実にする順番を選びました。
//
//  ---- おすすめ順について ------------------------------------------------------
//
//  設計書 §2-1 の表の並びと、**実際のコードの並びは違いました**（読み直して発覚）。
//  コードでは「へんしん(ボイス変換)」がノイズ除去のすぐ後、「くち音おさえ」は
//  その後ろにあります。設計書の表の順に並べ替えると、**上げた瞬間に全員の音が
//  変わります**。それは受け入れられないので、
//    **おすすめ順 = いまのコードの順** と定義し直しました。
//  並べ替え（じぶんで順）は v3.0-b で、この定義の上に足します。
//==============================================================================
namespace gz
{

struct ModuleChain
{
    // 切れるモジュールは8つ。「マイクから(入)」と「しあげ(出)」は固定で、
    // ここには入れない（切ると音が出なくなる／音量が跳ねるため）。
    enum Id
    {
        Souji = 0,   // 1 おそうじ    ゲート・ノイズ除去・ポップ・リップ
        Henshin,     // 2 へんしん    ボイス変換・ユニゾン・ハモリ・ピッチ補正・こぶし
        Totonoe,     // 3 ととのえ    ローカット・こもり・キンキン・なめらか
        Soroe,       // 4 音量そろえ  圧縮1・2・SmartEQ・音量キープ
        Sagyo,       // 5 サ行おさえ  ディエッサー
        Neiro,       // 6 音色づくり  ことば・ヌケ感・キラキラ・息・艶・あたたかみ・のび
        Chara,       // 7 キャラ声    ロボ声・メガホン・エモ・サビリフト
        Hirogari,    // 8 ひろがり    かさね・ひろがり・コーラス・やまびこ・ひびき
        Count
    };

    // APVTS のパラメータID（自動化・保存に載る）
    static const char* paramId (int m) noexcept
    {
        static const char* ids[Count] = { "mod_souji", "mod_henshin", "mod_totonoe",
                                          "mod_soroe", "mod_sagyo",   "mod_neiro",
                                          "mod_chara", "mod_hirogari" };
        return ids[m];
    }

    // 画面に出す名前（日本語 / 英語）。ここを正本にして、画面側では持たない。
    static const char* jpName (int m) noexcept
    {
        static const char* nm[Count] = { "\xe3\x81\x8a\xe3\x81\x9d\xe3\x81\x86\xe3\x81\x98",           // おそうじ
                                         "\xe3\x81\xb8\xe3\x82\x93\xe3\x81\x97\xe3\x82\x93",           // へんしん
                                         "\xe3\x81\xa8\xe3\x81\xa8\xe3\x81\xae\xe3\x81\x88",           // ととのえ
                                         "\xe9\x9f\xb3\xe9\x87\x8f\xe3\x81\x9d\xe3\x82\x8d\xe3\x81\x88",   // 音量そろえ
                                         "\xe3\x82\xb5\xe8\xa1\x8c\xe3\x81\x8a\xe3\x81\x95\xe3\x81\x88",   // サ行おさえ
                                         "\xe9\x9f\xb3\xe8\x89\xb2\xe3\x81\xa5\xe3\x81\x8f\xe3\x82\x8a",   // 音色づくり
                                         "\xe3\x82\xad\xe3\x83\xa3\xe3\x83\xa9\xe5\xa3\xb0",           // キャラ声
                                         "\xe3\x81\xb2\xe3\x82\x8d\xe3\x81\x8c\xe3\x82\x8a" };         // ひろがり
        return nm[m];
    }
    // 1行の説明（カードに出す）。「何をする箱か」がひと目で分かる長さに。
    static const char* jpNote (int m) noexcept
    {
        static const char* nm[Count] = {
            "\xe3\x83\x8e\xe3\x82\xa4\xe3\x82\xba\xe3\x81\xa8\xe9\x9b\x91\xe9\x9f\xb3\xe3\x82\x92\xe3\x81\xa8\xe3\x82\x8b",
            "\xe5\xa3\xb0\xe3\x82\x92\xe5\xa4\x89\xe3\x81\x88\xe3\x82\x8b\xe3\x83\xbb\xe3\x83\x8f\xe3\x83\xa2\xe3\x82\x8b",
            "\xe8\x80\xb3\xe3\x81\x96\xe3\x82\x8f\xe3\x82\x8a\xe3\x81\xaa\xe5\xb8\xaf\xe3\x82\x92\xe3\x81\xaa\xe3\x82\x89\xe3\x81\x99",
            "\xe5\xa4\xa7\xe5\xb0\x8f\xe3\x81\xae\xe5\xb7\xae\xe3\x82\x92\xe3\x81\x9d\xe3\x82\x8d\xe3\x81\x88\xe3\x82\x8b",
            "\xe3\x82\xb5\xe8\xa1\x8c\xe3\x81\xae\xe5\x88\xba\xe3\x81\x95\xe3\x82\x8a\xe3\x82\x92\xe3\x81\x8a\xe3\x81\x95\xe3\x81\x88\xe3\x82\x8b",
            "\xe6\x98\x8e\xe3\x82\x8b\xe3\x81\x95\xe3\x83\xbb\xe5\xa4\xaa\xe3\x81\x95\xe3\x82\x92\xe3\x81\xa4\xe3\x81\x8f\xe3\x82\x8b",
            "\xe3\x83\xad\xe3\x83\x9c\xe5\xa3\xb0\xe3\x83\xbb\xe3\x83\xa1\xe3\x82\xac\xe3\x83\x9b\xe3\x83\xb3\xe3\x81\xaa\xe3\x81\xa9",
            "\xe5\xba\x83\xe3\x81\x8c\xe3\x82\x8a\xe3\x81\xa8\xe6\xae\x8b\xe9\x9f\xbf\xe3\x82\x92\xe3\x81\xa4\xe3\x81\x91\xe3\x82\x8b",
        };
        return nm[m];
    }
    static const char* enNote (int m) noexcept
    {
        static const char* nm[Count] = {
            "Removes noise and hiss",
            "Changes and harmonises the voice",
            "Smooths harsh bands",
            "Evens out loud and quiet",
            "Tames sibilance",
            "Shapes brightness and body",
            "Robot, megaphone and more",
            "Adds width and reverb",
        };
        return nm[m];
    }
    static const char* enName (int m) noexcept
    {
        static const char* nm[Count] = { "Clean up", "Transform", "Shape", "Level",
                                         "De-ess", "Tone", "Character", "Space" };
        return nm[m];
    }

    static const char* paramName (int m) noexcept
    {
        static const char* nm[Count] = { "Module Clean Up", "Module Transform",
                                         "Module Shape",    "Module Level",
                                         "Module De-Ess",   "Module Tone",
                                         "Module Character","Module Space" };
        return nm[m];
    }

    //--------------------------------------------------------------------------
    void prepare (double sampleRate, int maxBlock, int numCh)
    {
        // 10 ms で渡し切る。これより短いとプチッと言い、長いと「効いてない?」と
        // 思われる。実測で 10 ms が「押した瞬間に変わったが、段差はない」だった。
        rampSamples = juce::jmax (1, (int) (sampleRate * 0.010));
        step        = 1.0f / (float) rampSamples;
        dry.setSize (juce::jmax (1, numCh), juce::jmax (1, maxBlock), false, false, true);
        dry.clear();
        for (int m = 0; m < Count; ++m) { gain[m] = target[m]; g0[m] = gain[m]; }
        sr      = juce::jmax (8000.0, sampleRate);
        lastLen = 0;
        for (int m = 0; m < Count; ++m)
        {
            work[m].store (0.0f, std::memory_order_relaxed);
            accDiff[m] = accRef[m] = 0.0f; accN[m] = 0; pN[m] = 0;
        }
    }

    // ブロックの先頭で1回だけ呼ぶ。ここで「このブロックの始まりと終わりの値」を
    // 決めてしまうので、同じモジュールが2か所に分かれていても足並みが揃う。
    void beginBlock (int numSamples) noexcept
    {
        blockLen = juce::jmax (1, numSamples);
        if (blockLen != lastLen)                      // 下がりの速さ(約0.3秒)
        {
            lastLen = blockLen;
            relCoef = 1.0f - std::exp (-(float) blockLen / (float) (sr * 0.30));
        }
        finishMeasure();
        for (int m = 0; m < Count; ++m)
        {
            g0[m] = gain[m];
            const float d = target[m] - gain[m];
            const float mx = step * (float) blockLen;
            gain[m] = (std::abs (d) <= mx) ? target[m]
                                           : gain[m] + (d > 0.0f ? mx : -mx);
            g1[m] = gain[m];
        }
    }

    void setOn (int m, bool on) noexcept { target[m] = on ? 1.0f : 0.0f; }

    bool isFull (int m) const noexcept { return g0[m] >= 1.0f && g1[m] >= 1.0f; }
    bool isOff  (int m) const noexcept { return g0[m] <= 0.0f && g1[m] <= 0.0f; }
    bool moving (int m) const noexcept { return ! isFull (m) && ! isOff (m); }

    // 表示用（ヘッダのカードで「いま渡している最中」を出せるように）
    float displayGain (int m) const noexcept { return g1[m]; }

    //--------------------------------------------------------------------------
    // v3.0-c 「はたらき量」— カードのミニメーターに出す 0.0〜1.0
    //
    //  ここは自分でも一度まちがえた所なので、考えた筋道ごと残す。
    //
    //  はじめは「モジュールごとに専用のメーターを作る」つもりだった。だが実際に
    //  測っている値があるのは 8つのうち 5つ（ノイズ除去・圧縮量・サ行・なめらか・
    //  ことば）だけで、へんしん・キャラ声・ひろがり には無い。
    //  **5つは本物・3つは飾り** になるので、それは作らないと決めた。
    //
    //  代わりに、8つ**全部に共通で意味のある**量をひとつ選んだ:
    //      「この箱を通す前と後で、音がどれだけ変わったか」
    //  これは「いまこの工程が効いているか」という、カードを見る人が知りたい
    //  ことそのもので、しかも 8つとも同じ物差しなので**上下で比べられる**。
    //
    //  測りかた: save/restore の間で、前後の波形を 32点だけ拾って
    //      比 = √(Σ(あと−まえ)² / Σ最大²)
    //  を出し、−45dB→0 / 0dB→1 に写す。全サンプル舐めるのではなく間引くのは、
    //  メーターのために音の処理を重くしないため（1ブロックあたり約1000演算）。
    //
    //  注意していること:
    //   * OFF のときは restore が dry を書き戻すので差は 0 → メーターも 0。
    //     「切ってあるのにバーが動く」ことは起きない。
    //   * 完全OFF の区間は processBlock 側で丸ごと飛ばされ save/restore すら
    //     呼ばれない。そのときは beginBlock でゆっくり 0 へ落とす。
    //   * おそうじ・音量そろえ・音色づくりは**1ブロックに2か所**ある。
    //     上書きせず足し込み、次の beginBlock でまとめて1つの値にする。
    //   * 立ち上がりは即時・下がりは約0.3秒。サ行のような一瞬の仕事も見える。
    float workAmount (int m) const noexcept
    {
        return work[m].load (std::memory_order_relaxed);
    }

    //--------------------------------------------------------------------------
    // save/restore は必ず対で使う。間に**元のコードをそのまま**置く。
    //   mods.save (Id::Totonoe, buffer);
    //   ... 既存の処理（そのまま） ...
    //   mods.restore (Id::Totonoe, buffer);
    // ONのときは両方とも即 return するので、いつもの音は 1 命令も遠回りしない。
    void save (int m, const juce::AudioBuffer<float>& buf) noexcept
    {
        probeIn (m, buf);                             // v3.0-c はたらき量（前）
        saveImpl (m, buf);
    }

    void restore (int m, juce::AudioBuffer<float>& buf) noexcept
    {
        restoreImpl (m, buf);
        probeOut (m, buf);                            // ★渡し終わった**後**の音で測る。
                                                      //  前で測ると、切りかけの途中でも
                                                      //  「全開で効いている」と出てしまう。
    }

    // 「へんしん」専用。ここだけ save/restore を使わない（遅延を持つのでスイッチ側で
    // 切る）ので、**音には触らず測るだけ**の対を用意する。
    // 断り: へんしんは約16msの遅延を持つため、ボイス変換やハモリが入っている間は
    //       「音を変えている量」が大きめに出る。ずらすこと自体が音を変えているので
    //       嘘ではないが、他の7つと同じ細かさで比べる物差しではない。
    void probeOnlyBegin (int m, const juce::AudioBuffer<float>& buf) noexcept { probeIn  (m, buf); }
    void probeOnlyEnd   (int m, const juce::AudioBuffer<float>& buf) noexcept { probeOut (m, buf); }

private:
    void saveImpl (int m, const juce::AudioBuffer<float>& buf) noexcept
    {
        if (isFull (m)) return;                       // 通常運転: 何もしない
        const int n  = juce::jmin (blockLen, buf.getNumSamples(), dry.getNumSamples());
        const int ch = juce::jmin (buf.getNumChannels(), dry.getNumChannels());
        for (int c = 0; c < ch; ++c)
            juce::FloatVectorOperations::copy (dry.getWritePointer (c),
                                               buf.getReadPointer (c), n);
        held = m;                                     // 対になっているかの自己点検
    }

    void restoreImpl (int m, juce::AudioBuffer<float>& buf) noexcept
    {
        if (isFull (m)) return;
        jassert (held == m);                          // save と restore がずれていたら止める
        held = -1;
        const int n  = juce::jmin (blockLen, buf.getNumSamples(), dry.getNumSamples());
        const int ch = juce::jmin (buf.getNumChannels(), dry.getNumChannels());

        if (isOff (m))                                // 完全OFF: 1サンプルも変えない
        {
            for (int c = 0; c < ch; ++c)
                juce::FloatVectorOperations::copy (buf.getWritePointer (c),
                                                   dry.getReadPointer (c), n);
            return;
        }

        // 渡し中: dry → wet を直線で渡す（ブロックをまたいでも連続する）
        const float a = g0[m];
        const float inc = (g1[m] - g0[m]) / (float) juce::jmax (1, n);
        for (int c = 0; c < ch; ++c)
        {
            float*       w = buf.getWritePointer (c);
            const float* d = dry.getReadPointer (c);
            float        g = a;
            for (int i = 0; i < n; ++i, g += inc)
                w[i] = d[i] + g * (w[i] - d[i]);
        }
    }

    //--------------------------------------------------------------------------
    // 「はたらき量」の測り: 前後を 32点だけ拾って比べる
    enum { kProbe = 32 };

    void probeIn (int m, const juce::AudioBuffer<float>& buf) noexcept
    {
        const int n = juce::jmin (blockLen, buf.getNumSamples());
        const int ch = juce::jmin (2, buf.getNumChannels());
        if (n <= 0 || ch <= 0) { pN[m] = 0; return; }
        const int stride = juce::jmax (1, n / (int) kProbe);
        int k = 0;
        float s = 0.0f;
        for (int i = 0; i < n && k < (int) kProbe; i += stride, ++k)
            for (int c = 0; c < ch; ++c)
            {
                const float v = buf.getReadPointer (c)[i];
                pIn[m][c][k] = v;
                s += v * v;
            }
        pN[m] = k; pCh[m] = ch; pStride[m] = stride; pRef[m] = s;
    }

    void probeOut (int m, const juce::AudioBuffer<float>& buf) noexcept
    {
        const int k = pN[m];
        if (k <= 0) return;
        pN[m] = 0;                                    // 対で1回だけ使う
        const int n  = juce::jmin (blockLen, buf.getNumSamples());
        const int ch = juce::jmin (pCh[m], buf.getNumChannels());
        const int stride = pStride[m];
        float dS = 0.0f, oS = 0.0f;
        int j = 0;
        for (int i = 0; i < n && j < k; i += stride, ++j)
            for (int c = 0; c < ch; ++c)
            {
                const float o = buf.getReadPointer (c)[i];
                const float d = o - pIn[m][c][j];
                dS += d * d; oS += o * o;
            }
        accDiff[m] += dS;
        accRef[m]  += juce::jmax (pRef[m], oS);       // 前と後の大きいほう＝基準
        accN[m]    += j * ch;
    }

    // ブロックの区切りで、そのブロックぶんを1つの数にする（2か所あるモジュールも合算）
    void finishMeasure() noexcept
    {
        for (int m = 0; m < Count; ++m)
        {
            // ★切ってあるなら、下がるのを待たずに**その場で 0**。
            //  最初はゆっくり下げていたが、切った直後 1秒ちかくバーが残った。
            //  切れている工程のメーターが動いて見えるのは、ただの嘘になる。
            //  （テストが 0.04〜0.15 を拾って気づいた）
            if (isOff (m))
            {
                work[m].store (0.0f, std::memory_order_relaxed);
                accDiff[m] = accRef[m] = 0.0f; accN[m] = 0;
                continue;
            }
            float w = 0.0f;
            // 平均パワーが −70 dBFS 未満＝ほぼ無音。無音で動くメーターは嘘になる。
            if (accN[m] > 0 && accRef[m] / (float) accN[m] > 1.0e-7f)
            {
                const float ratio = std::sqrt (accDiff[m] / juce::jmax (accRef[m], 1.0e-20f));
                const float db    = 20.0f * std::log10 (juce::jmax (ratio, 1.0e-6f));
                w = juce::jlimit (0.0f, 1.0f, (db + 45.0f) / 45.0f);
            }
            const float cur = work[m].load (std::memory_order_relaxed);
            work[m].store (w > cur ? w : cur + relCoef * (w - cur),
                           std::memory_order_relaxed);
            accDiff[m] = accRef[m] = 0.0f; accN[m] = 0;
        }
    }

    juce::AudioBuffer<float> dry;
    float gain[Count]   { 1,1,1,1,1,1,1,1 };
    float target[Count] { 1,1,1,1,1,1,1,1 };
    float g0[Count]     { 1,1,1,1,1,1,1,1 };
    float g1[Count]     { 1,1,1,1,1,1,1,1 };
    int   rampSamples { 480 };
    float step { 1.0f / 480.0f };
    int   blockLen { 0 };
    int   held { -1 };

    // はたらき量（GUI から読むので atomic。音側は relaxed で書くだけ）
    std::atomic<float> work[Count] { };
    float pIn[Count][2][kProbe] { };
    int   pN[Count] { }, pCh[Count] { }, pStride[Count] { };
    float pRef[Count] { };
    float accDiff[Count] { }, accRef[Count] { };
    int   accN[Count] { };
    double sr { 44100.0 };
    int    lastLen { 0 };
    float  relCoef { 0.02f };
};

} // namespace gz
