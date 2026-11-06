param([Parameter(Mandatory = $true)][string]$Path)

$ErrorActionPreference = 'Stop'
$culture = [Globalization.CultureInfo]::InvariantCulture
$lines = @(Get-Content -LiteralPath $Path -Encoding UTF8)
if ($lines.Count -lt 3 -or !$lines[0].StartsWith('# ')) {
    throw 'Expected trial information on line 1, column names on line 2, and at least one sample.'
}
$rows = @($lines[1..($lines.Count - 1)] | ConvertFrom-Csv)
$columns = @('interval_s', 'displayed_frames', 'rtp_mbps', 'pc_latency_samples', 'pc_latency_min_ms',
    'pc_latency_avg_ms', 'pc_latency_max_ms', 'ping_samples', 'ping_rtt_min_ms', 'ping_rtt_avg_ms',
    'ping_rtt_max_ms', 'control_disconnects', 'reconnect_attempts', 'ack_timeouts')
foreach ($column in $columns) {
    if ($rows[0].PSObject.Properties.Name -notcontains $column) { throw "Missing column: $column" }
}
function Number([string]$Value) {
    if ([string]::IsNullOrWhiteSpace($Value)) { throw 'Missing numeric value.' }
    $number = [double]::Parse($Value, $culture)
    if ([double]::IsNaN($number) -or [double]::IsInfinity($number) -or $number -lt 0) {
        throw "Invalid nonnegative value: $Value"
    }
    return $number
}
function Format-Number([double]$Value) { $Value.ToString('F3', $culture) }
function Weighted-Statistics([string]$CountColumn, [string]$Prefix) {
    $count = 0.0
    $total = 0.0
    $minimum = [double]::PositiveInfinity
    $maximum = 0.0
    foreach ($row in $rows) {
        $samples = Number $row.$CountColumn
        if ($samples -eq 0) { continue }
        $low = Number $row.($Prefix + '_min_ms')
        $average = Number $row.($Prefix + '_avg_ms')
        $high = Number $row.($Prefix + '_max_ms')
        if ($low -gt $average -or $average -gt $high) { throw 'Invalid min/avg/max order.' }
        $count += $samples
        $total += $samples * $average
        $minimum = [math]::Min($minimum, $low)
        $maximum = [math]::Max($maximum, $high)
    }
    if ($count -eq 0) { return 'unavailable (0 samples)' }
    return ('samples={0}; min={1} ms; avg={2} ms; max={3} ms' -f $count,
        (Format-Number $minimum), (Format-Number ($total / $count)), (Format-Number $maximum))
}

$seconds = 0.0
$frames = 0.0
$megabits = 0.0
$disconnects = 0.0
$reconnects = 0.0
$timeouts = 0.0
foreach ($row in $rows) {
    $duration = Number $row.interval_s
    if ($duration -le 0) { throw 'interval_s must be positive.' }
    $seconds += $duration
    $frames += Number $row.displayed_frames
    $megabits += (Number $row.rtp_mbps) * $duration
    $disconnects += Number $row.control_disconnects
    $reconnects += Number $row.reconnect_attempts
    $timeouts += Number $row.ack_timeouts
}
Write-Output $lines[0]
Write-Output ('Observed: {0} s; displayed frames: {1}' -f (Format-Number $seconds), $frames)
Write-Output ('Display FPS: {0}; received RTP Mbps: {1}' -f (Format-Number ($frames / $seconds)),
    (Format-Number ($megabits / $seconds)))
Write-Output ('PC receive-to-paint: ' + (Weighted-Statistics 'pc_latency_samples' 'pc_latency'))
Write-Output ('TCP PING application RTT: ' + (Weighted-Statistics 'ping_samples' 'ping_rtt'))
Write-Output ('Control disconnects: {0}; reconnect attempts: {1}; ACK timeouts: {2}' -f $disconnects, $reconnects, $timeouts)
Write-Output 'PC-local observations; these are not video network one-way or camera-to-screen latency.'
