/*
	libloc - A library to determine the location of someone on the Internet

	Copyright (C) 2020 IPFire Development Team <info@ipfire.org>

	This library is free software; you can redistribute it and/or
	modify it under the terms of the GNU Lesser General Public
	License as published by the Free Software Foundation; either
	version 2.1 of the License, or (at your option) any later version.

	This library is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
	Lesser General Public License for more details.
*/

#include <errno.h>
#include <stdlib.h>

#include <loc/as.h>
#include <loc/as-list.h>
#include <loc/private.h>

struct loc_as_list {
	struct loc_ctx* ctx;
	int refcount;

	struct loc_as* list[1024];
	size_t size;
	size_t max_size;
};

LOC_EXPORT int loc_as_list_new(struct loc_ctx* ctx,
		struct loc_as_list** list) {
	struct loc_as_list* l = calloc(1, sizeof(*l));
	if (!l)
		return -ENOMEM;

	l->ctx = loc_ref(ctx);
	l->refcount = 1;

	// Do not allow this list to grow larger than this
	l->max_size = 1024;

	DEBUG(l->ctx, "AS list allocated at %p\n", l);
	*list = l;

	return 0;
}

LOC_EXPORT struct loc_as_list* loc_as_list_ref(struct loc_as_list* list) {
	list->refcount++;

	return list;
}

static void loc_as_list_free(struct loc_as_list* list) {
	DEBUG(list->ctx, "Releasing AS list at %p\n", list);

	loc_as_list_clear(list);

	loc_unref(list->ctx);
	free(list);
}

LOC_EXPORT struct loc_as_list* loc_as_list_unref(struct loc_as_list* list) {
	if (!list)
		return NULL;

	if (--list->refcount > 0)
		return list;

	loc_as_list_free(list);
	return NULL;
}

LOC_EXPORT size_t loc_as_list_size(struct loc_as_list* list) {
	return list->size;
}

LOC_EXPORT int loc_as_list_empty(struct loc_as_list* list) {
	return list->size == 0;
}

LOC_EXPORT void loc_as_list_clear(struct loc_as_list* list) {
	for (unsigned int i = 0; i < list->size; i++)
		loc_as_unref(list->list[i]);
}

LOC_EXPORT struct loc_as* loc_as_list_get(struct loc_as_list* list, size_t index) {
	// Check index
	if (index >= list->size)
		return NULL;

	return loc_as_ref(list->list[index]);
}

LOC_EXPORT int loc_as_list_append(
		struct loc_as_list* list, struct loc_as* as) {
	if (loc_as_list_contains(list, as))
		return 0;

	// Check if we have space left
	if (list->size == list->max_size) {
		ERROR(list->ctx, "%p: Could not append AS to the list. List full\n", list);
		return -ENOMEM;
	}

	DEBUG(list->ctx, "%p: Appending AS %p to list\n", list, as);

	list->list[list->size++] = loc_as_ref(as);

	return 0;
}

LOC_EXPORT int loc_as_list_contains(
		struct loc_as_list* list, struct loc_as* as) {
	for (unsigned int i = 0; i < list->size; i++) {
		if (loc_as_cmp(as, list->list[i]) == 0)
			return 1;
	}

	return 0;
}

LOC_EXPORT int loc_as_list_contains_number(
		struct loc_as_list* list, uint32_t number) {
	struct loc_as* as;

	int r = loc_as_new(list->ctx, &as, number);
	if (r)
		return -1;

	r = loc_as_list_contains(list, as);
	loc_as_unref(as);

	return r;
}
