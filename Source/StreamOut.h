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
        temp.setSize (2, 16384);
        temp.clear();
    }

    ~StreamOut() override { stop(); }

    //== メッセージスレッド ==================================================

    // Windowsの「既定の再生デバイス（スピーカー）」が仮想ドライバに奪われていたら実スピーカーへ復元し、
    // 実スピーカーのWindows WDMサンプルレート（例: 48000Hz）を返す。
    static double rescueWindowsDefaultSpeakerAndGetSampleRate (juce::String* outRealSpeaker = nullptr,
                                                               juce::String* outRealMic     = nullptr);

    // 接続中のインターフェース（Roland Rubix / Yamaha AG / Steinberg UR / Focusrite Scarlett / Volt / MOTU /
    // SSL / Audient / RME / PreSonus / TASCAM / ZOOM / Behringer / USBマイク / VRヘッドセット等）の
    // Windows WDM再生・録音エンドポイントと既定スピーカーを照合・修復し、最適な同期サンプルレートと診断結果を返す。
    static double inspectAndHealWindowsAudioForInterface (const juce::String& activeDeviceName,
                                                          juce::String* outRealSpeaker = nullptr,
                                                          juce::String* outRealMic     = nullptr,
                                                          juce::String* outDiagReport  = nullptr);

    static bool isVirtualLoopbackName (const juce::String& n) noexcept
    {
        // VRヘッドセット(Oculus Virtual Audio Device等)やGPU音声、ゲーミングDACのヘッドホン側は実スピーカーなので除外
        if (n.containsIgnoreCase ("Oculus") || n.containsIgnoreCase ("Quest")
            || n.containsIgnoreCase ("Steam Streaming") || n.containsIgnoreCase ("VIVE")
            || n.containsIgnoreCase ("NVIDIA") || n.containsIgnoreCase ("AMD")
            || n.containsIgnoreCase ("Sonar - Gaming") || n.containsIgnoreCase ("Sonar - Headphone")
            || n.containsIgnoreCase ("Wave Link Monitor"))
            return false;

        return n.containsIgnoreCase ("VocalGzzio")
            || n.containsIgnoreCase ("GzzioRoom")
            || n.containsIgnoreCase ("SYNCROOM")
            || n.containsIgnoreCase ("NETDUETTO")
            || n.containsIgnoreCase ("VB-Audio")
            || n.containsIgnoreCase ("CABLE")
            || n.containsIgnoreCase ("VoiceMeeter")
            || n.containsIgnoreCase ("VAIO")
            || n.containsIgnoreCase ("BlackHole")
            || n.containsIgnoreCase ("Soundflower")
            || n.containsIgnoreCase ("Rogue Amoeba")
            || n.containsIgnoreCase ("Wave Link Stream")
            || n.containsIgnoreCase ("Broadcast Stream Mix")
            || n.containsIgnoreCase ("Loopback");
    }

    // 他アプリ(cluster / VRChat / Discord / ブラウザ等)の音を排他ロックで消してしまうラッパー系ASIOドライバ判定
    static bool isExclusiveWrapperAsioName (const juce::String& n) noexcept
    {
        return n.containsIgnoreCase ("ASIO4ALL")
            || n.containsIgnoreCase ("Realtek ASIO")
            || n.containsIgnoreCase ("Generic Low Latency ASIO")
            || n.containsIgnoreCase ("MAGIX Low Latency")
            || n.containsIgnoreCase ("DirectX Full Duplex");
    }

    // 選べる出力先の一覧。ASIO は他アプリと排他になりやすく、メイン出力と
    // 取り合いになるので、ここでは出さない(仮想ケーブルは WASAPI 等に出る)。
    // 専用仮想ドライバ「ライン (VocalGzzio Driver (WDM))」を最優先で先頭へ配置する。
    juce::StringArray getOutputDeviceNames()
    {
        juce::StringArray vocalGzzioNames, virtualNames, otherNames;
        for (auto* t : adm().getAvailableDeviceTypes())
        {
            if (t->getTypeName().containsIgnoreCase ("ASIO")) continue;
            t->scanForDevices();
            for (const auto& n : t->getDeviceNames (false))     // false = 出力側
            {
                if (n.isEmpty() || vocalGzzioNames.contains (n) || virtualNames.contains (n) || otherNames.contains (n))
                    continue;
                if (n.containsIgnoreCase ("VocalGzzio"))
                    vocalGzzioNames.add (n);
                else if (isVirtualLoopbackName (n))
                    virtualNames.add (n);
                else
                    otherNames.add (n);
            }
        }
        vocalGzzioNames.addArray (virtualNames);
        vocalGzzioNames.addArray (otherNames);
        return vocalGzzioNames;
    }

    juce::String findVocalGzzioDriverDevice()
    {
        const auto names = getOutputDeviceNames();
        for (const auto& n : names)
            if (n.containsIgnoreCase ("VocalGzzio")) return n;
        return {};
    }

    juce::String findPreferredVirtualDevice()
    {
        const auto names = getOutputDeviceNames();
        for (const auto& n : names)
            if (n.containsIgnoreCase ("VocalGzzio")) return n;
        for (const auto& n : names)
            if (n.containsIgnoreCase ("GzzioRoom")) return n;
        for (const auto& n : names)
            if (n.containsIgnoreCase ("SYNCROOM")) return n;
        for (const auto& n : names)
            if (isVirtualLoopbackName (n)) return n;
        return {};
    }

    static bool installVocalGzzioWdmDriver()
    {
       #if JUCE_WINDOWS
        const auto exeDir = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory();
        juce::File installer = exeDir.getChildFile ("VocalGzzioWdmInstaller.exe");
        if (! installer.existsAsFile())
            installer = juce::File ("C:\\Program Files\\VocalGzzio\\VocalGzzioWdmInstaller.exe");
        if (! installer.existsAsFile())
            return false;

        juce::ChildProcess proc;
        if (proc.start ("\"" + installer.getFullPathName() + "\" --install"))
        {
            proc.waitForProcessToFinish (25000);
            rescueWindowsDefaultSpeakerAndGetSampleRate();
            return proc.getExitCode() == 0;
        }
       #endif
        return false;
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

        int devBufSamples = 480;
        if (auto* dev = adm().getCurrentAudioDevice())
        {
            devSr = dev->getCurrentSampleRate() > 0.0 ? dev->getCurrentSampleRate() : hostSr;
            devBufSamples = juce::jmax (128, dev->getCurrentBufferSizeSamples());
        }
        else
        {
            devSr = hostSr;
        }
        lastDevBufSamples = devBufSamples;

        // FIFO を目標量(約35ms または デバイスバッファ×3 の大きい方)まで無音で満たしてから流し始める。
        // 低遅延を保ちつつ、2スレッド間の起床タイミング揺らぎや長時間稼働での在庫切れ・プチノイズを防ぐ。
        fifo.reset();
        ring.clear();
        target = juce::jlimit (512, ringLen / 4, juce::jmax ((int) (hostSr * 0.035), devBufSamples * 3));
        {
            int s1, sz1, s2, sz2;
            fifo.prepareToWrite (target, s1, sz1, s2, sz2);
            fifo.finishedWrite (sz1 + sz2);
        }
        fillSm = (double) target;
        lastOut[0] = lastOut[1] = 0.0f;
        needCrossfade = false;
        ratioSm = hostSr / juce::jmax (8000.0, devSr);
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

    bool isRunning() const noexcept
    {
        return active.load() && admPtr != nullptr && admPtr->getCurrentAudioDevice() != nullptr
               && admPtr->getCurrentAudioDevice()->isOpen();
    }
    juce::String getDeviceName() const       { return deviceName_; }
    juce::String getMirrorDeviceName() const { return {}; }
    juce::String getLastError() const        { return lastError; }
    double getDeviceSampleRate() const noexcept { return devSr; }
    int  getUnderrunCount() const noexcept   { return underruns.load(); }
    bool isForceMono() const noexcept        { return forceMono.load (std::memory_order_relaxed); }
    void setForceMono (bool b) noexcept
    {
        forceMono.store (b, std::memory_order_relaxed);
    }

    void setHostSampleRate (double sr) noexcept
    {
        if (sr > 0.0 && std::abs (sr - hostSr) > 1.0)
        {
            hostSr = sr;
            target = juce::jlimit (512, ringLen / 4, juce::jmax ((int) (hostSr * 0.035), lastDevBufSamples * 3));
            fillSm = (double) target;
            if (devSr > 0.0)
                ratioSm = juce::jlimit (0.2, 4.5, hostSr / devSr);
        }
        else if (sr > 0.0)
        {
            hostSr = sr;
        }
    }

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
            lastDevBufSamples = juce::jmax (128, dev->getCurrentBufferSizeSamples());
            target = juce::jlimit (512, ringLen / 4, juce::jmax ((int) (hostSr * 0.035), lastDevBufSamples * 3));
            fillSm = (double) target;
            const int want = juce::jmax (4096, lastDevBufSamples * 8 + 64);
            if (temp.getNumSamples() < want) temp.setSize (2, want, false, false, true);
        }
        lastOut[0] = lastOut[1] = 0.0f;
        needCrossfade = false;
        ratioSm = juce::jlimit (0.2, 4.5, hostSr / juce::jmax (8000.0, devSr));
    }

    void audioDeviceStopped() override {}

    void audioDeviceIOCallbackWithContext (const float* const*, int,
                                           float* const* out, int numOut, int numSamples,
                                           const juce::AudioIODeviceCallbackContext&) override
    {
        for (int ch = 0; ch < numOut; ++ch)
            if (out[ch] != nullptr) juce::FloatVectorOperations::clear (out[ch], numSamples);

        if (! active.load() || numOut < 1 || numSamples <= 0) return;

        int fill = fifo.getNumReady();

        // OSやVRワールド読み込み等でWASAPIスレッドが一時停止し、FIFOに過大な遅延が溜まった場合は
        // 目標量までスキップし、クロスフェードでプチノイズ無しに復帰する
        const int maxAllowedFill = juce::jmax (target * 3, 4096);
        if (fill > maxAllowedFill)
        {
            const int drop = fill - target;
            int ds1, dsz1, ds2, dsz2;
            fifo.prepareToRead (drop, ds1, dsz1, ds2, dsz2);
            fifo.finishedRead (dsz1 + dsz2);
            fill = fifo.getNumReady();
            fillSm = (double) target;
            for (auto& i : interp) i.reset();
            needCrossfade = true;
        }

        // --- クロックずれの吸収: 2スレッド間のブロック起床ジッターを平滑化してから再生速度を微調整 ---
        fillSm += 0.015 * ((double) fill - fillSm);
        const double diff = fillSm - (double) target;
        const double deadband = 64.0;
        const double effDiff = (std::abs (diff) <= deadband) ? 0.0
                             : (diff > 0.0 ? diff - deadband : diff + deadband);
        const double err  = effDiff / (double) juce::jmax (1, target);
        const double corr = juce::jlimit (-0.0025, 0.0025, err * 0.015);   // 最大±0.25%の超滑らか追従
        const double want = (hostSr / juce::jmax (8000.0, devSr)) * (1.0 + corr);
        ratioSm += 0.015 * (want - ratioSm);
        ratioSm = juce::jlimit (0.2, 4.5, ratioSm);

        const int numIn = (int) std::ceil ((double) numSamples * ratioSm) + 2;
        if (numIn > temp.getNumSamples() || fill < numIn)
        {
            underruns.fetch_add (1);
            // 在庫切れ時: いきなり0埋めすると境界でチリチリ・プチノイズが出るため、
            // 直前サンプルから滑らかに減衰させつつ次回分の安全クッションを回復する
            for (int i = 0; i < numSamples; ++i)
            {
                lastOut[0] *= 0.985f;
                lastOut[1] *= 0.985f;
                if (out[0] != nullptr) out[0][i] = lastOut[0];
                if (numOut > 1 && out[1] != nullptr) out[1][i] = lastOut[1];
            }
            for (auto& i : interp) i.reset();
            fillSm = (double) target;
            needCrossfade = true;
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

        if (forceMono.load (std::memory_order_relaxed))
        {
            auto* a = temp.getWritePointer (0);
            auto* b = temp.getWritePointer (1);
            for (int i = 0; i < numIn; ++i)
            {
                const float m = 0.5f * (a[i] + b[i]);
                a[i] = m;
                b[i] = m;
            }
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

        if (needCrossfade)
        {
            const int xfadeLen = juce::jmin (numSamples, 64);
            for (int i = 0; i < xfadeLen; ++i)
            {
                const float g = (float) (i + 1) / (float) (xfadeLen + 1);
                if (out[0] != nullptr)
                    out[0][i] = lastOut[0] * (1.0f - g) + out[0][i] * g;
                if (numOut > 1 && out[1] != nullptr)
                    out[1][i] = lastOut[1] * (1.0f - g) + out[1][i] * g;
            }
            needCrossfade = false;
        }
        if (out[0] != nullptr)
            lastOut[0] = out[0][numSamples - 1];
        if (numOut > 1 && out[1] != nullptr)
            lastOut[1] = out[1][numSamples - 1];
        else
            lastOut[1] = lastOut[0];
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
    std::atomic<bool> forceMono { false };
    std::atomic<int>  underruns { 0 };
    double hostSr = 48000.0, devSr = 48000.0, ratioSm = 1.0, fillSm = 1680.0;
    int    target = 1680, lastDevBufSamples = 480;
    float  lastOut[2] { 0.0f, 0.0f };
    bool   needCrossfade = false;
    juce::String deviceName_, lastError;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StreamOut)
};

} // namespace gz
