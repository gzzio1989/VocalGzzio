# X告知文 — VocalGzzio v2.2.0

## 日本語

### 1. 配信出力（新機能の目玉）
```
単体起動版の声が、そのまま配信に乗るようになりました。

ヘッドホンで自分の声を聴きながら、同じ音を別の出力先へ同時に送れます。
VB-CABLEを配信先に選べば、OBSがそれを拾えます。

「加工した声を配信に出せない」が終わります。
```

### 2. なぜドライバを作らなかったか（技術者向け）
```
シンクルームみたいな専用ドライバも考えました。

でもWindowsのカーネルドライバはEV証明書が要るし、
署名が切れれば動かないし、バグればブルースクリーン。
個人開発で一生面倒を見るには重すぎました。

なのでプラグイン側から二重に出す方式にしました。
```

### 3. 時計のズレの話
```
出力デバイスを2つ同時に鳴らすと、地味な問題が起きます。

どちらも「48kHz」と名乗っていても、水晶が別なので少しずつズレる。
放っておくと数分ごとにプチッと鳴ります。

たまり具合を見て再生速度を±0.4%だけ常に調整して吸収しています。
```

### 4. MIDIスイッチ
```
足元で切り替えられるようにしました。

フットスイッチに「ハモだけ」「ひびきON/OFF」「A/B切替」などを
割り当てられます。設定は"学習"を押してスイッチを踏むだけ。

歌いながら、手を使わずに音が変わります。
```

### 5. ハモだけ修正（正直に）
```
「ハモだけ」が直りました。

原音を消したつもりが、本人とほぼ同じ音程で歌うユニゾン隊2声が
残っていました。ハモリの2声だけを出すようにして、Mixからの
混ぜ戻しも止めて、子音で一瞬本人の声に戻る癖も直しました。

今度こそ、自分の声を聴きながらハモリだけ録れます。
```

---

## English（和訳つき）

### 1. Stream out
```
The standalone app can now feed your broadcast directly.

It sends the finished voice to a second output device while you
keep monitoring on headphones - point it at a virtual cable and
OBS picks it up.
```
【和訳】単体起動版の音を、そのまま配信に送れるようになりました。ヘッドホンで自分の声を聴きながら、同じ音を別の出力先へ同時に送れます。仮想オーディオデバイスを配信先にすればOBSがそれを拾えます。

### 2. Why not a driver
```
I looked at writing a virtual audio driver, like the ones session
apps ship with.

It's a Windows kernel driver: EV certificate, attestation signing,
and a blue screen every time you get it wrong. Too much for a solo
developer - so the plugin does the dual output itself.
```
【和訳】専用の仮想オーディオドライバも検討しました。ただWindowsのカーネルドライバはEV証明書とアテステーション署名が要り、間違えればブルースクリーン。個人開発で持ち続けるには重すぎるので、プラグイン側から二重出力する方式にしました。

### 3. Clock drift
```
Two output devices, two crystals.

Even when both say "48 kHz" they drift - left alone, you get a
click every few minutes. The playback rate is nudged within
0.4% based on how full the buffer is, and the drift disappears.
```
【和訳】出力デバイスが2つあれば水晶も2つ。どちらも48kHzと名乗っていてもズレるので、放っておくと数分ごとにプチッと鳴ります。たまり具合を見て再生速度を0.4%以内で調整し、ズレを消しています。

### 4. MIDI switching
```
New: MIDI switching.

Map a foot switch to harmony-only, reverb on/off, A/B compare and
more - twelve actions, eight slots. Press Learn, hit the switch,
done. Change your sound mid-song without using your hands.
```
【和訳】新機能「MIDIスイッチ」。フットスイッチに「ハモだけ」「ひびきON/OFF」「A/B切替」など12種類・8枠まで割り当てられます。設定は"学習"を押してスイッチを踏むだけ。
