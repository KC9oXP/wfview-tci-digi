param(
    [string]$OutputDirectory = "build",
    [string]$LUpdate = "lupdate",
    [string]$LRelease = "lrelease"
)

$ErrorActionPreference = "Stop"
$TranslationDirectory = Join-Path $OutputDirectory "translations"
New-Item -ItemType Directory -Force -Path "translations" | Out-Null
New-Item -ItemType Directory -Force -Path $TranslationDirectory | Out-Null

& $LUpdate "wfview-tci-digi.pro"
if ($LASTEXITCODE -ne 0) { throw "lupdate failed with exit code $LASTEXITCODE" }

$Catalogs = Get-ChildItem -Path "translations" -Filter "*.ts" -File
foreach ($Catalog in $Catalogs) {
    $QmPath = Join-Path $TranslationDirectory ($Catalog.BaseName + ".qm")
    & $LRelease $Catalog.FullName "-qm" $QmPath
    if ($LASTEXITCODE -ne 0) { throw "lrelease failed for $($Catalog.Name)" }
}

Write-Host "Compiled translation catalogs into: $TranslationDirectory"
