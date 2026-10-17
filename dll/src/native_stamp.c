/* Read the game's original zlib/FileDB stamps. No intermediate file format. */
#include "native_stamp.h"
#include "miniz.h" // same architecture configuration as miniz_tinfl.c
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_DATA (32u * 1024 * 1024)
#define MAX_NODES 200000u
#define NONE UINT32_MAX
typedef struct {
    uint32_t tag, first, last, next, bytes;
    const uint8_t* payload;
} Node;
typedef struct {
    uint8_t* data;
    size_t size, content, trailing;
    const char** names;
    Node* nodes;
    uint32_t count, capacity;
    const char* error;
} DB;

static uint32_t u32(const void* p) { uint32_t x; memcpy(&x, p, 4); return x; }
static uint16_t u16(const void* p) { uint16_t x; memcpy(&x, p, 2); return x; }
static int fail(DB* db, const char* error) { db->error = error; return 0; }
static int dict(DB* db, size_t start, size_t end, int leaf) {
    uint32_t count, i;
    size_t pos;
    if (end < start || end - start < 4) return fail(db, "dictionnaire FileDB invalide");
    count = u32(db->data + start);
    if (count > 32768 || count > (end - start - 4) / 2)
        return fail(db, "dictionnaire FileDB tronque");
    pos = start + 4 + count * 2;
    for (i = 0; i < count; ++i) {
        uint16_t tag = u16(db->data + start + 4 + i * 2);
        const uint8_t* zero;
        if (!!(tag & 0x8000) != leaf || db->names[tag] || pos >= end)
            return fail(db, "tag FileDB invalide");
        zero = memchr(db->data + pos, 0, end - pos);
        if (!zero) return fail(db, "nom FileDB tronque");
        db->names[tag] = (const char*)db->data + pos;
        pos = (size_t)(zero - db->data) + 1;
    }
    return 1;
}
static uint32_t node_add(DB* db, uint32_t parent, uint32_t tag) {
    uint32_t index;
    Node* node;
    if (db->count >= MAX_NODES) { fail(db, "trop de noeuds FileDB"); return NONE; }
    if (db->count == db->capacity) {
        uint32_t capacity = db->capacity ? db->capacity * 2 : 256;
        Node* nodes = realloc(db->nodes, capacity * sizeof(Node));
        if (!nodes) { fail(db, "allocation FileDB impossible"); return NONE; }
        db->nodes = nodes;
        db->capacity = capacity;
    }
    index = db->count++;
    node = &db->nodes[index];
    memset(node, 0, sizeof(*node));
    node->tag = tag;
    node->first = node->last = node->next = NONE;
    if (parent != NONE) {
        Node* p = &db->nodes[parent];
        if (p->last != NONE) db->nodes[p->last].next = index;
        else p->first = index;
        p->last = index;
    }
    return index;
}
static int block(DB* db, size_t* pos, size_t end, uint32_t parent, int depth, int needs_end) {
    if (depth > 64) return fail(db, "profondeur FileDB excessive");
    while (*pos < end) {
        uint32_t size, tag, index;
        size_t padded;
        if (end - *pos < 8) return fail(db, "noeud FileDB tronque");
        size = u32(db->data + *pos); tag = u32(db->data + *pos + 4);
        *pos += 8;
        if (!size && !tag) return 1;
        if (tag > 65535) return fail(db, "tag FileDB inconnu");
        index = node_add(db, parent, tag);
        if (index == NONE) return 0;
        if (tag & 0x8000) {
            padded = ((size_t)size + 7) & ~(size_t)7;
            if (padded > end - *pos) return fail(db, "feuille FileDB tronquee");
            db->nodes[index].payload = db->data + *pos;
            db->nodes[index].bytes = size;
            *pos += padded;
        } else if (!size) {
            if (!block(db, pos, end, index, depth + 1, 1)) return 0;
        } else {
            size_t sub_end;
            if (size > end - *pos) return fail(db, "bloc FileDB tronque");
            sub_end = *pos + size;
            if (!block(db, pos, sub_end, index, depth + 1, 0)) return 0;
            if (*pos != sub_end) return fail(db, "taille de bloc FileDB incoherente");
        }
    }
    return needs_end ? fail(db, "fin de bloc FileDB absente") : 1;
}
static uint32_t child(DB* db, uint32_t parent, const char* name) {
    uint32_t i;
    if (parent == NONE) return NONE;
    for (i = db->nodes[parent].first; i != NONE; i = db->nodes[i].next) {
        const char* tag = db->names[db->nodes[i].tag];
        if (tag && !strcmp(tag, name)) return i;
    }
    return NONE;
}
static int field(DB* db, uint32_t parent, const char* name, void* out, uint32_t bytes, int required) {
    uint32_t i = child(db, parent, name);
    if (i == NONE) return required ? fail(db, "champ obligatoire absent") : 1;
    if (!db->nodes[i].payload || db->nodes[i].bytes != bytes)
        return fail(db, "taille de champ FileDB invalide");
    memcpy(out, db->nodes[i].payload, bytes);
    return 1;
}
static int direction(DB* db, uint32_t item, uint8_t* out) {
    uint32_t i = child(db, item, "Dir"), value;
    int depth = 0;
    if (i == NONE) return 1; // omitted enum = native default 0
    while (!db->nodes[i].payload) {
        uint32_t first = db->nodes[i].first;
        if (++depth > 8 || first == NONE || db->nodes[first].next != NONE)
            return fail(db, "direction FileDB invalide");
        i = first;
    }
    if (db->nodes[i].bytes != 4 || (value = u32(db->nodes[i].payload)) > 7)
        return fail(db, "direction hors limites");
    *out = (uint8_t)value;
    return 1;
}
// Polygon layout is serialized by FUN_143417300. Keep the bit grid, its world
// origin, relative offset, owner and optional CreateRect together.
static int polygons(DB* db, NativeStamp* s) {
    uint32_t list=child(db,0,"Polygons"), item;
    if (list==NONE) return 1;
    for (item=db->nodes[list].first; item!=NONE; item=db->nodes[item].next) {
        NativePolygon* p;
        uint32_t grid, bits, rect, value, preview, j;
        uint64_t bytes;
        if (s->polygon_count>=NATIVE_STAMP_POLYGONS) return fail(db,"plus de 256 zones polygonales");
        if (s->count+s->polygon_count>=NATIVE_STAMP_BUILDINGS) return fail(db,"plus de 4096 elements de stamp");
        p=&s->polygons[s->polygon_count]; p->preview_type=2;
        grid=child(db,item,"SubTileGrid");
        if (!field(db,item,"GUID",&p->guid,4,1) ||
            !field(db,item,"RelativePos",p->relative,8,1) ||
            !field(db,item,"ComplexOwnerID",&p->owner,8,0) ||
            !field(db,item,"IsBluePrint",&p->blueprint,1,0) ||
            !field(db,grid,"GridOriginWS",p->origin,8,1)) return 0;
        preview=child(db,item,"PreviewType");
        if (preview!=NONE) {
            uint8_t t=0;
            // Same nested enum encoding as Dir.
            uint32_t node=preview;
            for (j=0; !db->nodes[node].payload && j<8; ++j) {
                node=db->nodes[node].first;
                if (node==NONE) return fail(db,"PreviewType invalide");
            }
            if (!db->nodes[node].payload || db->nodes[node].bytes!=4 || u32(db->nodes[node].payload)>255)
                return fail(db,"PreviewType invalide");
            t=(uint8_t)u32(db->nodes[node].payload); p->preview_type=t;
        }
        grid=child(db,child(db,grid,"Grid"),"grid"); bits=child(db,grid,"bits");
        if (!field(db,grid,"x",&p->width,4,1) || !field(db,grid,"y",&p->height,4,1)) return 0;
        if (!p->guid || !p->width || !p->height || p->width>8192 || p->height>2048 ||
            !isfinite(p->relative[0]) || !isfinite(p->relative[1]) ||
            fabsf(p->relative[0])>1000 || fabsf(p->relative[1])>1000 || p->blueprint>1)
            return fail(db,"geometrie de zone polygonale invalide");
        bytes=(uint64_t)((p->width+31)/32)*p->height*4;
        if (bits==NONE || !db->nodes[bits].payload || bytes!=db->nodes[bits].bytes ||
            bytes>NATIVE_STAMP_POLYGON_BYTES-s->polygon_bytes)
            return fail(db,"grille polygonale tronquee ou excessive");
        p->bits_offset=s->polygon_bytes; p->bits_size=(uint32_t)bytes;
        memcpy(s->polygon_bits+p->bits_offset,db->nodes[bits].payload,p->bits_size);
        s->polygon_bytes+=p->bits_size;
        rect=child(db,child(db,item,"CreateRect"),"Rect");
        if (rect!=NONE) {
            uint64_t index=0;
            if (!field(db,rect,"index",&index,8,1) || index!=0)
                return fail(db,"variante CreateRect non prise en charge");
            value=child(db,rect,"value");
            if (value==NONE || !db->nodes[value].payload || db->nodes[value].bytes!=16)
                return fail(db,"CreateRect tronque");
            memcpy(p->rect,db->nodes[value].payload,16); p->has_rect=1;
        }
        ++s->polygon_count;
    }
    return 1;
}
static int geometry(DB* db, NativeStamp* s) {
    uint32_t info = child(db, 0, "BuildingInfo"), mesh = child(db, 0, "MeshGraphData");
    uint32_t item, edges, group, k;
    float minx = 0, minz = 0, maxx = 0, maxz = 0;
    if (info == NONE) return fail(db, "BuildingInfo absent (stamp Anno 117 requis)");
    if (!field(db, 0, "IconGUID", &s->icon_guid, 4, 0)) return 0;
    for (item = db->nodes[info].first; item != NONE; item = db->nodes[item].next) {
        uint8_t* entry;
        uint32_t guid;
        float x, z;
        if (s->count >= NATIVE_STAMP_BUILDINGS) return fail(db, "plus de 4096 batiments");
        entry = s->entries[s->count];
        if (!field(db, item, "Pos", entry, 8, 1) ||
            !field(db, item, "GUID", entry + 0x0c, 4, 1) ||
            !direction(db, item, entry + 8) ||
            !field(db, item, "Skin", entry + 0x10, 4, 0) ||
            !field(db, item, "Variation", entry + 0x14, 4, 0) ||
            !field(db, item, "IsBluePrint", entry + 0x18, 1, 0) ||
            !field(db, item, "ComplexOwnerID", entry + 0x44, 8, 0)) return 0;
        memcpy(&x, entry, 4); memcpy(&z, entry + 4, 4); guid = u32(entry + 0x0c);
        if (!guid || !isfinite(x) || !isfinite(z) || fabsf(x) > 200 || fabsf(z) > 200 || entry[0x18] > 1)
            return fail(db, "geometrie de batiment hors limites");
        if (!s->count) minx = maxx = x, minz = maxz = z;
        if (x < minx) minx = x;
        if (x > maxx) maxx = x;
        if (z < minz) minz = z;
        if (z > maxz) maxz = z;
        ++s->count;
    }
    if (!s->count) return fail(db, "stamp sans batiment");
    s->bbox[0] = (int32_t)lrintf(minx); s->bbox[1] = (int32_t)lrintf(minz);
    s->bbox[2] = (int32_t)lrintf(maxx); s->bbox[3] = (int32_t)lrintf(maxz);
    edges = child(db, mesh, "Edges");
    group = edges == NONE ? NONE : db->nodes[edges].first;
    for (k = 0; group != NONE; group = db->nodes[group].next, ++k) {
        if (k && db->nodes[group].first != NONE)
            return fail(db, "type de reseau autre que Street non pris en charge");
        for (item = db->nodes[group].first; item != NONE; item = db->nodes[item].next) {
            uint32_t n = s->road_count;
            if (n >= NATIVE_STAMP_ROADS) return fail(db, "plus de 256 segments de route");
            if (!field(db, item, "guid", &s->roads[n].guid, 4, 1) ||
                !field(db, item, "PosMin", &s->roads[n].x1, 8, 1) ||
                !field(db, item, "PosMax", &s->roads[n].x2, 8, 1)) return 0;
            /* v85's Street input has no GUID field; fail rather than change a road. */
            if (s->roads[n].guid != 8541) return fail(db, "type de route non pris en charge par la v85");
            ++s->road_count;
        }
    }
    if (s->road_count) {
        int32_t lo[2] = {INT32_MAX, INT32_MAX}, hi[2] = {INT32_MIN, INT32_MIN};
        float origin[2] = {0, 0};
        int absolute = 0;
        for (k = 0; k < s->road_count; ++k) {
            const int32_t* xy = &s->roads[k].x1;
            uint32_t j;
            for (j = 0; j < 4; ++j) {
                if (xy[j] < lo[j % 2]) lo[j % 2] = xy[j];
                if (xy[j] > hi[j % 2]) hi[j % 2] = xy[j];
            }
        }
        /* Buildings are relative already. Keep relative road coordinates intact.
           Fresh game captures can store roads in world half-units instead. */
        if (hi[0] < minx * 2 - 32 || lo[0] > maxx * 2 + 32 ||
            hi[1] < minz * 2 - 32 || lo[1] > maxz * 2 + 32) absolute = 1;
        if (absolute && !field(db, mesh, "DragRectCenter", origin, 8, 1)) return 0;
        if (!isfinite(origin[0]) || !isfinite(origin[1]) || fabsf(origin[0]) > 1000000 || fabsf(origin[1]) > 1000000)
            return fail(db, "origine de routes invalide");
        for (k = 0; k < s->road_count; ++k) {
            int32_t* xy = &s->roads[k].x1;
            uint32_t j;
            for (j = 0; j < 4; ++j) {
                double v = (double)xy[j] - (double)origin[j % 2] * 2;
                if (fabs(v) > 800 || fabs(v - nearbyint(v)) > .001)
                    return fail(db, "coordonnees de routes incoherentes");
                xy[j] = (int32_t)lrint(v);
            }
        }
        for (k = 0; k < 2; ++k) {
            s->aabb[k] = (int32_t)lrint((lo[k] - (double)origin[k] * 2) / 2);
            s->aabb[k + 2] = (int32_t)lrint((hi[k] - (double)origin[k] * 2) / 2);
            s->centre[k] = (s->aabb[k] + s->aabb[k + 2]) * .5f;
        }
        if (!field(db, mesh, "TileCount", &s->tiles, 4, 1)) return 0;
        /* Preserve native local mesh bounds/centre when available, including
           asymmetric selections. Absolute metadata must be translated first. */
        if (!absolute && (!field(db, mesh, "AABB", s->aabb, 16, 0) ||
                          !field(db, mesh, "DragRectCenter", s->centre, 8, 0))) return 0;
        if (s->tiles > 1000000 || !isfinite(s->centre[0]) || !isfinite(s->centre[1]))
            return fail(db, "metadonnees de routes invalides");
    }
    return polygons(db,s);
}

