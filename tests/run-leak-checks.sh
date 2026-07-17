#!/bin/sh

set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
sdk_ref=${PINPOINT_SDK_REF:-org.gnome.Sdk//50}
build_dir="$root/_build-sanitize"

if sdk_location=$(flatpak info --user --show-location "$sdk_ref" 2>/dev/null)
then
  installation=--user
else
  sdk_location=$(flatpak info --system --show-location "$sdk_ref")
  installation=--system
fi

if [ -f "$build_dir/build.ninja" ]
then
  flatpak run "$installation" "--filesystem=$root" --command=meson \
    "$sdk_ref" setup --reconfigure "$build_dir" \
    -Db_sanitize=address,undefined -Db_lundef=false
else
  flatpak run "$installation" "--filesystem=$root" --command=meson \
    "$sdk_ref" setup "$build_dir" \
    -Db_sanitize=address,undefined -Db_lundef=false
fi
flatpak run "$installation" "--filesystem=$root" --command=meson \
  "$sdk_ref" compile -C "$build_dir"

arch=$(flatpak --default-arch)
sdk_libdir="$sdk_location/files/lib/$arch-linux-gnu"

# LeakSanitizer cannot inspect processes inside the Flatpak sandbox. Execute
# the SDK-built binaries directly while resolving their runtime libraries from
# the exact SDK commit that compiled them and graphics drivers from the exact
# Flatpak extension branch that the SDK declares.
. "$root/tests/sdk-host-environment.sh"
pinpoint_configure_sdk_host_environment "$sdk_location" "$sdk_libdir"
export ASAN_OPTIONS=${ASAN_OPTIONS:-detect_leaks=1:halt_on_error=1:abort_on_error=1:print_summary=1:detect_stack_use_after_return=1:strict_string_checks=1:suppressions=$root/tests/asan.supp}
export LSAN_OPTIONS=${LSAN_OPTIONS:-suppressions=$root/tests/lsan.supp:print_suppressions=1}
export UBSAN_OPTIONS=${UBSAN_OPTIONS:-halt_on_error=1:abort_on_error=1:print_summary=1:print_stacktrace=1}
export G_DEBUG=${G_DEBUG:-fatal-criticals,gc-friendly}
export G_SLICE=${G_SLICE:-always-malloc}

"$build_dir/tests/test-parser" "$root/tests/fixtures/compatibility.pin"
"$build_dir/tests/test-performance"
# The release-size budget remains in the normal Meson suite. ASan/UBSan
# instrumentation intentionally makes the executable larger than that budget.
"$build_dir/tests/test-pixels" \
  "$root/tests/fixtures/pixel-reference.pin" \
  "$root/tests/fixtures/svg-quality.pin" 800 600
"$build_dir/tests/test-pixels" \
  "$root/tests/fixtures/pixel-reference.pin" \
  "$root/tests/fixtures/svg-quality.pin" 1280 720
GDK_SCALE=2 "$build_dir/tests/test-pixels" \
  "$root/tests/fixtures/pixel-reference.pin" \
  "$root/tests/fixtures/svg-quality.pin" 800 600
"$build_dir/tests/test-lifecycle" \
  "$root/tests/fixtures/missing-video.pin" \
  "$root/tests/fixtures/legacy-transition.pin" \
  "$root/tests/fixtures/native-transitions.pin" \
  "$root/tests/fixtures/page-curl.pin" \
  "$root/tests/fixtures/multi-monitor.pin" \
  "$root/tests/fixtures/camera.pin"
"$build_dir/tests/test-application" \
  "$build_dir/src/pinpoint" \
  "$root/tests/fixtures/multi-monitor.pin" \
  "$root/tests/fixtures/media-pipeline.pin"
