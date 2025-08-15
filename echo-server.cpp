#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <sys/socket.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <thread>
#include <mutex>
#include <vector>
#include <algorithm>
#include <csignal>

void myerror(const char* error) { perror(error); }

void usage() {
	printf("echo-server:\n");
	printf("syntax : echo-server <port> [-e[-b]]\n");
	printf("sample : echo-server 1234 -e -b\n");
}

std::vector<int> client_sockets;
std::mutex client_mutex;

struct Param {
	bool echo{false};
	bool broadcast{false};
	uint16_t port{0};
	uint32_t srcIp{0};

	struct KeepAlive {
		int idle_{0};
		int interval_{1};
		int count_{10};
	} keepAlive_;

	bool parse(int argc, char* argv[]) {	
		for (int i = 1; i < argc;) {
			if (strcmp(argv[i], "-e") == 0) { echo = true; i++; continue; }
			if (strcmp(argv[i], "-b") == 0) { broadcast = true; i++; continue; }
			if (i < argc) port = atoi(argv[i++]);
		}
		return port != 0;
	}
} param;

void remove_client(int sd) {
    std::lock_guard<std::mutex> lock(client_mutex);
    auto it = std::find(client_sockets.begin(), client_sockets.end(), sd);
    if (it != client_sockets.end()) client_sockets.erase(it);
}

void broadcast(int from_sd, const char* buf, size_t len) {
    std::lock_guard<std::mutex> lock(client_mutex);
    for (int client_sd : client_sockets) {
        ssize_t s = ::send(client_sd, buf, len, 0);
    }
}

void recvThread(int sd) {
	printf("connected\n");
	fflush(stdout);

	static const int BUFSIZE = 65536;
	char buf[BUFSIZE];

	while (true) {
		ssize_t res = ::recv(sd, buf, BUFSIZE - 1, 0);
		if (res == 0 || res == -1) {
			fprintf(stderr, "recv return %zd\n", res);
			myerror(" ");
			break;
		}

		buf[res] = '\0';
		printf("%s", buf);
		fflush(stdout);

		if (param.echo && !param.broadcast) {
			res = ::send(sd, buf, res, 0);
			if (res == 0 || res == -1) {
				fprintf(stderr, "send return %zd\n", res);
				myerror(" ");
				break;
			}
		}
		else if (!param.echo && param.broadcast) {
			broadcast(sd, buf, static_cast<size_t>(res));
		}
		else if (param.echo && param.broadcast) {
			broadcast(sd, buf, static_cast<size_t>(res));
		}
	}

	printf("disconnected\n");
	fflush(stdout);
	remove_client(sd);
	::close(sd);
}

int main(int argc, char* argv[]) {
	std::signal(SIGPIPE, SIG_IGN);
	
	if (!param.parse(argc, argv)) {
		usage();
		return -1;
	}

	// socket
	int sd = ::socket(AF_INET, SOCK_STREAM, 0);
	if (sd == -1) {
		myerror("socket");
		return -1;
	}

	// setsockopt
	{
		int optval = 1;
		int res = ::setsockopt(sd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(int));
		if (res == -1) {
			myerror("setsockopt");
			return -1;
		}
	}

	// bind
	{
		struct sockaddr_in addr;
		memset(&addr, 0, sizeof(addr));
		addr.sin_family = AF_INET;
		addr.sin_addr.s_addr = param.srcIp;
		addr.sin_port = htons(param.port);

		ssize_t res = ::bind(sd, (struct sockaddr *)&addr, sizeof(addr));
		if (res == -1) {
			myerror("bind");
			return -1;
		}
	}

	// listen
	{
		int res = listen(sd, 5);
		if (res == -1) {
			myerror("listen");
			return -1;
		}
	}

	while (true) {
		struct sockaddr_in addr;
		socklen_t len = sizeof(addr);
		int newsd = ::accept(sd, (struct sockaddr *)&addr, &len);
		if (newsd == -1) {
			myerror("accept");
			break;
		}

		{
			std::lock_guard<std::mutex> lock(client_mutex);
        	client_sockets.push_back(newsd);
		}

		// keepalive
		if (param.keepAlive_.idle_ != 0) {
			int optval = 1;
			if (setsockopt(newsd, SOL_SOCKET, SO_KEEPALIVE, (const char*)&optval, sizeof(int)) < 0) {
				myerror("setsockopt(SO_KEEPALIVE)");
				return -1;
			}

			if (setsockopt(newsd, IPPROTO_TCP, TCP_KEEPIDLE, (const char*)&param.keepAlive_.idle_, sizeof(int)) < 0) {
				myerror("setsockopt(TCP_KEEPIDLE)");
				return -1;
			}

			if (setsockopt(newsd, IPPROTO_TCP, TCP_KEEPINTVL, (const char*)&param.keepAlive_.interval_, sizeof(int)) < 0) {
				myerror("setsockopt(TCP_KEEPINTVL)");
				return -1;
			}

			if (setsockopt(newsd, IPPROTO_TCP, TCP_KEEPCNT, (const char*)&param.keepAlive_.count_, sizeof(int)) < 0) {
				myerror("setsockopt(TCP_KEEPCNT)");
				return -1;
			}
		}

		std::thread(recvThread, newsd).detach();
	}
	::close(sd);
}