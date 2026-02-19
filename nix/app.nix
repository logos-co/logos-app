# Builds the logos-app standalone application
{ pkgs, common, src, logosLiblogos, logosSdk, logosPackageManager, logosCapabilityModule, logosDesignSystem, counterPlugin, counterQmlPlugin, mainUIPlugin, packageManagerUIPlugin, webviewAppPlugin }:

let
  # webkitgtk became ABI-versioned; pick the newest available while staying
  # compatible with older nixpkgs where the unversioned attribute still exists.
  webkitgtk = pkgs.webkitgtk_4_1 or pkgs.webkitgtk_4_0 or pkgs.webkitgtk;
in
pkgs.stdenv.mkDerivation rec {
  pname = "logos-app";
  version = common.version;
  
  inherit src;
  # Platform-specific build inputs for system webviews
  buildInputs = common.buildInputs ++ [
    pkgs.qt6.qtwebview
    pkgs.qt6.qtdeclarative
  ] ++ (
    if pkgs.stdenv.isLinux then
      # Linux: WebKitGTK as backend
      [ webkitgtk ]
    else
      []
  );
  inherit (common) meta;
  
  # Add logosSdk to nativeBuildInputs for logos-cpp-generator
  nativeBuildInputs = common.nativeBuildInputs ++ [ logosSdk pkgs.patchelf pkgs.removeReferencesTo ];
  
  # Provide Qt/GL runtime paths so the wrapper can inject them
  qtLibPath = pkgs.lib.makeLibraryPath (
    [
      pkgs.qt6.qtbase
      pkgs.qt6.qtremoteobjects
      pkgs.qt6.qtwebview
      pkgs.qt6.qtdeclarative
      pkgs.zstd
      pkgs.krb5
      pkgs.zlib
      pkgs.glib
      pkgs.stdenv.cc.cc
      pkgs.freetype
      pkgs.fontconfig
    ]
    ++ pkgs.lib.optionals pkgs.stdenv.isLinux [
      pkgs.libglvnd
      pkgs.mesa.drivers
      pkgs.xorg.libX11
      pkgs.xorg.libXext
      pkgs.xorg.libXrender
      pkgs.xorg.libXrandr
      pkgs.xorg.libXcursor
      pkgs.xorg.libXi
      pkgs.xorg.libXfixes
      pkgs.xorg.libxcb
    ]
  );
  qtPluginPath = "${pkgs.qt6.qtbase}/lib/qt-6/plugins:${pkgs.qt6.qtwebview}/lib/qt-6/plugins";
  qmlImportPath = "${placeholder "out"}/lib:${pkgs.qt6.qtdeclarative}/lib/qt-6/qml:${pkgs.qt6.qtwebview}/lib/qt-6/qml";
  
  preConfigure = ''
    runHook prePreConfigure
    
    # Set macOS deployment target to match Qt frameworks
    export MACOSX_DEPLOYMENT_TARGET=12.0
    
    # Copy logos-cpp-sdk headers to expected location
    echo "Copying logos-cpp-sdk headers for app..."
    mkdir -p ./logos-cpp-sdk/include/cpp
    cp -r ${logosSdk}/include/cpp/* ./logos-cpp-sdk/include/cpp/
    
    # Also copy core headers
    echo "Copying core headers..."
    mkdir -p ./logos-cpp-sdk/include/core
    cp -r ${logosSdk}/include/core/* ./logos-cpp-sdk/include/core/
    
    # Copy SDK library files to lib directory
    echo "Copying SDK library files..."
    mkdir -p ./logos-cpp-sdk/lib
    if [ -f "${logosSdk}/lib/liblogos_sdk.dylib" ]; then
      cp "${logosSdk}/lib/liblogos_sdk.dylib" ./logos-cpp-sdk/lib/
    elif [ -f "${logosSdk}/lib/liblogos_sdk.so" ]; then
      cp "${logosSdk}/lib/liblogos_sdk.so" ./logos-cpp-sdk/lib/
    elif [ -f "${logosSdk}/lib/liblogos_sdk.a" ]; then
      cp "${logosSdk}/lib/liblogos_sdk.a" ./logos-cpp-sdk/lib/
    fi
    
    runHook postPreConfigure
  '';
  
  passthru = { extraDirs = [ "modules" "plugins" ]; };

  # This is an aggregate runtime layout; avoid stripping to prevent hook errors
  dontStrip = true;
  
  # Skip wrapQtApps: wrapper renames binary to .LogosApp-wrapped; macOS Dock uses executable filename
  # We create a custom launcher that execs the binary (keeps process name "LogosApp")
  dontWrapQtApps = true;
  
  # Additional environment variables for Qt and RPATH cleanup
  preFixup = ''
    runHook prePreFixup
    
    # Set up Qt environment variables
    export QT_PLUGIN_PATH="${pkgs.qt6.qtbase}/lib/qt-6/plugins:${pkgs.qt6.qtwebview}/lib/qt-6/plugins"
    export QML2_IMPORT_PATH="${pkgs.qt6.qtdeclarative}/lib/qt-6/qml:${pkgs.qt6.qtwebview}/lib/qt-6/qml"
    
    # Remove any remaining references to /build/ in binaries and set proper RPATH
    find $out -type f -executable -exec sh -c '
      if file "$1" | grep -q "ELF.*executable"; then
        # Use patchelf to clean up RPATH if it contains /build/
        if patchelf --print-rpath "$1" 2>/dev/null | grep -q "/build/"; then
          echo "Cleaning RPATH for $1"
          patchelf --remove-rpath "$1" 2>/dev/null || true
        fi
        # Set proper RPATH for the main binary
        if echo "$1" | grep -q "/LogosApp$"; then
          echo "Setting RPATH for $1"
          patchelf --set-rpath "$out/lib" "$1" 2>/dev/null || true
        fi
      fi
    ' _ {} \;
    
    # Also clean up shared libraries
    find $out -name "*.so" -exec sh -c '
      if patchelf --print-rpath "$1" 2>/dev/null | grep -q "/build/"; then
        echo "Cleaning RPATH for $1"
        patchelf --remove-rpath "$1" 2>/dev/null || true
      fi
    ' _ {} \;
    
    runHook prePostFixup
  '';
  
  configurePhase = ''
    runHook preConfigure
    
    echo "Configuring logos-app..."
    echo "liblogos: ${logosLiblogos}"
    echo "cpp-sdk: ${logosSdk}"
    echo "package-manager: ${logosPackageManager}"
    echo "capability-module: ${logosCapabilityModule}"
    echo "counter-plugin: ${counterPlugin}"
    echo "counter-qml-plugin: ${counterQmlPlugin}"
    echo "main-ui-plugin: ${mainUIPlugin}"
    echo "package-manager-ui-plugin: ${packageManagerUIPlugin}"
    echo "webview-app-plugin: ${webviewAppPlugin}"
    echo "logos-design-system: ${logosDesignSystem}"
    
    # Verify that the built components exist
    test -d "${logosLiblogos}" || (echo "liblogos not found" && exit 1)
    test -d "${logosSdk}" || (echo "cpp-sdk not found" && exit 1)
    test -d "${logosPackageManager}" || (echo "package-manager not found" && exit 1)
    test -d "${logosCapabilityModule}" || (echo "capability-module not found" && exit 1)
    test -d "${counterPlugin}" || (echo "counter-plugin not found" && exit 1)
    test -d "${counterQmlPlugin}" || (echo "counter-qml-plugin not found" && exit 1)
    test -d "${mainUIPlugin}" || (echo "main-ui-plugin not found" && exit 1)
    test -d "${packageManagerUIPlugin}" || (echo "package-manager-ui-plugin not found" && exit 1)
    test -d "${webviewAppPlugin}" || (echo "webview-app-plugin not found" && exit 1)
    test -d "${logosDesignSystem}" || (echo "logos-design-system not found" && exit 1)
    
    cmake -S app -B build \
      -GNinja \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_OSX_DEPLOYMENT_TARGET=12.0 \
      -DCMAKE_INSTALL_RPATH_USE_LINK_PATH=FALSE \
      -DCMAKE_INSTALL_RPATH="" \
      -DCMAKE_SKIP_BUILD_RPATH=TRUE \
      -DLOGOS_LIBLOGOS_ROOT=${logosLiblogos} \
      -DLOGOS_CPP_SDK_ROOT=$(pwd)/logos-cpp-sdk
    
    runHook postConfigure
  '';
  
  buildPhase = ''
    runHook preBuild
    
    cmake --build build
    echo "logos-app built successfully!"
    
    runHook postBuild
  '';
  
  installPhase = ''
    runHook preInstall
    
    # Create output directories
    mkdir -p $out/bin $out/lib $out/modules $out/plugins
    
    # Install our app binary (real binary, so Qt hook can wrap it)
    if [ -f "build/LogosApp" ]; then
      cp build/LogosApp "$out/bin/LogosApp"
      echo "Installed LogosApp binary"
    fi
    
    # Copy the core binaries from liblogos
    if [ -f "${logosLiblogos}/bin/logoscore" ]; then
      cp -L "${logosLiblogos}/bin/logoscore" "$out/bin/"
      echo "Installed logoscore binary"
    fi
    if [ -f "${logosLiblogos}/bin/logos_host" ]; then
      cp -L "${logosLiblogos}/bin/logos_host" "$out/bin/"
      echo "Installed logos_host binary"
    fi
    
    # Copy required shared libraries from liblogos
    if ls "${logosLiblogos}/lib/"liblogos_core.* >/dev/null 2>&1; then
      cp -L "${logosLiblogos}/lib/"liblogos_core.* "$out/lib/" || true
    fi
    
    # Copy SDK library if it exists
    if ls "${logosSdk}/lib/"liblogos_sdk.* >/dev/null 2>&1; then
      cp -L "${logosSdk}/lib/"liblogos_sdk.* "$out/lib/" || true
    fi

    # Determine platform-specific plugin extension
    OS_EXT="so"
    case "$(uname -s)" in
      Darwin) OS_EXT="dylib";;
      Linux) OS_EXT="so";;
      MINGW*|MSYS*|CYGWIN*) OS_EXT="dll";;
    esac

    # Copy module plugins into the modules directory (each in its own subdirectory with manifest.json)
    if [ -f "${logosCapabilityModule}/lib/capability_module_plugin.$OS_EXT" ]; then
      mkdir -p "$out/modules/capability_module_plugin"
      cp -L "${logosCapabilityModule}/lib/capability_module_plugin.$OS_EXT" "$out/modules/capability_module_plugin/"
      cat > "$out/modules/capability_module_plugin/manifest.json" <<MANIFEST
{ "name": "capability_module", "type": "core", "version": "1.0.0" }
MANIFEST
      echo "Installed capability_module_plugin to modules/capability_module_plugin/"
    fi
    if [ -f "${logosPackageManager}/lib/package_manager_plugin.$OS_EXT" ]; then
      mkdir -p "$out/modules/package_manager_plugin"
      cp -L "${logosPackageManager}/lib/package_manager_plugin.$OS_EXT" "$out/modules/package_manager_plugin/"
      cat > "$out/modules/package_manager_plugin/manifest.json" <<MANIFEST
{ "name": "package_manager", "type": "core", "version": "1.0.0" }
MANIFEST
      echo "Installed package_manager_plugin to modules/package_manager_plugin/"
    fi
    
    # Copy UI plugins to plugins directory (each in its own subdirectory)
    if [ -f "${counterPlugin}/lib/counter.$OS_EXT" ]; then
      mkdir -p "$out/plugins/counter"
      cp -L "${counterPlugin}/lib/counter.$OS_EXT" "$out/plugins/counter/"
      echo "Copied counter.$OS_EXT to plugins/counter/"
    fi
    if [ -f "${mainUIPlugin}/lib/main_ui.$OS_EXT" ]; then
      mkdir -p "$out/plugins/main_ui"
      cp -L "${mainUIPlugin}/lib/main_ui.$OS_EXT" "$out/plugins/main_ui/"
      echo "Copied main_ui.$OS_EXT to plugins/main_ui/"
    fi
    if [ -f "${packageManagerUIPlugin}/lib/package_manager_ui.$OS_EXT" ]; then
      mkdir -p "$out/plugins/package_manager_ui"
      cp -L "${packageManagerUIPlugin}/lib/package_manager_ui.$OS_EXT" "$out/plugins/package_manager_ui/"
      echo "Copied package_manager_ui.$OS_EXT to plugins/package_manager_ui/"
    fi
    if [ -f "${webviewAppPlugin}/lib/webview_app.$OS_EXT" ]; then
      mkdir -p "$out/plugins/webview_app"
      cp -L "${webviewAppPlugin}/lib/webview_app.$OS_EXT" "$out/plugins/webview_app/"
      echo "Copied webview_app.$OS_EXT to plugins/webview_app/"
    fi

    if [ -d "${counterQmlPlugin}/qml_plugins/counter_qml" ]; then
      cp -R "${counterQmlPlugin}/qml_plugins/counter_qml" "$out/plugins/"
      echo "Copied counter_qml QML plugin to plugins/"
    fi

    # Copy design system QML modules (Logos.Theme, Logos.Controls) for runtime
    if [ -d "${logosDesignSystem}/lib/Logos/Theme" ]; then
      mkdir -p "$out/lib/Logos"
      cp -R "${logosDesignSystem}/lib/Logos/Theme" "$out/lib/Logos/"
      echo "Copied Logos.Theme to lib/Logos/Theme/"
    fi
    if [ -d "${logosDesignSystem}/lib/Logos/Controls" ]; then
      mkdir -p "$out/lib/Logos"
      cp -R "${logosDesignSystem}/lib/Logos/Controls" "$out/lib/Logos/"
      echo "Copied Logos.Controls to lib/Logos/Controls/"
    fi
    
    # Note: webview_app QML and HTML files are now embedded in the plugin via qrc

    # Create launcher script (sets Qt env, execs binary - process name stays "LogosApp" for Dock)
    cat > $out/bin/logos-app << 'EOF'
#!/bin/sh
EOF
    echo "export QT_PLUGIN_PATH=\"${qtPluginPath}\"" >> $out/bin/logos-app
    echo "export QML2_IMPORT_PATH=\"${qmlImportPath}\"" >> $out/bin/logos-app
    echo "export DYLD_LIBRARY_PATH=\"${qtLibPath}:\$DYLD_LIBRARY_PATH\"" >> $out/bin/logos-app
    echo "export LD_LIBRARY_PATH=\"${qtLibPath}:\$LD_LIBRARY_PATH\"" >> $out/bin/logos-app
    echo 'exec "$(dirname "$0")/LogosApp" "$@"' >> $out/bin/logos-app
    chmod +x $out/bin/logos-app

    # Create a README for reference
    cat > $out/README.txt <<EOF
Logos App - Build Information
==================================
liblogos: ${logosLiblogos}
cpp-sdk: ${logosSdk}
package-manager: ${logosPackageManager}
capability-module: ${logosCapabilityModule}
counter-plugin: ${counterPlugin}
main-ui-plugin: ${mainUIPlugin}
package-manager-ui-plugin: ${packageManagerUIPlugin}
webview-app-plugin: ${webviewAppPlugin}
logos-design-system: ${logosDesignSystem}

Runtime Layout:
    - Binary: $out/bin/LogosApp
- Libraries: $out/lib
- Modules: $out/modules
- Plugins: $out/plugins

Usage:
  $out/bin/LogosApp
EOF
    
    runHook postInstall
  '';
  
}
