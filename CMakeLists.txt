cmake_minimum_required(VERSION 3.24)

project(computer.cpp
    VERSION 0.3.0
    DESCRIPTION "Agent-oriented desktop computer control surface"
    LANGUAGES CXX
)

set(COMPUTER_CPP_CODE_SIGN_IDENTITY "auto" CACHE STRING "macOS code signing identity for ComputerCpp.app; 'auto' prefers a stable Apple identity, '-' uses fragile ad-hoc signing")
option(COMPUTER_CPP_CODE_SIGN_APP "Code sign the macOS ComputerCpp.app bundle after building" ON)
option(COMPUTER_CPP_ENABLE_WARNINGS "Enable strict compiler warnings for project targets" ON)
option(COMPUTER_CPP_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer for project targets" OFF)
option(COMPUTER_CPP_BUILD_GUI "Build the ComputerCpp desktop tray application when wxWidgets is available" ON)

function(computer_cpp_target_defaults target_name)
    target_compile_features(${target_name} PRIVATE cxx_std_20)

    if(COMPUTER_CPP_ENABLE_WARNINGS)
        target_compile_options(${target_name}
            PRIVATE
                $<$<CXX_COMPILER_ID:AppleClang,Clang,GNU>:-Wall -Wextra -Wpedantic>
                $<$<CXX_COMPILER_ID:MSVC>:/W4>
        )
    endif()

    if(COMPUTER_CPP_ENABLE_SANITIZERS AND NOT MSVC)
        target_compile_options(${target_name}
            PRIVATE
                -fsanitize=address,undefined
                -fno-omit-frame-pointer
        )
        target_link_options(${target_name}
            PRIVATE
                -fsanitize=address,undefined
        )
    endif()
endfunction()

find_package(wxWidgets QUIET COMPONENTS core base adv)
if(wxWidgets_FOUND AND COMPUTER_CPP_BUILD_GUI)
    include(${wxWidgets_USE_FILE})
endif()
find_package(SQLite3 REQUIRED)
if(NOT TARGET SQLite3::SQLite3)
    add_library(SQLite3::SQLite3 UNKNOWN IMPORTED)
    set_target_properties(SQLite3::SQLite3 PROPERTIES
        IMPORTED_LOCATION "${SQLite3_LIBRARIES}"
        INTERFACE_INCLUDE_DIRECTORIES "${SQLite3_INCLUDE_DIRS}"
    )
endif()
find_package(CURL REQUIRED COMPONENTS HTTPS SSL)
find_package(ZLIB REQUIRED)

include(FetchContent)
FetchContent_Declare(
    json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(json)

FetchContent_Declare(
    tomlplusplus
    URL https://github.com/marzer/tomlplusplus/archive/refs/tags/v3.4.0.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(tomlplusplus)

include(CTest)

find_package(Git QUIET)
set(COMPUTER_CPP_BUILD_GIT_SHA "unknown")
if(Git_FOUND)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" -C "${CMAKE_SOURCE_DIR}" rev-parse --short=12 HEAD
        OUTPUT_VARIABLE COMPUTER_CPP_BUILD_GIT_SHA
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(NOT COMPUTER_CPP_BUILD_GIT_SHA)
        set(COMPUTER_CPP_BUILD_GIT_SHA "unknown")
    endif()
endif()
string(TIMESTAMP COMPUTER_CPP_BUILD_TIMESTAMP "%Y-%m-%dT%H:%M:%SZ" UTC)

add_library(computer_cpp_core
    src/core/AppPaths.cpp
    src/core/AppConfig.cpp
    src/core/Image.cpp
    src/core/ControlSessionAcquire.cpp
    src/core/ControlSession.cpp
    src/core/ControlSessionInternal.cpp
    src/core/ControlSessionJson.cpp
    src/core/ControlSessionMetrics.cpp
    src/core/ControlSessionPrometheus.cpp
    src/core/ControlSessionQueries.cpp
    src/core/ControlSessionResources.cpp
    src/core/ControlSessionStore.cpp
    src/core/ControlSessionValidation.cpp
    src/core/InferenceConfig.cpp
    src/core/InferenceContent.cpp
    src/core/InferenceClient.cpp
    src/core/Json.cpp
    src/core/NativeDeps.cpp
    src/core/RefStore.cpp
    src/core/RuntimeProvenance.cpp
    src/core/Sqlite.cpp
    src/core/StringUtils.cpp
    src/core/Timeline.cpp
    src/core/TrayServerState.cpp
    src/core/Updater.cpp
    src/daemon/Daemon.cpp
    src/daemon/DaemonArtifacts.cpp
    src/daemon/DaemonBatch.cpp
    src/daemon/DaemonDesktop.cpp
    src/daemon/DaemonClick.cpp
    src/daemon/DaemonControlSession.cpp
    src/daemon/DaemonImage.cpp
    src/daemon/DaemonInput.cpp
    src/daemon/DaemonIdle.cpp
    src/daemon/DaemonJson.cpp
    src/daemon/DaemonMetadata.cpp
    src/daemon/DaemonObservedEvent.cpp
    src/daemon/DaemonObservation.cpp
    src/daemon/DaemonProtocol.cpp
    src/daemon/DaemonScreenshot.cpp
    src/daemon/DaemonSnapshot.cpp
    src/daemon/DaemonSocket.cpp
    src/daemon/DaemonScrollCommand.cpp
    src/daemon/DaemonTargetCommand.cpp
    src/daemon/DaemonTargetGeometry.cpp
    src/daemon/DaemonTargetRefs.cpp
    src/daemon/DaemonTargetResolve.cpp
    src/daemon/DaemonTargetText.cpp
    src/daemon/DaemonTextInput.cpp
    src/daemon/Transport.cpp
)

target_include_directories(computer_cpp_core
    PUBLIC
        include
)

computer_cpp_target_defaults(computer_cpp_core)
target_compile_features(computer_cpp_core PUBLIC cxx_std_20)

target_compile_definitions(computer_cpp_core
    PUBLIC
        COMPUTER_CPP_BUILD_GIT_SHA="${COMPUTER_CPP_BUILD_GIT_SHA}"
        COMPUTER_CPP_BUILD_TIMESTAMP="${COMPUTER_CPP_BUILD_TIMESTAMP}"
        COMPUTER_CPP_PROJECT_VERSION="${PROJECT_VERSION}"
        COMPUTER_CPP_GITHUB_REPOSITORY="gobii-ai/computer.cpp"
)

target_link_libraries(computer_cpp_core
    PUBLIC
        nlohmann_json::nlohmann_json
        tomlplusplus::tomlplusplus
        SQLite3::SQLite3
        CURL::libcurl
        ZLIB::ZLIB
)

if(APPLE)
    target_sources(computer_cpp_core
        PRIVATE
            src/platform/macos/PlatformMac.mm
    )

    find_library(APPKIT_LIBRARY AppKit REQUIRED)
    find_library(APPLICATIONSERVICES_LIBRARY ApplicationServices REQUIRED)
    find_library(CARBON_LIBRARY Carbon REQUIRED)
    find_library(COREGRAPHICS_LIBRARY CoreGraphics REQUIRED)
    find_library(FOUNDATION_LIBRARY Foundation REQUIRED)
    find_library(IMAGEIO_LIBRARY ImageIO REQUIRED)
    find_library(SCREENCAPTUREKIT_LIBRARY ScreenCaptureKit REQUIRED)

    target_link_libraries(computer_cpp_core
        PUBLIC
            ${APPKIT_LIBRARY}
            ${APPLICATIONSERVICES_LIBRARY}
            ${CARBON_LIBRARY}
            ${COREGRAPHICS_LIBRARY}
            ${FOUNDATION_LIBRARY}
            ${IMAGEIO_LIBRARY}
            ${SCREENCAPTUREKIT_LIBRARY}
    )
elseif(WIN32)
    target_sources(computer_cpp_core PRIVATE
        src/platform/windows/PlatformWindows.cpp
    )
elseif(UNIX)
    target_sources(computer_cpp_core PRIVATE
        src/platform/linux/LinuxPng.cpp
        src/platform/linux/PlatformLinux.cpp
    )
    find_package(X11 REQUIRED COMPONENTS Xtst)
    target_link_libraries(computer_cpp_core PUBLIC ${X11_LIBRARIES} ${X11_Xtst_LIB})
    target_include_directories(computer_cpp_core PUBLIC ${X11_INCLUDE_DIR})

    if(PkgConfig_FOUND)
        pkg_check_modules(DBUS1 QUIET dbus-1)
        pkg_check_modules(LIBEI QUIET libei-1.0)
        if(DBUS1_FOUND AND LIBEI_FOUND)
            add_executable(computer.cpp-wayland-input
                src/platform/linux/KWinWaylandInput.cpp
            )
            target_include_directories(computer.cpp-wayland-input
                PRIVATE
                    include
                    ${DBUS1_INCLUDE_DIRS}
                    ${LIBEI_INCLUDE_DIRS}
            )
            target_link_directories(computer.cpp-wayland-input
                PRIVATE
                    ${DBUS1_LIBRARY_DIRS}
                    ${LIBEI_LIBRARY_DIRS}
            )
            target_compile_options(computer.cpp-wayland-input
                PRIVATE
                    ${DBUS1_CFLAGS_OTHER}
                    ${LIBEI_CFLAGS_OTHER}
            )
            target_link_libraries(computer.cpp-wayland-input
                PRIVATE
                    ${DBUS1_LIBRARIES}
                    ${LIBEI_LIBRARIES}
            )
        endif()
    endif()
endif()

add_executable(computer.cpp
    src/cli/CliApp.cpp
    src/cli/CliConfig.cpp
    src/cli/CliCommands.cpp
    src/cli/CliInputCommands.cpp
    src/cli/CliObserveTargetCommands.cpp
    src/cli/CliOptions.cpp
    src/cli/CliOutput.cpp
    src/cli/CliRuntime.cpp
    src/cli/CliRunCommand.cpp
    src/cli/CliSession.cpp
    src/cli/CliSessionCleanup.cpp
    src/cli/CliSessionInspect.cpp
    src/cli/CliSessionLease.cpp
    src/cli/CliSessionParsing.cpp
    src/cli/CliSessionProcess.cpp
    src/cli/CliSessionRun.cpp
    src/cli/CliUsage.cpp
    src/cli/LuaPrelude.cpp
    src/cli/LuaRunner.cpp
    src/cli/main.cpp
)

computer_cpp_target_defaults(computer.cpp)

target_link_libraries(computer.cpp
    PRIVATE
        computer_cpp_core
)

install(TARGETS computer.cpp RUNTIME DESTINATION bin)

if(TARGET computer.cpp-wayland-input)
    install(TARGETS computer.cpp-wayland-input RUNTIME DESTINATION bin)
endif()

if(wxWidgets_FOUND AND COMPUTER_CPP_BUILD_GUI)
    add_executable(ComputerCpp MACOSX_BUNDLE
        src/app/Main.cpp
        src/app/TrayIcon.cpp
        src/app/UpdateFlow.cpp
    )
    add_dependencies(ComputerCpp computer.cpp)

    target_include_directories(ComputerCpp
        PRIVATE
            include
            src/app
    )

    computer_cpp_target_defaults(ComputerCpp)

    target_link_libraries(ComputerCpp
        PRIVATE
            computer_cpp_core
            ${wxWidgets_LIBRARIES}
    )

    if(APPLE)
        set_target_properties(ComputerCpp PROPERTIES
            MACOSX_BUNDLE_BUNDLE_NAME "ComputerCpp"
            MACOSX_BUNDLE_GUI_IDENTIFIER "org.computercpp.app"
            MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/cmake/macos/Info.plist.in"
        )

        add_custom_command(TARGET ComputerCpp POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy "$<TARGET_FILE:computer.cpp>" "$<TARGET_BUNDLE_DIR:ComputerCpp>/Contents/MacOS/computer.cpp"
            VERBATIM
        )

        if(COMPUTER_CPP_CODE_SIGN_APP)
            set(COMPUTER_CPP_RESOLVED_CODE_SIGN_IDENTITY "${COMPUTER_CPP_CODE_SIGN_IDENTITY}")
            if(COMPUTER_CPP_CODE_SIGN_IDENTITY STREQUAL "auto")
                execute_process(
                    COMMAND security find-identity -v -p codesigning
                    OUTPUT_VARIABLE COMPUTER_CPP_CODE_SIGN_IDENTITIES
                    ERROR_QUIET
                    OUTPUT_STRIP_TRAILING_WHITESPACE
                )
                string(REGEX MATCH "\"[^\"]*(Apple Development|Developer ID Application)[^\"]*\"" COMPUTER_CPP_CODE_SIGN_MATCH "${COMPUTER_CPP_CODE_SIGN_IDENTITIES}")
                if(NOT COMPUTER_CPP_CODE_SIGN_MATCH)
                    string(REGEX MATCH "\"ComputerCpp Local Code Signing\"" COMPUTER_CPP_CODE_SIGN_MATCH "${COMPUTER_CPP_CODE_SIGN_IDENTITIES}")
                endif()
                if(COMPUTER_CPP_CODE_SIGN_MATCH)
                    string(REGEX REPLACE "^\"|\"$" "" COMPUTER_CPP_RESOLVED_CODE_SIGN_IDENTITY "${COMPUTER_CPP_CODE_SIGN_MATCH}")
                    message(STATUS "Signing ComputerCpp.app with ${COMPUTER_CPP_RESOLVED_CODE_SIGN_IDENTITY}")
                    if(COMPUTER_CPP_RESOLVED_CODE_SIGN_IDENTITY STREQUAL "ComputerCpp Local Code Signing")
                        message(WARNING "ComputerCpp.app is signed with a local self-signed identity. macOS Screen Recording prompts may be denied by TCC; install an Apple Development or Developer ID Application certificate for reliable ScreenCapture onboarding.")
                    endif()
                else()
                    set(COMPUTER_CPP_RESOLVED_CODE_SIGN_IDENTITY "-")
                    message(WARNING "No Apple code-signing identity found; ComputerCpp.app will be ad-hoc signed. macOS TCC permissions may be stale or unreliable across rebuilds.")
                endif()
            elseif(COMPUTER_CPP_CODE_SIGN_IDENTITY STREQUAL "-")
                message(WARNING "ComputerCpp.app is configured for ad-hoc signing. macOS TCC permissions may be stale or unreliable across rebuilds.")
            endif()
            add_custom_command(TARGET ComputerCpp POST_BUILD
                COMMAND codesign --force --deep --sign "${COMPUTER_CPP_RESOLVED_CODE_SIGN_IDENTITY}" "$<TARGET_BUNDLE_DIR:ComputerCpp>"
                VERBATIM
            )
        endif()

        set(COMPUTER_CPP_LSREGISTER_EXECUTABLE "/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister")
        if(EXISTS "${COMPUTER_CPP_LSREGISTER_EXECUTABLE}")
            add_custom_command(TARGET ComputerCpp POST_BUILD
                COMMAND "${COMPUTER_CPP_LSREGISTER_EXECUTABLE}" -f "$<TARGET_BUNDLE_DIR:ComputerCpp>"
                VERBATIM
            )
        endif()

        find_program(COMPUTER_CPP_MDIMPORT_EXECUTABLE mdimport)
        if(COMPUTER_CPP_MDIMPORT_EXECUTABLE)
            add_custom_command(TARGET ComputerCpp POST_BUILD
                COMMAND "${COMPUTER_CPP_MDIMPORT_EXECUTABLE}" "$<TARGET_BUNDLE_DIR:ComputerCpp>"
                VERBATIM
            )
        endif()
    endif()
endif()

if(BUILD_TESTING)
    add_executable(computer_cpp_tests
        src/cli/CliApp.cpp
        src/cli/CliConfig.cpp
        src/cli/CliCommands.cpp
        src/cli/CliInputCommands.cpp
        src/cli/CliObserveTargetCommands.cpp
        src/cli/CliOptions.cpp
        src/cli/CliOutput.cpp
        src/cli/CliRunCommand.cpp
        src/cli/CliSession.cpp
        src/cli/CliSessionCleanup.cpp
        src/cli/CliSessionInspect.cpp
        src/cli/CliSessionLease.cpp
        src/cli/CliSessionParsing.cpp
        src/cli/CliSessionProcess.cpp
        src/cli/CliSessionRun.cpp
        src/cli/LuaPrelude.cpp
        src/cli/LuaRunner.cpp
        src/platform/linux/LinuxPng.cpp
        tests/CliTests.cpp
        tests/ControlSessionTests.cpp
        tests/CoreTests.cpp
        tests/DaemonDispatchTests.cpp
        tests/DaemonTests.cpp
        tests/InferenceTests.cpp
        tests/TestSupport.cpp
    )

    computer_cpp_target_defaults(computer_cpp_tests)

    target_include_directories(computer_cpp_tests
        PRIVATE
            src/cli
            src/core
            src/daemon
            src/platform/linux
    )

    target_link_libraries(computer_cpp_tests
        PRIVATE
            computer_cpp_core
    )

    add_test(NAME computer_cpp_tests COMMAND computer_cpp_tests)
endif()
