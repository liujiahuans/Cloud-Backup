#include "cloud_client.h"
#include<iostream>

#define SERVER_IP "192.168.111.139"
#define SERVER_PORT 22

int main() {
	CloudClient client(LISTEN_DIR, STORE_FILE, SERVER_IP, SERVER_PORT);
	client.Start();
	return 0;
}