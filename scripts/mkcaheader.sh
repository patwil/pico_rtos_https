#!/usr/bin/env bash

# Downloads certificates from given URL and
# writes first root (i.e. non-leaf) certificate
# to given output file, or stdout if no file given.
# Output format is C #define strings to be included in C source.
#
# #define HTTPS_HOST <url>
# #define HTTPS_CA_CERT ...
#

set -euo pipefail

PROGNAME=$(basename $0)

usage() {
    printf "usage: ${PROGNAME} <url> [outfile]\n" 2>&1
    exit 1
}

[[ $# -eq 0 ]] && usage

URL="$1"
OUTFILE=""
# save certificate as a generated header file
[[ $# -eq 2 ]] && OUTFILE="$2"

MYPID="$$"
TEMP_CERT_ROOT=""/tmp/${URL}_${MYPID}_cert-""
TEMP_OUTFILE="/tmp/${URL}_${MYPID}_OUT"

# Fetch the full presented chain and
# save each certificate in separate temp file
openssl s_client -connect ${URL}:443 -servername ${URL} -showcerts </dev/null 2>/dev/null \
  | awk '/BEGIN CERTIFICATE/,/END CERTIFICATE/ {print}' |
csplit -q -f ${TEMP_CERT_ROOT} - '/-----BEGIN CERTIFICATE-----/' '{*}'

# find the first root certificate
for f in ${TEMP_CERT_ROOT}*; do
  # check if it's a real certificate
  openssl x509 -in "$f" -noout -subject || continue
  # ignore certificate if subject is same as URL
  openssl x509 -in "$f" -noout -subject | grep "${URL}" && continue
  break
done >/dev/null 2>&1
CERT="$f"

# save certificate as file to stdout
{
  echo "#pragma once"
  echo ""
  echo "#define HTTPS_HOST \"${URL}\""
  echo ""
  echo "#define HTTPS_CA_CERT \\"
  # Escape backslashes/quotes and add \n line endings
  sed -e 's/\\/\\\\/g' -e 's/"/\\"/g' \
      -e '/END CERTIFICATE/! s/$/\\n\\/' \
      -e '1s/^/"/' \
      -e '/END CERTIFICATE/ s/$/\"/' "$CERT"
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

/bin/rm -f ${TEMP_CERT_ROOT}* >/dev/null 2>&1

