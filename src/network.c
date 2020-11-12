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
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef HAVE_ENDIAN_H
#  include <endian.h>
#endif

#include <loc/libloc.h>
#include <loc/compat.h>
#include <loc/country.h>
#include <loc/network.h>
#include <loc/private.h>

struct loc_network {
	struct loc_ctx* ctx;
	int refcount;

	int family;
	struct in6_addr first_address;
	struct in6_addr last_address;
	unsigned int prefix;

	char country_code[3];
	uint32_t asn;
	enum loc_network_flags flags;
};

static int valid_prefix(struct in6_addr* address, unsigned int prefix) {
	// The prefix cannot be larger than 128 bits
	if (prefix > 128)
		return 1;

	// And the prefix cannot be zero
	if (prefix == 0)
		return 1;

	// For IPv4-mapped addresses the prefix has to be 96 or lager
	if (IN6_IS_ADDR_V4MAPPED(address) && prefix <= 96)
		return 1;

	return 0;
}

static struct in6_addr prefix_to_bitmask(unsigned int prefix) {
	struct in6_addr bitmask;

	for (unsigned int i = 0; i < 16; i++)
		bitmask.s6_addr[i] = 0;

	for (int i = prefix, j = 0; i > 0; i -= 8, j++) {
		if (i >= 8)
			bitmask.s6_addr[j] = 0xff;
		else
			bitmask.s6_addr[j] = 0xff << (8 - i);
	}

	return bitmask;
}

static struct in6_addr make_first_address(const struct in6_addr* address, const struct in6_addr* bitmask) {
	struct in6_addr a;

	// Perform bitwise AND
	for (unsigned int i = 0; i < 4; i++)
		a.s6_addr32[i] = address->s6_addr32[i] & bitmask->s6_addr32[i];

	return a;
}

static struct in6_addr make_last_address(const struct in6_addr* address, const struct in6_addr* bitmask) {
	struct in6_addr a;

	// Perform bitwise OR
	for (unsigned int i = 0; i < 4; i++)
		a.s6_addr32[i] = address->s6_addr32[i] | ~bitmask->s6_addr32[i];

	return a;
}

static struct in6_addr address_increment(const struct in6_addr* address) {
	struct in6_addr a = *address;

	for (int octet = 15; octet >= 0; octet--) {
		if (a.s6_addr[octet] < 255) {
			a.s6_addr[octet]++;
			break;
		} else {
			a.s6_addr[octet] = 0;
		}
	}

	return a;
}

LOC_EXPORT int loc_network_new(struct loc_ctx* ctx, struct loc_network** network,
		struct in6_addr* address, unsigned int prefix) {
	// Address cannot be unspecified
	if (IN6_IS_ADDR_UNSPECIFIED(address)) {
		DEBUG(ctx, "Start address is unspecified\n");
		return -EINVAL;
	}

	// Address cannot be loopback
	if (IN6_IS_ADDR_LOOPBACK(address)) {
		DEBUG(ctx, "Start address is loopback address\n");
		return -EINVAL;
	}

	// Address cannot be link-local
	if (IN6_IS_ADDR_LINKLOCAL(address)) {
		DEBUG(ctx, "Start address cannot be link-local\n");
		return -EINVAL;
	}

	// Address cannot be site-local
	if (IN6_IS_ADDR_SITELOCAL(address)) {
		DEBUG(ctx, "Start address cannot be site-local\n");
		return -EINVAL;
	}

	// Validate the prefix
	if (valid_prefix(address, prefix) != 0) {
		DEBUG(ctx, "Invalid prefix: %u\n", prefix);
		return -EINVAL;
	}

	struct loc_network* n = calloc(1, sizeof(*n));
	if (!n)
		return -ENOMEM;

	n->ctx = loc_ref(ctx);
	n->refcount = 1;

	// Store the prefix
	n->prefix = prefix;

	// Convert the prefix into a bitmask
	struct in6_addr bitmask = prefix_to_bitmask(n->prefix);

	// Store the first and last address in the network
	n->first_address = make_first_address(address, &bitmask);
	n->last_address = make_last_address(&n->first_address, &bitmask);

	// Set family
	if (IN6_IS_ADDR_V4MAPPED(&n->first_address))
		n->family = AF_INET;
	else
		n->family = AF_INET6;

	DEBUG(n->ctx, "Network allocated at %p\n", n);
	*network = n;
	return 0;
}

