#pragma once
// ============================================================================
// AppAudioCapture (v5.5.0) — アプリ別カラオケ音声ダイレクト取り込みエンジン
//
// 目的:
//   YouTube公式オフボーカル（Chrome / Edge / Brave / Firefox 等）や
//   JOYSOUND for Streamer / カラオケ@DAM / Spotify / メディアプレイヤー等の音声だけを、
//   Discord通話・通知音や VocalGzzio 自身のマイク出力を一切混ぜずに直接取り込み、
//   VocalGzzio 内蔵BGMパイプライン（キー変更±6・ボーカル除去・声ポケット自動掘り・
//   伴奏追従ライダー・サビ自動検知・伴奏コード追従ハモリ・音ズレ補正・完パケ録音/SNS動画化）
//   へゼロ確保・ロックフリーで供給する。
//
// 方式:
//   Windows 10 (Build 20348+) / Windows 11 の Process Loopback Capture API
//   (VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK + AUDIOCLIENT_PROCESS_LOOPBACK_PARAMS) を使用。
//   ・特定アプリ指定 (targetPid > 0): そのアプリのプロセスツリー全体 (ChromeのAudioService子プロセス含む) のみ取得。
//   ・PC再生音モード (targetPid == 0): VocalGzzio自身のプロセスツリーだけを自動除外してPC再生音を取得（発振ループ100%防止）。
// ============================================================================

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <atomic>
#include <cmath>
#include <vector>
#include <map>

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
 #include <mmdeviceapi.h>
 #include <audioclient.h>
 #include <tlhelp32.h>
 #include <psapi.h>
 #ifdef small
  #undef small
 #endif
 #ifdef near
  #undef near
 #endif
#endif

namespace gz
{

class AppAudioCapture
{
public:
    struct AppEntry
    {
        juce::uint32 pid = 0;        // 0 = PC再生音（VocalGzzio自身を自動除外）
        juce::String processName;    // 例: "chrome.exe"
        juce::String windowTitle;    // 例: "YouTube - Google Chrome"
        juce::String displayLabel;   // メニュー表示用ラベル
    };

    AppAudioCapture()
    {
        ring.setSize (2, kRingLen);
        ring.clear();
        resampSrc.setSize (2, 4096);
        resampSrc.clear();
    }

    ~AppAudioCapture()
    {
        stop();
    }

    static bool isSupportedOnCurrentOs() noexcept
    {
       #if JUCE_WINDOWS
        return true;
       #else
        return false;
       #endif
    }

