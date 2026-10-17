// v56-pipette : charge un cachet sauvegardé DANS LA PIPETTE via le code du MOTEUR.
//
// POURQUOI CE DESIGN (v56) :
//   La pipette s'obtient via le contexte TLS du thread LOGIQUE du jeu.
//   Un hook du message pump (v55) tournait sur un AUTRE thread -> contexte = 0
//   (vérifié en jeu : "contexte indisponible" alors que Ctrl+Numpad1/2 marchaient).
//   Donc : on intercepte CreateStamp — appelé par le raccourci Lua Ctrl+Numpad1,
//   donc sur le thread logique — et on remplit la pipette JUSTE AVANT que le
//   moteur ne la lise. Le moteur construit alors le cachet avec NOS bâtiments.
//
// FLUX :
//   Ctrl+Numpad0 (DLL)  -> lit C:\anno117-stamps\current.stamp.bin, arme le remplissage
//   Ctrl+Numpad1 (mod)  -> Stamp:CreateStamp() -> [hook : pipette remplie] -> cachet créé
//   Ctrl+Numpad2 (mod)  -> Stamp:BuildStamp()  -> GROUPE posé nativement
//
// Signatures générées depuis le build 2.1.0 (2026-10-11) — indépendantes des RVA.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include "MinHook.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#ifdef STAMPS_NATIVE_UI
#include <shlobj.h>
#include "native_stamp.h"
#include "native_catalogue.h"
static NativeStamp* g_native_stamp = NULL;
static BOOL g_native_active = FALSE;
#endif

// ---------------------------------------------------------------- signatures
static const BYTE sig_getter[] = {
    0x03, 0x48, 0x8B, 0xCB, 0xFF, 0x50, 0x18, 0x84, 0xC0, 0x74, 0x1B, 0xC6,
    0x44, 0x24, 0x30, 0x01, 0xC7, 0x44, 0x24, 0x34, 0x0D, 0x00, 0x00, 0x00,
    0x48, 0x8B, 0x4C, 0x24, 0x30, 0xE8, 0xAC, 0x6D };
static const int  sig_getter_off = -0x22;   // signature trouvée à +0x22 du début

static const BYTE sig_veccopy[] = {
    0x48, 0x89, 0x5C, 0x24, 0x18, 0x56, 0x57, 0x41, 0x56, 0x48, 0x83, 0xEC,
    0x20, 0x4C, 0x8B, 0x49, 0x10, 0x48, 0x8B, 0xFA, 0x49, 0x8B, 0xF0, 0x49,
    0xBA, 0xAB, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA };
static const int  sig_veccopy_off = 0x0;    // signature = début de la fonction

static const BYTE sig_createstamp[] = {
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x40, 0x4C, 0x8B,
    0xCA, 0x33, 0xDB, 0x48, 0x8B, 0x05, 0xCA, 0x97, 0xF9, 0x02, 0x48, 0x85,
    0xC9, 0x0F, 0x84, 0xC6, 0x00, 0x00, 0x00, 0x80 };
static const int  sig_createstamp_off = 0x0;

static const BYTE sig_buildstamp[] = {
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x40, 0x4C, 0x8B,
    0xCA, 0x33, 0xDB, 0x48, 0x8B, 0x05, 0x9A, 0x96, 0xF9, 0x02, 0x48, 0x85,
    0xC9, 0x0F, 0x84, 0xD6, 0x00, 0x00, 0x00, 0x80 };
static const int  sig_buildstamp_off = 0x0;

#ifdef STAMPS_NATIVE_UI
// Wrapper Lua GetStampIconGUID(weak self, int stampID, lua state),
// FUN_144319200. Les indices ordinaires gardent leur comportement d'origine.
static const BYTE sig_stampicon[] = {
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x40, 0x4D, 0x8B,
    0xC8, 0x8B, 0xFA, 0x0F, 0xB6, 0x41, 0x01, 0x3C, 0x01, 0x74, 0x08, 0x3C,
    0x02, 0x0F, 0x85, 0xD5, 0x00, 0x00, 0x00 };
// Fin du getter TLS de CTargetManager (FUN_140aa3420, signature à +0x48).
static const BYTE sig_targetgetter[] = {
    0x48, 0x8B, 0x83, 0x30, 0x05, 0x00, 0x00, 0x48, 0x83, 0xC4, 0x20,
    0x5B, 0xC3, 0x33, 0xC0, 0x48, 0x83, 0xC4, 0x20, 0x5B, 0xC3 };
// Constructeur complet du payload natif (0x5a0 octets), FUN_1433a70e0.
static const BYTE sig_payload_ctor[] = {
    0x48,0x89,0x5c,0x24,0x18,0x48,0x89,0x74,0x24,0x20,0x48,0x89,0x4c,0x24,0x08,
    0x57,0x48,0x83,0xec,0x20,0x48,0x8b,0xf9,0x33,0xf6,0x89,0x31,0x48,0xb8,
    0xff,0xff,0xff,0xff,0xff,0xff,0xff,0x7f };
// Dans le chargement natif : call ClearTool ; lea source ; ... ; call Fill.
// Résoudre le call précédent conserve le vrai nettoyage, même s'il est protégé.
static const BYTE sig_native_load[] = {
    0x48,0x8d,0x53,0x60,0x45,0x33,0xc0,0x48,0x8d,0x8d,0x70,0x01,0x00,0x00 };
static void* g_payload_ctor = NULL;
static const BYTE sig_polygon_ctor[] = {
    0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0xd9,0x48,0x85,0xd2,0x74,0x45,
    0x48,0x8d,0x05,0x5b,0x2d,0xea,0x01 };
static void* g_polygon_ctor = NULL;
static BYTE g_ui_polygons[NATIVE_STAMP_POLYGONS][0x60];
// CAsset.Icon's native call site: asset manager lookup, then CStandard.
// Derive both addresses from instructions instead of assuming a module base.
static const BYTE sig_asset_icon_source[] = {
    0x48,0x8b,0x0d,0x58,0x24,0xd0,0xf4,0x48,0x8b,0x10,0x48,0x8b,0x01,
    0x8b,0x52,0x08,0xff,0x50,0x18,0x48,0x85,0xc0,0x74,0x29,0x48,0x89,
    0xc1,0xe8,0xa7,0x49,0x2b,0xf0,0x48,0x85,0xc0,0x74,0x1c };
// CTextManager::GetText(TextId, makeNew, language), returning const wstring*.
static const BYTE sig_text_source[] = {
    0x48,0x89,0x5c,0x24,0x08,0x66,0x44,0x89,0x4c,0x24,0x20,0x55,0x56,
    0x57,0x41,0x56,0x41,0x57,0x48,0x8b,0xec,0x48,0x83,0xec,0x60,0x41,
    0x0f,0xb7,0xf1,0x45,0x0f,0xb6,0xf8,0x48,0x8b,0xfa,0x4c,0x8b,0xf1 };
static void** g_ui_asset_manager = NULL;
static void* g_ui_standard_getter = NULL;
typedef const void* (*ui_text_source_t)(void*, UINT64, BYTE, unsigned short);
static ui_text_source_t orig_ui_text_source = NULL;
static LONG g_ui_presentation_hook_ready = 0;
static uint32_t g_ui_presentation_revision = 0;
static BYTE* g_ui_polygon_bits = NULL;
static void* g_clear_tool = NULL;
static void* g_reset_payload = NULL;
// Source séparée du payload actif : aucun pointeur d'aperçu du monde n'y entre.
// Une seule allocation par session, réutilisée ; Fill copie les conteneurs.
static BYTE g_ui_payload[0x5a0];
static void* g_ui_road_array = NULL;
static BOOL g_ui_payload_ready = FALSE;
#endif

// FUN_1433a7610 : remplissage de la pipette (pipette, source, flag) — appelé par
// le PASTE (Ctrl+V / clic) avec la source RÉELLE (celle qui a les positions).
static const BYTE sig_fill[] = {
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89,
    0x74, 0x24, 0x18, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57,
    0x48, 0x83, 0xEC, 0x30, 0x45, 0x0F, 0xB6, 0xE8 };
static const int  sig_fill_off = 0x0;

// FUN_144d8bc00 : allocateur du moteur (utilisé par l'écrivain de cachets)
static const BYTE sig_alloc[] = { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9, 0xEB, 0x0F, 0x48, 0x8B, 0xCB, 0xE8, 0x99, 0xDA, 0x03, 0x00, 0x85, 0xC0, 0x74, 0x13, 0x48 };
static const int  sig_alloc_off = 0;
// FUN_144dbf0bc : libération du moteur
static const BYTE sig_free[] = { 0x48, 0x83, 0xEC, 0x28, 0x48, 0x85, 0xC9, 0x74, 0x0D, 0x48, 0x83, 0xE1, 0xF8, 0x48, 0x8B, 0x49, 0xF8, 0xE8, 0xB6, 0xB2, 0xFE, 0xFF, 0x48, 0x83 };
static const int  sig_free_off = 0;

// ---------------------------------------------------------------- constantes
#define PIPETTE_OFFSET   0x170   // pipette = contexte + 0x170
#define PIPETTE_VEC1     0x20    // { begin, end, cap } du vecteur principal
#define PIPETTE_VEC2     0x520   // second vecteur (doit rester vide pour CreateStamp)
#define ENTRY_SIZE       0x60    // taille d'une entrée bâtiment
#define MAX_ENTRIES      4096
#define STAMP_BIN        "C:\\anno117-stamps\\current.stamp.bin"
#define LOG_PATH         "C:\\anno117-pipette.log"
#define HOTKEY_VK        VK_NUMPAD0  // Ctrl + Numpad0 (le jeu ne binde pas Ctrl+Numpad*)

// ---------------------------------------------------------------- log
static HANDLE g_log = INVALID_HANDLE_VALUE;
static CRITICAL_SECTION g_log_cs;
static BOOL g_log_ready = FALSE;

