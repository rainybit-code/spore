# Cut a release: move the [Unreleased] notes under a dated [vX.Y.Z] heading in
# CHANGELOG.md, commit, tag, and push. CI then builds and attaches the binaries,
# using that same CHANGELOG section as the release body.
#
# Usage:  scripts\release.ps1 v1.2.3
param([Parameter(Mandatory = $true)][string]$Version)
$ErrorActionPreference = 'Stop'

if ($Version -notmatch '^v\d+\.\d+\.\d+$') { Write-Error 'usage: release.ps1 vX.Y.Z'; exit 1 }
$root = (Resolve-Path "$PSScriptRoot\..").Path
Set-Location $root
if (-not (Test-Path CHANGELOG.md)) { Write-Error "no CHANGELOG.md in $root"; exit 1 }

git diff --quiet; if (-not $?) { Write-Error 'working tree not clean - commit or stash first'; exit 1 }
$branch = (git rev-parse --abbrev-ref HEAD).Trim()
if ($branch -ne 'main') { Write-Error "on '$branch', not 'main' - refusing"; exit 1 }
git rev-parse -q --verify "refs/tags/$Version" > $null 2>&1
if ($?) { Write-Error "tag $Version already exists"; exit 1 }

$cl = Get-Content CHANGELOG.md -Raw
$m = [regex]::Match($cl, '(?ms)^## \[Unreleased\]\s*?\n(.*?)(?=^## \[)')
$body = if ($m.Success) { $m.Groups[1].Value.Trim() } else { '' }
if (-not $body) { Write-Error 'nothing under [Unreleased] - add release notes there first'; exit 1 }

$date = Get-Date -Format 'yyyy-MM-dd'
$new = [regex]::Replace($cl, '(?m)^## \[Unreleased\]\r?\n', "## [Unreleased]`n`n## [$Version] - $date`n", 1)
Set-Content CHANGELOG.md $new -NoNewline -Encoding utf8

git add CHANGELOG.md
# keep the firmware version constant in sync with the tag (no-op outside the firmware repo)
if (Test-Path src/config/params.h) {
  $noV = $Version.TrimStart('v')
  $ph = Get-Content src/config/params.h -Raw
  $ph = [regex]::Replace($ph, '(kFwVersion\[\] = ")[^"]*"', "`${1}$noV`"")
  Set-Content src/config/params.h $ph -NoNewline -Encoding utf8
  git add src/config/params.h
}
git commit -m "release $Version"
git tag -a $Version -m $Version
Write-Host "Tagged $Version - pushing main + tag..."
git push origin main
git push origin $Version
Write-Host "Done. The CI release workflow will build and publish $Version."
