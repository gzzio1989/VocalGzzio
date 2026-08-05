#pragma once
// StreamOut (v2.2.0 配信出力) — 処理後の音を「もう1つの出力先」へ同時に流す。
//
// 何のため:
//   単体起動版の音を OBS 等の配信ソフトへ送りたいが、メインの出力は
//   ヘッドホン(ASIO)のままにしたい。ASIO は他アプリから拾えないので、
//   ここから VB-CABLE のような仮想オーディオデバイスへ同じ音を送り、
//   配信ソフトはそれを録る、という経路を作る。
//   ＝ 専用の仮想ドライバを自作しなくても「配信に乗せる」が成立する。
//
// 難しいところ (2デバイス間のクロックずれ):
//   メインの出力とここの出力は別々の水晶で動くので、たとえ両方48kHzでも
//   1時間で数十msずれる。片方が必ず余る/足りなくなる。
//   そこで FIFO のたまり具合を見て、再生速度を ±0.4% の範囲で常に微調整する
//   (可変レート補間)。プチッというノイズにならずに吸収できる。
//
// スレッド安全性:
//   push() はオーディオスレッドから呼ぶ。ロック・確保・IO は一切しない。
//   start()/stop() はメッセージスレッドから。バッファは生存期間中ずっと保持する
//   ので、push() 中に解放されることはない。

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <cmath>
#include <memory>

namespace gz
{

class StreamOut : private juce::AudioIODeviceCallback
{
public:
    StreamOut()
    {
        ring.setSize (2, ringLen);
        ring.clear();
        temp.setSize (2, 8192);
        temp.clear();
    }

    ~StreamOut() override { stop(); }

    //== メッセージスレッド ==================================================

    // 選べる出力先の一覧。ASIO は他アプリと排他になりやすく、メイン出力と
    // 取り合いになるので、ここでは出さない(仮想ケーブルは WASAPI 等に出る)。
    juce::StringArray getOutputDeviceNames()
    {
        juce::StringArray names;
        for (auto* t : adm().getAvailableDeviceTypes())
        {
            if (t->getTypeName().containsIgnoreCase ("ASIO")) continue;
            t->scanForDevices();
            for (const auto& n : t->getDeviceNames (false))     // false = 出力側
                if (n.isNotEmpty() && ! names.contains (n)) names.add (n);
        }
        return names;
    }

    bool start (const juce::String& deviceName, double hostSampleRate)
    {
        stop();
        if (deviceName.isEmpty()) { lastError = "no device"; return false; }

        hostSr = hostSampleRate > 0.0 ? hostSampleRate : 48000.0;

        // その名前を持つデバイス種別へ切り替えてから開く
        for (auto* t : adm().getAvailableDeviceTypes())
        {
            if (t->getTypeName().containsIgnoreCase ("ASIO")) continue;
            t->scanForDevices();
            if (t->getDeviceNames (false).contains (deviceName))
            {
                adm().setCurrentAudioDeviceType (t->getTypeName(), true);
                break;
            }
        }

        // 入力0・出力2。見つからなければ既定デバイスへ逃げずに失敗させる
        // (勝手にスピーカーへ音を出さないため)。
        lastError = adm().initialise (0, 2, nullptr, false, deviceName, nullptr);
        if (lastError.isNotEmpty()) { adm().closeAudioDevice(); return false; }

        if (auto* dev = adm().getCurrentAudioDevice())
            devSr = dev->getCurrentSampleRate() > 0.0 ? dev->getCurrentSampleRate() : hostSr;
        else
            devSr = hostSr;

        // FIFO を目標量(約60ms)まで無音で満たしてから流し始める。
        // 出だしの数ブロックで在庫切れ→プチッと鳴るのを防ぐ。
        fifo.reset();
        ring.clear();
        target = juce::jlimit (256, ringLen / 2, (int) (hostSr * 0.06));
        {
            int s1, sz1, s2, sz2;
            fifo.prepareToWrite (target, s1, sz1, s2, sz2);
            fifo.finishedWrite (sz1 + sz2);
        }
        ratioSm = hostSr / devSr;
        for (auto& i : interp) i.reset();
        underruns.store (0);

        deviceName_ = deviceName;
        active.store (true);
        adm().addAudioCallback (this);
        return true;
    }

    void stop()
    {
        if (admPtr == nullptr) { active.store (false); return; }
        if (! active.exchange (false) && admPtr->getCurrentAudioDevice() == nullptr) return;
        admPtr->removeAudioCallback (this);
        admPtr->closeAudioDevice();
        deviceName_.clear();
    }

    bool isRunning() const noexcept          { return active.load(); }
    juce::String getDeviceName() const       { return deviceName_; }
    juce::String getLastError() const        { return lastError; }
    double getDeviceSampleRate() const noexcept { return devSr; }
    int  getUnderrunCount() const noexcept   { return underruns.load(); }

    void setHostSampleRate (double sr) noexcept { if (sr > 0.0) hostSr = sr; }

    //== オーディオスレッド ==================================================