static void log_init(void) {
    if (g_log_ready) return;
    InitializeCriticalSection(&g_log_cs);
    g_log = CreateFileA(LOG_PATH, GENERIC_WRITE, FILE_SHARE_READ, NULL,
                        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    g_log_ready = TRUE;
}

static void plog(const char* fmt, ...) {
    char buf[768];
    va_list ap;
    DWORD w;
    if (!g_log_ready) log_init();
    if (g_log == INVALID_HANDLE_VALUE) return;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    buf[sizeof(buf) - 1] = 0;
    va_end(ap);
    EnterCriticalSection(&g_log_cs);
    WriteFile(g_log, buf, lstrlenA(buf), &w, NULL);
    WriteFile(g_log, "\r\n", 2, &w, NULL);
    FlushFileBuffers(g_log);
    LeaveCriticalSection(&g_log_cs);
}

// ---------------------------------------------------------------- entrée 0x60
// LAYOUT RÉEL (dump d'une source de paste réelle) :
//   +0x00 : float x   ← POSITION MONDE (1183.0, 831.5 ...)
//   +0x04 : float z
//   +0x08 : u8 Dir (Direction8)
//   +0x0c : u32 GUID  ← le type de bâtiment (3087 ✓)
//   +0x10 : i32 Skin ; +0x14 : u32 Variation ; +0x18 : bool IsBluePrint
//   +0x44 : u64 ComplexOwnerID (sérialiseur natif FUN_143417110)
//   +0x30 : u32 id d'instance (44, 45, 46 ... incrémental)
//   reste : ids internes de zone
#pragma pack(push, 1)
typedef struct {
    float     x;             // +0x00  <-- Pos.x (MONDE)
    float     z;             // +0x04  <-- Pos.z (MONDE)
    BYTE      pad08[4];      // +0x08
    UINT32    guid;          // +0x0c  <-- GUID
    BYTE      rest[0x50];    // +0x10..+0x5f (ids internes -> laissés à 0)
} Entry60;
#pragma pack(pop)

typedef struct {
    UINT32 magic;            // 'STP1'
    UINT32 count;
} StampHeader;

typedef struct {
    UINT32 guid;
    float  x, z;
    INT32  dir;
    UINT32 owner;
} StampEntryIn;

static Entry60 g_entries[MAX_ENTRIES];
static int     g_count = 0;
static volatile LONG g_pending = 0;      // remplissage armé (fait par Ctrl+Numpad0)
static volatile LONG g_dump_wanted = 0;  // dump demandé (Ctrl+Numpad9) -> exécuté dans le hook

// ---------------------------------------------------------------- adresses
static void* g_getter = NULL;
static void* g_veccopy = NULL;
static void* g_alloc = NULL;   /* FUN_144d8bc00 : allocateur du moteur */
static void* g_free = NULL;    /* FUN_144dbf0bc : liberation du moteur */

static BOOL readable(const void* p, SIZE_T len) {
    MEMORY_BASIC_INFORMATION mbi;
    if (!p) return FALSE;
    if (!VirtualQuery(p, &mbi, sizeof(mbi))) return FALSE;
    if (mbi.State != MEM_COMMIT) return FALSE;
    if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return FALSE;
    if (!(mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ |
                         PAGE_EXECUTE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_WRITECOPY)))
        return FALSE;
    return ((UINT_PTR)p + len - 1) <= ((UINT_PTR)mbi.BaseAddress + mbi.RegionSize - 1);
}

// cherche une signature d'octets dans l'image du module
static void* find_sig(HMODULE mod, const BYTE* sig, int siglen, int adjust) {
    BYTE* base = (BYTE*)mod;
    IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)base;
    IMAGE_NT_HEADERS64* nt;
    BYTE* end;
    BYTE* p;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return NULL;
    nt = (IMAGE_NT_HEADERS64*)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return NULL;
    end = base + nt->OptionalHeader.SizeOfImage - siglen;
    for (p = base; p < end; p++) {
        if (*p != sig[0]) continue;
        if (memcmp(p, sig, siglen) == 0)
            return (void*)(p + adjust);
    }
    return NULL;
}

// ---------------------------------------------------------------- bibliothèque de cachets
// stamps.bin : u32 magic 'STPL', u32 nb, puis par cachet { u32 nom_len, nom, u32 nb_bat,
// puis nb_bat x { u32 guid, f32 x, f32 z } }
#define LIB_PATH   "C:\\anno117-stamps\\stamps.bin"
#define MAX_LIB    512

typedef struct { char name[1024]; int count; int off; int rcount; int roff;
                 int bb[4];                 /* bbox bâtiments (unités) */
                 unsigned int mtiles;       /* TileCount du mesh */
                 int maabb[4];              /* AABB du mesh (unités) */
                 float mdrc[2]; } LibStamp; /* DragRectCenter du mesh */
static BYTE*    g_lib = NULL;
static int      g_lib_count = 0;
static int      g_lib_index = -1;
static LibStamp g_lib_tab[MAX_LIB];
static SRWLOCK g_lib_lock = SRWLOCK_INIT;

static int lib_load_from(const char* path) {
    HANDLE h;
    DWORD rd = 0, size;
    BYTE* buf;
    UINT32 n, i, pos;
    LibStamp* parsed;

    h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) { plog("[i] %.200s absent (bibliotheque non preparee)", path); return 0; }
    size = GetFileSize(h, NULL);
    if (size < 8 || size > 16u * 1024 * 1024) { CloseHandle(h); return 0; }
    buf = (BYTE*)HeapAlloc(GetProcessHeap(), 0, size);
    if (!buf) { CloseHandle(h); return 0; }
    ReadFile(h, buf, size, &rd, NULL);
    CloseHandle(h);
    if (rd != size || *(UINT32*)buf != 0x4C505453u /* 'STPL' */) {
        HeapFree(GetProcessHeap(), 0, buf);
        plog("[!] %.200s invalide", path);
        return 0;
    }
    n = *(UINT32*)(buf + 4);
    if (!n || n > MAX_LIB) { HeapFree(GetProcessHeap(), 0, buf); return 0; }
    parsed = (LibStamp*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(g_lib_tab));
    if (!parsed) { HeapFree(GetProcessHeap(), 0, buf); return 0; }
    pos = 8;
    for (i = 0; i < n; i++) {
        UINT32 nlen, cnt, rcnt;
        if (size - pos < 4) break;
        nlen = *(UINT32*)(buf + pos);
        pos += 4;
        if (!nlen || nlen >= sizeof(parsed[i].name) || nlen > size - pos) break;
        memcpy(parsed[i].name, buf + pos, nlen);
        pos += nlen;
        if (size - pos < 48) break;
        memcpy(parsed[i].bb, buf + pos, 16);
        parsed[i].mtiles = *(UINT32*)(buf + pos + 16);
        memcpy(parsed[i].maabb, buf + pos + 20, 16);
        memcpy(parsed[i].mdrc, buf + pos + 36, 8);
        cnt = *(UINT32*)(buf + pos + 44);
        pos += 48;
        if (!cnt || cnt > MAX_ENTRIES || cnt > (size - pos) / 12u) break;
        parsed[i].count = (int)cnt;
        parsed[i].off = (int)pos;
        pos += cnt * 12u;
        if (size - pos < 4) break;
        rcnt = *(UINT32*)(buf + pos);
        pos += 4;
        if (rcnt > (size - pos) / 20u) break;
        parsed[i].rcount = (int)rcnt;
        parsed[i].roff = (int)pos;
        pos += rcnt * 20u;
    }
    if (i != n || pos != size) {
        plog("[!] bibliotheque tronquee/invalide : %.200s", path);
        HeapFree(GetProcessHeap(), 0, parsed);
        HeapFree(GetProcessHeap(), 0, buf);
        return 0;
    }
    if (g_lib) HeapFree(GetProcessHeap(), 0, g_lib);
    memcpy(g_lib_tab, parsed, sizeof(g_lib_tab));
    HeapFree(GetProcessHeap(), 0, parsed);
    g_lib = buf;
    g_lib_index = -1;
    g_lib_count = (int)i;
    plog("[i] bibliotheque : %d cachet(s) (%lu octets)", g_lib_count, (unsigned long)size);
    for (i = 0; i < (UINT32)g_lib_count && i < 8; i++)
        plog("      - \"%s\" (%d bat., %d routes)", g_lib_tab[i].name, g_lib_tab[i].count, g_lib_tab[i].rcount);
    return g_lib_count;
}

static int lib_load(void) { return lib_load_from(LIB_PATH); }

static int lib_select_index(int index) {
    LibStamp* s;
    int i;
    if (!g_lib || index < 0 || index >= g_lib_count) return 0;
#ifdef STAMPS_NATIVE_UI
    g_native_active = FALSE;
#endif
    g_lib_index = index;
    s = &g_lib_tab[g_lib_index];
    if (s->count <= 0 || s->count > MAX_ENTRIES) return 0;
    ZeroMemory(g_entries, sizeof(g_entries));
    for (i = 0; i < s->count; i++) {
        BYTE* p = g_lib + s->off + i * 12;
        g_entries[i].guid = *(UINT32*)p;
        g_entries[i].x = *(float*)(p + 4);
        g_entries[i].z = *(float*)(p + 8);
    }
    g_count = s->count;
    plog("[i] >>> cachet %d/%d : \"%s\" (%d batiments, %d routes)",
         g_lib_index + 1, g_lib_count, s->name, g_count, s->rcount);
    return g_count;
}

// passe au cachet suivant et prépare g_entries
static int lib_select_next(void) {
    if (!g_lib && !lib_load()) return 0;
    if (g_lib_count <= 0) return 0;
    return lib_select_index((g_lib_index + 1) % g_lib_count);
}

