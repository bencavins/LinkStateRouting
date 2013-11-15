/*
 * routed_LS.c
 *
 *  Created on: 2013-11-02
 *      Author: Ben Cavins 
 */

#include <sys/socket.h>
#include <sys/types.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>
#include <limits.h>
#include "vector.h"
#include "hashmap.h"

#define USAGE "<router ID> <log file name> <initialization file>"
#define ARG_MIN 3
#define MAX_ID_LEN 24
#define MAX_PORT_LEN 16
#define MAX_LSP_ENTRIES 64
#define TTL 6
#define FLAG_KILL 1

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

void init_router(FILE* fp, char *router_id, vector_p neighbors, vector_p table) {

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
				vector_add(table, &entry, sizeof(entry));
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
		} else {
			// Set socket as non-blocking
			fcntl(sock, F_SETFL, O_NONBLOCK);
		}

		// Create mapping
		hashmap_put(map, entry->dest_id, &sock, sizeof(int));
	}

	// Accept listening sockets
	for (i = 0; i < listening->length; ++i) {
		struct tuple *item = vector_get(listening, i);
		int new_sock = accept(item->sock, NULL, NULL);
		fcntl(new_sock, F_SETFL, O_NONBLOCK);
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

table_entry_t* table_get_by_id(vector_p table, char *id) {
	unsigned int i;
	for (i = 0; i < table->length; ++i) {
		table_entry_t *entry = vector_get(table, i);
		if (strncmp(entry->dest_id, id, MAX_ID_LEN) == 0) {
			return entry;
		}
	}
	return NULL;
}

void update_routing_table(vector_p table, lsp_packet_t *packet, char *id) {
	int i;
	int index;
	int c;
	int out_port;
	int dest_port;
	table_entry_t *entry;
	table_entry_t new_entry;
	if (!table_contains(table, packet->header.src_id)) {
		return;
	}
	entry = table_get_by_id(table, packet->header.src_id);
	c = entry->cost;
	out_port = entry->out_port;
	dest_port = entry->dest_port;
	for (i = 0; i < packet->header.entries; ++i) {
		new_entry.cost = packet->data[i].cost + c;
		strncpy(new_entry.dest_id, packet->data[i].id, MAX_ID_LEN);
		new_entry.out_port = out_port;
		new_entry.dest_port = dest_port;
		if (strncmp(new_entry.dest_id, id, MAX_ID_LEN) != 0) {
			if (!table_contains(table, packet->data[i].id)) {
				vector_add(table, &new_entry, sizeof(table_entry_t));
			} else {
				entry = table_get_by_id(table, packet->data[i].id);

				// Replace if cost is less
				if (new_entry.cost < entry->cost) {
					index = vector_index(table, entry, sizeof(table_entry_t));
					vector_set(table, index, &new_entry, sizeof(table_entry_t));

				// Replace if cost is same and ID is less
				} else if (new_entry.cost == entry->cost) {
					if (new_entry.out_port < entry->out_port) {
						index = vector_index(table, entry, sizeof(table_entry_t));
						vector_set(table, index, &new_entry, sizeof(table_entry_t));
					}
					// TODO check ids: A<B<C<D<E<F
				}
			}
		}
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

void log_table(FILE *fp, vector_p table) {
	unsigned int i;
	fprintf(fp, "TIME = %ld\n", time(NULL));
	fprintf(fp, " ID | COST | OUT PORT | DEST PORT \n");
	fprintf(fp, "----------------------------------\n");
	for (i = 0; i < table->length; ++i) {
		table_entry_t *entry = vector_get(table, i);
		fprintf(fp, "  %s | %4d | %8d | %9d \n", entry->dest_id, entry->cost, entry->out_port, entry->dest_port);
	}
	fprintf(fp, "\n");
	fflush(fp);
}

void sendall(vector_p neighbors, hashmap_p socks, lsp_packet_t *packet, char *ignore_id) {
	unsigned int i;
	for (i = 0; i < neighbors->length; ++i) {
		table_entry_t *entry = vector_get(neighbors, i);
		if (ignore_id == NULL || strncmp(entry->dest_id, ignore_id, MAX_ID_LEN) != 0) {
			int *sock = hashmap_get(socks, entry->dest_id);
			if (send(*sock, packet, sizeof(lsp_packet_t), 0) < 0) {
				perror("send");
			}
		}
	}
}

int main(int argc, char *argv[]) {

	char *router_id;
	char *log_filename;
	char *init_filename;
	FILE *logfp;
	FILE *initfp;
	vector_p neighbors;
	vector_p routing_table;
	hashmap_p recvd_packets;
	hashmap_p socks;  // Maps router IDs to socket FDs
	int sequence_num = 0;
	time_t old_time;
	time_t new_time;
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
	routing_table = create_vector();
	recvd_packets = create_hashmap();
	socks = create_hashmap();

	init_router(initfp, router_id, neighbors, routing_table);
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
	packet.header = build_header(sequence_num, router_id, 0, len, entries, TTL);
	++sequence_num;

	log_table(logfp, routing_table);

	old_time = new_time = time(NULL);

	int done = 0;

	while (!done) {

		new_time = time(NULL);

		if (new_time >= old_time + 5) {
			old_time = new_time;
			printf("%s: sending...\n", router_id);
			sequence_num++;
			packet.header.seq_num = sequence_num;
			sendall(neighbors, socks, &packet, NULL);
		}

		for (i = 0; i < neighbors->length; ++i) {
			table_entry_t *entry = vector_get(neighbors, i);
			int *sock = hashmap_get(socks, entry->dest_id);
			lsp_packet_t new_packet;
			memset(&new_packet, '\0', sizeof(new_packet));
			int retval = recv(*sock, &new_packet, sizeof(new_packet), O_NONBLOCK);
			if (retval < 0) {
				if (errno != EAGAIN && errno != EWOULDBLOCK) {
					perror("recv");
				}
			} else if (retval > 0) {
				int *entry = hashmap_get(recvd_packets, new_packet.header.src_id);
				if (entry == NULL || *entry < new_packet.header.seq_num) {
					fprintf(logfp, "%s: received from %s\n", router_id, new_packet.header.src_id);

					if (new_packet.header.flags & FLAG_KILL) {  // Kill packet
						fprintf(logfp, "%s: kill packet received\n", router_id);
						log_lsp(logfp, &new_packet);
						char from[MAX_ID_LEN];
						strncpy(from, new_packet.header.src_id, MAX_ID_LEN);
						strncpy(new_packet.header.src_id, router_id, MAX_ID_LEN);
						new_packet.header.ttl--;
						if (new_packet.header.ttl > 0) {
							sendall(neighbors, socks, &new_packet, from);
						}
						done = 1;

					} else {  // Regular packet
						hashmap_put(recvd_packets, new_packet.header.src_id, &(new_packet.header.seq_num), sizeof(int));
						update_routing_table(routing_table, &new_packet, router_id);
						log_lsp(logfp, &new_packet);
						log_table(logfp, routing_table);
						new_packet.header.ttl--;
						if (new_packet.header.ttl > 0) {
							fprintf(logfp, "forwarding...\n");
							sendall(neighbors, socks, &new_packet, new_packet.header.src_id);
						}
					}
				}
			}
		}

		fd_set fdset;
		struct timeval tv;
		int retval;
		FD_ZERO(&fdset);
		FD_SET(fileno(stdin), &fdset);
		tv.tv_sec = 0;
		tv.tv_usec = 1;
		retval = select(1, &fdset, NULL, NULL, &tv);
		if (retval < 0) {
			perror("select");
		} else if (retval > 0) {
			if (FD_ISSET(fileno(stdin), &fdset)) {
				char cmd[32];
				if (read(fileno(stdin), cmd, sizeof(cmd)) < 0) {
					perror("read");
				}
				if (strncmp(cmd, "exit", 4) == 0) {
					lsp_packet_t kill_packet;
					kill_packet.header = build_header(INT_MAX, router_id, FLAG_KILL, 0, 0, TTL);
					sendall(neighbors, socks, &kill_packet, NULL);
					printf("%s: exiting...\n", router_id);
					done = 1;
				}
			}
		}
	}

	// Destroy data structures
	destroy_vector(neighbors);
	destroy_vector(routing_table);
	destroy_hashmap(recvd_packets);
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