    // 仕上がった音を渡す。満杯なら入る分だけ書く(配信側が一瞬途切れるだけで、
    // 本線の音には一切影響しない)。
    void push (const float* L, const float* R, int n) noexcept
    {
        if (! active.load() || n <= 0) return;

        int s1, sz1, s2, sz2;
        fifo.prepareToWrite (n, s1, sz1, s2, sz2);
        if (sz1 > 0)
        {
            juce::FloatVectorOperations::copy (ring.getWritePointer (0, s1), L,        sz1);
            juce::FloatVectorOperations::copy (ring.getWritePointer (1, s1), R,        sz1);
        }
        if (sz2 > 0)
        {
            juce::FloatVectorOperations::copy (ring.getWritePointer (0, s2), L + sz1,  sz2);
            juce::FloatVectorOperations::copy (ring.getWritePointer (1, s2), R + sz1,  sz2);
        }
        fifo.finishedWrite (sz1 + sz2);
    }

private:
    void audioDeviceAboutToStart (juce::AudioIODevice* dev) override
    {
        if (dev != nullptr)
        {
            devSr = dev->getCurrentSampleRate() > 0.0 ? dev->getCurrentSampleRate() : hostSr;
            const int want = juce::jmax (2048, dev->getCurrentBufferSizeSamples() * 8 + 64);
            if (temp.getNumSamples() < want) temp.setSize (2, want, false, false, true);
        }
        ratioSm = hostSr / devSr;
    }

    void audioDeviceStopped() override {}

    void audioDeviceIOCallbackWithContext (const float* const*, int,
                                           float* const* out, int numOut, int numSamples,
                                           const juce::AudioIODeviceCallbackContext&) override
    {
        for (int ch = 0; ch < numOut; ++ch)
            if (out[ch] != nullptr) juce::FloatVectorOperations::clear (out[ch], numSamples);

        if (! active.load() || numOut < 1 || numSamples <= 0) return;

        // --- クロックずれの吸収: たまり具合から再生速度を微調整 ---
        const int fill = fifo.getNumReady();
        const double err  = (double) (fill - target) / (double) juce::jmax (1, target);
        const double corr = juce::jlimit (-0.004, 0.004, err * 0.05);   // 最大±0.4%
        const double want = (hostSr / devSr) * (1.0 + corr);
        ratioSm += 0.05 * (want - ratioSm);
        ratioSm = juce::jlimit (0.25, 4.0, ratioSm);

        const int numIn = (int) std::ceil ((double) numSamples * ratioSm) + 2;
        if (numIn > temp.getNumSamples() || fill < numIn)
        {
            underruns.fetch_add (1);      // 在庫切れ: この回は無音のまま抜ける
            return;
        }

        int s1, sz1, s2, sz2;
        fifo.prepareToRead (numIn, s1, sz1, s2, sz2);
        for (int ch = 0; ch < 2; ++ch)
        {
            auto* dst = temp.getWritePointer (ch);
            if (sz1 > 0) juce::FloatVectorOperations::copy (dst,       ring.getReadPointer (ch, s1), sz1);
            if (sz2 > 0) juce::FloatVectorOperations::copy (dst + sz1, ring.getReadPointer (ch, s2), sz2);
        }

        int used = 0;
        if (numOut == 1)
        {
            // モノラル出力: L+R を混ぜてから1本に
            auto* a = temp.getWritePointer (0);
            const auto* b = temp.getReadPointer (1);
            for (int i = 0; i < numIn; ++i) a[i] = 0.5f * (a[i] + b[i]);
            used = interp[0].process (ratioSm, a, out[0], numSamples);
        }
        else
        {
            used = interp[0].process (ratioSm, temp.getReadPointer (0), out[0], numSamples);
            (void) interp[1].process (ratioSm, temp.getReadPointer (1), out[1], numSamples);
            for (int ch = 2; ch < numOut; ++ch)       // 3ch以上のデバイスは先頭2chだけ使う
                if (out[ch] != nullptr) juce::FloatVectorOperations::clear (out[ch], numSamples);
        }
        fifo.finishedRead (juce::jlimit (0, numIn, used));
    }

    static constexpr int ringLen = 1 << 16;       // 65536 サンプル ≒ 1.3秒 @48k

    // v2.3.0: AudioDeviceManager は使うときだけ作る。プラグイン版では一度も
    // 触らないので、DAW終了時にオーディオ関連の後始末が走る余地をなくす。
    std::unique_ptr<juce::AudioDeviceManager> admPtr;
    juce::AudioDeviceManager& adm()
    {
        if (admPtr == nullptr) admPtr = std::make_unique<juce::AudioDeviceManager>();
        return *admPtr;
    }
    juce::AbstractFifo       fifo { ringLen };
    juce::AudioBuffer<float> ring, temp;
    juce::LagrangeInterpolator interp[2];

    std::atomic<bool> active { false };
    std::atomic<int>  underruns { 0 };
    double hostSr = 48000.0, devSr = 48000.0, ratioSm = 1.0;
    int    target = 2880;
    juce::String deviceName_, lastError;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StreamOut)
};

} // namespace gz
