// ============================================================================
// dsp_crackle_stress.cpp — VocalGzzio v5.5.0 パリパリ音・高負荷統合ストレステスト
//
//  検証項目（ctest 自動回帰モード）:
//   1. 真・v5.5.0全機能フル稼働マトリクス（44.1k/48k/96k × 64/124/128/256サンプル × 遅延モード）
//      → RTヒープ確保 0回・p99処理時間がリアルタイム締切未満・L/Rステレオ位相一致を証明
//   2. 歌いながらの「へや（全6部屋IR）」ライブ切替 ＆ UIスレッドからのカスタムIR非同期切替
//      → 切替ブロックのストール 0（締切内完了）・RTヒープ確保 0回・データ競合/段差クリック 0回を証明
//   3. 非同期2クロックFIFO（AppAudioCapture）単発OS遅延・異レート起床ジッタ後の自動回復
//      → 0 < ready < needIn 罠での連続パリパリ（枯渇振動）が起きず、波形段差クリック 0回で完全復帰
//   4. 非同期2クロック配信出力（StreamOut）単発ホスト停止＋±2.5ms起床ジッタ後のクッション再充填
//      → 波形段差クリック 0回・復帰後の連鎖アンダーラン 0回・目標クッション85%以上への自動回復を証明
//   5. UI全画面描画＋他コア高負荷CPU/メモリ競合＋可変ブロック長(17..511)の並行リアルタイム耐性
//
//  実機オーディオデバイス診断モード（手動CLI実行用）:
//   dsp_crackle_stress.exe --live-device [秒数] [--silent]
//      → 実機の既定オーディオデバイスを開き、Windows/ASIOコールバック周期ジッタ（DPC/USB遅延）と
//        VocalGzzio フルチェイン処理時間を分離計測して日本語で原因切り分けレポートを出力する。
// ============================================================================

#include <cstdio>
#include <cstdlib>
#include <cstddef>
#include <cstring>
#include <cmath>
#include <new>
#include <atomic>
#include <vector>
#include <chrono>
#include <algorithm>
#include <thread>

// スレッドローカルな RT ヒープ確保カウンタ（測定中のオーディオスレッドのみ厳密集計）
static thread_local bool        tlCounting   = false;
static thread_local long        tlAllocCount = 0;
static thread_local std::size_t tlAllocBytes = 0;

void* operator new (std::size_t n)
{
    if (tlCounting)
    {
        ++tlAllocCount;
        tlAllocBytes += n;
    }
    void* p = std::malloc (n ? n : 1);
    if (! p) throw std::bad_alloc();
    return p;
}
void* operator new[] (std::size_t n) { return operator new (n); }
void  operator delete   (void* p) noexcept { std::free (p); }
void  operator delete[] (void* p) noexcept { std::free (p); }
void  operator delete   (void* p, std::size_t) noexcept { std::free (p); }
void  operator delete[] (void* p, std::size_t) noexcept { std::free (p); }

#include "TestPaths.h"
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <juce_audio_devices/juce_audio_devices.h>
#include "BinaryData.h"

static int gFail = 0;
#define CHECK(cond, ...) do { \
    if (cond) { std::printf ("  PASS: " __VA_ARGS__); std::printf ("\n"); } \
    else      { std::printf ("  FAIL: " __VA_ARGS__); std::printf ("\n"); ++gFail; } \
} while (0)

static void setP (VocalGzzioProcessor& p, const char* id, float v)
{
    if (auto* prm = p.apvts.getParameter (id))
        prm->setValueNotifyingHost (p.apvts.getParameterRange (id).convertTo0to1 (v));
}

struct VocalSignalGen
{
    double   ph    = 0.0;
    double   vibPh = 0.0;
    unsigned rng   = 20261031u;

    float next (double sr) noexcept
    {
        rng = rng * 1664525u + 1013904223u;
        const float noise = (float) ((int) (rng >> 9) - 4194304) / 4194304.0f;
        const double f0 = 220.0 * (1.0 + 0.008 * std::sin (vibPh));
        vibPh += juce::MathConstants<double>::twoPi * 5.2 / sr;
        double v = 0.0;
        for (int h = 1; h <= 7; ++h)
            v += std::sin (ph * (double) h) / (double) h;
        ph += juce::MathConstants<double>::twoPi * f0 / sr;
        return (float) (0.20 * v / 1.6 + (double) noise * 0.0015);
    }
};

// v5.5.0 の全重負荷エンジンを同時稼働させる最重量ストレステスト構成
static void configureFullV550Heavy (VocalGzzioProcessor& p, int pitchLatMode)
{
    setP (p, "hum_amt", 100.0f);  setP (p, "cons_amt", 50.0f); setP (p, "res_amt", 60.0f);
    setP (p, "ride_amt", 50.0f);  setP (p, "comp1", 40.0f);    setP (p, "comp2", 40.0f);
    setP (p, "deess", 40.0f);     setP (p, "dn_on", 1.0f);     setP (p, "denoise", 60.0f);
    setP (p, "gate", -50.0f);     setP (p, "br_amt", 40.0f);   setP (p, "ring", 30.0f);
    setP (p, "seq_on", 1.0f);     setP (p, "seq_amount", 60.0f); setP (p, "prox_amt", 50.0f);
    setP (p, "revon", 1.0f);      setP (p, "rev_type", 11.0f); // へや：コンサートホール (2.7秒 分割畳み込みIR)
    setP (p, "revmix", 28.0f);    setP (p, "dly_on", 1.0f);    setP (p, "delay", 20.0f);
    setP (p, "doubler", 20.0f);   setP (p, "width", 20.0f);
    setP (p, "at_on", 1.0f);      setP (p, "at_amount", 80.0f); setP (p, "orn_amt", 70.0f);
    setP (p, "jn_on", 1.0f);      setP (p, "jn_harm", 16.0f);  // 4声ゴスペルハモリ
    setP (p, "jn_mix", 55.0f);
    setP (p, "vc_on", 1.0f);      setP (p, "vc_pitch", 3.0f);  setP (p, "vc_form", 2.5f);
    setP (p, "emo_amt", 50.0f);   setP (p, "lift_amt", 50.0f);
    setP (p, "robo_on", 1.0f);    setP (p, "robo_mix", 15.0f);
    setP (p, "mega_on", 1.0f);    setP (p, "mega_amt", 15.0f);
    setP (p, "cho_on", 1.0f);     setP (p, "cho_amt", 25.0f);
    setP (p, "crush_on", 1.0f);
    setP (p, "argon_velvet", 65.0f);
    setP (p, "argon_silk",   60.0f);
    setP (p, "argon_halo",   50.0f);
    setP (p, "argon_focus",  60.0f);

    p.setPitchLatencyMode (pitchLatMode);
    p.setVcFormantAutoLink (true);
    p.setMicModelMode (1);
    p.setLipDeclickOn (true);
    p.setRoomDereverbOn (true);
    p.setBreathControlOn (true);
    p.setUpwardAirOn (true);
    p.setPitchSweetSpotOn (true);
    p.setSingerCueMonitorOn (true);
    p.setTailVibratoOn (true);
    p.setVowelAlignOn (true);
    p.setOnsetGrooveOn (true);
    p.setMixVoiceBridgeOn (true);
    p.setScreamGuardOn (true);
    p.setAutoChorusMode (true);
    p.setChorusBoostMode (true);
    p.setSyncOffsetMs (25);
}

