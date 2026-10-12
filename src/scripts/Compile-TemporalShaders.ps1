param([Parameter(Mandatory)][string]$Source,[Parameter(Mandatory)][string]$OutputDirectory,
      [Parameter(Mandatory)][string]$FxcPath)
$ErrorActionPreference='Stop'
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$shaderMatch=[regex]::Match([IO.File]::ReadAllText($Source),'R"hlsl\((.*?)\)hlsl"',[Text.RegularExpressions.RegexOptions]::Singleline)
if(!$shaderMatch.Success){throw 'Temporal HLSL source was not found'}
$shaderPath=Join-Path $OutputDirectory 'Temporal.hlsl'
[IO.File]::WriteAllText($shaderPath,$shaderMatch.Groups[1].Value,[Text.UTF8Encoding]::new($false))
foreach($entry in @('main','reduce')){
    $header=Join-Path $OutputDirectory ('Temporal'+$entry+'.h')
    $symbol='kTemporal'+$entry+'Bytecode'
    & $FxcPath /nologo /T cs_5_1 /E $entry /O3 /Ges /Fh $header /Vn $symbol $shaderPath
    if($LASTEXITCODE -ne 0){throw "Temporal shader compilation failed: $entry"}
}
