#!/usr/bin/env bash
set -euo pipefail

source_path="$(readlink -f -- "${BASH_SOURCE[0]}")"
gui_dir="$(dirname -- "${source_path}")"
board_ip="${1:-${LWIR_BOARD_IP:-192.168.50.2}}"
state_dir="${XDG_STATE_HOME:-${HOME}/.local/state}/lwir-gui"
mkdir -p -- "${state_dir}"

exec 9>"${state_dir}/launcher.lock"
if ! flock -n 9; then
    exit 0
fi

cd -- "${gui_dir}/.."
export PYTHONDONTWRITEBYTECODE=1
export PYTHONPATH="${gui_dir}"
exec python3 "${gui_dir}/desktop_gui.py" --board "${board_ip}" \
    >>"${state_dir}/gui.log" 2>&1