// ---------------------------------------------------------------- remplissage
// IMPORTANT : doit tourner sur le thread LOGIQUE du jeu (contexte TLS).
static int do_fill(void) {
    typedef unsigned long long (*getter_t)(void);
    typedef void (*veccopy_t)(void* vec, void* src, unsigned long long count);

    unsigned long long ctx, pipette, v1b, v1e, v2b, v2e;
    int n;

    if (g_count <= 0) { plog("[!] aucun batiment charge (Ctrl+Numpad0 sur un stamp.bin valide)"); return 0; }
    if (!g_getter || !g_veccopy) { plog("[!] signatures non resolues -> abandon"); return 0; }

    ctx = ((getter_t)g_getter)();
    if (!ctx) {
        void** tls_array = (void**)(UINT_PTR)__readgsqword(0x58);
        void* tls0 = (tls_array && readable(tls_array, 8)) ? tls_array[0] : NULL;
        void* sess = NULL;
        if (tls0 && readable(tls0, 0x1c8)) sess = *(void**)((char*)tls0 + 0x1c0);
        plog("[!] contexte=0  diag TLS: tls_array=%p tls0=%p session(+0x1c0)=%p",
             (void*)tls_array, tls0, sess);
        return 0;
    }

    pipette = ctx + PIPETTE_OFFSET;
    v1b = *(unsigned long long*)(pipette + PIPETTE_VEC1);
    v1e = *(unsigned long long*)(pipette + PIPETTE_VEC1 + 8);
    v2b = *(unsigned long long*)(pipette + PIPETTE_VEC2);
    v2e = *(unsigned long long*)(pipette + PIPETTE_VEC2 + 8);
    plog("[i] ctx=%p pipette=%p  v1=%p..%p  v2=%p..%p",
         (void*)ctx, (void*)pipette, (void*)v1b, (void*)v1e, (void*)v2b, (void*)v2e);

    // garde-fous : la pipette doit etre coherente
    if ((v1b && !readable((void*)v1b, ENTRY_SIZE)) || (v1e < v1b) ||
        ((v1e - v1b) % ENTRY_SIZE) != 0) {
        plog("[!] pipette incoherente -> abandon (rien ecrit)");
        return 0;
    }
    if (v2b != v2e) {
        plog("[!] 2e vecteur non vide (%llu o) : CreateStamp prendra le chemin complexe",
             (unsigned long long)(v2e - v2b));
    }

    n = g_count;
    ((veccopy_t)g_veccopy)((void*)(pipette + PIPETTE_VEC1), g_entries, (unsigned long long)n);
    *(volatile BYTE*)pipette = 1;      // "pipette active"

    /* BOÎTE ENGLOBANTE (drag rect) : le moteur la lit en +0x10/+0x14 (min) et
       +0x18/+0x1c (max) — sans elle, le fantôme du cachet est un carré vide. */
#ifdef STAMPS_NATIVE_UI
    if (g_native_active && g_native_stamp) {
        memcpy((void*)(pipette + 0x10), g_native_stamp->bbox, 16);
    } else
#endif
    if (g_lib && g_lib_index >= 0 && g_lib_index < g_lib_count) {
        LibStamp* ls = &g_lib_tab[g_lib_index];
        *(INT32*)(pipette + 0x10) = ls->bb[0];
        *(INT32*)(pipette + 0x14) = ls->bb[1];
        *(INT32*)(pipette + 0x18) = ls->bb[2];
        *(INT32*)(pipette + 0x1c) = ls->bb[3];
        plog("[i] bbox écrite : (%d,%d)..(%d,%d)", ls->bb[0], ls->bb[1], ls->bb[2], ls->bb[3]);
    }

    v1b = *(unsigned long long*)(pipette + PIPETTE_VEC1);
    v1e = *(unsigned long long*)(pipette + PIPETTE_VEC1 + 8);
    plog("[OK] %d batiment(s) -> pipette (v1=%p..%p, %llu entrees)",
         n, (void*)v1b, (void*)v1e, (unsigned long long)((v1e - v1b) / ENTRY_SIZE));
    return v1e >= v1b && (v1e - v1b) / ENTRY_SIZE == (unsigned long long)n;
}

// ---------------------------------------------------------------- dump pipette
// Dump hexa des entrées d'un objet pipette-like (vecteur à +0x20 ET +0x520).
static void dump_vec(const char* tag, unsigned long long obj, int vecoff) {
    unsigned long long b, e;
    int i, n, k;
    unsigned char* p;

    b = *(unsigned long long*)(obj + vecoff);
    e = *(unsigned long long*)(obj + vecoff + 8);
    n = (int)((e - b) / ENTRY_SIZE);
    plog("[dump:%s+0x%x] obj=%p v=%p..%p  %d entree(s)", tag, vecoff, (void*)obj, (void*)b, (void*)e, n);
    if (n <= 0) return;
    if (n > 2) n = 2;                       // 2 premieres suffisent
    for (i = 0; i < n; i++) {
        p = (unsigned char*)(b + (unsigned long long)i * ENTRY_SIZE);
        if (!readable(p, ENTRY_SIZE)) { plog("[dump] entree %d illisible", i); break; }
        for (k = 0; k < ENTRY_SIZE; k += 16) {
            plog("[dump] e%d %02x: %02x %02x %02x %02x %02x %02x %02x %02x  %02x %02x %02x %02x %02x %02x %02x %02x",
                 i, k,
                 p[k], p[k+1], p[k+2], p[k+3], p[k+4], p[k+5], p[k+6], p[k+7],
                 p[k+8], p[k+9], p[k+10], p[k+11], p[k+12], p[k+13], p[k+14], p[k+15]);
        }
        plog("[dump] e%d  +0x00=%llx  +0x08=%llx  guid(+0xc)=%u  +0x14=%u  +0x18=%llx  +0x30=%llx",
             i, *(unsigned long long*)p, *(unsigned long long*)(p + 8),
             *(UINT32*)(p + 0xc), *(UINT32*)(p + 0x14),
             *(unsigned long long*)(p + 0x18), *(unsigned long long*)(p + 0x30));
    }
}

static void dump_object(const char* tag, unsigned long long obj) {
    unsigned long long off;
    int found = 0;
    if (!obj) { plog("[dump:%s] objet=0", tag); return; }
    dump_vec(tag, obj, PIPETTE_VEC1);       // bâtiments (+0x20)
    dump_vec(tag, obj, PIPETTE_VEC2);       // routes  (+0x520)

    /* SCAN : où est le GUID de route (8541 = 0x215D) dans l'objet ? */
    for (off = 0; off < 0x600; off += 4) {
        if (!readable((void*)(obj + off), 4)) break;
        if (*(UINT32*)(obj + off) == 8541u) {
            plog("[dump:%s] ROUTE guid=8541 a +0x%llx  (contexte: %08x %08x %08x %08x)",
                 tag, off,
                 (unsigned)*(UINT32*)(obj + off - 8), (unsigned)*(UINT32*)(obj + off - 4),
                 (unsigned)*(UINT32*)(obj + off + 4), (unsigned)*(UINT32*)(obj + off + 8));
            if (++found >= 4) break;
        }
    }
    if (!found) plog("[dump:%s] aucun guid de route (8541) dans les 0x600 premiers octets", tag);
    /* SCAN : coordonnée de route typique (0x959 = 2393) */
    found = 0;
    for (off = 0; off < 0x600; off += 4) {
        if (!readable((void*)(obj + off), 4)) break;
        if (*(UINT32*)(obj + off) == 0x959u) {
            plog("[dump:%s] coord route 0x959 a +0x%llx", tag, off);
            if (++found >= 3) break;
        }
    }
    /* MESH : conteneur à +0x38+0x08 -> { flags(+0x08), COUNT(+0x10), POINTEUR(+0x18) }
       Les données sont un TABLEAU SUR LE TAS, éléments de 0x18 octets.
       (Formats wvsprintfA : %x/%u/%d seulement -> on imprime les moitiés 32 bits.) */
    {
        UINT32 cnt = *(UINT32*)(obj + 0x38 + 0x10);
        UINT64 ptr = *(UINT64*)(obj + 0x38 + 0x18);
        plog("[mesh:%s] count=%u  ptr=%08x%08x", tag, cnt,
             (unsigned)(ptr >> 32), (unsigned)(ptr & 0xffffffffu));
        if (cnt > 0 && cnt < 512 && ptr >= 0x10000 &&
            readable((void*)ptr, (SIZE_T)cnt * 0x18)) {
            unsigned int i;
            for (i = 0; i < cnt && i < 6; i++) {
                unsigned char* q = (unsigned char*)(ptr + (UINT64)i * 0x18);
                plog("[mesh]  e%u : %08x %08x %08x %08x %08x %08x", i,
                     *(unsigned*)(q), *(unsigned*)(q + 4), *(unsigned*)(q + 8),
                     *(unsigned*)(q + 12), *(unsigned*)(q + 16), *(unsigned*)(q + 20));
            }
        }
    }
    /* dump hexa du sous-objet mesh (+0x38), 0x80 octets */
    if (readable((void*)(obj + 0x38), 0x80)) {
        unsigned char* p = (unsigned char*)(obj + 0x38);
        int k;
        plog("[dump:%s] sous-objet +0x38 (mesh) :", tag);
        for (k = 0; k < 0x80; k += 16)
            plog("[dump] +38 %02x: %02x %02x %02x %02x %02x %02x %02x %02x  %02x %02x %02x %02x %02x %02x %02x %02x",
                 k, p[k], p[k+1], p[k+2], p[k+3], p[k+4], p[k+5], p[k+6], p[k+7],
                 p[k+8], p[k+9], p[k+10], p[k+11], p[k+12], p[k+13], p[k+14], p[k+15]);
    }
}

// dump de la pipette du joueur (contexte TLS -> +0x170)
static void dump_entries(const char* tag) {
    typedef unsigned long long (*getter_t)(void);
    unsigned long long ctx;
    if (!g_getter) { plog("[dump] getter absent"); return; }
    ctx = ((getter_t)g_getter)();
    if (!ctx) { plog("[dump] contexte=0"); return; }
    dump_object(tag, ctx + PIPETTE_OFFSET);
}

// ---------------------------------------------------------------- hook remplissage
// FUN_1433a7610(dst, src, flag) — appelé par le PASTE avec la SOURCE RÉELLE
// (celle qui contient les positions). On dumpe source + destination.
static void mesh_restore(void);          // définie plus bas (capture/restore du mesh)

typedef void (*Fill_t)(void*, void*, char);
static Fill_t orig_Fill = NULL;

