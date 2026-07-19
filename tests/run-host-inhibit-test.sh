#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build_dir=${PINPOINT_BUILD_DIR:-"$root/_build"}
fixture="$root/tests/fixtures/multi-monitor.pin"
sdk_ref=${PINPOINT_SDK_REF:-org.gnome.Sdk//50}
flatpak_id=${PINPOINT_FLATPAK_ID:-}
installation=--user

if ! flatpak info --user "$sdk_ref" >/dev/null 2>&1
then
  installation=--system
fi

if [ -z "$flatpak_id" ] && [ ! -x "$build_dir/src/pinpoint" ]
then
  echo "Build Pinpoint before running the host inhibit test: $build_dir/src/pinpoint" >&2
  exit 1
fi

if ! gdbus introspect --session \
  --dest org.gnome.SessionManager \
  --object-path /org/gnome/SessionManager >/dev/null 2>&1
then
  echo "GNOME SessionManager is unavailable; host inhibit validation skipped." >&2
  exit 77
fi

if [ -z "$flatpak_id" ]
then
  sdk_location=$(flatpak info "$installation" --show-location "$sdk_ref")
  arch=$(flatpak --default-arch)
  sdk_libdir="$sdk_location/files/lib/$arch-linux-gnu"
  . "$root/tests/sdk-host-environment.sh"
  pinpoint_configure_sdk_host_environment "$sdk_location" "$sdk_libdir"
elif ! flatpak info --user "$flatpak_id" >/dev/null 2>&1
then
  echo "Install the requested Flatpak before running the inhibit test: $flatpak_id" >&2
  exit 1
elif flatpak ps --columns=application |
     awk -v expected="$flatpak_id" '$0 == expected { found = 1 } END { exit found ? 0 : 1 }'
then
  echo "Close the existing $flatpak_id instance before running the inhibit test." >&2
  exit 1
fi

list_inhibitors ()
{
  gdbus call --session \
    --dest org.gnome.SessionManager \
    --object-path /org/gnome/SessionManager \
    --method org.gnome.SessionManager.GetInhibitors |
    awk -F"'" '{ for (i = 2; i <= NF; i += 2) if ($i ~ /^\/org\/gnome\/SessionManager\/Inhibitor/) print $i }'
}

inhibitor_string ()
{
  path=$1
  method=$2
  gdbus call --session \
    --dest org.gnome.SessionManager \
    --object-path "$path" \
    --method "org.gnome.SessionManager.Inhibitor.$method" |
    awk -F"'" '{ print $2 }'
}

inhibitor_flags ()
{
  gdbus call --session \
    --dest org.gnome.SessionManager \
    --object-path "$1" \
    --method org.gnome.SessionManager.Inhibitor.GetFlags |
    sed -E 's/.*uint32 ([0-9]+).*/\1/'
}

new_inhibitors ()
{
  for path in $(list_inhibitors)
  do
    case "
$baseline_inhibitors
" in
      *"
$path
"*) ;;
      *) printf '%s\n' "$path" ;;
    esac
  done
}

dump_inhibitors ()
{
  for path in $(list_inhibitors)
  do
    printf '%s app=%s reason=%s flags=%s\n' \
      "$path" \
      "$(inhibitor_string "$path" GetAppId)" \
      "$(inhibitor_string "$path" GetReason)" \
      "$(inhibitor_flags "$path")"
  done
}

wait_for_new_count ()
{
  expected=$1
  attempts=0
  while [ "$attempts" -lt 100 ]
  do
    matches=$(new_inhibitors)
    count=$(printf '%s\n' "$matches" | awk 'NF { count++ } END { print count + 0 }')
    if [ "$count" -eq "$expected" ]
    then
      printf '%s\n' "$matches"
      return 0
    fi
    attempts=$((attempts + 1))
    sleep 0.1
  done
  return 1
}

wait_for_path_absent ()
{
  path=$1
  attempts=0
  while [ "$attempts" -lt 100 ]
  do
    if ! list_inhibitors | awk -v expected="$path" '$0 == expected { found = 1 } END { exit found ? 0 : 1 }'
    then
      return 0
    fi
    attempts=$((attempts + 1))
    sleep 0.1
  done
  return 1
}

run_tmp=$(mktemp -d -t pinpoint-host-inhibit-XXXXXX)
pinpoint_pid=
stop_pinpoint ()
{
  if [ -n "$flatpak_id" ]
  then
    flatpak kill "$flatpak_id" 2>/dev/null || true
  elif [ -n "$pinpoint_pid" ] && kill -0 "$pinpoint_pid" 2>/dev/null
  then
    kill "$pinpoint_pid" 2>/dev/null || true
  fi
  if [ -n "$pinpoint_pid" ]
  then
    wait "$pinpoint_pid" 2>/dev/null || true
    pinpoint_pid=
  fi
}

cleanup ()
{
  stop_pinpoint
  rm -f -- "$run_tmp/stdout" "$run_tmp/stderr"
  rmdir -- "$run_tmp"
}
trap cleanup EXIT HUP INT TERM

run_case ()
{
  mode=$1
  shift
  baseline_inhibitors=$(list_inhibitors)
  if [ -n "$flatpak_id" ]
  then
    flatpak run --user "--filesystem=$root:ro" "$flatpak_id" "$@" "$fixture" \
      >"$run_tmp/stdout" 2>"$run_tmp/stderr" &
  else
    "$build_dir/src/pinpoint" "$@" "$fixture" \
      >"$run_tmp/stdout" 2>"$run_tmp/stderr" &
  fi
  pinpoint_pid=$!

  if ! match=$(wait_for_new_count 1)
  then
    echo "Pinpoint did not acquire exactly one idle inhibitor in $mode mode." >&2
    dump_inhibitors >&2
    systemd-inhibit --list >&2 || true
    cat "$run_tmp/stderr" >&2
    exit 1
  fi
  app_id=$(inhibitor_string "$match" GetAppId)
  reason=$(inhibitor_string "$match" GetReason)
  flags=$(inhibitor_flags "$match")
  if [ -z "$app_id" ] || [ "$flags" -ne 8 ] ||
     { [ "$reason" != "Presenting slides" ] &&
       { [ "$app_id" != "mutter" ] || [ "$reason" != "idle-inhibit" ]; }; }
  then
    echo "Unexpected inhibitor in $mode mode: app=$app_id reason=$reason flags=$flags" >&2
    exit 1
  fi

  sleep 0.5
  wait_for_new_count 1 >/dev/null || {
    echo "Pinpoint duplicated or lost its inhibitor in $mode mode." >&2
    exit 1
  }
  stop_pinpoint
  wait_for_path_absent "$match" || {
    echo "Pinpoint did not release its inhibitor after leaving $mode mode." >&2
    exit 1
  }
  printf '%s mode: app=%s reason=%s flags=%s\n' "$mode" "$app_id" "$reason" "$flags"
}

run_case windowed
run_case fullscreen --fullscreen
echo "Host inhibit automation passed: presentation display mode owns one idle inhibitor and releases it on exit."
