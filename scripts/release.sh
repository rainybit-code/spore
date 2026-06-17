#!/usr/bin/env bash
# Cut a release: move the [Unreleased] notes under a dated [vX.Y.Z] heading in
# CHANGELOG.md, commit, tag, and push. CI then builds and attaches the binaries,
# using that same CHANGELOG section as the release body.
#
# Usage:  scripts/release.sh v1.2.3
set -euo pipefail

ver="${1:-}"
[[ "$ver" =~ ^v[0-9]+\.[0-9]+\.[0-9]+$ ]] || { echo "usage: $0 vX.Y.Z"; exit 1; }

root="$(cd "$(dirname "$0")/.." && pwd)"; cd "$root"
[ -f CHANGELOG.md ] || { echo "no CHANGELOG.md in $root"; exit 1; }

git diff --quiet && git diff --cached --quiet || { echo "working tree not clean — commit or stash first"; exit 1; }
branch="$(git rev-parse --abbrev-ref HEAD)"
[ "$branch" = "main" ] || { echo "on '$branch', not 'main' — refusing"; exit 1; }
git rev-parse -q --verify "refs/tags/$ver" >/dev/null && { echo "tag $ver already exists"; exit 1; } || true

# require something under [Unreleased]
body="$(awk '/^## \[Unreleased\]/{f=1;next} /^## \[/{f=0} f' CHANGELOG.md | grep -v '^[[:space:]]*$' || true)"
[ -n "$body" ] || { echo "nothing under [Unreleased] — add release notes there first"; exit 1; }

date="$(date +%F)"
awk -v ver="$ver" -v date="$date" '
  /^## \[Unreleased\]/ && !done { print; print ""; print "## [" ver "] - " date; done=1; next }
  { print }
' CHANGELOG.md > CHANGELOG.tmp && mv CHANGELOG.tmp CHANGELOG.md

git add CHANGELOG.md
# keep the firmware version constant in sync with the tag (no-op outside the firmware repo)
if [ -f src/config/params.h ]; then
  sed -i -E "s/(kFwVersion\[\] = \")[^\"]*\"/\1${ver#v}\"/" src/config/params.h
  git add src/config/params.h
fi
git commit -m "release $ver"
git tag -a "$ver" -m "$ver"
echo "Tagged $ver — pushing main + tag…"
git push origin main
git push origin "$ver"
echo "Done. The CI release workflow will build and publish $ver."
