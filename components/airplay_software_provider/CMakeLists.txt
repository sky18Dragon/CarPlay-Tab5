idf_component_register(
    SRCS "airplay_software_provider.c"
    INCLUDE_DIRS "include"
    REQUIRES airplay_identity_port airplay_pairing_store airplay_control
             ed25519 esp_common esp_hw_support log mbedtls nvs_flash
)

foreach(height 192 240 288 360 384 480 576)
    target_add_binary_data(${COMPONENT_LIB} "${CMAKE_CURRENT_SOURCE_DIR}/assets/info_${height}.plist" BINARY)
endforeach()
