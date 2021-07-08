#ifndef FTP_SERVER_HPP
#define FTP_SERVER_HPP

#include <memory>
#include <iostream>

#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>

#include "io.hpp"

template <class ClientHandler>
class Server : public std::enable_shared_from_this<Server<ClientHandler>>, public IO::Context::Handler {
public:
  Server(IO::Context* io, const std::string &host, uint16_t port, ClientHandler h, int queue_size = 5)
      : IO::Context::Handler(io),
        sock_(socket(AF_INET, SOCK_STREAM, 0)),
        handler(h) {
    if (sock_ == -1) {
      throw std::runtime_error("Can not create socket");
    }
    int enable = 1;
    if (setsockopt(sock_, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)) == -1) {
      close(sock_);
      throw std::runtime_error("Can not set reusable");
    }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(host.c_str());
    addr.sin_port = htons(port);
    if (bind(sock_, (sockaddr*)&addr, sizeof(addr)) == -1) {
      close(sock_);
      throw std::runtime_error("Can not bind");
    }
    if (listen(sock_, queue_size) == -1) {
      close(sock_);
      throw std::runtime_error("Can not listen");
    }
  }

  ~Server() override {
    close(sock_);
  }

  void run() {
    io->add(sock_, EPOLLIN, this->shared_from_this());
  }

  void operator()(epoll_event ev) final {
    if (ev.events & EPOLLERR) {
      throw std::runtime_error("Listen error");
    }
    if (ev.events & EPOLLIN) {
      sockaddr_in client{};
      socklen_t size = sizeof(client);
      int fd = accept(sock_, reinterpret_cast<sockaddr *>(&client), &size);
      if (fd == -1) {
        std::cerr << "Accept failed\n";
        return;
      }
      auto flags = fcntl(fd, F_GETFL, 0);
      if (flags == -1) {
        std::cerr << "Flags get failed\n";
        close(fd);
        return;
      }
      fcntl(fd, F_SETFL, flags | O_NONBLOCK);
      handler(fd, client);
    }
  }

private:
  int sock_;
  ClientHandler handler;
};

#endif // FTP_SERVER_HPP
