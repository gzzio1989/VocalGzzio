#include "TestPaths.h"
// dsp_belt.cpp — v2.12.0 張り保護(§6-2)の検証。プラグイン本体で実測。
//
//  「声を張って歌ったときの抜けが悪い」:
//   ・なめらか … 張った声の 2-4kHz の倍音の束(声の通り道)を「出っ張り」と
//     みなして削っていた → 張った直後は削りを最大50%緩める
//   ・サ行おさえ … しきい値が絶対値だったので、張る=全体が大きくなるだけで
//     高域を削っていた → しきい値を声全体の音量に追従させる
//
//  [1] 静かな声→張った声に切り替えた直後、3kHz帯の保持が悪化しないこと
//  [2] 張っても 6-8kHz(サ行おさえの棚)が余計に削れないこと
//  [3] サ行の割合が増えたときは、ちゃんと削れること(誤って無効化していない)
#include "PluginProcessor.h"
#include <cstdio>
#include <cmath>

static int gFail = 0;
#define CHECK(cond, ...) do { \
    if (cond) { std::printf("  PASS: " __VA_ARGS__); std::printf("\n"); } \
    else      { std::printf("  FAIL: " __VA_ARGS__); std::printf("\n"); ++gFail; } } while (0)

static void setP (VocalGzzioProcessor& p, const char* id, float v)
{
    if (auto* prm = p.apvts.getParameter (id))
        prm->setValueNotifyingHost (p.apvts.getParameterRange (id).convertTo0to1 (v));
}

static void flatten (VocalGzzioProcessor& p)
{
    // dsp_srcmode と同じ「全部平ら」。既定は mud=-3dB, harsh=-1.5dB, 息やことば等も
    // 入っているため、これを怠ると測定が汚れる(このテストの初版は+4dBの謎の
    // 増幅を「削れていない」と誤読した。正体は既定ONの機能群だった)。
    for (const char* id : { "comp1", "comp2", "ride_amt", "res_amt", "deess",
                            "seq_amount", "drive", "sustain", "cons_amt", "br_amt",
                            "ring", "hum_amt", "denoise", "makeup", "prox_amt",
                            "doubler", "width", "delay", "revmix" })
        setP (p, id, 0.0f);
    setP (p, "mud", 0.0f);
    setP (p, "harsh", 0.0f);
    setP (p, "air", 0.0f);
    setP (p, "presence", 0.0f);
    setP (p, "seq_on", 0.0f);
    setP (p, "revon", 0.0f);
    setP (p, "dly_on", 0.0f);
    setP (p, "gate", -80.0f);
}

// 220Hz の倍音列 + 3kHz の束(歌手のフォルマント)。belt で束が濃くなる。
struct Voice
{
    double ph = 0.0;
    float sh1 = 0, sh2 = 0, sl1 = 0, sl2 = 0;  // サ行用: HP×2(5k) + LP×2(8k)
    float sib (unsigned& s, float amp)
    {
        s = s * 1664525u + 1013904223u;
        const float w = (float)((int)(s>>9)-4194304)/4194304.0f;
        const float a = 0.55f, b = 0.32f;
        sh1 = a * (sh1 + w); const float y1 = w - sh1;
        sh2 = a * (sh2 + y1); float y = y1 - sh2;      // HP側
        sl1 = b * sl1 + (1-b) * y;                      // LP側(9k超を落とす)
        sl2 = b * sl2 + (1-b) * sl1;
        return sl2 * amp * 12.0f;   // 帯域制限でRMSが落ちるぶんを補う
    }
    float next (double sr, float amp, float formantBoost)
    {
        double v = 0.0;
        for (int h = 1; h <= 40; ++h)
        {
            const double f = 220.0 * h;
            if (f > 9000.0) break;
            double a = 1.0 / h;                                    // 自然な傾斜
            if (f > 2200.0 && f < 4200.0) a *= formantBoost;       // 声の通り道
            if (f > 5800.0 && f < 8200.0) a *= 0.35;               // サ行帯(常に少し)
            v += a * std::sin (ph * h);
        }
        ph += 2.0 * 3.14159265358979 * 220.0 / sr;
        return (float) (v * amp * 0.12);
    }
};

// 帯域エネルギー測定(2次バンドパスもどき: HP×2 と LP×2 の直列)
struct Band
{
    float ah, bh; float h1=0,h2=0,l1=0,l2=0;
    Band (double lo, double hi, double sr)
    {
        ah = (float) std::exp (-2.0*3.141592653589*lo/sr);
        bh = (float) std::exp (-2.0*3.141592653589*hi/sr);
    }
    float run (float x)
    {
        h1 = ah*h1 + (1-ah)*x; float y = x - h1;      // HP(lo)
        h2 = ah*h2 + (1-ah)*y; y = y - h2;
        l1 = bh*l1 + (1-bh)*y; y = l1;                 // LP(hi)
        l2 = bh*l2 + (1-bh)*y; return l2;
    }
};

