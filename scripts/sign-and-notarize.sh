#!/usr/bin/env bash
set -uxo pipefail

###############################################################################
# macOS Code Signing & Notarization for LogosApp.app
#
# Supports modes:
#   --mode sign      - Sign only
#   --mode notarize  - Notarize only (requires pre-signed app)
#   --mode both      - Sign and notarize (default)
#
# Usage:
#   ./scripts/sign-and-notarize.sh [--dmg PATH] [--bundle PATH] [--output PATH] [--mode MODE] [--timeout TIMEOUT]
#
# Required env vars:
#   MACOS_CODESIGN_IDENT       - Developer ID Application identity
#   MACOS_NOTARY_ISSUER_ID     - App Store Connect API issuer ID
#   MACOS_NOTARY_KEY_ID        - App Store Connect API key ID
#   MACOS_NOTARY_KEY_FILE      - Path to .p8 AuthKey file
#   MACOS_KEYCHAIN_PASS        - Password for the .p12 certificate
#   MACOS_KEYCHAIN_FILE        - Path to the .p12 certificate file
###############################################################################

codesign_with_retry() {
    local attempts=3
    local delay=15
    local i=1
    while (( i <= attempts )); do
        if codesign "$@"; then
            return 0
        fi
        echo "  codesign attempt ${i}/${attempts} failed, retrying in ${delay}s..."
        sleep "$delay"
        (( i++ ))
    done
    echo "ERROR: codesign failed after ${attempts} attempts"
    return 1
}

# Parse arguments
DMG_PATH=""
BUNDLE_PATH=""
OUTPUT_PATH=""
MODE="both"
TIMEOUT="30m"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --dmg)
      DMG_PATH="$2"
      shift 2
      ;;
    --bundle)
      BUNDLE_PATH="$2"
      shift 2
      ;;
    --output)
      OUTPUT_PATH="$2"
      shift 2
      ;;
    --mode)
      MODE="$2"
      shift 2
      ;;
    --timeout)
      TIMEOUT="$2"
      shift 2
      ;;
    *)
      echo "Unknown option: $1" >&2
      exit 1
      ;;
  esac
done

# Validate mode
if [[ ! "$MODE" =~ ^(sign|notarize|both)$ ]]; then
  echo "Error: --mode must be 'sign', 'notarize', or 'both'" >&2
  exit 1
fi

# Create temp directory for all operations (needed for Nix read-only store paths)
TEMP_DIR=$(mktemp -d)
trap "rm -rf '${TEMP_DIR}'" EXIT

# Determine APP_BUNDLE path and copy to writable temp directory
if [[ -n "$DMG_PATH" ]]; then
  [[ -f "$DMG_PATH" ]] || { echo "Error: DMG not found: $DMG_PATH" >&2; exit 1; }

  echo "Extracting DMG."
  MOUNT_POINT="${TEMP_DIR}/mnt"
  APP_BUNDLE="${TEMP_DIR}/LogosApp.app"

  mkdir -p "$MOUNT_POINT"
  hdiutil attach -readonly -mountpoint "$MOUNT_POINT" "$DMG_PATH"
  cp -a "$MOUNT_POINT/LogosApp.app" "$APP_BUNDLE"
  hdiutil detach "$MOUNT_POINT"
elif [[ -n "$BUNDLE_PATH" ]]; then
  [[ -d "$BUNDLE_PATH" ]] || { echo "Error: Bundle not found: $BUNDLE_PATH" >&2; exit 1; }

  echo "Copying bundle to temp directory."
  APP_BUNDLE="${TEMP_DIR}/LogosApp.app"
  cp -a "$BUNDLE_PATH" "$APP_BUNDLE"
  chmod -R u+w "$APP_BUNDLE"
else
  # Default: current directory
  [[ -d "./LogosApp.app" ]] || { echo "Error: Bundle not found at ./LogosApp.app" >&2; exit 1; }

  echo "Copying bundle to temp directory."
  APP_BUNDLE="${TEMP_DIR}/LogosApp.app"
  cp -a "./LogosApp.app" "$APP_BUNDLE"
  chmod -R u+w "$APP_BUNDLE"
fi

CONTENTS="${APP_BUNDLE}/Contents"
ENTITLEMENTS="${TEMP_DIR}/entitlements.plist"

# FIX 1: Use a single consistent keychain path throughout.
# create-keychain always appends -db to the filename on disk, so we define
# KEYCHAIN_DB_PATH as the canonical path and use it everywhere.
KEYCHAIN_NAME="build.keychain"
KEYCHAIN_DB_PATH="${HOME}/Library/Keychains/${KEYCHAIN_NAME}-db"

