#ifndef OUT123_INTSYM_H
#define OUT123_INTSYM_H
/* Mapping of internal mpg123 symbols to something that is less likely to conflict in case of static linking. */
#define safe_realloc IOT123_safe_realloc
#define compat_open IOT123_compat_open
#define compat_fopen IOT123_compat_fopen
#define compat_close IOT123_compat_close
#define compat_fclose IOT123_compat_fclose
#define win32_wide_utf8 IOT123_win32_wide_utf8
#define win32_utf8_wide IOT123_win32_utf8_wide
#define open_module IOT123_open_module
#define close_module IOT123_close_module
#define buffer_loop IOT123_buffer_loop
#define real_buffer_ignore_lowmem IOT123_real_buffer_ignore_lowmem
#define real_buffer_end IOT123_real_buffer_end
#define real_buffer_resync IOT123_real_buffer_resync
#define real_plain_buffer_resync IOT123_real_plain_buffer_resync
#define real_buffer_reset IOT123_real_buffer_reset
#define real_buffer_start IOT123_real_buffer_start
#define real_buffer_stop IOT123_real_buffer_stop
#define xfermem_init IOT123_xfermem_init
#define xfermem_init_writer IOT123_xfermem_init_writer
#define xfermem_init_reader IOT123_xfermem_init_reader
#define xfermem_get_freespace IOT123_xfermem_get_freespace
#define xfermem_get_usedspace IOT123_xfermem_get_usedspace
#define xfermem_getcmd IOT123_xfermem_getcmd
#define xfermem_putcmd IOT123_xfermem_putcmd
#define xfermem_block IOT123_xfermem_block
#define xfermem_sigblock IOT123_xfermem_sigblock
#define xfermem_write IOT123_xfermem_write
#define xfermem_done IOT123_xfermem_done
#ifndef HAVE_STRDUP
#define strdup IOT123_strdup
#endif
#ifndef HAVE_STRERROR
#define strerror IOT123_strerror
#endif
#endif
