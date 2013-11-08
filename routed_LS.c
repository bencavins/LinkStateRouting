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

#define USAGE "<router ID> <log file name> <initialization file>"
#define ARG_MIN 3
#define MAX_ID_LEN 24
#define MAX_PORT_LEN 16


void init_router(FILE* fp, char *router_id) {

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
			while ((str = strtok(NULL, " ,<>\n")) != NULL) {
				printf("%s\n", str);
			}
		}
	}

	// Free memory
	free(line);
}


int main(int argc, char *argv[]) {

	char *router_id;
	char *log_filename;
	char *init_filename;
	FILE *logfp;
	FILE *initfp;
	struct sockaddr_in local_addr;
	struct sockaddr_in remote_addr;
	int sock;


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

	// Is this A or B?
	if (strcmp("A", router_id) == 0) {

		// Populate local_addr with address data
		memset(&local_addr, '\0', sizeof(local_addr));
		local_addr.sin_family = AF_INET;
		local_addr.sin_port = htons(9601);
		local_addr.sin_addr.s_addr = INADDR_ANY;

		// Populate remote_addr with address data
		memset(&remote_addr, '\0', sizeof(remote_addr));
		remote_addr.sin_family = AF_INET;
		remote_addr.sin_port = htons(9604);
		remote_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

	} else {

		// Populate local_addr with address data
		memset(&local_addr, '\0', sizeof(local_addr));
		local_addr.sin_family = AF_INET;
		local_addr.sin_port = htons(9604);
		local_addr.sin_addr.s_addr = INADDR_ANY;

		// Populate remote_addr with address data
		memset(&remote_addr, '\0', sizeof(remote_addr));
		remote_addr.sin_family = AF_INET;
		remote_addr.sin_port = htons(9601);
		remote_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
	}

	// Create socket
	if ((sock = socket(PF_INET, SOCK_STREAM, 0)) < 0) {
		perror("socket");
		return EXIT_FAILURE;
	}

	// Bind socket to port
	if (bind(sock, (struct sockaddr *) &local_addr, sizeof(local_addr)) < 0) {
		perror("bind");
		return EXIT_FAILURE;
	}

	// Try to connect
	if (connect(sock, (struct sockaddr *) &remote_addr, sizeof(remote_addr)) < 0) {

		// If connect fails, listen instead
		if (listen(sock, 10) != 0) {
			perror("listen");
			return EXIT_FAILURE;
		}

		socklen_t addr_size = sizeof(remote_addr);
		int new_sock = accept(sock, (struct sockaddr *) &remote_addr, &addr_size);
		sock = new_sock;
	}

	// Create a message
	char msg[32];
	snprintf(msg, 32, "Hello from %s!", router_id);

	// Send the message
	if (send(sock, msg, 32, 0) < 0) {
		perror("send");
		return EXIT_FAILURE;
	}

	// Receive a message
	char from_msg[32];
	if (recv(sock, from_msg, 32, 0) < 0) {
		perror("recv");
		return EXIT_FAILURE;
	}

	// Print the received message
	printf("%s: %s\n", router_id, from_msg);

	//init_router(initfp, router_id);

	// Close socket
	if (close(sock) < 0) {
		perror("close");
		return EXIT_FAILURE;
	}

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
