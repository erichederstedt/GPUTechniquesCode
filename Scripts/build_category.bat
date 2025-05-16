@echo off
setlocal

if "%~1"=="" (
    echo Usage: %~nx0 ^<directory_path^>
    exit /b 1
)

set "BASE_DIR=%~1"

if not exist "%BASE_DIR%" (
    echo Directory "%BASE_DIR%" does not exist.
    exit /b 1
)

for /D %%D in ("%BASE_DIR%\*") do (
    echo Calling build_project.bat on "%%D"
    call "Scripts/build_project.bat" "%%D"
)

endlocal