    // 現在起動中の主要ブラウザ・カラオケ・音楽アプリ一覧を列挙（同一アプリの子プロセスはルートPIDへ統合）
    static std::vector<AppEntry> enumerateRunningAudioApps (bool english = false)
    {
        std::vector<AppEntry> list;
       #if JUCE_WINDOWS
        // 先頭に「PC全体のカラオケ・再生音（VocalGzzio自身の声は自動除外）」を用意
        {
            AppEntry allOther;
            allOther.pid = 0;
            allOther.processName = "PC Loopback";
            allOther.windowTitle = english ? "All PC Playback (Auto-excludes VocalGzzio)"
                                           : "PC再生音すべて (VocalGzzio自身の音は自動除外)";
            allOther.displayLabel = english ? "🖥️ All PC Audio (Excludes VocalGzzio — No Feedback)"
                                            : "🖥️ PC上の再生音すべて（自分の声は自動除外・ハウリングなし）";
            list.push_back (allOther);
        }

        // プロセス親子マップを作成し、Chromium系ブラウザ等の子ウィンドウPIDも最上位の同名親プロセスPIDへ集約する
        std::map<DWORD, DWORD> parentOf;
        std::map<DWORD, juce::String> exeOf;
        HANDLE snap = CreateToolhelp32Snapshot (TH32CS_SNAPPROCESS, 0);
        if (snap != INVALID_HANDLE_VALUE)
        {
            PROCESSENTRY32W pe {};
            pe.dwSize = sizeof (pe);
            if (Process32FirstW (snap, &pe))
            {
                do
                {
                    parentOf[pe.th32ProcessID] = pe.th32ParentProcessID;
                    exeOf[pe.th32ProcessID]    = juce::String (pe.szExeFile);
                }
                while (Process32NextW (snap, &pe));
            }
            CloseHandle (snap);
        }

        auto resolveRootPid = [&] (DWORD pid) -> DWORD
        {
            const auto itExe = exeOf.find (pid);
            if (itExe == exeOf.end()) return pid;
            const juce::String targetExe = itExe->second;
            DWORD cur = pid;
            for (int depth = 0; depth < 8; ++depth)
            {
                const auto itP = parentOf.find (cur);
                if (itP == parentOf.end() || itP->second == 0 || itP->second == cur)
                    break;
                const auto itParentExe = exeOf.find (itP->second);
                if (itParentExe != exeOf.end() && itParentExe->second.equalsIgnoreCase (targetExe))
                    cur = itP->second;
                else
                    break;
            }
            return cur;
        };

        struct EnumCtx
        {
            DWORD selfPid = 0;
            std::map<DWORD, juce::String> bestTitleByPid;
        } ctx;
        ctx.selfPid = GetCurrentProcessId();

        EnumWindows ([] (HWND hwnd, LPARAM lParam) -> BOOL
        {
            auto* c = reinterpret_cast<EnumCtx*> (lParam);
            if (! IsWindowVisible (hwnd)) return TRUE;
            const int len = GetWindowTextLengthW (hwnd);
            if (len <= 0) return TRUE;

            DWORD pid = 0;
            GetWindowThreadProcessId (hwnd, &pid);
            if (pid == 0 || pid == c->selfPid) return TRUE;

            std::vector<wchar_t> buf ((size_t) len + 2, L'\0');
            GetWindowTextW (hwnd, buf.data(), len + 1);
            juce::String title (buf.data());
            title = title.trim();
            if (title.isEmpty() || title == "Program Manager" || title == "Windows Input Experience")
                return TRUE;

            auto& slot = c->bestTitleByPid[pid];
            if (slot.isEmpty() || title.containsIgnoreCase ("YouTube") || title.containsIgnoreCase ("JOYSOUND")
                || title.containsIgnoreCase ("カラオケ") || title.containsIgnoreCase ("Karaoke"))
            {
                slot = title;
            }
            return TRUE;
        }, reinterpret_cast<LPARAM> (&ctx));

        std::map<DWORD, AppEntry> merged;
        const DWORD selfRoot = resolveRootPid (ctx.selfPid);
        for (const auto& kv : ctx.bestTitleByPid)
        {
            const DWORD rootPid = resolveRootPid (kv.first);
            if (rootPid == 0 || rootPid == ctx.selfPid || rootPid == selfRoot)
                continue;

            const juce::String exe = exeOf.count (rootPid) ? exeOf[rootPid] : juce::String ("app.exe");
            const juce::String low = exe.toLowerCase();
            // OSシェルやシステム裏画面プロセスは除外
            if (low == "explorer.exe" || low == "applicationframehost.exe" || low == "textinputhost.exe"
                || low == "systemsettings.exe" || low == "searchhost.exe" || low == "shellexperiencehost.exe"
                || low == "taskmgr.exe" || low == "cmd.exe" || low == "powershell.exe" || low == "pwsh.exe")
                continue;

            auto& dst = merged[rootPid];
            if (dst.pid == 0 || kv.second.containsIgnoreCase ("YouTube") || kv.second.containsIgnoreCase ("JOYSOUND"))
            {
                dst.pid         = (juce::uint32) rootPid;
                dst.processName = exe;
                dst.windowTitle = kv.second.substring (0, 54);
                const bool isPriorityApp = low.contains ("chrome") || low.contains ("msedge") || low.contains ("brave")
                    || low.contains ("firefox") || low.contains ("opera") || low.contains ("vivaldi")
                    || low.contains ("joysound") || low.contains ("dam") || low.contains ("spotify")
                    || low.contains ("vlc") || low.contains ("itunes") || low.contains ("music");
                const juce::String icon = isPriorityApp ? "🎵 " : "🪟 ";
                dst.displayLabel = icon + exe + " — " + dst.windowTitle;
            }
        }

        // カラオケ・ブラウザ系を先頭に並べる
        std::vector<AppEntry> priorityList, normalList;
        for (auto& kv : merged)
        {
            const juce::String l = (kv.second.processName + " " + kv.second.windowTitle).toLowerCase();
            if (l.contains ("youtube") || l.contains ("joysound") || l.contains ("dam") || l.contains ("karaoke")
                || l.contains ("カラオケ") || l.contains ("chrome") || l.contains ("msedge")
                || l.contains ("firefox") || l.contains ("brave") || l.contains ("spotify") || l.contains ("music"))
                priorityList.push_back (kv.second);
            else
                normalList.push_back (kv.second);
        }
        list.insert (list.end(), priorityList.begin(), priorityList.end());
        list.insert (list.end(), normalList.begin(), normalList.end());
       #else
        juce::ignoreUnused (english);
       #endif
        return list;
    }