static int decode(DB* dest,const void* compressed,size_t size) {
    DB db={0};
    size_t capacity = 65536, pos = 0;
    int ok = 0;
    db.error = "stamp zlib/FileDB invalide";
    if (!compressed || size < 6 || size > NATIVE_STAMP_MAX_FILE) goto done;
    for (;;) {
        size_t input = size, output = capacity;
        tinfl_decompressor inflater;
        tinfl_status status;
        free(db.data); db.data = malloc(capacity);
        if (!db.data) { db.error = "allocation de decompression impossible"; goto done; }
        tinfl_init(&inflater);
        status = tinfl_decompress(&inflater, compressed, &input, db.data, db.data, &output,
                     TINFL_FLAG_PARSE_ZLIB_HEADER | TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
        if (status == TINFL_STATUS_DONE) {
            /* Official files append an empty zlib stream and the decoded size. */
            static const uint8_t tail[] = {0x78, 0xda, 3, 0, 0, 0, 0, 1};
            if (input != size && (size - input != 12 ||
                memcmp((const uint8_t*)compressed + input, tail, sizeof(tail)) ||
                u32((const uint8_t*)compressed + size - 4) != output)) goto done;
            db.size = output;
            db.trailing = size-input;
            break;
        }
        if (status != TINFL_STATUS_HAS_MORE_OUTPUT || capacity >= MAX_DATA) goto done;
        capacity *= 2;
    }
    if (db.size < 28 || u32(db.data + db.size - 4) != 0xfffffffd) goto done;
    db.content = u32(db.data + db.size - 16);
    {
        size_t leaves = u32(db.data + db.size - 12);
        if (db.content >= leaves || leaves >= db.size - 20) goto done;
        db.names = calloc(65536, sizeof(char*));
        if (!db.names || !dict(&db, db.content, leaves, 0) || !dict(&db, leaves, db.size - 20, 1)) goto done;
    }
    if (node_add(&db, NONE, 0) == NONE || !block(&db, &pos, db.content, 0, 0, 0) || pos != db.content) goto done;
    ok = 1;
done:
    *dest=db;
    return ok;
}
static void dispose_db(DB* db) { free(db->nodes); free(db->names); free(db->data); }
int native_stamp_read(const void* compressed, size_t size, NativeStamp* out,
                      char* error, size_t error_size) {
    DB db={0};
    NativeStamp* parsed=NULL;
    int ok=0;
    if (!decode(&db,compressed,size) || !out) goto done;
    parsed=calloc(1,sizeof(*parsed));
    if (!parsed || !geometry(&db,parsed)) goto done;
    memcpy(out,parsed,sizeof(*out)); ok=1;
done:
    if (error && error_size) snprintf(error, error_size, "%s", ok ? "" : db.error);
    free(parsed); dispose_db(&db);
    return ok;
}
int native_stamp_set_icon(const void* compressed,size_t size,uint32_t icon,
                         void** output,size_t* output_size,char* error,size_t error_size) {
    DB db={0}; uint32_t node,index=NONE,count=0; uint8_t* packed=NULL;
    size_t length=0; int ok=0;
    if (!output || !output_size) return 0;
    *output=NULL; *output_size=0;
    if (!decode(&db,compressed,size)) goto done;
    for (node=db.nodes[0].first;node!=NONE;node=db.nodes[node].next)
        if (db.names[db.nodes[node].tag] && !strcmp(db.names[db.nodes[node].tag],"IconGUID")) { index=node; ++count; }
    if (count!=1 || !db.nodes[index].payload || db.nodes[index].bytes!=4) {
        db.error="IconGUID natif absent, duplique ou de taille incorrecte"; goto done;
    }
    if (u32(db.nodes[index].payload)==icon) {
        packed=malloc(size);
        if (!packed) { db.error="allocation impossible"; goto done; }
        memcpy(packed,compressed,size); length=size;
    } else {
        // Patch, never serialize the tree: every other decoded byte is retained.
        memcpy((uint8_t*)db.nodes[index].payload,&icon,4);
        packed=tdefl_compress_mem_to_heap(db.data,db.size,&length,TDEFL_DEFAULT_MAX_PROBES|TDEFL_WRITE_ZLIB_HEADER);
        if (!packed) { db.error="compression du stamp impossible"; goto done; }
        if (db.trailing) {
            uint8_t* full=realloc(packed,length+db.trailing);
            if (!full) { db.error="allocation du trailer impossible"; goto done; }
            packed=full;
            memcpy(packed+length,(const uint8_t*)compressed+size-db.trailing,db.trailing);
            length+=db.trailing;
        }
        if (length>NATIVE_STAMP_MAX_FILE) { db.error="stamp compresse trop volumineux"; goto done; }
    }
    *output=packed; *output_size=length; packed=NULL; ok=1;
done:
    if (error && error_size) snprintf(error,error_size,"%s",ok ? "" : db.error);
    free(packed); dispose_db(&db); return ok;
}
void native_stamp_free(void* buffer) { free(buffer); }
