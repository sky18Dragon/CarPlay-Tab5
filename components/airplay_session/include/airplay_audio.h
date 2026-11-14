/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "airplay_bplist.h"
#include "airplay_rtsp_pairing.h"
#include "lwip/sockets.h"
/* peer: the phone's RTSP address, used to send the microphone uplink to its dataPort. */
bool airplay_audio_start(const airplay_rtsp_pairing_t *pairing,
    const airplay_bplist_doc_t *doc,airplay_bplist_object_t stream,
    const struct sockaddr_in6 *peer,airplay_stream_ports_t *ports);
void airplay_audio_stop(unsigned type);
/* The phone's duckAudio (true) / unduckAudio (false) around navigation prompts. */
void airplay_audio_duck(bool ducked);
