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

project(SimMasterQt VERSION ${PROJECT_VERSION} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Xml)

set(SOURCES
    src/main.cpp
    src/util/pinyin_search.cpp
    src/scd/scd_model.cpp
    src/mms/mms_client_bridge.cpp
    src/event/event_store.cpp
    src/event/station_event_list_widget.cpp
    src/ui/ied_tree_widget.cpp
    src/ui/report_ctrl_panel.cpp
    src/ui/mainwindow.cpp
)

if(WIN32)
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/sim_master_qt.rc.in"
                   "${CMAKE_CURRENT_BINARY_DIR}/sim_master_qt.rc" @ONLY)
    list(APPEND SOURCES "${CMAKE_CURRENT_BINARY_DIR}/sim_master_qt.rc")
endif()

add_executable(sim_master_qt ${SOURCES})

target_compile_definitions(sim_master_qt PRIVATE
    APP_VERSION_DISPLAY="${APP_VERSION_DISPLAY}"
)

target_include_directories(sim_master_qt PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(sim_master_qt PRIVATE Qt6::Widgets Qt6::Xml)

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

set(CPACK_PACKAGE_NAME "61850仿真主站")
set(CPACK_PACKAGE_VENDOR "master_sim")
set(CPACK_PACKAGE_VERSION "${APP_VERSION_DISPLAY}")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "SimMasterQt")
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
    set(CPACK_NSIS_DISPLAY_NAME "61850仿真主站 ${APP_VERSION_DISPLAY}")
    set(CPACK_NSIS_PACKAGE_NAME "61850仿真主站 ${APP_VERSION_DISPLAY}")
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
endif()

include(CPack)
