#!/usr/bin/env bash

if [ "$1" == "debug" ]; then
  KERNEL="$(uname -a)"
  case "$KERNEL" in
    Linux*)
      PREFIX="gdb --args"
      ;;
    Darwin*)
      PREFIX="lldb --"
      ;;
  esac
  shift 1
else
	PREFIX=
fi

$PREFIX ./roq-fix-proxy \
  --name "fix-proxy" \
  --config_file config/test.toml \
  --server_target_comp_id "roq-fix-bridge" \
  --server_sender_comp_id "roq-fix-client-test" \
  --server_username "trader" \
  --client_listen_address "$HOME/run/fix-proxy.sock" \
  --client_comp_id "proxy" \
  $@

#  --auth_uri "ws://localhost:1234" \
