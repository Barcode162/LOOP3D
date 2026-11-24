#!/usr/bin/env bash
# Puts the built player (site/, from tools/build_site.sh on a commit whose Check is green) on gh-pages as one
# new commit, then checks that every file on the branch is byte for byte the one built.
#   tools/publish_pages.sh "what changed (Check run N green, main abc1234)"
# Files are copied with fresh times (not cp -a): with the old times git can take a changed file of the same size
# for unchanged and leave it out, as it once left the loader behind while the engine moved on (the page then
# stopped at "ASM_CONSTS[code] is not a function").
set -euo pipefail
cd "$(dirname "$0")/.."
msg="${1:?say what changed}"
[ -f site/index.html ] || { echo "no site/: run tools/build_site.sh first" >&2; exit 1; }
tmp="$(mktemp -d)"
trap 'git worktree remove --force "$tmp/pages" 2>/dev/null || true; rm -rf "$tmp"' EXIT
git fetch -q origin gh-pages
git worktree add -q "$tmp/pages" origin/gh-pages
cp -r site/. "$tmp/pages/"
(cd "$tmp/pages" && git add -A && git update-index -q --really-refresh || true)
(cd "$tmp/pages" && git add -A)
if (cd "$tmp/pages" && git diff --cached --quiet); then echo "gh-pages already has this build"; exit 0; fi
(cd "$tmp/pages" && git commit -q -m "Publish the player: $msg" && git push -q origin HEAD:gh-pages)
git fetch -q origin gh-pages
bad=0
while IFS= read -r f; do
  [ "$(git rev-parse "origin/gh-pages:$f" 2>/dev/null)" = "$(git hash-object "site/$f")" ] || { echo "not published as built: $f" >&2; bad=1; }
done < <(cd site && find . -type f | sed 's|^\./||')
[ "$bad" = 0 ] && echo "published: every file on gh-pages is the one built"
exit "$bad"
