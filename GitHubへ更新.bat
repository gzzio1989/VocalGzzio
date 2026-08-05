@echo off
setlocal
cd /d "%~dp0"

echo ============================================
echo   VocalGzzio を GitHub へ更新します
echo ============================================
echo.

where git >nul 2>nul
if errorlevel 1 (
  echo [中止] git が見つかりません。Git for Windows を入れてください。
  pause
  exit /b 1
)

if not exist ".git" (
  echo このフォルダはまだ git の管理下ではないので、用意します。
  git init -b main
  git remote add origin https://github.com/gzzio1989/VocalGzzio.git
) else (
  git remote set-url origin https://github.com/gzzio1989/VocalGzzio.git
)

echo.
echo GitHub の今の中身を取ってきます...
git fetch origin main
if errorlevel 1 (
  echo [中止] fetch に失敗しました。ネットワークかログインを確認してください。
  pause
  exit /b 1
)

rem 履歴は書き換えない（force push はしない）。
rem GitHub の最新コミットを土台にして、その上に今回ぶんを積む。
git reset --mixed origin/main
git add -A

echo.
echo ========== これから GitHub へ上げるもの ==========
git status --short
echo ==================================================
echo.
echo 中身を確認してください。
echo   よければ何かキーを押す  → コミットして push します
echo   やめる場合             → このウィンドウを閉じてください
pause

git commit -m "v2.9.0 セッションモード（追加遅延を0サンプルに固定）とハモリの遅延申告バグ修正"
if errorlevel 1 (
  echo [中止] コミットするものがありませんでした。
  pause
  exit /b 1
)

git push origin main
if errorlevel 1 (
  echo.
  echo [失敗] push に失敗しました。上のメッセージを見てください。
  echo   ログインを求められた場合は、ブラウザで GitHub にサインインしてから再実行してください。
  pause
  exit /b 1
)

echo.
echo 完了しました。https://github.com/gzzio1989/VocalGzzio を開いて確認してください。
pause
