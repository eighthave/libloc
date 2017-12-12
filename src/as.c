/*
	libloc - A library to determine the location of someone on the Internet

	Copyright (C) 2017 IPFire Development Team <info@ipfire.org>

	This library is free software; you can redistribute it and/or
	modify it under the terms of the GNU Lesser General Public
	License as published by the Free Software Foundation; either
	version 2.1 of the License, or (at your option) any later version.

	This library is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
	Lesser General Public License for more details.
*/

#include <arpa/inet.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>

#include <loc/libloc.h>
#include <loc/format.h>

#include "libloc-private.h"
#include "as.h"
#include "stringpool.h"

struct loc_as {
	struct loc_ctx* ctx;
	int refcount;

	struct loc_stringpool* pool;

	uint32_t number;
	off_t name;
};

LOC_EXPORT int loc_as_new(struct loc_ctx* ctx, struct loc_stringpool* pool, struct loc_as** as, uint32_t number) {
	struct loc_as* a = calloc(1, sizeof(*a));
	if (!a)
		return -ENOMEM;

	a->ctx = loc_ref(ctx);
	a->refcount = 1;
	a->pool = loc_stringpool_ref(pool);

	a->number = number;

	DEBUG(a->ctx, "AS%u allocated at %p\n", a->number, a);
	*as = a;

	return 0;
}

LOC_EXPORT struct loc_as* loc_as_ref(struct loc_as* as) {
	as->refcount++;

	return as;
}

static void loc_as_free(struct loc_as* as) {
	DEBUG(as->ctx, "Releasing AS%u %p\n", as->number, as);

	loc_stringpool_unref(as->pool);
	loc_unref(as->ctx);

	free(as);
}

LOC_EXPORT struct loc_as* loc_as_unref(struct loc_as* as) {
	if (--as->refcount > 0)
		return NULL;

	loc_as_free(as);

	return NULL;
}

LOC_EXPORT uint32_t loc_as_get_number(struct loc_as* as) {
	return as->number;
}

LOC_EXPORT const char* loc_as_get_name(struct loc_as* as) {
	return loc_stringpool_get(as->pool, as->name);
}

LOC_EXPORT int loc_as_set_name(struct loc_as* as, const char* name) {
	// Add the string to the string pool
	off_t offset = loc_stringpool_add(as->pool, name);
	if (offset < 0)
		return offset;

	as->name = offset;
	return 0;
}

LOC_EXPORT int loc_as_cmp(struct loc_as* as1, struct loc_as* as2) {
	if (as1->number > as2->number)
		return 1;

	if (as1->number < as2->number)
		return -1;

	return 0;
}

int loc_as_new_from_database_v0(struct loc_ctx* ctx, struct loc_stringpool* pool,
		struct loc_as** as, const struct loc_database_as_v0* dbobj) {
	uint32_t number = ntohl(dbobj->number);

	int r = loc_as_new(ctx, pool, as, number);
	if (r)
		return r;

	(*as)->name = ntohl(dbobj->name);

	return 0;
}

int loc_as_to_database_v0(struct loc_as* as, struct loc_database_as_v0* dbobj) {
	dbobj->number = htonl(as->number);
	dbobj->name   = htonl(as->name);

	return 0;
}
