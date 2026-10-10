#!/usr/bin/env bash
# Accord - the certificates the TLS tests run against (T3.1).
#
# The files in tests/fixtures/tls/ are committed, so this script is not part of
# the build: it is here to say where they came from and to make them again if
# they ever expire or a case needs another name. Nothing in them is a secret --
# a key that signs nothing but a test server on loopback is a fixture, and having
# it in the repository is what lets the tests run with no setup step.
#
#   tools/make-test-certs.sh [openssl]
#
# The samples are ECDSA P-256 and last twenty years, so the fixtures do not
# quietly expire between runs.

set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
openssl="${1:-openssl}"
out="$root/tests/fixtures/tls"

command -v "$openssl" >/dev/null || {
    printf 'make-test-certs: no openssl at %s\n' "$openssl" >&2
    exit 2
}

mkdir -p -- "$out"
cd -- "$out"

files=(ca.key ca.pem ca.srl server-localhost.key server-localhost.csr server-localhost.crt
    server-selfsigned.key server-selfsigned.csr server-selfsigned.crt)
rm -f -- "${files[@]}"

days=7300

# The CA. This is the file the client is handed as its trust store, so a server
# certificate signed by it is the one that must be accepted.
"$openssl" req -x509 -newkey ec -pkeyopt ec_paramgen_curve:P-256 -nodes \
    -keyout ca.key -out ca.pem -days "$days" -subj "/CN=Accord test CA" \
    -addext "basicConstraints=critical,CA:TRUE" \
    -addext "keyUsage=critical,keyCertSign,cRLSign"

# The server certificate. The name the client checks lives in the SAN, which is
# where verification looks; the CN would not be enough. 127.0.0.1 is in there as
# well, so a test may connect by address and be right about it.
cat >localhost.ext <<'EOF'
basicConstraints=critical,CA:FALSE
keyUsage=critical,digitalSignature
extendedKeyUsage=serverAuth
subjectAltName=DNS:localhost,IP:127.0.0.1
EOF
"$openssl" req -newkey ec -pkeyopt ec_paramgen_curve:P-256 -nodes \
    -keyout server-localhost.key -out server-localhost.csr -subj "/CN=localhost"
"$openssl" x509 -req -in server-localhost.csr -CA ca.pem -CAkey ca.key -CAcreateserial \
    -out server-localhost.crt -days "$days" -extfile localhost.ext

# And one that no client in the tests trusts: same name, same everything a
# verifier looks at, except that it signs itself. It is what the "another CA"
# case serves, so the only reason to refuse it is the chain.
cat >selfsigned.ext <<'EOF'
basicConstraints=critical,CA:FALSE
keyUsage=critical,digitalSignature
extendedKeyUsage=serverAuth
subjectAltName=DNS:localhost,IP:127.0.0.1
EOF
"$openssl" req -x509 -newkey ec -pkeyopt ec_paramgen_curve:P-256 -nodes \
    -keyout server-selfsigned.key -out server-selfsigned.crt -days "$days" \
    -subj "/CN=localhost" -addext "basicConstraints=critical,CA:FALSE" \
    -addext "keyUsage=critical,digitalSignature" \
    -addext "extendedKeyUsage=serverAuth" \
    -addext "subjectAltName=DNS:localhost,IP:127.0.0.1"

rm -f -- ca.srl localhost.ext selfsigned.ext server-localhost.csr server-selfsigned.csr

printf '==> [test-certs] %s\n' "$out"
"$openssl" verify -CAfile ca.pem server-localhost.crt
ls -l ca.pem server-localhost.crt server-localhost.key server-selfsigned.crt server-selfsigned.key
