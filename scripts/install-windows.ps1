[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$PluginDll,

    [string]$LocaleFile,
    [string]$ObsPath,
    [switch]$DryRun,
    [switch]$WaitForConfirmation
)

$ErrorActionPreference = 'Stop'

function Wait-ForInstallerConfirmation {
    if ($WaitForConfirmation) {
        Write-Host
        Read-Host '내용을 확인한 뒤 Enter 키를 눌러 설치 창을 닫으세요'
    }
}

trap {
    Write-Host
    Write-Host "StreamPing 설치 실패: $($_.Exception.Message)" -ForegroundColor Red
    Wait-ForInstallerConfirmation
    exit 1
}

function Test-Administrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]::new($identity)
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Test-ObsRoot([string]$Path) {
    if ([string]::IsNullOrWhiteSpace($Path)) {
        return $false
    }

    return Test-Path -LiteralPath (Join-Path $Path 'bin\64bit\obs64.exe') -PathType Leaf
}

function Get-ObsRootFromExecutable([string]$Executable) {
    if ([string]::IsNullOrWhiteSpace($Executable)) {
        return $null
    }

    $cleanPath = $Executable.Trim().Trim('"') -replace ',\d+$', ''
    if (-not (Test-Path -LiteralPath $cleanPath -PathType Leaf)) {
        return $null
    }

    $root = Split-Path -Parent $cleanPath
    $root = Split-Path -Parent $root
    $root = Split-Path -Parent $root
    return $root
}

function Find-ObsRoot {
    param([string]$PreferredPath)

    if ($PreferredPath) {
        $resolved = [IO.Path]::GetFullPath($PreferredPath)
        if (-not (Test-ObsRoot $resolved)) {
            throw "지정한 경로에서 OBS를 찾을 수 없습니다: $resolved"
        }
        return $resolved
    }

    $candidates = [Collections.Generic.List[string]]::new()

    Get-Process obs64 -ErrorAction SilentlyContinue | ForEach-Object {
        $root = Get-ObsRootFromExecutable $_.Path
        if ($root) {
            $candidates.Add($root)
        }
    }

    $uninstallRoots = @(
        'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\*',
        'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\*',
        'HKCU:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\*'
    )
    Get-ItemProperty $uninstallRoots -ErrorAction SilentlyContinue |
        Where-Object { $_.DisplayName -like '*OBS Studio*' } |
        ForEach-Object {
            if (Test-ObsRoot $_.InstallLocation) {
                $candidates.Add($_.InstallLocation)
            }

            $root = Get-ObsRootFromExecutable $_.DisplayIcon
            if ($root) {
                $candidates.Add($root)
            }

            if ($_.UninstallString) {
                $uninstaller = ($_.UninstallString.Trim().Trim('"') -replace '\s+/.*$', '')
                if (Test-Path -LiteralPath $uninstaller -PathType Leaf) {
                    $candidate = Split-Path -Parent $uninstaller
                    if (Test-ObsRoot $candidate) {
                        $candidates.Add($candidate)
                    }
                }
            }
        }

    @(
        (Join-Path $env:ProgramFiles 'obs-studio'),
        $(if (${env:ProgramFiles(x86)}) { Join-Path ${env:ProgramFiles(x86)} 'obs-studio' }),
        (Join-Path $env:LOCALAPPDATA 'Programs\obs-studio')
    ) | Where-Object { $_ -and (Test-ObsRoot $_) } | ForEach-Object { $candidates.Add($_) }

    $match = $candidates | Select-Object -Unique | Select-Object -First 1
    if (-not $match) {
        throw 'OBS Studio 설치 폴더를 찾지 못했습니다. -ObsPath로 폴더를 지정하세요.'
    }
    return $match
}

$pluginSource = (Resolve-Path -LiteralPath $PluginDll).Path
$localeSource = $null
if ($LocaleFile) {
    $localeSource = (Resolve-Path -LiteralPath $LocaleFile).Path
}

$obsRoot = Find-ObsRoot -PreferredPath $ObsPath
$targetDll = Join-Path $obsRoot 'obs-plugins\64bit\streamping.dll'
$targetLocale = Join-Path $obsRoot 'data\obs-plugins\streamping\locale\ko-KR.ini'

Write-Host "OBS 경로: $obsRoot"
Write-Host "대상 DLL: $targetDll"

if ($DryRun) {
    Write-Host '탐색 테스트 완료. 파일은 변경하지 않았습니다.'
    Wait-ForInstallerConfirmation
    exit 0
}

if (Get-Process obs64 -ErrorAction SilentlyContinue) {
    throw 'OBS Studio가 실행 중입니다. OBS를 완전히 종료한 뒤 다시 실행하세요.'
}

$programFilesRoots = @($env:ProgramFiles, ${env:ProgramFiles(x86)}) |
    Where-Object { -not [string]::IsNullOrWhiteSpace($_) }
$requiresElevation = $programFilesRoots | Where-Object {
    $obsRoot.StartsWith($_, [StringComparison]::OrdinalIgnoreCase)
}

if ($requiresElevation -and -not (Test-Administrator)) {
    $arguments = @(
        '-NoProfile',
        '-ExecutionPolicy', 'Bypass',
        '-File', "`"$PSCommandPath`"",
        '-PluginDll', "`"$pluginSource`"",
        '-ObsPath', "`"$obsRoot`""
    )
    if ($localeSource) {
        $arguments += @('-LocaleFile', "`"$localeSource`"")
    }
    if ($WaitForConfirmation) {
        $arguments += '-WaitForConfirmation'
    }

    $process = Start-Process powershell.exe -Verb RunAs -ArgumentList $arguments -Wait -PassThru
    exit $process.ExitCode
}

$targetDllDirectory = Split-Path -Parent $targetDll
New-Item -ItemType Directory -Force -Path $targetDllDirectory | Out-Null

if (Test-Path -LiteralPath $targetDll) {
    Copy-Item -LiteralPath $targetDll -Destination "$targetDll.bak" -Force
}
Copy-Item -LiteralPath $pluginSource -Destination $targetDll -Force

if ($localeSource) {
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $targetLocale) | Out-Null
    Copy-Item -LiteralPath $localeSource -Destination $targetLocale -Force
}

$sourceHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $pluginSource).Hash
$targetHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $targetDll).Hash
if ($sourceHash -ne $targetHash) {
    throw '설치 후 DLL 해시가 일치하지 않습니다.'
}

Write-Host 'StreamPing 업데이트가 완료되었습니다.' -ForegroundColor Green
Write-Host 'OBS Studio를 실행해 주세요.'
Wait-ForInstallerConfirmation
