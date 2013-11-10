/*
 * routed_LS.c
 *
 *  Created on: 2013-11-02
 *      Author: Ben Cavins 
 */

#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "vector.h"
#include "hashmap.h"

#define USAGE "<router ID> <log file name> <initialization file>"
#define ARG_MIN 3
#define MAX_ID_LEN 24
#define MAX_PORT_LEN 16

typedef struct {
	char dest_id[MAX_ID_LEN];
	unsigned int cost;
	unsigned int out_port;
	unsigned int dest_port;
} table_entry_t;


void init_router(FILE* fp, char *router_id, vector_p neighbors) {

	char *line = NULL;  // Current line
	size_t len = 0;     // Buffer length
	ssize_t read;       // Bytes read
	char *str;          // Current string

	// Read line in file
	while ((read = getline(&line, &len, fp)) != -1) {

		// Parse for router ID
		str = strtok(line, " ,<>\n");

		// Only parse line fully if direct neighbor of router ID
		if (strncmp(str, router_id, MAX_ID_LEN) == 0) {
			char *port1 = strtok(NULL, " ,<>\n");
			char *node  = strtok(NULL, " ,<>\n");
			char *port2 = strtok(NULL, " ,<>\n");
			char *cost  = strtok(NULL, " ,<>\n");

			if (port1 != NULL && node != NULL && port2 != NULL && cost != NULL) {
				table_entry_t entry;
				strncpy(entry.dest_id, node, MAX_ID_LEN);
				entry.out_port = atoi(port1);
				entry.dest_port = atoi(port2);
				entry.cost = atoi(cost);
				vector_add(neighbors, &entry, sizeof(entry));
			}
		}
	}

	// Free memory
	free(line);
}

void create_sockets_map(hashmap_p map, vector_p neighbors) {
	struct sockaddr_in local_addr;
	struct sockaddr_in remote_addr;
	vector_p listening;
	unsigned int i;
	int sock;

	struct tuple {
		int sock;
		char id[MAX_ID_LEN];
	};

	listening = create_vector();

	// Create sockets for neighboring nodes
	for (i = 0; i < neighbors->length; ++i) {
		table_entry_t *entry = vector_get(neighbors, i);

		memset(&local_addr, '\0', sizeof(local_addr));
		local_addr.sin_family = AF_INET;
		local_addr.sin_port = htons(entry->out_port);
		local_addr.sin_addr.s_addr = INADDR_ANY;

		memset(&remote_addr, '\0', sizeof(remote_addr));
		remote_addr.sin_family = AF_INET;
		remote_addr.sin_port = htons(entry->dest_port);
		remote_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

		// Create socket
		if ((sock = socket(PF_INET, SOCK_STREAM, 0)) < 0) {
			perror("socket");
			exit(EXIT_FAILURE);
		}

		// Bind socket to port
		if (bind(sock, (struct sockaddr *) &local_addr, sizeof(local_addr)) < 0) {
			perror("bind");
			exit(EXIT_FAILURE);
		}

		// Try to connect
		if (connect(sock, (struct sockaddr *) &remote_addr, sizeof(remote_addr)) < 0) {

			// If connect fails, listen instead
			if (listen(sock, 10) != 0) {
				perror("listen");
				exit(EXIT_FAILURE);
			}

			struct tuple pair;
			pair.sock = sock;
			strncpy(pair.id, entry->dest_id, MAX_ID_LEN);

			// This this socket as listening
			vector_add(listening, &pair, sizeof(struct tuple));
		}

		// Create mapping
		hashmap_put(map, entry->dest_id, &sock, sizeof(int));
	}

	// Accept listening sockets
	for (i = 0; i < listening->length; ++i) {
		struct tuple *item = vector_get(listening, i);
		int new_sock = accept(item->sock, NULL, NULL);
		hashmap_put(map, item->id, &new_sock, sizeof(int));
	}

	destroy_vector(listening);
}


int main(int argc, char *argv[]) {

	char *router_id;
	char *log_filename;
	char *init_filename;
	FILE *logfp;
	FILE *initfp;
	vector_p neighbors;
	hashmap_p socks;  // Maps router IDs to socket FDs

	// Check arguments
	if (argc < ARG_MIN) {
		fprintf(stderr, "Usage: %s %s\n", argv[0], USAGE);
		return EXIT_FAILURE;
	}

	// Extract arguments
	router_id = argv[1];
	log_filename = argv[2];
	init_filename = argv[3];

	// Open initialization file
	if ((initfp = fopen(init_filename, "r")) == NULL) {
		fprintf(stderr, "Error opening file: %s\n", init_filename);
		perror("fopen");
		return EXIT_FAILURE;
	}

	// Open log file
	if ((logfp = fopen(log_filename, "w+")) == NULL) {
		fprintf(stderr, "Error opening file: %s\n", log_filename);
		perror("fopen");
		return EXIT_FAILURE;
	}

	// Initialize data structures
	neighbors = create_vector();
	socks = create_hashmap();

	init_router(initfp, router_id, neighbors);

	unsigned int i;
	for (i = 0; i < neighbors->length; ++i) {
		table_entry_t *entry = vector_get(neighbors, i);
		printf("node %s: out port = %d, dest port = %d, cost = %d\n", entry->dest_id, entry->out_port, entry->dest_port, entry->cost);
	}

	create_sockets_map(socks, neighbors);

	//printf("map size = %d\n", (int) socks->num_buckets);
	for (i = 0; i < neighbors->length; ++i) {
		table_entry_t *entry = vector_get(neighbors, i);
		int *val = hashmap_get(socks, entry->dest_id);
		printf("%s: %s=%d\n", router_id, entry->dest_id, *val);
	}

	// Destroy data structures
	destroy_vector(neighbors);
	destroy_hashmap(socks);

	// Close initialization file
	if (fclose(initfp) != 0) {
		fprintf(stderr, "Error closing file %s\n", init_filename);
		perror("fclose");
		return EXIT_FAILURE;
	}

	// Close log file
	if (fclose(logfp) != 0) {
		fprintf(stderr, "Error closing file %s\n", log_filename);
		perror("fclose");
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
