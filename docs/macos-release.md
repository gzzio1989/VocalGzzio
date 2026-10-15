# Mac配布物の作成と確認

現行の4.3.3は開発元の正式署名・公証を行っていません。正式配布の工程は、実際の証明書とMacでの検証が必要な準備段階です。自動検査が通っただけで、既存配布物が公証済みに変わることはありません。

## 現行の未公証版を作る

Macで製品版または体験版をビルドし、次を実行します。製品版を公開リポジトリの成果物に含めないでください。

```sh
python3 tools/package_macos.py --build build/package --out release/macos --edition trial --version 4.3.3 --public-artifact
```

署名オプションをすべて省略した場合だけ、アドホック署名・未公証の方式になります。ZIP直下の「ここから始める.html」と説明書内の `mac-install.html` は、その版・インストーラー名に合わせて生成します。4.3.2からは両方式ともPKGでの導入に統一し、直接開いてしまう原因となる手動配置用の部品はZIPに含めません。`.component` や `.vst3` は録音ソフトから読み込みます。

GitHubの使い捨てMac実行機では `--validate-install` を指定します。完成したPKGを実際に導入し、3形式の配置・版番号・実行権限を確認してから、導入されたAUをAppleの検証ホストで読み込みます。4.3.3では単体アプリのアイコン参照・ICNS本体の同梱も配布前と導入後に検査し、配布前にはAppleのiconutilで実画像を取り出せることも確認します。失敗した場合は配布ZIPを作りません。既存の同名製品は上書きせず、ローカルMacでの実行も拒否します。この検査はブラウザーからダウンロードした際の警告や、Logic Pro・実マイクの検査とは別です。

## 正式署名・公証版を作る準備

Apple Developer Programの加入、Developer ID Application証明書、Developer ID Installer証明書と対応する秘密鍵、Team ID、公証用認証情報を保存済みのキーチェーンが必要です。パスワードや秘密鍵をソース・配布ZIP・会話に貼り付けないでください。認証情報の保存はAppleの[公証手順](https://developer.apple.com/documentation/security/customizing-the-notarization-workflow)を参照し、作業するMacで行います。

```sh
python3 tools/package_macos.py \
  --build build/package --out release/macos-signed \
  --edition product --version 4.3.3 \
  --signing-identity 'Developer ID Application: 登録名 (TEAMID1234)' \
  --installer-identity 'Developer ID Installer: 登録名 (TEAMID1234)' \
  --team-id TEAMID1234 --notary-profile VocalGzzioNotary
```

登録名・Team ID・プロファイル名は例です。実際の値に置き換えます。4つの指定が一部でも不足した場合は中断し、未公証方式へ自動的に切り替えません。

本体3形式の署名・両CPUの確認、本体の公証、単体アプリへのチケット添付、インストーラー署名、公証、チケット検査、インストール許可の検査の順に進みます。すべて成功した場合だけ、`_公証済み.pkg` を含む配布ZIPを作成します。正式モードはPKGでの導入に統一し、未公証版用の手動インストール部品と案内は混ぜません。説明書のMac導入リンク先には、その配布物に合うページを生成します。

公証が不承認・未完了・検査失敗の場合は配布ZIPを作成しません。記録は出力先の `公証記録` に保存します。既存ZIPと検証値は上書きしないので、やり直すときは別の出力先を指定してください。

## 公開前に行う確認

- `配布情報.json` の版・形式・署名・公証結果と、ZIPのSHA-256を確認します。
- Apple SiliconとIntelの実機で、ブラウザーから取得した配布物の導入・起動・マイク入力を確認します。
- Logic ProのAU読み込み、対応する録音ソフトのVST3読み込み、保存した設定の復元とノイズ除去を確認します。
- 体験版と製品版を別々に確認します。既存購入者が同じBOOTH購入履歴から取得できる構成を維持します。

自動検査は `python3 -m unittest discover -s tools -p 'test_macos*.py' -v` で実行できます。配布工程の検査では外部コマンドを置き換えて失敗時の中断を確認します。これは本物のApple証明書、公証サービス、実機や録音ソフトの動作確認の代わりにはなりません。セキュリティ設定の一括無効化や隔離属性の一括削除は行いません。