int main()
{
    juce::ScopedJuceInitialiser_GUI init;

    auto autosave = gz::dataDirectory().getChildFile ("autosave.xml");
    auto backup = autosave.getSiblingFile ("autosave.bak_belt");
    autosave.getParentDirectory().createDirectory();
    if (autosave.existsAsFile()) autosave.moveFileTo (backup);
    struct Restore { juce::File a, b;
        ~Restore() { a.deleteFile(); if (b.existsAsFile()) b.moveFileTo (a); } } restore { autosave, backup };
    autosave.deleteFile();

    std::printf ("張り保護(張った声の抜け)の検証\n\n");

    const double sr = 44100.0; const int bs = 128;

    // 測定を1回ぶん回す: 4秒ふつう(amp, boost=2) → 1.5秒張る(amp*6, boost=3)
    // 返り値: {ふつう区間の帯域比dB, 張り区間の帯域比dB} を2帯域ぶん
    auto measure = [&] (float deessAmt, float resAmt, float sibExtra)
    {
        VocalGzzioProcessor p;
        flatten (p);                                     // まっさらにしてから
        setP (p, "res_amt", resAmt); setP (p, "deess", deessAmt);
        setP (p, "dn_on", 0);
        p.prepareToPlay (sr, bs);

        juce::AudioBuffer<float> buf (2, bs); juce::MidiBuffer midi;
        Voice vo; unsigned s = 3u;
        Band inMid (2200, 4200, sr), outMid (2200, 4200, sr);
        Band inHi (5800, 8200, sr),  outHi (5800, 8200, sr);
        Band inRef (400, 1500, sr),  outRef (400, 1500, sr);
        double eIM=0,eOM=0,eIH=0,eOH=0,eIR=0,eOR=0;             // 張り区間
        double qIM=0,qOM=0,qIH=0,qOH=0,qIR=0,qOR=0;             // ふつう区間

        const int nQ = (int)(4.0*sr), nB = (int)(1.5*sr), total = nQ+nB;
        for (int done = 0; done < total; done += bs)
        {
            auto* L = buf.getWritePointer (0); auto* R = buf.getWritePointer (1);
            float inS[128];
            for (int i = 0; i < bs; ++i)
            {
                const int t = done + i;
                const bool belt = t >= nQ;
                float v = vo.next (sr, belt ? 0.30f : 0.05f, belt ? 3.0f : 2.0f);
                // サ行成分は6kHz寄りの帯域ノイズで足す(白色だと全帯域が
                // 同時に上がって「サ行の割合」が変わらず、テストにならない)
                v += vo.sib (s, (belt ? 0.30f : 0.05f) * 0.06f * sibExtra);
                inS[i] = v; L[i] = v; R[i] = v;
            }
            p.processBlock (buf, midi);
            for (int i = 0; i < bs; ++i)
            {
                const int t = done + i;
                const float o = buf.getSample (0, i);
                const float im=inMid.run(inS[i]), om=outMid.run(o);
                const float ih=inHi.run(inS[i]),  oh=outHi.run(o);
                const float ir=inRef.run(inS[i]), orf=outRef.run(o);
                if (t >= (int)(1.5*sr) && t < nQ)                          // ふつう(整定後)
                { qIM+=im*im; qOM+=om*om; qIH+=ih*ih; qOH+=oh*oh; qIR+=ir*ir; qOR+=orf*orf; }
                if (t >= nQ + 2205 && t < nQ + (int)(1.2*sr))              // 張り(50ms過渡除き)
                { eIM+=im*im; eOM+=om*om; eIH+=ih*ih; eOH+=oh*oh; eIR+=ir*ir; eOR+=orf*orf; }
            }
        }
        auto db = [] (double o, double i) { return 10.0 * std::log10 (o / juce::jmax (1e-20, i)); };
        struct R4 { double qMid, bMid, qHi, bHi; };
        // 基準帯(400-1500)に対する相対で見る＝全体音量の影響を消す
        return R4 { db(qOM,qIM) - db(qOR,qIR), db(eOM,eIM) - db(eOR,eIR),
                    db(qOH,qIH) - db(qOR,qIR), db(eOH,eIH) - db(eOR,eIR) };
    };

    std::printf ("[1] なめらか60%%: 張った直後の 2.2-4.2kHz\n");
    {
        const auto r = measure (0.0f, 60.0f, 1.0f);
        std::printf ("      ふつう %.2f dB / 張り %.2f dB\n", r.qMid, r.bMid);
        CHECK (r.bMid > r.qMid - 1.5, "張っても悪化 %.2f dB 以内 (<1.5dB)", r.qMid - r.bMid);
    }

    std::printf ("\n[2] サ行おさえ40%%: 張ったときの 5.8-8.2kHz\n");
    {
        const auto r = measure (40.0f, 0.0f, 1.0f);
        std::printf ("      ふつう %.2f dB / 張り %.2f dB\n", r.qHi, r.bHi);
        CHECK (r.bHi > r.qHi - 2.0, "張っても悪化 %.2f dB 以内 (<2.0dB)", r.qHi - r.bHi);
    }

    std::printf ("\n[3] サ行の割合そのものが増えたら、ちゃんと削れる\n");
    {
        const auto a = measure (40.0f, 0.0f, 1.0f);
        const auto b = measure (40.0f, 0.0f, 6.0f);       // サ行成分を6倍
        std::printf ("      ふつうのサ行 %.2f dB / 強いサ行 %.2f dB\n", a.bHi, b.bHi);
        // 段階の細かさは dsp_autoset[3](サ行割合→deess量)が主検査。ここでは
        // 相対しきい値化で「多いサ行ほど多く削る」の向きが残っていることを見る。
        CHECK (b.bHi < a.bHi - 0.5, "強いサ行は %.2f dB 多く削れている (>0.5dB)", a.bHi - b.bHi);
    }

    std::printf (gFail ? "\n== %d 件 FAIL ==\n" : "\n== 全テスト PASS ==\n", gFail);
    return gFail ? 1 : 0;
}
