@echo off

pushd %~dp0

rem Checks if cl is defined. If not call vcvars64.bat.
where /q cl
if errorlevel 1 (
    call "run_vcvar.bat"
)

pushd ..
setlocal enabledelayedexpansion

call "Scripts/copy_file_from_path.bat" clang_rt.asan_dynamic-x86_64.dll %1

rem /O2
@set CC=cl
@set FLAGS=/std:c++17 /D_CRT_SECURE_NO_WARNINGS /fsanitize=address /DSDL_MAIN_HANDLED /Z7 /W4 /WX /MP /EHsc /I "./Extra" /I "./Extra/YetAnotherRenderingAPI"
@set "SRC_FILES="
rem set "SRC_FILES=!SRC_FILES! "Src/main.c""
for /r %1 %%I in (*.c) do (
    set "SRC_FILES=!SRC_FILES! "%%~fI""
)

set "SRC_FILES=!SRC_FILES! "Extra\util.c""
set "SRC_FILES=!SRC_FILES! "Extra\YetAnotherRenderingAPI\yara_d3d12.c""

rem vcperf /start SessionName

%CC% %FLAGS% %SRC_FILES% /MP /link /out:%1\main.exe /LIBPATH:"./Lib" Shell32.lib User32.lib

rem vcperf /stop SessionName /timetrace outputFile.json

del /S *.obj 1>nul 2>nul
del /S vc140.pdb 1>nul 2>nul

endlocal

popd
popd