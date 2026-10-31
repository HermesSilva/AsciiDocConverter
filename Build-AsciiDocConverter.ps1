<#
.SYNOPSIS
    Compila a biblioteca nativa AsciiDocConverter para Windows (x64) e Linux (x64 e arm64).

.DESCRIPTION
    Produz, em Dist\, um binário por sistema operacional e arquitetura, com nomes distintos — é por esse
    nome que o Tootega.Core (XAsciiDocConverter) escolhe qual carregar em tempo de execução:

        AsciiDocConverter.win-x64.dll
        AsciiDocConverter.linux-x64.so
        AsciiDocConverter.linux-arm64.so
        AsciiDocConverter.wasm            (entrada WebAssembly — a extensão do VS Code; DAD-13)

    Windows: MSBuild.exe do Visual Studio (o dotnet CLI não carrega os alvos do C++) sobre AsciiDocConverter.vcxproj.
    Linux:   contêiner Debian (Docker) com build-essential, crossbuild-essential-arm64 e python3;
             a compilação em si vive em build-linux.sh, ao lado deste arquivo.
    Wasm:    wasm/build-wasm.sh pelo WSL, com o wasi-sdk em ~/wasi-sdk (ou WASI_SDK_PATH).

    Mesmo padrão do Build-TootegaLZMA.ps1 da plataforma.

.PARAMETER Alvo
    Todos (padrão), Windows, Linux ou Wasm.
.PARAMETER Configuracao
    Release (padrão) ou Debug — só afeta o Windows.
.PARAMETER Limpar
    Esvazia Dist antes de compilar.
#>
[CmdletBinding()]
param(
    [ValidateSet('Todos', 'Windows', 'Linux', 'Wasm')] [string] $Alvo = 'Todos',
    [ValidateSet('Release', 'Debug')] [string] $Configuracao = 'Release',
    [switch] $Limpar
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$RaizProjeto = $PSScriptRoot
$Dist        = Join-Path $RaizProjeto 'Dist'
$Vcxproj     = Join-Path $RaizProjeto 'AsciiDocConverter.vcxproj'
$ImagemLinux = 'debian:bookworm-slim'

if (-not (Test-Path $Vcxproj)) { throw "Projeto nativo não encontrado: $Vcxproj" }

function Escrever-Etapa([string] $pTexto) { Write-Host ''; Write-Host "=== $pTexto" -ForegroundColor Cyan }

if ($Limpar -and (Test-Path $Dist)) { Escrever-Etapa 'Limpando Dist'; Remove-Item -Path (Join-Path $Dist '*') -Recurse -Force }
if (-not (Test-Path $Dist)) { New-Item -ItemType Directory -Path $Dist | Out-Null }

function Localizar-MSBuild {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $vswhere)) { throw "vswhere.exe não encontrado. Instale o Visual Studio com a carga 'Desenvolvimento para desktop com C++'." }
    $caminho = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
    if (-not $caminho) { throw "MSBuild.exe com toolset C++ não encontrado. Instale a carga 'Desenvolvimento para desktop com C++'." }
    return $caminho
}

function Compilar-Windows {
    Escrever-Etapa 'Windows x64'
    $msbuild = Localizar-MSBuild
    Write-Host "MSBuild: $msbuild"
    & $msbuild $Vcxproj /nologo /v:minimal /p:Configuration=$Configuracao /p:Platform=x64
    if ($LASTEXITCODE -ne 0) { throw "MSBuild falhou (código $LASTEXITCODE)." }
    $origem = Join-Path $RaizProjeto "bin\x64\$Configuracao\AsciiDocConverter.dll"
    if (-not (Test-Path $origem)) { throw "Binário não produzido: $origem" }
    Copy-Item $origem (Join-Path $Dist 'AsciiDocConverter.win-x64.dll') -Force
    Write-Host "    $Dist\AsciiDocConverter.win-x64.dll"
}

function Compilar-Linux {
    Escrever-Etapa 'Linux x64 + arm64 (Docker)'
    $docker = Get-Command docker -ErrorAction SilentlyContinue
    if (-not $docker) { throw "Docker não encontrado no PATH — necessário para os binários Linux." }
    $comando = 'sed -i "s/\r$//" /src/build-linux.sh && bash /src/build-linux.sh'
    & docker run --rm -v "$($RaizProjeto):/src" $ImagemLinux bash -lc $comando
    if ($LASTEXITCODE -ne 0) { throw "Build Linux falhou (código $LASTEXITCODE). O daemon do Docker está ativo?" }
}

function Compilar-Wasm {
    Escrever-Etapa 'WebAssembly (wasi-sdk via WSL)'
    $wsl = Get-Command wsl -ErrorAction SilentlyContinue
    if (-not $wsl) { throw "WSL não encontrado — o build WebAssembly roda wasm/build-wasm.sh num Linux com o wasi-sdk (~/wasi-sdk ou WASI_SDK_PATH)." }
    $caminhoWsl = & wsl -e wslpath -a $RaizProjeto
    if ($LASTEXITCODE -ne 0) { throw "wslpath falhou para $RaizProjeto." }
    & wsl -e bash -lc "cd '$caminhoWsl' && bash wasm/build-wasm.sh"
    if ($LASTEXITCODE -ne 0) { throw "Build WebAssembly falhou (código $LASTEXITCODE). O wasi-sdk está em ~/wasi-sdk no WSL?" }
}

if ($Alvo -in @('Todos', 'Windows')) { Compilar-Windows }
if ($Alvo -in @('Todos', 'Linux'))   { Compilar-Linux }
if ($Alvo -in @('Todos', 'Wasm'))    { Compilar-Wasm }

Escrever-Etapa 'Distribuição'
$esperados = @{ 'Windows' = @('AsciiDocConverter.win-x64.dll'); 'Linux' = @('AsciiDocConverter.linux-x64.so', 'AsciiDocConverter.linux-arm64.so'); 'Wasm' = @('AsciiDocConverter.wasm') }
$aConferir = switch ($Alvo) { 'Windows' { $esperados['Windows'] } 'Linux' { $esperados['Linux'] } 'Wasm' { $esperados['Wasm'] } default { $esperados['Windows'] + $esperados['Linux'] + $esperados['Wasm'] } }
$faltando = @()
foreach ($nome in $aConferir) {
    $arquivo = Join-Path $Dist $nome
    if (Test-Path $arquivo) { $kb = [math]::Round((Get-Item $arquivo).Length / 1KB, 1); Write-Host ("  {0,-36} {1,8} KB" -f $nome, $kb) -ForegroundColor Green }
    else { $faltando += $nome; Write-Host ("  {0,-36} {1}" -f $nome, 'AUSENTE') -ForegroundColor Red }
}
if ($faltando.Count -gt 0) { throw "Binários ausentes: $($faltando -join ', ')" }
Write-Host ''
Write-Host 'AsciiDocConverter: distribuição completa.' -ForegroundColor Green
