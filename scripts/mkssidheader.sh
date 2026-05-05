#!/usr/bin/env bash

# 
# to given output file, or stdout if no file given.
# Output format is C #define strings to be included in C source.
#
# #define PICO_WIFI_SSID <ssid>
# #define PICO_WIFI_PASSWORD <password>
#

set -euo pipefail

PROGNAME=$(basename $0)

usage() {
    printf "usage: ${PROGNAME} <ssid> <password> [outfile]\n" 2>&1
    exit 1
}

[[ $# -lt 2 ]] && usage

PICO_SSID="$1"
PICO_PASSWORD="$2"
OUTFILE=""
# save ssid/password as a generated header file
[[ $# -eq 3 ]] && OUTFILE="$3"

MYPID="$$"
TEMP_OUTFILE="/tmp/SSID_${MYPID}_OUT"
{
  echo "#pragma once"
  echo ""
  echo "#define PICO_WIFI_SSID \"${PICO_SSID}\""
  echo "#define PICO_WIFI_PASSWORD \"${PICO_PASSWORD}\""
  echo "#define CY43_COUNTRY_CODE CYW43_COUNTRY_CANADA"
  echo ""
} >${TEMP_OUTFILE}

# Save to OUTFILE if given, or write to stdout otherwise.
if [ -n "${OUTFILE}" ]; then
  OUTDIR=$(dirname ${OUTFILE})
  # create directory in OUTFILE pathname if necessary
  [[ -d "${OUTDIR}" ]] || mkdir -p "${OUTDIR}"
  mv -f "${TEMP_OUTFILE}" "${OUTFILE}" 
else
  cat "${TEMP_OUTFILE}"
  rm -f "${TEMP_OUTFILE}" >/dev/null 2>&1
fi