// remplace les entrées d'un objet pipette-like par les nôtres (helper du moteur)
static void fill_object(const char* tag, unsigned long long obj) {
    typedef void (*veccopy_t)(void* vec, void* src, unsigned long long count);
    unsigned long long b, e;
    if (!obj || !g_veccopy || g_count <= 0) return;
    b = *(unsigned long long*)(obj + PIPETTE_VEC1);
    e = *(unsigned long long*)(obj + PIPETTE_VEC1 + 8);
    plog("[i] %s : %p  v1=%p..%p (%d entrees) -> remplacement par %d entrees",
         tag, (void*)obj, (void*)b, (void*)e, (int)((e - b) / ENTRY_SIZE), g_count);
    ((veccopy_t)g_veccopy)((void*)(obj + PIPETTE_VEC1), g_entries, (unsigned long long)g_count);
    b = *(unsigned long long*)(obj + PIPETTE_VEC1);
    e = *(unsigned long long*)(obj + PIPETTE_VEC1 + 8);
    plog("[OK] %s rempli : %llu entrees", tag, (unsigned long long)((e - b) / ENTRY_SIZE));
}

static void hook_Fill(void* dst, void* src, char flag) {
    if (g_dump_wanted) {
        InterlockedExchange(&g_dump_wanted, 0);
        plog("[i] FUN_1433a7610 : dst=%p src=%p flag=%d", dst, src, (int)flag);
        dump_object("SOURCE", (unsigned long long)src);
        dump_object("DEST", (unsigned long long)dst);
    }
    if (g_pending && flag) {
        AcquireSRWLockExclusive(&g_lib_lock);
        if (g_pending) {
            // paste armé (Ctrl+Numpad0 puis Ctrl+V) -> on remplace la source
            InterlockedExchange(&g_pending, 0);
            plog("[i] PASTE intercepte -> injection du cachet");
            fill_object("SOURCE", (unsigned long long)src);
        }
        ReleaseSRWLockExclusive(&g_lib_lock);
    }
    orig_Fill(dst, src, flag);
}

// ---------------------------------------------------------------- hook BuildStamp
// FUN_144318f90 : pose le cachet de la région courante. Il REMPLIT la pipette
// depuis le payload du cachet -> en dumpant la pipette juste après, on voit le
// layout RÉEL des entrées (avec les positions) d'un vrai cachet.
typedef long long (*BuildStamp_t)(long long, unsigned long long);
static BuildStamp_t orig_BuildStamp = NULL;

static long long hook_BuildStamp(long long self, unsigned long long ret) {
    long long r;
    if (g_dump_wanted) plog("[i] BuildStamp : dump demande -> on dumpe la pipette APRES");
    r = orig_BuildStamp(self, ret);
    if (g_dump_wanted) {
        InterlockedExchange(&g_dump_wanted, 0);
        dump_entries("apres-BuildStamp");
    }
    return r;
}

// ---------------------------------------------------------------- capture/restore du MESH (routes)
// La pipette contient le mesh (routes) inline à +0x38 (~1 Ko). On le capture tel quel
// et on le réinjecte plus tard -> les routes reviennent (le moteur place le mesh).
#define MESH_OFF   0x38
#define MESH_SIZE  0x400

static BYTE      g_mesh[MESH_SIZE];
static ULONGLONG g_mesh_src = 0;      // objet d'origine (pour rebaser les pointeurs internes)
static volatile LONG g_mesh_wanted = 0;
static volatile LONG g_mesh_ok = 0;

// capture depuis la pipette LIVE (à faire quand la copie du joueur est en main)
static void mesh_capture(void) {
    typedef unsigned long long (*getter_t)(void);
    unsigned long long ctx, obj;
    if (!g_getter) return;
    ctx = ((getter_t)g_getter)();
    if (!ctx) { plog("[mesh] contexte=0 -> capture impossible"); return; }
    obj = ctx + PIPETTE_OFFSET;
    if (!readable((void*)(obj + MESH_OFF), MESH_SIZE)) { plog("[mesh] zone illisible"); return; }
    memcpy(g_mesh, (void*)(obj + MESH_OFF), MESH_SIZE);
    g_mesh_src = obj;
    InterlockedExchange(&g_mesh_ok, 1);
    plog("[mesh] capture OK depuis %p (0x%x octets) : mesh=+0x%x  ptr=+0x%x",
         (void*)obj, MESH_SIZE, MESH_OFF, MESH_OFF + 0x18);
}

// réinjecte le mesh dans la pipette (rebase des pointeurs internes qui pointaient dans l'objet)
static void mesh_restore(void) {
    typedef unsigned long long (*getter_t)(void);
    unsigned long long ctx, obj, off;
    int fixed = 0;
    if (!g_mesh_ok || !g_getter) return;
    ctx = ((getter_t)g_getter)();
    if (!ctx) { plog("[mesh] contexte=0 -> restore impossible"); return; }
    obj = ctx + PIPETTE_OFFSET;
    if (!readable((void*)(obj + MESH_OFF), MESH_SIZE)) return;
    memcpy((void*)(obj + MESH_OFF), g_mesh, MESH_SIZE);
    // rebase : tout pointeur qui visait l'ancien objet -> le nouvel objet
    if (g_mesh_src) {
        for (off = 0; off + 8 <= MESH_SIZE; off += 8) {
            unsigned long long v = *(unsigned long long*)(g_mesh + off);
            if (v >= g_mesh_src && v < g_mesh_src + 0x800) {
                unsigned long long nv = obj + (v - g_mesh_src);
                memcpy((void*)(obj + MESH_OFF + off), &nv, 8);
                fixed++;
            }
        }
    }
    plog("[mesh] restore OK dans %p (%d pointeur(s) rebase(s))", (void*)obj, fixed);
}

// capture AUTO : si la pipette LIVE a du contenu (copie du joueur en main), on garde son mesh
static void mesh_capture_auto(void) {
    typedef unsigned long long (*getter_t)(void);
    unsigned long long ctx, obj, b, e;
    if (!g_getter) return;
    ctx = ((getter_t)g_getter)();
    if (!ctx) return;
    obj = ctx + PIPETTE_OFFSET;
    b = *(unsigned long long*)(obj + PIPETTE_VEC1);
    e = *(unsigned long long*)(obj + PIPETTE_VEC1 + 8);
    if (e <= b) return;                       // pipette vide -> rien à capturer
    mesh_capture();
}

// ÉCRITURE du mesh (routes) dans la pipette à partir des segments du cachet chargé.
// Structure décodée des dumps :
//   +0x10 : u32 = nombre de POINTS
//   +0x18 : pointeur interne (laissé tel quel)
//   +0x20 : les points — 8 octets chacun (x, z en DEMI-UNITÉS), 2 par segment,
//           séparés par un point (0,0)
static int mesh_write_from_roads(void) {
    typedef unsigned long long (*getter_t)(void);
    typedef void* (*alloc_t)(unsigned long long);
    unsigned long long ctx, obj;
    unsigned char* p;
    LibStamp* s;
#ifdef STAMPS_NATIVE_UI
    LibStamp native_mesh = {0};
#endif
    int i, n;
    void* arr;
    void* oldarr;

    if (!g_getter) return 0;
#ifdef STAMPS_NATIVE_UI
    if (g_native_active && g_native_stamp) {
        native_mesh.rcount = (int)g_native_stamp->road_count;
        native_mesh.mtiles = g_native_stamp->tiles;
        memcpy(native_mesh.maabb, g_native_stamp->aabb, 16);
        memcpy(native_mesh.mdrc, g_native_stamp->centre, 8);
        s = &native_mesh;
    } else
#endif
    {
        if (!g_lib || g_lib_count <= 0 || g_lib_index < 0) return 0;
        s = &g_lib_tab[g_lib_index];
    }
#ifndef STAMPS_NATIVE_UI
    if (s->rcount <= 0) { plog("[mesh] ce cachet n'a pas de routes"); return 1; }
#endif

    ctx = ((getter_t)g_getter)();
    if (!ctx) return 0;
    obj = ctx + PIPETTE_OFFSET;
    p = (unsigned char*)(obj + MESH_OFF);
    if (!readable(p, 0x4b0)) return 0; // also covers AABB and DragRectCenter

    n = s->rcount;
    if (n > 256) n = 256;

    /* 1) allouer le tableau (n éléments de 0x18) avec l'ALLOCATEUR DU MOTEUR */
    arr = NULL;
    if (n) {
        if (!g_alloc) return 0;
        arr = ((alloc_t)g_alloc)((unsigned long long)n * 0x18);
        if (!arr) { plog("[mesh] allocation echouee"); return 0; }
    }

    /* 2) remplir : { x1, z1, x2, z2, type(0=Street), param(0) } */
    for (i = 0; i < n; i++) {
        const UINT32* r;
#ifdef STAMPS_NATIVE_UI
        if (g_native_active) r = (const UINT32*)&g_native_stamp->roads[i];
        else
#endif
        r = (const UINT32*)(g_lib + s->roff + i * 20); /* guid, minx, minz, maxx, maxz */
        unsigned char* q = (unsigned char*)((UINT_PTR)arr + (UINT64)i * 0x18);
        *(UINT32*)(q + 0)  = r[1];
        *(UINT32*)(q + 4)  = r[2];
        *(UINT32*)(q + 8)  = r[3];
        *(UINT32*)(q + 12) = r[4];
        *(UINT32*)(q + 16) = 0;      /* MeshGraphType::Street */
        *(UINT32*)(q + 20) = 0;
    }

    /* 3) conteneur : flags(+0x08) = count*2+1, count(+0x10), pointeur(+0x18) */
    /* v83 : NE PLUS LIBÉRER l'ancien tableau — il appartient au moteur (use-after-free -> freeze).
       On accepte une petite fuite (n*0x18 octets par remplissage) plutôt qu'un crash. */
    (void)oldarr;
    *(UINT64*)(p + 0x08) = (UINT64)(n * 2 + 1);
    *(UINT64*)(p + 0x10) = (UINT64)n;
    *(UINT64*)(p + 0x18) = (UINT64)(UINT_PTR)arr;

    /* + les 3 champs de géométrie du mesh (sans eux : fantôme sans dimensions) */
    *(UINT32*)(p + 0x000) = s->mtiles;                 /* TileCount */
    *(INT32*)(p + 0x480) = s->maabb[0];                /* AABB */
    *(INT32*)(p + 0x484) = s->maabb[1];
    *(INT32*)(p + 0x488) = s->maabb[2];
    *(INT32*)(p + 0x48c) = s->maabb[3];
    *(float*)(p + 0x4a8) = s->mdrc[0];                 /* DragRectCenter */
    *(float*)(p + 0x4ac) = s->mdrc[1];

    plog("[mesh] mesh ECRIT : %d aretes -> tableau %08x%08x  (tiles=%u aabb=%d,%d,%d,%d)",
         n, (unsigned)((UINT64)(UINT_PTR)arr >> 32), (unsigned)((UINT_PTR)arr & 0xffffffffu),
         s->mtiles, s->maabb[0], s->maabb[1], s->maabb[2], s->maabb[3]);
    return 1;
}