LOC_EXPORT int loc_network_new_from_string(struct loc_ctx* ctx, struct loc_network** network,
		const char* address_string) {
	struct in6_addr first_address;
	char* prefix_string;
	unsigned int prefix = 128;
	int r = -EINVAL;

	DEBUG(ctx, "Attempting to parse network %s\n", address_string);

	// Make a copy of the string to work on it
	char* buffer = strdup(address_string);
	address_string = prefix_string = buffer;

	// Split address and prefix
	address_string = strsep(&prefix_string, "/");

	DEBUG(ctx, "  Split into address = %s, prefix = %s\n", address_string, prefix_string);

	// Parse the address
	r = loc_parse_address(ctx, address_string, &first_address);
	if (r) {
		DEBUG(ctx, "The address could not be parsed\n");
		goto FAIL;
	}

	// If a prefix was given, we will try to parse it
	if (prefix_string) {
		// Convert prefix to integer
		prefix = strtol(prefix_string, NULL, 10);

		if (!prefix) {
			DEBUG(ctx, "The prefix was not parsable: %s\n", prefix_string);
			goto FAIL;
		}

		// Map the prefix to IPv6 if needed
		if (IN6_IS_ADDR_V4MAPPED(&first_address))
			prefix += 96;
	}

FAIL:
	// Free temporary buffer
	free(buffer);

	// Exit if the parsing was unsuccessful
	if (r)
		return r;

	// Create a new network
	return loc_network_new(ctx, network, &first_address, prefix);
}

LOC_EXPORT struct loc_network* loc_network_ref(struct loc_network* network) {
	network->refcount++;

	return network;
}

static void loc_network_free(struct loc_network* network) {
	DEBUG(network->ctx, "Releasing network at %p\n", network);

	loc_unref(network->ctx);
	free(network);
}

LOC_EXPORT struct loc_network* loc_network_unref(struct loc_network* network) {
	if (!network)
		return NULL;

	if (--network->refcount > 0)
		return network;

	loc_network_free(network);
	return NULL;
}

static int format_ipv6_address(const struct in6_addr* address, char* string, size_t length) {
	const char* ret = inet_ntop(AF_INET6, address, string, length);
	if (!ret)
		return -1;

	return 0;
}

static int format_ipv4_address(const struct in6_addr* address, char* string, size_t length) {
	struct in_addr ipv4_address;
	ipv4_address.s_addr = address->s6_addr32[3];

	const char* ret = inet_ntop(AF_INET, &ipv4_address, string, length);
	if (!ret)
		return -1;

	return 0;
}

LOC_EXPORT char* loc_network_str(struct loc_network* network) {
	int r;
	const size_t length = INET6_ADDRSTRLEN + 4;

	char* string = malloc(length);
	if (!string)
		return NULL;

	unsigned int prefix = network->prefix;

	switch (network->family) {
		case AF_INET6:
			r = format_ipv6_address(&network->first_address, string, length);
			break;

		case AF_INET:
			r = format_ipv4_address(&network->first_address, string, length);
			prefix -= 96;
			break;

		default:
			r = -1;
			break;
	}

	if (r) {
		ERROR(network->ctx, "Could not convert network to string: %s\n", strerror(errno));
		free(string);

		return NULL;
	}

	// Append prefix
	sprintf(string + strlen(string), "/%u", prefix);

	return string;
}

LOC_EXPORT int loc_network_address_family(struct loc_network* network) {
	return network->family;
}

static char* loc_network_format_address(struct loc_network* network, const struct in6_addr* address) {
	const size_t length = INET6_ADDRSTRLEN;

	char* string = malloc(length);
	if (!string)
		return NULL;

	int r = 0;

	switch (network->family) {
		case AF_INET6:
			r = format_ipv6_address(address, string, length);
			break;

		case AF_INET:
			r = format_ipv4_address(address, string, length);
			break;

		default:
			r = -1;
			break;
	}

	if (r) {
		ERROR(network->ctx, "Could not format IP address to string: %s\n", strerror(errno));
		free(string);

		return NULL;
	}

	return string;
}

LOC_EXPORT char* loc_network_format_first_address(struct loc_network* network) {
	return loc_network_format_address(network, &network->first_address);
}

