cmake_minimum_required(VERSION 3.16)
project(tab5_crypto_test C)
get_filename_component(root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
set(MBEDTLS_FATAL_WARNINGS OFF CACHE BOOL "" FORCE)
set(IDF_MBEDTLS "$ENV{IDF_PATH}/components/mbedtls/mbedtls" CACHE PATH "SDK Mbed TLS source (export ESP-IDF first)")
if(NOT EXISTS "${IDF_MBEDTLS}/CMakeLists.txt")
 message(FATAL_ERROR "Export ESP-IDF v5.5.3 or set IDF_MBEDTLS to its Mbed TLS source")
endif()
add_subdirectory("${IDF_MBEDTLS}" mbedtls)
add_executable(test_crypto test_crypto.c
 "${root}/main/core/rtsp_stream.c"
 "${root}/main/core/airplay_rtsp_codec.c"
 "${root}/components/airplay_screen/airplay_screen_stream.c"
 "${root}/components/airplay_screen/airplay_screen_codec.c"
 "${root}/components/airplay_control/airplay_control_cipher.c")
target_include_directories(test_crypto PRIVATE stubs "${root}/main/core" "${root}/components/airplay_screen/include" "${root}/components/airplay_control/include")
target_compile_definitions(test_crypto PRIVATE CONFIG_MBEDTLS_CHACHAPOLY_C=1 CONFIG_MBEDTLS_HKDF_C=1)
target_compile_options(test_crypto PRIVATE -Wall -Wextra -Werror -UNDEBUG -fsanitize=address,undefined)
target_link_options(test_crypto PRIVATE -fsanitize=address,undefined)
target_link_libraries(test_crypto PRIVATE mbedcrypto)
