cmake_minimum_required(VERSION 3.21)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" APP_VERSION_RAW)
string(STRIP "${APP_VERSION_RAW}" APP_VERSION_DISPLAY)
if(NOT DEFINED APP_VERSION OR APP_VERSION STREQUAL "")
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

find_package(Qt6 REQUIRED COMPONENTS Widgets Xml Network)

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
    src/event/event_store.cpp
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

add_executable(sim_master_qt ${SOURCES})

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

target_link_libraries(sim_master_qt PRIVATE Qt6::Widgets Qt6::Xml Qt6::Network miniz_vendored)
if(WIN32)
  target_link_libraries(sim_master_qt PRIVATE ws2_32)
endif()

install(TARGETS sim_master_qt RUNTIME DESTINATION bin)

set(MMS_CLI "${CMAKE_CURRENT_LIST_DIR}/../bin/mms_master_cli.exe")
if(EXISTS "${MMS_CLI}")
  install(FILES "${MMS_CLI}" DESTINATION bin)
endif()

set(IED_LIST "${CMAKE_CURRENT_LIST_DIR}/../out/IEDList.xml")
if(EXISTS "${IED_LIST}")
  install(FILES "${IED_LIST}" DESTINATION share/sim_master_qt)
endif()
set(SMCD "${CMAKE_CURRENT_LIST_DIR}/../out/station.SMCD")
if(EXISTS "${SMCD}")
  install(FILES "${SMCD}" DESTINATION share/sim_master_qt)
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
# Same tree as install_sim_master.ps1 (forward slashes only — CPack treats \ as escape)
if(DEFINED ENV{LOCALAPPDATA})
  set(_simmaster_nsis_root "$ENV{LOCALAPPDATA}/Programs")
else()
  set(_simmaster_nsis_root "$ENV{USERPROFILE}/Programs")
endif()
string(REPLACE "\\" "/" _simmaster_nsis_root "${_simmaster_nsis_root}")
set(CPACK_NSIS_INSTALL_ROOT "${_simmaster_nsis_root}")
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
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL OFF)
    set(CPACK_NSIS_MENU_LINKS "sim_master_qt.exe" "SimMasterQt")
endif()

include(CPack)
