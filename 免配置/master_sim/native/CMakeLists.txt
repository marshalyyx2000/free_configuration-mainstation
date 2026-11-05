cmake_minimum_required(VERSION 3.15)
project(mms_master_cli C)

set(LIBIEC61850_ROOT "${CMAKE_CURRENT_LIST_DIR}/../third_party/libiec61850")
set(LIBIEC61850_BUILD "${CMAKE_CURRENT_LIST_DIR}/../build-win")

add_executable(mms_master_cli mms_master_cli.c ied_client_error.c)

target_include_directories(mms_master_cli PRIVATE
    "${LIBIEC61850_ROOT}/src/iec61850/inc"
    "${LIBIEC61850_ROOT}/src/common/inc"
    "${LIBIEC61850_ROOT}/src/mms/inc"
    "${LIBIEC61850_ROOT}/hal/inc"
    "${LIBIEC61850_ROOT}/src/logging"
)

if(WIN32)
    target_link_libraries(mms_master_cli PRIVATE ws2_32 iphlpapi)
endif()

set(IEC61850_LIB "${LIBIEC61850_BUILD}/src/Release/iec61850.lib")
set(HAL_LIB "${LIBIEC61850_BUILD}/hal/Release/hal.lib")
if(NOT EXISTS "${IEC61850_LIB}")
    set(IEC61850_LIB "${LIBIEC61850_BUILD}/src/Debug/iec61850.lib")
    set(HAL_LIB "${LIBIEC61850_BUILD}/hal/Debug/hal.lib")
endif()
if(NOT EXISTS "${IEC61850_LIB}" OR NOT EXISTS "${HAL_LIB}")
    message(FATAL_ERROR "Build libiec61850 first (missing iec61850.lib or hal.lib under ${LIBIEC61850_BUILD})")
endif()

target_link_libraries(mms_master_cli PRIVATE "${IEC61850_LIB}" "${HAL_LIB}")

# Child of sim_master_qt via QProcess — no console window when started
if(MSVC)
  set_target_properties(mms_master_cli PROPERTIES LINK_FLAGS "/SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup")
endif()
