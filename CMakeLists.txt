cmake_minimum_required(VERSION 3.16)
file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/version.txt" PROJECT_VER LIMIT_COUNT 1)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# main/usb_hal_fix.c and sd_log.c replace ESP-IDF internals with --wrap. Their
# signatures and behaviour are only verified against this release, and a mismatch
# would link cleanly and fail at run time.
set(TAB5_REQUIRED_IDF "5.5.3")
if(NOT "${IDF_VERSION_MAJOR}.${IDF_VERSION_MINOR}.${IDF_VERSION_PATCH}" STREQUAL TAB5_REQUIRED_IDF
   AND NOT TAB5_ALLOW_OTHER_IDF)
    message(FATAL_ERROR "CarPlay-Tab5 requires ESP-IDF v${TAB5_REQUIRED_IDF}, found "
        "v${IDF_VERSION_MAJOR}.${IDF_VERSION_MINOR}.${IDF_VERSION_PATCH}. The USB HAL and panic "
        "handler wraps must be re-checked before moving; pass -DTAB5_ALLOW_OTHER_IDF=ON to try anyway.")
endif()
project(carplay_tab5)
