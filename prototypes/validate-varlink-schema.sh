#!/bin/sh
set -eu

if [ "$#" -ne 1 ]
then
  echo "Usage: $0 /path/to/varlink-codegen" >&2
  exit 2
fi

codegen=$1
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
schema="$root/prototypes/com.nedrichards.Pinpoint.Control.varlink"
run_tmp=$(mktemp -d -t pinpoint-varlink-schema-XXXXXX)

cleanup ()
{
  rm -f -- "$run_tmp/control.h" "$run_tmp/control.c"
  rmdir -- "$run_tmp"
}
trap cleanup EXIT HUP INT TERM

"$codegen" \
  --c-namespace=Pinpoint \
  --interface-prefix=com.nedrichards.Pinpoint. \
  --header="$run_tmp/control.h" \
  --body="$run_tmp/control.c" \
  "$schema"

test -s "$run_tmp/control.h"
test -s "$run_tmp/control.c"
echo "Varlink control schema parsed and generated C bindings."
