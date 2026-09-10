$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$msbuild = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe'
$buildRoot = Join-Path $repoRoot 'build\stability'

& $msbuild (Join-Path $repoRoot 'Rish.sln') /p:Configuration=Release /p:Platform=x86 "/p:OutDir=$buildRoot\app\" "/p:IntDir=$buildRoot\obj\" /m /v:minimal /nologo
if ($LASTEXITCODE -ne 0) { throw 'Reader build failed.' }
& $msbuild (Join-Path $PSScriptRoot 'ReadingRegression.vcxproj') /p:Configuration=Release /p:Platform=Win32 /m /v:minimal /nologo
if ($LASTEXITCODE -ne 0) { throw 'Regression build failed.' }
Push-Location (Join-Path $buildRoot 'tests')
try {
    $testProcess = Start-Process -FilePath (Join-Path $buildRoot 'tests\ReadingRegression.exe') -PassThru -WindowStyle Hidden -RedirectStandardOutput 'results.log' -RedirectStandardError 'errors.log'
    $null = $testProcess.Handle
    if (-not $testProcess.WaitForExit(30000)) {
        $testProcess.Kill()
        throw 'Regression timed out after 30 seconds.'
    }
    Get-Content 'results.log' -Tail 15
    Get-Content 'errors.log'
    if ($testProcess.ExitCode -ne 0) { throw "Regression failed: $($testProcess.ExitCode)" }
} finally { Pop-Location }
