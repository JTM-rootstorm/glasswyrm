#!/usr/bin/env bash
set -euo pipefail

if (($# != 1)); then
  printf 'Usage: %s OUTPUT_DIRECTORY\n' "$0" >&2
  exit 2
fi

output=$1
archive=$output/SDL2-2.32.10.tar.gz
signature=$archive.sig
key_page=$output/signing-keys.html
key=$output/sdl-signing-key.asc
gnupg=$output/gnupg
archive_url=https://github.com/libsdl-org/SDL/releases/download/release-2.32.10/SDL2-2.32.10.tar.gz
signature_url=$archive_url.sig
key_url=https://www.libsdl.org/signing-keys.php
archive_sha=5f5993c530f084535c65a6879e9b26ad441169b3e25d789d83287040a9ca5165
signature_sha=9533de95863efb5f3fb47fd22adda9be8f1d438b928b1287cea433d4d2ef10ad
fingerprint=1528635D8053A57F77D1E08630A59377A7763BE6

for tool in curl sha256sum gpg awk tr; do
  command -v "$tool" >/dev/null || { printf 'Missing required tool: %s\n' "$tool" >&2; exit 1; }
done
mkdir -p "$output"
curl -L --fail --silent --show-error -o "$archive" "$archive_url"
curl -L --fail --silent --show-error -o "$signature" "$signature_url"
curl -L --fail --silent --show-error -o "$key_page" "$key_url"
printf '%s  %s\n%s  %s\n' "$archive_sha" "$archive" "$signature_sha" "$signature" |
  sha256sum --check --strict

tr -d '\r' <"$key_page" |
  awk '/-----BEGIN PGP PUBLIC KEY BLOCK-----/{copy=1} copy{print} /-----END PGP PUBLIC KEY BLOCK-----/{exit}' >"$key"
grep -q '^-----END PGP PUBLIC KEY BLOCK-----$' "$key"
rm -rf "$gnupg"
mkdir -m 0700 "$gnupg"
GNUPGHOME=$gnupg gpg --batch --quiet --import "$key"
actual=$(GNUPGHOME=$gnupg gpg --batch --with-colons --fingerprint |
  awk -F: '$1=="fpr"{print $10; exit}')
[[ $actual == "$fingerprint" ]] || {
  printf 'SDL signing key fingerprint mismatch: %s\n' "$actual" >&2
  exit 1
}
GNUPGHOME=$gnupg gpg --batch --verify "$signature" "$archive"
printf 'SDL 2.32.10 source and detached signature: verified\n'
