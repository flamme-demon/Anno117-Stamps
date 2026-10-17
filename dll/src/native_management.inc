/* Native CPopupManager dialogs and the game's own stamp folder/path helpers.
 * Included in dllmain_pipette.c to keep all engine calls on the Lua thread.
 * Win32 input only edits a pending name; filesystem actions wait for confirmation.
 */
#include "native_management.h"
#include <stddef.h>

static const BYTE sig_popup_show[]={0x48,0x89,0x5c,0x24,0x08,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x8d,0xac,0x24,0xf0,0xf9,0xff,0xff,0x48,0x81,0xec,0x10,0x07,0x00,0x00,0x4d,0x8b,0xf0};
static const BYTE sig_popup_close[]={0x48,0x89,0x5c,0x24,0x10,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x8d,0xac,0x24,0x10,0xfa,0xff,0xff,0x48,0x81,0xec,0xf0,0x06,0x00,0x00,0x4c,0x63,0xe2};
static const BYTE sig_popup_global[]={0x4d,0x8b,0xd0,0x8b,0xf2,0x33,0xdb,0x48,0x8b,0x05,0xd3,0x47,0x00,0x03,0x48,0x85,0xc9,0x0f,0x84,0xd9,0x00,0x00,0x00};
static const BYTE sig_folder_name[]={0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x30,0x48,0x8b,0x0d,0xb7,0x59,0x42,0x04,0x48,0x8b,0xda,0xba,0x20,0x2e,0x02,0x00,0x33,0xff};
static const BYTE sig_stamp_path[]={0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x6c,0x24,0x18,0x48,0x89,0x74,0x24,0x20,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x81,0xec,0xa0,0x00,0x00,0x00,0x49,0x8b,0xf0,0x48,0x8b,0xea,0x8b,0x51,0x24};
static const BYTE sig_asset_text[]={0x48,0x8b,0xc4,0x48,0x89,0x58,0x10,0x48,0x89,0x70,0x18,0x57,0x48,0x83,0xec,0x70,0x48,0x8d,0x59,0x08,0x48,0x8b,0xf1,0xbf,0x03,0x00,0x00,0x00,0x4c,0x8d,0x40,0x08};
static const BYTE sig_window_input[]={0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x4c,0x24,0x08,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x8d,0x6c,0x24,0xe1,0x48,0x81,0xec,0xa0,0x00,0x00,0x00,0x49,0x8b,0xf1,0x45,0x8b,0xf8,0x4c,0x8b,0xe2};
static const BYTE sig_popup_bind[]={0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57,0x41,0x56,0x41,0x57,0x48,0x83,0xec,0x60,0x48,0x8b,0xea,0x48,0x8b,0xd9};
static const BYTE sig_gui_update[]={0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x6c,0x24,0x18,0x48,0x89,0x74,0x24,0x20,0x57,0x48,0x83,0xec,0x40,0x48,0x8b,0x41,0x68,0x48,0x8b,0xe9,0x80,0x78,0x38,0x00};
typedef void (*PopupClose_t)(void*,int);
typedef void* (*FolderName_t)(void*,void*);
typedef void* (*StampPath_t)(void*,void*,const void*);
typedef void* (*WideCopy_t)(void*,const void*);
typedef LRESULT (*WindowInput_t)(void*,HWND,UINT,WPARAM,LPARAM);
static WindowInput_t orig_window_input;
typedef void (*PopupBind_t)(void*,void*);
typedef void (*GuiUpdate_t)(void*);
static PopupBind_t orig_popup_bind;
static GuiUpdate_t orig_gui_update;
static void *g_bound_scene,*g_bound_data,*g_bound_vtable;
static uint32_t g_bound_dialog,g_gui_reported_dialog;
static UINT64 g_bound_revision;
static void* g_popup_show;
static void* g_popup_getter;
static void* g_asset_text_getter;
static void** g_popup_global;
static PopupClose_t orig_popup_close;
static FolderName_t orig_folder_name;
static StampPath_t orig_stamp_path;
static WideCopy_t g_wide_copy;
static LONG g_management_ready;
static volatile LONG g_capture_thread, g_capture_input, g_hover_guid, g_context_request,g_capture_routed;
static volatile LONG g_popup_button=LONG_MIN, g_editor_changed;
static uint32_t g_capture_folder, g_capture_ready, g_load_ready;
static UiWideString g_capture_name;
static WCHAR g_capture_name_buffer[256];
static SRWLOCK g_editor_lock=SRWLOCK_INIT;
static int g_suppress_right_up;
static volatile LONG g_editor_input_seen;

typedef struct UiTextBlob { UiWideString string; WCHAR text[1]; } UiTextBlob;
#define UI_DYNAMIC_TEXT (UI_TEXT_BASE+0x100000u)
#define UI_DYNAMIC_COUNT 65536u
static UiTextBlob* volatile g_dialog_text[5];
static UiTextBlob* volatile g_dynamic_text[UI_DYNAMIC_COUNT];
static uint32_t g_dynamic_count;
static const WCHAR* g_dialog_names[]={L"Renommer le stamp",L"Renommer le dossier",L"Créer un dossier"};
enum { PICK_CREATE=1,PICK_FOLDER=2,PICK_STAMP=3,EDIT_STAMP=4,EDIT_FOLDER=5,EDIT_NEW=6,PICK_ICON=7,EDIT_ICON=8 };
static struct {
    uint32_t popup,id,target,region,page,kind;
    uint32_t choices[UI_PICK_ITEMS],choice_count,page_count;
    int create_after,select_all;
    WCHAR name[256];
    size_t cursor;
    char error[512];
} g_dialog;
static volatile LONG g_editor_active;
static char g_editor_body_utf8[UI_EDITOR_TEXT_CAPACITY];
static uint32_t g_editor_body_length;
static UINT64 g_editor_body_revision;

