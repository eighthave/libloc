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
#include <sys/queue.h>
#include <time.h>

#include <loc/libloc.h>
#include <loc/as.h>
#include <loc/country.h>
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
	off_t license;

	struct loc_as** as;
	size_t as_count;

	struct loc_country** countries;
	size_t countries_count;

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

LOC_EXPORT const char* loc_writer_get_license(struct loc_writer* writer) {
	return loc_stringpool_get(writer->pool, writer->license);
}

LOC_EXPORT int loc_writer_set_license(struct loc_writer* writer, const char* license) {
	// Add the string to the string pool
	off_t offset = loc_stringpool_add(writer->pool, license);
	if (offset < 0)
		return offset;

	writer->license = offset;
	return 0;
}

static int __loc_as_cmp(const void* as1, const void* as2) {
	return loc_as_cmp(*(struct loc_as**)as1, *(struct loc_as**)as2);
}

LOC_EXPORT int loc_writer_add_as(struct loc_writer* writer, struct loc_as** as, uint32_t number) {
	int r = loc_as_new(writer->ctx, as, number);
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

static int __loc_country_cmp(const void* country1, const void* country2) {
	return loc_country_cmp(*(struct loc_country**)country1, *(struct loc_country**)country2);
}

LOC_EXPORT int loc_writer_add_country(struct loc_writer* writer, struct loc_country** country, const char* country_code) {
	int r = loc_country_new(writer->ctx, country, country_code);
	if (r)
		return r;

	// We have a new country to add
	writer->countries_count++;

	// Make space
	writer->countries = realloc(writer->countries, sizeof(*writer->countries) * writer->countries_count);
	if (!writer->countries)
		return -ENOMEM;

	// Add as last element
	writer->countries[writer->countries_count - 1] = loc_country_ref(*country);

	// Sort everything
	qsort(writer->countries, writer->countries_count, sizeof(*writer->countries), __loc_country_cmp);

	return 0;
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
		loc_as_to_database_v0(writer->as[i], writer->pool, &as);

		// Write to disk
		*offset += fwrite(&as, 1, sizeof(as), f);
		as_length += sizeof(as);
	}

	DEBUG(writer->ctx, "AS section has a length of %zu bytes\n", as_length);
	header->as_length = htobe32(as_length);

	align_page_boundary(offset, f);

	return 0;
}

struct node {
	TAILQ_ENTRY(node) nodes;

	struct loc_network_tree_node* node;

	// Indices of the child nodes
	uint32_t index_zero;
	uint32_t index_one;
};

static struct node* make_node(struct loc_network_tree_node* node) {
	struct node* n = malloc(sizeof(*n));
	if (!n)
		return NULL;

	n->node  = loc_network_tree_node_ref(node);
	n->index_zero = n->index_one = 0;

	return n;
}

static void free_node(struct node* node) {
	loc_network_tree_node_unref(node->node);

	free(node);
}

struct network {
	TAILQ_ENTRY(network) networks;

	struct loc_network* network;
};

static struct network* make_network(struct loc_network* network) {
	struct network* n = malloc(sizeof(*n));
	if (!n)
		return NULL;

	n->network = loc_network_ref(network);

	return n;
}

static void free_network(struct network* network) {
	loc_network_unref(network->network);

	free(network);
}

