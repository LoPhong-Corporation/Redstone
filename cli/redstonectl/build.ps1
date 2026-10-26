# Cross-compiles redstonectl into .\dist (needs Go installed)
$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot
New-Item -ItemType Directory -Force dist | Out-Null
$targets = @("windows/amd64","windows/arm64","linux/amd64","linux/arm64","darwin/amd64","darwin/arm64")
foreach ($t in $targets) {
  $os,$arch = $t.Split("/")
  $ext = ""; if ($os -eq "windows") { $ext = ".exe" }
  Write-Host "building $os/$arch"
  $env:CGO_ENABLED = "0"; $env:GOOS = $os; $env:GOARCH = $arch
  go build -trimpath -ldflags "-s -w" -o "dist/redstonectl-$os-$arch$ext" .
}
Remove-Item Env:GOOS, Env:GOARCH, Env:CGO_ENABLED
