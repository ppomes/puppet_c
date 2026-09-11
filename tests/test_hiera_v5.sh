#!/bin/bash
# Item 40 — environment-layer hiera.yaml in Hiera 5 format.
#
# puppet_hiera_config_create() used to understand only Hiera 3
# (":hierarchy:" list of extension-less strings + ":yaml: :datadir:").
# Against the version-5 hiera.yaml every real puppetserver ships, the
# ":hierarchy" key is simply absent: the hierarchy came out empty and every
# lookup failed silently — lookup('k') returned undef, lookup('k','d')
# always returned the default. Class parameters only appeared to work
# because puppet_apl_lookup() carried a hard-coded, *cwd-relative* scan of
# hieradata/, hieralocal/, preprod/hieradata/... so results changed with the
# directory the compiler was launched from. That scan is gone.
#
# Covered here: v5 parsing (defaults.datadir/data_hash, name/path entries),
# %{module_name} / %{jbossenv} / %{facts.x.y} interpolation, the explicit
# warning on eyaml levels, cwd independence, the -D datadir override on a
# v5 file, the warning on a hiera.yaml that is neither v3 nor v5, and the
# Hiera 3 format still working unchanged.

set -u
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
PUPPETC="$PROJECT_DIR/compiler/.libs/puppetc-compile"
[ -x "$PUPPETC" ] || PUPPETC="$PROJECT_DIR/compiler/puppetc-compile"
V5="$SCRIPT_DIR/hieratest-v5"
V3="$SCRIPT_DIR/hieratest"

export DYLD_LIBRARY_PATH="$PROJECT_DIR/compiler/.libs:$PROJECT_DIR/common/.libs:$PROJECT_DIR/facter/.libs:${DYLD_LIBRARY_PATH:-}"
export LD_LIBRARY_PATH="$PROJECT_DIR/compiler/.libs:$PROJECT_DIR/common/.libs:$PROJECT_DIR/facter/.libs:${LD_LIBRARY_PATH:-}"

RED=$'\e[31m'; GREEN=$'\e[32m'; NC=$'\e[0m'
PASSED=0; FAILED=0
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

echo "=== Testing hiera.yaml v5 support for the environment layer ==="
echo

check() { if [ "$2" -eq 0 ]; then echo "  ${GREEN}✓${NC} $1"; ((PASSED++)); \
          else echo "  ${RED}✗${NC} $1"; [ -n "${3:-}" ] && echo "$3" | sed 's/^/      /'; ((FAILED++)); fi; }

expect_in() { # label needle file
    if grep -qF -- "$2" "$3"; then check "$1" 0; else check "$1" 1 "$(cat "$3")"; fi
}
expect_not_in() { # label needle file
    if grep -qF -- "$2" "$3"; then check "$1" 1 "$(cat "$3")"; else check "$1" 0; fi
}

# --- 1. v5 hierarchy, run from the fixture directory ----------------------
echo "1. Hiera 5 hierarchy (relative datadir, %{module_name}/%{jbossenv})"
( cd "$V5" && "$PUPPETC" -p . ) > "$TMP/rel.out" 2>&1
expect_in "class parameter from hiera 5 (APL)"        "param=APL-OK" "$TMP/rel.out"
expect_in "explicit lookup() resolves"                "x=LOOKUP-OK"  "$TMP/rel.out"
expect_in "lookup() default still honoured"           "y=nope"       "$TMP/rel.out"
expect_in "last hierarchy level (global.yaml) read"   "g=GLOBAL-OK"  "$TMP/rel.out"
echo

# --- 2. eyaml levels are skipped loudly, never silently -------------------
echo "2. Unsupported backends"
expect_in "eyaml level named in a warning" "level 'Secrets' ignored" "$TMP/rel.out"
expect_in "warning names the backend"      "eyaml_lookup_key"        "$TMP/rel.out"
echo

# --- 3. cwd independence --------------------------------------------------
echo "3. Result does not depend on the current directory"
( cd / && "$PUPPETC" -p "$V5" ) > "$TMP/abs.out" 2>&1
expect_in "APL from / with an absolute path"    "param=APL-OK" "$TMP/abs.out"
expect_in "lookup() from / with an absolute path" "x=LOOKUP-OK" "$TMP/abs.out"
( cd "$V5/.." && "$PUPPETC" -p hieratest-v5 ) > "$TMP/parent.out" 2>&1
expect_in "APL from the parent directory"       "param=APL-OK" "$TMP/parent.out"
expect_in "lookup() from the parent directory"  "x=LOOKUP-OK"  "$TMP/parent.out"
echo

# --- 4. %{facts.x.y} interpolation ---------------------------------------
echo "4. Structured fact interpolation in hierarchy paths"
( cd "$V5" && "$PUPPETC" -p -n v5node.preprod.it.int -f allfacts.yaml . ) > "$TMP/facts.out" 2>&1
expect_in "%{facts.networking.domain} level hit" "z=DOMAIN-OK" "$TMP/facts.out"
expect_in "other levels unaffected by facts"     "x=LOOKUP-OK" "$TMP/facts.out"
# Without facts the domain segment is empty, the level misses, default wins.
expect_in "missing fact -> level skipped, not an error" "z=nodomain" "$TMP/rel.out"
echo

# --- 5. -D still overrides the datadir of a v5 file -----------------------
echo "5. -D datadir override on a v5 hiera.yaml"
mkdir -p "$TMP/alt/local/foo"
printf -- '---\nmykey: "ALT-OK"\nfoo::bar::param: "ALT-PARAM"\n' > "$TMP/alt/local/foo/preprod.yaml"
( cd "$V5" && "$PUPPETC" -p -D "$TMP/alt" -m modules manifests/site.pp ) > "$TMP/dopt.out" 2>&1
expect_in     "-D redirects APL to the alternate tree"    "param=ALT-PARAM" "$TMP/dopt.out"
expect_in     "-D redirects lookup() too"                 "x=ALT-OK"        "$TMP/dopt.out"
expect_not_in "original datadir no longer consulted"      "GLOBAL-OK"       "$TMP/dopt.out"
echo

# --- 6. a hiera.yaml that is neither v3 nor v5 warns ----------------------
echo "6. Unrecognised hiera.yaml"
mkdir -p "$TMP/bogus"
printf -- '---\nhierarchy:\n  - common\n' > "$TMP/bogus/hiera.yaml"   # v5 keys, no version:
printf 'notify { "bogus-ok": }\n' > "$TMP/bogus/site.pp"
"$PUPPETC" -p "$TMP/bogus/site.pp" > "$TMP/bogus.out" 2>&1
expect_in "empty hierarchy is reported, not silent" "no usable hierarchy" "$TMP/bogus.out"
expect_in "compilation still succeeds"              "bogus-ok"            "$TMP/bogus.out"
echo

# --- 7. Hiera 3 regression ------------------------------------------------
echo "7. Hiera 3 format unchanged"
( cd "$V3" && "$PUPPETC" -p -n prod-x.example.com -f allfacts.yaml -m modules \
    -D hieralocal manifests/site.pp ) > "$TMP/v3.out" 2>&1
expect_in     "v3 %{module_name}/%{::env} level"  "bare=prod-specific-value" "$TMP/v3.out"
expect_in     "v3 global level"                   "shared=shared-from-global" "$TMP/v3.out"
expect_not_in "no spurious v5 warning on a v3 file" "no usable hierarchy"    "$TMP/v3.out"
echo

echo "=== Results: $PASSED passed, $FAILED failed ==="
[ "$FAILED" -eq 0 ]