LOC_EXPORT char* loc_network_format_last_address(struct loc_network* network) {
	return loc_network_format_address(network, &network->last_address);
}

LOC_EXPORT int loc_network_match_address(struct loc_network* network, const struct in6_addr* address) {
	// Address must be larger than the start address
	if (in6_addr_cmp(&network->first_address, address) > 0)
		return 1;

	// Address must be smaller than the last address
	if (in6_addr_cmp(&network->last_address, address) < 0)
		return 1;

	// The address is inside this network
	return 0;
}

LOC_EXPORT const char* loc_network_get_country_code(struct loc_network* network) {
	return network->country_code;
}

LOC_EXPORT int loc_network_set_country_code(struct loc_network* network, const char* country_code) {
	// Set empty country code
	if (!country_code || !*country_code) {
		*network->country_code = '\0';
		return 0;
	}

	// Check country code
	if (!loc_country_code_is_valid(country_code))
		return -EINVAL;

	loc_country_code_copy(network->country_code, country_code);

	return 0;
}

LOC_EXPORT int loc_network_match_country_code(struct loc_network* network, const char* country_code) {
	// Check country code
	if (!loc_country_code_is_valid(country_code))
		return -EINVAL;

	return (network->country_code[0] == country_code[0])
		&& (network->country_code[1] == country_code[1]);
}

LOC_EXPORT uint32_t loc_network_get_asn(struct loc_network* network) {
	return network->asn;
}

LOC_EXPORT int loc_network_set_asn(struct loc_network* network, uint32_t asn) {
	network->asn = asn;

	return 0;
}

LOC_EXPORT int loc_network_match_asn(struct loc_network* network, uint32_t asn) {
	return network->asn == asn;
}

LOC_EXPORT int loc_network_has_flag(struct loc_network* network, uint32_t flag) {
	return network->flags & flag;
}

LOC_EXPORT int loc_network_set_flag(struct loc_network* network, uint32_t flag) {
	network->flags |= flag;

	return 0;
}

LOC_EXPORT int loc_network_match_flag(struct loc_network* network, uint32_t flag) {
	return loc_network_has_flag(network, flag);
}

LOC_EXPORT int loc_network_eq(struct loc_network* self, struct loc_network* other) {
#ifdef ENABLE_DEBUG
	char* n1 = loc_network_str(self);
	char* n2 = loc_network_str(other);

	DEBUG(self->ctx, "Is %s equal to %s?\n", n1, n2);

	free(n1);
	free(n2);
#endif

	// Family must be the same
	if (self->family != other->family) {
		DEBUG(self->ctx, "  Family mismatch\n");

		return 0;
	}

	// The start address must be the same
	if (in6_addr_cmp(&self->first_address, &other->first_address) != 0) {
		DEBUG(self->ctx, "  Address mismatch\n");

		return 0;
	}

	// The prefix length must be the same
	if (self->prefix != other->prefix) {
		DEBUG(self->ctx, "  Prefix mismatch\n");
		return 0;
	}

	DEBUG(self->ctx, "  Yes!\n");

	return 1;
}

static int loc_network_gt(struct loc_network* self, struct loc_network* other) {
	// Families must match
	if (self->family != other->family)
		return -1;

	int r = in6_addr_cmp(&self->first_address, &other->first_address);

	switch (r) {
		// Smaller
		case -1:
			return 0;

		// Larger
		case 1:
			return 1;

		default:
			break;
	}

	if (self->prefix > other->prefix)
		return 1;

	// Dunno
	return 0;
}

LOC_EXPORT int loc_network_is_subnet_of(struct loc_network* self, struct loc_network* other) {
	// If the start address of the other network is smaller than this network,
	// it cannot be a subnet.
	if (in6_addr_cmp(&self->first_address, &other->first_address) < 0)
		return 0;

	// If the end address of the other network is greater than this network,
	// it cannot be a subnet.
	if (in6_addr_cmp(&self->last_address, &other->last_address) > 0)
		return 0;

	return 1;
}

