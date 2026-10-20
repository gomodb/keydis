# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: 2026 KeyDis contributors

cmake_minimum_required(VERSION 3.28)

project(KeyDis
    VERSION 0.4.40
    DESCRIPTION "Native Redis/Valkey client for KDE"
    HOMEPAGE_URL "https://github.com/gomodb/keydis"
    LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(CTest)
include(FetchContent)

if(POLICY CMP0169)
    cmake_policy(SET CMP0169 OLD)
endif()
include(GNUInstallDirs)

function(keydis_enable_warnings target)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets Network Test LinguistTools)
find_package(KF6CoreAddons REQUIRED)
find_package(KF6Config REQUIRED)
find_package(KF6ColorScheme REQUIRED)
find_package(KF6ConfigWidgets REQUIRED)
find_package(KF6DBusAddons REQUIRED)
find_package(KF6I18n REQUIRED)
find_package(KF6Wallet REQUIRED)
find_package(KF6XmlGui REQUIRED)
find_package(KF6Runner QUIET)
find_package(PkgConfig REQUIRED)
pkg_check_modules(MSGPACK REQUIRED IMPORTED_TARGET msgpack-c)
find_package(ZLIB REQUIRED)
find_path(ZSTD_INCLUDE_DIR NAMES zstd.h REQUIRED)
find_library(ZSTD_LIBRARY NAMES zstd REQUIRED)
find_path(BROTLI_INCLUDE_DIR NAMES brotli/decode.h REQUIRED)
find_library(BROTLIDEC_LIBRARY NAMES brotlidec REQUIRED)

# hiredis is used only through its public reader API. Pin the upstream stable
# release so parser behaviour does not drift between developer machines.
# Distributions should configure with -DKEYDIS_USE_SYSTEM_HIREDIS=ON so the
# package build does not fetch sources from the network.
option(KEYDIS_USE_SYSTEM_HIREDIS "Use the system hiredis instead of FetchContent" OFF)
set(KEYDIS_HIREDIS_TARGET hiredis)
if(KEYDIS_USE_SYSTEM_HIREDIS)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(HIREDIS REQUIRED IMPORTED_TARGET hiredis)
    set(KEYDIS_HIREDIS_TARGET PkgConfig::HIREDIS)
