#!/bin/sh

set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
sdk_ref=${PINPOINT_SDK_REF:-org.gnome.Sdk//50}
build_dir="$root/_build-gcc-analysis"
analysis_flags='-fanalyzer -Wno-analyzer-infinite-loop -Wformat=2 -Wshadow -Wstrict-prototypes -Wundef -Wcast-align=strict -Wwrite-strings -Wduplicated-cond -Wlogical-op -Wnull-dereference -Wvla'

if flatpak info --user "$sdk_ref" >/dev/null 2>&1
then
  installation=--user
else
  installation=--system
fi

# GLib's g_error() macro deliberately ends in an infinite loop after logging a
# fatal error. Suppress that analyzer false positive while retaining the rest
# of GCC's path-sensitive diagnostics as errors.
if [ -f "$build_dir/build.ninja" ]
then
  flatpak run "$installation" "--filesystem=$root" --command=meson \
    "$sdk_ref" setup --reconfigure "$build_dir" \
    -Dbuildtype=debug "-Dc_args=$analysis_flags"
else
  flatpak run "$installation" "--filesystem=$root" --command=meson \
    "$sdk_ref" setup "$build_dir" \
    -Dbuildtype=debug "-Dc_args=$analysis_flags"
fi

flatpak run "$installation" "--filesystem=$root" --command=meson \
  "$sdk_ref" compile -C "$build_dir"
