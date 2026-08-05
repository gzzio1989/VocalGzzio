# X 投稿文 v2.8.1

※ 数字は重み付き文字数（上限280）。`python3 tools/xlen.py X投稿文_v2.8.1.md` で数え直せます。
※ 画像なしの報告回です。

## メイン

```
VocalGzzio v2.8.1

「長時間でジーが走る」との報告を受けた回です。最有力の原因(処理内の毎回のメモリ確保)はv2.8.0で除去済み。今回は保険の自己回復と、20分〜6時間ぶんの長時間テストを足しました。音の処理は劣化しないことを実測済みです。

https://gzzio1989.github.io/
#DTM #VST3
```

## 英語

```
VocalGzzio v2.8.1

Follow-up to a report of buzz after hours of use. The prime suspect - heap allocation inside the audio callback - was removed in v2.8.0. This adds self-healing insurance and long-run tests (20 min to 6 h equivalents), all clean.

https://gzzio1989.github.io/
#VST3
```

## 追加・実測報告（負荷と遅延）

```
VocalGzzio 実測しました(44.1kHz/124サンプル=締切2.81ms)。

出荷状態: 負荷1.1%・遅延0サンプル
うた自動: 4.4%・0サンプル
補正+こぶし: 5.7%・申告768=実測768
全部盛り: 18%・締切の半分以下

「遅延ゼロ」は宣伝文句ではなく測った数字です。

https://gzzio1989.github.io/
#DTM #VST3
```
