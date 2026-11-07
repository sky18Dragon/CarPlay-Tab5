#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
mkdir -p artifacts
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g -Imain main/usb_descriptors.c tests/test_usb_descriptors.c -o artifacts/test_usb_descriptors
./artifacts/test_usb_descriptors
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g -Itests/stubs -Imain main/usb_config_scan.c main/core/ncm_descriptor.c main/usb_descriptors.c tests/test_usb_config_scan.c -o artifacts/test_usb_config_scan
./artifacts/test_usb_config_scan
c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -g -Imain tests/test_lockdown.cpp -o artifacts/test_lockdown
./artifacts/test_lockdown
c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -g -Imain tests/test_lockdown_xml.cpp -o artifacts/test_lockdown_xml
./artifacts/test_lockdown_xml
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g -Imain/vendor main/vendor/iap2_core.c tests/iap2_core_test.c -o artifacts/test_iap2_core
./artifacts/test_iap2_core

cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g -Imain main/core/session.c tests/test_session.c -o artifacts/test_session
./artifacts/test_session

c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -g -Imain/core tests/test_ntb16.cpp -o artifacts/test_ntb16
./artifacts/test_ntb16
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g -Imain main/core/airplay_rtsp_codec.c main/core/rtsp_stream.c tests/test_rtsp_stream.c -o artifacts/test_rtsp_stream
./artifacts/test_rtsp_stream

cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g -Imain main/core/touch.c tests/test_touch.c -o artifacts/test_touch
./artifacts/test_touch

cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g -Imain main/core/pcm_buffer.c main/core/video_queue.c tests/test_media_buffers.c -o artifacts/test_media_buffers
./artifacts/test_media_buffers
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g -Imain -Imain/core main/core/pixel_convert.c tests/test_pixel_convert.c -o artifacts/test_pixel_convert
./artifacts/test_pixel_convert

cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g -Itests/profile_stubs -Icomponents/airplay_control -Icomponents/airplay_control/include tests/test_video_profile.c -o artifacts/test_video_profile
./artifacts/test_video_profile
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g -Imain main/core/ncm_descriptor.c main/usb_descriptors.c tests/test_ncm_descriptor.c -o artifacts/test_ncm_descriptor
./artifacts/test_ncm_descriptor

cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g -Icomponents/airplay_rtsp/include components/airplay_rtsp/airplay_bplist.c tests/test_airplay_bplist.c -o artifacts/test_airplay_bplist
./artifacts/test_airplay_bplist
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g -Icomponents/airplay_screen/include components/airplay_screen/airplay_screen_codec.c tests/test_airplay_screen_codec.c -o artifacts/test_airplay_screen_codec
./artifacts/test_airplay_screen_codec
python3 scripts/configure_tab5_info.py --check

cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g -Itests/session_stubs -Itests/stubs -Imain main/app_session.c main/core/session.c tests/test_app_session.c -o artifacts/test_app_session
./artifacts/test_app_session

c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -g -Imain/core tests/test_iap2_params.cpp -o artifacts/test_iap2_params
./artifacts/test_iap2_params

c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -g -Imain tests/test_iap2_auth.cpp -o artifacts/test_iap2_auth
./artifacts/test_iap2_auth

c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -g -DCONFIG_TAB5_IAP2_NCM_INTERFACE=3 -Itests/iap2_stubs -Itests/stubs -Imain -Icomponents/auth_port/include main/iap2_client.cpp tests/test_iap2_client.cpp -o artifacts/test_iap2_client
./artifacts/test_iap2_client
c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -g -DCONFIG_TAB5_IAP2_NCM_INTERFACE=-1 -Itests/iap2_stubs -Itests/stubs -Imain -Icomponents/auth_port/include main/iap2_client.cpp tests/test_iap2_client.cpp -o artifacts/test_iap2_unconfigured
./artifacts/test_iap2_unconfigured