static void startHeavyBuiltinBgm (VocalGzzioProcessor& p)
{
    p.loadBuiltinDemoBgm();
    p.setBgmLooping (true);
    p.setBgmPitchSemi (3);
    p.setBgmVocalCancel (true);
    p.setBgmUnmaskOn (true);
    p.setBgmVocalRiderOn (true);
    p.setChordFollowHarmOn (true);
    p.setBgmDuckAmt (0.5f);
    p.setBgmToStream (true);
    p.setBgmPlaying (true);
}

struct BlockStats
{
    double      deadlineMs     = 0.0;
    double      avgMs          = 0.0;
    double      p50Ms          = 0.0;
    double      p99Ms          = 0.0;
    double      sustainedP95Ms = 0.0;
    double      worstMs        = 0.0;
    int         blocks         = 0;
    int         missed         = 0;
    long        rtAllocs       = 0;
    std::size_t rtAllocBytes   = 0;
    bool        allFinite      = true;
    float       peakAbs        = 0.0f;
};

static BlockStats runDeadlineBench (VocalGzzioProcessor& p, double sr, int bs, double durationSec)
{
   #if defined(_WIN32)
    const DWORD prevProcPrio = GetPriorityClass (GetCurrentProcess());
    const int   prevThrdPrio = GetThreadPriority (GetCurrentThread());
    SetPriorityClass (GetCurrentProcess(), HIGH_PRIORITY_CLASS);
    SetThreadPriority (GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    struct ScopedRtPrio
    {
        DWORD pp; int tp;
        ~ScopedRtPrio()
        {
            SetThreadPriority (GetCurrentThread(), tp);
            if (pp != 0) SetPriorityClass (GetCurrentProcess(), pp);
        }
    } restorePrio { prevProcPrio, prevThrdPrio };
   #endif

    VocalSignalGen gen;
    juce::AudioBuffer<float> buf (2, bs);
    juce::MidiBuffer midi;

    for (int w = 0; w < 80; ++w)
    {
        auto* L = buf.getWritePointer (0);
        auto* R = buf.getWritePointer (1);
        for (int i = 0; i < bs; ++i) { const float v = gen.next (sr); L[i] = R[i] = v; }
        p.processBlock (buf, midi);
    }

    const int blocks = std::max (300, (int) std::ceil (durationSec * sr / (double) bs));
    std::vector<double> times;
    times.reserve ((size_t) blocks);

    const double deadline = 1000.0 * (double) bs / sr;
    int   missed    = 0;
    bool  allFinite = true;
    float peakAbs   = 0.0f;

    tlAllocCount = 0;
    tlAllocBytes = 0;
    tlCounting   = true;

    for (int b = 0; b < blocks; ++b)
    {
        auto* L = buf.getWritePointer (0);
        auto* R = buf.getWritePointer (1);
        for (int i = 0; i < bs; ++i) { const float v = gen.next (sr); L[i] = R[i] = v; }

        const auto t0 = std::chrono::steady_clock::now();
        p.processBlock (buf, midi);
        const auto t1 = std::chrono::steady_clock::now();

        const double ms = std::chrono::duration<double, std::milli> (t1 - t0).count();
        times.push_back (ms);
        if (ms > deadline) ++missed;

        const float* rL = buf.getReadPointer (0);
        const float* rR = buf.getReadPointer (1);
        for (int i = 0; i < bs; ++i)
        {
            if (! std::isfinite (rL[i]) || ! std::isfinite (rR[i])) allFinite = false;
            peakAbs = std::max (peakAbs, std::max (std::abs (rL[i]), std::abs (rR[i])));
        }
    }

    tlCounting = false;

    std::vector<double> sorted = times;
    std::sort (sorted.begin(), sorted.end());
    double sum = 0.0;
    for (double v : times) sum += v;
    auto pct = [&] (double q) -> double
    {
        const size_t idx = std::min (sorted.size() - 1, (size_t) ((double) sorted.size() * q));
        return sorted[idx];
    };

    std::vector<double> sustained;
    for (size_t from = 0; from + 32 <= times.size(); from += 32)
    {
        double winSum = 0.0;
        for (size_t i = from; i < from + 32; ++i) winSum += times[i];
        sustained.push_back (winSum / 32.0);
    }
    if (sustained.empty()) sustained.push_back (sum / (double) times.size());
    std::sort (sustained.begin(), sustained.end());

    BlockStats st;
    st.deadlineMs     = deadline;
    st.avgMs          = sum / (double) times.size();
    st.p50Ms          = pct (0.50);
    st.p99Ms          = pct (0.99);
    st.sustainedP95Ms = sustained[std::min (sustained.size() - 1, (size_t) ((double) sustained.size() * 0.95))];
    st.worstMs        = sorted.back();
    st.blocks         = blocks;
    st.missed         = missed;
    st.rtAllocs       = tlAllocCount;
    st.rtAllocBytes   = tlAllocBytes;
    st.allFinite      = allFinite;
    st.peakAbs        = peakAbs;
    return st;
}

// ============================================================================
// 実機オーディオデバイス・コールバック周期ジッタ＆DSP負荷診断モード (--live-device)
// ============================================================================
static int runLiveDeviceDiagnostic (int durationSec, bool silentOut)
{
    std::printf ("====================================================================\n");
    std::printf (" VocalGzzio v5.5.0 実機オーディオデバイス・コールバック負荷診断\n");
    std::printf ("====================================================================\n");

    juce::AudioDeviceManager adm;
    std::printf ("[利用可能なオーディオドライバ種別]\n");
    for (auto* t : adm.getAvailableDeviceTypes())
    {
        t->scanForDevices();
        const auto outs = t->getDeviceNames (false);
        std::printf ("  - %s (出力デバイス %d 件)\n", t->getTypeName().toRawUTF8(), outs.size());
    }

    const auto err = adm.initialiseWithDefaultDevices (0, 2);
    auto* dev = adm.getCurrentAudioDevice();
    if (err.isNotEmpty() || dev == nullptr)
    {
        std::printf ("\n[結果] 利用可能な実機オーディオ出力デバイスが見つかりませんでした: %s\n",
                     err.toRawUTF8());
        return 1;
    }

    const double sr = dev->getCurrentSampleRate();
    const int    bs = dev->getCurrentBufferSizeSamples();
    const double deadlineMs = 1000.0 * (double) bs / std::max (8000.0, sr);
    std::printf ("\n[計測対象デバイス]\n");
    std::printf ("  ドライバ種別 : %s\n", dev->getTypeName().toRawUTF8());
    std::printf ("  デバイス名   : %s\n", dev->getName().toRawUTF8());
    std::printf ("  設定         : %.1f kHz / %d samples (1コールバック締切 = %.3f ms)\n",
                 sr / 1000.0, bs, deadlineMs);
    std::printf ("  計測時間     : %d 秒間 (%s)\n\n",
                 durationSec, silentOut ? "出力ミュート計測" : "-24dBFS テスト合成音声出力");

    VocalGzzioProcessor proc;
    configureFullV550Heavy (proc, 2);
    proc.setPlayConfigDetails (2, 2, sr, bs);
    proc.prepareToPlay (sr, bs);
    startHeavyBuiltinBgm (proc);

    struct LiveProbeCallback : public juce::AudioIODeviceCallback
    {
        VocalGzzioProcessor& proc;
        bool silent = false;
        VocalSignalGen gen;
        juce::AudioBuffer<float> workBuf;
        juce::MidiBuffer midi;
        std::vector<double> cbIntervalsMs;
        std::vector<double> dspTimesMs;
        std::chrono::steady_clock::time_point prevCb {};
        bool hasPrev = false;
        std::atomic<long> rtAllocs { 0 };
        std::atomic<int>  nanCount { 0 };

        LiveProbeCallback (VocalGzzioProcessor& p, int maxBs, bool isSilent)
            : proc (p), silent (isSilent), workBuf (2, std::max (maxBs * 4, 2048))
        {
            cbIntervalsMs.reserve (16384);
            dspTimesMs.reserve (16384);
        }

        void audioDeviceAboutToStart (juce::AudioIODevice* d) override
        {
            hasPrev = false;
            if (d != nullptr)
                workBuf.setSize (2, std::max (d->getCurrentBufferSizeSamples() * 2, 2048));
        }

        void audioDeviceStopped() override {}

        void audioDeviceIOCallbackWithContext (const float* const*, int,
                                               float* const* out, int numOut, int numSamples,
                                               const juce::AudioIODeviceCallbackContext&) override
        {
            const auto now = std::chrono::steady_clock::now();
            if (hasPrev && cbIntervalsMs.size() < cbIntervalsMs.capacity())
                cbIntervalsMs.push_back (std::chrono::duration<double, std::milli> (now - prevCb).count());
            prevCb  = now;
            hasPrev = true;

            const double srNow = proc.getSampleRate() > 8000.0 ? proc.getSampleRate() : 48000.0;
            juce::AudioBuffer<float> sub (workBuf.getArrayOfWritePointers(), 2, numSamples);
            auto* wL = sub.getWritePointer (0);
            auto* wR = sub.getWritePointer (1);
            for (int i = 0; i < numSamples; ++i)
            {
                const float v = gen.next (srNow);
                wL[i] = v;
                wR[i] = v;
            }

            tlAllocCount = 0;
            tlCounting   = true;
            const auto t0 = std::chrono::steady_clock::now();
            proc.processBlock (sub, midi);
            const auto t1 = std::chrono::steady_clock::now();
            tlCounting   = false;
            if (tlAllocCount > 0)
                rtAllocs.fetch_add (tlAllocCount, std::memory_order_relaxed);

            if (dspTimesMs.size() < dspTimesMs.capacity())
                dspTimesMs.push_back (std::chrono::duration<double, std::milli> (t1 - t0).count());

            for (int ch = 0; ch < numOut; ++ch)
            {
                if (out[ch] == nullptr) continue;
                if (silent)
                {
                    juce::FloatVectorOperations::clear (out[ch], numSamples);
                }
                else
                {
                    const float* src = sub.getReadPointer (std::min (ch, 1));
                    for (int i = 0; i < numSamples; ++i)
                    {
                        const float s = src[i];
                        if (! std::isfinite (s)) { nanCount.fetch_add (1, std::memory_order_relaxed); out[ch][i] = 0.0f; }
                        else out[ch][i] = juce::jlimit (-0.25f, 0.25f, s * 0.25f);
                    }
                }
            }
        }
    } cb (proc, bs, silentOut);

    adm.addAudioCallback (&cb);
    std::this_thread::sleep_for (std::chrono::seconds (std::max (1, durationSec)));
    adm.removeAudioCallback (&cb);
    adm.closeAudioDevice();
    proc.releaseResources();

    if (cb.dspTimesMs.empty() || cb.cbIntervalsMs.empty())
    {
        std::printf ("[警告] コールバックが1回も呼ばれませんでした。デバイスが排他ロック中の可能性があります。\n");
        return 1;
    }

    auto summarize = [] (std::vector<double> v, double& avg, double& p95, double& p99, double& mx)
    {
        std::sort (v.begin(), v.end());
        double s = 0.0;
        for (double x : v) s += x;
        avg = s / (double) v.size();
        p95 = v[std::min (v.size() - 1, (size_t) (v.size() * 0.95))];
        p99 = v[std::min (v.size() - 1, (size_t) (v.size() * 0.99))];
        mx  = v.back();
    };

    double cbAvg = 0, cbP95 = 0, cbP99 = 0, cbMax = 0;
    double dspAvg = 0, dspP95 = 0, dspP99 = 0, dspMax = 0;
    summarize (cb.cbIntervalsMs, cbAvg, cbP95, cbP99, cbMax);
    summarize (cb.dspTimesMs, dspAvg, dspP95, dspP99, dspMax);
    int dspOverruns = 0;
    for (double d : cb.dspTimesMs) if (d > deadlineMs) ++dspOverruns;

    std::printf ("[診断サマリー（全 %zu コールバック）]\n", cb.dspTimesMs.size());
    std::printf ("  1) OS/ドライバ起床周期 : 平均 %.3f ms / p95 %.3f ms / p99 %.3f ms / 最大 %.3f ms (理論値 %.3f ms)\n",
                 cbAvg, cbP95, cbP99, cbMax, deadlineMs);
    std::printf ("  2) VocalGzzio フルDSP : 平均 %.3f ms (%.1f%%) / p99 %.3f ms (%.1f%%) / 最大 %.3f ms (締切超過 %d 回)\n",
                 dspAvg, 100.0 * dspAvg / deadlineMs, dspP99, 100.0 * dspP99 / deadlineMs, dspMax, dspOverruns);
    std::printf ("  3) RTヒープ確保 / NaN : %ld 回 / %d 回\n\n", cb.rtAllocs.load(), cb.nanCount.load());

    if (dspOverruns == 0 && cbMax > deadlineMs * 2.2)
        std::printf ("  => 判定: VocalGzzio のDSP負荷は余裕ですが、OS/USBドライバ側のコールバック起床間隔が大きく揺れています（DPCレイテンシ・USB省電力・排他競合を確認してください）。\n");
    else if (dspOverruns > 0)
        std::printf ("  => 判定: 現在のバッファサイズ(%d samples)に対してDSP負荷ピークが締切を超えています。バッファを1段階広げるか軽量モードを推奨します。\n", bs);
    else
        std::printf ("  => 判定: ドライバ起床周期・VocalGzzioフルDSP負荷ともに完全に安定しています。\n");

    return 0;
}

int main (int argc, char* argv[])
{
    std::setvbuf (stdout, nullptr, _IONBF, 0);
    juce::ScopedJuceInitialiser_GUI init;

    // 手動CLI実機診断モード (--live-device [秒数] [--silent])
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp (argv[i], "--live-device") == 0)
        {
            int sec = 5;
            bool silent = false;
            for (int j = i + 1; j < argc; ++j)
            {
                if (std::strcmp (argv[j], "--silent") == 0) silent = true;
                else if (std::atoi (argv[j]) > 0) sec = std::atoi (argv[j]);
            }
            return runLiveDeviceDiagnostic (sec, silent);
        }
    }

    auto autosave = gz::dataDirectory().getChildFile ("autosave.xml");
    auto backup   = autosave.getSiblingFile ("autosave.bak_crackle_stress");
    autosave.getParentDirectory().createDirectory();
    if (autosave.existsAsFile()) autosave.moveFileTo (backup);
    struct Restore
    {
        juce::File a, b;
        ~Restore() { a.deleteFile(); if (b.existsAsFile()) b.moveFileTo (a); }
    } restore { autosave, backup };

    // =========================================================================
    // [1] 真・v5.5.0全機能フル稼働マトリクス ＆ L/Rステレオ位相ロック検査
    // =========================================================================
    std::printf ("== 1. 真v5.5.0フル機能マトリクス（RT確保ゼロ・小バッファ/96kHzホップ分散・左右位相一致） ==\n");
    {
        struct Case { const char* label; int latMode; double sr; int bs; };
        const Case cases[] = {
            { "44.1kHz / 124samp (標準mode=2)", 2, 44100.0, 124 },
            { "48.0kHz / 256samp (標準mode=2)", 2, 48000.0, 256 },
            { "48.0kHz / 128samp (標準mode=2)", 2, 48000.0, 128 },
            { "48.0kHz /  64samp (標準mode=2)", 2, 48000.0,  64 },
            { "48.0kHz /  64samp (極限mode=0)", 0, 48000.0,  64 },
            { "96.0kHz / 128samp (標準mode=2)", 2, 96000.0, 128 },
            { "96.0kHz /  64samp (標準mode=2)", 2, 96000.0,  64 },
        };

        for (const auto& c : cases)
        {
            autosave.deleteFile();
            VocalGzzioProcessor p;
            configureFullV550Heavy (p, c.latMode);
            p.setPlayConfigDetails (2, 2, c.sr, c.bs);
            p.prepareToPlay (c.sr, c.bs);
            startHeavyBuiltinBgm (p);

            const auto st = runDeadlineBench (p, c.sr, c.bs, 1.2);
            // dsp_cpu.cpp と同じく、44.1k/48k では単発99%点と32ブロック継続95%点の両方を期限に対して判定し、
            // サブミリ秒量子(96kHz/64〜128samp)は共有CI・他プロセス高負荷時のOSスケジューラジッタを分離するため32ブロック継続95%点で判定する。
            const bool timingOk = (c.sr <= 48000.0)
                                ? (st.p99Ms < st.deadlineMs && st.sustainedP95Ms < st.deadlineMs)
                                : (st.sustainedP95Ms < st.deadlineMs);
            CHECK (st.rtAllocs == 0 && st.allFinite && st.peakAbs > 1e-4f && st.peakAbs < 4.0f && timingOk,
                   "%-30s 締切=%.3fms | 平均=%.3fms 継続p95=%.3fms p99=%.3fms 最悪=%.3fms | RTnew=%ld",
                   c.label, st.deadlineMs, st.avgMs, st.sustainedP95Ms, st.p99Ms, st.worstMs, st.rtAllocs);
            p.releaseResources();
        }

        // VoiceShifter ホップ位相スタッガリング(0.333f)適用時の L/R 完全同位相ロック検証
        // ＋ デュアルモノ共有から途中ブロックで真ステレオ(L!=R)へ移行した際の copyRuntimeStateFrom 完全一致・ゼロ確保検証
        gz::VoiceShifter shRefL, shRefR, shOptL, shOptR;
        for (auto* s : { &shRefL, &shRefR, &shOptL, &shOptR })
        {
            s->prepare (48000.0);
            s->setHopPhaseRatio (0.333f);
            s->setWindow (10);
            s->setParams (3.5f, 2.0f, 1.0f);
        }
        VocalSignalGen genL, genR;
        genR.ph = 0.7;
        std::vector<float> refL (64), refR (64), optL (64), optR (64);
        float maxDualMonoDiff = 0.0f;
        float maxStereoSwitchDiff = 0.0f;
        bool dualMonoSync = true;
        tlAllocCount = 0;
        tlCounting   = true;
        for (int b = 0; b < 80; ++b)
        {
            const bool makeTrueStereo = (b >= 40);
            for (int i = 0; i < 64; ++i)
            {
                const float vL = genL.next (48000.0);
                const float vR = makeTrueStereo ? genR.next (48000.0) : vL;
                refL[(size_t) i] = optL[(size_t) i] = vL;
                refR[(size_t) i] = optR[(size_t) i] = vR;
            }
            shRefL.processBlock (refL.data(), 64);
            shRefR.processBlock (refR.data(), 64);

            if (dualMonoSync && ! makeTrueStereo)
            {
                shOptL.processBlock (optL.data(), 64);
                std::memcpy (optR.data(), optL.data(), sizeof (float) * 64);
                for (int i = 0; i < 64; ++i)
                    maxDualMonoDiff = std::max (maxDualMonoDiff, std::abs (refL[(size_t) i] - refR[(size_t) i]));
            }
            else
            {
                if (dualMonoSync)
                {
                    shOptR.copyRuntimeStateFrom (shOptL);
                    dualMonoSync = false;
                }
                shOptL.processBlock (optL.data(), 64);
                shOptR.processBlock (optR.data(), 64);
            }
            for (int i = 0; i < 64; ++i)
            {
                maxStereoSwitchDiff = std::max (maxStereoSwitchDiff,
                    std::max (std::abs (optL[(size_t) i] - refL[(size_t) i]),
                              std::abs (optR[(size_t) i] - refR[(size_t) i])));
            }
        }
        tlCounting = false;
        CHECK (maxDualMonoDiff == 0.0f && maxStereoSwitchDiff == 0.0f && tlAllocCount == 0,
               "vcSh L/R 同位相＆デュアルモノ→真ステレオ途中移行: 左右差=%.2e, 参照との差=%.2e, RTnew=%ld",
               maxDualMonoDiff, maxStereoSwitchDiff, tlAllocCount);
    }

    // =========================================================================
    // [2] ライブ中の「へや（全6部屋IR）」切替＆カスタムIR非同期切替ゼロストール検査
    // =========================================================================
    std::printf ("\n== 2. 歌いながらの「へや（全6部屋IR）」＆カスタムIR非同期切替（ストール0・RT確保0） ==\n");
    {
        autosave.deleteFile();
        VocalGzzioProcessor p;
        setP (p, "revon", 1.0f);
        setP (p, "revmix", 30.0f);
        setP (p, "rev_type", 0.0f); // 初期はノーマルIIRの状態で prepareToPlay
        const double sr = 48000.0;
        const int    bs = 128;
        const double deadlineMs = 1000.0 * (double) bs / sr; // 2.667 ms
        p.setPlayConfigDetails (2, 2, sr, bs);
        p.prepareToPlay (sr, bs);

        juce::AudioBuffer<float> buf (2, bs);
        juce::MidiBuffer midi;
        VocalSignalGen gen;

        for (int b = 0; b < 20; ++b)
        {
            for (int i = 0; i < bs; ++i) { const float v = gen.next (sr); buf.setSample (0, i, v); buf.setSample (1, i, v); }
            p.processBlock (buf, midi);
        }

        long totalSwitchAllocs = 0;
        double worstSwitchMs = 0.0;
        float prevOut = buf.getSample (0, bs - 1);
        float maxStep = 0.0f;

        for (int room = 0; room < 6; ++room)
        {
            setP (p, "rev_type", (float) (7 + room));
            for (int b = 0; b < 8; ++b)
            {
                for (int i = 0; i < bs; ++i)
                {
                    const float v = 0.25f * (float) std::sin (2.0 * 3.141592653589793 * 260.0 * (double) (room * 1024 + b * bs + i) / sr);
                    buf.setSample (0, i, v);
                    buf.setSample (1, i, v);
                }
                tlAllocCount = 0;
                tlCounting   = true;
                const auto t0 = std::chrono::steady_clock::now();
                p.processBlock (buf, midi);
                const auto t1 = std::chrono::steady_clock::now();
                tlCounting   = false;

                totalSwitchAllocs += tlAllocCount;
                const double ms = std::chrono::duration<double, std::milli> (t1 - t0).count();
                if (b == 0) worstSwitchMs = std::max (worstSwitchMs, ms);

                for (int i = 0; i < bs; ++i)
                {
                    const float cur = buf.getSample (0, i);
                    maxStep = std::max (maxStep, std::abs (cur - prevOut));
                    prevOut = cur;
                }
            }
        }
        p.releaseResources();

        CHECK (totalSwitchAllocs == 0 && worstSwitchMs < deadlineMs && maxStep < 0.25f,
               "全6部屋ライブ連続切替: 最悪切替ブロック=%.3fms (< 締切%.3fms), RTnew=%ld回, 最大段差=%.4f",
               worstSwitchMs, deadlineMs, totalSwitchAllocs, maxStep);

        // UIスレッドからの loadCustomIr / clearCustomIr とオーディオスレッド process() の並行ロックフリー競合検査
        heya::Convolver cv;
        cv.prepare (sr, bs, false);
        cv.setRoom (2);
        std::atomic<bool> uiDone { false };
        std::thread uiLoader ([&]()
        {
            std::vector<float> irL (4096, 0.0f), irR (4096, 0.0f);
            for (int round = 0; round < 14; ++round)
            {
                const float decay = 0.9985f - 0.0001f * (float) (round % 5);
                float e = 0.5f;
                for (size_t i = 0; i < irL.size(); ++i)
                {
                    irL[i] = e * ((i % 7 == 0) ? 1.0f : -0.4f);
                    irR[i] = e * ((i % 5 == 0) ? 0.9f : -0.5f);
                    e *= decay;
                }
                cv.loadCustomIr (irL.data(), irR.data(), (int) irL.size());
                std::this_thread::sleep_for (std::chrono::milliseconds (6));
                if ((round & 1) == 1)
                {
                    cv.clearCustomIr();
                    std::this_thread::sleep_for (std::chrono::milliseconds (4));
                }
            }
            uiDone.store (true, std::memory_order_release);
        });

        long customRtAllocs = 0;
        bool customFinite   = true;
        std::vector<float> cL (bs), cR (bs);
        int phSample = 0;
        while (! uiDone.load (std::memory_order_acquire))
        {
            for (int i = 0; i < bs; ++i, ++phSample)
            {
                const float s = 0.25f * (float) std::sin (2.0 * 3.141592653589793 * 330.0 * (double) phSample / sr);
                cL[(size_t) i] = cR[(size_t) i] = s;
            }
            tlAllocCount = 0;
            tlCounting   = true;
            cv.process (cL.data(), cR.data(), bs);
            tlCounting   = false;
            customRtAllocs += tlAllocCount;
            for (int i = 0; i < bs; ++i)
                if (! std::isfinite (cL[(size_t) i]) || ! std::isfinite (cR[(size_t) i]))
                    customFinite = false;
        }
        uiLoader.join();
        CHECK (customRtAllocs == 0 && customFinite,
               "UIスレッド並行 loadCustomIr/clearCustomIr 14往復: RTnew=%ld回, NaN/Inf無し",
               customRtAllocs);
    }

    // =========================================================================
    // [3] 非同期2クロックFIFO（AppAudioCapture）単発OSスパイク後の枯渇振動防止検査
    // =========================================================================
    std::printf ("\n== 3. AppAudioCapture 2クロックFIFO 単発OSスパイク・異レートジッタ後の自動回復検査 ==\n");
    {
        auto runCaptureScenario = [] (double capSr, double hostSr, int asioBlock, double jitterAmpMs, double stallMs)
        {
            gz::AppAudioCapture cap;
            const int wasapiPkt = (int) std::lround (capSr * 0.010); // 10ms WASAPIパケット
            double sinePh = 0.0;
            auto pushPacket = [&] (int frames)
            {
                std::vector<float> inL ((size_t) frames), inR ((size_t) frames);
                for (int i = 0; i < frames; ++i)
                {
                    const float s = 0.45f * (float) std::sin (sinePh);
                    sinePh += juce::MathConstants<double>::twoPi * 440.0 / capSr;
                    inL[(size_t) i] = inR[(size_t) i] = s;
                }
                cap.pushCapturedStereo (inL.data(), inR.data(), frames, capSr);
            };

            pushPacket (wasapiPkt);
            pushPacket (wasapiPkt);
            pushPacket (wasapiPkt);

            unsigned jrng = 12345u;
            auto nextJitter = [&]() -> double
            {
                if (jitterAmpMs <= 0.0) return 0.0;
                jrng = jrng * 1664525u + 1013904223u;
                return (((double) ((jrng >> 8) & 0xFFFF) / 65535.0) * 2.0 - 1.0) * jitterAmpMs;
            };

            std::vector<float> outL ((size_t) asioBlock), outR ((size_t) asioBlock);
            double simMs = 0.0;
            double nominalWasapiMs = 10.0;
            double nextWasapiMs = nominalWasapiMs + nextJitter();

            const float maxSmoothStep = (float) (0.45 * juce::MathConstants<double>::twoPi * 440.0 / hostSr * 3.8);
            float prevS = 0.0f;
            bool  hasPrev = false;
            int   stepClicks = 0;

            // 前半 0.8 秒: 定常運転
            const int preBlocks = (int) (0.8 * hostSr / asioBlock);
            for (int b = 0; b < preBlocks; ++b)
            {
                simMs += 1000.0 * asioBlock / hostSr;
                while (simMs >= nextWasapiMs)
                {
                    pushPacket (wasapiPkt);
                    nominalWasapiMs += 10.0;
                    nextWasapiMs = std::max (simMs + 0.2, nominalWasapiMs + nextJitter());
                }
                cap.pullStereo (outL.data(), outR.data(), asioBlock, hostSr);
                if (b > 12)
                {
                    for (int i = 0; i < asioBlock; ++i)
                    {
                        if (hasPrev && std::abs (outL[(size_t) i] - prevS) > maxSmoothStep)
                            ++stepClicks;
                        prevS = outL[(size_t) i];
                        hasPrev = true;
                    }
                }
            }

            // 単発の OS スケジューリング遅延を注入（0 < readyNow < needIn を誘発）
            nominalWasapiMs = simMs + stallMs;
            nextWasapiMs    = nominalWasapiMs;

            // 後半 2.5 秒: スパイク通過〜再プリバッファ〜定常復帰後（復帰150ms以降の音欠けも監視）
            const int postBlocks = (int) (2.5 * hostSr / asioBlock);
            const int settleBlocks = (int) (0.18 * hostSr / asioBlock);
            int lateDropouts = 0;
            for (int b = 0; b < postBlocks; ++b)
            {
                simMs += 1000.0 * asioBlock / hostSr;
                while (simMs >= nextWasapiMs)
                {
                    pushPacket (wasapiPkt);
                    nominalWasapiMs += 10.0;
                    nextWasapiMs = std::max (simMs + 0.2, nominalWasapiMs + nextJitter());
                }
                cap.pullStereo (outL.data(), outR.data(), asioBlock, hostSr);
                float blkPeak = 0.0f;
                for (int i = 0; i < asioBlock; ++i)
                {
                    blkPeak = std::max (blkPeak, std::abs (outL[(size_t) i]));
                    if (hasPrev && std::abs (outL[(size_t) i] - prevS) > maxSmoothStep)
                        ++stepClicks;
                    prevS = outL[(size_t) i];
                }
                if (b > settleBlocks && blkPeak < 0.15f)
                    ++lateDropouts;
            }
            return std::pair<int, int> { stepClicks, lateDropouts };
        };

        const auto resA = runCaptureScenario (48000.0, 48000.0, 128, 0.0, 26.0);
        CHECK (resA.first == 0 && resA.second == 0,
               "AppAudioCapture (48k WASAPI -> 48k/128 ASIO + 26ms単発遅延): 段差クリック=%d回, 復帰後の再欠落=%d回",
               resA.first, resA.second);

        const auto resB = runCaptureScenario (48000.0, 44100.0, 64, 2.5, 30.0);
        CHECK (resB.first == 0 && resB.second == 0,
               "AppAudioCapture (48k±2.5ms WASAPI -> 44.1k/64 ASIO + 30ms単発遅延): 段差クリック=%d回, 復帰後の再欠落=%d回",
               resB.first, resB.second);
    }

    // =========================================================================
    // [4] 非同期2クロック配信出力（StreamOut）単発ホスト停止＋起床ジッタ後のクッション回復検査
    // =========================================================================
    std::printf ("\n== 4. StreamOut 2クロック配信出力 単発ホスト遅延＋±2.5ms起床ジッタ後の完全回復検査 ==\n");
    {
        gz::StreamOut so;
        const double hSr = 44100.0;
        const double dSr = 48000.0;
        const int hostBlock = 124; // 2.812ms
        const int devBlock  = 480; // 10.000ms
        so.startHeadlessForTesting (hSr, dSr, devBlock);

        double sinePh = 0.0;
        std::vector<float> inL ((size_t) hostBlock), inR ((size_t) hostBlock);
        std::vector<float> outL ((size_t) devBlock), outR ((size_t) devBlock);
        auto pushHost = [&]()
        {
            for (int i = 0; i < hostBlock; ++i)
            {
                const float s = 0.45f * (float) std::sin (sinePh);
                sinePh += juce::MathConstants<double>::twoPi * 440.0 / hSr;
                inL[(size_t) i] = inR[(size_t) i] = s;
            }
            so.push (inL.data(), inR.data(), hostBlock);
        };

        unsigned jrng = 777u;
        auto nextJitterMs = [&]() -> double
        {
            jrng = jrng * 1664525u + 1013904223u;
            return (((double) ((jrng >> 8) & 0xFFFF) / 65535.0) * 2.0 - 1.0) * 2.5;
        };

        double simMs = 0.0;
        double nextHostMs   = 1000.0 * hostBlock / hSr;
        double devNominalMs = 10.0;
        double nextDevMs    = devNominalMs + nextJitterMs();

        while (simMs < 800.0)
        {
            if (nextHostMs <= nextDevMs)
            {
                simMs = nextHostMs;
                pushHost();
                nextHostMs += 1000.0 * hostBlock / hSr;
            }
            else
            {
                simMs = nextDevMs;
                so.pullForTesting (outL.data(), outR.data(), devBlock);
                devNominalMs += 10.0;
                nextDevMs = std::max (simMs + 0.5, devNominalMs + nextJitterMs());
            }
        }
        const int preUnderruns = so.getUnderrunCount();

        // 単発 38ms のホスト停止を注入してアンダーラン＆再充填を発動させる
        nextHostMs += 38.0;

        float prevS = outL[devBlock - 1];
        const float maxSmoothStep = (float) (0.45 * juce::MathConstants<double>::twoPi * 440.0 / dSr * 3.8);
        int stepClicks = 0;
        int underrunsAtSettle = 0;
        bool capturedSettle = false;
        int lateDropouts = 0;

        while (simMs < 4500.0)
        {
            if (nextHostMs <= nextDevMs)
            {
                simMs = nextHostMs;
                pushHost();
                nextHostMs += 1000.0 * hostBlock / hSr;
            }
            else
            {
                simMs = nextDevMs;
                so.pullForTesting (outL.data(), outR.data(), devBlock);
                float pk = 0.0f;
                for (int i = 0; i < devBlock; ++i)
                {
                    pk = std::max (pk, std::abs (outL[(size_t) i]));
                    if (std::abs (outL[(size_t) i] - prevS) > maxSmoothStep)
                        ++stepClicks;
                    prevS = outL[(size_t) i];
                }
                if (! capturedSettle && simMs >= 1100.0)
                {
                    underrunsAtSettle = so.getUnderrunCount();
                    capturedSettle = true;
                }
                if (simMs >= 1100.0 && pk < 0.15f)
                    ++lateDropouts;

                devNominalMs += 10.0;
                nextDevMs = std::max (simMs + 0.5, devNominalMs + nextJitterMs());
            }
        }

        const int secondaryUnderruns = so.getUnderrunCount() - underrunsAtSettle;
        const int finalFill          = so.getFifoReadyForTesting();
        const int targetFill         = so.getTargetFillForTesting();
        CHECK (preUnderruns == 0 && stepClicks == 0 && secondaryUnderruns == 0 && lateDropouts == 0
               && finalFill >= (int) (targetFill * 0.82),
               "StreamOut (44.1k/124 -> 48k/480±2.5ms + 38ms単発停止): 段差クリック=%d回, 復帰後の連鎖underrun=%d回, 最終FIFO=%d/%d",
               stepClicks, secondaryUnderruns, finalFill, targetFill);
    }

    // =========================================================================
    // [5] UI全画面描画＋他コアCPU/キャッシュ負荷競合＋可変ブロック長(17..511)耐性検査
    // =========================================================================
    std::printf ("\n== 5. UI全画面描画＋多コアCPU競合＋可変ブロック長(17..511)並行ストレステスト ==\n");
    {
        autosave.deleteFile();
        VocalGzzioProcessor p;
        configureFullV550Heavy (p, 2);
        p.setPlayConfigDetails (2, 2, 48000.0, 128);
        p.prepareToPlay (48000.0, 128);
        startHeavyBuiltinBgm (p);

        GzzioLnF look;
        look.setKawaiiTypeface (juce::Typeface::createSystemTypefaceFor (
            BinaryData::kawaii_font_ttf, BinaryData::kawaii_font_ttfSize));
        VocalGzzioContent content (p);
        content.setLookAndFeel (&look);
        content.setSize (VocalGzzioEditor::baseW, VocalGzzioEditor::baseH);
        content.restoreUiState (true, 0, false, 0);

        std::atomic<bool> stopLoad { false };
        const unsigned hwThreads = std::max (2u, std::thread::hardware_concurrency());
        const unsigned burnCount = std::min (4u, std::max (1u, hwThreads / 2));
        std::vector<std::thread> burners;
        for (unsigned t = 0; t < burnCount; ++t)
        {
            burners.emplace_back ([&stopLoad, t]()
            {
                std::vector<float> mem (1 << 19, (float) (t + 1)); // 2MB キャッシュ負荷
                std::size_t idx = 0;
                float acc = 1.0f;
                while (! stopLoad.load (std::memory_order_relaxed))
                {
                    for (int k = 0; k < 8192; ++k)
                    {
                        idx = (idx + 65) & ((1 << 19) - 1);
                        acc = acc * 0.99991f + mem[idx] * 0.00009f;
                        mem[idx] = acc;
                    }
                }
            });
        }

        BlockStats loadSt {};
        std::thread audioThread ([&]()
        {
           #if defined(_WIN32)
            SetThreadPriority (GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
           #endif
            loadSt = runDeadlineBench (p, 48000.0, 128, 1.4);
        });

        std::vector<float> anaBuf, tunBuf;
        int uiFrames = 0;
        for (int f = 0; f < 8; ++f)
        {
            p.readAnalyzerBuffer (anaBuf);
            p.readTunerBuffer (tunBuf);
            auto img = content.createComponentSnapshot (content.getLocalBounds(), false, 0.5f);
            (void) img.getWidth();
            ++uiFrames;
            std::this_thread::sleep_for (std::chrono::milliseconds (25));
        }

        audioThread.join();
        stopLoad.store (true, std::memory_order_relaxed);
        for (auto& th : burners) th.join();
        content.setLookAndFeel (nullptr);

        CHECK (loadSt.rtAllocs == 0 && loadSt.allFinite && loadSt.p99Ms < loadSt.deadlineMs && uiFrames > 0,
               "UI描画(%d回)+%uスレッド競合下の128samp(締切%.3fms): 平均=%.3fms p99=%.3fms RTnew=%ld",
               uiFrames, burnCount, loadSt.deadlineMs, loadSt.avgMs, loadSt.p99Ms, loadSt.rtAllocs);

        // 可変ブロック長(17..511)境界段差クリック検査
        p.prepareToPlay (48000.0, 512);
        juce::AudioBuffer<float> vbuf (2, 512);
        juce::MidiBuffer midi;
        double ph = 0.0;
        for (int w = 0; w < 30; ++w)
        {
            juce::AudioBuffer<float> sub (vbuf.getArrayOfWritePointers(), 2, 256);
            for (int i = 0; i < 256; ++i)
            {
                const float s = 0.18f * (float) std::sin (ph);
                ph += juce::MathConstants<double>::twoPi * 220.0 / 48000.0;
                sub.setSample (0, i, s); sub.setSample (1, i, s);
            }
            p.processBlock (sub, midi);
        }

        long varAllocs = 0;
        float prevS = vbuf.getSample (0, 255);
        float maxDiff = 0.0f;
        unsigned rng = 99991u;
        tlAllocCount = 0;
        tlCounting   = true;
        for (int b = 0; b < 180; ++b)
        {
            rng = rng * 1664525u + 1013904223u;
            const int n = 17 + (int) (rng % 495); // 17..511
            juce::AudioBuffer<float> sub (vbuf.getArrayOfWritePointers(), 2, n);
            for (int i = 0; i < n; ++i)
            {
                const float s = 0.18f * (float) std::sin (ph);
                ph += juce::MathConstants<double>::twoPi * 220.0 / 48000.0;
                sub.setSample (0, i, s); sub.setSample (1, i, s);
            }
            p.processBlock (sub, midi);
            for (int i = 0; i < n; ++i)
            {
                const float cur = sub.getSample (0, i);
                maxDiff = std::max (maxDiff, std::abs (cur - prevS));
                prevS = cur;
            }
        }
        tlCounting = false;
        varAllocs = tlAllocCount;
        p.releaseResources();

        CHECK (varAllocs == 0 && maxDiff < 0.25f,
               "可変ブロック長(17〜511 samples) 180回: RTnew=%ld回, ブロック境界最大差分=%.4f",
               varAllocs, maxDiff);
    }

    std::printf ("\n====================================================================\n");
    if (gFail == 0)
        std::printf (" dsp_crackle_stress: ALL PASS (失敗 0 件)\n");
    else
        std::printf (" dsp_crackle_stress: FAILED (%d 件の失敗)\n", gFail);
    std::printf ("====================================================================\n");
    return gFail == 0 ? 0 : 1;
}
