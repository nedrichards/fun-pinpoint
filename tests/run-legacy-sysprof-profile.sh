#!/bin/sh

set -eu

if [ "$#" -lt 2 ] || [ "$#" -gt 4 ]; then
  echo "usage: $0 OUTPUT.syscap PRESENTATION [STEPS [INTERVAL_SECONDS]]" >&2
  exit 2
fi

output=$1
presentation=$2
steps=${3:-20}
interval=${4:-0.9}
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_dir=$(dirname -- "$script_dir")
accessibility_bus="unix:path=/run/user/$(id -u)/at-spi/bus"
event_controller=/org/a11y/atspi/registry/deviceeventcontroller
event_method=org.a11y.atspi.DeviceEventController.GenerateKeyboardEvent
profiler_pid=
legacy_pid=

mkdir -p "$(dirname -- "$output")"
if [ -e "$output" ]; then
  echo "refusing to overwrite existing capture: $output" >&2
  exit 1
fi
if [ ! -r "$presentation" ]; then
  echo "presentation not found: $presentation" >&2
  exit 1
fi

keepalive_pid_file=$(mktemp)

send_key ()
{
  gdbus call --address "$accessibility_bus" \
    --dest org.a11y.atspi.Registry \
    --object-path "$event_controller" \
    --method "$event_method" "$1" '' 3 >/dev/null
}

stop_processes ()
{
  if [ -n "$legacy_pid" ] && kill -0 "$legacy_pid" 2>/dev/null; then
    kill -TERM "$legacy_pid"
    wait "$legacy_pid" || true
  fi
  if [ -s "$keepalive_pid_file" ]; then
    keepalive_pid=$(sed -n '1p' "$keepalive_pid_file")
    if kill -0 "$keepalive_pid" 2>/dev/null; then
      kill -TERM "$keepalive_pid"
      sleep 1
      if kill -0 "$keepalive_pid" 2>/dev/null; then
        kill -TERM "$keepalive_pid"
        sleep 1
      fi
      if kill -0 "$keepalive_pid" 2>/dev/null; then
        kill -TERM "$keepalive_pid"
      fi
    fi
  fi
  if [ -n "$profiler_pid" ] && kill -0 "$profiler_pid" 2>/dev/null; then
    wait "$profiler_pid" || true
  fi
  rm -f "$keepalive_pid_file"
}

trap stop_processes EXIT INT TERM
sysprof-cli --force "$output" \
  --buffer-size=16384 --no-disk --no-network --gnome-shell --rapl \
  --scheduler --speedtrack --power-profile=balanced --no-debuginfod --no-decode \
  -- /bin/sh -c 'trap "exit 0" TERM INT; echo "$$" > "$1"; while :; do /usr/bin/sleep 1; done' sh \
  "$keepalive_pid_file" &
profiler_pid=$!

sleep 1
flatpak run --user --filesystem="$repo_dir" org.gnome.Pinpoint \
  -m "$presentation" &
legacy_pid=$!
sleep 2

step=0
while [ "$step" -lt "$steps" ]; do
  send_key 65363
  sleep "$interval"
  step=$((step + 1))
done
while [ "$step" -gt 0 ]; do
  send_key 65361
  sleep "$interval"
  step=$((step - 1))
done

send_key 113
sleep 1
if kill -0 "$legacy_pid" 2>/dev/null; then
  kill -TERM "$legacy_pid"
fi
wait "$legacy_pid" || true
legacy_pid=
stop_processes
profiler_pid=
trap - EXIT INT TERM
