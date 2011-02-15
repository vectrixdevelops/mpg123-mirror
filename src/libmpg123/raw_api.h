/*
	raw_api: low level decoding api

	copyright 2011 by the mpg123 project - free software under the terms of the LGPL 2.1
	see COPYING and AUTHORS files in distribution or http://mpg123.org
	initially written by Vincent Falco
*/

#ifndef MPG123_RAWAPI_H
#define MPG123_RAWAPI_H

#include "config.h"
#include "mpg123.h"

int raw_init(mpg123_handle *fr);
void raw_close(mpg123_handle *fr);
ssize_t raw_read(mpg123_handle *fr, unsigned char *out, ssize_t count);
off_t raw_skip_bytes(mpg123_handle *fr,off_t len);
int raw_back_bytes(mpg123_handle *fr, off_t bytes);
int raw_seek_frame(mpg123_handle *fr, off_t num);
off_t raw_tell(mpg123_handle *fr);
void raw_rewind(mpg123_handle *fr);
void raw_forget(mpg123_handle *fr);

#endif