    // targetPid == 0: VocalGzzio自身の音を除外したPC再生音すべて
    // targetPid  > 0: 指定アプリのプロセスツリー音声のみ
    bool start (juce::uint32 targetPid, const juce::String& labelText)
    {
        stop();
       #if JUCE_WINDOWS
        targetPid_ = targetPid;
        {
            const juce::ScopedLock sl (labelLock);
            activeLabel_ = labelText.isNotEmpty() ? labelText : juce::String ("App Audio");
            lastError_.clear();
        }
        fifo.reset();
        ring.clear();
        needPrebuffer = true;
        needResumeCrossfade = true;
        overflowCrossfade.store (false, std::memory_order_relaxed);
        primedSamples = 0;
        lastOut[0] = lastOut[1] = 0.0f;
        for (auto& ip : interp) ip.reset();

        stopSignal.store (false, std::memory_order_release);
        startReadyEvent = CreateEventW (nullptr, TRUE, FALSE, nullptr);
        startSucceeded.store (false, std::memory_order_relaxed);

        workerThread = std::make_unique<CaptureThread> (*this);
        workerThread->startThread (juce::Thread::Priority::highest);

        if (startReadyEvent != nullptr)
        {
            WaitForSingleObject (startReadyEvent, 4000);
            CloseHandle (startReadyEvent);
            startReadyEvent = nullptr;
        }
        if (! startSucceeded.load (std::memory_order_acquire))
        {
            stop();
            return false;
        }
        return true;
       #else
        juce::ignoreUnused (targetPid, labelText);
        return false;
       #endif
    }

    void stop()
    {
       #if JUCE_WINDOWS
        stopSignal.store (true, std::memory_order_release);
        if (audioWakeEvent != nullptr)
            SetEvent (audioWakeEvent);
        if (workerThread != nullptr)
        {
            workerThread->stopThread (2500);
            workerThread.reset();
        }
        if (startReadyEvent != nullptr)
        {
            CloseHandle (startReadyEvent);
            startReadyEvent = nullptr;
        }
       #endif
        capturing.store (false, std::memory_order_release);
        fifo.reset();
    }

    bool isCapturing() const noexcept           { return capturing.load (std::memory_order_acquire); }
    juce::uint32 getTargetPid() const noexcept  { return targetPid_; }

    juce::String getActiveLabel() const
    {
        const juce::ScopedLock sl (labelLock);
        return activeLabel_;
    }

    juce::String getLastError() const
    {
        const juce::ScopedLock sl (labelLock);
        return lastError_;
    }

    //== オーディオスレッド（完全ロックフリー・ヒープ確保ゼロ・クロックドリフト自動吸収） ==
    // 単体テストや外部注入にも対応できるよう pushCapturedStereo も公開
    void pushCapturedStereo (const float* inL, const float* inR, int numFrames, double captureSampleRate) noexcept
    {
        if (numFrames <= 0) return;
        if (captureSampleRate > 8000.0)
            captureSr.store (captureSampleRate, std::memory_order_relaxed);

        const int freeSpace = fifo.getFreeSpace();
        if (numFrames > freeSpace)
        {
            const int drop = juce::jmin (fifo.getNumReady(), numFrames - freeSpace + 512);
            if (drop > 0)
            {
                int s1, sz1, s2, sz2;
                fifo.prepareToRead (drop, s1, sz1, s2, sz2);
                fifo.finishedRead (sz1 + sz2);
                overflowCrossfade.store (true, std::memory_order_relaxed);
            }
        }
        const int toWrite = juce::jmin (numFrames, fifo.getFreeSpace());
        if (toWrite <= 0) return;

        int s1, sz1, s2, sz2;
        fifo.prepareToWrite (toWrite, s1, sz1, s2, sz2);
        if (sz1 > 0)
        {
            juce::FloatVectorOperations::copy (ring.getWritePointer (0, s1), inL, sz1);
            juce::FloatVectorOperations::copy (ring.getWritePointer (1, s1), inR, sz1);
        }
        if (sz2 > 0)
        {
            juce::FloatVectorOperations::copy (ring.getWritePointer (0, s2), inL + sz1, sz2);
            juce::FloatVectorOperations::copy (ring.getWritePointer (1, s2), inR + sz1, sz2);
        }
        fifo.finishedWrite (sz1 + sz2);
    }

