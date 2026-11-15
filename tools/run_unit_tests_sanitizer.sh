#!/bin/bash
#
# run_unit_tests_sanitizer.sh — build pagespeed_automatic_test under a
# sanitizer and run the memory-safety-relevant suites.
#
# We have no CI; this is the local adoption of the nightly sanitizer gate:
# run it over uncommitted work before landing threading, lifetime, cache,
# fetcher, or rewriter-area changes.
#
# Prerequisites: an isolated gyp build tree (sources overlaid on a populated
# third_party/testing substrate) with generated makefiles, as used for the
# normal Release unit-test build. This script only swaps the compiler for
# sanitizer-injecting wrappers; see the SEC-08 ops record for the full
# tree-establishment recipe.
#
# Usage:
#   run_unit_tests_sanitizer.sh <build-dir> <asan|tsan> [gtest-filter] [--skip-build]
#
# Default filters: asan covers the shared-memory, cache, http/fetcher,
# thread/scheduler, and resource suites, minus RedisCache* (needs a live
# redis server, and EnsureDatabaseSelection recurses via RedisCommand when
# database_index_ is set — a pre-existing stack overflow, recorded in
# SEC-08 as a follow-up, not triaged by this gate). tsan is scoped to http
# concurrency plus the threading/lifetime tests (see TSAN_DEFAULT below).
# Leak detection is off by default (LSan on this tree is a separate, noisy
# gate); pass ASAN_OPTIONS yourself to turn it on. TSan stops at the first
# report: triage whether it names a line you changed before anything else.
set -euo pipefail

BUILD_DIR="${1:?usage: $0 <build-dir> <asan|tsan> [gtest-filter] [--skip-build]}"
MODE="${2:?usage: $0 <build-dir> <asan|tsan> [gtest-filter] [--skip-build]}"
shift 2
# TSan default is scoped like upstream's TSan lane (http concurrency + the
# threading/lifetime tests): the fork-based shared-memory harness predates
# sanitizers (spin handshakes, waitpid edges TSan cannot see) and needs its
# own modernization pass before whole-suite TSan is meaningful.
TSAN_DEFAULT='CacheUrlAsyncFetcherTest.*:UrlInputResourceTest.ConcurrentFirstReads*:PthreadSharedMemInstanceTest.*:*ThreadSystem*:*Scheduler*:*WorkerPool*:*Sequence*'
ASAN_DEFAULT='*SharedMem*:*Cache*:*Fetch*:*Http*:*Thread*:*Scheduler*:*Worker*:*Mutex*:*Resource*:*Inflat*:*Gzip*-RedisCache*'
FILTER=""
SKIP_BUILD=0
for arg in "$@"; do
  case "$arg" in
    --skip-build) SKIP_BUILD=1 ;;
    -*) echo "FAIL: unknown flag $arg" >&2; exit 1 ;;
    *) FILTER="$arg" ;;
  esac
done
if [ "$MODE" = "tsan" ]; then
  FILTER="${FILTER:-$TSAN_DEFAULT}"
else
  FILTER="${FILTER:-$ASAN_DEFAULT}"
fi

[ -f "$BUILD_DIR/Makefile" ] || { echo "FAIL: no Makefile in $BUILD_DIR" >&2; exit 1; }

RIG="$BUILD_DIR/.sanitizer-rig"
mkdir -p "$RIG"
if [ "$MODE" = "asan" ]; then
  printf '#!/bin/sh\nexec gcc -fsanitize=address -fno-omit-frame-pointer "$@"\n' \
    > "$RIG/cc-wrap"
  printf '#!/bin/sh\nexec g++ -fsanitize=address -fno-omit-frame-pointer "$@"\n' \
    > "$RIG/cxx-wrap"
  chmod +x "$RIG/cc-wrap" "$RIG/cxx-wrap"
  export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0}"
elif [ "$MODE" = "tsan" ]; then
  # -pie only at link time (gcc warns about it on -c compiles).
  cat > "$RIG/cc-wrap" <<'EOF'
#!/bin/sh
extra=""
case " $* " in *" -c "*) ;; *) extra="-pie" ;; esac
exec gcc -fsanitize=thread -fno-omit-frame-pointer -fPIE $extra "$@"
EOF
  cat > "$RIG/cxx-wrap" <<'EOF'
#!/bin/sh
extra=""
case " $* " in *" -c "*) ;; *) extra="-pie" ;; esac
exec g++ -fsanitize=thread -fno-omit-frame-pointer -fPIE $extra "$@"
EOF
  chmod +x "$RIG/cc-wrap" "$RIG/cxx-wrap"
  export TSAN_OPTIONS="${TSAN_OPTIONS:-halt_on_error=1:second_deadlock_stack=1}"
else
  echo "FAIL: mode must be asan or tsan" >&2
  exit 1
fi

# TSan maps a fixed shadow range; under restricted/sandboxed kernels that
# range can collide at startup (FATAL: unexpected memory mapping, exit 66).
# Disabling ASLR for the build's host tools and the test run works around
# it; race detection is unaffected.
SETARCH=()
if [ "$MODE" = "tsan" ] && command -v setarch >/dev/null 2>&1; then
  SETARCH=(setarch "$(uname -m)" -R)
fi

if [ "$SKIP_BUILD" -eq 0 ]; then
  # make does not track command changes, so objects built without the
  # sanitizer would be silently reused: force a full rebuild.
  find "$BUILD_DIR/out/Release/obj.target" "$BUILD_DIR/out/Release/obj.host" \
    -name '*.o' -delete 2>/dev/null || true
  "${SETARCH[@]}" make -C "$BUILD_DIR" -j"$(nproc)" BUILDTYPE=Release \
    CC="$RIG/cc-wrap" CXX="$RIG/cxx-wrap" pagespeed_automatic_test
fi

BIN="$BUILD_DIR/out/Release/pagespeed_automatic_test"
[ -x "$BIN" ] || { echo "FAIL: test binary missing: $BIN" >&2; exit 1; }
cd "$BUILD_DIR"  # testdata paths resolve from the checkout root.
exec "${SETARCH[@]}" "$BIN" --gtest_filter="$FILTER"
