@echo off
pushd %~dp0
pushd ..
setlocal

call "Scripts\build_category.bat" "Code\Rendering"
@REM call "Scripts\build_category.bat" "Code\Compute"

endlocal
popd
popd