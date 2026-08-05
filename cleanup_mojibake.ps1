# VocalGzzio フォルダのお掃除スクリプト
# 消すのではなく _to_delete フォルダへ「よけるだけ」です。中身を見てから捨ててください。
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$dest = Join-Path $root "_to_delete"
if (-not (Test-Path $dest)) { New-Item -ItemType Directory -Path $dest | Out-Null }

$files = @(
  "BOOTH雋ｩ螢ｲ縺ｨ萓｡譬ｼ_X謚慕ｨｿ繝励Λ繝ｳ.md",
  "v1.8.0_繝｢繝ｼ繝牙姐譁ｰ_螟画峩轤ｹ.md",
  "v1.8.1_縺疲欠鞫伜ｯｾ蠢彑螟画峩轤ｹ.md",
  "v1.8.3_UI繝舌Λ繝ｳ繧ｹ隱ｿ謨ｴ_螟画峩轤ｹ.md",
  "v1.8.4_髻ｳ雉ｪ縺ｨ螟剰レ譎ｯ_螟画峩轤ｹ.md",
  "v1.8.5_螟上ヱ繝阪Ν騾城℃_螟画峩轤ｹ.md",
  "v1.9.0_繧ｪ繝ｼ繝医メ繝･繝ｼ繝ｳ_螟画峩轤ｹ.md",
  "v1.9.2_Windows繝薙Ν繝我ｿｮ豁｣_螟画峩轤ｹ.md",
  "v1.9.3_繧ｱ繝ｭ繧ｱ繝ｭ縺ｨ螳牙ｮ壽ｧ_螟画峩轤ｹ.md",
  "v1.9.4_菴馴ｨ鍋沿_螟画峩轤ｹ.md",
  "v1.9.5_濶ｶ_螟画峩轤ｹ.md",
  "v1.9.7_菴朱≦蟒ｶ縺ｨLite_螟画峩轤ｹ.md",
  "v1.9.8_繝上Δ繝ｪ縺ｨ隕冶ｪ肴ｧ_螟画峩轤ｹ.md",
  "v1.9.9_繝上Δ縺縺狙螟画峩轤ｹ.md",
  "v2.0.0_繝上Δ繝ｪ螟ｧ謾ｹ菫ｮ_螟画峩轤ｹ.md",
  "v2.2.0_驟堺ｿ｡蜃ｺ蜉媽螟画峩轤ｹ.md",
  "v2.4.0_縺九ｓ縺溘ｓ繝｢繝ｼ繝牙姐譁ｰ_螟画峩轤ｹ.md",
  "v2.5.0_縺ｪ繧√ｉ縺九→髻ｳ驥柔螟画峩轤ｹ.md",
  "v2.6.0_繧ｸ繝ｼ髻ｳ縺ｨ縺薙→縺ｰ_螟画峩轤ｹ.md",
  "X謚慕ｨｿ_蠑ｷ蛹也沿.md",
  "X謚慕ｨｿ譁㍉v1.9.1.md",
  "X謚慕ｨｿ譁㍉v1.9.2.md",
  "X謚慕ｨｿ譁㍉v1.9.3.md",
  "X謚慕ｨｿ譁㍉v1.9.7.md",
  "X謚慕ｨｿ譁㍉v1.9.8.md",
  "X謚慕ｨｿ譁㍉v1.9.9.md",
  "X謚慕ｨｿ譁㍉v2.0.0.md",
  "X謚慕ｨｿ譁㍉v2.0.1.md",
  "X謚慕ｨｿ譁㍉v2.1.0.md",
  "X謚慕ｨｿ譁㍉v2.2.0.md",
  "X謚慕ｨｿ譁㍉v2.3.0.md",
  "X謚慕ｨｿ譁㍉v2.4.0.md",
  "X謚慕ｨｿ譁㍉v2.5.0.md",
  "X謚慕ｨｿ譁㍉v2.6.0.md",
  "X邏譚神v2.5.0_10遘偵♀縺ｾ縺九○.png",
  "X邏譚神v2.6.0_縺薙→縺ｰ.png",
  "X邏譚神v2.6.0_繧ｸ繝ｼ髻ｳ.png",
  # --- ここから下は v2.6.1 に置き換わった古い版 ---
  "GitHubリリースコメント_v2.6.0.md",
  "v2.6.0_ジー音とことば_変更点.md",
  "X投稿文_v2.6.0.md",
  "X素材_v2.6.0_ことば.png",
  "X素材_v2.6.0_ジー音.png"
)

$moved = 0; $missing = 0
foreach ($f in $files) {
  $p = Join-Path $root $f
  if (Test-Path -LiteralPath $p) {
    Move-Item -LiteralPath $p -Destination $dest -Force
    Write-Host ("よけた : " + $f)
    $moved++
  } else { $missing++ }
}
Write-Host ""
Write-Host ("よけたファイル: " + $moved + " 件 / もともと無かった: " + $missing + " 件")
Write-Host ("行き先: " + $dest)
Write-Host ""
Write-Host "中身を確認して問題なければ _to_delete フォルダごと削除してください。"
Read-Host "Enter キーで閉じます"
