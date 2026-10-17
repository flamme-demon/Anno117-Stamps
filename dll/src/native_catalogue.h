#ifndef ANNO117_NATIVE_CATALOGUE_H
#define ANNO117_NATIVE_CATALOGUE_H
#include <windows.h>
#include <stdint.h>

#define LIVE_GUID_MIN 1500100000u
#define LIVE_PROTOCOL (LIVE_GUID_MIN + 2)
#define LIVE_REVISION (LIVE_GUID_MIN + 3)
#define LIVE_REFRESH (LIVE_GUID_MIN + 4)
#define LIVE_COUNT (LIVE_GUID_MIN + 5)
#define LIVE_ERRORS (LIVE_GUID_MIN + 6)
#define LIVE_PRESENTATION (LIVE_GUID_MIN + 9)
#define LIVE_ROW_GUID (LIVE_GUID_MIN + 0x1000)
#define LIVE_ROW_ICON (LIVE_GUID_MIN + 0x2000)
#define LIVE_NAME_LENGTH (LIVE_GUID_MIN + 0x3000)
#define LIVE_ICON_LENGTH (LIVE_GUID_MIN + 0x4000)
#define LIVE_COMPONENT_COUNT (LIVE_GUID_MIN + 0x5000)
#define LIVE_BUILDING_COUNT (LIVE_GUID_MIN + 0x6000)
#define LIVE_ROAD_TILES (LIVE_GUID_MIN + 0x7000)
#define LIVE_POLYGON_COUNT (LIVE_GUID_MIN + 0x8000)
#define LIVE_NAME_BYTES (LIVE_GUID_MIN + 0x10000)
#define LIVE_ICON_BYTES (LIVE_GUID_MIN + 0x100000)
#define LIVE_TEXT_STRIDE 256u
#define LIVE_POOL_BASE (LIVE_GUID_MIN + 0x200000)
#define LIVE_FOLDERS 16u
#define LIVE_STAMPS 64u
#define LIVE_SLOT_STRIDE (LIVE_STAMPS + 1)
#define LIVE_FOLDER_COUNT (2 * LIVE_FOLDERS)
#define LIVE_RECORD_COUNT (LIVE_FOLDER_COUNT * LIVE_SLOT_STRIDE)
#define LIVE_COMPONENT_STRIDE 4097u
#define LIVE_COMPONENT_GUID (LIVE_GUID_MIN + 0x400000)
#define LIVE_COMPONENT_AMOUNT (LIVE_GUID_MIN + 0xc30000)

typedef struct { uint32_t guid, amount; } LiveComponent;

typedef struct {
    char path[1024], name[256], icon_file[512];
    uint32_t icon_guid;
    LiveComponent* components;
    uint32_t component_count, building_count, road_tiles, polygon_count;
    uint64_t modified, size;
    int assigned, active, seen;
} LiveRecord;
typedef struct {
    LiveRecord records[LIVE_RECORD_COUNT];
    uint32_t rows[LIVE_RECORD_COUNT], row_count, revision, errors;
    int initialized, dirty, registry_dirty;
    char last_error[512];
    ULONGLONG scanned;
    char* icons;
    size_t icons_size;
    uint32_t folder_icons[LIVE_FOLDER_COUNT];
} NativeCatalogue;

/* Inventory reads stamps and folder-icons.json; slots.tsv stores stable identities. */
int native_catalogue_refresh(NativeCatalogue* c, const WCHAR* root, const WCHAR* icons,
                             const WCHAR* registry, ULONGLONG now, int force);
const LiveRecord* native_catalogue_find(const NativeCatalogue* c, uint32_t guid);
uint32_t native_catalogue_query(const NativeCatalogue* c, uint32_t command);
int native_catalogue_is_query(uint32_t command);
void native_catalogue_dispose(NativeCatalogue* c);
int native_catalogue_save(const NativeCatalogue* c, const WCHAR* registry);
const char* native_catalogue_icon(const NativeCatalogue* c,uint32_t guid);
int native_folder_icons_read(const NativeCatalogue* c,const WCHAR* registry,uint32_t* values);
int native_library_set_icon(NativeCatalogue* c,const WCHAR* root,const WCHAR* registry,
    uint32_t guid,uint32_t icon,char* error,size_t size);

/* Explicit name/location actions. Icon edits are isolated in native_icons.c. */
uint32_t native_library_create_folder(NativeCatalogue* c, const WCHAR* root,
    const WCHAR* registry, uint32_t region, const WCHAR* name, char* error, size_t size);
int native_library_rename(NativeCatalogue* c, const WCHAR* root, const WCHAR* registry,
    uint32_t guid, const WCHAR* name, char* error, size_t size);
int native_library_trash(NativeCatalogue* c, const WCHAR* root, const WCHAR* registry,
    const WCHAR* trash, uint32_t guid, char* error, size_t size);
int native_library_path(const WCHAR* root, const LiveRecord* record, WCHAR* path, size_t count);
int native_library_valid_name(const WCHAR* name);
#endif
