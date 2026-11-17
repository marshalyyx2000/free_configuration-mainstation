# 为 SCD 中 10.100.100.x 批量添加本机 IP（需管理员）
# 用法: powershell -ExecutionPolicy Bypass -File add_sim_ips.ps1 -InterfaceAlias "以太网"

param(
    [string]$InterfaceAlias = "",
    [int]$Start = 20,
    [int]$End = 99
)

$ErrorActionPreference = "Stop"

if (-not $InterfaceAlias) {
    $InterfaceAlias = (Get-NetIPAddress -AddressFamily IPv4 |
        Where-Object { $_.IPAddress -like "192.168.0.*" } |
        Select-Object -First 1).InterfaceAlias
}

if (-not $InterfaceAlias) {
    Write-Error "未找到 192.168.0.x 网卡，请用 -InterfaceAlias 指定"
}

Write-Host "网卡: $InterfaceAlias, 添加 10.100.100.$Start .. 10.100.100.$End"

for ($i = $Start; $i -le $End; $i++) {
    $ip = "10.100.100.$i"
    $exists = Get-NetIPAddress -InterfaceAlias $InterfaceAlias -IPAddress $ip -ErrorAction SilentlyContinue
    if ($exists) { continue }
    New-NetIPAddress -InterfaceAlias $InterfaceAlias -IPAddress $ip -PrefixLength 24 -SkipAsSource $true | Out-Null
    Write-Host "  + $ip"
}

Write-Host "完成。装置侧请执行: sudo ip route add 10.100.100.0/24 via 192.168.0.20 dev eth0"
