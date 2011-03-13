## Makefile.am: produce Makefile.in from this

## copyright by the mpg123 project - free software under the terms of the LGPL 2.1
## see COPYING and AUTHORS files in distribution or http://mpg123.org
## initially written by Nicholas J. Humfrey

#AM_CFLAGS = @AUDIO_CFLAGS@
#AM_LDFLAGS = 
INCLUDES = -I$(top_srcdir)/src -I$(top_srcdir)/src/libout123

SUBDIRS = modules
EXTRA_DIST = mpg123.h.in legacy_module.c module.c


CLEANFILES = *.a

lib_LTLIBRARIES = libout123.la
nodist_include_HEADERS = out123.h

libout123_la_LDFLAGS = -no-undefined -version-info @LIBOUT123_VERSION@ -export-symbols-regex '^out123_'
libout123_la_LIBADD =  @MODULE_OBJ@ @OUTPUT_OBJ@ @OUTPUT_LIBS@
libout123_la_DEPENDENCIES = @OUTPUT_LOBJ@ @MODULE_LOBJ@

libout123_la_SOURCES = \
	module.h \
	wavhead.h \
	wav.c \
	libout123.c

# That is not nice... but it is how I manage to get the dependency on modules/alsa.o without error about .deps/modules/alsa.Tpo .
# Did I mention that recursive make sucks?
# `%'-style pattern rules are a GNU make extension
#find modules/ -name '*.c' | perl -ne 'chomp; $mod=$_; $mod=~s/\.c$/.\$(OBJEXT)/;
#print "$mod: $_ audio.h module.h\n";
#print "\tcd output && \$(MAKE)\n\n";'