    bool pullStereo (float* outL, float* outR, int numSamples, double hostSampleRate) noexcept
    {
        if (numSamples <= 0 || outL == nullptr || outR == nullptr) return false;
        const double hSr = hostSampleRate > 8000.0 ? hostSampleRate : 48000.0;
        const double cSr = captureSr.load (std::memory_order_relaxed);
        const double srcSr = cSr > 8000.0 ? cSr : hSr;

        const int targetFill = juce::jlimit (512, kRingLen / 4, (int) (srcSr * 0.028)); // 約28msの低遅延ジッタ吸収窓
        int avail = fifo.getNumReady();

        // ホスト一時停止等で過剰な遅延(約110ms超)が溜まった場合は目標クッションまでスキップしてクロスフェード復帰
        const int maxAllowedFill = juce::jmax (targetFill * 4, 5400);
        if (avail > maxAllowedFill)
        {
            const int drop = avail - targetFill;
            int ds1, dsz1, ds2, dsz2;
            fifo.prepareToRead (drop, ds1, dsz1, ds2, dsz2);
            fifo.finishedRead (dsz1 + dsz2);
            avail = fifo.getNumReady();
            fillSm = (double) avail;
            for (auto& ip : interp) ip.reset();
            needResumeCrossfade = true;
        }

        if (overflowCrossfade.exchange (false, std::memory_order_relaxed))
        {
            for (auto& ip : interp) ip.reset();
            needResumeCrossfade = true;
        }

        if (needPrebuffer)
        {
            if (avail < targetFill)
            {
                // プリバッファ再充填中も直前サンプルから連続減衰させ、境界の段差クリックをゼロにする
                for (int i = 0; i < numSamples; ++i)
                {
                    lastOut[0] = (std::abs (lastOut[0]) > 1.0e-6f) ? (lastOut[0] * 0.96f) : 0.0f;
                    lastOut[1] = (std::abs (lastOut[1]) > 1.0e-6f) ? (lastOut[1] * 0.96f) : 0.0f;
                    outL[i] = lastOut[0];
                    outR[i] = lastOut[1];
                }
                return capturing.load (std::memory_order_relaxed);
            }
            needPrebuffer = false;
            needResumeCrossfade = true;
            for (auto& ip : interp) ip.reset();
            fillSm = (double) avail;
            ratioSm = srcSr / hSr;
        }

        fillSm += 0.03 * ((double) avail - fillSm);
        const double nominal = srcSr / hSr;
        const double err = (fillSm - (double) targetFill) / (double) juce::jmax (256, targetFill);
        const double adj = juce::jlimit (-0.004, 0.004, err * 0.0018);
        const double wantRatio = nominal * (1.0 + adj);
        ratioSm += 0.10 * (wantRatio - ratioSm);
        const double ratio = juce::jlimit (nominal * 0.985, nominal * 1.015, ratioSm);

        const int maxChunk = resampSrc.getNumSamples();
        int done = 0;
        while (done < numSamples)
        {
            const int chunkOut = juce::jmin (numSamples - done, 1024);
            const int needIn   = juce::jmin (maxChunk, (int) std::ceil (chunkOut * ratio) + 6);
            const int readyNow = fifo.getNumReady();
            if (readyNow < needIn)
            {
                // 単発OSジッタやブラウザ無音で readyNow が中途半端な値(1..needIn-1)に落ちた場合も、
                // 必ず needPrebuffer=true に戻して28msクッションを再構築し、枯渇境界での連続パリパリを根絶する
                const int rem = numSamples - done;
                for (int i = 0; i < rem; ++i)
                {
                    lastOut[0] = (std::abs (lastOut[0]) > 1.0e-6f) ? (lastOut[0] * 0.96f) : 0.0f;
                    lastOut[1] = (std::abs (lastOut[1]) > 1.0e-6f) ? (lastOut[1] * 0.96f) : 0.0f;
                    outL[done + i] = lastOut[0];
                    outR[done + i] = lastOut[1];
                }
                needPrebuffer = true;
                needResumeCrossfade = true;
                break;
            }

            int s1, sz1, s2, sz2;
            const int peekN = juce::jmin (needIn, readyNow);
            fifo.prepareToRead (peekN, s1, sz1, s2, sz2);
            if (sz1 > 0)
            {
                resampSrc.copyFrom (0, 0, ring, 0, s1, sz1);
                resampSrc.copyFrom (1, 0, ring, 1, s1, sz1);
            }
            if (sz2 > 0)
            {
                resampSrc.copyFrom (0, sz1, ring, 0, s2, sz2);
                resampSrc.copyFrom (1, sz1, ring, 1, s2, sz2);
            }

            const int used0 = interp[0].process (ratio, resampSrc.getReadPointer (0), outL + done, chunkOut, peekN, 0);
            const int used1 = interp[1].process (ratio, resampSrc.getReadPointer (1), outR + done, chunkOut, peekN, 0);
            const int consumed = juce::jlimit (0, sz1 + sz2, juce::jmax (used0, used1));
            fifo.finishedRead (consumed);

            if (needResumeCrossfade)
            {
                const int xfadeLen = juce::jmin (chunkOut, 64);
                const float startL = lastOut[0];
                const float startR = lastOut[1];
                for (int i = 0; i < xfadeLen; ++i)
                {
                    const float g = (float) (i + 1) / (float) (xfadeLen + 1);
                    outL[done + i] = startL * (1.0f - g) + outL[done + i] * g;
                    outR[done + i] = startR * (1.0f - g) + outR[done + i] * g;
                }
                needResumeCrossfade = false;
            }

            lastOut[0] = outL[done + chunkOut - 1];
            lastOut[1] = outR[done + chunkOut - 1];
            done += chunkOut;
        }
        return true;
    }

private:
    static constexpr int kRingLen = 65536;
    juce::AbstractFifo       fifo { kRingLen };
    juce::AudioBuffer<float> ring;
    juce::AudioBuffer<float> resampSrc;
    juce::Interpolators::Lagrange interp[2];
    std::atomic<double>      captureSr { 48000.0 };
    std::atomic<bool>        capturing { false };
    std::atomic<bool>        overflowCrossfade { false };
    bool                     needPrebuffer { true };
    bool                     needResumeCrossfade { true };
    int                      primedSamples { 0 };
    double                   fillSm { 0.0 };
    double                   ratioSm { 1.0 };
    float                    lastOut[2] { 0.0f, 0.0f };
    juce::uint32             targetPid_ { 0 };
    mutable juce::CriticalSection labelLock;
    juce::String             activeLabel_;
    juce::String             lastError_;

