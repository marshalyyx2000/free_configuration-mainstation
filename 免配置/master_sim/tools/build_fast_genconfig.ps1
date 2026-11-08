# 构建加速 genconfig.jar（Main-Class: FastGenconfigMain）
$ErrorActionPreference = "Stop"
$mg = Join-Path $PSScriptRoot "..\third_party\libiec61850\tools\model_generator"
$build = Join-Path $mg "build"
$outJar = Join-Path $mg "genconfig-fast.jar"
$vendorJar = Join-Path $mg "genconfig.jar"

if (-not (Test-Path $vendorJar)) {
    Write-Host "先用 build2.sh 或复制装置 genconfig.jar 到 $vendorJar"
}

Remove-Item -Recurse -Force $build -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Path $build | Out-Null

$javaFiles = Get-ChildItem -Path (Join-Path $mg "src") -Filter "*.java" -Recurse | ForEach-Object { $_.FullName }
# 装置上是 OpenJDK 8：必须 --release 8，否则会用 JDK 19 的 Java 11 API 编译通过但运行 NoSuchMethodError
$args = @("-encoding", "UTF-8", "--release", "8", "-d", $build) + $javaFiles
& javac @args
if ($LASTEXITCODE -ne 0) { throw "javac failed" }

Push-Location $mg
jar cfm genconfig-fast.jar manifest-fast.mf -C build com/
Pop-Location

Write-Host "OK $outJar"
Get-Item $outJar | Select-Object FullName, Length
