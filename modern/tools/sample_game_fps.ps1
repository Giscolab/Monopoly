#Requires -Version 5.1
<#
.SYNOPSIS
Samples actual game window-title FPS telemetry every 500 ms.
.EXAMPLE
./sample_game_fps.ps1 -ProcessId 1234 -ProcessPath ./modern/build/Debug/MonopolyModern.exe -OutputCsv ./modern/build/game-fps.csv -DurationSeconds 120
.NOTES
This records the application's title telemetry, not GPU frame fences or
individual frame times. Missing titles/FPS stay empty and do not enter statistics.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)] [ValidateRange(1, 2147483647)] [int] $ProcessId,
    [Parameter(Mandatory = $true)] [string] $ProcessPath,
    [Parameter(Mandatory = $true)] [string] $OutputCsv,
    [ValidateRange(1, 600)] [int] $DurationSeconds = 120,
    [ValidateRange(1, 1000)] [double] $TargetFps = 60
)
$ErrorActionPreference = 'Stop'
$expectedPath = (Resolve-Path -LiteralPath $ProcessPath).ProviderPath
if (-not (Test-Path -LiteralPath $expectedPath -PathType Leaf)) {
    throw 'ProcessPath must identify an existing executable file.'
}
$gameProcess = Get-Process -Id $ProcessId -ErrorAction Stop
if (-not [string]::Equals($gameProcess.Path, $expectedPath, [StringComparison]::OrdinalIgnoreCase)) {
    throw "PID $ProcessId does not run the requested ProcessPath."
}
$initialStartTime = $gameProcess.StartTime.ToUniversalTime()
$outputPath = [IO.Path]::GetFullPath($OutputCsv)
if ([string]::Equals($outputPath, $expectedPath, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'OutputCsv must differ from the executable path.'
}
$outputDirectory = [IO.Path]::GetDirectoryName($outputPath)
if (-not (Test-Path -LiteralPath $outputDirectory -PathType Container)) {
    throw 'OutputCsv parent directory must already exist.'
}
$rows = New-Object 'System.Collections.Generic.List[object]'
$samples = New-Object 'System.Collections.Generic.List[double]'
$timer = [Diagnostics.Stopwatch]::StartNew()
$writer = New-Object IO.StreamWriter($outputPath, $false, (New-Object Text.UTF8Encoding($false)))
try {
    $writer.WriteLine('"utc","elapsed_seconds","process_id","title_fps","window_title","metric_source"')
    while ($timer.Elapsed.TotalSeconds -lt $DurationSeconds) {
        $gameProcess = Get-Process -Id $ProcessId -ErrorAction SilentlyContinue
        if ($null -eq $gameProcess) { break }
        if ($gameProcess.StartTime.ToUniversalTime() -ne $initialStartTime -or
            -not [string]::Equals($gameProcess.Path, $expectedPath, [StringComparison]::OrdinalIgnoreCase)) {
            throw 'Process identity changed while sampling; refusing to sample a reused PID.'
        }
        $gameProcess.Refresh()
        $title = $gameProcess.MainWindowTitle
        $fpsText = ''
        if ($title -match '(?i)(\d+(?:\.\d+)?)\s*FPS\b') {
            $fps = [double]::Parse($Matches[1], [Globalization.CultureInfo]::InvariantCulture)
            if (-not [double]::IsInfinity($fps) -and -not [double]::IsNaN($fps)) {
                $fpsText = $fps.ToString('R', [Globalization.CultureInfo]::InvariantCulture)
                $samples.Add($fps)
            }
        }
        $row = [pscustomobject]@{
            utc = [DateTime]::UtcNow.ToString('o')
            elapsed_seconds = $timer.Elapsed.TotalSeconds.ToString('F3', [Globalization.CultureInfo]::InvariantCulture)
            process_id = $ProcessId
            title_fps = $fpsText
            window_title = $title
            metric_source = 'window-title telemetry'
        }
        $rows.Add($row)
        $csv = @($row | ConvertTo-Csv -NoTypeInformation)
        $writer.WriteLine($csv[1])
        $writer.Flush()
        $remainingMs = [int][Math]::Ceiling(($DurationSeconds - $timer.Elapsed.TotalSeconds) * 1000)
        if ($remainingMs -gt 0) { Start-Sleep -Milliseconds ([Math]::Min(500, $remainingMs)) }
    }
}
finally {
    $writer.Dispose()
    $timer.Stop()
}
$minimum = $median = $p5 = $fraction = $nearFraction = $null
if ($samples.Count -gt 0) {
    $sorted = @($samples | Sort-Object)
    $minimum = $sorted[0]
    $middle = [int][Math]::Floor($sorted.Count / 2)
    $median = if ($sorted.Count % 2) { $sorted[$middle] } else { ($sorted[$middle - 1] + $sorted[$middle]) / 2 }
    $p5 = $sorted[[Math]::Max(0, [int][Math]::Ceiling($sorted.Count * 0.05) - 1)]
    $fraction = @($samples | Where-Object { $_ -ge $TargetFps }).Count / [double]$samples.Count
    $nearFraction = @($samples | Where-Object { $_ -ge ($TargetFps - 0.5) }).Count / [double]$samples.Count
}
[pscustomobject]@{
    MetricSource = 'window-title telemetry; not frame-time or GPU-fence measurement'
    Csv = $outputPath
    ProcessId = $ProcessId
    Samples = $rows.Count
    SamplesWithFps = $samples.Count
    MinimumFps = $minimum
    MedianFps = $median
    P5FpsNearestRank = $p5
    TargetFps = $TargetFps
    FractionAtOrAboveTarget = $fraction
    FractionWithinHalfFpsOfTarget = $nearFraction
}