   #if JUCE_WINDOWS
    struct GzProcessLoopbackParams
    {
        DWORD TargetProcessId;
        int   ProcessLoopbackMode; // 0 = INCLUDE_TARGET_PROCESS_TREE, 1 = EXCLUDE_TARGET_PROCESS_TREE
    };

    struct GzAudioClientActivationParams
    {
        int                     ActivationType; // 1 = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK
        GzProcessLoopbackParams ProcessLoopbackParams;
    };

    // Process Loopback 非同期アクティベーション完了ハンドラ（IAgileObject 実装必須）
    class LoopbackActivationHandler final : public IActivateAudioInterfaceCompletionHandler
    {
    public:
        LoopbackActivationHandler()
        {
            doneEvent = CreateEventW (nullptr, TRUE, FALSE, nullptr);
        }

        ~LoopbackActivationHandler()
        {
            if (client != nullptr) client->Release();
            if (doneEvent != nullptr) CloseHandle (doneEvent);
        }

        HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, void** ppvObject) override
        {
            if (ppvObject == nullptr) return E_POINTER;
            // IID_IAgileObject: {94ea2b94-e9cc-49e0-c0ff-ee64ca8f5b90}
            static const GUID kIidAgileObject =
                { 0x94ea2b94, 0xe9cc, 0x49e0, { 0xc0, 0xff, 0xee, 0x64, 0xca, 0x8f, 0x5b, 0x90 } };
            if (riid == __uuidof (IUnknown)
                || riid == __uuidof (IActivateAudioInterfaceCompletionHandler)
                || IsEqualGUID (riid, kIidAgileObject))
            {
                *ppvObject = static_cast<IActivateAudioInterfaceCompletionHandler*> (this);
                AddRef();
                return S_OK;
            }
            *ppvObject = nullptr;
            return E_NOINTERFACE;
        }

