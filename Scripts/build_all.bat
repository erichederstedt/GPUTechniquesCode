@echo off
pushd %~dp0
pushd ..
setlocal

powershell -nologo -noprofile -executionpolicy bypass -file "Scripts/generate_vscode_launch_config.ps1"

call "Scripts\build_category.bat" "Code\Rendering"
@REM call "Scripts\build_category.bat" "Code\Compute"

endlocal
popd
popd