#!/bin/sh

set -eu

if [ "$#" -lt 2 ]; then
  echo "usage: $0 OUTPUT.syscap PROFILE-ARGUMENTS..." >&2
  exit 2
fi

output=$1
shift
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_dir=$(dirname -- "$script_dir")
build_dir=${PINPOINT_BUILD_DIR:-"$repo_dir/_build"}
profile_binary="$build_dir/tests/test-lifecycle"
profiler_pid=

if [ ! -x "$profile_binary" ]; then
  echo "profiling binary not found: $profile_binary" >&2
  exit 1
fi

mkdir -p "$(dirname -- "$output")"
if [ -e "$output" ]; then
  echo "refusing to overwrite existing capture: $output" >&2
  exit 1
fi

keepalive_pid_file=$(mktemp)

stop_profiler ()
{
  if [ -s "$keepalive_pid_file" ]; then
    keepalive_pid=$(sed -n '1p' "$keepalive_pid_file")
    if kill -0 "$keepalive_pid" 2>/dev/null; then
      kill -TERM "$keepalive_pid"
    fi
  fi
  if [ -n "$profiler_pid" ] && kill -0 "$profiler_pid" 2>/dev/null; then
    wait "$profiler_pid" || true
  fi
  rm -f "$keepalive_pid_file"
}

trap stop_profiler EXIT INT TERM
sysprof-cli --force "$output" \
  --buffer-size=16384 --no-disk --no-network --gnome-shell --rapl \
  --scheduler --speedtrack --power-profile=balanced --no-debuginfod \
  --no-decode \
  -- /bin/sh -c 'trap "exit 0" TERM INT; echo "$$" > "$1"; while :; do /usr/bin/sleep 1; done' sh \
  "$keepalive_pid_file" &
profiler_pid=$!

# Give the host providers time to attach before starting the measured process.
sleep 1
if [ -n "${PINPOINT_PROFILE_GSK_RENDERER:-}" ]; then
  flatpak run --filesystem="$repo_dir" --device=dri \
    --socket=wayland --socket=fallback-x11 \
    --env="GSK_RENDERER=$PINPOINT_PROFILE_GSK_RENDERER" \
    --command="$profile_binary" org.gnome.Builder "$@"
else
  flatpak run --filesystem="$repo_dir" --device=dri \
    --socket=wayland --socket=fallback-x11 \
    --command="$profile_binary" org.gnome.Builder "$@"
fi

stop_profiler
profiler_pid=
trap - EXIT INT TERM
