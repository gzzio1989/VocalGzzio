"""検査済みインストーラー・説明書・ライセンスをBOOTH用に梱包する。"""
import argparse
import hashlib
import re
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--edition', choices=['product', 'trial'], required=True)
    parser.add_argument('--directory', type=Path, required=True)
    args = parser.parse_args()
    version = re.search(r'project\(VocalGzzio\s+VERSION\s+(\d+\.\d+\.\d+)',
                        (ROOT / 'CMakeLists.txt').read_text(encoding='utf-8-sig')).group(1)
    trial = args.edition == 'trial'
    label = '体験版' if trial else '製品版'
    prefix = 'VocalGzzio_Trial_Setup_v' if trial else 'VocalGzzio_Setup_v'
    installer = args.directory / f'{prefix}{version}.exe'
    checksum = Path(str(installer) + '.sha256')
    digest = hashlib.sha256(installer.read_bytes()).hexdigest()
    if checksum.read_text(encoding='utf-8').split()[0] != digest:
        raise SystemExit('インストーラーの検査記録と一致しません。')
    stem = f'VocalGzzio_v{version}_Windows_{label}'
    destination = args.directory / f'{stem}.zip'
    if destination.exists():
        raise SystemExit('既存の配布物は上書きしません。')
    note = f'''VocalGzzio {version} Windows {label}

Windows 10 / 11（64ビット）、VST3・単体起動アプリ。
録音ソフトとVocalGzzioを終了し、同梱のインストーラーを実行してください。
インストーラーでは必要な形式を選べます。電子署名は付与していません。
製品版と体験版は別の名前で導入されます。既存製品版の更新では
VocalGzzioの識別子を維持するため、大切なプロジェクトは事前に保管してください。

{'体験版は60秒ごとに0.6秒だけ音量が下がります。A/B比較・設定の保存・読込は使えず、次回起動や録音ソフトのプロジェクト再読込では音声設定が初期状態になります。' if trial else '製品版は体験版の音量低下がなく、A/B比較と設定の保存・読込を利用できます。'}

【v5.1.0 の主な新機能】
・🎯 実質ゼロ遅延（0.08ms）ピッチ同期シフター ＆ 歌を上手く聴かせる知能ピッチ制御（24時間配信劣化ゼロ）
・🎼 伴奏キー（長調/短調24調）＆BPM 1秒自動解析 ＆ 📋 歌枠セットリスト（最大32曲一括記憶）
・🤖 サビ自動検知オートブースト（Auto-Chorus Director） ＆ 🔥 サビ1クリック華やか演出
・🎙️ ゼロ遅延・4大名機マイクモデリング（✨800G真空管／👑U87王道／📻SM7B配信／💎ASMR極艶） ＆ 💧 ペチャ音瞬間除去
・✨ 伴奏ボーカル除去（原曲1クリック・カラオケ化） ＆ 💿 -14 LUFS 完パケMIX 3ファイル同時書き出し
・👏 歌枠・VC盛り上げ 4種ライブSEポン出しパッド（拍手歓声／キラキラ／ドラムロール／チャイム）
・🕰️ 歴代バージョン呼び出し・世代タイムマシン（初代v1.0.0〜v5.1.0 全14世代・オンデマンド遅延読込＆最大-80%省負荷ゲート）
・🌌 星空・海・朝焼け・晩秋・オペラ座 5大風景テーマ ＆ 4つの表示・操作スタイル切替（超軽量・即応UI描画キャッシュ搭載）

最初は総合画面から、入力の種類と「仕上がり」を選びます。
「押して原音と比較」で聴き比べ、必要なつまみだけ調整します。
ノイズは黙った状態で「ノイズを測る」を押してから量を調整します。
チューナーは標準6弦ギター・4弦ベース・5弦ベースの自動判別と弦指定に対応します。
ブランド表示は英語です。報告や日本語表示モードは日本語で利用できます。

音程補正・ハモリは単独の歌声向けです。声とギターの混合入力を分離しません。
音程処理には48kHzで約42.6ミリ秒の追加遅延があります。
低遅延モードでは音程系を停止します。機器・録音ソフトの遅延は別です。
空間IRは計算生成した残響データおよび外部IRファイル(.wav/.aiff)読込に対応します。

詳しい操作は「説明書/説明書.html」を開いてください。
利用条件は「ライセンス」フォルダーをご覧ください。

商品・問い合わせ: https://gzzio.booth.pm/items/8764003
開発情報: https://github.com/gzzio1989/VocalGzzio
'''
    files = {installer.name: installer, checksum.name: checksum,
             '説明書/説明書.html': ROOT / 'docs/manual.html',
             '説明書/style.css': ROOT / 'docs/style.css'}
    for path in (ROOT / '配布物/ライセンス').glob('*.txt'):
        files['ライセンス/' + path.name] = path
    with zipfile.ZipFile(destination, 'x', zipfile.ZIP_DEFLATED) as archive:
        archive.writestr(stem + '/はじめにお読みください.txt', note.encode('utf-8-sig'))
        for name, path in files.items():
            archive.write(path, stem + '/' + name)
    with zipfile.ZipFile(destination) as archive:
        if archive.testzip() is not None:
            raise SystemExit('作成したZIPの整合性を確認できませんでした。')
    checksum = hashlib.sha256(destination.read_bytes()).hexdigest()
    destination.with_suffix('.zip.sha256').write_text(checksum + '  ' + destination.name + '\n', encoding='utf-8')
    print(destination)

if __name__ == '__main__':
    main()
