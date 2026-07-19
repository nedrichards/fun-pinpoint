#!/bin/sh

set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
sdk_ref=${PINPOINT_SDK_REF:-org.gnome.Sdk//50}
build_dir="$root/_build-coverage"
policy="$root/tests/coverage-policy.json"
report="$build_dir/meson-logs/coverage.json"

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
    -Dbuildtype=debug -Db_coverage=true
else
  flatpak run "$installation" "--filesystem=$root" --command=meson \
    "$sdk_ref" setup "$build_dir" -Dbuildtype=debug -Db_coverage=true
fi
flatpak run "$installation" "--filesystem=$root" --command=meson \
  "$sdk_ref" compile -C "$build_dir"

# Never combine counters from separate runs; doing so makes a stale execution
# look like current coverage.
find "$build_dir" -type f -name '*.gcda' -delete

arch=$(flatpak --default-arch)
sdk_libdir="$sdk_location/files/lib/$arch-linux-gnu"
. "$root/tests/sdk-host-environment.sh"
pinpoint_configure_sdk_host_environment "$sdk_location" "$sdk_libdir"
registry_file="$build_dir/gstreamer-registry.bin"
rm -f "$registry_file"
export GST_REGISTRY="$registry_file"
export G_DEBUG=${G_DEBUG:-fatal-criticals}

"$build_dir/tests/test-parser" "$root/tests/fixtures/compatibility.pin"
"$build_dir/tests/test-performance"
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
  "$root/tests/fixtures/camera.pin" \
  "$root/tests/fixtures/media-formats.pin" \
  "$root/tests/fixtures/corrupt-video.pin"
"$build_dir/tests/test-application" \
  "$build_dir/src/pinpoint" \
  "$root/tests/fixtures/multi-monitor.pin" \
  "$root/tests/fixtures/media-pipeline.pin"

PYTHONDONTWRITEBYTECODE=1 flatpak run "$installation" \
  "--filesystem=$root" --command=python3 "$sdk_ref" \
  "$root/tests/coverage-report.py" \
  --root "$root" \
  --build-dir "$build_dir" \
  --output "$report" \
  --policy "$policy"
