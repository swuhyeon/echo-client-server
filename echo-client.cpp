#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <thread>
#include <csignal>

void myerror(const char* what) { perror(what); }

void usage() {
    printf("echo-client:\n");
    printf("syntax : echo-client <server_ip> <server_port>\n");
    printf("sample : echo-client 192.168.10.2 1234\n");
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
	}

	printf("disconnected\n");
	fflush(stdout);
	::close(sd);
	exit(0);
}

int main(int argc, char* argv[]) {
	std::signal(SIGPIPE, SIG_IGN);

    if (argc != 3) {
        usage();
        return -1;
    }

	const char* server_ip = argv[1];
    uint16_t port = atoi(argv[2]);

    in_addr ip{};
    if (inet_aton(server_ip, &ip) == 0) {
        fprintf(stderr, "invalid server ip\n");
        return -1;
    }

    int sd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (sd == -1) { myerror("socket"); return -1; }

    sockaddr_in svr{};
    svr.sin_family = AF_INET;
    svr.sin_addr = ip;
    svr.sin_port = htons(port);

    if (::connect(sd, reinterpret_cast<sockaddr*>(&svr), sizeof(svr)) == -1) {
        myerror("connect");
        ::close(sd);
        return -1;
    }

	std::thread(recvThread, sd).detach();

    static const int BUFSIZE = 65536;
    char line[BUFSIZE];

    while (fgets(line, sizeof(line), stdin)) {
        size_t len = strlen(line);
        if (len == 0) continue;
        ssize_t s = ::send(sd, line, len, 0);
        if (s <= 0) {
            if (s < 0) myerror("send\n");
            break;
        }
    }	

    ::shutdown(sd, SHUT_WR);
    return 0;
}