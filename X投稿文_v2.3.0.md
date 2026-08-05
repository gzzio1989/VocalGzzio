# X告知文 — VocalGzzio v2.3.0

## 日本語

### 1. Cubaseバグ修正（報告のお礼を兼ねて）
```
Cubaseが終了できない不具合を直しました。

原因は「更新確認」でした。画面を開くたびGitHubへ通信していて、
その処理はプラグインのDLL内で動きます。通信中にDAWを終了すると、
Windowsはそのスレッドが終わるまで待ちます。

プラグイン版では通信をやめました。ご報告ありがとうございました。
```

### 2. ポップノイズ除去
```
「ぱ・ば行」の"ボフッ"を狙って消せるようにしました。

ローカットを上げれば消えますが、歌の低音も痩せます。
なので「突発的」かつ「倍音に対して低域が突出」の両方を満たした
一瞬だけ、急峻なハイパスへ寄せています。

伸ばした歌声は一切触りません（実測0.0dB）。
```

### 3. リップノイズ除去
```
フレーズの合間の"ペチャ"も抑えられます。

3〜4kHzに一瞬だけ立つ鋭い音を見つけて、その瞬間だけ高域を落とす。
歌っている最中は絶対に動かないので、サ行が鈍ることはありません。

宅録っぽさが少し減ります。
```

### 4. 正直な制限
```
ポップ除去には弱点があります。

VocalGzzioはゼロ遅延なので"先読み"ができません。
なので低い男声の歌い出しの一瞬（約48ms）だけ、わずかに反応します。

先読みするプラグインにこの制限はありませんが、代わりに遅延が出ます。
モニターしながら歌う道具なので、遅延ゼロを選びました。
```

---

## English（和訳つき）

### 1. The Cubase fix
```
Fixed Cubase refusing to quit with VocalGzzio on an insert.

The culprit was the update check. Opening the window started a
thread that called GitHub from inside the plugin DLL - so Windows
waited for it before unloading the module.

The plugin build no longer makes that call.
```
【和訳】インサートに挿したままCubaseを終了できない不具合を修正しました。原因は更新確認で、画面を開くたびにGitHubへ通信するスレッドが動いており、そのコードはプラグインのDLL内にあるため、Windowsがモジュール解放前にそれを待っていました。プラグイン版では通信をやめました。

### 2. De-plosive and de-click
```
New: plosive and mouth-click removal.

The de-plosive only fires when the low end is both sudden and
standing out against the harmonics a sung note always has - so it
never mistakes a low note for a pop. Sustained singing measures
0.0 dB: completely untouched.
```
【和訳】ポップ（破裂音）とリップ（口の粘着音）の除去を追加。破裂音除去は「突発的」かつ「歌なら必ず出る倍音に対して低域が突出」の両方を満たしたときだけ動くので、低い歌声を誤検出しません。伸ばした歌声は実測0.0dB＝一切触りません。
