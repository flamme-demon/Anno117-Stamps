/* Live inventory for predeclared native menu slots. Originals remain read-only. */
#include "native_catalogue.h"
#include "native_stamp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

static const WCHAR* regions[] = {L"Region Roman", L"Region Celtic"};
static const char* region_utf8[] = {"Region Roman", "Region Celtic"};

static uint64_t file_time(FILETIME ft) { return ((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime; }
static BYTE* read_file(const WCHAR* path, DWORD limit, DWORD* size) {
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    BYTE* bytes = NULL;
    LARGE_INTEGER length;
    DWORD read;
    *size = 0;
    if (file == INVALID_HANDLE_VALUE) return NULL;
    if (GetFileSizeEx(file, &length) && length.QuadPart >= 0 && length.QuadPart <= limit) {
        bytes = malloc((size_t)length.QuadPart + 1);
        if (bytes && ReadFile(file, bytes, (DWORD)length.QuadPart, &read, NULL) && read == length.QuadPart) {
            bytes[read] = 0;
            *size = read;
        } else { free(bytes); bytes = NULL; }
    }
    CloseHandle(file);
    return bytes;
}
static int utf8(const WCHAR* input, char* output, int capacity) {
    return WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, input, -1, output, capacity, NULL, NULL) != 0;
}
static int safe_component(const WCHAR* name) {
    return name[0] && name[0] != L'.' && !wcspbrk(name, L"\t\r\n/\\:");
}
static int make_path(WCHAR* out, size_t capacity, const WCHAR* root, const WCHAR* child) {
    if (wcslen(root) + wcslen(child) + 2 >= capacity) return 0;
    swprintf(out, capacity, L"%ls\\%ls", root, child);
    return 1;
}
static void set_record(LiveRecord* r, const char* path) {
    const char* name = strrchr(path, '/');
    memset(r, 0, sizeof(*r));
    snprintf(r->path, sizeof(r->path), "%s", path);
    snprintf(r->name, sizeof(r->name), "%s", name ? name + 1 : path);
    r->assigned = 1;
}
const char* native_catalogue_icon(const NativeCatalogue* c, uint32_t guid) {
    const char* line = c->icons;
    if (!line) return "";
    while (line < c->icons + c->icons_size) {
        char* end;
        unsigned long number;
        if (!*line) { ++line; continue; }
        number = strtoul(line, &end, 10);
        if (*end == '\t' && number == guid) return end + 1;
        line += strlen(line) + 1;
    }
    return "";
}
#define icon_file(c,guid) native_catalogue_icon(c,guid)
static int valid_registry_path(uint32_t index, const char* path) {
    uint32_t folder = index / LIVE_SLOT_STRIDE, region = folder / LIVE_FOLDERS;
    const char* slash;
    size_t prefix = strlen(region_utf8[region]);
    if (strlen(path) >= 1024 || strncmp(path, region_utf8[region], prefix) || path[prefix] != '/' ||
        strchr(path, '\\') || strchr(path, '\t') || strchr(path, '\r') || strstr(path, "/.")) return 0;
    slash = strchr(path + prefix + 1, '/');
    if (index % LIVE_SLOT_STRIDE == 0) return !slash && path[prefix + 1];
    return slash && slash[1] && !strchr(slash + 1, '/') && (size_t)(slash - path - prefix - 1) < 256 && strlen(slash + 1) < 256;
}
static void load_registry(NativeCatalogue* c, const WCHAR* path) {
    DWORD size;
    BYTE* bytes = read_file(path, 4 * 1024 * 1024, &size);
    char* line = (char*)bytes;
    while (line && *line) {
        char* next = strchr(line, '\n'), *end;
        unsigned long guid;
        if (next) *next++ = 0;
        guid = strtoul(line, &end, 10);
        if (*end == '\t' && guid >= LIVE_POOL_BASE && guid < LIVE_POOL_BASE + LIVE_RECORD_COUNT) {
            uint32_t index = (uint32_t)guid - LIVE_POOL_BASE;
            char* value = end + 1;
            size_t length = strlen(value);
            if (length && value[length - 1] == '\r') value[--length] = 0;
            if (valid_registry_path(index, value) && !c->records[index].assigned) set_record(&c->records[index], value);
        }
        line = next;
    }
    free(bytes);
}
static int save_registry(const NativeCatalogue* c, const WCHAR* path) {
    WCHAR temporary[32768];
    HANDLE file;
    uint32_t i;
    int ok = 1;
    if (wcslen(path) + 5 >= 32768) return 0;
    swprintf(temporary, 32768, L"%ls.tmp", path);
    file = CreateFileW(temporary, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return 0;
    for (i = 0; i < LIVE_RECORD_COUNT; ++i) {
        const LiveRecord* r = &c->records[i];
        if (r->assigned) {
            char line[1100];
            DWORD written;
            int length = snprintf(line, sizeof(line), "%u\t%s\n", LIVE_POOL_BASE + i, r->path);
            if (length < 0 || length >= (int)sizeof(line) || !WriteFile(file, line, (DWORD)length, &written, NULL) || written != (DWORD)length) { ok = 0; break; }
        }
    }
    if (!FlushFileBuffers(file)) ok = 0;
    CloseHandle(file);
    if (ok) ok = MoveFileExW(temporary, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
    if (!ok) DeleteFileW(temporary);
    return ok;
}
int native_catalogue_save(const NativeCatalogue* c, const WCHAR* path) { return save_registry(c,path); }
static int assign(NativeCatalogue* c, uint32_t first, uint32_t count, uint32_t step, const char* path, int* added) {
    uint32_t i, free_index = UINT32_MAX;
    for (i = 0; i < count; ++i) {
        uint32_t index = first + i * step;
        LiveRecord* r = &c->records[index];
        if (r->assigned && !strcmp(r->path, path)) return (int)index;
        if (!r->assigned && free_index == UINT32_MAX) free_index = index;
    }
    if (free_index == UINT32_MAX) return -1;
    set_record(&c->records[free_index], path);
    *added = 1;
    return (int)free_index;
}
static int existing(const NativeCatalogue* c, uint32_t first, uint32_t count, uint32_t step, const char* path) {
    uint32_t i;
    for (i = 0; i < count; ++i) {
        uint32_t index = first + i * step;
        if (c->records[index].assigned && !strcmp(c->records[index].path, path)) return (int)index;
    }
    return -1;
}
static int composition(LiveRecord* record, const NativeStamp* stamp) {
    LiveComponent* parts = calloc(stamp->count + stamp->polygon_count + 1, sizeof(*parts));
    uint32_t i, j, count = 0;
    if (!parts) return 0;
    for (i = 0; i < stamp->count; ++i) {
        uint32_t guid;
        memcpy(&guid, stamp->entries[i] + 0x0c, sizeof(guid));
        for (j = 0; j < count && parts[j].guid != guid; ++j) {}
        if (j == count) parts[count++].guid = guid;
        ++parts[j].amount;
    }
    /* The reader accepts earthen roads (8541). TileCount counts tiles, not edges. */
    for (i=0; i<stamp->polygon_count; ++i) {
        const NativePolygon* p=&stamp->polygons[i];
        uint32_t x,y,amount=0,stride=(p->width+31)/32;
        for (y=0;y<p->height;++y) for (x=0;x<p->width;x+=4) {
            uint32_t bits;
            memcpy(&bits,stamp->polygon_bits+p->bits_offset+(y*stride+x/32)*4,4);
            if ((bits>>(x%32))&15) ++amount;
        }
        for (j=0;j<count && parts[j].guid!=p->guid;++j) {}
        if (j==count) parts[count++].guid=p->guid;
        parts[j].amount+=amount;
    }
    if (stamp->road_count && stamp->tiles) {
        for (j = 0; j < count && parts[j].guid != 8541; ++j) {}
        if (j == count) parts[count++].guid = 8541;
        parts[j].amount += stamp->tiles;
    }
    free(record->components);
    record->components = parts;
    record->component_count = count;
    record->building_count = stamp->count;
    record->road_tiles = stamp->road_count ? stamp->tiles : 0;
    record->polygon_count = stamp->polygon_count;
    return 1;
}
static void scan_folder(NativeCatalogue* c, const WCHAR* folder_path,
                         const char* relative, uint32_t region, NativeStamp* scratch, int* changed, int* added) {
    WIN32_FIND_DATAW info;
    WCHAR pattern[32768], path[32768];
    HANDLE search;
    int folder_index = existing(c, region * LIVE_FOLDERS * LIVE_SLOT_STRIDE, LIVE_FOLDERS, LIVE_SLOT_STRIDE, relative);
    // Empty folders are real destinations too. Their menu contains Create/Manage
    // actions, so they can remain visible without any unlocked stamp child.
    if (folder_index<0) folder_index=assign(c,region*LIVE_FOLDERS*LIVE_SLOT_STRIDE,LIVE_FOLDERS,LIVE_SLOT_STRIDE,relative,added);
    if (folder_index<0) { ++c->errors; return; }
    {
        LiveRecord* f=&c->records[folder_index];
        f->seen=1;
        if (!f->active) *changed=1;
        f->active=1; f->icon_guid=c->folder_icons[folder_index/LIVE_SLOT_STRIDE];
        if (!f->icon_guid) f->icon_guid=80587;
        snprintf(f->icon_file,sizeof(f->icon_file),"%s",icon_file(c,f->icon_guid));
    }
    if (!make_path(pattern, 32768, folder_path, L"*")) return;
    search = FindFirstFileW(pattern, &info);
    if (search == INVALID_HANDLE_VALUE) return;
    do {
        char name[256], identity[1024], error[256];
        DWORD raw_size;
        BYTE* raw;
        LiveRecord* r;
        uint64_t size, modified;
        int index, valid;
        if ((info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) || !safe_component(info.cFileName)) continue;
        if (!utf8(info.cFileName, name, sizeof(name)) || strlen(relative) + strlen(name) + 2 >= sizeof(identity)) { ++c->errors; continue; }
        {
            size_t prefix = strlen(relative), length = strlen(name);
            memcpy(identity, relative, prefix);
            identity[prefix] = '/';
            memcpy(identity + prefix + 1, name, length + 1);
        }
        index = folder_index < 0 ? -1 : existing(c, (uint32_t)folder_index + 1, LIVE_STAMPS, 1, identity);
        size = ((uint64_t)info.nFileSizeHigh << 32) | info.nFileSizeLow;
        modified = file_time(info.ftLastWriteTime);
        r = index < 0 ? NULL : &c->records[index];
        if (!r || !r->active || r->modified != modified || r->size != size) {
            if (!make_path(path, 32768, folder_path, info.cFileName)) { ++c->errors; continue; }
            raw = read_file(path, NATIVE_STAMP_MAX_FILE, &raw_size);
            snprintf(error, sizeof(error), "fichier en cours d'ecriture ou illisible");
            valid = raw && native_stamp_read(raw, raw_size, scratch, error, sizeof(error));
            free(raw);
            if (!valid) {
                if (r) { r->seen = 1; if (r->active) *changed = 1; r->active = 0; }
                ++c->errors;
                snprintf(c->last_error, sizeof(c->last_error), "%.220s : %.240s", identity, error);
                continue;
            }
            // Incomplete captures and unrelated files never consume reserved slots.
            if (folder_index < 0) folder_index = assign(c, region * LIVE_FOLDERS * LIVE_SLOT_STRIDE, LIVE_FOLDERS, LIVE_SLOT_STRIDE, relative, added);
            if (folder_index < 0) { ++c->errors; snprintf(c->last_error, sizeof(c->last_error), "limite de 16 dossiers atteinte dans la region"); continue; }
            if (index < 0) index = assign(c, (uint32_t)folder_index + 1, LIVE_STAMPS, 1, identity, added);
            if (index < 0) { ++c->errors; snprintf(c->last_error, sizeof(c->last_error), "%.220s : limite de 64 stamps atteinte", relative); continue; }
            r = &c->records[index];
            if (!composition(r, scratch)) {
                ++c->errors; r->active = 0; r->seen = 1; *changed = 1;
                snprintf(c->last_error, sizeof(c->last_error), "memoire insuffisante pour le cout du stamp");
                continue;
            }
            r->active = 1;
            r->icon_guid = scratch->icon_guid ? scratch->icon_guid : *(const uint32_t*)(scratch->entries[0] + 0x0c);
            snprintf(r->icon_file, sizeof(r->icon_file), "%s", icon_file(c, r->icon_guid));
            r->modified = modified; r->size = size;
            *changed = 1;
        }
        r->seen = 1;
        {
            LiveRecord* f = &c->records[folder_index];
            f->seen = 1;
            if (!f->active) *changed = 1;
            f->active = 1;
            if (!f->icon_guid) {
                f->icon_guid = 80587;
                snprintf(f->icon_file, sizeof(f->icon_file), "%s", icon_file(c, f->icon_guid));
            }
        }
    } while (FindNextFileW(search, &info));
    FindClose(search);
}

int native_catalogue_refresh(NativeCatalogue* c, const WCHAR* root, const WCHAR* icons,
                             const WCHAR* registry, ULONGLONG now, int force) {
    uint32_t i, region;
    NativeStamp* scratch;
    int changed = 0, added = 0;
    if (c->initialized && !force && !c->dirty && now - c->scanned < 750) return 1;
    scratch = malloc(sizeof(*scratch));
    if (!scratch) return 0;
    if (!c->initialized) {
        DWORD size;
        size_t k;
        c->icons = (char*)read_file(icons, 32 * 1024 * 1024, &size);
        c->icons_size = size;
        /* Newline-delimited records become C strings; preserve the final sentinel. */
        for (k = 0; k < size; ++k) if (c->icons[k] == '\n' || c->icons[k] == '\r') c->icons[k] = 0;
        load_registry(c, registry);
        c->initialized = 1; changed = 1;
    }
    c->errors = 0;
    c->last_error[0] = 0;
    {
        uint32_t values[LIVE_FOLDER_COUNT];
        if (!native_folder_icons_read(c,registry,values)) {
            ++c->errors; snprintf(c->last_error,sizeof(c->last_error),"folder-icons.json invalide ou illisible ; choix precedents conserves");
        } else if (memcmp(values,c->folder_icons,sizeof(values))) {
            memcpy(c->folder_icons,values,sizeof(values)); changed=1;
        }
    }
    for (i = 0; i < LIVE_RECORD_COUNT; ++i) c->records[i].seen = 0;
    for (region = 0; region < 2; ++region) {
        WCHAR base[32768], pattern[32768], folder[32768];
        WIN32_FIND_DATAW info;
        HANDLE search;
        if (!make_path(base, 32768, root, regions[region]) || !make_path(pattern, 32768, base, L"*")) continue;
        search = FindFirstFileW(pattern, &info);
        if (search == INVALID_HANDLE_VALUE) continue;
        do {
            char name[256], identity[1024];
            if (!(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) || !safe_component(info.cFileName)) continue;
            if (!utf8(info.cFileName, name, sizeof(name)) || !make_path(folder, 32768, base, info.cFileName)) { ++c->errors; continue; }
            snprintf(identity, sizeof(identity), "%s/%s", region_utf8[region], name);
            scan_folder(c, folder, identity, region, scratch, &changed, &added);
        } while (FindNextFileW(search, &info));
        FindClose(search);
    }
    c->row_count = 0;
    for (i = 0; i < LIVE_RECORD_COUNT; ++i) {
        LiveRecord* r = &c->records[i];
        if (r->active && !r->seen) { r->active = 0; changed = 1; }
        if (r->active) c->rows[c->row_count++] = i;
    }
    if (added) c->registry_dirty = 1;
    if (c->registry_dirty) {
        if (save_registry(c, registry)) c->registry_dirty = 0;
        else { ++c->errors; snprintf(c->last_error, sizeof(c->last_error), "enregistrement des identifiants d'UI impossible ; nouvelle tentative prevue"); }
    }
    if (changed) ++c->revision;
    c->scanned = now; c->dirty = 0;
    free(scratch);
    return 1;
}
const LiveRecord* native_catalogue_find(const NativeCatalogue* c, uint32_t guid) {
    uint32_t index = guid - LIVE_POOL_BASE;
    if (guid < LIVE_POOL_BASE || index >= LIVE_RECORD_COUNT || !c->records[index].active) return NULL;
    return &c->records[index];
}
static const LiveRecord* row(const NativeCatalogue* c, uint32_t index) {
    return index < c->row_count ? &c->records[c->rows[index]] : NULL;
}
static uint32_t chunk(const char* text, uint32_t part) {
    size_t length = strlen(text), start = (size_t)part * 3, i;
    uint32_t value = 0;
    for (i = 0; i < 3 && start + i < length; ++i) value |= (uint32_t)(unsigned char)text[start + i] << (8 * i);
    return value;
}
int native_catalogue_is_query(uint32_t command) {
    return (command >= LIVE_PROTOCOL && command <= LIVE_ERRORS) ||
           (command >= LIVE_ROW_GUID && command < LIVE_ROW_GUID + LIVE_RECORD_COUNT) ||
           (command >= LIVE_ROW_ICON && command < LIVE_ROW_ICON + LIVE_RECORD_COUNT) ||
           (command >= LIVE_NAME_LENGTH && command < LIVE_NAME_LENGTH + LIVE_RECORD_COUNT) ||
           (command >= LIVE_ICON_LENGTH && command < LIVE_ICON_LENGTH + LIVE_RECORD_COUNT) ||
           (command >= LIVE_COMPONENT_COUNT && command < LIVE_COMPONENT_COUNT + LIVE_RECORD_COUNT) ||
           (command >= LIVE_BUILDING_COUNT && command < LIVE_BUILDING_COUNT + LIVE_RECORD_COUNT) ||
           (command >= LIVE_ROAD_TILES && command < LIVE_ROAD_TILES + LIVE_RECORD_COUNT) ||
           (command >= LIVE_POLYGON_COUNT && command < LIVE_POLYGON_COUNT + LIVE_RECORD_COUNT) ||
           (command >= LIVE_COMPONENT_GUID && command < LIVE_COMPONENT_GUID + LIVE_RECORD_COUNT * LIVE_COMPONENT_STRIDE) ||
           (command >= LIVE_COMPONENT_AMOUNT && command < LIVE_COMPONENT_AMOUNT + LIVE_RECORD_COUNT * LIVE_COMPONENT_STRIDE) ||
           (command >= LIVE_NAME_BYTES && command < LIVE_NAME_BYTES + LIVE_RECORD_COUNT * LIVE_TEXT_STRIDE) ||
           (command >= LIVE_ICON_BYTES && command < LIVE_ICON_BYTES + LIVE_RECORD_COUNT * LIVE_TEXT_STRIDE);
}
uint32_t native_catalogue_query(const NativeCatalogue* c, uint32_t command) {
    const LiveRecord* r;
    if (command == LIVE_PROTOCOL) return 6;
    if (command == LIVE_REVISION || command == LIVE_REFRESH) return c->revision;
    if (command == LIVE_COUNT) return c->row_count;
    if (command == LIVE_ERRORS) return c->errors;
    if (command >= LIVE_ROW_GUID && command < LIVE_ROW_GUID + LIVE_RECORD_COUNT) {
        uint32_t n = command - LIVE_ROW_GUID;
        return n < c->row_count ? LIVE_POOL_BASE + c->rows[n] : 0;
    }
    if (command >= LIVE_ROW_ICON && command < LIVE_ROW_ICON + LIVE_RECORD_COUNT) { r = row(c, command - LIVE_ROW_ICON); return r ? r->icon_guid : 0; }
    if (command >= LIVE_NAME_LENGTH && command < LIVE_NAME_LENGTH + LIVE_RECORD_COUNT) { r = row(c, command - LIVE_NAME_LENGTH); return r ? (uint32_t)strlen(r->name) : 0; }
    if (command >= LIVE_ICON_LENGTH && command < LIVE_ICON_LENGTH + LIVE_RECORD_COUNT) { r = row(c, command - LIVE_ICON_LENGTH); return r ? (uint32_t)strlen(r->icon_file) : 0; }
    if (command >= LIVE_COMPONENT_COUNT && command < LIVE_COMPONENT_COUNT + LIVE_RECORD_COUNT) { r = row(c, command - LIVE_COMPONENT_COUNT); return r ? r->component_count : 0; }
    if (command >= LIVE_BUILDING_COUNT && command < LIVE_BUILDING_COUNT + LIVE_RECORD_COUNT) { r = row(c, command - LIVE_BUILDING_COUNT); return r ? r->building_count : 0; }
    if (command >= LIVE_POLYGON_COUNT && command < LIVE_POLYGON_COUNT + LIVE_RECORD_COUNT) { r = row(c, command - LIVE_POLYGON_COUNT); return r ? r->polygon_count : 0; }
    if (command >= LIVE_ROAD_TILES && command < LIVE_ROAD_TILES + LIVE_RECORD_COUNT) { r = row(c, command - LIVE_ROAD_TILES); return r ? r->road_tiles : 0; }
    {
        uint32_t bases[] = {LIVE_COMPONENT_GUID, LIVE_COMPONENT_AMOUNT}, k;
        for (k = 0; k < 2; ++k) if (command >= bases[k] && command < bases[k] + LIVE_RECORD_COUNT * LIVE_COMPONENT_STRIDE) {
            uint32_t n = command - bases[k], part = n % LIVE_COMPONENT_STRIDE;
            r = row(c, n / LIVE_COMPONENT_STRIDE);
            return r && part < r->component_count ? (k ? r->components[part].amount : r->components[part].guid) : 0;
        }
    }
    if (command >= LIVE_NAME_BYTES && command < LIVE_NAME_BYTES + LIVE_RECORD_COUNT * LIVE_TEXT_STRIDE) {
        uint32_t n = command - LIVE_NAME_BYTES; r = row(c, n / LIVE_TEXT_STRIDE); return r ? chunk(r->name, n % LIVE_TEXT_STRIDE) : 0;
    }
    if (command >= LIVE_ICON_BYTES && command < LIVE_ICON_BYTES + LIVE_RECORD_COUNT * LIVE_TEXT_STRIDE) {
        uint32_t n = command - LIVE_ICON_BYTES; r = row(c, n / LIVE_TEXT_STRIDE); return r ? chunk(r->icon_file, n % LIVE_TEXT_STRIDE) : 0;
    }
    return 0;
}
void native_catalogue_dispose(NativeCatalogue* c) {
    uint32_t i;
    for (i = 0; i < LIVE_RECORD_COUNT; ++i) free(c->records[i].components);
    free(c->icons); memset(c, 0, sizeof(*c));
}
