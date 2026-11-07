#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef void (*descriptor_visitor_t)(const uint8_t *, size_t, void *);
/* Validates the entire configuration before invoking visitor; no partial dispatch. */
bool usb_descriptors_walk(const uint8_t *data, size_t size, descriptor_visitor_t visit, void *ctx);
