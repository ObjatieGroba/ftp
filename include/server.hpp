#ifndef FTP_SERVER_HPP
#define FTP_SERVER_HPP

#include <string>
#include <stdexcept>
#include <iostream>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>


template <class Handler>
class Server {
public:
    Server(const std::string &host, uint16_t port, int queue_size = 5)
            : sock_(socket(AF_INET, SOCK_STREAM, 0)) {
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

    Server(Server &&other) noexcept : sock_(other.sock_) {
        other.sock_ = -1;
    }

    Server& operator=(Server &&other) noexcept {
        sock_ = other.sock_;
        other.sock_ = -1;
        return *this;
    }

    ~Server() {
        close(sock_);
    }

    template <class Settings>
    void run(const Settings &settings) {
        int user_sock;
        while ((user_sock = accept(sock_, nullptr, nullptr)) != -1) {
            try {
                Handler(user_sock, settings).run();
            } catch (const std::exception& e) {
                std::cerr << e.what() << '\n';
            }
        }
    }

    template <class Func>
    bool run_one(Func func) {
        int user_sock;
        if ((user_sock = accept(sock_, nullptr, nullptr)) != -1) {
            try {
                func(user_sock);
                return true;
            } catch (const std::exception& e) {
                std::cerr << e.what() << '\n';
                return false;
            }
        }
        return false;
    }

    int accept_one() {
        struct timeval tv{};
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(sock_, &rfds);
        tv.tv_sec = 30;
        tv.tv_usec = 0;

        int res = select(sock_ + 1, &rfds, nullptr, nullptr, &tv);
        if (res > 0) {
            return accept(sock_, nullptr, nullptr);
        }
        return -1;
    }

private:
    int sock_;
};

bool set_timeout_fd(int fd, int type, int seconds=60) {
    struct timeval timeout{seconds, 0};
    return setsockopt(fd, SOL_SOCKET, type, (char *)&timeout, sizeof(timeout)) != -1;
}

#endif //FTP_SERVER_HPP
