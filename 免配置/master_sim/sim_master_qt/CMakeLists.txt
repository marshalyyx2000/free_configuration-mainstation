cmake_minimum_required(VERSION 3.21)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" APP_VERSION_RAW)
string(STRIP "${APP_VERSION_RAW}" APP_VERSION_DISPLAY)
if(DEFINED APP_VERSION AND NOT APP_VERSION STREQUAL "")
  set(APP_VERSION_DISPLAY "${APP_VERSION}")
else()
  set(APP_VERSION "${APP_VERSION_DISPLAY}")
endif()

string(REPLACE "." ";" _VER_PARTS "${APP_VERSION_DISPLAY}")
list(LENGTH _VER_PARTS _VER_LEN)
list(GET _VER_PARTS 0 PROJECT_VERSION_MAJOR)
if(_VER_LEN GREATER 1)
  list(GET _VER_PARTS 1 _MINOR_STR)
  math(EXPR PROJECT_VERSION_MINOR "${_MINOR_STR}" OUTPUT_FORMAT DECIMAL)
else()
  set(PROJECT_VERSION_MINOR 0)
endif()
set(PROJECT_VERSION_PATCH 0)
set(PROJECT_VERSION "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}")

project(SimMasterQt VERSION ${PROJECT_VERSION} LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Xml Network Concurrent)

add_library(miniz_vendored STATIC
    third_party/miniz/miniz.c
    third_party/miniz/miniz_tdef.c
    third_party/miniz/miniz_tinfl.c
    third_party/miniz/miniz_zip.c
)
target_include_directories(miniz_vendored PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party/miniz)
if(MSVC)
  target_compile_definitions(miniz_vendored PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

set(SOURCES
    src/main.cpp
    src/util/app_paths.cpp
    src/util/pinyin_search.cpp
    src/settings/shared_connection_settings.cpp
    src/scd/scd_model.cpp
    src/mms/mms_client_bridge.cpp
    src/mms/comm_state_support.cpp
    src/cli/cli_console.cpp
    src/cli/headless_main.cpp
    src/cli/mms_session_server.cpp
    src/cli/mms_session_client.cpp
    src/event/event_store.cpp
    src/event/report_event_logic.cpp
    src/event/event_format.cpp
    src/event/station_event_list_widget.cpp
    src/ops/config_generator.cpp
    src/ops/station_zip_writer.cpp
    src/ops/iec104_maintain_client.cpp
    src/ops/lab_operations.cpp
    src/ui/ied_tree_widget.cpp
    src/ui/report_ctrl_panel.cpp
    src/ui/lab_worker.cpp
    src/ui/ops_deploy_widget.cpp
    src/ui/mms_master_widget.cpp
    src/ui/master_sim_shell.cpp
)

if(WIN32)
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/sim_master_qt.rc.in"
                   "${CMAKE_CURRENT_BINARY_DIR}/sim_master_qt.rc" @ONLY)
    list(APPEND SOURCES "${CMAKE_CURRENT_BINARY_DIR}/sim_master_qt.rc")
endif()

add_executable(sim_master_qt WIN32 ${SOURCES})

