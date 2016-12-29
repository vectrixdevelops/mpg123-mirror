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

#ifndef WANT_WIN32_UNICODE
#error No windows modules without Unicode.
#endif

static const wchar_t* modulesearch[] =
{
	L"..\\lib\\mpg123"
,	L"plugins"
,	L"libout123\\modules\\.libs"
,	L"libout123\\modules"
,	L"..\\libout123\\modules\\.libs"
,	L"..\\libout123\\modules"
,	NULL
};

#ifdef USE_MODULES_WIN32
#include <windows.h>
#include <shlwapi.h>
#include <pathcch.h>
#include <stdlib.h>

void close_module(mpg123_module_t* module, int verbose) {
  int err = FreeLibrary(module->handle);
  if(!err && verbose > -1)
    error1("Failed to close module. GetLastError: %u", GetLastError());
}

static wchar_t* getplugdir(const char *root, int verbose) {
  // This variation of combinepath can work with UNC paths, but is not
  // officially exposed in any DLLs, It also allocates all its buffers
  // internally via LocalAlloc, avoiding buffer overflow problems.
  HRESULT (*__stdcall mypac)(const wchar_t *in, const wchar_t* more
  , unsigned long flags, wchar_t **out) = NULL;

  size_t sz, szd, ptr;
  wchar_t *env, *ret;
  int isdir = 0;

  env = _wgetenv(L"MPG123_MODDIR");
  if(verbose > 1)
    fwprintf(stderr, L"Trying module directory from environment: %s\n", env);
  if(env) {
    isdir = GetFileAttributesW(env);
  }

  if(isdir != INVALID_FILE_ATTRIBUTES && isdir & FILE_ATTRIBUTE_DIRECTORY) {
    sz = wcslen(env);
    ret = LocalAlloc(LPTR, (sz + 1) * sizeof(*ret));
    wcsncpy(ret, env, sz);
   return ret;
  }

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

  for(ptr = 0; modulesearch[ptr]; ptr++) {
    szd = wcslen(modulesearch[ptr]);
    if (!mypac) {
      ret = LocalAlloc(LPTR, (sz + szd + 2) * sizeof(*ret));
      if (!ret)
        goto end;
    }

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
    if(isdir != INVALID_FILE_ATTRIBUTES && isdir & FILE_ATTRIBUTE_DIRECTORY)
      break;
    LocalFree(ret);
    ret = NULL;
  }

end:
  free(rootw);
  return ret;
}

mpg123_module_t* open_module(const char* type, const char* name, int verbose, const char* bindir) {
  wchar_t *plugdir, *dllname;
  char *symbol;
  size_t dllnamelen, symbollen;
  HMODULE dll;
  mpg123_module_t *ret = NULL;

  plugdir = getplugdir(bindir, verbose);
  if(verbose)
    debug1("Plugin dir found! %s\n", plugdir ? plugdir : WW("NULL"));
  if(!plugdir)
    return ret;

  dllnamelen = snwprintf(NULL, 0, L"%s\\%S_%S%S", plugdir, type, name, LT_MODULE_EXT);

  dllname = calloc(dllnamelen + 1, sizeof(*dllname));
  if(!dllname) {
    debug1("Out of Memory allocating %I64u bytes!\n",
      (unsigned long long)((dllnamelen + 1) * sizeof(*dllname)));
    goto end;
  }

  snwprintf(dllname, dllnamelen, L"%ws\\%S_%S%S", plugdir, type, name, LT_MODULE_EXT);
  if(verbose) debug1("Trying to load plugin! %S\n", dllname);

  dll = LoadLibraryW(dllname);
  if(!dll) debug1("Failed to load plugin! %S\n", dllname);

  symbollen = strlen( MODULE_SYMBOL_PREFIX ) + strlen( type ) + strlen( MODULE_SYMBOL_SUFFIX ) + 1;
  symbol = calloc(symbollen + 1, sizeof(*symbol));
  if(!symbol) {
    debug1("Out of Memory allocating %I64u bytes!\n",
      (unsigned long long)((symbollen + 1) * sizeof(*symbol)));
    goto end1;
  }
  snprintf(symbol, symbollen, "%s%s%s", MODULE_SYMBOL_PREFIX, type, MODULE_SYMBOL_SUFFIX);
  ret = (mpg123_module_t*) GetProcAddress(dll, symbol);
  ret->handle = dll;

  if (!ret) {
      error2("Cannot load symbol %s, error %u", symbol, GetLastError());
      close_module(ret, 0);
      ret = NULL;
      goto end2;
  }

  if (MPG123_MODULE_API_VERSION != ret->api_version) {
    error2( "API version of module does not match (got %i, expected %i).", ret->api_version, MPG123_MODULE_API_VERSION);
    close_module(ret, 0);
    ret = NULL;
    goto end2;
  }

end2:
  free(symbol);
end1:
  free(dllname);
end:
  LocalFree(plugdir);
  return ret;
}

int list_modules(const char *type, char ***names, char ***descr, int verbose, const char* bindir) {
  size_t sz;
  wchar_t *plugdir, *plugdirdot, *pattern, *name;
  char *nameA;
  WIN32_FIND_DATAW d;
  HANDLE ffn;
  mpg123_module_t* module;
  int next, ret = 0;
  *names = *descr = NULL;

  plugdir = getplugdir(bindir, verbose);
  if(!plugdir)
    return 0;

  sz = strlen(type) + 2 + strlen(LT_MODULE_EXT);
  pattern = calloc(sz + 1, sizeof(*pattern));
  snwprintf(pattern, sz, L"%S_*%S", type, LT_MODULE_EXT);

  sz += wcslen(plugdir) + 1;
  plugdirdot = calloc(sz + 1, sizeof(*plugdirdot));
  snwprintf(plugdirdot, sz, L"%ws\\%ws", plugdir, pattern);
  free(pattern);

  ffn = FindFirstFileW(plugdirdot, &d);
  if (ffn == INVALID_HANDLE_VALUE) {
    error1("FindFirstFileW failed %u!", GetLastError());
    goto end;
  }
  next = 1;

  ret = 0;
  for (; next; next = FindNextFileW(ffn, &d)) {
    PathRemoveExtensionW(d.cFileName);
    name = wcschr(d.cFileName, L'_');
    if(!name || name[1] == L'\0')
      continue;
    name++;

    win32_wide_utf8(name, &nameA, NULL);
    if(!nameA)
      continue;
    module = open_module(type, nameA, verbose, bindir);
    if(!module) {
      error1("Unable to load %s!", nameA);
      free(nameA);
      continue;
    }

    if( stringlists_add( names, descr,
      module->name, module->description, &ret) )
      if(verbose > -1)
        error("OOM");

    close_module(module, verbose);
    free(nameA);
  }

  FindClose(ffn);

end:
  LocalFree(plugdir);
  FindClose(ffn);
  return ret;
}

#endif
