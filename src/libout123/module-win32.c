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
#error No windows modules without Unicode.
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

void close_module(mpg123_module_t* module, int verbose) {
  int err = FreeLibrary(module->handle);
  if(!err && verbose > -1)
    error1("Failed to close module. GetLastError: %u", GetLastError());
}

static WWC* getplugdir(const char *root, int verbose) {
  // This variation of combinepath can work with UNC paths, but is not
  // officially exposed in any DLLs, It also allocates all its buffers
  // internally via LocalAlloc, avoiding buffer overflow problems.
  HRESULT (*__stdcall mypac)(const wchar_t *in, const wchar_t* more
  , unsigned long flags, wchar_t **out) = NULL;

  size_t sz, szd, ptr;
  WWC *env, *ret;
  int isdir = 0;

#ifdef WANT_WIN32_UNICODE
  env = _wgetenv(L"MPG123_MODDIR");
  if(verbose > 1)
    fwprintf(stderr, L"Trying module directory from environment: %s\n", env);
  if(env) {
    isdir = GetFileAttributesW(env);
  }
#endif

  if(isdir != INVALID_FILE_ATTRIBUTES && isdir & FILE_ATTRIBUTE_DIRECTORY) {
#ifdef WANT_WIN32_UNICODE
    sz = wcslen(env);
    ret = LocalAlloc(LPTR, (sz + 1) * sizeof(*ret));
    wcsncpy(ret, env, sz);
#endif
   return ret;
  }

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
#endif

  for(ptr = 0; modulesearch[ptr]; ptr++) {
#ifdef WANT_WIN32_UNICODE
    szd = wcslen(modulesearch[ptr]);
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

mpg123_module_t* open_module(const char* type, const char* name, int verbose, const char* bindir) {
  WWC *plugdir, *dllname;
  char *symbol;
  size_t dllnamelen, symbollen;
  HMODULE dll;
  mpg123_module_t *ret = NULL;

  plugdir = getplugdir(bindir, verbose);
  if(verbose)
    debug1("Plugin dir found! %s\n", plugdir ? plugdir : WW("NULL"));
  if(!plugdir)
    return ret;

#ifdef WANT_WIN32_UNICODE
  dllnamelen = snwprintf(NULL, 0, L"%s\\%S_%S%S", plugdir, type, name, LT_MODULE_EXT);
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
#endif

#ifdef WANT_WIN32_UNICODE
  dll = LoadLibraryW(dllname);
  if(!dll) debug1("Failed to load plugin! %S\n", dllname);
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
