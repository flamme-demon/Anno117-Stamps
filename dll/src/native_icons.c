/* Explicit icon edits: native IconGUID for stamps, JSON only for folders. */
#include "native_catalogue.h"
#include "native_stamp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

static int message(char* error,size_t size,const char* text) {
    if (error && size) snprintf(error,size,"%s",text);
    return 0;
}
static int state_path(const WCHAR* registry,WCHAR* path) {
    WCHAR* slash;
    if (wcslen(registry)+32>=32768) return 0;
    wcscpy(path,registry); slash=wcsrchr(path,L'\\');
    if (!slash) return 0;
    wcscpy(slash+1,L"folder-icons.json"); return 1;
}
static void space(const char** p) { while (**p==' ' || **p=='\t' || **p=='\r' || **p=='\n') ++*p; }
static int token(const char** p,char value) { space(p); if (**p!=value) return 0; ++*p; return 1; }
static int number(const char** p,uint32_t* value) {
    uint64_t n=0; const char* start;
    space(p); start=*p;
    while (**p>='0' && **p<='9') {
        n=n*10+(unsigned)(*(*p)++-'0'); if (n>UINT32_MAX) return 0;
    }
    if (*p==start || (*p-start>1 && *start=='0')) return 0;
    *value=(uint32_t)n; return 1;
}
static int key(const char** p,const char* name) {
    size_t n=strlen(name);
    if (!token(p,'"') || strncmp(*p,name,n) || (*p)[n]!='"') return 0;
    *p+=n+1; return token(p,':');
}
int native_folder_icons_read(const NativeCatalogue* c,const WCHAR* registry,uint32_t* values) {
    WCHAR path[32768]; char bytes[8193]; const char* p;
    HANDLE file; DWORD read; uint32_t seen=0; int version=0,folders=0;
    memset(values,0,LIVE_FOLDER_COUNT*sizeof(*values));
    if (!state_path(registry,path)) return 0;
    file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,NULL,OPEN_EXISTING,0,NULL);
    if (file==INVALID_HANDLE_VALUE) return GetLastError()==ERROR_FILE_NOT_FOUND;
    if (!ReadFile(file,bytes,sizeof(bytes),&read,NULL) || read>=sizeof(bytes)) { CloseHandle(file); return 0; }
    CloseHandle(file);
    if (memchr(bytes,0,read)) return 0;
    bytes[read]=0; p=bytes;
    if (read>=3 && !memcmp(p,"\xef\xbb\xbf",3)) p+=3;
    if (!token(&p,'{')) return 0;
    for (;;) {
        const char* save=p;
        if (key(&p,"version")) {
            uint32_t value;
            if (version || !number(&p,&value) || value!=1) return 0;
            version=1;
        } else {
            p=save;
            if (folders || !key(&p,"folders") || !token(&p,'{')) return 0;
            folders=1; space(&p);
            if (*p!='}') for (;;) {
                uint32_t guid,value,index;
                if (!token(&p,'"') || !number(&p,&guid) || !token(&p,'"') ||
                    !token(&p,':') || !number(&p,&value) || guid<LIVE_POOL_BASE ||
                    guid>=LIVE_POOL_BASE+LIVE_RECORD_COUNT || (guid-LIVE_POOL_BASE)%LIVE_SLOT_STRIDE ||
                    (value && !*native_catalogue_icon(c,value))) return 0;
                index=(guid-LIVE_POOL_BASE)/LIVE_SLOT_STRIDE;
                if (seen&(1u<<index)) return 0;
                seen|=1u<<index; values[index]=value;
                space(&p); if (*p=='}') break;
                if (!token(&p,',')) return 0;
            }
            if (!token(&p,'}')) return 0;
        }
        space(&p); if (*p=='}') break;
        if (!token(&p,',')) return 0;
    }
    if (!token(&p,'}')) return 0;
    space(&p); return version && folders && !*p;
}
static int save_folders(const WCHAR* registry,const uint32_t* values) {
    WCHAR path[32768],temporary[32768]; char bytes[2048];
    size_t n=0; uint32_t i; DWORD written; HANDLE file; int first=1,ok;
    if (!state_path(registry,path)) return 0;
    swprintf(temporary,32768,L"%ls.tmp",path);
    n+=(size_t)snprintf(bytes+n,sizeof(bytes)-n,"{\n  \"version\": 1,\n  \"folders\": {");
    for (i=0;i<LIVE_FOLDER_COUNT;++i) if (values[i]) {
        n+=(size_t)snprintf(bytes+n,sizeof(bytes)-n,"%s\n    \"%u\": %u",first ? "" : ",",LIVE_POOL_BASE+i*LIVE_SLOT_STRIDE,values[i]); first=0;
    }
    n+=(size_t)snprintf(bytes+n,sizeof(bytes)-n,"\n  }\n}\n");
    if (n>=sizeof(bytes)) return 0;
    file=CreateFileW(temporary,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
    if (file==INVALID_HANDLE_VALUE) return 0;
    ok=WriteFile(file,bytes,(DWORD)n,&written,NULL) && written==n && FlushFileBuffers(file);
    CloseHandle(file);
    if (ok) ok=MoveFileExW(temporary,path,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
    if (!ok) DeleteFileW(temporary);
    return ok;
}
static int edit_stamp(const WCHAR* path,uint32_t guid,uint32_t icon,char* error,size_t size) {
    HANDLE file=INVALID_HANDLE_VALUE,target=INVALID_HANDLE_VALUE;
    LARGE_INTEGER length; BYTE* original=NULL; void* changed=NULL;
    DWORD read,written; size_t count=0; WCHAR temporary[32768]={0};
    WCHAR* slash; int ok=0,created=0; unsigned attempt;
    // Deny concurrent content writes while preparing the replacement.
    file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_DELETE,NULL,OPEN_EXISTING,0,NULL);
    if (file==INVALID_HANDLE_VALUE || !GetFileSizeEx(file,&length) || length.QuadPart<6 || length.QuadPart>NATIVE_STAMP_MAX_FILE) {
        message(error,size,"Stamp indisponible ou en cours de modification"); goto done;
    }
    original=malloc((size_t)length.QuadPart);
    if (!original || !ReadFile(file,original,(DWORD)length.QuadPart,&read,NULL) || read!=length.QuadPart) {
        message(error,size,"Lecture du stamp impossible"); goto done;
    }
    if (!native_stamp_set_icon(original,read,icon,&changed,&count,error,size)) goto done;
    if (count==read && !memcmp(original,changed,count)) { ok=1; goto done; }
    wcscpy(temporary,path); slash=wcsrchr(temporary,L'\\');
    if (!slash || (size_t)(slash-temporary)+100>=32768) { message(error,size,"Chemin du stamp invalide"); goto done; }
    for (attempt=0;attempt<16;++attempt) {
        // Hidden from the stamp scanner; same filesystem for atomic replacement.
        swprintf(slash+1,100,L".icon-%u-%lu-%llu-%u.tmp",guid,(unsigned long)GetCurrentProcessId(),GetTickCount64(),attempt);
        target=CreateFileW(temporary,GENERIC_WRITE,0,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);
        if (target!=INVALID_HANDLE_VALUE) break;
        if (GetLastError()!=ERROR_FILE_EXISTS) break;
    }
    if (target==INVALID_HANDLE_VALUE) { temporary[0]=0; message(error,size,"Création du fichier temporaire impossible"); goto done; }
    created=1;
    ok=WriteFile(target,changed,(DWORD)count,&written,NULL) && written==count && FlushFileBuffers(target);
    CloseHandle(target); target=INVALID_HANDLE_VALUE;
    if (!ok || !ReplaceFileW(path,temporary,NULL,REPLACEFILE_IGNORE_MERGE_ERRORS,NULL,NULL)) {
        ok=0; message(error,size,"Modification de l'icône impossible ; stamp conservé"); goto done;
    }
    created=0; temporary[0]=0; ok=1;
done:
    if (target!=INVALID_HANDLE_VALUE) CloseHandle(target);
    if (file!=INVALID_HANDLE_VALUE) CloseHandle(file);
    if (created) DeleteFileW(temporary);
    free(original); native_stamp_free(changed); return ok;
}
int native_library_set_icon(NativeCatalogue* c,const WCHAR* root,const WCHAR* registry,
    uint32_t guid,uint32_t icon,char* error,size_t size) {
    const LiveRecord* record=native_catalogue_find(c,guid);
    WCHAR path[32768]; uint32_t slot=guid-LIVE_POOL_BASE;
    int folder=slot%LIVE_SLOT_STRIDE==0;
    if (error && size) error[0]=0;
    if (!record || !native_library_path(root,record,path,32768)) return message(error,size,"Élément indisponible");
    if ((!icon && !folder) || (icon && !*native_catalogue_icon(c,icon))) return message(error,size,"ID d'icône inconnu");
    if (folder) {
        uint32_t values[LIVE_FOLDER_COUNT];
        memcpy(values,c->folder_icons,sizeof(values)); values[slot/LIVE_SLOT_STRIDE]=icon==80587 ? 0 : icon;
        if (!save_folders(registry,values)) return message(error,size,"Enregistrement de l'icône du dossier impossible");
        memcpy(c->folder_icons,values,sizeof(values));
    } else if (!edit_stamp(path,guid,icon,error,size)) return 0;
    c->records[slot].icon_guid=icon ? icon : 80587;
    snprintf(c->records[slot].icon_file,sizeof(c->records[slot].icon_file),"%s",native_catalogue_icon(c,c->records[slot].icon_guid));
    c->records[slot].modified=0; c->dirty=1; ++c->revision;
    return 1;
}
