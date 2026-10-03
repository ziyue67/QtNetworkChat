#!/usr/bin/env bash
set -euo pipefail

mode="${1:-selfsigned}"
domain="${2:-qt.ziyuexc.top}"
email="${3:-}"
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cert_dir="${script_dir}/nginx/certs"

mkdir -p "$cert_dir"

case "$mode" in
  selfsigned)
    openssl req -x509 -newkey rsa:3072 -nodes \
      -keyout "$cert_dir/privkey.pem" \
      -out "$cert_dir/fullchain.pem" \
      -days 365 \
      -subj "/CN=$domain" \
      -addext "subjectAltName=DNS:$domain"
    ;;
  letsencrypt)
    if [[ -z "$email" ]]; then
      echo "usage: $0 letsencrypt <domain> <email>" >&2
      exit 2
    fi
    if ! command -v certbot >/dev/null 2>&1; then
      echo "certbot is required for letsencrypt mode" >&2
      exit 3
    fi
    certbot certonly --standalone --non-interactive --agree-tos \
      --email "$email" -d "$domain"
    install -m 0644 "/etc/letsencrypt/live/$domain/fullchain.pem" \
      "$cert_dir/fullchain.pem"
    install -m 0600 "/etc/letsencrypt/live/$domain/privkey.pem" \
      "$cert_dir/privkey.pem"
    ;;
  *)
    echo "usage: $0 {selfsigned|letsencrypt} <domain> [email]" >&2
    exit 2
    ;;
esac

chmod 0600 "$cert_dir/privkey.pem"
fingerprint="$(openssl x509 -in "$cert_dir/fullchain.pem" -noout \
  -fingerprint -sha256 | sed 's/^.*=//' | tr -d ':' | tr '[:upper:]' '[:lower:]')"
echo "certificate directory: $cert_dir"
echo "sha256 fingerprint: $fingerprint"