#ifdef STAMPS_NATIVE_UI
#define UI_ROOT "C:\\anno117-stamps\\native-ui\\"
#define UI_GUID_MIN 1500100000
#define UI_GUID_MAX (UI_GUID_MIN + 0x4000000)
#define UI_CAPTURE_COMMAND UI_GUID_MIN
#define UI_COPY_QUERY (UI_GUID_MIN + 1)
#define UI_CLEAR_COPY (UI_GUID_MIN + 8)
// Empty until the first click; resolved through the user's Windows Documents.
static WCHAR g_ui_stamp_root[32768];
static NativeCatalogue g_live_catalogue;
static char g_live_error[512];

// Immutable strings for the native text provider. Their addresses remain valid
// for the session, including readers still finishing a previous UI frame.
// MSVC basic_string<wchar_t>: 16-byte union, size, capacity (SSO threshold 8).
typedef struct { const WCHAR* data; UINT64 unused, length, capacity; } UiWideString;
typedef struct UiLabel {
    struct UiLabel* previous;
    WCHAR name[256], description[512], icon[512];
    UiWideString name_string, description_string;
    UINT64 name_text_id, description_text_id;
} UiLabel;
static UiLabel* volatile g_ui_labels[LIVE_RECORD_COUNT];
static UiLabel* g_ui_label_history;
static const void* ui_management_text(UINT64 id);
static int ui_update_name_source(uint32_t guid,BYTE* standard,UiLabel* label);

static const void* hook_ui_text_source(void* self, UINT64 id, BYTE make_new, unsigned short language) {
    const void* managed=ui_management_text(id);
    if (managed) return managed;
    UINT64 slot = id - LIVE_POOL_BASE;
    int description = 0;
    UiLabel* label;
    if (slot >= LIVE_RECORD_COUNT) {
        slot = id - (LIVE_POOL_BASE + 100000000u);
        description = 1;
    }
    if (slot < LIVE_RECORD_COUNT) {
        label = InterlockedCompareExchangePointer((PVOID volatile*)&g_ui_labels[slot], NULL, NULL);
        if (label) return description ? &label->description_string : &label->name_string;
    }
    return orig_ui_text_source(self, id, make_new, language);
}

static BYTE* ui_standard(uint32_t guid) {
    typedef void* (*find_t)(void*, uint32_t);
    typedef BYTE* (*standard_t)(void*);
    void* manager;
    void* asset;
    UINT_PTR table;
    BYTE* standard;
    const void* name;
    if (!g_ui_asset_manager || !g_ui_standard_getter || !readable(g_ui_asset_manager,8)) return NULL;
    manager=*g_ui_asset_manager;
    if (!manager || !readable(manager,8)) return NULL;
    table=*(UINT_PTR*)manager;
    if (!readable((void*)table,0x20) || !readable((void*)*(UINT_PTR*)(table+0x18),1)) return NULL;
    asset=((find_t)*(UINT_PTR*)(table+0x18))(manager,guid);
    if (!asset) return NULL;
    standard=((standard_t)g_ui_standard_getter)(asset);
    // CStandard serializer FUN_1426db040: GUID +10, Name +18, IconFilename +20.
    // Check that this is one of our generated assets before changing its icon.
    if (!standard || !readable(standard,0x38) || *(uint32_t*)(standard+0x10)!=guid) return NULL;
    name=*(void**)(standard+0x18);
    if (!readable(name,20) || (memcmp(name,"Stamps_UI_",10) && memcmp(name,L"Stamps_UI_",20))) return NULL;
    return standard;
}

static int ui_publish_presentation(void) {
    uint32_t n, applied=0;
    if (!g_ui_presentation_hook_ready || !g_alloc) return 0;
    for (n=0;n<g_live_catalogue.row_count;++n) {
        uint32_t slot=g_live_catalogue.rows[n], guid=LIVE_POOL_BASE+slot;
        const LiveRecord* row=&g_live_catalogue.records[slot];
        WCHAR name[256], icon[512], description[512];
        BYTE* standard=ui_standard(guid);
        UiLabel* label=g_ui_labels[slot];
        if (!standard || !MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,row->name,-1,name,256) ||
            !MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,row->icon_file,-1,icon,512)) continue;
        if (slot%LIVE_SLOT_STRIDE==0) wcscpy(description,L"Dossier de stamps");
        else swprintf(description,512,L"%u bâtiment(s) · %u case(s) de route · %u zone(s)",
                      row->building_count,row->road_tiles,row->polygon_count);
        if (!label || wcscmp(label->name,name) || wcscmp(label->icon,icon) || wcscmp(label->description,description)) {
            label=calloc(1,sizeof(*label));
            if (!label) continue;
            wcscpy(label->name,name); wcscpy(label->icon,icon); wcscpy(label->description,description);
            label->name_string=(UiWideString){label->name,0,wcslen(name),255};
            label->description_string=(UiWideString){label->description,0,wcslen(description),511};
            label->previous=g_ui_label_history; g_ui_label_history=label;
            InterlockedExchangePointer((PVOID volatile*)&g_ui_labels[slot],label);
        }
        if (icon[0]) {
            WCHAR* current=*(WCHAR**)(standard+0x20);
            size_t bytes=(wcslen(icon)+1)*sizeof(WCHAR);
            if (!readable(current,bytes) || wcsncmp(current,icon,bytes/sizeof(WCHAR))) {
                typedef void* (*alloc_t)(UINT64);
                WCHAR* owned=((alloc_t)g_alloc)(bytes);
                if (!owned) continue;
                memcpy(owned,icon,bytes);
                // IconFilename owns a plain engine-allocated UTF-16 string.
                // Retain its old buffer: another render thread may still read it.
                InterlockedExchangePointer((PVOID volatile*)(standard+0x20),owned);
            }
        }
        if (!ui_update_name_source(guid,standard,label)) continue;
        ++applied;
    }
    for (n=0;n<LIVE_RECORD_COUNT;++n)
        if (!g_live_catalogue.records[n].active)
            InterlockedExchangePointer((PVOID volatile*)&g_ui_labels[n],NULL);
    if (applied!=g_live_catalogue.row_count) return 0;
    g_ui_presentation_revision=g_live_catalogue.revision;
    plog("[UI] sources natives actualisees : revision=%u entrees=%u (icone et texte)",
         g_ui_presentation_revision,applied);
    return 1;
}

static int ui_stamp_root(void) {
    if (!g_ui_stamp_root[0]) {
        WCHAR documents[MAX_PATH];
        if (FAILED(SHGetFolderPathW(NULL, CSIDL_PERSONAL, NULL, SHGFP_TYPE_CURRENT, documents))) return 0;
        swprintf(g_ui_stamp_root, 32768, L"%ls\\Anno 117 - Pax Romana\\stamps", documents);
    }
    return 1;
}
static int ui_refresh_catalogue(int force) {
    uint32_t previous = g_live_catalogue.revision;
    if (!ui_stamp_root()) return 0;
    if (!native_catalogue_refresh(&g_live_catalogue, g_ui_stamp_root,
          L"C:\\anno117-stamps\\native-ui\\icons.tsv", L"C:\\anno117-stamps\\native-ui\\slots.tsv",
          GetTickCount64(), force)) return 0;
    if (g_ui_presentation_revision!=g_live_catalogue.revision) ui_publish_presentation();
    if (previous != g_live_catalogue.revision)
        plog("[UI] catalogue vivant : revision=%u entrees=%u fichiers refuses=%u",
             g_live_catalogue.revision, g_live_catalogue.row_count, g_live_catalogue.errors);
    if (g_live_catalogue.last_error[0] && strcmp(g_live_error, g_live_catalogue.last_error))
        plog("[UI] catalogue : %s", g_live_catalogue.last_error);
    snprintf(g_live_error, sizeof(g_live_error), "%s", g_live_catalogue.last_error);
    return 1;
}

static BYTE* ui_read_file(const WCHAR* path, DWORD limit, DWORD* length) {
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    BYTE* bytes = NULL;
    DWORD size, read = 0;
    if (file == INVALID_HANDLE_VALUE) return NULL;
    size = GetFileSize(file, NULL);
    if (size && size <= limit) {
        bytes = malloc((size_t)size + 1);
        if (bytes && (!ReadFile(file, bytes, size, &read, NULL) || read != size)) {
            free(bytes); bytes = NULL;
        }
        if (bytes) { bytes[size] = 0; *length = size; }
    }
    CloseHandle(file);
    return bytes;
}

static int ui_load_original(unsigned int guid) {
    DWORD size = 0, raw_size = 0;
    BYTE* catalogue = ui_read_file(L"C:\\anno117-stamps\\native-ui\\catalogue.tsv", 1024 * 1024, &size);
    BYTE* raw = NULL;
    char* line;
    WCHAR relative[16384], path[32768];
    char error[256] = "fichier absent du catalogue";
    NativeStamp* parsed = NULL;
    int found = 0, ok = 0;
    if (guid >= LIVE_POOL_BASE && guid < LIVE_POOL_BASE + LIVE_RECORD_COUNT) {
        const LiveRecord* record = native_catalogue_find(&g_live_catalogue, guid);
        free(catalogue); catalogue = NULL;
        if (!record || (guid - LIVE_POOL_BASE) % LIVE_SLOT_STRIDE == 0 ||
            !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, record->path, -1, relative, 16384)) goto done;
        found = 1;
        goto read_original;
    }
    if (!catalogue) goto done;
    line = (char*)catalogue;
    while (*line) {
        char* end = strchr(line, '\n');
        char* separator;
        if (end) *end = 0;
        separator = strchr(line, '\t');
        if (separator) {
            char* number_end;
            unsigned long number = strtoul(line, &number_end, 10);
            if (number_end == separator && number == guid) {
                char* value = separator + 1;
                size_t length = strlen(value);
                if (length && value[length - 1] == '\r') value[--length] = 0;
                // Generated catalogue paths are relative and never traverse out.
                if (!length || strchr(value, ':') || strchr(value, '\\') ||
                    value[0] == '/' || strstr(value, "..") || strchr(value, '\t') ||
                    !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value, -1,
                                         relative, sizeof(relative) / sizeof(*relative))) break;
                found = 1;
                break;
            }
        }
        if (!end) break;
        line = end + 1;
    }
    if (!found) goto done;