LOC_EXPORT struct loc_network_list* loc_network_subnets(struct loc_network* network) {
	struct loc_network_list* list;

	// New prefix length
	unsigned int prefix = network->prefix + 1;

	// Check if the new prefix is valid
	if (valid_prefix(&network->first_address, prefix))
		return NULL;

	// Create a new list with the result
	int r = loc_network_list_new(network->ctx, &list);
	if (r) {
		ERROR(network->ctx, "Could not create network list: %d\n", r);
		return NULL;
	}

	struct loc_network* subnet1 = NULL;
	struct loc_network* subnet2 = NULL;

	// Create the first half of the network
	r = loc_network_new(network->ctx, &subnet1, &network->first_address, prefix);
	if (r)
		goto ERROR;

	// The next subnet starts after the first one
	struct in6_addr first_address = address_increment(&subnet1->last_address);

	// Create the second half of the network
	r = loc_network_new(network->ctx, &subnet2, &first_address, prefix);
	if (r)
		goto ERROR;

	// Push the both onto the stack (in reverse order)
	r = loc_network_list_push(list, subnet2);
	if (r)
		goto ERROR;

	r = loc_network_list_push(list, subnet1);
	if (r)
		goto ERROR;

	loc_network_unref(subnet1);
	loc_network_unref(subnet2);

	return list;

ERROR:
	if (subnet1)
		loc_network_unref(subnet1);

	if (subnet2)
		loc_network_unref(subnet2);

	if (list)
		loc_network_list_unref(list);

	return NULL;
}

LOC_EXPORT struct loc_network_list* loc_network_exclude(
		struct loc_network* self, struct loc_network* other) {
	struct loc_network_list* list;

#ifdef ENABLE_DEBUG
	char* n1 = loc_network_str(self);
	char* n2 = loc_network_str(other);

	DEBUG(self->ctx, "Returning %s excluding %s...\n", n1, n2);

	free(n1);
	free(n2);
#endif

	// Family must match
	if (self->family != other->family) {
		DEBUG(self->ctx, "Family mismatch\n");

		return NULL;
	}

	// Other must be a subnet of self
	if (!loc_network_is_subnet_of(other, self)) {
		DEBUG(self->ctx, "Network %p is not contained in network %p\n", other, self);

		return NULL;
	}

	// We cannot perform this operation if both networks equal
	if (loc_network_eq(self, other)) {
		DEBUG(self->ctx, "Networks %p and %p are equal\n", self, other);

		return NULL;
	}

	// Create a new list with the result
	int r = loc_network_list_new(self->ctx, &list);
	if (r) {
		ERROR(self->ctx, "Could not create network list: %d\n", r);
		return NULL;
	}

	struct loc_network_list* subnets = loc_network_subnets(self);

	struct loc_network* subnet1 = NULL;
	struct loc_network* subnet2 = NULL;

	while (subnets) {
		// Fetch both subnets
		subnet1 = loc_network_list_get(subnets, 0);
		subnet2 = loc_network_list_get(subnets, 1);

		// Free list
		loc_network_list_unref(subnets);
		subnets = NULL;

		if (loc_network_eq(other, subnet1)) {
			r = loc_network_list_push(list, subnet2);
			if (r)
				goto ERROR;

		} else if (loc_network_eq(other, subnet2)) {
			r = loc_network_list_push(list, subnet1);
			if (r)
				goto ERROR;

		} else  if (loc_network_is_subnet_of(other, subnet1)) {
			r = loc_network_list_push(list, subnet2);
			if (r)
				goto ERROR;

			subnets = loc_network_subnets(subnet1);

		} else if (loc_network_is_subnet_of(other, subnet2)) {
			r = loc_network_list_push(list, subnet1);
			if (r)
				goto ERROR;

			subnets = loc_network_subnets(subnet2);

		} else {
			ERROR(self->ctx, "We should never get here\n");
			goto ERROR;
		}

		loc_network_unref(subnet1);
		loc_network_unref(subnet2);
	}

#ifdef ENABLE_DEBUG
	loc_network_list_dump(list);
#endif

	// Return the result
	return list;

ERROR:
	if (subnet1)
		loc_network_unref(subnet1);

	if (subnet2)
		loc_network_unref(subnet2);

	if (list)
		loc_network_list_unref(list);

	return NULL;
}

LOC_EXPORT int loc_network_to_database_v1(struct loc_network* network, struct loc_database_network_v1* dbobj) {
	// Add country code
	loc_country_code_copy(dbobj->country_code, network->country_code);

	// Add ASN
	dbobj->asn = htobe32(network->asn);

	// Flags
	dbobj->flags = htobe16(network->flags);

	return 0;
}

