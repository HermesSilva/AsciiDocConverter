<#
.SYNOPSIS
    Compilação de desenvolvimento no Windows (MSVC): DLL, adc_cli.exe e adc_tck.exe em obj\dev\.

.DESCRIPTION
    Localiza o vcvars64.bat do Visual Studio mais recente (vswhere) e compila com cl.exe:
    C11 (/std:c11), fontes UTF-8 (/utf-8), avisos como erro (/W4 /WX). O build oficial de
    distribuição (Dist\) é o Build-AsciiDocConverter.ps1; este é o ciclo rápido de iteração.

.PARAMETER Configuracao
    Debug (padrão: /Od /Zi) ou Release (/O2).
#>
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')] [string] $Configuracao = 'Debug'
)
$ErrorActionPreference = 'Stop'
$Raiz = $PSScriptRoot
$Obj  = Join-Path $Raiz 'obj\dev'
New-Item -ItemType Directory -Force -Path $Obj | Out-Null

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $vswhere)) { throw 'vswhere.exe não encontrado: instale o Visual Studio com a carga C++.' }
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'Visual Studio com ferramentas C++ não encontrado.' }
$vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'

$fontes = Get-ChildItem -Path (Join-Path $Raiz 'src') -Recurse -Filter '*.c' | ForEach-Object { '"' + $_.FullName + '"' }
$terceiros = @('"' + (Join-Path $Raiz 'terceiros\miniz.c') + '"')
# fontes embutidas: geradas pelo Python da máquina (só biblioteca padrão)
$fontesC = Join-Path $Raiz 'obj\fontes\adc_fontes_embutidas.c'
& python -I (Join-Path $Raiz 'ferramentas\gerar_fontes.py') $fontesC
if ($LASTEXITCODE -ne 0) { throw 'Falha ao gerar as fontes embutidas (python -I ferramentas/gerar_fontes.py).' }
$terceiros += '"' + $fontesC + '"'

$opt = if ($Configuracao -eq 'Debug') { '/Od /Zi /RTC1 /MDd /D_DEBUG' } else { '/O2 /MD /DNDEBUG' }
$comuns = "/nologo /std:c11 /utf-8 /W4 /WX /wd4996 /wd4100 /wd4127 /wd4204 /wd4221 /wd4456 /wd4457 /wd4459 /D_CRT_SECURE_NO_WARNINGS /I`"$Raiz\include`" /I`"$Raiz\src`" /I`"$Raiz\src\pdf`" /wd4295 /wd4310 /wd4267 /wd4244 /wd4245 /wd4701 /wd4702 /wd4703 /wd4706 /wd4324 /wd4005 /Fo`"$Obj\\`" $opt"

$linhas = @(
    "call `"$vcvars`" >nul",
    "cl $comuns /DADC_EXPORTS /LD $($fontes -join ' ') $($terceiros -join ' ') /Fe`"$Obj\AsciiDocConverter.dll`" /link /DEBUG || exit /b 1",
    "cl $comuns /DADC_ESTATICO `"$Raiz\test\adc_cli.c`" $($fontes -join ' ') $($terceiros -join ' ') /Fe`"$Obj\adc_cli.exe`" /link /DEBUG || exit /b 1",
    "cl $comuns /DADC_ESTATICO `"$Raiz\test\tck\adc_tck.c`" $($fontes -join ' ') $($terceiros -join ' ') /Fe`"$Obj\adc_tck.exe`" /link /DEBUG || exit /b 1"
)
$cmd = Join-Path $Obj 'compilar.cmd'
# o .cmd precisa de CRLF
[System.IO.File]::WriteAllText($cmd, ("@echo off`r`n" + ($linhas -join "`r`n") + "`r`n"), [System.Text.UTF8Encoding]::new($false))
& cmd.exe /c "`"$cmd`""
if ($LASTEXITCODE -ne 0) { throw "Compilação falhou (código $LASTEXITCODE)." }
Write-Host "OK: $Obj\AsciiDocConverter.dll, adc_cli.exe, adc_tck.exe" -ForegroundColor Green
