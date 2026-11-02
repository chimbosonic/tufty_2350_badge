// Host-preview stub: just enough of pico-keys-sdk for tufty/ui.c to compile.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
typedef struct { uint16_t fid; const uint8_t *data; uint32_t size; } file_t;
#define SPECIFY_EF 1
file_t *file_search_by_fid(uint16_t fid, const file_t *parent, uint8_t sp);
static inline bool file_has_data(const file_t *f) { return f && f->data && f->size; }
static inline uint8_t *file_get_data(const file_t *f) { return (uint8_t *)f->data; }
static inline uint32_t file_get_size(const file_t *f) { return f->size; }