LOC_EXPORT int loc_network_new_from_database_v1(struct loc_ctx* ctx, struct loc_network** network,
		struct in6_addr* address, unsigned int prefix, const struct loc_database_network_v1* dbobj) {
	char country_code[3] = "\0\0";

	int r = loc_network_new(ctx, network, address, prefix);
	if (r) {
		ERROR(ctx, "Could not allocate a new network: %s", strerror(-r));
		return r;
	}

	// Import country code
	loc_country_code_copy(country_code, dbobj->country_code);

	r = loc_network_set_country_code(*network, country_code);
	if (r) {
		ERROR(ctx, "Could not set country code: %s\n", country_code);
		return r;
	}

	// Import ASN
	uint32_t asn = be32toh(dbobj->asn);
	r = loc_network_set_asn(*network, asn);
	if (r) {
		ERROR(ctx, "Could not set ASN: %d\n", asn);
		return r;
	}

	// Import flags
	int flags = be16toh(dbobj->flags);
	r = loc_network_set_flag(*network, flags);
	if (r) {
		ERROR(ctx, "Could not set flags: %d\n", flags);
		return r;
	}

	return 0;
}

struct loc_network_tree {
	struct loc_ctx* ctx;
	int refcount;

	struct loc_network_tree_node* root;
};

struct loc_network_tree_node {
	struct loc_ctx* ctx;
	int refcount;

	struct loc_network_tree_node* zero;
	struct loc_network_tree_node* one;

	struct loc_network* network;
};

LOC_EXPORT int loc_network_tree_new(struct loc_ctx* ctx, struct loc_network_tree** tree) {
	struct loc_network_tree* t = calloc(1, sizeof(*t));
	if (!t)
		return -ENOMEM;

	t->ctx = loc_ref(ctx);
	t->refcount = 1;

	// Create the root node
	int r = loc_network_tree_node_new(ctx, &t->root);
	if (r) {
		loc_network_tree_unref(t);
		return r;
	}

	DEBUG(t->ctx, "Network tree allocated at %p\n", t);
	*tree = t;
	return 0;
}

LOC_EXPORT struct loc_network_tree_node* loc_network_tree_get_root(struct loc_network_tree* tree) {
	return loc_network_tree_node_ref(tree->root);
}

static struct loc_network_tree_node* loc_network_tree_get_node(struct loc_network_tree_node* node, int path) {
	struct loc_network_tree_node** n;

	if (path == 0)
		n = &node->zero;
	else
		n = &node->one;

	// If the desired node doesn't exist, yet, we will create it
	if (*n == NULL) {
		int r = loc_network_tree_node_new(node->ctx, n);
		if (r)
			return NULL;
	}

	return *n;
}

static struct loc_network_tree_node* loc_network_tree_get_path(struct loc_network_tree* tree, const struct in6_addr* address, unsigned int prefix) {
	struct loc_network_tree_node* node = tree->root;

	for (unsigned int i = 0; i < prefix; i++) {
		// Check if the ith bit is one or zero
		node = loc_network_tree_get_node(node, in6_addr_get_bit(address, i));
	}

	return node;
}

static int __loc_network_tree_walk(struct loc_ctx* ctx, struct loc_network_tree_node* node,
		int(*filter_callback)(struct loc_network* network, void* data),
		int(*callback)(struct loc_network* network, void* data), void* data) {
	int r;

	// Finding a network ends the walk here
	if (node->network) {
		if (filter_callback) {
			int f = filter_callback(node->network, data);
			if (f < 0)
				return f;

			// Skip network if filter function returns value greater than zero
			if (f > 0)
				return 0;
		}

		r = callback(node->network, data);
		if (r)
			return r;
	}

	// Walk down on the left side of the tree first
	if (node->zero) {
		r = __loc_network_tree_walk(ctx, node->zero, filter_callback, callback, data);
		if (r)
			return r;
	}

	// Then walk on the other side
	if (node->one) {
		r = __loc_network_tree_walk(ctx, node->one, filter_callback, callback, data);
		if (r)
			return r;
	}

	return 0;
}

LOC_EXPORT int loc_network_tree_walk(struct loc_network_tree* tree,
		int(*filter_callback)(struct loc_network* network, void* data),
		int(*callback)(struct loc_network* network, void* data), void* data) {
	return __loc_network_tree_walk(tree->ctx, tree->root, filter_callback, callback, data);
}

