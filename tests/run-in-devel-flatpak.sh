#!/bin/sh

set -eu

if [ "$#" -lt 1 ]; then
  echo "usage: $0 COMMAND [ARGUMENT...]" >&2
  exit 2
fi

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_dir=$(dirname -- "$script_dir")
app_id=${PINPOINT_FLATPAK_ID:-com.nedrichards.pinpoint.Devel}
command=$1
shift

if [ "$(basename -- "$command")" != "test-parser" ]; then
  exec "$command" "$@"
fi

run_test()
{
  flatpak run --user \
    --filesystem="$repo_dir" \
    --device=dri \
    --socket=wayland \
    --socket=fallback-x11 \
    --command="$command" \
    "$app_id" "$@"
}

if [ -e /.flatpak-info ]; then
  exec flatpak-spawn --host flatpak run --user \
    --filesystem="$repo_dir" \
    --device=dri \
    --socket=wayland \
    --socket=fallback-x11 \
    --command="$command" \
    "$app_id" "$@"
else
  run_test "$@"
fi
