param(
    [Parameter(Mandatory = $true, Position = 0)]
    [string]$Url,

    [Parameter(Mandatory = $false, Position = 1)]
    [string]$OutFile
)

$ErrorActionPreference = "Stop"

function Usage {
    Write-Host "usage: mkcaheader.ps1 <url> [outfile]" -ForegroundColor Yellow
    exit 1
}

if ([string]::IsNullOrWhiteSpace($Url)) {
    Usage
}

# Prefer openssl.exe from PATH
$OpenSsl = "\Program Files\Git\usr\bin\openssl.exe"

# Create a unique temp working directory
$TempDir = Join-Path $env:TEMP ("mkcaheader_{0}_{1}" -f $Url.Replace(":", "_").Replace("/", "_"), $PID)
New-Item -ItemType Directory -Path $TempDir -Force | Out-Null

try {
    $TempOutFile = Join-Path $TempDir "out.h"

    # Equivalent of:
    # openssl s_client -connect URL:443 -servername URL -showcerts </dev/null 2>/dev/null
    #
    # On Windows, pipe an empty string to close stdin.
		$oldErrorActionPreference = $ErrorActionPreference
		$ErrorActionPreference = "Continue"

    try {
        $chainText = "" | & $OpenSsl s_client `
            -connect "$Url`:443" `
            -servername "$Url" `
            -showcerts 2>$null
        if (-not $chainText) {
            throw "openssl did not return a certificate chain for $Url"
        }
    }
    finally {
        $ErrorActionPreference = $oldErrorActionPreference
    }

    # Extract PEM blocks: BEGIN CERTIFICATE through END CERTIFICATE
    $certMatches = [regex]::Matches(
        ($chainText -join "`n"),
        "-----BEGIN CERTIFICATE-----.*?-----END CERTIFICATE-----",
        [System.Text.RegularExpressions.RegexOptions]::Singleline
    )

    if ($certMatches.Count -eq 0) {
        throw "No certificates found in openssl output for $Url"
    }

    # Write each cert to its own temp file
    $certFiles = @()
    for ($i = 0; $i -lt $certMatches.Count; $i++) {
        $certPath = Join-Path $TempDir ("cert-{0}.pem" -f $i)
        $certMatches[$i].Value | Set-Content -Path $certPath -Encoding ascii
        $certFiles += $certPath
    }

    # Find the first certificate whose subject does not contain the URL.
    # This matches the intent of the Bash script's:
    # openssl x509 ... -subject | grep "${URL}" && continue
    $Cert = $null

    foreach ($file in $certFiles) {
        try {
            $subject = & $OpenSsl x509 -in $file -noout -subject 2>$null
            if (-not $subject) {
                continue
            }

            if ($subject -match [regex]::Escape($Url)) {
                continue
            }

            $Cert = $file
            break
        }
        catch {
            continue
        }
    }

    if (-not $Cert) {
        throw "Could not find a non-leaf certificate in the presented chain for $Url"
    }

    $certLines = Get-Content -Path $Cert

    $output = New-Object System.Collections.Generic.List[string]

    $output.Add("#pragma once")
    $output.Add("")
    $output.Add("#define HTTPS_HOST `"$Url`"")
    $output.Add("")
    $output.Add("#define HTTPS_CA_CERT \")

    for ($i = 0; $i -lt $certLines.Count; $i++) {
        $line = $certLines[$i]

        # Escape backslashes and double-quotes for C string literal
        $line = $line.Replace("\", "\\").Replace('"', '\"')

        if ($line -eq "-----END CERTIFICATE-----") {
            $output.Add($line + "`"")
        }
        elseif ($i -eq 0) {
            $output.Add('"' + $line + '\n\')
        }
        else {
            $output.Add($line + '\n\')
        }
    }

    $output.Add("")

    $output | Set-Content -Path $TempOutFile -Encoding ascii

    if (-not [string]::IsNullOrWhiteSpace($OutFile)) {
        $outDir = Split-Path -Parent $OutFile

        if (-not [string]::IsNullOrWhiteSpace($outDir)) {
            New-Item -ItemType Directory -Path $outDir -Force | Out-Null
        }

        Move-Item -Path $TempOutFile -Destination $OutFile -Force
    }
    else {
        Get-Content -Path $TempOutFile
    }
}
finally {
    Remove-Item -Path $TempDir -Recurse -Force -ErrorAction SilentlyContinue
}