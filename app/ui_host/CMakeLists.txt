cmake_minimum_required(VERSION 3.16)
if(APPLE)
    project(LogosUiHost LANGUAGES CXX OBJCXX)
else()
    project(LogosUiHost LANGUAGES CXX)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Quick Qml QuickWidgets QuickControls2 RemoteObjects Network)

# RPATH settings
set(CMAKE_SKIP_BUILD_RPATH FALSE)
set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
if(APPLE)
    set(CMAKE_INSTALL_RPATH "@executable_path/../lib")
else()
    set(CMAKE_INSTALL_RPATH "\$ORIGIN/../lib:\$ORIGIN/lib")
endif()

# Include paths — reuse from the parent app build
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../interfaces)

# logos-cpp-sdk (for LogosAPI)
if(DEFINED LOGOS_CPP_SDK_ROOT)
    if(EXISTS "${LOGOS_CPP_SDK_ROOT}/cpp/logos_api.h")
        set(_ui_host_sdk_is_source TRUE)
    else()
        set(_ui_host_sdk_is_source FALSE)
    endif()

    if(_ui_host_sdk_is_source)
        include_directories(${LOGOS_CPP_SDK_ROOT}/cpp)
    else()
        include_directories(
            ${LOGOS_CPP_SDK_ROOT}/include
            ${LOGOS_CPP_SDK_ROOT}/include/cpp
            ${LOGOS_CPP_SDK_ROOT}/include/core
        )
    endif()
endif()

# EmbedControlChannel source (shared with main_ui plugin)
set(OOP_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../src/oop)

set(SOURCES
    main.cpp
    ${OOP_SRC_DIR}/EmbedControlChannel.cpp
    ${OOP_SRC_DIR}/EmbedControlChannel.h
)

# macOS Objective-C++ helpers (accessory app, window level)
if(APPLE)
    list(APPEND SOURCES macos_helpers.mm)
else()
    list(APPEND SOURCES macos_helpers_stub.cpp)
endif()

# Add SDK sources for source layout
if(_ui_host_sdk_is_source)
    list(APPEND SOURCES
        ${LOGOS_CPP_SDK_ROOT}/cpp/logos_api.cpp
        ${LOGOS_CPP_SDK_ROOT}/cpp/logos_api_client.cpp
        ${LOGOS_CPP_SDK_ROOT}/cpp/logos_api_consumer.cpp
        ${LOGOS_CPP_SDK_ROOT}/cpp/logos_api_provider.cpp
        ${LOGOS_CPP_SDK_ROOT}/cpp/token_manager.cpp
        ${LOGOS_CPP_SDK_ROOT}/cpp/module_proxy.cpp
    )
endif()

qt_add_executable(logos_ui_host ${SOURCES})

target_include_directories(logos_ui_host PRIVATE
    ${OOP_SRC_DIR}/..
)

target_link_libraries(logos_ui_host PRIVATE
    Qt6::Core
    Qt6::Widgets
    Qt6::Quick
    Qt6::Qml
    Qt6::QuickWidgets
    Qt6::QuickControls2
    Qt6::RemoteObjects
    Qt6::Network
)

# Link SDK library for installed layout
if(NOT _ui_host_sdk_is_source AND DEFINED LOGOS_CPP_SDK_ROOT)
    find_library(UI_HOST_SDK_LIB logos_sdk PATHS ${LOGOS_CPP_SDK_ROOT}/lib NO_DEFAULT_PATH)
    if(UI_HOST_SDK_LIB)
        target_link_libraries(logos_ui_host PRIVATE ${UI_HOST_SDK_LIB})
    endif()
endif()
