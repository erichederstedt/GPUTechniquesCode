@echo off

rem Notes:
rem Only VS2019 and VS2022 has been tested and works.
rem You cannot have more that one install of a VS version.
rem Such as two installs of VS2022.

pushd %~dp0

for /f "delims=" %%i in ('powershell -nologo -noprofile -executionpolicy bypass -file "VCVarsFinder.ps1"') do (
    set "filepath=%%i"
)

echo Executing: "%filepath%"

call "%filepath%"

popd