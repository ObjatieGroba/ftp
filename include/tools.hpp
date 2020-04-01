#ifndef FTP_TOOLS_HPP
#define FTP_TOOLS_HPP

#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

int open_connection(unsigned ip, unsigned port) {
  int sock;
  if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    return -1;
  }
  int synRetries = 1;
  if (setsockopt(sock, IPPROTO_TCP, TCP_SYNCNT, &synRetries, sizeof(synRetries)) < 0) {
    close(sock);
    return -1;
  }
  struct sockaddr_in serv_addr{};
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_port = htons(port);
  serv_addr.sin_addr.s_addr = htonl(ip);
  if (connect(sock, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
    close(sock);
    return -1;
  }
  return sock;
}

#endif // FTP_TOOLS_HPP
