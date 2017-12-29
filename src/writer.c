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

#include <endian.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <loc/libloc.h>
#include <loc/as.h>
#include <loc/format.h>
#include <loc/network.h>
#include <loc/private.h>
#include <loc/writer.h>

struct loc_writer {
	struct loc_ctx* ctx;
	int refcount;

	struct loc_stringpool* pool;
	off_t vendor;
	off_t description;

	struct loc_as** as;
	size_t as_count;

	struct loc_network_tree* networks;
};

LOC_EXPORT int loc_writer_new(struct loc_ctx* ctx, struct loc_writer** writer) {
	struct loc_writer* w = calloc(1, sizeof(*w));
	if (!w)
		return -ENOMEM;

	w->ctx = loc_ref(ctx);
	w->refcount = 1;

	int r = loc_stringpool_new(ctx, &w->pool);
	if (r) {
		loc_writer_unref(w);
		return r;
	}

	// Initialize the network tree
	r = loc_network_tree_new(ctx, &w->networks);
	if (r) {
		loc_writer_unref(w);
		return r;
	}

	*writer = w;
	return 0;
}

LOC_EXPORT struct loc_writer* loc_writer_ref(struct loc_writer* writer) {
	writer->refcount++;

	return writer;
}

static void loc_writer_free(struct loc_writer* writer) {
	DEBUG(writer->ctx, "Releasing writer at %p\n", writer);

	// Unref all AS
	for (unsigned int i = 0; i < writer->as_count; i++) {
		loc_as_unref(writer->as[i]);
	}

	// Release network tree
	if (writer->networks)
		loc_network_tree_unref(writer->networks);

	// Unref the string pool
	loc_stringpool_unref(writer->pool);

	loc_unref(writer->ctx);
	free(writer);
}

LOC_EXPORT struct loc_writer* loc_writer_unref(struct loc_writer* writer) {
	if (--writer->refcount > 0)
		return writer;

	loc_writer_free(writer);

	return NULL;
}

LOC_EXPORT const char* loc_writer_get_vendor(struct loc_writer* writer) {
	return loc_stringpool_get(writer->pool, writer->vendor);
}

LOC_EXPORT int loc_writer_set_vendor(struct loc_writer* writer, const char* vendor) {
	// Add the string to the string pool
	off_t offset = loc_stringpool_add(writer->pool, vendor);
	if (offset < 0)
		return offset;

	writer->vendor = offset;
	return 0;
}

LOC_EXPORT const char* loc_writer_get_description(struct loc_writer* writer) {
	return loc_stringpool_get(writer->pool, writer->description);
}

LOC_EXPORT int loc_writer_set_description(struct loc_writer* writer, const char* description) {
	// Add the string to the string pool
	off_t offset = loc_stringpool_add(writer->pool, description);
	if (offset < 0)
		return offset;

	writer->description = offset;
	return 0;
}

static int __loc_as_cmp(const void* as1, const void* as2) {
	return loc_as_cmp(*(struct loc_as**)as1, *(struct loc_as**)as2);
}

LOC_EXPORT int loc_writer_add_as(struct loc_writer* writer, struct loc_as** as, uint32_t number) {
	int r = loc_as_new(writer->ctx, writer->pool, as, number);
	if (r)
		return r;

	// We have a new AS to add
	writer->as_count++;

	// Make space
	writer->as = realloc(writer->as, sizeof(*writer->as) * writer->as_count);
	if (!writer->as)
		return -ENOMEM;

	// Add as last element
	writer->as[writer->as_count - 1] = loc_as_ref(*as);

	// Sort everything
	qsort(writer->as, writer->as_count, sizeof(*writer->as), __loc_as_cmp);

	return 0;
}

LOC_EXPORT int loc_writer_add_network(struct loc_writer* writer, struct loc_network** network, const char* string) {
	int r;

	// Create a new network object
	r = loc_network_new_from_string(writer->ctx, network, string);
	if (r)
		return r;

	// Add it to the local tree
	return loc_network_tree_add_network(writer->networks, *network);
}

static void make_magic(struct loc_writer* writer, struct loc_database_magic* magic) {
	// Copy magic bytes
	for (unsigned int i = 0; i < strlen(LOC_DATABASE_MAGIC); i++)
		magic->magic[i] = LOC_DATABASE_MAGIC[i];

	// Set version
	magic->version = htobe16(LOC_DATABASE_VERSION);
}