if(WIN32)
  set_target_properties(sim_master_qt PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

target_compile_definitions(sim_master_qt PRIVATE
    APP_VERSION_DISPLAY="${APP_VERSION_DISPLAY}"
)

target_include_directories(sim_master_qt PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/miniz
)

target_link_libraries(sim_master_qt PRIVATE Qt6::Widgets Qt6::Xml Qt6::Network Qt6::Concurrent miniz_vendored)
if(WIN32)
  target_link_libraries(sim_master_qt PRIVATE ws2_32)
endif()

install(TARGETS sim_master_qt RUNTIME DESTINATION bin)

# Always register the CLI install rule. EXISTS at configure time would skip
# packing mms_master_cli.exe if cmake ran before the native CLI was built.
set(MMS_CLI "${CMAKE_CURRENT_LIST_DIR}/../bin/mms_master_cli.exe")
install(FILES "${MMS_CLI}" DESTINATION bin OPTIONAL)

set(IED_LIST "${CMAKE_CURRENT_LIST_DIR}/../out/IEDList.xml")
if(EXISTS "${IED_LIST}")
  install(FILES "${IED_LIST}" DESTINATION share/sim_master_qt)
endif()
set(SMCD "${CMAKE_CURRENT_LIST_DIR}/../out/station.SMCD")
# The bundled model is only a fallback for opening a device tree before the
# first 生成配置. Real stations can reach ~100 MB, which must not end up inside
# the installer, so skip anything above the cap.
set(_SMCD_BUNDLE_MAX_MB 24)
if(EXISTS "${SMCD}")
  file(SIZE "${SMCD}" _SMCD_SIZE)
  math(EXPR _SMCD_LIMIT "${_SMCD_BUNDLE_MAX_MB} * 1024 * 1024")
  if(_SMCD_SIZE LESS _SMCD_LIMIT)
    install(FILES "${SMCD}" DESTINATION share/sim_master_qt)
  else()
    message(STATUS "跳过打包 station.SMCD（${_SMCD_SIZE} 字节超过 ${_SMCD_BUNDLE_MAX_MB} MB 上限）")
  endif()
endif()

if(WIN32)
  find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${Qt6_DIR}/bin" "${CMAKE_PREFIX_PATH}/bin")
  if(WINDEPLOYQT_EXECUTABLE)
    install(CODE "
      set(_exe \"\${CMAKE_INSTALL_PREFIX}/bin/sim_master_qt.exe\")
      if(DEFINED ENV{DESTDIR})
        set(_exe \"\$ENV{DESTDIR}\${_exe}\")
      endif()
      execute_process(COMMAND \"${WINDEPLOYQT_EXECUTABLE}\"
        --no-translations --no-compiler-runtime \"\${_exe}\"
        RESULT_VARIABLE _wd_rc ERROR_VARIABLE _wd_err)
      if(NOT _wd_rc EQUAL 0)
        message(FATAL_ERROR \"windeployqt failed: \${_wd_err}\")
      endif()
    ")
  endif()
endif()

set(CPACK_PACKAGE_NAME "SimMasterQt")
set(CPACK_PACKAGE_VENDOR "master_sim")
set(CPACK_PACKAGE_VERSION "${APP_VERSION_DISPLAY}")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "SimMasterQt")
# NSIS: always the native 64-bit Program Files (never LocalAppData).
# $PROGRAMFILES64 is expanded by NSIS itself, so the installer shows a clean
# "C:\Program Files\SimMasterQt" instead of a mixed "C:/Program Files\..." path.
set(CPACK_NSIS_INSTALL_ROOT "$PROGRAMFILES64")
# Must appear before MUI_PAGE_DIRECTORY in CPack template (injected at @CPACK_NSIS_DEFINES@).
# VERIFYONLEAVE keeps "Next" enabled; default DirVerify disables it for Program Files.
# The helper script is copied next to project.nsi because makensis resolves
# !include relative to the generated script (and CPack does not pass include dirs).
set(CPACK_NSIS_DEFINES [[
!define MUI_DIRECTORYPAGE_VERIFYONLEAVE
!define MUI_STARTMENUPAGE_DEFAULTFOLDER SimMasterQt
!include cpack_nsis_include.nsh
]])
set(CPACK_NSIS_EXTRA_PREINSTALL_COMMANDS "Call simMasterStopRunning")
# windeployqt creates plugin sub-folders (platforms, imageformats, ...) that are
# not part of the CMake install manifest, so CPack's generated Delete/RMDir list
# leaves them behind.  Remove the whole tree, but only when the folder really is
# our own install root (never a folder the user picked for something else).
set(CPACK_NSIS_EXTRA_UNINSTALL_COMMANDS [[
  StrCpy $0 $INSTDIR 11 -11
  StrCmp $0 SimMasterQt 0 +2
    RMDir /r $INSTDIR
]])
set(CPACK_NSIS_UNINSTALL_NAME "Uninstall SimMasterQt")
set(CPACK_PACKAGE_FILE_NAME "SimMasterQt-${APP_VERSION_DISPLAY}-win64")
if(DEFINED SIMMASTER_CPACK_DIR)
  set(CPACK_PACKAGE_DIRECTORY "${SIMMASTER_CPACK_DIR}")
elseif(DEFINED ENV{SIMMASTER_CPACK_DIR})
  set(CPACK_PACKAGE_DIRECTORY "$ENV{SIMMASTER_CPACK_DIR}")
else()
  set(CPACK_PACKAGE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/dist")
endif()
if(WIN32)
    set(CPACK_GENERATOR "NSIS")
    # ASCII only in NSIS metadata (UTF-8 here becomes mojibake in Start Menu shortcuts)
    set(CPACK_NSIS_DISPLAY_NAME "SimMasterQt ${APP_VERSION_DISPLAY}")
    set(CPACK_NSIS_PACKAGE_NAME "SimMasterQt ${APP_VERSION_DISPLAY}")
    # In-place upgrade: kill running exes before copying (see cpack_nsis_include.nsh).
    # Uninstall-before-install fails when Qt DLLs are still locked by sim_master_qt.
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL OFF)
    # Start Menu shortcut must point at bin\sim_master_qt.exe (the payload lives in bin).
    set(CPACK_NSIS_MENU_LINKS "bin/sim_master_qt.exe" "SimMasterQt")
endif()

include(CPack)

# makensis resolves "!include cpack_nsis_include.nsh" against the directory that
# holds project.nsi inside the CPack staging tree.  Drop the helper there so the
# packaging step is self-contained (no manual copy, no stale copy on rebuild).
if(WIN32)
  if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(_simmaster_cpack_sys "win64")
  else()
    set(_simmaster_cpack_sys "win32")
  endif()
  set(_simmaster_nsis_stage
      "${CPACK_PACKAGE_DIRECTORY}/_CPack_Packages/${_simmaster_cpack_sys}/NSIS")
  file(MAKE_DIRECTORY "${_simmaster_nsis_stage}")
  configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/cpack_nsis_include.nsh"
    "${_simmaster_nsis_stage}/cpack_nsis_include.nsh"
    COPYONLY)
endif()
