param(
    [Parameter(Mandatory = $true)][string]$Path,
    [string]$OutputCsv
)

$ErrorActionPreference = 'Stop'
$rows = @(Import-Csv -LiteralPath $Path)
if ($rows.Count -eq 0) { throw 'No finalized frame rows. Stop the receiver normally and retry.' }
foreach ($column in @('role', 'frameId', 'ssrc', 'rtpTimestamp', 'firstRtpNs',
                      'markerRtpNs', 'appsinkNs', 'paintNs', 'displayed', 'clockDomain', 'timeUnit')) {
    if ($rows[0].PSObject.Properties.Name -notcontains $column) { throw "Missing column: $column" }
}
if (@($rows | Where-Object { $_.clockDomain -ne 'local_steady_clock' -or $_.timeUnit -ne 'ns' }).Count) {
    throw 'Expected local monotonic clock values in nanoseconds.'
}

function Get-IntervalMs([string]$Start, [string]$End) {
    if ($Start -eq '' -or $End -eq '') { return $null }
    # Subtract integer timestamps before converting to floating point.
    $delta = [long]$End - [long]$Start
    if ($delta -lt 0) { return $null }
    return $delta / 1000000.0
}

$receiverRows = @($rows | Where-Object { $_.role -eq 'receiver' })
if ($receiverRows.Count -eq 0) { throw 'Use a receiver frame summary, not a sender or packet CSV.' }
$measured = @($receiverRows | ForEach-Object {
    [pscustomobject]@{
        FrameId = $_.frameId
        SSRC = $_.ssrc
        RtpTimestamp = $_.rtpTimestamp
        Displayed = $_.displayed
        FirstRtpToPaintMs = Get-IntervalMs $_.firstRtpNs $_.paintNs
        MarkerToAppsinkMs = Get-IntervalMs $_.markerRtpNs $_.appsinkNs
        AppsinkToPaintMs = Get-IntervalMs $_.appsinkNs $_.paintNs
    }
})
Write-Host ('Frame rows: {0}; drawn: {1}; not drawn: {2} (not a network-loss count)' -f
    $receiverRows.Count, @($receiverRows | Where-Object { $_.displayed -eq '1' }).Count,
    @($receiverRows | Where-Object { $_.displayed -eq '0' }).Count)
$statistics = foreach ($metric in @('FirstRtpToPaintMs', 'MarkerToAppsinkMs', 'AppsinkToPaintMs')) {
    $values = @($measured | Where-Object { $null -ne $_.$metric })
    if ($values.Count -eq 0) { continue }
    $stats = $values | Measure-Object -Property $metric -Minimum -Maximum -Average
    [pscustomobject]@{
        Metric = $metric
        Samples = $values.Count
        MinMs = [math]::Round($stats.Minimum, 3)
        AverageMs = [math]::Round($stats.Average, 3)
        MaxMs = [math]::Round($stats.Maximum, 3)
    }
}
$statistics | Format-Table -AutoSize
if ($OutputCsv) {
    $measured | Export-Csv -LiteralPath $OutputCsv -NoTypeInformation -Encoding UTF8
}
