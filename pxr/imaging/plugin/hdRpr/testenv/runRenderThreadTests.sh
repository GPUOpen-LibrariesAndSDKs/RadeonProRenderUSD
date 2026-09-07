#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 || $# -gt 3 ]]; then
    echo "Usage: $0 /path/to/houdini [c++-standard=20] [sanitizers]" >&2
    exit 2
fi

hd_rpr_hfs=$1
hd_rpr_standard=${2:-20}
hd_rpr_sanitizers=${3:-}
hd_rpr_cxx=${CXX:-c++}
hd_rpr_test_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
hd_rpr_source_dir=$(cd -- "$hd_rpr_test_dir/.." && pwd)
hd_rpr_build_dir=$(mktemp -d "${TMPDIR:-/tmp}/hdRpr-thread-tests.XXXXXX")
trap 'rm -rf -- "$hd_rpr_build_dir"' EXIT

hd_rpr_flags=(
    "-std=c++$hd_rpr_standard" -DNDEBUG -pthread -O1 -g -Wall -Wextra
    -Wno-deprecated -Wno-deprecated-declarations
    "-I$hd_rpr_hfs/toolkit/include" "-I$hd_rpr_source_dir"
)
# Tf diagnostics can include Houdini's Python headers indirectly.
for hd_rpr_python_include in "$hd_rpr_hfs"/toolkit/include/python3.*; do
    [[ ! -d "$hd_rpr_python_include" ]] || hd_rpr_flags+=("-I$hd_rpr_python_include")
done
if [[ -n "$hd_rpr_sanitizers" ]]; then
    hd_rpr_flags+=("-fsanitize=$hd_rpr_sanitizers" -fno-omit-frame-pointer)
fi

for hd_rpr_test in testHdRprPausedShutdown testHdRprRenderThread; do
    "$hd_rpr_cxx" "${hd_rpr_flags[@]}" \
        "$hd_rpr_source_dir/renderThread.cpp" "$hd_rpr_test_dir/$hd_rpr_test.cpp" \
        "-L$hd_rpr_hfs/dsolib" "-Wl,-rpath,$hd_rpr_hfs/dsolib" \
        -lpxr_tf -lpxr_arch -o "$hd_rpr_build_dir/$hd_rpr_test"
    env LD_LIBRARY_PATH="$hd_rpr_hfs/dsolib:$hd_rpr_hfs/python/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
        timeout 15s "$hd_rpr_build_dir/$hd_rpr_test"
    echo "PASS: $hd_rpr_test"
done
