/* Need snprintf(). */
#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#include "config.h"
#include "intsym.h"
#include "stringlists.h"
#include "compat.h"

#include "module.h"
#include "debug.h"

#ifndef USE_MODULES
#error This is a build without modules. Why am I here?
#endif

#define MODULE_SYMBOL_PREFIX 	"mpg123_"
#define MODULE_SYMBOL_SUFFIX 	"_module_info"

#ifdef WANT_WIN32_UNICODE
#define WW(x) L##x
#define WWC wchar_t
#else
#define WW(x) x
#define WWC char
#endif

static const WWC* modulesearch[] =
{
	WW("..\\lib\\mpg123"),
	WW("plugins"),
	WW("libout123\\modules\\.libs"),
	WW("libout123\\modules"),
	WW("..\\libout123\\modules\\.libs"),
	WW("..\\libout123\\modules"),
	NULL
};

#ifdef USE_MODULES_WIN32
#include <windows.h>
#include <shlwapi.h>
#include <pathcch.h>
#include <stdlib.h>

static HRESULT (*__stdcall mypac)(const wchar_t *in, const wchar_t* more, unsigned long flags, wchar_t **out);

void close_module(mpg123_module_t* module, int verbose) {
  int err = FreeLibrary(module->handle);
  if(!err && verbose > -1)
    error("Failed to close module.");
}

static WWC* getplugdir(const char *root) {
  size_t sz, szd, ptr;
  WWC *ret;
  int isdir;

#ifdef WANT_WIN32_UNICODE
  wchar_t *rootw;
  wchar_t *unc;
  win32_utf8_wide(root, &rootw, NULL);
  if(!rootw) {
    error("Out of Memory converting plugin dir path!");
    return NULL;
  }
  sz = wcslen(rootw);

  if(!mypac) {
    HMODULE pathcch = GetModuleHandleA("kernelbase");
    if (pathcch)
      mypac = (void *)GetProcAddress(pathcch, "PathAllocCombine");
  }

#else
  sz = strnlen(root);
#endif

  for(ptr = 0; modulesearch[ptr]; ptr++) {
#ifdef WANT_WIN32_UNICODE
    szd = wcslen(modulesearch[ptr]);
#else
    sz = wcslen(modulesearch[ptr]);
#endif
   if (!mypac) {
      ret = LocalAlloc(LPTR, (sz + szd + 2) * sizeof(*ret));
      if (!ret)
        goto end;
    }

#ifdef WANT_WIN32_UNICODE
    if(mypac)
      mypac(rootw, modulesearch[ptr], PATHCCH_ALLOW_LONG_PATHS, &ret);
    else
      PathCombineW(ret, rootw, modulesearch[ptr]);

  if(!PathIsUNCW(ret)) {
    sz = wcslen(ret) + 4;
    unc = LocalAlloc(LPTR, (sz + 1) * sizeof(*unc));
    if(!unc) {
      error("Out of Memory converting UNC plugin path!");
      goto end;
    }
    snwprintf(unc, sz, L"\\\\?\\%ws", ret);
    LocalFree(ret);
    ret = unc;
  }

    isdir = GetFileAttributesW(ret);
#else
    PathCombineA(ret, root, modulesearch[ptr]);
    isdir = GetFileAttributesA(ret);
#endif

    if(isdir != INVALID_FILE_ATTRIBUTES && isdir & FILE_ATTRIBUTE_DIRECTORY)
      break;
    LocalFree(ret);
    ret = NULL;
  }

end:
#ifdef WANT_WIN32_UNICODE
  free(rootw);
#endif
  return ret;
}

mpg123_module_t* open_module(const char* type, const char* name, int verbose
,const char* bindir) {
  WWC *plugdir, *dllname;
  char *symbol;
  size_t dllnamelen, symbollen;
  HMODULE dll;
  mpg123_module_t *ret = NULL;

  plugdir = getplugdir(bindir);
  if(verbose)
    debug1("Plugin dir found! %s\n", plugdir ? plugdir : WW("NULL"));
  if(!plugdir)
    return ret;

#ifdef WANT_WIN32_UNICODE
  dllnamelen = snwprintf(NULL, 0, L"%s\\%S_%S%S", plugdir, type, name, LT_MODULE_EXT);
#else
  dllnamelen = snprintf(NULL, 0, L"%s\\%s_%s%s", plugdir, type, name, LT_MODULE_EXT);
#endif

  dllname = calloc(dllnamelen + 1, sizeof(*dllname));
  if(!dllname) {
    debug1("Out of Memory allocating %I64u bytes!\n",
      (unsigned long long)((dllnamelen + 1) * sizeof(*dllname)));
    goto end;
  }

#ifdef WANT_WIN32_UNICODE
  snwprintf(dllname, dllnamelen, L"%ws\\%S_%S%S", plugdir, type, name, LT_MODULE_EXT);
  if(verbose) debug1("Trying to load plugin! %S\n", dllname);
#else
  snprintf(dllname, dllnamelen, L"%s\\%s_%s%s", plugdir, type, name, LT_MODULE_EXT);
  if(verbose) debug1("Trying to load plugin! %s\n", dllname);
#endif

#ifdef WANT_WIN32_UNICODE
  dll = LoadLibraryW(dllname);
  if(!dll) debug1("Failed to load plugin! %S\n", dllname);
#else
  dll = LoadLibraryA(dllname);
  if(!dll) debug1("Failed to load plugin! %s\n", dllname);
#endif

  symbollen = strlen( MODULE_SYMBOL_PREFIX ) + strlen( type ) + strlen( MODULE_SYMBOL_SUFFIX ) + 1;
  symbol = calloc(symbollen + 1, sizeof(*symbol));
  if(!symbol) {
    debug1("Out of Memory allocating %I64u bytes!\n",
      (unsigned long long)((symbollen + 1) * sizeof(*symbol)));
    goto end1;
  }
  snprintf(symbol, symbollen, "%s%s%s", MODULE_SYMBOL_PREFIX, type, MODULE_SYMBOL_SUFFIX);
  ret = (mpg123_module_t*) GetProcAddress(dll, symbol);

  if(verbose)
    debug1("Found symbol at %p\n", ret);

  free(symbol);
end1:
  free(dllname);
end:
  LocalFree(plugdir);
  return ret;
}

static int list_modulesW(const char *type, char ***names, char ***descr, int verbose, const char* bindir) {
  size_t sz;
  wchar_t *plugdir, *plugdirdot, *pattern;
  WIN32_FIND_DATAW d;
  HANDLE ffn;

  plugdir = getplugdir(bindir);

  if(!plugdir)
    return -1;

  sz = strlen(type) + 2 + strlen(LT_MODULE_EXT);
  pattern = calloc(sz + 1, sizeof(*pattern));
  snwprintf(pattern, sz, L"%S_*%S", type, LT_MODULE_EXT);

  sz += wcslen(plugdir) + 1;
  plugdirdot = calloc(sz + 1, sizeof(*plugdirdot));
  snwprintf(plugdirdot, sz, L"%ws\\%ws", plugdir, pattern);
  free(pattern);
  LocalFree(plugdir);

  ffn = FindFirstFileW(plugdirdot, &d);
  if (ffn == INVALID_HANDLE_VALUE)
    return -2;

  while(FindNextFileW(ffn, &d));

  FindClose(ffn);
  return 0;
}

#endif