        ULONG STDMETHODCALLTYPE AddRef() override  { return (ULONG) InterlockedIncrement (&refCount); }
        ULONG STDMETHODCALLTYPE Release() override
        {
            const ULONG r = (ULONG) InterlockedDecrement (&refCount);
            if (r == 0) delete this;
            return r;
        }

        HRESULT STDMETHODCALLTYPE ActivateCompleted (IActivateAudioInterfaceAsyncOperation* operation) override
        {
            if (operation != nullptr)
            {
                HRESULT hrAct = E_FAIL;
                IUnknown* punk = nullptr;
                const HRESULT hr = operation->GetActivateResult (&hrAct, &punk);
                activateHr = SUCCEEDED (hr) ? hrAct : hr;
                if (SUCCEEDED (activateHr) && punk != nullptr)
                    punk->QueryInterface (__uuidof (IAudioClient), reinterpret_cast<void**> (&client));
                if (punk != nullptr)
                    punk->Release();
            }
            if (doneEvent != nullptr)
                SetEvent (doneEvent);
            return S_OK;
        }

        LONG          refCount   { 1 };
        HANDLE        doneEvent  { nullptr };
        HRESULT       activateHr { E_FAIL };
        IAudioClient* client     { nullptr };
    };

    class CaptureThread final : public juce::Thread
    {
    public:
        explicit CaptureThread (AppAudioCapture& o) : juce::Thread ("VocalGzzioAppAudioCapture"), owner (o) {}
        void run() override { owner.captureLoop(); }
    private:
        AppAudioCapture& owner;
    };

    std::unique_ptr<CaptureThread> workerThread;
    std::atomic<bool>              stopSignal { false };
    std::atomic<bool>              startSucceeded { false };
    HANDLE                         startReadyEvent { nullptr };
    HANDLE                         audioWakeEvent { nullptr };

    void setErrorText (const juce::String& err)
    {
        const juce::ScopedLock sl (labelLock);
        lastError_ = err;
    }

