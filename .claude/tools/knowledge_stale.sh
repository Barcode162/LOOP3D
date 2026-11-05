#!/usr/bin/env bash
# Lists engine files changed since the knowledge base was last verified, so
# Claude knows which notes in .claude/knowledge/ to re-check before trusting them.
#   .claude/tools/knowledge_stale.sh            changes since the recorded commit
#   .claude/tools/knowledge_stale.sh --stamp    record HEAD as verified (after updating the notes)
set -euo pipefail
cd "$(dirname "$0")/../.."
stamp=.claude/knowledge/VERIFIED_AT
if [ "${1:-}" = "--stamp" ]; then git rev-parse --short HEAD > "$stamp"; echo "verified at $(cat "$stamp")"; exit 0; fi
base=$(cat "$stamp")
changed=$(git diff --name-only "$base" HEAD -- engine web tools tests docs CMakeLists.txt projects/*/project.json; git diff --name-only -- engine web tools tests docs CMakeLists.txt)
changed=$(echo "$changed" | sed '/^$/d' | sort -u)
if [ -z "$changed" ]; then echo "knowledge is current (verified at $base)"; exit 0; fi
echo "changed since $base (re-check the matching notes):"
echo "$changed" | while read -r f; do
  case "$f" in
    engine/script/scene_api*) n=subsystems/scene-api.md ;;
    engine/script/*) n=subsystems/script.md ;;
    engine/render/*) n=subsystems/render.md ;;
    engine/gpu/*) n=subsystems/gpu.md ;;
    engine/physics/*) n=subsystems/physics.md ;;
    engine/anim/*) n=subsystems/anim.md ;;
    engine/audio/*) n=subsystems/audio.md ;;
    engine/geometry/*) n=subsystems/geometry.md ;;
    engine/core/*) n=subsystems/core.md ;;
    web/*) n=subsystems/web.md ;;
    tools/*|tests/*) n=subsystems/tools-tests.md ;;
    docs/LANGUAGE.md) n=language-quickref.md ;;
    *) n=00-fast-start.md ;;
  esac
  printf '  %-45s -> %s\n' "$f" "$n"
done
