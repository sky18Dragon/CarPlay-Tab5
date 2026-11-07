// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stddef.h>
#include <stdint.h>

// USB client task owns packets, sequence numbers and connection state changes.
// The Lockdown worker uses bounded blocking byte streams through a command queue.
void mux_channels_transport(bool attached);
void mux_channels_ready();
void mux_channels_poll();
void mux_channels_receive(const uint8_t *tcp, size_t size);
uint32_t mux_transport_generation();
bool mux_transport_alive(uint32_t generation);
uint32_t mux_stream_open(uint16_t port, uint32_t generation);
int mux_stream_read(uint32_t token, uint8_t *out, size_t size, unsigned timeout_ms);
bool mux_stream_write(uint32_t token, const uint8_t *data, size_t size);
void mux_stream_close(uint32_t token);

bool usbmux_send_tcp_packet(uint16_t local_port, uint16_t remote_port,
                           uint32_t sequence, uint32_t acknowledgement,
                           uint8_t flags, const uint8_t *data, size_t size);

bool mux_worker_drained();
