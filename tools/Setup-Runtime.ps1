[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$runtime=Join-Path $PSScriptRoot 'NativeSpatial'
$downloads=Join-Path $root 'build\downloads'
New-Item -ItemType Directory -Path $downloads -Force | Out-Null
function Get-VerifiedFile([string]$Uri,[string]$Name,[string]$Hash) {
    $path=Join-Path $downloads $Name
    if(-not (Test-Path -LiteralPath $path)){Invoke-WebRequest -Uri $Uri -OutFile $path}
    if((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $Hash){throw "Download checksum failed: $Name"}
    return $path
}
$python=Get-VerifiedFile 'https://www.python.org/ftp/python/3.12.10/python-3.12.10-embed-amd64.zip' 'python-3.12.10-embed-amd64.zip' '4ACBED6DD1C744B0376E3B1CF57CE906F9DC9E95E68824584C8099A63025A3C3'
$metadata=Invoke-RestMethod -Uri 'https://pypi.org/pypi/frida/17.23.3/json'
$wheel=$metadata.urls | Where-Object {$_.filename -eq 'frida-17.23.3-cp37-abi3-win_amd64.whl'} | Select-Object -First 1
if(-not $wheel){throw 'Pinned Frida wheel unavailable'}
$frida=Get-VerifiedFile $wheel.url $wheel.filename '88B1F07514C38650FAF6E803ED13DFAEC1BA94EFA4C8ED34C1525F8C151689D4'
Expand-Archive -LiteralPath $python -DestinationPath (Join-Path $runtime 'python') -Force
& (Join-Path $runtime 'python\python.exe') (Join-Path $PSScriptRoot 'prepare_runtime.py') $frida (Join-Path $runtime 'deps')
if($LASTEXITCODE -ne 0){throw 'Capture runtime verification failed'}