CERT_DIR="/Users/jenkins/certs"

# FIX 3: Add --keychain to CODESIGN_OPTS so every codesign invocation that
# uses this array finds the key without falling back to the default keychain.
CODESIGN_OPTS=(
    --force
    --timestamp
    --options runtime
    --keychain "${KEYCHAIN_DB_PATH}"
    --sign "${MACOS_CODESIGN_IDENT}"
)

###############################################################################
# SIGN MODE
###############################################################################
if [[ "$MODE" =~ ^(sign|both)$ ]]; then
  echo "Starting signing phase."

  ###############################################################################
  # 0. Create entitlements file
  ###############################################################################
  cat > "${ENTITLEMENTS}" <<'EOF'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>com.apple.security.cs.allow-jit</key>
    <true/>
    <key>com.apple.security.cs.allow-unsigned-executable-memory</key>
    <true/>
    <key>com.apple.security.cs.allow-dyld-environment-variables</key>
    <true/>
    <key>com.apple.security.cs.disable-library-validation</key>
    <true/>
</dict>
</plist>
EOF

  ###############################################################################
  # 0.5 Remove all existing signatures (critical!)
  ###############################################################################
  echo "Removing existing signatures..."
  find "${APP_BUNDLE}" -type f -name "_CodeSignature" -exec rm -rf {} + 2>/dev/null || true
  find "${APP_BUNDLE}" -type d -name "_CodeSignature" -exec rm -rf {} + 2>/dev/null || true

  ###############################################################################
  # 1. Set up a temporary keychain and import the certificate
  #
  # FIX 1 (continued): All security commands now use KEYCHAIN_DB_PATH.
  # Previously, create-keychain was called with KEYCHAIN_PATH (no -db suffix)
  # which macOS transparently appended, but subsequent unlock/set-settings
  # calls used the un-suffixed path — unreliable on Sequoia and the root cause
  # of errSecInternalComponent.
  ###############################################################################
  echo "Setting up keychain at ${KEYCHAIN_DB_PATH}"
  security delete-keychain "${KEYCHAIN_DB_PATH}" 2>/dev/null || true
  security create-keychain -p "${MACOS_KEYCHAIN_PASS}" "${KEYCHAIN_DB_PATH}"
  security unlock-keychain -p "${MACOS_KEYCHAIN_PASS}" "${KEYCHAIN_DB_PATH}"
  security set-keychain-settings -lut 21600 "${KEYCHAIN_DB_PATH}"

  ###############################################################################
  # 2. Import Trust Chain
  ###############################################################################
  echo "Importing Apple Trust Chain from local storage..."
  security import "${CERT_DIR}/AppleRootCA-G2.cer"   -k "${KEYCHAIN_DB_PATH}" -t cert
  security import "${CERT_DIR}/AppleWWDRCAG2.cer"    -k "${KEYCHAIN_DB_PATH}" -t cert
  security import "${CERT_DIR}/DeveloperIDG2CA.cer"  -k "${KEYCHAIN_DB_PATH}" -t cert

  echo "Setting trust for CA certs..."
  security add-trusted-cert -d -r trustRoot \
      -k "${KEYCHAIN_DB_PATH}" \
      "${CERT_DIR}/AppleRootCA-G2.cer"

  security add-trusted-cert -d -r trustAsRoot \
      -k "${KEYCHAIN_DB_PATH}" \
      "${CERT_DIR}/AppleWWDRCAG2.cer"

  security add-trusted-cert -d -r trustAsRoot \
      -k "${KEYCHAIN_DB_PATH}" \
      "${CERT_DIR}/DeveloperIDG2CA.cer"

  # Ensure the system looks at our build keychain first
  security list-keychains -d user -s "${KEYCHAIN_DB_PATH}" /Library/Keychains/System.keychain

  ###############################################################################
  # 3. Import Identity and Set Permissions
  ###############################################################################
  echo "Extracting identity from P12..."
  openssl pkcs12 -in "${MACOS_KEYCHAIN_FILE}" \
      -passin pass:"${MACOS_KEYCHAIN_PASS}" \
      -nodes -out /tmp/cert_and_key.pem

  echo "Importing identity from PEM..."
  security import /tmp/cert_and_key.pem \
      -k "${KEYCHAIN_DB_PATH}" \
      -T /usr/bin/codesign

  # Cleanup PEM immediately
  rm -f /tmp/cert_and_key.pem

  # Allow codesign to access the key without UI prompts (required on Sequoia)
  security set-key-partition-list \
      -S apple-tool:,apple:,codesign: \
      -s -k "${MACOS_KEYCHAIN_PASS}" \
      "${KEYCHAIN_DB_PATH}"

  # FIX 4: Debug commands now use KEYCHAIN_DB_PATH, not the bare name string.
  echo "Debug: Keychain contents"
  echo "Available identities in ${KEYCHAIN_DB_PATH}:"
  security find-identity -v "${KEYCHAIN_DB_PATH}" || echo "No identities found"

  echo "Debug: All codesigning identities in all keychains"
  security find-identity -v -p codesigning || echo "No codesigning identities found"

  echo "Debug: Dump of ${KEYCHAIN_DB_PATH}"
  security dump-keychain "${KEYCHAIN_DB_PATH}" 2>&1 | head -50 || true

  # FIX 7: Re-unlock the keychain immediately before signing. Signing loops can
  # take several minutes; an expired session lock causes errSecInternalComponent
  # mid-run. Belt-and-suspenders given the -lut 21600 setting above.
  echo "Re-unlocking keychain before signing..."
  security unlock-keychain -p "${MACOS_KEYCHAIN_PASS}" "${KEYCHAIN_DB_PATH}"

  ###############################################################################
  # 4. Sign + repack .lgx archives in Contents/preinstall/
  #
  # FIX 2: Switched from `find -exec codesign \;` to a while-read loop so that
  # individual codesign failures are caught and abort the build. Also added
  # --keychain so the key is found without ambiguity.
  ###############################################################################
  echo "Signing dylibs inside .lgx archives."
  find "${CONTENTS}/preinstall" -name "*.lgx" 2>/dev/null | while read -r lgx; do
      echo "  Processing: ${lgx}"
      tmpdir=$(mktemp -d)
      gtar -xzf "$lgx" -C "$tmpdir"

      while IFS= read -r dylib; do
          [[ -n "$dylib" ]] || continue
          echo "    Signing: ${dylib}"
          codesign_with_retry --force --options runtime \
              --keychain "${KEYCHAIN_DB_PATH}" \
              --sign "${MACOS_CODESIGN_IDENT}" \
              --timestamp \
              "$dylib" \
              || { echo "ERROR: failed to sign ${dylib}"; rm -rf "$tmpdir"; exit 1; }
      done < <(find "$tmpdir" -name "*.dylib")

      gtar -czf "$lgx" -C "$tmpdir" --transform 's|^\./||' .
      rm -rf "$tmpdir"
      echo "  Repacked: ${lgx}"
  done

  ###############################################################################
  # 5. Sign all dylibs in Frameworks/
  ###############################################################################
  echo "Signing dylibs in Frameworks."
  while IFS= read -r dylib; do
      [[ -n "$dylib" ]] || continue
      echo "  Signing: ${dylib}"
      codesign_with_retry "${CODESIGN_OPTS[@]}" "$dylib" \
          || { echo "ERROR: failed to sign ${dylib}"; exit 1; }
  done < <(find "${CONTENTS}/Frameworks" -name '*.dylib' -type f)

  ###############################################################################
  # 6. Sign all dylibs in Resources/qt/
  ###############################################################################
  echo "Signing dylibs in Resources/qt."
  while IFS= read -r dylib; do
      [[ -n "$dylib" ]] || continue
      echo "  Signing: ${dylib}"
      codesign_with_retry "${CODESIGN_OPTS[@]}" "$dylib" \
          || { echo "ERROR: failed to sign ${dylib}"; exit 1; }
  done < <(find "${CONTENTS}/Resources/qt" -type f -name '*.dylib' 2>/dev/null)

  ###############################################################################
  # 7. Sign Qt frameworks (binary inside Versions/A/ then the .framework dir)
  ###############################################################################
  echo "Signing Qt frameworks."
  find "${CONTENTS}/Frameworks" -name '*.framework' -type d -maxdepth 1 2>/dev/null | sort | while read -r fw; do
      fw_name=$(basename "${fw}" .framework)
      fw_binary="${fw}/Versions/A/${fw_name}"

      if [[ -f "${fw_binary}" ]]; then
          echo "  Signing framework binary: ${fw_binary}"
          codesign_with_retry "${CODESIGN_OPTS[@]}" "${fw_binary}" \
              || { echo "ERROR: failed to sign ${fw_binary}"; exit 1; }
      fi
  done

  ###############################################################################
  # 8. Sign executables in Contents/MacOS/
  ###############################################################################
  echo "Signing executables."
  for exe in "${CONTENTS}/MacOS/LogosApp.bin" \
             "${CONTENTS}/MacOS/logos-app" \
             "${CONTENTS}/MacOS/logos_host" \
             "${CONTENTS}/MacOS/logoscore"; do
      if [[ -f "${exe}" ]]; then
          echo "  Signing: ${exe}"
          codesign_with_retry "${CODESIGN_OPTS[@]}" --entitlements "${ENTITLEMENTS}" "${exe}" \
              || { echo "ERROR: failed to sign ${exe}"; exit 1; }
      fi
  done

  # Sign the shell script wrapper last (no --options runtime for scripts)
  if [[ -f "${CONTENTS}/MacOS/LogosApp" ]]; then
    echo "  Signing wrapper: ${CONTENTS}/MacOS/LogosApp"
    codesign_with_retry --force --timestamp \
        --keychain "${KEYCHAIN_DB_PATH}" \
        --sign "${MACOS_CODESIGN_IDENT}" \
        "${CONTENTS}/MacOS/LogosApp" \
        || { echo "ERROR: failed to sign wrapper"; exit 1; }
  fi

  ###############################################################################
  # 9. Sign the top-level app bundle
  ###############################################################################
  echo "Signing app bundle."
  codesign_with_retry "${CODESIGN_OPTS[@]}" --entitlements "${ENTITLEMENTS}" "${APP_BUNDLE}" \
      || { echo "ERROR: failed to sign app bundle"; exit 1; }

  ###############################################################################
  # 10. Verify
  #
  # FIX 6: Both checks are now hard failures. If either fails the script aborts
  # before wasting a notarization submission on a broken bundle.
  ###############################################################################
  echo "Verifying signature."
  codesign --verify --deep --strict --verbose=2 "${APP_BUNDLE}" \
      || { echo "ERROR: Signature verification failed, aborting before notarization."; exit 1; }

  echo "Checking Gatekeeper assessment."
  spctl --assess --type execute --verbose=2 "${APP_BUNDLE}" \
      || { echo "ERROR: Gatekeeper rejected the app, aborting before notarization."; exit 1; }

  echo "Signing phase complete"
