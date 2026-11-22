[CmdletBinding()]
# Opcoes: -Relogio, -Python, -Remover, -Sim, -SoMostrar e -Nome.
param(
    [string]$Relogio,
    [string]$Python,
    [switch]$Remover,
    [switch]$Sim,
    [switch]$SoMostrar,
    [string]$Nome = 'tokensmall'
)

$ErrorActionPreference = 'Stop'

function Obter-Pythonw {
    param([string]$Caminho, [bool]$AceitarInexistente)

    if ($Caminho) {
        $resolvido = [IO.Path]::GetFullPath($Caminho)
    }
    else {
        $comando = Get-Command 'pythonw.exe' -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
        if (-not $comando) {
            throw 'pythonw.exe nao foi encontrado. Passe -Python com o caminho completo do pythonw.exe.'
        }
        $resolvido = [IO.Path]::GetFullPath($comando.Source)
    }
    if ($resolvido -match '(?i)\\WindowsApps\\') {
        throw 'O alias da Microsoft Store em WindowsApps nao roda em atalho. Passe -Python com outro pythonw.exe.'
    }
    if (-not $AceitarInexistente -and -not (Test-Path -LiteralPath $resolvido -PathType Leaf)) {
        throw "pythonw.exe nao existe em: $resolvido"
    }
    return $resolvido
}

function Testar-IPv4Privado {
    param([string]$Texto)

    $ip = $null
    if (-not [Net.IPAddress]::TryParse($Texto, [ref]$ip) -or
        $ip.AddressFamily -ne [Net.Sockets.AddressFamily]::InterNetwork) {
        return $false
    }
    $b = $ip.GetAddressBytes()
    return ($b[0] -eq 10 -or
            ($b[0] -eq 172 -and $b[1] -ge 16 -and $b[1] -le 31) -or
            ($b[0] -eq 192 -and $b[1] -eq 168) -or
            ($b[0] -eq 169 -and $b[1] -eq 254) -or
            $b[0] -eq 127)
}

if ([string]::IsNullOrWhiteSpace($Nome) -or
    $Nome.IndexOfAny([IO.Path]::GetInvalidFileNameChars()) -ge 0 -or
    $Nome.Contains('/') -or $Nome.Contains('\')) {
    throw 'Use -Nome com um nome de arquivo simples.'
}

$pastaTokensmall = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
$scriptTokensmall = [IO.Path]::GetFullPath((Join-Path $pastaTokensmall 'tokensmall.py'))
$pastaInicializar = [Environment]::GetFolderPath('Startup')
$atalho = Join-Path $pastaInicializar ($Nome + '.lnk')

if ($Remover) {
    if ($SoMostrar) {
        Write-Host "Removeria o atalho: $atalho"
        Write-Host 'Perguntaria antes de apagar a chave do Gerenciador de Credenciais do Windows.'
        if ($Python) {
            $pythonw = Obter-Pythonw $Python $true
            $pythonExe = Join-Path (Split-Path -Parent $pythonw) 'python.exe'
            Write-Host "Comando, se confirmado: `"$pythonExe`" `"$scriptTokensmall`" chave apagar"
        }
        exit 0
    }

    if (Test-Path -LiteralPath $atalho) {
        Remove-Item -LiteralPath $atalho -Force
        Write-Host "Atalho removido: $atalho"
    }
    else {
        Write-Host 'O atalho ja nao existe.'
    }

    $confirmado = $Sim
    if (-not $confirmado) {
        $resposta = Read-Host 'Apagar tambem a chave do Gerenciador de Credenciais do Windows? (S/N)'
        $confirmado = $resposta -match '^[Ss]$'
    }
    if ($confirmado) {
        $pythonw = Obter-Pythonw $Python $false
        $pythonExe = Join-Path (Split-Path -Parent $pythonw) 'python.exe'
        if (-not (Test-Path -LiteralPath $pythonExe -PathType Leaf)) {
            throw "python.exe nao existe ao lado do pythonw.exe: $pythonExe"
        }
        & $pythonExe $scriptTokensmall chave apagar
        if ($LASTEXITCODE -ne 0) {
            throw 'Nao foi possivel apagar a chave.'
        }
        Write-Host 'Chave apagada do Gerenciador de Credenciais do Windows.'
    }
    exit 0
}

if (-not (Testar-IPv4Privado $Relogio)) {
    throw 'Passe -Relogio com um IPv4 privado, link-local ou de loopback.'
}

$pythonw = Obter-Pythonw $Python ([bool]$SoMostrar)
$pythonExe = Join-Path (Split-Path -Parent $pythonw) 'python.exe'
$argumentos = ('"{0}" enviar --relogio {1} --log' -f $scriptTokensmall, $Relogio)

Write-Warning 'Se o IP do relogio mudar com DHCP, reserve esse IP no roteador.'
Write-Warning 'Use um relogio por PC.'

if ($SoMostrar) {
    Write-Host "Criaria o atalho: $atalho"
    Write-Host "Alvo: $pythonw"
    Write-Host "Argumentos: $argumentos"
    Write-Host "Pasta de trabalho: $pastaTokensmall"
    Write-Host 'Antes, consultaria apenas o estado da chave.'
    exit 0
}

if (-not (Test-Path -LiteralPath $pythonExe -PathType Leaf)) {
    throw "python.exe nao existe ao lado do pythonw.exe: $pythonExe"
}

& $pythonExe $scriptTokensmall chave estado | Out-Host
if ($LASTEXITCODE -eq 1) {
    Write-Warning 'Nenhuma fonte de chave esta pronta. Rode: python tokensmall.py chave guardar'
}
elseif ($LASTEXITCODE -ne 0) {
    Write-Warning 'Nao foi possivel consultar o estado da chave; o atalho sera criado mesmo assim.'
}

$shell = New-Object -ComObject WScript.Shell
$link = $shell.CreateShortcut($atalho)
$link.TargetPath = $pythonw
$link.Arguments = $argumentos
$link.WorkingDirectory = $pastaTokensmall
$link.WindowStyle = 7
$link.Save()

Write-Host "Atalho criado: $atalho"
Write-Host 'Para verificar: abra %LOCALAPPDATA%\tokensmall\ultimo.txt e %LOCALAPPDATA%\tokensmall\envio.log.'
Write-Host "Para desinstalar: powershell -ExecutionPolicy Bypass -File `"$PSCommandPath`" -Remover -Nome `"$Nome`" -Python `"$pythonw`""
