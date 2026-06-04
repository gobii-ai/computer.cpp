#!/usr/bin/env bash
set -euo pipefail

identity_name="${1:-ComputerCpp Local Code Signing}"
work_dir="$(mktemp -d "${TMPDIR:-/tmp}/computer.cpp-codesign.XXXXXX")"
trap 'rm -rf "$work_dir"' EXIT

keychain="${HOME}/Library/Keychains/login.keychain-db"
cert_pem="${work_dir}/cert.pem"
key_pem="${work_dir}/key.pem"
p12="${work_dir}/identity.p12"
openssl_config="${work_dir}/openssl.cnf"
p12_password="computer.cpp-local"

if security find-identity -v -p codesigning 2>/dev/null | grep -F "\"${identity_name}\"" >/dev/null; then
    echo "Code-signing identity already exists: ${identity_name}"
    exit 0
fi

cat >"${openssl_config}" <<EOF
[ req ]
default_bits = 2048
prompt = no
default_md = sha256
distinguished_name = dn
x509_extensions = codesign_ext

[ dn ]
CN = ${identity_name}

[ codesign_ext ]
basicConstraints = critical, CA:false
keyUsage = critical, digitalSignature
extendedKeyUsage = codeSigning
subjectKeyIdentifier = hash
EOF

openssl req \
    -new \
    -newkey rsa:2048 \
    -nodes \
    -x509 \
    -days 3650 \
    -config "${openssl_config}" \
    -keyout "${key_pem}" \
    -out "${cert_pem}" >/dev/null 2>&1

openssl pkcs12 \
    -export \
    -legacy \
    -name "${identity_name}" \
    -inkey "${key_pem}" \
    -in "${cert_pem}" \
    -out "${p12}" \
    -passout "pass:${p12_password}" >/dev/null 2>&1

security import "${p12}" \
    -k "${keychain}" \
    -P "${p12_password}" \
    -T /usr/bin/codesign \
    -T /usr/bin/security >/dev/null

security add-trusted-cert \
    -r trustRoot \
    -p codeSign \
    -k "${keychain}" \
    "${cert_pem}" >/dev/null

echo "Created local code-signing identity: ${identity_name}"
echo "Use with: cmake -S . -B build/debug -DCOMPUTER_CPP_CODE_SIGN_IDENTITY=\"${identity_name}\""
