cmake_minimum_required(VERSION 3.26)

project(streamping VERSION 0.5.1 LANGUAGES CXX)

set(STREAMPING_CONNECT_SERVICE_URL "" CACHE STRING
    "Base URL of the StreamPing Discord OAuth service")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(libobs CONFIG REQUIRED)
find_package(obs-frontend-api CONFIG REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Core Widgets)

qt_standard_project_setup()

add_library(streamping MODULE)

target_sources(
  streamping
  PRIVATE
    src/chzzk-client.cpp
    src/chzzk-client.hpp
    src/discord-client.cpp
    src/discord-client.hpp
    src/discord-message.cpp
    src/discord-message.hpp
    src/discord-connection-client.cpp
    src/discord-connection-client.hpp
    src/http-client.cpp
    src/http-client.hpp
    src/message-template.cpp
    src/message-template.hpp
    src/notifier.cpp
    src/notifier.hpp
    src/plugin-main.cpp
    src/settings-dialog.cpp
    src/settings-dialog.hpp
    src/settings.cpp
    src/settings.hpp
    src/update-checker.cpp
    src/update-checker.hpp
    src/validation.cpp
    src/x-client.cpp
    src/x-client.hpp
)

target_link_libraries(
  streamping
  PRIVATE
    OBS::libobs
    OBS::obs-frontend-api
    Qt6::Core
    Qt6::Widgets
    crypt32
    winhttp
)

target_compile_definitions(
  streamping
  PRIVATE
    STREAMPING_VERSION="${PROJECT_VERSION}"
    STREAMPING_CONNECT_SERVICE_URL="${STREAMPING_CONNECT_SERVICE_URL}"
)

set_target_properties(
  streamping
  PROPERTIES
    AUTOMOC ON
    AUTOUIC ON
    AUTORCC ON
    PREFIX ""
)

if(MSVC)
  target_compile_options(streamping PRIVATE /W4 /permissive- /utf-8)
else()
  target_compile_options(streamping PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(GNUInstallDirs)
install(
  TARGETS streamping
  LIBRARY DESTINATION "obs-plugins/64bit"
  RUNTIME DESTINATION "obs-plugins/64bit"
)
install(DIRECTORY data/ DESTINATION "data/obs-plugins/streamping")
install(FILES install.cmd DESTINATION ".")
install(FILES DISTRIBUTION.md DESTINATION "." RENAME README.md)
install(FILES CHANGELOG.md DESTINATION ".")
install(FILES scripts/install-windows.ps1 DESTINATION "scripts")

if(WIN32)
  find_program(POWERSHELL_EXECUTABLE powershell REQUIRED)
  add_custom_target(
    deploy
    COMMAND
      "${POWERSHELL_EXECUTABLE}" -NoProfile -ExecutionPolicy Bypass
      -File "${CMAKE_CURRENT_SOURCE_DIR}/scripts/install-windows.ps1"
      -PluginDll "$<TARGET_FILE:streamping>"
      -LocaleFile "${CMAKE_CURRENT_SOURCE_DIR}/data/locale/ko-KR.ini"
    DEPENDS streamping
    USES_TERMINAL
    COMMENT "Find OBS Studio and update StreamPing"
  )
endif()

include(CTest)
if(BUILD_TESTING)
  add_executable(
    streamping-tests
    tests/logic-tests.cpp
    src/discord-message.cpp
    src/discord-message.hpp
    src/http-client.cpp
    src/http-client.hpp
    src/message-template.cpp
    src/message-template.hpp
    src/settings.hpp
    src/update-checker.cpp
    src/update-checker.hpp
    src/validation.cpp
    src/x-client.cpp
    src/x-client.hpp
  )
  target_include_directories(streamping-tests PRIVATE src)
  target_link_libraries(streamping-tests PRIVATE Qt6::Core Qt6::Gui winhttp)
  target_compile_definitions(streamping-tests PRIVATE STREAMPING_VERSION="${PROJECT_VERSION}")
  set_target_properties(streamping-tests PROPERTIES AUTOMOC ON)
  add_test(NAME streamping-logic COMMAND streamping-tests)

  add_executable(
    streamping-http-smoke
    tests/http-smoke.cpp
    src/http-client.cpp
    src/http-client.hpp
  )
  target_include_directories(streamping-http-smoke PRIVATE src)
  target_link_libraries(streamping-http-smoke PRIVATE Qt6::Core winhttp)
  target_compile_definitions(
    streamping-http-smoke
    PRIVATE STREAMPING_VERSION="${PROJECT_VERSION}"
  )
  set_target_properties(streamping-http-smoke PROPERTIES AUTOMOC ON)
endif()
