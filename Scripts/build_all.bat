@echo off
pushd %~dp0
pushd ..
setlocal

call "Scripts\build_category.bat" "Code\Rendering"
@REM call "Scripts\build_category.bat" "Code\Compute"

powershell -nologo -noprofile -executionpolicy bypass -file "Scripts/generate_vscode_launch_config.ps1"

endlocal
popd
popd