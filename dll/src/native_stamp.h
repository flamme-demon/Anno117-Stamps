#ifndef ANNO117_NATIVE_STAMP_H
#define ANNO117_NATIVE_STAMP_H
#include <stddef.h>
#include <stdint.h>

#define NATIVE_STAMP_BUILDINGS 4096
#define NATIVE_STAMP_ROADS 256
#define NATIVE_STAMP_MAX_FILE (16u * 1024 * 1024)
#define NATIVE_STAMP_POLYGONS 256
#define NATIVE_STAMP_POLYGON_BYTES (1024u * 1024)

typedef struct {
    uint32_t guid, preview_type;
    int32_t origin[2];
    uint32_t width, height, bits_offset, bits_size;
    float relative[2];
    uint64_t owner;
    int32_t rect[4];
    uint32_t has_rect, blueprint;
} NativePolygon;

/* Engine input, built in memory only. Never serialized to disk. */
typedef struct {
    uint8_t entries[NATIVE_STAMP_BUILDINGS][0x60];
    struct { uint32_t guid; int32_t x1, z1, x2, z2; } roads[NATIVE_STAMP_ROADS];
    uint32_t count, road_count, tiles;
    int32_t bbox[4], aabb[4];
    float centre[2];
    uint32_t icon_guid;
    uint32_t polygon_count, polygon_bytes;
    NativePolygon polygons[NATIVE_STAMP_POLYGONS];
    uint8_t polygon_bits[NATIVE_STAMP_POLYGON_BYTES];
} NativeStamp;

/* Transactional: out is unchanged on failure. Input bytes are read-only. */
int native_stamp_read(const void* compressed, size_t size, NativeStamp* out,
                      char* error, size_t error_size);
/* Patch the existing four-byte root IconGUID in the original decoded buffer.
 * Dictionaries, unknown nodes, padding and the native trailer remain intact.
 * The returned native zlib/FileDB file belongs to the caller. */
int native_stamp_set_icon(const void* compressed, size_t size, uint32_t icon,
                         void** output, size_t* output_size, char* error, size_t error_size);
void native_stamp_free(void* buffer);
#endif
