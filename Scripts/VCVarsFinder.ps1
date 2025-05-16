# https://github.com/erichederstedt/VCVarsFinder

# Path to vswhere
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"

# Run vswhere and get all installation paths
$installPaths = & $vswhere -all -property installationPath

if (-not $installPaths) {
    Write-Error "No Visual Studio installations found."
    exit 1
}

# Show all installs found
Write-Host "Found Visual Studio installations:"
$installPaths | ForEach-Object { Write-Host "`t$_" }

# Pick the latest one
$selectedPath = $installPaths | Sort-Object -Descending | Select-Object -First 1

# Build the path to vcvars64.bat
$vcvarsPath = Join-Path -Path $selectedPath -ChildPath "VC\Auxiliary\Build\vcvars64.bat"

# Return this path (for use in other scripts)
return $vcvarsPath