static int loc_database_write_networks(struct loc_writer* writer,
		struct loc_database_header_v0* header, off_t* offset, FILE* f) {
	// Write the network tree
	DEBUG(writer->ctx, "Network tree starts at %jd bytes\n", *offset);
	header->network_tree_offset = htobe32(*offset);

	size_t network_tree_length = 0;
	size_t network_data_length = 0;

	struct node* node;
	struct node* child_node;

	uint32_t index = 0;
	uint32_t network_index = 0;

	struct loc_database_network_v0 db_network;
	struct loc_database_network_node_v0 db_node;

	// Initialize queue for nodes
	TAILQ_HEAD(node_t, node) nodes;
	TAILQ_INIT(&nodes);

	// Initialize queue for networks
	TAILQ_HEAD(network_t, network) networks;
	TAILQ_INIT(&networks);

	// Add root
	struct loc_network_tree_node* root = loc_network_tree_get_root(writer->networks);
	node = make_node(root);

	TAILQ_INSERT_TAIL(&nodes, node, nodes);

	while (!TAILQ_EMPTY(&nodes)) {
		// Pop first node in list
		node = TAILQ_FIRST(&nodes);
		TAILQ_REMOVE(&nodes, node, nodes);

		DEBUG(writer->ctx, "Processing node %p\n", node);

		// Get child nodes
		struct loc_network_tree_node* node_zero = loc_network_tree_node_get(node->node, 0);
		if (node_zero) {
			node->index_zero = ++index;

			child_node = make_node(node_zero);
			loc_network_tree_node_unref(node_zero);

			TAILQ_INSERT_TAIL(&nodes, child_node, nodes);
		}

		struct loc_network_tree_node* node_one = loc_network_tree_node_get(node->node, 1);
		if (node_one) {
			node->index_one = ++index;

			child_node = make_node(node_one);
			loc_network_tree_node_unref(node_one);

			TAILQ_INSERT_TAIL(&nodes, child_node, nodes);
		}

		// Prepare what we are writing to disk
		db_node.zero = htobe32(node->index_zero);
		db_node.one  = htobe32(node->index_one);

		if (loc_network_tree_node_is_leaf(node->node)) {
			struct loc_network* network = loc_network_tree_node_get_network(node->node);

			// Append network to be written out later
			struct network* nw = make_network(network);
			TAILQ_INSERT_TAIL(&networks, nw, networks);

			db_node.network = htobe32(network_index++);
			loc_network_unref(network);
		} else {
			db_node.network = htobe32(0xffffffff);
		}

		// Write the current node
		DEBUG(writer->ctx, "Writing node %p (0 = %d, 1 = %d)\n",
			node, node->index_zero, node->index_one);

		*offset += fwrite(&db_node, 1, sizeof(db_node), f);
		network_tree_length += sizeof(db_node);

		free_node(node);
	}

	loc_network_tree_node_unref(root);

	header->network_tree_length = htobe32(network_tree_length);

	align_page_boundary(offset, f);

	DEBUG(writer->ctx, "Networks data section starts at %jd bytes\n", *offset);
	header->network_data_offset = htobe32(*offset);

	// We have now written the entire tree and have all networks
	// in a queue in order as they are indexed
	while (!TAILQ_EMPTY(&networks)) {
		struct network* nw = TAILQ_FIRST(&networks);
		TAILQ_REMOVE(&networks, nw, networks);

		// Prepare what we are writing to disk
		int r = loc_network_to_database_v0(nw->network, &db_network);
		if (r)
			return r;

		*offset += fwrite(&db_network, 1, sizeof(db_network), f);
		network_data_length += sizeof(db_network);

		free_network(nw);
	}

	header->network_data_length = htobe32(network_data_length);

	align_page_boundary(offset, f);

	return 0;
}

static int loc_database_write_countries(struct loc_writer* writer,
		struct loc_database_header_v0* header, off_t* offset, FILE* f) {
	DEBUG(writer->ctx, "Countries section starts at %jd bytes\n", *offset);
	header->countries_offset = htobe32(*offset);

	size_t countries_length = 0;

	struct loc_database_country_v0 country;
	for (unsigned int i = 0; i < writer->countries_count; i++) {
		// Convert country into database format
		loc_country_to_database_v0(writer->countries[i], writer->pool, &country);

		// Write to disk
		*offset += fwrite(&country, 1, sizeof(country), f);
		countries_length += sizeof(country);
	}

	DEBUG(writer->ctx, "Countries section has a length of %zu bytes\n", countries_length);
	header->countries_length = htobe32(countries_length);

	align_page_boundary(offset, f);

	return 0;
}

LOC_EXPORT int loc_writer_write(struct loc_writer* writer, FILE* f) {
	struct loc_database_magic magic;
	make_magic(writer, &magic);

	// Make the header
	struct loc_database_header_v0 header;
	header.vendor      = htobe32(writer->vendor);
	header.description = htobe32(writer->description);
	header.license     = htobe32(writer->license);

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

	// Write all networks
	r = loc_database_write_networks(writer, &header, &offset, f);
	if (r)
		return r;

	// Write pool
	r = loc_database_write_pool(writer, &header, &offset, f);
	if (r)
		return r;

	// Write countries
	r = loc_database_write_countries(writer, &header, &offset, f);
	if (r)
		return r;

	// Write the header
	r = fseek(f, sizeof(magic), SEEK_SET);
	if (r)
		return r;

	offset += fwrite(&header, 1, sizeof(header), f);

	return 0;
}
