[CmdletBinding()]
param(
    [ValidateSet('x64')]
    [string] $Target = 'x64',
    [ValidateSet('Debug', 'RelWithDebInfo', 'Release', 'MinSizeRel')]
    [string] $Configuration = 'RelWithDebInfo',
    [switch] $Package
)

$ErrorActionPreference = 'Stop'

if ( $DebugPreference -eq 'Continue' ) {
    $VerbosePreference = 'Continue'
    $InformationPreference = 'Continue'
}

if ( $env:CI -eq $null ) {
    throw "Package-Windows.ps1 requires CI environment"
}

if ( ! ( [System.Environment]::Is64BitOperatingSystem ) ) {
    throw "Packaging script requires a 64-bit system to build and run."
}

if ( $PSVersionTable.PSVersion -lt '7.2.0' ) {
    Write-Warning 'The packaging script requires PowerShell Core 7. Install or upgrade your PowerShell version: https://aka.ms/pscore6'
    exit 2
}

function Package {
    trap {
        Write-Error $_
        exit 2
    }

    $ScriptHome = $PSScriptRoot
    $ProjectRoot = Resolve-Path -Path "$PSScriptRoot/../.."
    $BuildSpecFile = "${ProjectRoot}/buildspec.json"

    $UtilityFunctions = Get-ChildItem -Path $PSScriptRoot/utils.pwsh/*.ps1 -Recurse

    foreach( $Utility in $UtilityFunctions ) {
        Write-Debug "Loading $($Utility.FullName)"
        . $Utility.FullName
    }

    $BuildSpec = Get-Content -Path ${BuildSpecFile} -Raw | ConvertFrom-Json
    $ProductName = $BuildSpec.name
    $ProductVersion = $BuildSpec.version

    $OutputName = "${ProductName}-${ProductVersion}-windows-${Target}"

    $RemoveArgs = @{
        ErrorAction = 'SilentlyContinue'
        Path = @(
            "${ProjectRoot}/release/${ProductName}-*-windows-*.zip"
            "${ProjectRoot}/release/${ProductName}-*-windows-*.exe"
        )
    }

    Remove-Item @RemoveArgs

    Log-Group "Archiving ${ProductName}..."
    $CompressArgs = @{
        Path = (Get-ChildItem -Path "${ProjectRoot}/release/${Configuration}" -Exclude "${OutputName}*.*")
        CompressionLevel = 'Optimal'
        DestinationPath = "${ProjectRoot}/release/${OutputName}.zip"
        Verbose = ($Env:CI -ne $null)
    }
    Compress-Archive -Force @CompressArgs
    Log-Group

    if ( $Package ) {
        # The zip is for anyone who would rather drop the folder in by hand;
        # the installer is for everyone else. Both ship.
        Log-Group "Building installer for ${ProductName}..."

        $IsccPath = Get-Command iscc -ErrorAction SilentlyContinue
        if ( $IsccPath -eq $null ) {
            # Inno Setup ships with the GitHub-hosted Windows runners, but not
            # necessarily on PATH.
            $IsccCandidates = @(
                "${Env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe"
                "${Env:ProgramFiles}\Inno Setup 6\ISCC.exe"
            )
            $IsccPath = $IsccCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
            if ( $IsccPath -eq $null ) {
                throw "Inno Setup (ISCC.exe) not found; cannot build the Windows installer."
            }
        } else {
            $IsccPath = $IsccPath.Source
        }

        $IsccArgs = @(
            "/DAppName=$($ProductName)"
            "/DAppDisplayName=$($BuildSpec.displayName)"
            "/DAppVersion=$($ProductVersion)"
            "/DAppPublisher=$($BuildSpec.author)"
            "/DAppURL=$($BuildSpec.website)"
            "/DSourceDir=$((Resolve-Path "${ProjectRoot}/release/${Configuration}").Path)"
            "/DOutputDir=$((Resolve-Path "${ProjectRoot}/release").Path)"
            "$((Resolve-Path "${ProjectRoot}/cmake/windows/resources/installer-windows.iss").Path)"
        )

        & $IsccPath @IsccArgs
        if ( $LASTEXITCODE -ne 0 ) {
            throw "Inno Setup failed with exit code ${LASTEXITCODE}."
        }
        Log-Group
    }
}

Package
