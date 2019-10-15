/*
	libloc - A library to determine the location of someone on the Internet

	Copyright (C) 2019 IPFire Development Team <info@ipfire.org>

	This library is free software; you can redistribute it and/or
	modify it under the terms of the GNU Lesser General Public
	License as published by the Free Software Foundation; either
	version 2.1 of the License, or (at your option) any later version.

	This library is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
	Lesser General Public License for more details.
*/

#ifndef LIBLOC_COUNTRY_H
#define LIBLOC_COUNTRY_H

#include <loc/libloc.h>
#include <loc/format.h>
#include <loc/stringpool.h>

struct loc_country;
int loc_country_new(struct loc_ctx* ctx, struct loc_country** country, const char* country_code);
struct loc_country* loc_country_ref(struct loc_country* country);
struct loc_country* loc_country_unref(struct loc_country* country);

const char* loc_country_get_code(struct loc_country* country);

const char* loc_country_get_continent_code(struct loc_country* country);
int loc_country_set_continent_code(struct loc_country* country, const char* continent_code);

const char* loc_country_get_name(struct loc_country* country);
int loc_country_set_name(struct loc_country* country, const char* name);

int loc_country_cmp(struct loc_country* country1, struct loc_country* country2);

#ifdef LIBLOC_PRIVATE

int loc_country_new_from_database_v0(struct loc_ctx* ctx, struct loc_stringpool* pool,
		struct loc_country** country, const struct loc_database_country_v0* dbobj);
int loc_country_to_database_v0(struct loc_country* country,
    struct loc_stringpool* pool, struct loc_database_country_v0* dbobj);

static inline void loc_country_copy_code(char* dst, const char* src) {
    for (unsigned int i = 0; i < 2; i++) {
        dst[i] = src[i];
    }

    // Terminate the string
    dst[3] = '\0';
}

#endif

#endif
