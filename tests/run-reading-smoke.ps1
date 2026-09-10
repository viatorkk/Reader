$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$app = Join-Path $repoRoot 'build\stability\app\Rish.exe'
$fixture = Join-Path $PSScriptRoot 'smoke.txt'
if (Get-Process -Name Rish -ErrorAction SilentlyContinue) {
    throw 'An existing Rish process is running. Close it before running this isolated smoke test.'
}
if (-not (Test-Path -LiteralPath $app)) { throw 'Run run-reading-regression.ps1 first.' }

Add-Type -TypeDefinition @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class RishSmokeWindow {
    private delegate bool EnumProc(IntPtr window, IntPtr param);
    [DllImport("user32.dll")] private static extern bool EnumWindows(EnumProc callback, IntPtr param);
    [DllImport("user32.dll")] private static extern uint GetWindowThreadProcessId(IntPtr window, out uint process);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] private static extern int GetClassName(IntPtr window, StringBuilder text, int capacity);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] private static extern int GetWindowText(IntPtr window, StringBuilder text, int capacity);
    [DllImport("user32.dll")] private static extern IntPtr SendMessageTimeout(IntPtr window, uint message, IntPtr w, IntPtr l, uint flags, uint timeout, out IntPtr result);
    [DllImport("user32.dll")] private static extern bool PostMessage(IntPtr window, uint message, IntPtr w, IntPtr l);
    public static IntPtr Find(uint process) {
        IntPtr found = IntPtr.Zero;
        EnumWindows(delegate(IntPtr window, IntPtr param) {
            uint owner;
            GetWindowThreadProcessId(window, out owner);
            if (owner != process) return true;
            var name = new StringBuilder(256);
            GetClassName(window, name, name.Capacity);
            if (name.ToString() != "RISH") return true;
            found = window;
            return false;
        }, IntPtr.Zero);
        return found;
    }
    public static string Title(IntPtr window) {
        var text = new StringBuilder(1024);
        GetWindowText(window, text, text.Capacity);
        return text.ToString();
    }
    public static bool Responsive(IntPtr window) {
        IntPtr result;
        return SendMessageTimeout(window, 0, IntPtr.Zero, IntPtr.Zero, 2, 1000, out result) != IntPtr.Zero;
    }
    public static bool Close(IntPtr window) { return PostMessage(window, 0x10, IntPtr.Zero, IntPtr.Zero); }
}
'@

for ($attempt = 0; $attempt -lt 2; $attempt++) {
    $startOptions = @{
        FilePath = $app
        WorkingDirectory = (Split-Path -Parent $app)
        WindowStyle = 'Hidden'
        PassThru = $true
    }
    if ($attempt -eq 0) { $startOptions.ArgumentList = '"' + $fixture + '"' }
    $smokeProcess = Start-Process @startOptions
    $null = $smokeProcess.Handle
    try {
        $null = $smokeProcess.WaitForInputIdle(10000)
        $deadline = [DateTime]::UtcNow.AddSeconds(10)
        do {
            $smokeProcess.Refresh()
            if ($smokeProcess.HasExited) { throw 'Reader exited before loading the test book.' }
            $readerWindow = [RishSmokeWindow]::Find($smokeProcess.Id)
            $readerTitle = [RishSmokeWindow]::Title($readerWindow)
            if ($readerTitle -like '*smoke*') { break }
            Start-Sleep -Milliseconds 100
        } while ([DateTime]::UtcNow -lt $deadline)
        if ($readerTitle -notlike '*smoke*') { throw "Test book was not reflected in window title: $readerTitle" }
        if (-not [RishSmokeWindow]::Responsive($readerWindow)) { throw 'Reader window is not responding.' }
        if (-not [RishSmokeWindow]::Close($readerWindow)) { throw 'Could not request normal window close.' }
        if (-not $smokeProcess.WaitForExit(5000)) { throw 'Reader did not close within five seconds.' }
        if ($smokeProcess.ExitCode -ne 0) { throw "Reader exited with code $($smokeProcess.ExitCode)." }
        if ($attempt -eq 0) { Write-Output 'PASS: application opens TXT, responds, and closes normally.' }
        else { Write-Output 'PASS: application restores the test book on restart and closes normally.' }
    } finally {
        if (-not $smokeProcess.HasExited) {
            # Only terminate the test process created above, never another instance.
            $smokeProcess.Kill()
            $smokeProcess.WaitForExit()
        }
        $smokeProcess.Dispose()
    }
}
