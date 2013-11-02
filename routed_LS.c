/*
 * routed_LS.c
 *
 *  Created on: 2013-11-02
 *      Author: Ben Cavins
 */

#include "routed_LS.h"

int main(int argc, char *argv[]) {

	char *router_id;
	char *log_filename;
	char *init_filename;

	// Check arguments
	if (argc < ARG_MIN) {
		fprintf(stderr, "Usage: %s %s\n", argv[0], USAGE);
		return EXIT_FAILURE;
	}

	// Extract arguments
	router_id = argv[1];
	log_filename = argv[2];
	init_filename = argv[3];

	// Print arguments
	printf("Router ID = %s\n", router_id);
	printf("Log File = %s\n", log_filename);
	printf("Initialization File = %s\n", init_filename);

	return EXIT_SUCCESS;
}
