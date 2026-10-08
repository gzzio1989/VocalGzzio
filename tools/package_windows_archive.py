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

{'体験版は60秒ごとに0.6秒だけ音量が下がり、設定の保存・読込は使えません。' if trial else '製品版には体験版の音量低下・保存制限はありません。'}

最初は総合画面から、入力の種類と「仕上がり」を選びます。
「押して原音と比較」で聴き比べ、必要なつまみだけ調整します。
ノイズは黙った状態で「ノイズを測る」を押してから量を調整します。
チューナーは標準6弦ギター・4弦ベース・5弦ベースの自動判別と弦指定に対応します。
ブランド表示は英語です。報告や日本語表示モードは日本語で利用できます。

音程補正・ハモリは単独の歌声向けです。声とギターの混合入力を分離しません。
音程処理には48kHzで約42.6ミリ秒の追加遅延があります。
低遅延モードでは音程系を停止します。機器・録音ソフトの遅延は別です。
空間IRは計算生成した残響データです。

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
