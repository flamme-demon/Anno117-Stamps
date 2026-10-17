/* Explicit library management. Files are renamed/moved with no replacement.
 * Geometry and native FileDB payloads are never rewritten. */
#include "native_catalogue.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>

static const WCHAR* region_names[2]={L"Region Roman",L"Region Celtic"};
static const char* region_paths[2]={"Region Roman","Region Celtic"};
static int fail(char* out,size_t size,const char* message) {
    snprintf(out,size,"%s",message);
    return 0;
}
int native_library_valid_name(const WCHAR* name) {
    WCHAR stem[256];
    char utf8[256];
    size_t i,n;
    if (!name || !(n=wcslen(name)) || n>255 || name[0]==L'.' ||
        iswspace(name[0]) || iswspace(name[n-1]) || name[n-1]==L'.') return 0;
    for (i=0;i<n;++i) if (name[i]<32 || name[i]==127 || wcschr(L"<>:\"/\\|?*",name[i])) return 0;
    if (!WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,name,-1,utf8,sizeof(utf8),NULL,NULL)) return 0;
    for (i=0;i<n && name[i]!=L'.';++i) stem[i]=towupper(name[i]);
    while (i && stem[i-1]==L' ') --i;
    stem[i]=0;
    if (!wcscmp(stem,L"CON") || !wcscmp(stem,L"PRN") || !wcscmp(stem,L"AUX") ||
        !wcscmp(stem,L"NUL") || !wcscmp(stem,L"CONIN$") || !wcscmp(stem,L"CONOUT$")) return 0;
    if (wcslen(stem)==4 && (!wcsncmp(stem,L"COM",3) || !wcsncmp(stem,L"LPT",3)) &&
        wcschr(L"123456789¹²³",stem[3])) return 0;
    return 1;
}
static int plain_directory(const WCHAR* path) {
    DWORD attributes=GetFileAttributesW(path);
    return attributes!=INVALID_FILE_ATTRIBUTES && (attributes&FILE_ATTRIBUTE_DIRECTORY) &&
           !(attributes&FILE_ATTRIBUTE_REPARSE_POINT);
}
int native_library_path(const WCHAR* root,const LiveRecord* r,WCHAR* path,size_t count) {
    WCHAR relative[1024];
    size_t length=wcslen(root),i;
    DWORD attributes;
    if (!plain_directory(root) || !r || !r->path[0] ||
        !MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,r->path,-1,relative,1024) ||
        length+wcslen(relative)+2>=count) return 0;
    wcscpy(path,root); path[length++]=L'\\';
    // Each relative component must be below the stamp root and contain no link.
    for (i=0;;++i) {
        WCHAR ch=relative[i];
        if (ch==L'\\' || ch==L':' || (ch==L'.' && (i==0 || relative[i-1]==L'/'))) return 0;
        path[length++]=ch==L'/' ? L'\\' : ch;
        if (!ch || ch==L'/') {
            path[length-1]=0;
            attributes=GetFileAttributesW(path);
            if (attributes==INVALID_FILE_ATTRIBUTES || (attributes&FILE_ATTRIBUTE_REPARSE_POINT) ||
                (ch && !(attributes&FILE_ATTRIBUTE_DIRECTORY))) return 0;
            if (!ch) return 1;
            path[length-1]=L'\\';
        }
    }
}
static NativeCatalogue* clone(const NativeCatalogue* c) {
    NativeCatalogue* copy=malloc(sizeof(*c));
    if (copy) memcpy(copy,c,sizeof(*c));
    return copy;
}
static void publish(NativeCatalogue* c,NativeCatalogue* copy) {
    uint32_t i;
    copy->row_count=0;
    for (i=0;i<LIVE_RECORD_COUNT;++i) if (copy->records[i].active) copy->rows[copy->row_count++]=i;
    ++copy->revision; copy->dirty=1; copy->registry_dirty=0;
    memcpy(c,copy,sizeof(*c));
}
uint32_t native_library_create_folder(NativeCatalogue* c,const WCHAR* root,const WCHAR* registry,
    uint32_t region,const WCHAR* name,char* error,size_t size) {
    WCHAR base[32768],target[32768];
    char utf8[256],identity[1024];
    uint32_t i,index=UINT32_MAX;
    NativeCatalogue* copy;
    if (region>1 || !native_library_valid_name(name))
        return fail(error,size,"Nom de dossier invalide");
    if (!plain_directory(root) && (!CreateDirectoryW(root,NULL) || !plain_directory(root)))
        return fail(error,size,"Dossier des stamps indisponible");
    WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,name,-1,utf8,sizeof(utf8),NULL,NULL);
    snprintf(identity,sizeof(identity),"%s/%s",region_paths[region],utf8);
    for (i=0;i<LIVE_FOLDERS;++i) {
        uint32_t n=(region*LIVE_FOLDERS+i)*LIVE_SLOT_STRIDE;
        if (c->records[n].assigned && !strcmp(c->records[n].path,identity)) { index=n; break; }
        if (!c->records[n].assigned && index==UINT32_MAX) index=n;
    }
    if (index==UINT32_MAX) return fail(error,size,"Limite de 16 dossiers dans cette région");
    if (wcslen(root)+wcslen(name)+32>=32768) return fail(error,size,"Chemin trop long");
    swprintf(base,32768,L"%ls\\%ls",root,region_names[region]);
    if (!plain_directory(base) && (!CreateDirectoryW(base,NULL) || !plain_directory(base)))
        return fail(error,size,"Région indisponible");
    swprintf(target,32768,L"%ls\\%ls",base,name);
    copy=clone(c);
    if (!copy) return fail(error,size,"Mémoire insuffisante");
    if (!CreateDirectoryW(target,NULL)) { free(copy); return fail(error,size,"Ce dossier existe déjà ou ne peut pas être créé"); }
    snprintf(copy->records[index].path,sizeof(copy->records[index].path),"%s",identity);
    snprintf(copy->records[index].name,sizeof(copy->records[index].name),"%s",utf8);
    copy->records[index].assigned=copy->records[index].active=copy->records[index].seen=1;
    copy->records[index].icon_guid=80587;
    if (!native_catalogue_save(copy,registry)) {
        RemoveDirectoryW(target); free(copy); return fail(error,size,"Enregistrement du dossier impossible");
    }
    publish(c,copy); free(copy);
    return LIVE_POOL_BASE+index;
}
static int matches(const char* path,const char* prefix,int folder) {
    size_t n=strlen(prefix);
    return !strcmp(path,prefix) || (folder && !strncmp(path,prefix,n) && path[n]=='/');
}
int native_library_rename(NativeCatalogue* c,const WCHAR* root,const WCHAR* registry,
    uint32_t guid,const WCHAR* name,char* error,size_t size) {
    const LiveRecord* record=native_catalogue_find(c,guid);
    NativeCatalogue* copy;
    WCHAR source[32768],target[32768];
    char utf8[256],old[1024],next[1024];
    const char* slash;
    uint32_t i;
    int folder=(guid-LIVE_POOL_BASE)%LIVE_SLOT_STRIDE==0;
    if (!record || !native_library_valid_name(name) || !native_library_path(root,record,source,32768))
        return fail(error,size,"Nom invalide ou élément indisponible");
    WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,name,-1,utf8,sizeof(utf8),NULL,NULL);
    if (!strcmp(record->name,utf8)) return 1;
    snprintf(old,sizeof(old),"%s",record->path);
    slash=strrchr(old,'/');
    snprintf(next,sizeof(next),"%.*s/%s",(int)(slash-old),old,utf8);
    wcscpy(target,source); *wcsrchr(target,L'\\')=0;
    if (wcslen(target)+wcslen(name)+2>=32768) return fail(error,size,"Chemin trop long");
    wcscat(target,L"\\"); wcscat(target,name);
    copy=clone(c);
    if (!copy) return fail(error,size,"Mémoire insuffisante");
    for (i=0;i<LIVE_RECORD_COUNT;++i) {
        LiveRecord* r=&copy->records[i];
        char path[1024];
        const char* from=NULL; const char* to=NULL;
        if (!r->assigned) continue;
        if (matches(r->path,old,folder)) { from=old; to=next; }
        else if (matches(r->path,next,folder)) {
            // A deleted identity may reserve this name. Swap its inactive path
            // with the old path, retaining both slots without reusing a GUID.
            if (r->active) { free(copy); return fail(error,size,"Ce nom existe déjà"); }
            from=next; to=old;
        }
        if (!from) continue;
        if (strlen(to)+strlen(r->path+strlen(from))>=sizeof(path)) { free(copy); return fail(error,size,"Chemin trop long"); }
        memcpy(path,to,strlen(to));
        memcpy(path+strlen(to),r->path+strlen(from),strlen(r->path+strlen(from))+1);
        snprintf(r->path,sizeof(r->path),"%s",path);
        snprintf(r->name,sizeof(r->name),"%s",strrchr(path,'/')+1);
    }
    if (!MoveFileExW(source,target,0)) { free(copy); return fail(error,size,"Ce nom existe déjà ou le renommage est impossible"); }
    if (!native_catalogue_save(copy,registry)) {
        DWORD code=GetLastError();
        if (!MoveFileExW(target,source,0)) {
            publish(c,copy); c->registry_dirty=1;
            free(copy); return fail(error,size,"Renommage effectué, sauvegarde des identifiants à réessayer");
        }
        free(copy); SetLastError(code); return fail(error,size,"Renommage annulé : identifiants non enregistrés");
    }
    publish(c,copy); free(copy); return 1;
}
int native_library_trash(NativeCatalogue* c,const WCHAR* root,const WCHAR* registry,
    const WCHAR* trash,uint32_t guid,char* error,size_t size) {
    const LiveRecord* record=native_catalogue_find(c,guid);
    WCHAR source[32768],directory[32768],target[32768],name[256];
    NativeCatalogue* copy;
    unsigned attempt;
    if (!record || (guid-LIVE_POOL_BASE)%LIVE_SLOT_STRIDE==0 ||
        !native_library_path(root,record,source,32768) || !MultiByteToWideChar(CP_UTF8,0,record->name,-1,name,256))
        return fail(error,size,"Stamp indisponible");
    if (wcslen(trash)+wcslen(name)+100>=32768) return fail(error,size,"Chemin trop long");
    if (!plain_directory(trash) && (!CreateDirectoryW(trash,NULL) || !plain_directory(trash)))
        return fail(error,size,"Corbeille indisponible");
    for (attempt=0;attempt<32;++attempt) {
        swprintf(directory,32768,L"%ls\\%llu-%u-%u",trash,GetTickCount64(),guid,attempt);
        if (CreateDirectoryW(directory,NULL)) break;
    }
    if (attempt==32) return fail(error,size,"Corbeille indisponible");
    swprintf(target,32768,L"%ls\\%ls",directory,name);
    copy=clone(c);
    if (!copy) { RemoveDirectoryW(directory); return fail(error,size,"Mémoire insuffisante"); }
    copy->records[guid-LIVE_POOL_BASE].active=0;
    if (!MoveFileExW(source,target,0)) { free(copy); RemoveDirectoryW(directory); return fail(error,size,"Suppression impossible"); }
    if (!native_catalogue_save(copy,registry)) {
        DWORD code=GetLastError();
        if (!MoveFileExW(target,source,0)) {
            publish(c,copy); c->registry_dirty=1; free(copy);
            return fail(error,size,"Stamp déplacé dans la corbeille, identifiants à réessayer");
        }
        free(copy); RemoveDirectoryW(directory); SetLastError(code);
        return fail(error,size,"Suppression annulée : identifiants non enregistrés");
    }
    publish(c,copy); free(copy); return 1;
}
