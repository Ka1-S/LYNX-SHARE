#!/usr/bin/env bash
# Build the current work tree for Windows on GitHub Actions and put the result in OUT (default build-win).
# Run it from the root of a checkout. The build covers committed work, changes to tracked files and new files under
# the source folders; nothing else in the tree is sent. Exits with the run's result.
# The pushed tree is public: don't use this for anything that must stay private.
set -euo pipefail
repo="${LYNX_CI_REPO:-Ka1-S/LYNX-SHARE}"
out="${OUT:-build-win}"
limit="${LYNX_CI_LIMIT:-10}"
workflow="${LYNX_CI_WORKFLOW:-$(dirname "$(readlink -f "$0")")/../.github/workflows/round-build.yml}"
branch="ci/$(basename "$PWD")-$$"

# the tree to build, made with a private index so the work tree and its staging stay as they are
index="$(mktemp)"
trap 'rm -f "$index"' EXIT
cp "$(git rev-parse --git-path index)" "$index"
export GIT_INDEX_FILE="$index"
git add -u
for dir in src tests resources scripts tools third_party cmake; do
    [[ -d "$dir" ]] && git add "$dir"
done
if ! git cat-file -e "HEAD:.github/workflows/round-build.yml" 2>/dev/null; then
    blob="$(git hash-object -w "$workflow")"
    git update-index --add --cacheinfo "100644,$blob,.github/workflows/round-build.yml"
fi
tree="$(git write-tree)"
unset GIT_INDEX_FILE
sha="$(git commit-tree "$tree" -p HEAD -m "Round build of $(git rev-parse --short HEAD)")"

if git grep -qIi -e "${LYNX_CI_FORBIDDEN:-ligmuh2219}" "$sha" --; then
    echo "round build: the tree holds a forbidden string, not pushing" >&2
    exit 4
fi

running_builds() {
    gh run list -R "$repo" --limit 60 --json status,workflowName \
        -q '[.[] | select(.workflowName == "Round build" and .status != "completed")] | length'
}
# leave room for the project's own CI
until [[ "$(running_builds 2>/dev/null || echo "$limit")" -lt "$limit" ]]; do sleep 60; done

trap 'rm -f "$index"; git push -q origin --delete "$branch" 2>/dev/null || true' EXIT
git push -q origin "$sha:refs/heads/$branch" 2>/dev/null

run=""
for _ in $(seq 60); do
    run="$(gh run list -R "$repo" --branch "$branch" --limit 5 --json databaseId,workflowName \
             -q '[.[] | select(.workflowName == "Round build")][0].databaseId // empty' 2>/dev/null || true)"
    [[ -n "$run" ]] && break
    sleep 5
done
[[ -n "$run" ]] || { echo "round build: no run started for $branch" >&2; exit 3; }
echo "https://github.com/$repo/actions/runs/$run"

until [[ "$(gh run view "$run" -R "$repo" --json status -q .status 2>/dev/null || true)" == completed ]]; do sleep 30; done
conclusion="$(gh run view "$run" -R "$repo" --json conclusion -q .conclusion)"
if [[ "$conclusion" != success ]]; then
    gh run view "$run" -R "$repo" --log-failed 2>/dev/null | grep -E 'error:|Error |FAILED|fatal' | tail -40 >&2 || true
    echo "round build: $conclusion" >&2
    exit 1
fi
mkdir -p "$out"
gh run download "$run" -R "$repo" -n build-win -D "$out.dl$$"
for f in "$out.dl$$"/*; do mv -f "$f" "$out/"; done
rmdir "$out.dl$$"
ls -la "$out/LYNX.exe"
