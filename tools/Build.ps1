[CmdletBinding()]
param([switch]$Package,[switch]$SkipRuntime)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
Push-Location $root
try {
    & cmake -S . -B build -G 'Visual Studio 17 2022' -A x64
    if($LASTEXITCODE -ne 0){throw 'CMake configure failed'}
    & cmake --build build --config Release --parallel
    if($LASTEXITCODE -ne 0){throw 'Native build failed'}
    & ctest --test-dir build -C Release --output-on-failure
    if($LASTEXITCODE -ne 0){throw 'Native checks failed'}
    & dotnet publish app/SpatialASIO.csproj -c Release -o dist/app --self-contained true
    if($LASTEXITCODE -ne 0){throw 'Desktop publish failed'}
    & dotnet dist/app/SpatialASIO.dll --self-test
    if($LASTEXITCODE -ne 0){throw 'Desktop configuration checks failed'}
    if(-not $SkipRuntime){& "$PSScriptRoot\Setup-Runtime.ps1"}
    Copy-Item -LiteralPath build/Release/SpatialASIO.Engine.exe -Destination dist/app/SpatialASIO.Engine.exe -Force
    New-Item -ItemType Directory -Path dist/app/Tools,dist/app/Licenses -Force | Out-Null
    Copy-Item -LiteralPath tools/NativeSpatial -Destination dist/app/Tools -Recurse -Force
    Copy-Item -LiteralPath LICENSE,README.md,THIRD-PARTY-NOTICES.md -Destination dist/app -Force
    Copy-Item -LiteralPath docs -Destination dist/app -Recurse -Force
    Copy-Item -LiteralPath third_party/asio/LICENSE.txt -Destination dist/app/Licenses/ASIO.txt -Force
    $nuget=if($env:NUGET_PACKAGES){$env:NUGET_PACKAGES}else{Join-Path $env:USERPROFILE '.nuget\packages'}
    foreach($name in @('microsoft.netcore.app.runtime.win-x64','microsoft.windowsdesktop.app.runtime.win-x64')) {
        $runtimePackage=Join-Path $nuget "$name\10.0.8"
        $files=if($name -eq 'microsoft.windowsdesktop.app.runtime.win-x64'){@('LICENSE')}else{@('LICENSE.TXT','THIRD-PARTY-NOTICES.TXT')}
        foreach($file in $files) {
            if(-not (Test-Path (Join-Path $runtimePackage $file))){throw "Runtime notice is missing: $runtimePackage\$file"}
            Copy-Item -LiteralPath (Join-Path $runtimePackage $file) -Destination "dist/app/Licenses/$name-$file" -Force
        }
    }
    Copy-Item -LiteralPath third_party/WPF-NOTICES.txt,packaging/languages/INNO-LICENSE.txt -Destination dist/app/Licenses -Force
    if($Package) {
        $iscc=Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6\ISCC.exe'
        if(-not(Test-Path $iscc)){throw 'Install Inno Setup 6.5 or later to create the installer'}
        & $iscc packaging/SpatialASIO.iss
        if($LASTEXITCODE -ne 0){throw 'Installer compilation failed'}
        Get-ChildItem dist -Filter '*.exe' | Get-FileHash -Algorithm SHA256 | ForEach-Object { "$($_.Hash.ToLowerInvariant())  $([IO.Path]::GetFileName($_.Path))" } | Set-Content dist/SHA256SUMS.txt -Encoding ascii
    }
} finally {Pop-Location}
