param(
    [Parameter(Mandatory = $true, Position = 0)]
    [string]$Ssid,

    [Parameter(Mandatory = $true, Position = 1)]
    [string]$Password,

    [Parameter(Mandatory = $false, Position = 2)]
    [string]$OutFile
)

$ErrorActionPreference = "Stop"

function Escape-CString {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Value
    )

    # Escape backslash and double quote for C string literals
    return $Value.Replace('\', '\\').Replace('"', '\"')
}

$EscapedSsid = Escape-CString $Ssid
$EscapedPassword = Escape-CString $Password

$output = @(
    "#pragma once"
    ""
    "#define PICO_WIFI_SSID `"$EscapedSsid`""
    "#define PICO_WIFI_PASSWORD `"$EscapedPassword`""
    "#define CY43_COUNTRY_CODE CYW43_COUNTRY_CANADA"
    ""
)

if (-not [string]::IsNullOrWhiteSpace($OutFile)) {
    $outDir = Split-Path -Parent $OutFile

    if (-not [string]::IsNullOrWhiteSpace($outDir)) {
        New-Item -ItemType Directory -Path $outDir -Force | Out-Null
    }

    $output | Set-Content -Path $OutFile -Encoding ascii
}
else {
    $output
}