static void align_page_boundary(off_t* offset, FILE* f) {
	// Move to next page boundary
	while (*offset % LOC_DATABASE_PAGE_SIZE > 0)
		*offset += fwrite("", 1, 1, f);
}

static int loc_database_write_pool(struct loc_writer* writer,
		struct loc_database_header_v0* header, off_t* offset, FILE* f) {
	// Save the offset of the pool section
	DEBUG(writer->ctx, "Pool starts at %jd bytes\n", *offset);
	header->pool_offset = htobe32(*offset);

	// Write the pool
	size_t pool_length = loc_stringpool_write(writer->pool, f);
	*offset += pool_length;

	DEBUG(writer->ctx, "Pool has a length of %zu bytes\n", pool_length);
	header->pool_length = htobe32(pool_length);

	return 0;
}

static int loc_database_write_as_section(struct loc_writer* writer,
		struct loc_database_header_v0* header, off_t* offset, FILE* f) {
	DEBUG(writer->ctx, "AS section starts at %jd bytes\n", *offset);
	header->as_offset = htobe32(*offset);

	size_t as_length = 0;

	struct loc_database_as_v0 as;
	for (unsigned int i = 0; i < writer->as_count; i++) {
		// Convert AS into database format
		loc_as_to_database_v0(writer->as[i], &as);

		// Write to disk
		*offset += fwrite(&as, 1, sizeof(as), f);
		as_length += sizeof(as);
	}

	DEBUG(writer->ctx, "AS section has a length of %zu bytes\n", as_length);
	header->as_length = htobe32(as_length);

	return 0;
}

static int loc_database_write_network_section(struct loc_network* network, void* data) {
	FILE* f = (FILE*)data;

	struct loc_database_network_v0 n;

	int r = loc_network_to_database_v0(network, &n);
	if (r)
		return r;

	fwrite(&n, 1, sizeof(n), f);

	return 0;
}

static int loc_database_write_networks_section(struct loc_writer* writer,
		struct loc_database_header_v0* header, off_t* offset, FILE* f) {
	DEBUG(writer->ctx, "Networks section starts at %jd bytes\n", *offset);
	header->networks_offset = htobe32(*offset);

	size_t networks_length = sizeof(struct loc_database_network_v0)
		* loc_network_tree_count_networks(writer->networks);
	offset += networks_length;

	int r = loc_network_tree_walk(writer->networks, NULL, loc_database_write_network_section, f);
	if (r)
		return r;

	header->networks_length = htobe32(networks_length);

	return 0;
}

LOC_EXPORT int loc_writer_write(struct loc_writer* writer, FILE* f) {
	struct loc_database_magic magic;
	make_magic(writer, &magic);

	// Make the header
	struct loc_database_header_v0 header;
	header.vendor      = htobe32(writer->vendor);
	header.description = htobe32(writer->description);

	time_t now = time(NULL);
	header.created_at = htobe64(now);

	int r;
	off_t offset = 0;

	// Start writing at the beginning of the file
	r = fseek(f, 0, SEEK_SET);
	if (r)
		return r;

	// Write the magic
	offset += fwrite(&magic, 1, sizeof(magic), f);

	// Skip the space we need to write the header later
	r = fseek(f, sizeof(header), SEEK_CUR);
	if (r) {
		DEBUG(writer->ctx, "Could not seek to position after header\n");
		return r;
	}
	offset += sizeof(header);

	align_page_boundary(&offset, f);

	// Write all ASes
	r = loc_database_write_as_section(writer, &header, &offset, f);
	if (r)
		return r;

	align_page_boundary(&offset, f);

	// Write all networks
	r = loc_database_write_networks_section(writer, &header, &offset, f);
	if (r)
		return r;

	align_page_boundary(&offset, f);

	// Write pool
	r = loc_database_write_pool(writer, &header, &offset, f);
	if (r)
		return r;

	// Write the header
	r = fseek(f, sizeof(magic), SEEK_SET);
	if (r)
		return r;

	offset += fwrite(&header, 1, sizeof(header), f);

	return 0;
}