    void captureLoop()
    {
        const HRESULT hrCom = CoInitializeEx (nullptr, COINIT_MULTITHREADED);
        const bool shouldUninit = SUCCEEDED (hrCom);

        IAudioClient* audioClient = nullptr;
        IAudioCaptureClient* captureClient = nullptr;
        std::vector<float> tmpL (4096, 0.0f), tmpR (4096, 0.0f);

        using PFN_ActivateAudioInterfaceAsync = HRESULT (WINAPI*) (
            LPCWSTR, REFIID, PROPVARIANT*, IActivateAudioInterfaceCompletionHandler*, IActivateAudioInterfaceAsyncOperation**);

        HMODULE hMmdev = GetModuleHandleW (L"Mmdevapi.dll");
        if (hMmdev == nullptr)
            hMmdev = LoadLibraryW (L"Mmdevapi.dll");
        auto fnActivate = hMmdev != nullptr
            ? reinterpret_cast<PFN_ActivateAudioInterfaceAsync> (GetProcAddress (hMmdev, "ActivateAudioInterfaceAsync"))
            : nullptr;

        if (fnActivate != nullptr)
        {
            GzAudioClientActivationParams params {};
            params.ActivationType = 1; // AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK
            if (targetPid_ > 0)
            {
                params.ProcessLoopbackParams.TargetProcessId     = (DWORD) targetPid_;
                params.ProcessLoopbackParams.ProcessLoopbackMode = 0; // INCLUDE_TARGET_PROCESS_TREE
            }
            else
            {
                params.ProcessLoopbackParams.TargetProcessId     = GetCurrentProcessId();
                params.ProcessLoopbackParams.ProcessLoopbackMode = 1; // EXCLUDE_TARGET_PROCESS_TREE
            }

            PROPVARIANT activateProp {};
            activateProp.vt             = VT_BLOB;
            activateProp.blob.cbSize    = sizeof (params);
            activateProp.blob.pBlobData = reinterpret_cast<BYTE*> (&params);

            auto* handler = new LoopbackActivationHandler();
            IActivateAudioInterfaceAsyncOperation* asyncOp = nullptr;
            const HRESULT hrStart = fnActivate (L"VAD\\Process_Loopback",
                                                __uuidof (IAudioClient),
                                                &activateProp,
                                                handler,
                                                &asyncOp);
            if (SUCCEEDED (hrStart))
            {
                WaitForSingleObject (handler->doneEvent, 3500);
                if (SUCCEEDED (handler->activateHr) && handler->client != nullptr)
                {
                    audioClient = handler->client;
                    audioClient->AddRef();
                }
            }
            if (asyncOp != nullptr) asyncOp->Release();
            handler->Release();
        }

        // Process Loopback では GetMixFormat が E_NOTIMPL を返すため、標準の IEEE Float 48kHz ステレオで初期化する
        WAVEFORMATEX fmt {};
        fmt.wFormatTag      = WAVE_FORMAT_IEEE_FLOAT;
        fmt.nChannels       = 2;
        fmt.nSamplesPerSec  = 48000;
        fmt.wBitsPerSample  = 32;
        fmt.nBlockAlign     = (WORD) (fmt.nChannels * (fmt.wBitsPerSample / 8));
        fmt.nAvgBytesPerSec = fmt.nSamplesPerSec * fmt.nBlockAlign;
        fmt.cbSize          = 0;

        bool isFloat32 = true;
        int  numCh     = 2;
        double devSr   = 48000.0;

        // 古い Windows 10 環境等で Process Loopback が未対応だった場合のフォールバック（既定スピーカーのWASAPIループバック）
        if (audioClient == nullptr && targetPid_ == 0)
        {
            IMMDeviceEnumerator* enumerator = nullptr;
            IMMDevice* endpoint = nullptr;
            if (SUCCEEDED (CoCreateInstance (__uuidof (MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                             __uuidof (IMMDeviceEnumerator), reinterpret_cast<void**> (&enumerator)))
                && enumerator != nullptr)
            {
                if (SUCCEEDED (enumerator->GetDefaultAudioEndpoint (eRender, eConsole, &endpoint)) && endpoint != nullptr)
                {
                    endpoint->Activate (__uuidof (IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**> (&audioClient));
                    endpoint->Release();
                }
                enumerator->Release();
            }
            if (audioClient != nullptr)
            {
                WAVEFORMATEX* mixFmt = nullptr;
                if (SUCCEEDED (audioClient->GetMixFormat (&mixFmt)) && mixFmt != nullptr)
                {
                    fmt = *mixFmt;
                    fmt.cbSize = 0;
                    numCh = fmt.nChannels;
                    devSr = (double) fmt.nSamplesPerSec;
                    if (mixFmt->wFormatTag == WAVE_FORMAT_EXTENSIBLE && mixFmt->cbSize >= 22)
                    {
                        auto* ext = reinterpret_cast<WAVEFORMATEXTENSIBLE*> (mixFmt);
                        static const GUID kSubFloat = { 0x00000003, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };
                        isFloat32 = IsEqualGUID (ext->SubFormat, kSubFloat) && mixFmt->wBitsPerSample == 32;
                    }
                    else
                    {
                        isFloat32 = (mixFmt->wFormatTag == WAVE_FORMAT_IEEE_FLOAT && mixFmt->wBitsPerSample == 32);
                    }
                    CoTaskMemFree (mixFmt);
                }
            }
        }

        if (audioClient == nullptr)
        {
            setErrorText ("Windows Process Loopback API unavailable");
            if (startReadyEvent != nullptr) SetEvent (startReadyEvent);
            if (shouldUninit) CoUninitialize();
            return;
        }

        audioWakeEvent = CreateEventW (nullptr, FALSE, FALSE, nullptr);
        const DWORD streamFlags = AUDCLNT_STREAMFLAGS_LOOPBACK
                                | AUDCLNT_STREAMFLAGS_EVENTCALLBACK
                                | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM
                                | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
        // AUTOCONVERTPCM により常に IEEE Float32 48kHz Stereo へOS側で変換させる
        fmt.wFormatTag      = WAVE_FORMAT_IEEE_FLOAT;
        fmt.nChannels       = 2;
        fmt.nSamplesPerSec  = 48000;
        fmt.wBitsPerSample  = 32;
        fmt.nBlockAlign     = 8;
        fmt.nAvgBytesPerSec = 48000 * 8;
        fmt.cbSize          = 0;
        isFloat32 = true;
        numCh     = 2;
        devSr     = 48000.0;

        HRESULT hrInit = audioClient->Initialize (AUDCLNT_SHAREMODE_SHARED, streamFlags, 200000, 0, &fmt, nullptr);
        if (FAILED (hrInit))
        {
            // AUTOCONVERTPCM 非対応ドライバ向けリトライ
            hrInit = audioClient->Initialize (AUDCLNT_SHAREMODE_SHARED,
                                              AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                                              200000, 0, &fmt, nullptr);
        }

        if (FAILED (hrInit)
            || FAILED (audioClient->SetEventHandle (audioWakeEvent))
            || FAILED (audioClient->GetService (__uuidof (IAudioCaptureClient), reinterpret_cast<void**> (&captureClient)))
            || captureClient == nullptr
            || FAILED (audioClient->Start()))
        {
            setErrorText ("IAudioClient Initialize/Start failed");
            if (captureClient != nullptr) captureClient->Release();
            audioClient->Release();
            if (audioWakeEvent != nullptr) { CloseHandle (audioWakeEvent); audioWakeEvent = nullptr; }
            if (startReadyEvent != nullptr) SetEvent (startReadyEvent);
            if (shouldUninit) CoUninitialize();
            return;
        }

        captureSr.store (devSr, std::memory_order_relaxed);
        capturing.store (true, std::memory_order_release);
        startSucceeded.store (true, std::memory_order_release);
        if (startReadyEvent != nullptr)
            SetEvent (startReadyEvent);

        while (! threadShouldExitSafe() && ! stopSignal.load (std::memory_order_acquire))
        {
            WaitForSingleObject (audioWakeEvent, 15);
            if (stopSignal.load (std::memory_order_acquire))
                break;

            UINT32 packetFrames = 0;
            while (SUCCEEDED (captureClient->GetNextPacketSize (&packetFrames)) && packetFrames > 0)
            {
                BYTE* data = nullptr;
                UINT32 framesRead = 0;
                DWORD flags = 0;
                if (FAILED (captureClient->GetBuffer (&data, &framesRead, &flags, nullptr, nullptr)))
                    break;

                if (framesRead > 0)
                {
                    if ((int) tmpL.size() < (int) framesRead)
                    {
                        tmpL.resize (framesRead);
                        tmpR.resize (framesRead);
                    }
                    if ((flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0 || data == nullptr)
                    {
                        std::fill (tmpL.begin(), tmpL.begin() + framesRead, 0.0f);
                        std::fill (tmpR.begin(), tmpR.begin() + framesRead, 0.0f);
                    }
                    else if (isFloat32)
                    {
                        const float* src = reinterpret_cast<const float*> (data);
                        for (UINT32 f = 0; f < framesRead; ++f)
                        {
                            const float l = src[f * (UINT32) numCh];
                            const float r = numCh > 1 ? src[f * (UINT32) numCh + 1] : l;
                            tmpL[f] = std::isfinite (l) ? l : 0.0f;
                            tmpR[f] = std::isfinite (r) ? r : 0.0f;
                        }
                    }
                    else
                    {
                        const int16_t* src = reinterpret_cast<const int16_t*> (data);
                        constexpr float kScale = 1.0f / 32768.0f;
                        for (UINT32 f = 0; f < framesRead; ++f)
                        {
                            const float l = (float) src[f * (UINT32) numCh] * kScale;
                            const float r = numCh > 1 ? (float) src[f * (UINT32) numCh + 1] * kScale : l;
                            tmpL[f] = l;
                            tmpR[f] = r;
                        }
                    }
                    pushCapturedStereo (tmpL.data(), tmpR.data(), (int) framesRead, devSr);
                }
                captureClient->ReleaseBuffer (framesRead);
            }
        }

        capturing.store (false, std::memory_order_release);
        audioClient->Stop();
        captureClient->Release();
        audioClient->Release();
        if (audioWakeEvent != nullptr)
        {
            CloseHandle (audioWakeEvent);
            audioWakeEvent = nullptr;
        }
        if (shouldUninit)
            CoUninitialize();
    }

    bool threadShouldExitSafe() const noexcept
    {
        return workerThread != nullptr && workerThread->threadShouldExit();
    }
   #endif
};

} // namespace gz
