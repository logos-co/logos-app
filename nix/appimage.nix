# Builds a Linux AppImage for LogosBasecamp
{ pkgs, app, version, src }:

assert pkgs.stdenv.isLinux;

let
  # Select the AppImage runtime matching the build platform architecture.
  # Using the wrong runtime (e.g. aarch64 runtime on x86_64) produces an
  # AppImage whose ELF header is for the wrong ISA — it will silently fail
  # to launch with no clear error message.
  appimageRuntime =
    if pkgs.stdenv.hostPlatform.system == "aarch64-linux" then
      pkgs.fetchurl {
        url = "https://github.com/AppImage/type2-runtime/releases/download/continuous/runtime-aarch64";
        hash = "sha256-fyeowVvyCi5GNC6kqXcEemnYtNZKEj/gteI7IP0pDIU=";
      }
    else
      pkgs.fetchurl {
        url = "https://github.com/AppImage/type2-runtime/releases/download/continuous/runtime-x86_64";
        hash = "sha256-okGdzkdWg5WuecAf+ppaNB3TOVgTUv8QTQc1J1Qxd+U=";
      };

  runtimeLibs = [
    pkgs.qt6.qtbase
    pkgs.qt6.qtremoteobjects
    pkgs.qt6.qtwebview
    pkgs.qt6.qtdeclarative
    pkgs.zstd
    pkgs.krb5
    pkgs.zlib
    pkgs.glib
    pkgs.freetype
    pkgs.fontconfig
    pkgs.libglvnd
    pkgs.mesa
    pkgs.xorg.libX11
    pkgs.xorg.libXext
    pkgs.xorg.libXrender
    pkgs.xorg.libXrandr
    pkgs.xorg.libXcursor
    pkgs.xorg.libXi
    pkgs.xorg.libXfixes
    pkgs.xorg.libxcb
  ];
  runtimeLibsStr = pkgs.lib.concatStringsSep " " (map toString runtimeLibs);
in
pkgs.stdenv.mkDerivation rec {
  pname = "logos-basecamp-appimage";
  inherit version;

  dontUnpack = true;
  dontWrapQtApps = true;
  nativeBuildInputs = [ pkgs.squashfsTools ];
  buildInputs = runtimeLibs;

  installPhase = ''
    set -euo pipefail
    appDir=$out/LogosBasecamp.AppDir
    mkdir -p "$appDir/usr"

    # Application payload (dereference symlinks, drop read-only perms)
    cp -rL --no-preserve=mode ${app}/bin "$appDir/usr/"
    if [ -d ${app}/lib ]; then cp -rL --no-preserve=mode ${app}/lib "$appDir/usr/"; fi
    if [ -d ${app}/preinstall ]; then cp -rL --no-preserve=mode ${app}/preinstall "$appDir/usr/"; fi

    # Qt and system runtime libraries
    mkdir -p "$appDir/usr/lib"
    for dep in ${runtimeLibsStr}; do
      if [ -d "$dep/lib" ]; then
        cp -L --no-preserve=mode "$dep"/lib/*.so* "$appDir/usr/lib/" 2>/dev/null || true
        if [ -d "$dep/lib/qt-6" ]; then
          cp -rL --no-preserve=mode "$dep/lib/qt-6" "$appDir/usr/lib/" 2>/dev/null || true
        fi
      fi
    done

    # Qt plugins and QML modules
    mkdir -p "$appDir/usr/lib/qt-6/plugins" "$appDir/usr/lib/qt-6/qml"
    cp -rL --no-preserve=mode ${pkgs.qt6.qtbase}/lib/qt-6/plugins/* "$appDir/usr/lib/qt-6/plugins/" 2>/dev/null || true
    cp -rL --no-preserve=mode ${pkgs.qt6.qtwebview}/lib/qt-6/plugins/* "$appDir/usr/lib/qt-6/plugins/" 2>/dev/null || true
    cp -rL --no-preserve=mode ${pkgs.qt6.qtdeclarative}/lib/qt-6/qml/* "$appDir/usr/lib/qt-6/qml/" 2>/dev/null || true
    cp -rL --no-preserve=mode ${pkgs.qt6.qtwebview}/lib/qt-6/qml/* "$appDir/usr/lib/qt-6/qml/" 2>/dev/null || true

    # Desktop entry and icon
    mkdir -p "$appDir/usr/share/applications" "$appDir/usr/share/icons/hicolor/256x256/apps"
    cp ${src}/assets/logos-basecamp.desktop "$appDir/usr/share/applications/"
    cp ${src}/app/icons/logos.png "$appDir/usr/share/icons/hicolor/256x256/apps/logos-basecamp.png"
    ln -sf usr/share/icons/hicolor/256x256/apps/logos-basecamp.png "$appDir/.DirIcon"

    # AppRun launcher — sets Qt env vars, then delegates to the logos-basecamp wrapper
    # which in turn execs the real LogosBasecamp binary (keeps process name intact).
    cat > "$appDir/AppRun" <<'EOF'
#!/bin/sh
APPDIR="$(dirname "$(readlink -f "$0")")"
export PATH="$APPDIR/usr/bin:$PATH"
export LD_LIBRARY_PATH="$APPDIR/usr/lib''${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export QT_PLUGIN_PATH="$APPDIR/usr/lib/qt-6/plugins''${QT_PLUGIN_PATH:+:$QT_PLUGIN_PATH}"
export QML2_IMPORT_PATH="$APPDIR/usr/lib/qt-6/qml''${QML2_IMPORT_PATH:+:$QML2_IMPORT_PATH}"
export NIXPKGS_QT6_QML_IMPORT_PATH="$QML2_IMPORT_PATH"
exec "$APPDIR/usr/bin/logos-basecamp" "$@"
EOF
    chmod +x "$appDir/AppRun"

    # Root-level desktop file symlink (required by AppImage spec)
    ln -sf usr/share/applications/logos-basecamp.desktop "$appDir/logos-basecamp.desktop"

    # Pack squashfs payload and prepend the architecture-matching runtime
    mksquashfs "$appDir" "$out/squashfs.img" -root-owned -noappend -comp zstd -Xcompression-level 22
    cat ${appimageRuntime} "$out/squashfs.img" > "$out/LogosBasecamp-${version}.AppImage"
    chmod +x "$out/LogosBasecamp-${version}.AppImage"
    rm "$out/squashfs.img"
  '';

  meta = with pkgs.lib; {
    description = "Logos Basecamp AppImage";
    platforms = platforms.linux;
    mainProgram = "logos-basecamp";
  };
}
