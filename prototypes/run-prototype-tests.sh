#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build_dir=${PINPOINT_PROTOTYPE_BUILD_DIR:-"$root/_build-prototypes"}
prototype="$build_dir/prototypes/pinpoint-remote-prototype"
sdk_ref=${PINPOINT_SDK_REF:-org.gnome.Sdk//50}
installation=--user

if ! flatpak info --user "$sdk_ref" >/dev/null 2>&1
then
  installation=--system
fi
if [ ! -x "$prototype" ]
then
  echo "Configure with -Dremote_prototypes=true and build first: $prototype" >&2
  exit 1
fi

sdk_location=$(flatpak info "$installation" --show-location "$sdk_ref")
arch=$(flatpak --default-arch)
sdk_libdir="$sdk_location/files/lib/$arch-linux-gnu"
. "$root/tests/sdk-host-environment.sh"
pinpoint_configure_sdk_host_environment "$sdk_location" "$sdk_libdir"

run_tmp=$(mktemp -d -t pinpoint-remote-prototype-XXXXXX)
first_pid=
second_pid=
cleanup ()
{
  for pid in "$first_pid" "$second_pid"
  do
    if [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null
    then
      kill "$pid" 2>/dev/null || true
      wait "$pid" 2>/dev/null || true
    fi
  done
  rm -f -- "$run_tmp/first.out" "$run_tmp/first.err" \
    "$run_tmp/second.out" "$run_tmp/second.err"
  rmdir -- "$run_tmp"
}
trap cleanup EXIT HUP INT TERM

wait_for_value ()
{
  file=$1
  key=$2
  attempts=0
  while [ "$attempts" -lt 100 ]
  do
    value=$(sed -n "s/^$key=//p" "$file" | tail -n 1)
    if [ -n "$value" ]
    then
      printf '%s\n' "$value"
      return 0
    fi
    attempts=$((attempts + 1))
    sleep 0.1
  done
  return 1
}

"$prototype" --dbus --mpris --slides=3 \
  >"$run_tmp/first.out" 2>"$run_tmp/first.err" &
first_pid=$!
if ! dbus_name=$(wait_for_value "$run_tmp/first.out" DBUS_NAME)
then
  echo "Prototype did not export its D-Bus name." >&2
  cat "$run_tmp/first.err" >&2
  exit 1
fi
if ! mpris_name=$(wait_for_value "$run_tmp/first.out" MPRIS_NAME)
then
  echo "Prototype did not export its MPRIS name." >&2
  cat "$run_tmp/first.err" >&2
  exit 1
fi

gdbus introspect --session \
  --dest "$dbus_name" \
  --object-path /com/nedrichards/pinpoint/Control |
  awk '/org.gtk.Actions/ { found = 1 } END { exit found ? 0 : 1 }'
gdbus call --session \
  --dest "$dbus_name" \
  --object-path /com/nedrichards/pinpoint/Control \
  --method org.gtk.Actions.Activate \
  presentation-next '[]' '{}' >/dev/null

gdbus introspect --session \
  --dest "$mpris_name" \
  --object-path /org/mpris/MediaPlayer2 |
  awk '/org.mpris.MediaPlayer2.Player/ { found = 1 } END { exit found ? 0 : 1 }'
playback=$(gdbus call --session \
  --dest "$mpris_name" \
  --object-path /org/mpris/MediaPlayer2 \
  --method org.freedesktop.DBus.Properties.Get \
  org.mpris.MediaPlayer2.Player PlaybackStatus)
can_pause=$(gdbus call --session \
  --dest "$mpris_name" \
  --object-path /org/mpris/MediaPlayer2 \
  --method org.freedesktop.DBus.Properties.Get \
  org.mpris.MediaPlayer2.Player CanPause)
case "$playback" in
  *"'Stopped'"*) ;;
  *) echo "MPRIS prototype reported misleading playback state: $playback" >&2; exit 1 ;;
esac
case "$can_pause" in
  *false*) ;;
  *) echo "MPRIS prototype unexpectedly advertised pause: $can_pause" >&2; exit 1 ;;
esac

gdbus call --session \
  --dest "$mpris_name" \
  --object-path /org/mpris/MediaPlayer2 \
  --method org.mpris.MediaPlayer2.Player.Next >/dev/null
if gdbus call --session \
  --dest "$mpris_name" \
  --object-path /org/mpris/MediaPlayer2 \
  --method org.mpris.MediaPlayer2.Player.PlayPause >/dev/null 2>&1
then
  echo "MPRIS prototype accepted PlayPause without truthful timer semantics." >&2
  exit 1
fi

attempts=0
while [ "$attempts" -lt 100 ]
do
  if awk '/COMMAND=next SLIDE=3\/3/ { found = 1 } END { exit found ? 0 : 1 }' \
    "$run_tmp/first.out"
  then
    break
  fi
  attempts=$((attempts + 1))
  sleep 0.1
done
if [ "$attempts" -eq 100 ]
then
  echo "D-Bus and MPRIS calls did not reach the shared control model." >&2
  cat "$run_tmp/first.out" >&2
  cat "$run_tmp/first.err" >&2
  exit 1
fi

# A client may call Next even after observing CanGoNext=false. The adapter must
# not leak the internal final-slide action used to complete rehearsal timing.
gdbus call --session \
  --dest "$mpris_name" \
  --object-path /org/mpris/MediaPlayer2 \
  --method org.mpris.MediaPlayer2.Player.Next >/dev/null
if [ "$(awk '/COMMAND=next SLIDE=3\/3/ { count++ } END { print count + 0 }' \
  "$run_tmp/first.out")" -ne 1 ]
then
  echo "MPRIS advanced after CanGoNext became false." >&2
  exit 1
fi

"$prototype" --mpris --slides=2 \
  >"$run_tmp/second.out" 2>"$run_tmp/second.err" &
second_pid=$!
if ! second_mpris_name=$(wait_for_value "$run_tmp/second.out" MPRIS_NAME)
then
  echo "Second prototype did not export its MPRIS name." >&2
  cat "$run_tmp/second.err" >&2
  exit 1
fi
if [ "$mpris_name" = "$second_mpris_name" ]
then
  echo "Simultaneous prototypes did not receive distinct MPRIS names." >&2
  exit 1
fi
gdbus introspect --session \
  --dest "$mpris_name" \
  --object-path /org/mpris/MediaPlayer2 >/dev/null
gdbus introspect --session \
  --dest "$second_mpris_name" \
  --object-path /org/mpris/MediaPlayer2 >/dev/null

printf 'D-Bus adapter: %s\n' "$dbus_name"
printf 'MPRIS adapters: %s and %s\n' "$mpris_name" "$second_mpris_name"
echo "Remote prototypes passed: shared actions, conservative MPRIS semantics, and independent instances."
