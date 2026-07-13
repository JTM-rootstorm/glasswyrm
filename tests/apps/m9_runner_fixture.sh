#!/bin/sh
if [ "${1-}" = "-version" ]; then
  printf '%s\n' 'fixture 1.2.3'
  exit 0
fi
: >"$1"
while :; do sleep 1; done