static void loc_network_tree_free(struct loc_network_tree* tree) {
	DEBUG(tree->ctx, "Releasing network tree at %p\n", tree);

	loc_network_tree_node_unref(tree->root);

	loc_unref(tree->ctx);
	free(tree);
}

LOC_EXPORT struct loc_network_tree* loc_network_tree_unref(struct loc_network_tree* tree) {
	if (--tree->refcount > 0)
		return tree;

	loc_network_tree_free(tree);
	return NULL;
}

static int __loc_network_tree_dump(struct loc_network* network, void* data) {
	DEBUG(network->ctx, "Dumping network at %p\n", network);

	char* s = loc_network_str(network);
	if (!s)
		return 1;

	INFO(network->ctx, "%s\n", s);
	free(s);

	return 0;
}

LOC_EXPORT int loc_network_tree_dump(struct loc_network_tree* tree) {
	DEBUG(tree->ctx, "Dumping network tree at %p\n", tree);

	return loc_network_tree_walk(tree, NULL, __loc_network_tree_dump, NULL);
}

LOC_EXPORT int loc_network_tree_add_network(struct loc_network_tree* tree, struct loc_network* network) {
	DEBUG(tree->ctx, "Adding network %p to tree %p\n", network, tree);

	struct loc_network_tree_node* node = loc_network_tree_get_path(tree,
			&network->first_address, network->prefix);
	if (!node) {
		ERROR(tree->ctx, "Could not find a node\n");
		return -ENOMEM;
	}

	// Check if node has not been set before
	if (node->network) {
		DEBUG(tree->ctx, "There is already a network at this path\n");
		return -EBUSY;
	}

	// Point node to the network
	node->network = loc_network_ref(network);

	return 0;
}

static int __loc_network_tree_count(struct loc_network* network, void* data) {
	size_t* counter = (size_t*)data;

	// Increase the counter for each network
	counter++;

	return 0;
}

LOC_EXPORT size_t loc_network_tree_count_networks(struct loc_network_tree* tree) {
	size_t counter = 0;

	int r = loc_network_tree_walk(tree, NULL, __loc_network_tree_count, &counter);
	if (r)
		return r;

	return counter;
}

static size_t __loc_network_tree_count_nodes(struct loc_network_tree_node* node) {
	size_t counter = 1;

	if (node->zero)
		counter += __loc_network_tree_count_nodes(node->zero);

	if (node->one)
		counter += __loc_network_tree_count_nodes(node->one);

	return counter;
}

LOC_EXPORT size_t loc_network_tree_count_nodes(struct loc_network_tree* tree) {
	return __loc_network_tree_count_nodes(tree->root);
}

LOC_EXPORT int loc_network_tree_node_new(struct loc_ctx* ctx, struct loc_network_tree_node** node) {
	struct loc_network_tree_node* n = calloc(1, sizeof(*n));
	if (!n)
		return -ENOMEM;

	n->ctx = loc_ref(ctx);
	n->refcount = 1;

	n->zero = n->one = NULL;

	DEBUG(n->ctx, "Network node allocated at %p\n", n);
	*node = n;
	return 0;
}

LOC_EXPORT struct loc_network_tree_node* loc_network_tree_node_ref(struct loc_network_tree_node* node) {
	if (node)
		node->refcount++;

	return node;
}

static void loc_network_tree_node_free(struct loc_network_tree_node* node) {
	DEBUG(node->ctx, "Releasing network node at %p\n", node);

	if (node->network)
		loc_network_unref(node->network);

	if (node->zero)
		loc_network_tree_node_unref(node->zero);

	if (node->one)
		loc_network_tree_node_unref(node->one);

	loc_unref(node->ctx);
	free(node);
}

LOC_EXPORT struct loc_network_tree_node* loc_network_tree_node_unref(struct loc_network_tree_node* node) {
	if (!node)
		return NULL;

	if (--node->refcount > 0)
		return node;

	loc_network_tree_node_free(node);
	return NULL;
}

LOC_EXPORT struct loc_network_tree_node* loc_network_tree_node_get(struct loc_network_tree_node* node, unsigned int index) {
	if (index == 0)
		node = node->zero;
	else
		node = node->one;

	if (!node)
		return NULL;

	return loc_network_tree_node_ref(node);
}

