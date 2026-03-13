# Smoke-tests the logos-app binary.
# Launches the app with -platform offscreen and fails if:
#   - the app emits critical QML errors (engine failure, missing module, etc.)
#   - the app crashes before the timeout (non-zero exit that isn't timeout's 124)
#
# If the app crashes it exits immediately — the timeout is only ever waited out
# on the happy path (app stays alive and healthy).
#
# The logos-app launcher script (bin/logos-app) already bakes in the correct
# QT_PLUGIN_PATH and LD_LIBRARY_PATH at build time, so we only need to add
# the offscreen platform plugin and GL stubs on Linux.
{ pkgs, appPkg, timeoutSec ? 5
}:

pkgs.runCommand "logos-app-smoke-test" {
  nativeBuildInputs = [ pkgs.coreutils ]
    ++ pkgs.lib.optionals pkgs.stdenv.isLinux [
      pkgs.qt6.qtbase   # provides the offscreen platform plugin
      pkgs.libGL
      pkgs.libglvnd
    ];
} ''
  export QT_QPA_PLATFORM=offscreen
  export HOME="$TMPDIR/home"
  mkdir -p "$HOME"

  ${pkgs.lib.optionalString pkgs.stdenv.isLinux ''
    export QT_PLUGIN_PATH="${pkgs.qt6.qtbase}/${pkgs.qt6.qtbase.qtPluginPrefix}"
    export LD_LIBRARY_PATH="${pkgs.libGL}/lib:${pkgs.libglvnd}/lib''${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
  ''}

  mkdir -p $out
  LOG=$out/smoke-test.log

  echo "Running logos-app smoke test (timeout: ${toString timeoutSec}s)..."
  set +e
  timeout ${toString timeoutSec} ${appBin} -platform offscreen > "$LOG" 2>&1
  CODE=$?
  set -e

  cat "$LOG"

  if grep -qE "QQmlApplicationEngine failed|module.*is not installed|Cannot assign|failed to load component" "$LOG"; then
    echo "Critical QML errors detected"
    exit 1
  fi

  # timeout returns 124 when it kills the process (expected — app runs an event loop)
  if [ "$CODE" -ne 0 ] && [ "$CODE" -ne 124 ]; then
    echo "App crashed with exit code $CODE"
    exit 1
  fi

  echo "Smoke test passed (exit code: $CODE)"
''
