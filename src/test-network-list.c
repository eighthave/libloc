/*
	libloc - A library to determine the location of someone on the Internet

	Copyright (C) 2017 IPFire Development Team <info@ipfire.org>

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.
*/

#include <errno.h>
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

#include <loc/libloc.h>
#include <loc/network.h>
#include <loc/network-list.h>

int main(int argc, char** argv) {
	int err;

	struct loc_ctx* ctx;
	err = loc_new(&ctx);
	if (err < 0)
		exit(EXIT_FAILURE);

	// Enable debug logging
	loc_set_log_priority(ctx, LOG_DEBUG);

	// Create a network
	struct loc_network* network1;
	err = loc_network_new_from_string(ctx, &network1, "2001:db8::/32");
	if (err) {
		fprintf(stderr, "Could not create the network1\n");
		exit(EXIT_FAILURE);
	}

	struct loc_network* subnet1;
	err = loc_network_new_from_string(ctx, &subnet1, "2001:db8:a::/48");
	if (err) {
		fprintf(stderr, "Could not create the subnet1\n");
		exit(EXIT_FAILURE);
	}

	struct loc_network* subnet2;
	err = loc_network_new_from_string(ctx, &subnet2, "2001:db8:b::/48");
	if (err) {
		fprintf(stderr, "Could not create the subnet2\n");
		exit(EXIT_FAILURE);
	}

	struct loc_network* subnet3;
	err = loc_network_new_from_string(ctx, &subnet3, "2001:db8:c::/48");
	if (err) {
		fprintf(stderr, "Could not create the subnet3\n");
		exit(EXIT_FAILURE);
	}

	struct loc_network* subnet4;
	err = loc_network_new_from_string(ctx, &subnet4, "2001:db8:d::/48");
	if (err) {
		fprintf(stderr, "Could not create the subnet4\n");
		exit(EXIT_FAILURE);
	}

	// Make a list with both subnets
	struct loc_network_list* subnets;
	err = loc_network_list_new(ctx, &subnets);
	if (err) {
		fprintf(stderr, "Could not create subnets list\n");
		exit(EXIT_FAILURE);
	}

	size_t size = loc_network_list_size(subnets);
	if (size > 0) {
		fprintf(stderr, "The list is not empty: %zu\n", size);
		exit(EXIT_FAILURE);
	}

	err = loc_network_list_push(subnets, subnet1);
	if (err) {
		fprintf(stderr, "Could not add subnet1 to subnets list\n");
		exit(EXIT_FAILURE);
	}

	if (loc_network_list_empty(subnets)) {
		fprintf(stderr, "The subnets list reports that it is empty\n");
		exit(EXIT_FAILURE);
	}

	err = loc_network_list_push(subnets, subnet2);
	if (err) {
		fprintf(stderr, "Could not add subnet2 to subnets list\n");
		exit(EXIT_FAILURE);
	}

	// Add the fourth one next
	err = loc_network_list_push(subnets, subnet4);
	if (err) {
		fprintf(stderr, "Could not add subnet4 to subnets list\n");
		exit(EXIT_FAILURE);
	}

	// Add the third one
	err = loc_network_list_push(subnets, subnet3);
	if (err) {
		fprintf(stderr, "Could not add subnet3 to subnets list\n");
		exit(EXIT_FAILURE);
	}

	loc_network_list_dump(subnets);

	size = loc_network_list_size(subnets);
	if (size != 4) {
		fprintf(stderr, "Network list is reporting an incorrect size: %zu\n", size);
		exit(EXIT_FAILURE);
	}

	// Exclude subnet1 from network1
	struct loc_network_list* excluded = loc_network_exclude(network1, subnet1);
	if (!excluded) {
		fprintf(stderr, "Received an empty result from loc_network_exclude() for subnet1\n");
		exit(EXIT_FAILURE);
	}

	loc_network_list_dump(excluded);

	// Exclude all subnets from network1
	excluded = loc_network_exclude_list(network1, subnets);
	if (!excluded) {
		fprintf(stderr, "Received an empty result from loc_network_exclude() for subnets\n");
		exit(EXIT_FAILURE);
	}

	loc_network_list_dump(excluded);

	if (excluded)
		loc_network_list_unref(excluded);

	loc_network_list_unref(subnets);
	loc_network_unref(network1);
	loc_network_unref(subnet1);
	loc_network_unref(subnet2);
	loc_unref(ctx);

	return EXIT_SUCCESS;
}
