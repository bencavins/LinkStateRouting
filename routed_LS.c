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
#define MAX_LSP_ENTRIES 64

typedef struct {
	char dest_id[MAX_ID_LEN];
	unsigned int cost;
	unsigned int out_port;
	unsigned int dest_port;
} table_entry_t;

typedef struct {
	int seq_num;
	char src_id[MAX_ID_LEN];
	int flags;
	int length;
	int entries;
	int ttl;
} lsp_header_t;

typedef struct {
	char id[MAX_ID_LEN];
	int cost;
} lsp_entry_t;

typedef struct {
	lsp_header_t header;
	lsp_entry_t data[MAX_LSP_ENTRIES];
} lsp_packet_t;

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

void build_socks_map(hashmap_p map, vector_p neighbors) {
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

lsp_header_t build_header(int seq_num, char *src_id, int flags, int length, int entries, int ttl) {
	lsp_header_t header;
	header.seq_num = seq_num;
	strncpy(header.src_id, src_id, MAX_ID_LEN);
	header.flags = flags;
	header.length = length;
	header.entries = entries;
	header.ttl = ttl;
	return header;
}

/* Returns 1 if table contains id, 0 otherwise */
int table_contains(vector_p table, char *id) {
	unsigned int i;
	for (i = 0; i < table->length; ++i) {
		table_entry_t *entry = vector_get(table, i);
		if (strncmp(id, entry->dest_id, MAX_ID_LEN) == 0) {
			return 1;
		}
	}
	return 0;
}

void update_routing_table(vector_p table, lsp_packet_t *packet) {
	unsigned int i;
	if (!table_contains(table, packet->header.src_id)) {
		return;
	}
}

void log_lsp(FILE *fp, lsp_packet_t *packet) {
	int i;
	fprintf(fp, "Source: %s\n", packet->header.src_id);
	fprintf(fp, "ID | COST\n");
	fprintf(fp, "---------\n");
	for (i = 0; i < packet->header.entries; ++i) {
		lsp_entry_t entry = packet->data[i];
		fprintf(fp, "%s  | %4d\n", entry.id, entry.cost);
	}
	fprintf(fp, "\n");
	fflush(fp);
}

int main(int argc, char *argv[]) {

	char *router_id;
	char *log_filename;
	char *init_filename;
	FILE *logfp;
	FILE *initfp;
	vector_p neighbors;
	hashmap_p socks;  // Maps router IDs to socket FDs
	int sequence_num = 0;
	unsigned int i;

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

	build_socks_map(socks, neighbors);

	// Create LSP
	lsp_packet_t packet;
	int entries = 0;

	for (i = 0; i < neighbors->length; ++i) {
		table_entry_t *entry = vector_get(neighbors, i);

		lsp_entry_t lsp_entry;
		strncpy(lsp_entry.id, entry->dest_id, MAX_ID_LEN);
		lsp_entry.cost = entry->cost;

		packet.data[i] = lsp_entry;

		++entries;
	}

	int len = sizeof(lsp_header_t) + (sizeof(lsp_entry_t) * entries);
	packet.header = build_header(sequence_num, router_id, 0, len, entries, 1);

	log_lsp(logfp, &packet);

	for (i = 0; i < neighbors->length; ++i) {
		table_entry_t *entry = vector_get(neighbors, i);
		int *sock = hashmap_get(socks, entry->dest_id);
		if (send(*sock, &packet, sizeof(packet), 0) < 0) {
			perror("send");
		}
	}

	for (i = 0; i < neighbors->length; ++i) {
		table_entry_t *entry = vector_get(neighbors, i);
		int *sock = hashmap_get(socks, entry->dest_id);
		lsp_packet_t new_packet;
		memset(&new_packet, '\0', sizeof(new_packet));
		if (recv(*sock, &new_packet, sizeof(new_packet), 0) < 0) {
			perror("recv");
		} else {
			printf("%s: received from %s\n", router_id, new_packet.header.src_id);
		}
		log_lsp(logfp, &new_packet);
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
