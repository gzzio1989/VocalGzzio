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
    readme_path = ROOT / '配布物' / f'はじめにお読みください_{label}.txt'
    note = readme_path.read_text(encoding='utf-8-sig')
    files = {installer.name: installer, checksum.name: checksum,
             '説明書/取扱説明書.html': ROOT / 'docs/manual.html',
             '説明書/style.css': ROOT / 'docs/style.css'}
    en_guide = ROOT / '配布物/説明書/English_Quick_Start_Guide.html'
    if en_guide.is_file():
        files['説明書/English_Quick_Start_Guide.html'] = en_guide
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
