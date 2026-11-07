/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "airplay_bplist.h"
#include "airplay_rtsp_pairing.h"
bool airplay_audio_start(const airplay_rtsp_pairing_t *pairing,
    const airplay_bplist_doc_t *doc,airplay_bplist_object_t stream,
    airplay_stream_ports_t *ports);
void airplay_audio_stop(unsigned type);
