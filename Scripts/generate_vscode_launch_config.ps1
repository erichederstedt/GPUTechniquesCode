$workspaceRoot = Join-Path $PSScriptRoot "/.."
$debuggerType = "cppvsdbg" # "cppvsdbg" for MSVC, "cppdbg" for GDB/LLDB
$gdbPath = "C:/MinGW/bin/gdb.exe" # Only needed if $debuggerType is "cppdbg"
$vsCodeDir = Join-Path $workspaceRoot ".vscode"
$launchJsonPath = Join-Path $vsCodeDir "launch.json"

# Ensure .vscode directory exists
if (-not (Test-Path $vsCodeDir)) {
    New-Item -ItemType Directory -Path $vsCodeDir | Out-Null
}

$configurations = @()

# Find all .exe files
$executableFiles = Get-ChildItem -Path $workspaceRoot -Filter *.exe -Recurse

if ($executableFiles.Count -eq 0) {
    Write-Warning "No .exe files found in '$workspaceRoot'. launch.json will be empty or may not be updated correctly."
}

foreach ($exeFile in $executableFiles) {
    $exeDirectoryPath = $exeFile.DirectoryName # Gets the full path to the directory of the .exe
    
    $projectName = Split-Path -Leaf $exeDirectoryPath # Extracts the last part of the path (the directory name)
    $programPath = ($exeFile.FullName.TrimStart('\','/').Replace('\','/')) # Relative path

    $cwdPath = ($exeDirectoryPath.TrimStart('\','/').Replace('\','/'))

    $config = @{
        name    = "Debug $projectName"
        type    = $debuggerType
        request = "launch"
        program = $programPath
        args    = @()
        stopAtEntry = $false
        cwd     = $cwdPath
        environment = @()
        console = "integratedTerminal"
    }

    if ($debuggerType -eq "cppdbg") {
        $config.MIMode = "gdb"
        $config.miDebuggerPath = $gdbPath # Make sure this is correct for your system
        $config.setupCommands = @(
            @{
                description = "Enable pretty-printing for gdb"
                text        = "-enable-pretty-printing"
                ignoreFailures = $true
            }
        )
    }

    $configurations += $config
}

$launchJsonContent = @{
    version        = "0.2.0"
    configurations = $configurations
}

# Convert to JSON and write to file
$launchJsonContent | ConvertTo-Json -Depth 5 | Set-Content -Path $launchJsonPath -Encoding UTF8

Write-Host "launch.json generated successfully at '$launchJsonPath' with $($configurations.Count) configurations."