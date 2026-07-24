#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cerrno>
#include <cstdlib>

class udp_sender {
public:
    udp_sender() = delete;
    udp_sender(const char* ip, const int port, const int bandwidth);
    ~udp_sender();

    int send_buffer(const u_char *buf, int len);

private:
    int sockfd, port, bandwidth;
    struct sockaddr_in addr;
    socklen_t addr_len;
};
