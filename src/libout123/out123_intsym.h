#ifndef OUT123_INTSYM_H
#define OUT123_INTSYM_H
/* Mapping of internal out123 symbols to something that is less
   likely to conflict in case of static linking. */

/* Functions from compat.c. They're duplicated. Bad. But are these really
   worth a separate library? */
#define safe_realloc OUTINT123_safe_realloc
#define compat_open OUTINT123_compat_open
#define compat_close OUTINT123_compat_close
#define win32_wide_utf8 OUTINT123_win32_wide_utf8
#define win32_utf8_wide OUTINT123_win32_utf8_wide

#endif
