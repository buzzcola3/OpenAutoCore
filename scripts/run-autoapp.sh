#!/usr/bin/env bash
set -euo pipefail

# Resolve repository root even if the script is invoked via symlink.
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

# Sanitize GTK-related variables that were forcing snap libraries into the process.
unset GTK_PATH
unset GTK_EXE_PREFIX
unset GTK_MODULES

# Compose a safe library path that prioritizes system glibc/pthread first,
# then the project-provided libraries.
SYS_LIBS="/lib/x86_64-linux-gnu:/usr/lib/x86_64-linux-gnu"
LOCAL_LIBS="$REPO_ROOT/lib:$REPO_ROOT/OpenAutoSDK/lib"
export LD_LIBRARY_PATH="$SYS_LIBS:$LOCAL_LIBS"

BIN="$REPO_ROOT/bin/autoapp"
if [[ ! -x "$BIN" ]]; then
  echo "error: $BIN is missing or not executable. Build the project first." >&2
  exit 1
fi

exec "$BIN" "$@"
