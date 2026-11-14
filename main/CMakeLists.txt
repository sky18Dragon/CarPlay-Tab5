idf_component_register(
    SRCS
        "core/session.c"
        "core/video_decoder.cpp"
        "core/pixel_convert.c"
        "core/pixel_pie.S"
        "core/pcm_buffer.c"
        "core/audio_resample.c"
        "core/video_queue.c"
        "core/touch.c"
        "core/rtsp_stream.c"
        "app_main.c"
        "sd_log.c"
        "power_status.c"
        "app_session.c"
        "entropy_source.c"
        "airplay_service.c"
        "hid_output.cpp"
        "media_routes.cpp"
        "ncm_host.cpp"
        "core/ncm_descriptor.c"
        "app_ui.c"
        "pairing_reset.c"
        "video_output.cpp"
        "audio_output.c"
        "audio_routes.c"
        "usb_probe.c"
        "usb_hal_fix.c"
        "usb_descriptors.c"
        "usb_config_scan.c"
        "usbmux.cpp"
        "mux_channels.cpp"
        "lockdown_client.cpp"
        "iap2_client.cpp"
        "vendor/iap2_core.c"
    INCLUDE_DIRS
        "."
    REQUIRES
        "esp_driver_i2c"
        "esp_app_format"
        "auth_software_provider"
        "airplay_software_provider"
        "airplay_session"
        "esp_netif"
        "esp_event"
        "openh264_sw"
        "m5stack_tab5"
        "esp_timer"
        "usb"
        "mbedtls"
        "nvs_flash"
        "bootloader_support"
        "esp_driver_ppa"
        "esp_lcd"
        "esp_lvgl_port"
        "esp_mm"
)

# Runs on every video frame; the project default is -Og.
set_source_files_properties(core/pixel_convert.c PROPERTIES COMPILE_OPTIONS "-O2")

# app_ui.c takes the panel handle that the BSP does not expose.
target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=lvgl_port_add_disp_dsi")
# usb_hal_fix.c: ESP-IDF v5.5.3 asserts on a channel error without halt (esp-idf#19111).
target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=usb_dwc_hal_chan_decode_intr"
    "-Wl,--wrap=usb_dwc_hal_chan_activate"
    "-Wl,--undefined=__wrap_usb_dwc_hal_chan_decode_intr")
# sd_log.c records the panic reason before ESP-IDF prints it and reboots.
target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=esp_panic_handler"
    "-Wl,--undefined=__wrap_esp_panic_handler")