fi

###############################################################################
# NOTARIZE MODE
###############################################################################
if [[ "$MODE" =~ ^(notarize|both)$ ]]; then
  echo "Starting notarization phase."

  ###############################################################################
  # 11. Create ZIP for notarization
  ###############################################################################
  echo "Creating ZIP for notarization."
  NOTARIZE_ZIP="${TEMP_DIR}/LogosApp-$$.zip"
  ditto -c -k --keepParent "${APP_BUNDLE}" "${NOTARIZE_ZIP}"

  ###############################################################################
  # 12. Submit for notarization
  ###############################################################################
  echo "Submitting for notarization."
  xcrun notarytool submit "${NOTARIZE_ZIP}" \
      --issuer "${MACOS_NOTARY_ISSUER_ID}" \
      --key-id "${MACOS_NOTARY_KEY_ID}" \
      --key "${MACOS_NOTARY_KEY_FILE}" \
      --wait \
      --timeout "${TIMEOUT}" \
      || { echo "ERROR: notarytool submission failed or returned Invalid."; exit 1; }

  ###############################################################################
  # 13. Staple the notarization ticket
  ###############################################################################
  echo "Stapling notarization ticket."
  xcrun stapler staple "${APP_BUNDLE}" \
      || { echo "ERROR: stapler failed."; exit 1; }

  ###############################################################################
  # 14. Final verification
  ###############################################################################
  echo "Final verification."
  codesign --verify --deep --strict --verbose=2 "${APP_BUNDLE}" \
      || { echo "ERROR: Final signature verification failed."; exit 1; }
  spctl --assess --type execute --verbose=2 "${APP_BUNDLE}" \
      || { echo "ERROR: Final Gatekeeper check failed."; exit 1; }

  # Cleanup ZIP, entitlements, and keychain
  rm -f "${NOTARIZE_ZIP}"
  rm -f "${ENTITLEMENTS}"
  # FIX 5: delete-keychain now uses KEYCHAIN_DB_PATH, not the bare name string.
  security delete-keychain "${KEYCHAIN_DB_PATH}" 2>/dev/null || true

  echo "Notarization phase complete"
fi

###############################################################################
# OUTPUT
###############################################################################
if [[ -n "$OUTPUT_PATH" ]]; then
  echo "Creating output DMG."
  mkdir -p "$(dirname "$OUTPUT_PATH")"
  hdiutil create -volname "LogosApp" \
                 -srcfolder "${APP_BUNDLE}" \
                 -ov -format UDZO \
                 -puppetstrings \
                 "${OUTPUT_PATH}"
  echo "Output DMG created: $OUTPUT_PATH"
else
  echo "Done! Bundle signed/notarized: ${APP_BUNDLE}"
fi