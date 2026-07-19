#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build_dir=${PINPOINT_BUILD_DIR:-"$root/_build"}
fixture="$root/tests/fixtures/multi-monitor.pin"
sdk_ref=${PINPOINT_SDK_REF:-org.gnome.Sdk//50}
installation=--user

if ! flatpak info --user "$sdk_ref" >/dev/null 2>&1
then
  installation=--system
fi

shell_eval ()
{
  reply=$(gdbus call --session \
    --dest org.gnome.Shell \
    --object-path /org/gnome/Shell \
    --method org.gnome.Shell.Eval "$1")
  case "$reply" in
    "(true, '"*"')")
      value=${reply#"(true, '"}
      value=${value%"')"}
      printf '%s\n' "$value" | jq -r .
      ;;
    *)
      return 1
      ;;
  esac
}

window_state ()
{
  shell_eval '
    JSON.stringify(global.get_window_actors()
      .map(actor => actor.get_meta_window())
      .filter(window => window.get_title().includes("Pinpoint"))
      .map(window => ({
        id: window.get_id(),
        title: window.get_title(),
        monitor: window.get_monitor(),
        fullscreen: window.is_fullscreen()
      })))'
}

wait_for_window_count ()
{
  expected=$1
  attempts=0
  while [ "$attempts" -lt 100 ]
  do
    state=$(window_state || printf '[]')
    if [ "$(printf '%s' "$state" | jq 'length')" -eq "$expected" ]
    then
      printf '%s\n' "$state"
      return 0
    fi
    attempts=$((attempts + 1))
    sleep 0.1
  done
  return 1
}

wait_for_condition ()
{
  expression=$1
  attempts=0
  while [ "$attempts" -lt 100 ]
  do
    state=$(window_state)
    if printf '%s' "$state" | jq -e "$expression" >/dev/null
    then
      printf '%s\n' "$state"
      return 0
    fi
    attempts=$((attempts + 1))
    sleep 0.1
  done
  return 1
}

focus_speaker ()
{
  shell_eval '
    const window = global.get_window_actors()
      .map(actor => actor.get_meta_window())
      .find(candidate => candidate.get_title() === "Pinpoint Speaker View");
    if (!window)
      throw new Error("speaker window not found");
    window.activate(global.get_current_time());
    "focused"' >/dev/null
  sleep 0.2
}

send_key ()
{
  key=$1
  shell_eval "
    const device = Clutter.get_default_backend()
      .get_default_seat()
      .create_virtual_device(Clutter.InputDeviceType.KEYBOARD_DEVICE);
    const time = global.get_current_time();
    device.notify_keyval(time, Clutter.KEY_${key}, Clutter.KeyState.PRESSED);
    device.notify_keyval(time + 1, Clutter.KEY_${key}, Clutter.KeyState.RELEASED);
    'sent'" >/dev/null
}

if ! shell_eval 'String(1 + 1)' >/dev/null
then
  echo "GNOME Shell Eval is disabled." >&2
  echo "Open Looking Glass (Alt+F2, then lg) and run:" >&2
  echo "  global.context.unsafe_mode = true" >&2
  exit 77
fi

monitor_count=$(shell_eval 'String(global.display.get_n_monitors())')
if [ "$monitor_count" -lt 2 ]
then
  echo "Host display automation requires at least two connected monitors." >&2
  exit 77
fi

if [ ! -x "$build_dir/src/pinpoint" ]
then
  echo "Build Pinpoint before running the host display test: $build_dir/src/pinpoint" >&2
  exit 1
fi

sdk_location=$(flatpak info "$installation" --show-location "$sdk_ref")
arch=$(flatpak --default-arch)
sdk_libdir="$sdk_location/files/lib/$arch-linux-gnu"
. "$root/tests/sdk-host-environment.sh"
pinpoint_configure_sdk_host_environment "$sdk_location" "$sdk_libdir"

run_tmp=$(mktemp -d -t pinpoint-host-display-XXXXXX)
pinpoint_pid=
cleanup ()
{
  if [ -n "$pinpoint_pid" ] && kill -0 "$pinpoint_pid" 2>/dev/null
  then
    kill "$pinpoint_pid" 2>/dev/null || true
    wait "$pinpoint_pid" 2>/dev/null || true
  fi
  rm -f -- "$run_tmp/stdout" "$run_tmp/stderr"
  rmdir -- "$run_tmp"
}
trap cleanup EXIT HUP INT TERM

"$build_dir/src/pinpoint" --speakermode --fullscreen "$fixture" \
  >"$run_tmp/stdout" 2>"$run_tmp/stderr" &
pinpoint_pid=$!

if ! initial=$(wait_for_window_count 2)
then
  echo "Pinpoint did not expose both presentation windows." >&2
  cat "$run_tmp/stderr" >&2
  exit 1
fi

if ! printf '%s' "$initial" | jq -e '
  length == 2 and
  all(.[]; .fullscreen == true) and
  ([.[].monitor] | unique | length) == 2 and
  any(.[]; .title == "Pinpoint Speaker View") and
  any(.[]; .title | endswith("— Pinpoint"))' >/dev/null
then
  echo "Initial fullscreen windows are not on distinct displays:" >&2
  printf '%s\n' "$initial" | jq . >&2
  exit 1
fi

audience_before=$(printf '%s' "$initial" | jq -r '.[] | select(.title | endswith("— Pinpoint")) | .monitor')
speaker_before=$(printf '%s' "$initial" | jq -r '.[] | select(.title == "Pinpoint Speaker View") | .monitor')

focus_speaker
send_key s
if ! swapped=$(wait_for_condition \
  "(.[] | select(.title | endswith(\"— Pinpoint\")) | .monitor) == $speaker_before and (.[] | select(.title == \"Pinpoint Speaker View\") | .monitor) == $audience_before")
then
  echo "The Swap Displays action did not exchange monitor assignments." >&2
  window_state | jq . >&2
  exit 1
fi

focus_speaker
send_key F11
wait_for_condition 'all(.[]; .fullscreen == false)' >/dev/null || {
  echo "F11 did not leave fullscreen on both presentation windows." >&2
  exit 1
}

focus_speaker
send_key F11
wait_for_condition '
  all(.[]; .fullscreen == true) and
  ([.[].monitor] | unique | length) == 2' >/dev/null || {
  echo "F11 did not restore two-display fullscreen presentation." >&2
  exit 1
}

printf '%s\n' "$swapped" | jq .
echo "Host display automation passed: distinct fullscreen windows, display swap, and fullscreen restore."