static const void* ui_management_text(UINT64 id) {
    UiTextBlob* text=NULL;
    if (id>=UI_TEXT_BASE && id<UI_TEXT_BASE+5)
        text=InterlockedCompareExchangePointer((PVOID volatile*)&g_dialog_text[id-UI_TEXT_BASE],NULL,NULL);
    else if (id>=UI_DYNAMIC_TEXT && id<UI_DYNAMIC_TEXT+UI_DYNAMIC_COUNT)
        text=InterlockedCompareExchangePointer((PVOID volatile*)&g_dynamic_text[id-UI_DYNAMIC_TEXT],NULL,NULL);
    return text ? &text->string : NULL;
}
static UiTextBlob* ui_text_blob(const WCHAR* text) {
    size_t length=wcslen(text),capacity=length<8 ? 8 : length;
    UiTextBlob* blob=calloc(1,offsetof(UiTextBlob,text)+(capacity+1)*sizeof(WCHAR));
    if (!blob) return NULL;
    memcpy(blob->text,text,(length+1)*sizeof(WCHAR));
    blob->string=(UiWideString){blob->text,0,length,capacity};
    return blob;
}
static UINT64 ui_dynamic_snapshot(const WCHAR* value) {
    uint32_t index=g_dynamic_count;
    UiTextBlob* blob;
    if (index>=UI_DYNAMIC_COUNT) return 0;
    blob=ui_text_blob(value); if (!blob) return 0;
    InterlockedExchangePointer((PVOID volatile*)&g_dynamic_text[index],blob);
    ++g_dynamic_count;
    return UI_DYNAMIC_TEXT+index;
}
static int ui_update_name_source(uint32_t guid,BYTE* standard,UiLabel* label) {
    typedef void* (*find_t)(void*,uint32_t);
    typedef BYTE* (*property_t)(void*);
    void* manager; void* asset; BYTE* text;
    if (!g_management_ready) return 1;
    if (label->name_text_id && label->description_text_id) return 1;
    manager=*g_ui_asset_manager;
    asset=((find_t)*(UINT_PTR*)(*(UINT_PTR*)manager+0x18))(manager,guid);
    text=asset && g_asset_text_getter ? ((property_t)g_asset_text_getter)(asset) : NULL;
    if (!text || !readable(text,0x18)) return 0;
    // CText.OasisId +10 (FUN_143182a10) and CStandard.InfoDescription +30.
    // New text IDs make cached UI widgets refresh when a file/folder is renamed.
    if (!label->name_text_id) label->name_text_id=ui_dynamic_snapshot(label->name);
    if (!label->description_text_id) label->description_text_id=ui_dynamic_snapshot(label->description);
    if (!label->name_text_id || !label->description_text_id) return 0;
    InterlockedExchange64((LONG64 volatile*)(text+0x10),label->name_text_id);
    InterlockedExchange64((LONG64 volatile*)(standard+0x30),label->description_text_id);
    return 1;
}
static void ui_dialog_text(unsigned index,const WCHAR* value) {
    UiTextBlob* text=ui_text_blob(value);
    // The text manager returns const references; old readers retain valid data.
    if (text) InterlockedExchangePointer((PVOID volatile*)&g_dialog_text[index],text);
}
static void* ui_popup_manager(void) {
    BYTE* weak;
    if (!g_popup_global || !readable(g_popup_global,8) || !(weak=*g_popup_global)) return NULL;
    return readable(weak-8,0x68) ? weak-8 : NULL;
}
static BYTE* ui_popup_asset(uint32_t guid) {
    typedef void* (*find_t)(void*,uint32_t);
    typedef BYTE* (*property_t)(void*);
    void* manager;
    void* asset;
    if (!ui_standard(guid) || !g_popup_getter) return NULL;
    manager=*g_ui_asset_manager;
    asset=((find_t)*(UINT_PTR*)(*(UINT_PTR*)manager+0x18))(manager,guid);
    return asset ? ((property_t)g_popup_getter)(asset) : NULL;
}
static int ui_owns_popup(void* manager) {
    UINT_PTR begin,end;
    BYTE* top;
    if (!g_dialog.id || manager!=ui_popup_manager() || !readable((BYTE*)manager+0x48,24)) return 0;
    begin=*(UINT_PTR*)((BYTE*)manager+0x48); end=*(UINT_PTR*)((BYTE*)manager+0x50);
    if (end<=begin || end-begin<0x38 || (end-begin)%0x38) return 0;
    top=(BYTE*)(end-0x38);
    return readable(top,0x38) && *(uint32_t*)top==g_dialog.id && *(uint32_t*)(top+4)==g_dialog.popup;
}
static int ui_has_popup(void* manager) {
    UINT_PTR begin,end,p;
    if (!g_dialog.id || manager!=ui_popup_manager() || !readable((BYTE*)manager+0x48,24)) return 0;
    begin=*(UINT_PTR*)((BYTE*)manager+0x48); end=*(UINT_PTR*)((BYTE*)manager+0x50);
    if (end<begin || end-begin>256*0x38 || (end-begin)%0x38 || !readable((void*)begin,end-begin)) return 0;
    for (p=begin;p<end;p+=0x38)
        if (*(uint32_t*)p==g_dialog.id && *(uint32_t*)(p+4)==g_dialog.popup) return 1;
    return 0;
}
static void hook_popup_bind(void* scene,void* data) {
    orig_popup_bind(scene,data);
    if (g_editor_active && g_dialog.popup==UI_POPUP_BASE+UI_POP_EDIT && ui_owns_popup(ui_popup_manager()) &&
        readable(scene,0xf0) && readable(data,0x130) &&
        (BYTE*)*(void**)((BYTE*)scene+0xe8)+0x70==(BYTE*)data) {
        g_bound_scene=scene; g_bound_data=data; g_bound_vtable=*(void**)scene;
        g_bound_revision=g_editor_body_revision; g_bound_dialog=g_dialog.id;
    }
}
static void hook_popup_close(void* manager,int button) {
    if (ui_owns_popup(manager)) {
        InterlockedExchange(&g_editor_active,0);
        InterlockedExchange(&g_popup_button,button);
    }
    orig_popup_close(manager,button);
}
static void* hook_folder_name(void* self,void* out) {
    if ((DWORD)g_capture_thread==GetCurrentThreadId() && g_capture_name.data && g_wide_copy)
        return g_wide_copy(out,&g_capture_name);
    return orig_folder_name(self,out);
}
static void* hook_stamp_path(void* stamp,void* out,const void* folder) {
    // Both the native unique-name check and the final writer use this helper.
    // Override only while our explicit capture is executing on its owning thread.
    if ((DWORD)g_capture_thread==GetCurrentThreadId() && g_capture_name.data) {
        folder=&g_capture_name;
        InterlockedExchange(&g_capture_routed,1);
    }
    return orig_stamp_path(stamp,out,folder);
}
static void ui_editor_insert(const WCHAR* text) {
    size_t n=wcslen(g_dialog.name),amount=wcslen(text);
    if (g_dialog.select_all) { n=0; g_dialog.name[0]=0; g_dialog.cursor=0; g_dialog.select_all=0; }
    if (n+amount>=256) return;
    memmove(g_dialog.name+g_dialog.cursor+amount,g_dialog.name+g_dialog.cursor,(n-g_dialog.cursor+1)*sizeof(WCHAR));
    memcpy(g_dialog.name+g_dialog.cursor,text,amount*sizeof(WCHAR));
    g_dialog.cursor+=amount; InterlockedExchange(&g_editor_changed,1);
}
static void ui_editor_key(UINT key) {
    size_t n=wcslen(g_dialog.name),cursor=g_dialog.cursor;
    if (key==VK_HOME) cursor=0;
    else if (key==VK_END) cursor=n;
    else if (key==VK_LEFT && cursor) { --cursor; if (cursor && g_dialog.name[cursor]>=0xdc00 && g_dialog.name[cursor]<=0xdfff) --cursor; }
    else if (key==VK_RIGHT && cursor<n) { ++cursor; if (cursor<n && g_dialog.name[cursor]>=0xdc00 && g_dialog.name[cursor]<=0xdfff) ++cursor; }
    else if (key==VK_BACK || key==VK_DELETE) {
        if (g_dialog.select_all) { g_dialog.name[0]=0; cursor=0; }
        else if ((key==VK_BACK && cursor) || (key==VK_DELETE && cursor<n)) {
            size_t start=key==VK_BACK ? cursor-1 : cursor,end=key==VK_BACK ? cursor : cursor+1;
            if (key==VK_BACK && start && g_dialog.name[start]>=0xdc00 && g_dialog.name[start]<=0xdfff) --start;
            if (key==VK_DELETE && end<n && g_dialog.name[end]>=0xdc00 && g_dialog.name[end]<=0xdfff) ++end;
            memmove(g_dialog.name+start,g_dialog.name+end,(n-end+1)*sizeof(WCHAR)); cursor=start;
        }
    }
    g_dialog.cursor=cursor; g_dialog.select_all=0; InterlockedExchange(&g_editor_changed,1);
}
// FUN_14228ba90 queues the game's actual window messages. Intercept here rather
// than subclassing GetForegroundWindow: overlays/window recreation can replace
// that WndProc, leaving an editor that opens but never receives keyboard input.
static int ui_management_input(HWND window,UINT message,WPARAM wparam,LPARAM lparam) {
    if (g_editor_active && ui_owns_popup(ui_popup_manager())) {
        if (message==WM_CHAR) {
            if (!(InterlockedOr(&g_editor_input_seen,2)&2)) plog("[UI] clavier natif : texte recu");
            if (wparam>=32 && wparam!=127) {
                WCHAR value[2]={(WCHAR)wparam,0};
                AcquireSRWLockExclusive(&g_editor_lock); ui_editor_insert(value); ReleaseSRWLockExclusive(&g_editor_lock);
            }
            return 1;
        }
        if (message==WM_KEYDOWN) {
            if (!(InterlockedOr(&g_editor_input_seen,1)&1)) plog("[UI] clavier natif : touche recue");
            if (wparam==VK_RETURN || wparam==VK_ESCAPE) {
                InterlockedExchange(&g_popup_button,wparam==VK_RETURN ? 0 : 1);
                return 1;
            }
            AcquireSRWLockExclusive(&g_editor_lock);
            if ((GetKeyState(VK_CONTROL)&0x8000) && wparam=='A') {
                g_dialog.select_all=1; InterlockedExchange(&g_editor_changed,1);
            } else if ((GetKeyState(VK_CONTROL)&0x8000) && wparam=='V') {
                if (OpenClipboard(window)) {
                    HANDLE data=GetClipboardData(CF_UNICODETEXT);
                    const WCHAR* text=data ? GlobalLock(data) : NULL;
                    if (text) { if (wcslen(text)<256) ui_editor_insert(text); GlobalUnlock(data); }
                    CloseClipboard();
                }
            } else if (wparam==VK_HOME || wparam==VK_END || wparam==VK_LEFT || wparam==VK_RIGHT || wparam==VK_BACK || wparam==VK_DELETE)
                ui_editor_key((UINT)wparam);
            ReleaseSRWLockExclusive(&g_editor_lock);
            return 1;
        }
        if (message==WM_KEYUP) return 1;
    }
    if (message==WM_RBUTTONDOWN && g_hover_guid && !g_capture_input && !g_dialog.id) {
        InterlockedExchange(&g_context_request,g_hover_guid); g_suppress_right_up=1; return 1;
    }
    if (message==WM_RBUTTONUP && g_suppress_right_up) { g_suppress_right_up=0; return 1; }
    return 0;
}
static LRESULT hook_window_input(void* self,HWND window,UINT message,WPARAM wparam,LPARAM lparam) {
    if (ui_management_input(window,message,wparam,lparam)) return 0;
    return orig_window_input(self,window,message,wparam,lparam);
}
static int ui_attach_input(void) {
#ifdef NATIVE_UI_TEST
    return 1; // Tests simulate messages and engine objects; no desktop window.
#else
    HWND window=GetForegroundWindow(); DWORD process;
    return orig_window_input && window && GetWindowThreadProcessId(window,&process) && process==GetCurrentProcessId();
#endif
}
static int ui_dialog_show(uint32_t kind,const WCHAR* title,const WCHAR* body) {
    typedef uint32_t* (*show_t)(void*,uint32_t*,const void*);
    BYTE request[0x30]={0}; uint32_t id=0;
    void* manager=ui_popup_manager(); BYTE* popup;
    UINT64 title_id,body_id;
    if (!g_management_ready || !manager || g_dialog.id) return 0;
    ui_dialog_text(0,title); ui_dialog_text(1,body);
    popup=ui_popup_asset(UI_POPUP_BASE+kind);
    if (!popup || !readable(popup,0x40)) return 0;
    title_id=ui_dynamic_snapshot(title);
    body_id=kind==UI_POP_EDIT ? *(UINT64*)(popup+0x18) : ui_dynamic_snapshot(body);
    if (!title_id || !body_id) return 0;
    InterlockedExchange64((LONG64 volatile*)(popup+0x10),title_id);
    InterlockedExchange64((LONG64 volatile*)(popup+0x18),body_id);
    *(uint32_t*)request=UI_POPUP_BASE+kind;
    *(uint32_t*)(request+4)=g_dialog.target;
    ((show_t)g_popup_show)(manager,&id,request);
    if (!id) { plog("[UI] ouverture dialogue natif refusee : %u",kind); return 0; }
    g_dialog.popup=UI_POPUP_BASE+kind; g_dialog.id=id;
    InterlockedExchange(&g_popup_button,LONG_MIN);
    plog("[UI] dialogue natif ouvert : type=%u id=%u cible=%u",kind,id,g_dialog.target);
    return 1;
}
static void ui_dialog_error(const char* error) {
    WCHAR text[512];
    if (!MultiByteToWideChar(CP_UTF8,0,error,-1,text,512)) wcscpy(text,L"Action impossible.");
    ui_dialog_show(UI_POP_ERROR,L"Bibliothèque de stamps",text);
}
static uint32_t ui_region(uint32_t folder) { return (folder-LIVE_POOL_BASE)/(LIVE_FOLDERS*LIVE_SLOT_STRIDE); }
static int ui_capture_destination(uint32_t folder) {
    const LiveRecord* record=native_catalogue_find(&g_live_catalogue,folder);
    WCHAR path[32768];
    uint32_t i,count=0;
    if (!record || (folder-LIVE_POOL_BASE)%LIVE_SLOT_STRIDE || !native_library_path(g_ui_stamp_root,record,path,32768)) return 0;
    for (i=1;i<LIVE_SLOT_STRIDE;++i) if (g_live_catalogue.records[folder-LIVE_POOL_BASE+i].assigned) ++count;
    if (count>=LIVE_STAMPS) { ui_dialog_error("Ce dossier a atteint la limite de 64 emplacements."); return 0; }
    g_capture_folder=folder; g_capture_ready=folder;
    return 1;
}
static void ui_manage_item(uint32_t guid) {
    const LiveRecord* row=native_catalogue_find(&g_live_catalogue,guid);
    WCHAR name[256];
    if (!row || !MultiByteToWideChar(CP_UTF8,0,row->name,-1,name,256)) return;
    g_dialog.target=guid; g_dialog.region=ui_region(guid-(guid-LIVE_POOL_BASE)%LIVE_SLOT_STRIDE);
    if ((guid-LIVE_POOL_BASE)%LIVE_SLOT_STRIDE==0)
        ui_dialog_show(UI_POP_FOLDER,name,L"Renommer ce dossier, changer son icône ou gérer ses stamps.");
    else ui_dialog_show(UI_POP_STAMP,name,L"Renommer ce stamp, changer son icône ou le supprimer.");
}
static void ui_editor_body(void) {
    WCHAR body[1024],name[258],error[512];
    char utf8_body[UI_EDITOR_TEXT_CAPACITY];
    BYTE* popup;
    UiTextBlob* blob;
    size_t cursor;
    uint32_t index;
    int selected,length;
    AcquireSRWLockShared(&g_editor_lock);
    cursor=g_dialog.cursor;
    memcpy(name,g_dialog.name,cursor*sizeof(WCHAR)); name[cursor]=L'▏';
    wcscpy(name+cursor+1,g_dialog.name+cursor);
    selected=g_dialog.select_all;
    ReleaseSRWLockShared(&g_editor_lock);
    error[0]=0;
    if (g_dialog.error[0]) MultiByteToWideChar(CP_UTF8,0,g_dialog.error,-1,error,512);
    swprintf(body,1024,L"%ls\n\n%ls\n\n%ls%ls",
        g_dialog.kind==EDIT_ICON ? L"Saisis l'ID de l'icône du jeu, puis clique sur Enregistrer." : L"Saisis le nom au clavier, puis clique sur Enregistrer.",
        name,selected ? L"Tout est sélectionné.\n" : L"",error);
    length=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,body,-1,
        utf8_body,sizeof(utf8_body),NULL,NULL);
    if (!length) return;
    blob=ui_text_blob(body); index=g_dynamic_count;
    if (!blob || index>=UI_DYNAMIC_COUNT) { free(blob); return; }
    popup=ui_popup_asset(UI_POPUP_BASE+UI_POP_EDIT);
    if (!popup || !readable(popup,0x40)) { free(blob); return; }
    InterlockedExchangePointer((PVOID volatile*)&g_dynamic_text[index],blob);
    ++g_dynamic_count;
    // CPopup BodyText initializes the popup, but the scene retains a CTextData
    // string after opening. Publish the same snapshot for Lua to update that
    // visible DescriptionText through its supported SetText binding.
    InterlockedExchange64((LONG64 volatile*)(popup+0x18),UI_DYNAMIC_TEXT+index);
    memcpy(g_editor_body_utf8,utf8_body,(size_t)length);
    g_editor_body_length=(uint32_t)length-1;
    g_editor_body_revision=UI_DYNAMIC_TEXT+index;
}
static void ui_edit_begin(uint32_t kind,int create_after) {
    const LiveRecord* row=native_catalogue_find(&g_live_catalogue,g_dialog.target);
    if (!ui_attach_input()) { ui_dialog_error("La fenêtre du jeu n'est pas active."); return; }
    g_dialog.kind=kind; g_dialog.create_after=create_after; g_dialog.error[0]=0;
    AcquireSRWLockExclusive(&g_editor_lock);
    g_dialog.name[0]=0;
    if (kind==EDIT_ICON && row) swprintf(g_dialog.name,256,L"%u",row->icon_guid);
    else if (kind!=EDIT_NEW && row) MultiByteToWideChar(CP_UTF8,0,row->name,-1,g_dialog.name,256);
    g_dialog.cursor=wcslen(g_dialog.name); g_dialog.select_all=kind!=EDIT_NEW;
    InterlockedExchange(&g_editor_input_seen,0);
    ReleaseSRWLockExclusive(&g_editor_lock);
    ui_editor_body();
    if (ui_dialog_show(UI_POP_EDIT,kind==EDIT_ICON ? L"Choisir une icône par ID" : g_dialog_names[kind-EDIT_STAMP],L"Saisie au clavier.")) InterlockedExchange(&g_editor_active,1);
}
static UINT64 ui_pick_caption(const LiveRecord* row) {
    WCHAR name[256];
    size_t end;
    if (!row) return ui_dynamic_snapshot(L"—");
    if (!MultiByteToWideChar(CP_UTF8,0,row->name,-1,name,256)) return 0;
    // GenericPopup's BtnText widget displays literal text, including <img> and
    // HTML entities. Keep names literal and cap the visible caption so a long
    // filename cannot stretch the entire grid. The action dialog has the full
    // name; neither the catalogue identity nor the file name is shortened.
    if (wcslen(name)>24) {
        end=23;
        if (name[end-1]>=0xd800 && name[end-1]<=0xdbff) --end;
        name[end++]=L'…'; name[end]=0;
    }
    return ui_dynamic_snapshot(name);
}
static void ui_pick_show(uint32_t kind) {
    uint32_t choices[LIVE_RECORD_COUNT],count=0,i,start;
    WCHAR body[512];
    BYTE* popup;
    UINT_PTR begin,end;
    g_dialog.kind=kind;
    for (i=0;i<g_live_catalogue.row_count;++i) {
        uint32_t slot=g_live_catalogue.rows[i];
        if (kind==PICK_STAMP) {
            if (slot%LIVE_SLOT_STRIDE && slot/LIVE_SLOT_STRIDE==(g_dialog.target-LIVE_POOL_BASE)/LIVE_SLOT_STRIDE)
                choices[count++]=LIVE_POOL_BASE+slot;
        } else if (slot%LIVE_SLOT_STRIDE==0 && slot/(LIVE_FOLDERS*LIVE_SLOT_STRIDE)==g_dialog.region)
            choices[count++]=LIVE_POOL_BASE+slot;
    }
    if (!count) {
        if (kind==PICK_STAMP) ui_dialog_error("Ce dossier est vide.");
        else ui_edit_begin(EDIT_NEW,kind==PICK_CREATE);
        return;
    }
    // The native PHX grid override exposes three rows of three real buttons:
    // six items, then Previous / Next / Close, always in the same positions.
    g_dialog.page_count=(count+UI_PICK_ITEMS-1)/UI_PICK_ITEMS;
    if (g_dialog.page>=g_dialog.page_count) g_dialog.page=g_dialog.page_count-1;
    start=g_dialog.page*UI_PICK_ITEMS;
    g_dialog.choice_count=count-start<UI_PICK_ITEMS ? count-start : UI_PICK_ITEMS;
    memset(g_dialog.choices,0,sizeof(g_dialog.choices));
    popup=ui_popup_asset(UI_POPUP_BASE+UI_POP_PICK1);
    begin=popup && readable(popup,0x40) ? *(UINT_PTR*)(popup+0x20) : 0;
    end=popup && readable(popup,0x40) ? *(UINT_PTR*)(popup+0x28) : 0;
    if (end<begin || end-begin<UI_PICK_BUTTONS*0x10 || !readable((void*)begin,UI_PICK_BUTTONS*0x10)) return;
    for (i=0;i<g_dialog.choice_count;++i) {
        g_dialog.choices[i]=choices[start+i];
    }
    for (i=0;i<UI_PICK_ITEMS;++i) {
        const LiveRecord* row=i<g_dialog.choice_count ? native_catalogue_find(&g_live_catalogue,g_dialog.choices[i]) : NULL;
        UINT64 id=ui_pick_caption(row);
        if (!id) return;
        InterlockedExchange64((LONG64 volatile*)(begin+i*0x10),id);
    }
    swprintf(body,512,L"%ls\nPage %u / %u",kind==PICK_CREATE ? L"Choisis le dossier avant de tracer ta sélection." :
        kind==PICK_STAMP ? L"Choisis le stamp à gérer." : L"Choisis le dossier à gérer.",g_dialog.page+1,g_dialog.page_count);
    ui_dialog_show(UI_POP_PICK1,
        kind==PICK_CREATE ? L"Créer un stamp — dossier de destination" : L"Bibliothèque de stamps",body);
}
static void ui_choose_create(uint32_t region) {
    uint32_t i,folder=0,count=0;
    g_dialog.region=region; g_dialog.page=0;
    for (i=0;i<LIVE_FOLDERS;++i) {
        uint32_t guid=LIVE_POOL_BASE+(region*LIVE_FOLDERS+i)*LIVE_SLOT_STRIDE;
        if (native_catalogue_find(&g_live_catalogue,guid)) { folder=guid; ++count; }
    }
    if (count==1) ui_capture_destination(folder);
    else ui_pick_show(PICK_CREATE);
}
static void ui_icon_show(void) {
    const LiveRecord* row=native_catalogue_find(&g_live_catalogue,g_dialog.target);
    WCHAR body[512];
    if (!row) return;
    g_dialog.kind=PICK_ICON;
    swprintf(body,512,L"Choisis une icône ou saisis un ID précis.\nIcône actuelle : %u",row->icon_guid);
    ui_dialog_show(UI_POP_PICK2,L"Changer l'icône",body);
}
static int ui_icon_number(const WCHAR* name,uint32_t* icon) {
    uint64_t number=0; size_t i;
    if (!*name) return 0;
    for (i=0;name[i];++i) {
        if (name[i]<L'0' || name[i]>L'9') return 0;
        number=number*10+(name[i]-L'0'); if (number>UINT32_MAX) return 0;
    }
    *icon=(uint32_t)number; return number!=0;
}
static void ui_icon_apply(uint32_t icon) {
    char error[512]={0};
    uint32_t target=g_dialog.target;
    if (!native_library_set_icon(&g_live_catalogue,g_ui_stamp_root,UI_ROOT L"slots.tsv",target,icon,error,sizeof(error))) {
        ui_dialog_error(error); return;
    }
    ui_refresh_catalogue(1);
    plog("[UI] icone enregistree : cible=%u IconGUID=%u type=%s",target,icon,
        (target-LIVE_POOL_BASE)%LIVE_SLOT_STRIDE ? "stamp natif" : "dossier JSON");
    ui_manage_item(target);
}
static void ui_handle_button(int button,uint32_t popup) {
    uint32_t target=g_dialog.target;
    char error[512]={0};
    g_dialog.id=0; g_dialog.popup=0;
    if (button<0) return;
    if (popup==UI_POPUP_BASE+UI_POP_STAMP) {
        if (button==0) ui_edit_begin(EDIT_STAMP,0);
        else if (button==1) ui_dialog_show(UI_POP_DELETE,L"Supprimer ce stamp ?",L"Le stamp disparaîtra du menu. Son fichier sera conservé dans la corbeille du mod.");
        else if (button==2) ui_icon_show();
    } else if (popup==UI_POPUP_BASE+UI_POP_FOLDER) {
        if (button==0) ui_edit_begin(EDIT_FOLDER,0);
        else if (button==1) { g_dialog.page=0; ui_pick_show(PICK_STAMP); }
        else if (button==2) ui_icon_show();
    } else if (popup==UI_POPUP_BASE+UI_POP_ROOT) {
        if (button==0) ui_edit_begin(EDIT_NEW,0);
        else if (button==1) { g_dialog.page=0; ui_pick_show(PICK_FOLDER); }
    } else if (popup==UI_POPUP_BASE+UI_POP_EDIT && button==0) {
        WCHAR name[256]; uint32_t created=0;
        AcquireSRWLockShared(&g_editor_lock); wcscpy(name,g_dialog.name); ReleaseSRWLockShared(&g_editor_lock);
        if (g_dialog.kind==EDIT_ICON) {
            uint32_t icon;
            if (!ui_icon_number(name,&icon)) snprintf(error,sizeof(error),"Saisis un ID entier positif.");
            else native_library_set_icon(&g_live_catalogue,g_ui_stamp_root,UI_ROOT L"slots.tsv",target,icon,error,sizeof(error));
        } else if (g_dialog.kind==EDIT_NEW)
            created=native_library_create_folder(&g_live_catalogue,g_ui_stamp_root,UI_ROOT L"slots.tsv",g_dialog.region,name,error,sizeof(error));
        else if (!native_library_rename(&g_live_catalogue,g_ui_stamp_root,UI_ROOT L"slots.tsv",target,name,error,sizeof(error))) {}
        if (error[0]) {
            snprintf(g_dialog.error,sizeof(g_dialog.error),"%s",error);
            ui_editor_body();
            if (ui_dialog_show(UI_POP_EDIT,g_dialog.kind==EDIT_ICON ? L"Icône à corriger" : L"Nom à corriger",L"Vérifie la saisie.")) InterlockedExchange(&g_editor_active,1);
        } else {
            ui_refresh_catalogue(1);
            if (g_dialog.kind==EDIT_ICON) {
                plog("[UI] icone enregistree par ID : cible=%u",target);
                ui_manage_item(target);
            }
            if (created && g_dialog.create_after) ui_capture_destination(created);
        }
    } else if (popup==UI_POPUP_BASE+UI_POP_DELETE && button==0) {
        if (!native_library_trash(&g_live_catalogue,g_ui_stamp_root,UI_ROOT L"slots.tsv",UI_ROOT L"trash",target,error,sizeof(error))) ui_dialog_error(error);
        else ui_refresh_catalogue(1);
    } else if (popup==UI_POPUP_BASE+UI_POP_PICK2 && g_dialog.kind==PICK_ICON) {
        static const uint32_t icons[]={3087,8541,80587,2001043,2068,3310};
        if (button<6) ui_icon_apply(icons[button]);
        else if (button==6) ui_edit_begin(EDIT_ICON,0);
        else if (button==7) {
            const LiveRecord* row=native_catalogue_find(&g_live_catalogue,target);
            uint32_t icon=0;
            if ((target-LIVE_POOL_BASE)%LIVE_SLOT_STRIDE && row && row->component_count) icon=row->components[0].guid;
            ui_icon_apply(icon);
        } else if (button==8) ui_manage_item(target);
    } else if (popup>=UI_POPUP_BASE+UI_POP_PICK1 && popup<=UI_POPUP_BASE+UI_POP_PICK3) {
        if ((uint32_t)button<g_dialog.choice_count) {
            uint32_t guid=g_dialog.choices[button];
            if (g_dialog.kind==PICK_CREATE) ui_capture_destination(guid); else ui_manage_item(guid);
        } else if ((uint32_t)button<UI_PICK_ITEMS) ui_pick_show(g_dialog.kind); // empty cell
        else if ((uint32_t)button==UI_PICK_ITEMS || (uint32_t)button==UI_PICK_ITEMS+1) {
            if ((uint32_t)button==UI_PICK_ITEMS && g_dialog.page) --g_dialog.page;
            else if ((uint32_t)button==UI_PICK_ITEMS+1 && g_dialog.page+1<g_dialog.page_count) ++g_dialog.page;
            ui_pick_show(g_dialog.kind);
        }
    }
}
static void ui_management_tick(void) {
    LONG button,context;
    void* manager;
    if (!g_management_ready) return;
    ui_attach_input();
    button=InterlockedExchange(&g_popup_button,LONG_MIN);
    if (button!=LONG_MIN && g_dialog.id) {
        uint32_t popup=g_dialog.popup;
        manager=ui_popup_manager();
        if (ui_owns_popup(manager)) orig_popup_close(manager,button); // keyboard confirmation
        InterlockedExchange(&g_editor_active,0);
        ui_handle_button((int)button,popup);
    } else if (g_dialog.id && !ui_has_popup(ui_popup_manager())) {
        // The game cancelled the popup (reload, Escape or another UI flow).
        g_dialog.id=0; g_dialog.popup=0; InterlockedExchange(&g_editor_active,0);
    }
    if (g_editor_active && ui_owns_popup(ui_popup_manager()) && InterlockedExchange(&g_editor_changed,0)) ui_editor_body();
    context=InterlockedExchange(&g_context_request,0);
    if (context && !g_dialog.id && !g_capture_input) { ui_refresh_catalogue(1); ui_manage_item((uint32_t)context); }
}
// CStateController::Update runs while GenericPopup pauses meta-game ticks.
// Keep dialog actions and native data binding on this GUI update, never on the
// Win32 message thread. The world remains paused and no simulation tick is run.
static void hook_gui_update(void* self) {
    orig_gui_update(self);
    if (!g_management_ready || (!g_dialog.id && !g_context_request) ||
        !TryAcquireSRWLockExclusive(&g_lib_lock)) return;
    ui_management_tick();
    if (g_editor_active && g_dialog.id==g_bound_dialog && g_editor_body_revision!=g_bound_revision &&
        ui_owns_popup(ui_popup_manager()) && readable(g_bound_scene,0xf0) && readable(g_bound_data,0x130) &&
        *(void**)g_bound_scene==g_bound_vtable &&
        (BYTE*)*(void**)((BYTE*)g_bound_scene+0xe8)+0x70==(BYTE*)g_bound_data) {
        orig_popup_bind(g_bound_scene,g_bound_data);
        g_bound_revision=g_editor_body_revision;
        if (g_gui_reported_dialog!=g_dialog.id) {
            g_gui_reported_dialog=g_dialog.id;
            plog("[UI] saisie actualisee par la GUI pendant la pause : dialogue=%u",g_dialog.id);
        }
    }
    ReleaseSRWLockExclusive(&g_lib_lock);
}
static int ui_management_command_locked(uint32_t command,UINT64* result) {
    if (command==UI_EDITOR_REVISION || command==UI_EDITOR_LENGTH || command==UI_EDITOR_DIALOG ||
        (command>=UI_EDITOR_BYTES && command<UI_EDITOR_BYTES+(UI_EDITOR_TEXT_CAPACITY+2)/3)) {
        *result=0;
        if (g_editor_active && g_dialog.popup==UI_POPUP_BASE+UI_POP_EDIT && ui_owns_popup(ui_popup_manager())) {
            if (command==UI_EDITOR_REVISION) *result=g_editor_body_revision;
            else if (command==UI_EDITOR_LENGTH) *result=g_editor_body_length;
            else if (command==UI_EDITOR_DIALOG) *result=g_dialog.id;
            else {
                uint32_t offset=(command-UI_EDITOR_BYTES)*3,i;
                for (i=0;i<3 && offset+i<g_editor_body_length;++i)
                    *result|=(UINT64)(BYTE)g_editor_body_utf8[offset+i]<<(i*8);
            }
        }
        return 1;
    }
    if (command==UI_MANAGE_READY) { *result=g_management_ready; return 1; }
    if (command==UI_MANAGE_POLL) {
        ui_management_tick(); *result=g_capture_ready; g_capture_ready=0; return 1;
    }
    if (command==UI_MANAGE_LOAD) { *result=g_load_ready; g_load_ready=0; return 1; }
    if (command==UI_MANAGE_OPEN) { *result=g_dialog.id!=0; return 1; }
    if (command==UI_HOVER_CLEAR) { InterlockedExchange(&g_hover_guid,0); *result=1; return 1; }
    if (command==UI_CAPTURE_ON || command==UI_CAPTURE_OFF) {
        InterlockedExchange(&g_capture_input,command==UI_CAPTURE_ON); InterlockedExchange(&g_hover_guid,0); *result=1; return 1;
    }
    if (command>=UI_HOVER_BASE && command<UI_HOVER_BASE+LIVE_RECORD_COUNT) {
        uint32_t guid=LIVE_POOL_BASE+command-UI_HOVER_BASE;
        InterlockedExchange(&g_hover_guid,native_catalogue_find(&g_live_catalogue,guid) ? guid : 0); *result=1; return 1;
    }
    if (!g_management_ready) return 0;
    if (command>=UI_CHOOSE_CREATE && command<UI_CHOOSE_CREATE+2) {
        ui_refresh_catalogue(1); if (!g_dialog.id) ui_choose_create(command-UI_CHOOSE_CREATE); *result=1; return 1;
    }
    if (command>=UI_ACTION_BASE && command<UI_ACTION_BASE+2*LIVE_FOLDER_COUNT) {
        uint32_t offset=command-UI_ACTION_BASE,folder=LIVE_POOL_BASE+(offset/2)*LIVE_SLOT_STRIDE;
        ui_refresh_catalogue(1);
        if (!g_dialog.id) {
            if (offset%2) ui_manage_item(folder); else ui_capture_destination(folder);
        }
        *result=1; return 1;
    }
    if (command>=UI_ROOT_MANAGE && command<UI_ROOT_MANAGE+2) {
        ui_refresh_catalogue(1); g_dialog.region=command-UI_ROOT_MANAGE; g_dialog.page=0;
        if (!g_dialog.id) ui_dialog_show(UI_POP_ROOT,L"Gérer les dossiers",L"Créer un dossier ou parcourir les dossiers de cette région.");
        *result=1; return 1;
    }
    return 0;
}
static int ui_management_command(uint32_t command,UINT64* result) {
    int handled;
    if (command<UI_GUID_MIN || command>=UI_GUID_MAX) return 0;
    AcquireSRWLockExclusive(&g_lib_lock);
    handled=ui_management_command_locked(command,result);
    ReleaseSRWLockExclusive(&g_lib_lock);
    return handled;
}
static void ui_management_init(HMODULE module) {
    MH_STATUS close_status,folder_status,path_status,input_status,bind_status,gui_status;
    BYTE* global=find_sig(module,sig_popup_global,sizeof(sig_popup_global),0);
    BYTE* folder=find_sig(module,sig_folder_name,sizeof(sig_folder_name),0);
    BYTE* path=find_sig(module,sig_stamp_path,sizeof(sig_stamp_path),0);
    BYTE* close=find_sig(module,sig_popup_close,sizeof(sig_popup_close),0);
    BYTE* input=find_sig(module,sig_window_input,sizeof(sig_window_input),0);
    BYTE* bind=find_sig(module,sig_popup_bind,sizeof(sig_popup_bind),0);
    BYTE* gui=find_sig(module,sig_gui_update,sizeof(sig_gui_update),0);
    g_popup_show=find_sig(module,sig_popup_show,sizeof(sig_popup_show),0);
    g_asset_text_getter=find_sig(module,sig_asset_text,sizeof(sig_asset_text),0);
    if (global) g_popup_global=(void**)(global+14+*(INT32*)(global+10));
    if (g_popup_show && *((BYTE*)g_popup_show+0xaa)==0xe8) {
        BYTE* call=(BYTE*)g_popup_show+0xaa;
        g_popup_getter=call+5+*(INT32*)(call+1);
    }
    if (folder && folder[0x6d]==0xe8) g_wide_copy=(WideCopy_t)(folder+0x72+*(INT32*)(folder+0x6e));
    if (!folder || !path || !close || !input || !bind || !gui || !g_popup_show || !g_popup_getter || !g_popup_global || !g_wide_copy || !g_asset_text_getter) {
        plog("[UI] gestion indisponible : show=%p close=%p popup=%p global=%p folder=%p path=%p copy=%p input=%p liaison=%p GUI=%p",
            g_popup_show,close,g_popup_getter,g_popup_global,folder,path,g_wide_copy,input,bind,gui); return;
    }
    close_status=MH_CreateHook(close,(LPVOID)hook_popup_close,(LPVOID*)&orig_popup_close);
    folder_status=MH_CreateHook(folder,(LPVOID)hook_folder_name,(LPVOID*)&orig_folder_name);
    path_status=MH_CreateHook(path,(LPVOID)hook_stamp_path,(LPVOID*)&orig_stamp_path);
    input_status=MH_CreateHook(input,(LPVOID)hook_window_input,(LPVOID*)&orig_window_input);
    bind_status=MH_CreateHook(bind,(LPVOID)hook_popup_bind,(LPVOID*)&orig_popup_bind);
    gui_status=MH_CreateHook(gui,(LPVOID)hook_gui_update,(LPVOID*)&orig_gui_update);
    if (close_status==MH_OK && folder_status==MH_OK && path_status==MH_OK && input_status==MH_OK && bind_status==MH_OK && gui_status==MH_OK) g_management_ready=1;
    plog("[UI] gestion native : close=%d folder=%d path=%d clavier=%d liaison=%d GUI=%d global=%p",close_status,folder_status,path_status,input_status,bind_status,gui_status,g_popup_global);
}
