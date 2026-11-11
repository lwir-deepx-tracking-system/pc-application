param(
    [Parameter(Mandatory = $true)][string]$Path,
    [double]$FromElapsed = 0,
    [double]$ToElapsed = [double]::PositiveInfinity,
    [string]$ModelId = '',
    [switch]$AsJson
)

$ErrorActionPreference = 'Stop'
$culture = [Globalization.CultureInfo]::InvariantCulture
if ([double]::IsNaN($FromElapsed) -or [double]::IsInfinity($FromElapsed) -or $FromElapsed -lt 0 -or
    [double]::IsNaN($ToElapsed) -or $ToElapsed -lt $FromElapsed) { throw 'Invalid elapsed time range.' }
$metrics = @('camera_capture_ms', 'detection_queue_ms', 'detection_processing_ms', 'preprocess_ms',
    'inference_latency_ms', 'npu_core_ms', 'postprocess_ms', 'tracking_queue_ms', 'tracking_processing_ms',
    'capture_to_tracking_ms', 'control_queue_ms', 'control_submit_ms', 'capture_to_control_submit_ms',
    'gui_queue_ms', 'gui_convert_ms', 'gui_submit_ms', 'encode_rtp_ms', 'capture_to_rtp_ready_ms')
$required = @('elapsed_s', 'session_id', 'model_generation', 'tracker_epoch', 'metric_frame_id',
    'model_id', 'tracker_id', 'valid_mask') + $metrics
$rows = @(Import-Csv -LiteralPath $Path -Encoding UTF8)
if ($rows.Count -eq 0) { throw 'No runtime telemetry samples.' }
foreach ($name in $required) {
    if ($rows[0].PSObject.Properties.Name -notcontains $name) { throw "Missing column: $name" }
}
function Number([string]$value) {
    $number = [double]::Parse($value, $culture)
    if ([double]::IsNaN($number) -or [double]::IsInfinity($number) -or $number -lt 0) {
        throw "Invalid nonnegative value: $value"
    }
    return $number
}
$seen = @{}
$groups = @{}
foreach ($row in $rows) {
    # Preserve uint64 identities exactly; do not convert them through floating point.
    foreach ($name in @('session_id', 'model_generation', 'tracker_epoch', 'metric_frame_id', 'model_id', 'tracker_id')) {
        $identity = [uint64]::Parse($row.$name, $culture)
        if ($name -in @('session_id', 'metric_frame_id', 'model_id', 'tracker_id') -and $identity -eq 0) {
            throw "Invalid zero identity: $name"
        }
        $row.$name = $identity.ToString($culture)
    }
    $elapsed = Number $row.elapsed_s
    if ($elapsed -lt $FromElapsed -or $elapsed -gt $ToElapsed -or ($ModelId -ne '' -and $row.model_id -ne $ModelId)) { continue }
    $key = $row.session_id + '/' + $row.model_generation + '/' + $row.tracker_epoch + '/' + $row.metric_frame_id
    if ($seen.ContainsKey($key)) { continue }
    $seen[$key] = $true
    $mask = [uint32]::Parse($row.valid_mask, $culture)
    if ($mask -gt 262143) { throw 'Unknown validity mask bits.' }
    $groupKey = $row.session_id + '/' + $row.model_generation + '/' + $row.tracker_epoch + '/' + $row.model_id + '/' + $row.tracker_id
    if (!$groups.ContainsKey($groupKey)) {
        $groups[$groupKey] = @{ Identity = $row; Stats = @{} }
        foreach ($metric in $metrics) {
            $groups[$groupKey].Stats[$metric] = @{ Count = 0L; Sum = 0.0; Min = [double]::PositiveInfinity; Max = 0.0 }
        }
    }
    for ($i = 0; $i -lt $metrics.Count; ++$i) {
        $metric = $metrics[$i]
        if (($mask -band (1 -shl $i)) -eq 0) { continue }
        if ([string]::IsNullOrWhiteSpace($row.$metric)) { throw "Valid metric has no value: $metric" }
        $value = Number $row.$metric
        $stat = $groups[$groupKey].Stats[$metric]
        $stat.Count++; $stat.Sum += $value
        $stat.Min = [math]::Min($stat.Min, $value)
        $stat.Max = [math]::Max($stat.Max, $value)
    }
}
$result = @(foreach ($key in ($groups.Keys | Sort-Object)) {
    $group = $groups[$key]
    foreach ($metric in $metrics) {
        $stat = $group.Stats[$metric]
        [pscustomobject][ordered]@{
            Session = $group.Identity.session_id; Generation = $group.Identity.model_generation
            Epoch = $group.Identity.tracker_epoch; Model = $group.Identity.model_id; Tracker = $group.Identity.tracker_id
            Metric = $metric; Samples = $stat.Count
            MinMs = $(if ($stat.Count) { $stat.Min } else { $null })
            AvgMs = $(if ($stat.Count) { $stat.Sum / $stat.Count } else { $null })
            MaxMs = $(if ($stat.Count) { $stat.Max } else { $null })
        }
    }
})
if ($AsJson) { ConvertTo-Json -InputObject $result -Depth 3 }
else {
    Write-Output 'Received unique runtime samples only; empty statistics mean unmeasured. Groups separate sessions, models, and generations.'
    $result | Format-Table -AutoSize
}
