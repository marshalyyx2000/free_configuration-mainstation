$subnet = "10.237.1"
$alive = @()
foreach ($i in 1..254) {
    $ip = "$subnet.$i"
    $out = ping -n 1 -w 300 $ip 2>$null | Out-String
    if ($out -match "TTL=") { $alive += $ip }
}
Write-Host "本机参考: 10.237.1.95"
Write-Host "在线: $($alive.Count)"
$alive | Sort-Object { [int]($_ -split '\.')[-1] }
