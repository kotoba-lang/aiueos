#!/bin/sh
set -eu

case "${1:-status}" in
  status) command_text='runtime status' ;;
  restart) command_text='runtime restart' ;;
  *)
    echo "usage: k16-runtime.sh [status|restart]" >&2
    exit 2
    ;;
esac

key_dir=${AIUEOS_K16_SSH_KEY_DIR:-"$HOME/Library/Application Support/AIUEOS/K16 SSH"}
private_key="$key_dir/management-p256.pem"
known_hosts="$key_dir/known_hosts"
[ -f "$private_key" ] || {
  echo "error: missing K16 management key: $private_key" >&2
  exit 3
}

exec ssh \
  -i "$private_key" \
  -o BatchMode=yes \
  -o PasswordAuthentication=no \
  -o KbdInteractiveAuthentication=no \
  -o PreferredAuthentications=publickey \
  -o IdentitiesOnly=yes \
  -o StrictHostKeyChecking=accept-new \
  -o "UserKnownHostsFile=$known_hosts" \
  -o KexAlgorithms=curve25519-sha256 \
  -o HostKeyAlgorithms=ecdsa-sha2-nistp256 \
  -o Ciphers=aes128-gcm@openssh.com \
  runtime@10.77.0.10 "$command_text"