read_original:
    if (!ui_stamp_root()) goto done;
    if (wcslen(g_ui_stamp_root) + wcslen(relative) + 2 >= 32768) goto done;
    swprintf(path, 32768, L"%ls\\%ls", g_ui_stamp_root, relative);
    raw = ui_read_file(path, NATIVE_STAMP_MAX_FILE, &raw_size);
    if (!raw) { snprintf(error, sizeof(error), "stamp d'origine introuvable"); goto done; }
    parsed = malloc(sizeof(*parsed));
    if (!parsed || !native_stamp_read(raw, raw_size, parsed, error, sizeof(error))) goto done;
    free(g_native_stamp);
    g_native_stamp = parsed;
    parsed = NULL;
    g_native_active = TRUE;
    ZeroMemory(g_entries, sizeof(g_entries));
    memcpy(g_entries, g_native_stamp->entries, g_native_stamp->count * ENTRY_SIZE);
    g_count = (int)g_native_stamp->count;
    plog("[UI] fichier original lu : %u batiments, %u routes, %u zones ; aucune ecriture de stamp",
         g_native_stamp->count, g_native_stamp->road_count, g_native_stamp->polygon_count);
    ok = 1;
done:
    if (!ok) plog("[UI] lecture du stamp %u refusee : %s", guid, error);
    free(parsed); free(raw); free(catalogue);
    return ok;
}
static void* g_targetgetter = NULL;
typedef struct { unsigned int guid; DWORD thread; unsigned long long sequence; } UIRequest;
static UIRequest g_ui_selected = {0}, g_ui_completed = {0};
static unsigned long long g_ui_sequence = 0;
static int g_ui_success = 0;
static volatile LONG g_ui_bridge_ready = 0;
typedef unsigned long long (*StampIcon_t)(long long, int, unsigned long long);
static StampIcon_t orig_StampIcon = NULL;

static int ui_copy_active(void) {
    typedef UINT_PTR (*target_getter_t)(void);
    typedef unsigned short (*mode_t)(void*);
    UINT_PTR manager, target, table, method;
    if (!g_targetgetter) return 0;
    manager = ((target_getter_t)g_targetgetter)();
    if (!manager || !readable((void*)(manager + 0x830), sizeof(UINT_PTR))) return 0;
    target = *(UINT_PTR*)(manager + 0x830);
    if (!readable((void*)target, sizeof(UINT_PTR))) return 0;
    table = *(UINT_PTR*)target;
    if (!readable((void*)(table + 0x20), sizeof(UINT_PTR))) return 0;
    method = *(UINT_PTR*)(table + 0x20);
    if (!readable((void*)method, 1)) return 0;
    return ((mode_t)method)((void*)target) == 7; // datasets.TargetMode.Copy
}

static int ui_capture_available(void) {
    typedef UINT_PTR (*getter_t)(void);
    UINT_PTR context, pipette, begin, end;
    if (!g_getter) return 0;
    context = ((getter_t)g_getter)();
    if (!context || !readable((void*)(context + PIPETTE_OFFSET), PIPETTE_VEC1 + 16)) return 0;
    pipette = context + PIPETTE_OFFSET;
    begin = *(UINT_PTR*)(pipette + PIPETTE_VEC1);
    end = *(UINT_PTR*)(pipette + PIPETTE_VEC1 + 8);
    return begin && end > begin && (end - begin) % ENTRY_SIZE == 0 &&
           (end - begin) / ENTRY_SIZE <= MAX_ENTRIES && readable((void*)begin, end - begin);
}

#include "native_management.inc"

// Le Lua du jeu n'expose pas io. Utiliser uniquement la plage de GUIDs du mod
// comme commandes synchrones : +GUID prépare, -GUID consomme l'accusé de fill.
// Chaque sélection invalide l'ancien accusé ; l'appelant doit rester sur le même
// thread. Aucun indice de stamp normal ne passe par ce protocole.
static unsigned long long hook_StampIcon(long long self, int stamp_id, unsigned long long state) {
    unsigned int guid;
    unsigned long long result = 0;
    DWORD thread = GetCurrentThreadId();
    if (stamp_id>0 && ui_management_command((uint32_t)stamp_id,&result)) return result;
    if (stamp_id == UI_CLEAR_COPY) {
        typedef UINT_PTR (*getter_t)(void);
        typedef void (*clear_t)(void);
        typedef void (*reset_t)(void*);
        UINT_PTR ctx;
        if (!g_ui_bridge_ready || !g_getter || !g_clear_tool || !g_reset_payload) return 0;
        ctx=((getter_t)g_getter)();
        if (!ctx) return 0;
        // Run the native cleanup before discarding any old selection references.
        ((clear_t)g_clear_tool)();
        ctx=((getter_t)g_getter)();
        if (!ctx) return 0;
        ((reset_t)g_reset_payload)((void*)(ctx+PIPETTE_OFFSET));
        {
            BYTE* p=(BYTE*)(ctx+PIPETTE_OFFSET);
            if (*(UINT_PTR*)(p+PIPETTE_VEC1)!=*(UINT_PTR*)(p+PIPETTE_VEC1+8) ||
                *(UINT_PTR*)(p+PIPETTE_VEC2)!=*(UINT_PTR*)(p+PIPETTE_VEC2+8) ||
                *(UINT32*)(p+MESH_OFF)!=0) return 0;
        }
        AcquireSRWLockExclusive(&g_lib_lock);
        ZeroMemory(&g_ui_selected,sizeof(g_ui_selected));
        ZeroMemory(&g_ui_completed,sizeof(g_ui_completed));
        ++g_ui_sequence; g_ui_success=0; g_pending=0;
        ReleaseSRWLockExclusive(&g_lib_lock);
        plog("[UI] ancienne copie videe nativement avant un nouveau trace");
        return UI_CLEAR_COPY;
    }
    // Requête d'état sans effet de bord : sortir de Copy annule la création.
    if (stamp_id == UI_COPY_QUERY) return g_ui_bridge_ready && ui_copy_active();
    if (stamp_id == (int)LIVE_PRESENTATION)
        return g_ui_presentation_hook_ready && g_ui_presentation_revision &&
               g_ui_presentation_revision==g_live_catalogue.revision;
    if (stamp_id > 0 && native_catalogue_is_query((uint32_t)stamp_id)) {
        if (!g_ui_bridge_ready) return 0;
        AcquireSRWLockExclusive(&g_lib_lock);
        if (stamp_id == (int)LIVE_REVISION || stamp_id == (int)LIVE_REFRESH)
            ui_refresh_catalogue(stamp_id == (int)LIVE_REFRESH);
        result = native_catalogue_query(&g_live_catalogue, (uint32_t)stamp_id);
        ReleaseSRWLockExclusive(&g_lib_lock);
        return result;
    }
    if (!((stamp_id >= UI_GUID_MIN && stamp_id < UI_GUID_MAX) ||
          (stamp_id <= -UI_GUID_MIN && stamp_id > -UI_GUID_MAX)))
        return orig_StampIcon(self, stamp_id, state);
    guid = stamp_id > 0 ? (unsigned int)stamp_id : (unsigned int)-stamp_id;
    AcquireSRWLockExclusive(&g_lib_lock);
    if (stamp_id > 0) {
        ZeroMemory(&g_ui_selected, sizeof(g_ui_selected));
        ZeroMemory(&g_ui_completed, sizeof(g_ui_completed));
        g_ui_success = 0;
        ++g_ui_sequence;
        if (g_ui_bridge_ready) {
            g_ui_selected = (UIRequest){guid, thread, g_ui_sequence};
            result = guid;
        }
    } else if (g_ui_completed.guid == guid && g_ui_completed.thread == thread &&
               g_ui_completed.sequence == g_ui_sequence) {
        if (g_ui_success) result = guid;
        ZeroMemory(&g_ui_completed, sizeof(g_ui_completed));
        g_ui_success = 0;
    }
    ReleaseSRWLockExclusive(&g_lib_lock);
    return result;
}

// Appelé exclusivement depuis le hook Lua, donc avec le bon contexte TLS.
// g_lib_lock détenu par l'appelant.
static int ui_take_request(UIRequest* request) {
    if (!g_ui_selected.guid || g_ui_selected.thread != GetCurrentThreadId()) return 0;
    *request = g_ui_selected;
    ZeroMemory(&g_ui_selected, sizeof(g_ui_selected));
    plog("[UI] selection native GUID=%u sequence=%llu", request->guid, request->sequence);
    if (request->guid == UI_CAPTURE_COMMAND) {
        // Enregistrer LA COPIE RÉELLE : annuler tout ancien chargement armé,
        // sans copier g_entries ni réécrire les routes/positions de la pipette.
        InterlockedExchange(&g_pending, 0);
        return ui_capture_available() ? 2 : -1;
    }
    if (!ui_load_original(request->guid)) {
        InterlockedExchange(&g_pending, 0);
        return -1;
    }
    InterlockedExchange(&g_pending, 1);
    return 1;
}
#endif

