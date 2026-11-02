#!/usr/bin/env bash
# Installs build dependencies in Claude Code cloud sessions so Claude does not
# spend turns discovering and installing them. No-op on local machines.
set -euo pipefail
[[ "${CLAUDE_CODE_REMOTE:-}" == "true" ]] || exit 0
if ! dpkg -s libocct-modeling-algorithms-dev >/dev/null 2>&1; then
  export DEBIAN_FRONTEND=noninteractive
  (apt-get update -qq && apt-get install -y -qq \
    libocct-foundation-dev libocct-modeling-data-dev \
    libocct-modeling-algorithms-dev libocct-data-exchange-dev libtbb-dev) >/dev/null 2>&1 \
    || echo "session-start: OCCT install failed; install it manually" >&2
fi
