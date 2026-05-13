#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cerrno>
#include <cstdlib>
#include <iostream>

#include "udp_sender.h"

udp_sender::udp_sender(const char* ip, const int port, const int bandwidth) : port{ port }, bandwidth{ bandwidth } {
    if (bandwidth <= 0) {
	std::cerr << "bandwidth should be greater than 0.\n";
    }

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if(sockfd < 0) {
	std::cerr << "socket failed.\n";
	exit(1);
    }
    std::cout << "sockfd: " << sockfd << "\n";
	
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(ip);
    addr.sin_port = htons(port);

    addr_len = sizeof(addr);

    std::cout << "create udp_sender for ip: " << ip << ", port: " << port << ", bandwidth: " << bandwidth << "\n";
}

udp_sender::~udp_sender() {
    close(sockfd);
}

int udp_sender::send_buffer(const u_char *buf, int len) {
    /* Send of a buffer will be split by bandwidth. packet_info(curr_packet_num, total_packet_num, and remaining_size) will be sent to the target before sending each split packet. */
    int packet_info[3];
	
    int total_packet_num = (int)(len / bandwidth) + 1;
    int remaining_size = len;

    for (int num = 1; num <= total_packet_num; num++) {
	packet_info[0] = num;
	packet_info[1] = total_packet_num;
	packet_info[2] = remaining_size;
	    
	// send packet info
	int cnt = sendto(sockfd, packet_info, sizeof(packet_info), 0, (struct sockaddr*)&addr, addr_len);

	if (cnt == -1) {
	    std::cout << "sendto failed. errno: " << errno << "\n";
	}
	    
	// send packet
	cnt = sendto(sockfd, buf + (len - remaining_size), remaining_size > bandwidth ? bandwidth : remaining_size, 0, (struct sockaddr*)&addr, addr_len);
	remaining_size -= cnt;

	if (cnt == -1) {
	    std::cout << "sendto failed. errno: " << errno << "\n";
	}
    }

    if (remaining_size != 0) {
	std::cout << "send_buffer failed. " << remaining_size << " Bytes are not sent.\n";
    }
	
    int bytes_sent = len - remaining_size;
    return bytes_sent;
}