// ---------------------------------------------------------------- hook CreateStamp
#ifdef STAMPS_NATIVE_UI
// Reproduire ClearTool -> Fill(payload distinct) du chargeur natif.
// Remplacer seulement vec1 sur la pipette vivante perd les références permettant
// au moteur d'enlever les surbrillances et laisse les autres conteneurs de copie.
static int ui_fill_native(void) {
    typedef UINT_PTR (*getter_t)(void);
    typedef void* (*ctor_t)(void*);
    typedef void (*clear_t)(void);
    typedef void (*vec_t)(void*, void*, unsigned long long);
    typedef void* (*alloc_t)(unsigned long long);
    UINT_PTR ctx, begin, end;
    BYTE *mesh, *live;
    uint32_t i, n;
    if (!g_native_active || !g_native_stamp || !g_native_stamp->count ||
        !g_getter || !g_payload_ctor || !g_polygon_ctor || !g_clear_tool || !g_veccopy || !g_alloc || !orig_Fill)
        return 0;
    ctx = ((getter_t)g_getter)();
    if (!ctx || !readable((void*)(ctx + PIPETTE_OFFSET), sizeof(g_ui_payload))) return 0;
    if (!g_ui_payload_ready) {
        g_ui_road_array = ((alloc_t)g_alloc)(256 * 0x18);
        if (!g_ui_road_array) return 0;
        g_ui_polygon_bits = ((alloc_t)g_alloc)(NATIVE_STAMP_POLYGON_BYTES);
        if (!g_ui_polygon_bits) return 0;
        ((ctor_t)g_payload_ctor)(g_ui_payload);
        g_ui_payload_ready = TRUE;
    }
    ((vec_t)g_veccopy)(g_ui_payload + PIPETTE_VEC1, g_entries, (UINT64)g_count);
    mesh = g_ui_payload + MESH_OFF;
    n = g_native_stamp->road_count;
    if (n > 256) return 0;
    for (i=0; i<n; ++i) {
        const INT32* road = &g_native_stamp->roads[i].x1;
        INT32* edge = (INT32*)((BYTE*)g_ui_road_array + i*0x18);
        edge[0]=road[0]; edge[1]=road[1]; edge[2]=road[2]; edge[3]=road[3];
        edge[4]=0; edge[5]=0;
    }
    *(UINT32*)mesh = g_native_stamp->tiles;
    *(UINT64*)(mesh+0x08) = 256*2+1; // capacité, propriété de cette source seulement
    *(UINT64*)(mesh+0x10) = n;
    *(UINT_PTR*)(mesh+0x18) = (UINT_PTR)g_ui_road_array;
    memcpy(mesh+0x480, g_native_stamp->aabb, 16);
    memcpy(mesh+0x4a8, g_native_stamp->centre, 8);
    {
        typedef void* (*poly_ctor_t)(void*,UINT64);
        UINT_PTR* vector=(UINT_PTR*)(g_ui_payload+PIPETTE_VEC2);
        ((poly_ctor_t)g_polygon_ctor)(g_ui_polygons,g_native_stamp->polygon_count);
        memcpy(g_ui_polygon_bits,g_native_stamp->polygon_bits,g_native_stamp->polygon_bytes);
        for (i=0; i<g_native_stamp->polygon_count; ++i) {
            const NativePolygon* polygon=&g_native_stamp->polygons[i];
            BYTE* dest=g_ui_polygons[i];
            *(UINT32*)dest=polygon->guid; dest[4]=(BYTE)polygon->preview_type;
            memcpy(dest+8,polygon->origin,8);
            *(UINT32*)(dest+0x18)=polygon->width;
            *(UINT32*)(dest+0x1c)=polygon->height;
            *(UINT32*)(dest+0x20)=(polygon->width+31)/32;
            *(BYTE**)(dest+0x28)=g_ui_polygon_bits+polygon->bits_offset;
            memcpy(dest+0x30,polygon->relative,8); dest[0x38]=(BYTE)polygon->blueprint;
            memcpy(dest+0x3c,&polygon->owner,8);
            if (polygon->has_rect) memcpy(dest+0x48,polygon->rect,16);
        }
        vector[0]=(UINT_PTR)g_ui_polygons;
        vector[1]=vector[0]+g_native_stamp->polygon_count*0x60;
        vector[2]=vector[0]+sizeof(g_ui_polygons);
    }
    // Toutes les allocations/validations précèdent le nettoyage de l'outil actif.
    ((clear_t)g_clear_tool)();
    ctx = ((getter_t)g_getter)();
    if (!ctx) return 0;
    live = (BYTE*)(ctx + PIPETTE_OFFSET);
    orig_Fill(live, g_ui_payload, 0);
    memcpy(live+0x10, g_native_stamp->bbox, 16);
    begin=*(UINT_PTR*)(live+PIPETTE_VEC1);
    end=*(UINT_PTR*)(live+PIPETTE_VEC1+8);
    plog("[UI] nettoyage natif puis Fill : %u batiments, %u routes, %u zones ; source separee",
         g_native_stamp->count, n, g_native_stamp->polygon_count);
    return end>=begin && end-begin==(UINT_PTR)g_count*ENTRY_SIZE &&
        *(UINT_PTR*)(live+PIPETTE_VEC2+8)-*(UINT_PTR*)(live+PIPETTE_VEC2)==g_native_stamp->polygon_count*0x60;
}
#endif
// FUN_144318e60(long long self, undefined8 ret) — appelé par Stamp:CreateStamp()
// (raccourci Ctrl+Numpad1) sur le THREAD LOGIQUE du jeu.
typedef long long (*CreateStamp_t)(long long, unsigned long long);
static CreateStamp_t orig_CreateStamp = NULL;

static long long hook_CreateStamp(long long self, unsigned long long ret) {
    long long result;
    int filled = 0;
#ifdef STAMPS_NATIVE_UI
    UIRequest ui_command = {0};
    int ui_request;
#endif
    AcquireSRWLockExclusive(&g_lib_lock);
#ifdef STAMPS_NATIVE_UI
    ui_request = ui_take_request(&ui_command);
    if (ui_request == 2) {
        filled = 1; // Copie existante validée, aucun remplissage.
        if (g_management_ready) {
            const LiveRecord* destination=native_catalogue_find(&g_live_catalogue,g_capture_folder);
            WCHAR path[32768];
            if (!destination || !native_library_path(g_ui_stamp_root,destination,path,32768) ||
                !MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,destination->name,-1,g_capture_name_buffer,256)) filled=0;
            else {
                g_capture_name=(UiWideString){g_capture_name_buffer,0,wcslen(g_capture_name_buffer),255};
                plog("[UI] destination de creation : %u %s",g_capture_folder,destination->path);
            }
        }
    } else if (ui_request == 1) {
        InterlockedExchange(&g_pending, 0);
        filled = ui_fill_native();
    } else {
#endif
    if (g_mesh_wanted) {
        InterlockedExchange(&g_mesh_wanted, 0);
        mesh_capture();                      // capture manuelle (Ctrl+Numpad.)
    } else if (!g_pending) {
        mesh_capture_auto();                 // capture AUTO si une copie est en main
    }
    if (g_dump_wanted) {
        InterlockedExchange(&g_dump_wanted, 0);
        dump_entries("live-avant-fill");     // entrées REELLES (copie faite en jeu)
    }
    if (g_pending) {
        InterlockedExchange(&g_pending, 0);
        plog("[i] CreateStamp intercepte (thread logique) -> remplissage de la pipette");
        filled = do_fill();
        if (filled) filled = mesh_write_from_roads(); // v85 : arêtes + TileCount/AABB/DragRect
        dump_entries("apres-fill");          // ce que CreateStamp va lire
    }
#ifdef STAMPS_NATIVE_UI
    }
#endif
    ReleaseSRWLockExclusive(&g_lib_lock);
#ifdef STAMPS_NATIVE_UI
    // Charger remplit la pipette uniquement. Seule la création appelle l'écrivain.
    InterlockedExchange(&g_capture_routed,0);
    if (ui_request==2 && filled && g_management_ready) InterlockedExchange(&g_capture_thread,GetCurrentThreadId());
    result = (!ui_request || (ui_request == 2 && filled)) ? orig_CreateStamp(self, ret) : 0;
    InterlockedExchange(&g_capture_thread,0);
    if (ui_request==2 && filled && g_management_ready && !g_capture_routed) {
        filled=0; plog("[UI] chemin natif du nouveau stamp non confirme");
    }
    if (ui_request == 2 && !result) filled = 0;
    if (ui_request) {
        AcquireSRWLockExclusive(&g_lib_lock);
        if (ui_request == 2 && filled) g_live_catalogue.dirty = 1;
        if (ui_command.sequence == g_ui_sequence) {
            g_ui_completed = ui_command;
            g_ui_success = ui_request > 0 && filled;
        }
        ReleaseSRWLockExclusive(&g_lib_lock);
        plog("[UI] %s %u : %s", ui_command.guid == UI_CAPTURE_COMMAND ? "creation" : "chargement",
             ui_command.guid, ui_request > 0 && filled ? "OK" : "ERR");
    }
#else
    result = orig_CreateStamp(self, ret);
    (void)filled;
#endif
    return result;
}

// ---------------------------------------------------------------- fichier stamp (unitaire)
static int load_stamp_bin(void) {
    HANDLE h;
    DWORD rd = 0;
    StampHeader hdr;
    StampEntryIn in[MAX_ENTRIES];
    int i, n;

    h = CreateFileA(STAMP_BIN, GENERIC_READ, FILE_SHARE_READ, NULL,
                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        plog("[!] %s introuvable (lance tools/stamp2bin.py d'abord)", STAMP_BIN);
        return 0;
    }
    if (!ReadFile(h, &hdr, sizeof(hdr), &rd, NULL) || rd != sizeof(hdr) ||
        hdr.magic != 0x31505453u /* 'STP1' */) {
        plog("[!] entete invalide");
        CloseHandle(h);
        return 0;
    }
    n = (int)hdr.count;
    if (n < 0) n = 0;
    if (n > MAX_ENTRIES) n = MAX_ENTRIES;
    if (!ReadFile(h, in, (DWORD)(n * sizeof(StampEntryIn)), &rd, NULL)) {
        plog("[!] lecture des entrees KO");
        CloseHandle(h);
        return 0;
    }
    CloseHandle(h);
    n = (int)(rd / sizeof(StampEntryIn));

    ZeroMemory(g_entries, sizeof(g_entries));
    for (i = 0; i < n; i++) {
        g_entries[i].x = in[i].x;          // +0x00 (monde)
        g_entries[i].z = in[i].z;          // +0x04
        g_entries[i].guid = in[i].guid;    // +0x0c
    }
    g_count = n;
    plog("[i] %s : %d batiment(s) lus", STAMP_BIN, n);
    if (n > 0)
        plog("[i] 1er: guid=%u pos=(%.2f, %.2f)", in[0].guid, in[0].x, in[0].z);
    return n;
}