else()
    set(DISABLE_TESTS ON CACHE BOOL "" FORCE)
    set(ENABLE_SSL OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(hiredis
        GIT_REPOSITORY https://github.com/redis/hiredis.git
        GIT_TAG v1.5.0
        GIT_SHALLOW TRUE
    )
    FetchContent_GetProperties(hiredis)
    if(NOT hiredis_POPULATED)
        FetchContent_Populate(hiredis)
        set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
        add_subdirectory(${hiredis_SOURCE_DIR} ${hiredis_BINARY_DIR} EXCLUDE_FROM_ALL)
        unset(BUILD_SHARED_LIBS CACHE)
    endif()
    unset(DISABLE_TESTS CACHE)
endif()

add_library(keydis_core STATIC
    src/core/analysis/memoryanalyzer.cpp
    src/core/analysis/memoryanalyzer.h
    src/core/app_paths.cpp
    src/core/app_paths.h
    src/core/conn/connectionprofile.cpp
    src/core/conn/connectionprofile.h
    src/core/conn/profilestore.cpp
    src/core/conn/profilestore.h
    src/core/conn/sentinelresolver.cpp
    src/core/conn/sentinelresolver.h
    src/core/conn/sentinelwatcher.cpp
    src/core/conn/sentinelwatcher.h
    src/core/diagnostics.cpp
    src/core/diagnostics.h
    src/core/insights/redisinfo.cpp
    src/core/insights/redisinfo.h
    src/core/keyspace/keyinfo.cpp
    src/core/keyspace/keyinfo.h
    src/core/keyspace/keyscanner.cpp
    src/core/keyspace/keyscanner.h
    src/core/language.cpp
    src/core/language.h
    src/core/logger.cpp
    src/core/logger.h
    src/core/net/clusterclient.cpp
    src/core/net/clusterclient.h
    src/core/net/redisclient.h
    src/core/net/redisrouter.cpp
    src/core/net/redisrouter.h
    src/core/net/redisconnection.cpp
    src/core/net/redisconnection.h
    src/core/net/sshtunnel.cpp
    src/core/net/sshtunnel.h
    src/core/resp/commandencoder.cpp
    src/core/resp/commandencoder.h
    src/core/resp/hiredisreader.cpp
    src/core/resp/hiredisreader.h
    src/core/security/secretstore.cpp
    src/core/security/secretstore.h
    src/core/transfer/keytransfer.cpp
    src/core/transfer/keytransfer.h
    src/core/resp/respvalue.cpp
    src/core/resp/respvalue.h
    src/core/safety/commandsafety.cpp
    src/core/safety/commandsafety.h
    src/core/format/byteformat.cpp
    src/core/format/byteformat.h
    src/core/format/compressioncodec.cpp
    src/core/format/compressioncodec.h
    src/core/format/msgpackdecoder.cpp
    src/core/format/msgpackdecoder.h
    src/core/format/protobufdecoder.cpp
    src/core/format/protobufdecoder.h
)

qt_add_library(keydis_ui_models STATIC
    src/ui/models/keytablemodel.cpp
    src/ui/models/keytablemodel.h
    src/ui/models/namespacetreemodel.cpp
    src/ui/models/namespacetreemodel.h
)
target_include_directories(keydis_ui_models PUBLIC src)
target_link_libraries(keydis_ui_models PUBLIC Qt6::Core Qt6::Gui keydis_core)

target_include_directories(keydis_core PUBLIC src)
if(NOT KEYDIS_USE_SYSTEM_HIREDIS)
    target_include_directories(keydis_core PRIVATE ${hiredis_SOURCE_DIR})
endif()

target_include_directories(keydis_core PRIVATE ${ZSTD_INCLUDE_DIR} ${BROTLI_INCLUDE_DIR})
target_link_libraries(keydis_core
    PUBLIC Qt6::Core Qt6::Network
    PRIVATE KF6::ConfigCore KF6::I18n KF6::Wallet ${KEYDIS_HIREDIS_TARGET}
            ZLIB::ZLIB ${ZSTD_LIBRARY} ${BROTLIDEC_LIBRARY} PkgConfig::MSGPACK
)

target_compile_definitions(keydis_core PUBLIC
    KEYDIS_VERSION="${PROJECT_VERSION}"
)

add_library(keydis_ui STATIC
    src/app/mainwindow.cpp
    src/app/mainwindow.h
    src/app/sessionhost.cpp
    src/app/sessionhost.h
    src/ui/dashboard/dashboardpage.cpp
    src/ui/dashboard/dashboardpage.h
    src/ui/dialogs/batcheditdialog.cpp
    src/ui/dialogs/batcheditdialog.h
    src/ui/dialogs/profiledialog.cpp
    src/ui/dialogs/profiledialog.h
    src/ui/dialogs/binaryeditdialog.cpp
    src/ui/dialogs/binaryeditdialog.h
    src/ui/dialogs/compressionviewdialog.cpp
    src/ui/dialogs/compressionviewdialog.h
    src/ui/dialogs/msgpackviewdialog.cpp
    src/ui/dialogs/msgpackviewdialog.h
    src/ui/dialogs/bitmapviewdialog.cpp
    src/ui/dialogs/bitmapviewdialog.h
    src/ui/dialogs/hyperloglogviewdialog.cpp
    src/ui/dialogs/hyperloglogviewdialog.h
    src/ui/dialogs/geosearchdialog.cpp
    src/ui/dialogs/geosearchdialog.h
    src/ui/dialogs/protobufviewdialog.cpp
    src/ui/dialogs/protobufviewdialog.h
    src/ui/tools/servertools.cpp
    src/ui/tools/servertools.h
    src/ui/values/valuepanel.cpp
    src/ui/values/valuepanel.h
)
target_include_directories(keydis_ui PUBLIC src)
target_link_libraries(keydis_ui PUBLIC
    keydis_core
    keydis_ui_models
    Qt6::Gui
    Qt6::Widgets
    KF6::ColorScheme
    KF6::ConfigWidgets
    KF6::CoreAddons
    KF6::I18n
    KF6::XmlGui
)

qt_add_executable(keydis
    src/app/main.cpp
)

qt_add_resources(keydis "keydis_resources"
    PREFIX "/keydis"
    FILES src/keydisui.rc
)

qt_add_resources(keydis "keydis_icons"
    PREFIX "/icons"
    FILES resources/icons/keydis.svg
)

qt_add_lrelease(
    LRELEASE_TARGET keydis_lrelease
    TS_FILES translations/keydis_zh_CN.ts
    QM_FILES_OUTPUT_VARIABLE KEYDIS_QM_FILES
)
qt_add_resources(keydis "keydis_translations"
    PREFIX "/i18n"
    BASE ${CMAKE_CURRENT_BINARY_DIR}
    FILES ${KEYDIS_QM_FILES}
)
add_dependencies(keydis keydis_lrelease)

target_link_libraries(keydis PRIVATE
    keydis_ui
    KF6::DBusAddons
)

install(TARGETS keydis RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES src/keydisui.rc DESTINATION ${CMAKE_INSTALL_DATADIR}/keydis)
install(FILES data/io.github.keydis.KeyDis.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES data/io.github.keydis.KeyDis.metainfo.xml
    DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
install(FILES resources/icons/keydis.svg
    DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
install(FILES ${KEYDIS_QM_FILES}
    DESTINATION ${CMAKE_INSTALL_DATADIR}/keydis/translations)
install(FILES LICENSE NOTICE LICENSES/MIT.txt LICENSES/BSD-3-Clause.txt
    DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/keydis)

if(KF6Runner_FOUND)
    add_library(keydisrunner MODULE
        src/krunner/keydisrunner.cpp
        src/krunner/keydisrunner.h
    )
    target_include_directories(keydisrunner PRIVATE src)
    target_link_libraries(keydisrunner PRIVATE
        keydis_core
        KF6::CoreAddons
        KF6::Runner
    )
    install(TARGETS keydisrunner
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/qt6/plugins/kf6/krunner)
endif()

if(BUILD_TESTING)
    add_executable(keydis_tests
        tests/fakeserver.cpp
        tests/fakeserver.h
        tests/test_clusterclient.cpp
        tests/test_clusterclient.h
        tests/test_commandsafety.cpp
        tests/test_commandsafety.h
        tests/test_byteformat.cpp
        tests/test_byteformat.h
        tests/test_compression.cpp
        tests/test_compression.h
        tests/test_msgpack.cpp
        tests/test_msgpack.h
        tests/test_protobuf.cpp
        tests/test_protobuf.h
        tests/test_commandencoder.cpp
        tests/test_commandencoder.h
        tests/test_keyscanner.cpp
        tests/test_keyscanner.h
        tests/test_language.cpp
        tests/test_language.h
        tests/test_logger.cpp
        tests/test_logger.h
        tests/test_namespacetreemodel.cpp
        tests/test_namespacetreemodel.h
        tests/test_main.cpp
        tests/test_profilestore.cpp
        tests/test_profilestore.h
        tests/test_redisinfo.cpp
        tests/test_redisinfo.h
        tests/test_redisintegration.cpp
        tests/test_redisintegration.h
        tests/test_sshtunnel.cpp
        tests/test_sshtunnel.h
        tests/test_redisconnection.cpp
        tests/test_redisconnection.h
        tests/test_resp.cpp
        tests/test_resp.h
    )
    target_link_libraries(keydis_tests PRIVATE keydis_core keydis_ui_models Qt6::Test)
    qt_add_resources(keydis_tests "keydis_test_translations"
        PREFIX "/i18n"
        BASE ${CMAKE_CURRENT_BINARY_DIR}
        FILES ${KEYDIS_QM_FILES}
    )
    add_dependencies(keydis_tests keydis_lrelease)
    add_test(NAME keydis_tests COMMAND keydis_tests)
    set_tests_properties(keydis_tests PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/test-config;XDG_STATE_HOME=${CMAKE_CURRENT_BINARY_DIR}/test-state;XDG_CACHE_HOME=${CMAKE_CURRENT_BINARY_DIR}/test-cache;KEYDIS_TEST_CONFIG_RESET=1"
        TIMEOUT 120
    )

    add_executable(keydis_ui_tests
        tests/test_mainwindow.cpp
        tests/test_mainwindow.h
        tests/test_profiledialog.cpp
        tests/test_profiledialog.h
        tests/test_ui_main.cpp
        tests/test_valuepanel.cpp
        tests/test_valuepanel.h
    )
    target_link_libraries(keydis_ui_tests PRIVATE keydis_ui Qt6::Test)
    qt_add_resources(keydis_ui_tests "keydis_ui_test_resources"
        PREFIX "/keydis"
        FILES src/keydisui.rc
    )
    qt_add_resources(keydis_ui_tests "keydis_ui_test_translations"
        PREFIX "/i18n"
        BASE ${CMAKE_CURRENT_BINARY_DIR}
        FILES ${KEYDIS_QM_FILES}
    )
    add_dependencies(keydis_ui_tests keydis_lrelease)
    add_test(NAME keydis_ui_tests COMMAND keydis_ui_tests)
    set_tests_properties(keydis_ui_tests PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/test-config;XDG_STATE_HOME=${CMAKE_CURRENT_BINARY_DIR}/test-state;XDG_CACHE_HOME=${CMAKE_CURRENT_BINARY_DIR}/test-cache;KEYDIS_TEST_CONFIG_RESET=1"
        TIMEOUT 120
    )
endif()

foreach(keydis_target keydis_core keydis_ui_models keydis_ui keydis keydisrunner keydis_tests keydis_ui_tests)
    if(TARGET ${keydis_target})
        keydis_enable_warnings(${keydis_target})
    endif()
endforeach()