LOC_EXPORT int loc_network_tree_node_is_leaf(struct loc_network_tree_node* node) {
	return (!!node->network);
}

LOC_EXPORT struct loc_network* loc_network_tree_node_get_network(struct loc_network_tree_node* node) {
	return loc_network_ref(node->network);
}

// List

struct loc_network_list {
	struct loc_ctx* ctx;
	int refcount;

	struct loc_network* list[1024];
	size_t size;
	size_t max_size;
};

LOC_EXPORT int loc_network_list_new(struct loc_ctx* ctx,
		struct loc_network_list** list) {
	struct loc_network_list* l = calloc(1, sizeof(*l));
	if (!l)
		return -ENOMEM;

	l->ctx = loc_ref(ctx);
	l->refcount = 1;

	// Do not allow this list to grow larger than this
	l->max_size = 1024;

	DEBUG(l->ctx, "Network list allocated at %p\n", l);
	*list = l;
	return 0;
}

LOC_EXPORT struct loc_network_list* loc_network_list_ref(struct loc_network_list* list) {
	list->refcount++;

	return list;
}

static void loc_network_list_free(struct loc_network_list* list) {
	DEBUG(list->ctx, "Releasing network list at %p\n", list);

	for (unsigned int i = 0; i < list->size; i++)
		loc_network_unref(list->list[i]);

	loc_unref(list->ctx);
	free(list);
}

LOC_EXPORT struct loc_network_list* loc_network_list_unref(struct loc_network_list* list) {
	if (!list)
		return NULL;

	if (--list->refcount > 0)
		return list;

	loc_network_list_free(list);
	return NULL;
}

LOC_EXPORT size_t loc_network_list_size(struct loc_network_list* list) {
	return list->size;
}

LOC_EXPORT int loc_network_list_empty(struct loc_network_list* list) {
	return list->size == 0;
}

LOC_EXPORT void loc_network_list_clear(struct loc_network_list* list) {
	for (unsigned int i = 0; i < list->size; i++)
		loc_network_unref(list->list[i]);

	list->size = 0;
}

LOC_EXPORT void loc_network_list_dump(struct loc_network_list* list) {
	struct loc_network* network;
	char* s;

	for (unsigned int i = 0; i < list->size; i++) {
		network = list->list[i];

		s = loc_network_str(network);

		INFO(list->ctx, "%s\n", s);
		free(s);
	}
}

LOC_EXPORT struct loc_network* loc_network_list_get(struct loc_network_list* list, size_t index) {
	// Check index
	if (index >= list->size)
		return NULL;

	return loc_network_ref(list->list[index]);
}

LOC_EXPORT int loc_network_list_push(struct loc_network_list* list, struct loc_network* network) {
	// Check if we have space left
	if (list->size == list->max_size)
		return -ENOMEM;

	list->list[list->size++] = loc_network_ref(network);

	return 0;
}

LOC_EXPORT struct loc_network* loc_network_list_pop(struct loc_network_list* list) {
	// Return nothing when empty
	if (loc_network_list_empty(list))
		return NULL;

	return list->list[--list->size];
}

static void loc_network_list_swap(struct loc_network_list* list, unsigned int i1, unsigned int i2) {
	// Do nothing for invalid indices
	if (i1 >= list->size || i2 >= list->size)
		return;

	DEBUG(list->ctx, "Swapping %u with %u\n", i1, i2);

	struct loc_network* network1 = list->list[i1];
	struct loc_network* network2 = list->list[i2];

	list->list[i1] = network2;
	list->list[i2] = network1;
}

LOC_EXPORT void loc_network_list_reverse(struct loc_network_list* list) {
	unsigned int i = 0;
	unsigned int j = list->size - 1;

	while (i < j) {
		loc_network_list_swap(list, i++, j--);
	}
}

LOC_EXPORT void loc_network_list_sort(struct loc_network_list* list) {
	unsigned int n = list->size;
	int swapped;

	do {
		swapped = 0;

		for (unsigned int i = 1; i < n; i++) {
			if (loc_network_gt(list->list[i-1], list->list[i]) > 0) {
				loc_network_list_swap(list, i-1, i);
				swapped = 1;
			}
		}

		n--;
	} while (swapped);
}