// ---------------------------------------------------------------- thread touches
// Le jeu doit être au premier plan : evite de déclencher depuis une autre appli
// (GetAsyncKeyState lit l'état clavier GLOBAL du système).
static BOOL game_focused(void) {
    HWND fg = GetForegroundWindow();
    DWORD pid = 0;
    if (!fg) return FALSE;
    GetWindowThreadProcessId(fg, &pid);
    return pid == GetCurrentProcessId();
}

static DWORD WINAPI hotkey_thread(LPVOID p) {
    BOOL down_prev = FALSE, dump_prev = FALSE, mesh_prev = FALSE;
    (void)p;
    plog("=== v56-pipette : cachet sauvegarde -> pipette -> pose native ===");
#ifdef STAMPS_NATIVE_UI
    plog("[UI] charger/creer depuis le menu Stamps ; fichiers originaux lus au clic");
#else
    plog("[i] Ctrl+Numpad0 = charger le cachet | Ctrl+Numpad1 = creer | Ctrl+Numpad2 = poser");
#endif
    plog("[i] Ctrl+Numpad9 = dump des entrees pipette (a faire AVANT Ctrl+Numpad1)");
    for (;;) {
        BOOL ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
        BOOL down = ctrl && (GetAsyncKeyState(HOTKEY_VK) & 0x8000) && game_focused();
        BOOL dump = ctrl && ((GetAsyncKeyState(VK_MULTIPLY) & 0x8000) ||
                             (GetAsyncKeyState(VK_NUMPAD9) & 0x8000)) && game_focused();
        BOOL mesh = ctrl && (GetAsyncKeyState(VK_DECIMAL) & 0x8000) && game_focused();
#ifndef STAMPS_NATIVE_UI
        if (down && !down_prev) {
            plog("[i] Ctrl+Numpad0 -> cachet suivant...");
            AcquireSRWLockExclusive(&g_lib_lock);
            if (lib_select_next() > 0) {
                InterlockedExchange(&g_pending, 1);
                plog("[>] arme : Ctrl+Numpad1 (creer) puis Ctrl+Numpad2 (poser)");
            } else if (load_stamp_bin() > 0) {     // secours : fichier unitaire
                InterlockedExchange(&g_pending, 1);
                plog("[>] arme (fichier unitaire) : Ctrl+Numpad1 puis Ctrl+Numpad2");
            }
            ReleaseSRWLockExclusive(&g_lib_lock);
        }
#else
        (void)down_prev; // Native UI reads originals; never fall back to a .bin.
#endif
        if (dump && !dump_prev) {
            InterlockedExchange(&g_dump_wanted, 1);
            plog("[i] Ctrl+Numpad9 -> dump arme (sera execute au prochain Ctrl+Numpad1)");
        }
        if (mesh && !mesh_prev) {
            InterlockedExchange(&g_mesh_wanted, 1);
            plog("[i] Ctrl+Numpad. -> CAPTURE du mesh (routes) armee : fais Ctrl+Numpad1 avec ta copie en main");
        }
        down_prev = down;
        dump_prev = dump;
        mesh_prev = mesh;
        Sleep(50);
    }
    return 0;
}

// ---------------------------------------------------------------- init
static DWORD WINAPI init_thread(LPVOID p) {
    HMODULE anno = NULL;
    void* createstamp = NULL;
    void* buildstamp = NULL;
    void* fillfn = NULL;
#ifdef STAMPS_NATIVE_UI
    void* stampicon = NULL;
    void* textsource = NULL;
    MH_STATUS text_hook_status = MH_ERROR_NOT_CREATED;
    MH_STATUS ui_hook_status = MH_ERROR_NOT_CREATED;
#endif
    MH_STATUS s0, s1, s2;
    HANDLE t;
    int i;
    (void)p;
    log_init();
#ifdef STAMPS_NATIVE_UI
    plog("[UI] v85-native-ui v0.4.0 : IconGUID natif pour les stamps ; icones de dossiers en JSON");
#endif

    for (i = 0; i < 300; i++) {           // jusqu'a 30 s
        anno = GetModuleHandleA("Anno117.exe");
        if (anno) break;
        Sleep(100);
    }
    if (!anno) { plog("[!] Anno117.exe introuvable -> abandon"); return 1; }
    plog("Anno117.exe @ %p", (void*)anno);

    g_getter = find_sig(anno, sig_getter, sizeof(sig_getter), sig_getter_off);
    g_veccopy = find_sig(anno, sig_veccopy, sizeof(sig_veccopy), sig_veccopy_off);
    createstamp = find_sig(anno, sig_createstamp, sizeof(sig_createstamp), sig_createstamp_off);
    buildstamp = find_sig(anno, sig_buildstamp, sizeof(sig_buildstamp), sig_buildstamp_off);
    fillfn = find_sig(anno, sig_fill, sizeof(sig_fill), sig_fill_off);
    g_alloc = find_sig(anno, sig_alloc, sizeof(sig_alloc), 0);
    g_free  = find_sig(anno, sig_free,  sizeof(sig_free),  0);
#ifdef STAMPS_NATIVE_UI
    stampicon = find_sig(anno, sig_stampicon, sizeof(sig_stampicon), 0);
    textsource = find_sig(anno,sig_text_source,sizeof(sig_text_source),0);
    {
        BYTE* site=find_sig(anno,sig_asset_icon_source,sizeof(sig_asset_icon_source),0);
        if (site) {
            g_ui_asset_manager=(void**)(site+7+*(INT32*)(site+3));
            g_ui_standard_getter=site+32+*(INT32*)(site+28);
        }
    }
    plog("[UI] sources : assets=%p Standard=%p textes=%p",g_ui_asset_manager,g_ui_standard_getter,textsource);
    g_targetgetter = find_sig(anno, sig_targetgetter, sizeof(sig_targetgetter), -0x48);
    g_payload_ctor = find_sig(anno, sig_payload_ctor, sizeof(sig_payload_ctor), 0);
    g_polygon_ctor = find_sig(anno, sig_polygon_ctor, sizeof(sig_polygon_ctor), 0);
    if (fillfn && readable((BYTE*)fillfn+0x26,5) && *((BYTE*)fillfn+0x26)==0xe8) {
        BYTE* call=(BYTE*)fillfn+0x26;
        g_reset_payload=call+5+*(INT32*)(call+1);
    }
    {
        BYTE* load = find_sig(anno, sig_native_load, sizeof(sig_native_load), -5);
        // Le site doit appeler le même Fill que notre signature complète.
        if (load && readable(load, 24) && load[0]==0xe8 && load[19]==0xe8 &&
            load+24+*(INT32*)(load+20)==(BYTE*)fillfn)
            g_clear_tool=load+5+*(INT32*)(load+1);
    }
    plog("[UI] getter Target=%p", g_targetgetter);
    plog("[UI] payload ctor=%p polygones=%p nettoyage natif=%p", g_payload_ctor, g_polygon_ctor, g_clear_tool);
#endif
    plog("signatures : getter=%p veccopy=%p CreateStamp=%p BuildStamp=%p Fill=%p alloc=%p free=%p",
         g_getter, g_veccopy, createstamp, buildstamp, fillfn, g_alloc, g_free);

    s0 = MH_Initialize();
#ifdef STAMPS_NATIVE_UI
    ui_management_init(anno);
#endif
    if (createstamp) {
        s1 = MH_CreateHook(createstamp, (LPVOID)hook_CreateStamp, (LPVOID*)&orig_CreateStamp);
        plog("[i] hook CreateStamp @ %p -> %d", createstamp, (int)s1);
    }
    if (buildstamp) {
        s1 = MH_CreateHook(buildstamp, (LPVOID)hook_BuildStamp, (LPVOID*)&orig_BuildStamp);
        plog("[i] hook BuildStamp @ %p -> %d", buildstamp, (int)s1);
    }
    if (fillfn) {
        s1 = MH_CreateHook(fillfn, (LPVOID)hook_Fill, (LPVOID*)&orig_Fill);
        plog("[i] hook FUN_1433a7610 @ %p -> %d", fillfn, (int)s1);
    }
#ifdef STAMPS_NATIVE_UI
    if (stampicon) {
        ui_hook_status = MH_CreateHook(stampicon, (LPVOID)hook_StampIcon, (LPVOID*)&orig_StampIcon);
        plog("[UI] hook GetStampIconGUID @ %p -> %d", stampicon, (int)ui_hook_status);
    }
    if (textsource && g_ui_asset_manager && g_ui_standard_getter) {
        text_hook_status=MH_CreateHook(textsource,(LPVOID)hook_ui_text_source,(LPVOID*)&orig_ui_text_source);
        plog("[UI] hook source de texte @ %p -> %d",textsource,(int)text_hook_status);
    }
#endif
    s2 = MH_EnableHook(MH_ALL_HOOKS);
    plog("[i] MinHook init=%d enable=%d (0=OK)", (int)s0, (int)s2);
#ifdef STAMPS_NATIVE_UI
    if (s2!=MH_OK) g_management_ready=0;
    if (s2==MH_OK && text_hook_status==MH_OK) InterlockedExchange(&g_ui_presentation_hook_ready,1);
    if (s2 == MH_OK && ui_hook_status == MH_OK && orig_CreateStamp &&
        g_getter && g_veccopy && g_alloc && g_payload_ctor && g_polygon_ctor && g_clear_tool && g_reset_payload && orig_Fill)
        InterlockedExchange(&g_ui_bridge_ready, 1);
    plog("[UI] pont direct pret=%ld", g_ui_bridge_ready);
#endif

    t = CreateThread(NULL, 0, hotkey_thread, NULL, 0, NULL);
    if (t) CloseHandle(t);
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID rsv) {
    HANDLE t;
    (void)rsv;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(h);
        t = CreateThread(NULL, 0, init_thread, NULL, 0, NULL);
        if (t) CloseHandle(t);
    } else if (reason == DLL_PROCESS_DETACH) {
        MH_Uninitialize();
        if (g_log != INVALID_HANDLE_VALUE) CloseHandle(g_log);
    }
    return TRUE;
}

// marqueur de vie (le mod Lua peut verifier la presence de la DLL)
__declspec(dllexport) int __cdecl anno117_keepalive(void) { return 0x117; }
