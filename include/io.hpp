#ifndef FTP_IO_HPP
#define FTP_IO_HPP

#include <vector>
#include <functional>
#include <map>
#include <sys/epoll.h>

#include "scope_guard.hpp"

namespace IO {

static constexpr auto kMaxEvents = 10;

class Context {
public:
  class Handler {
  public:
    explicit Handler(Context* io_) : io(io_) { }
    virtual ~Handler() = default;
    virtual void operator()(epoll_event ev) = 0;

  protected:
    Context* io;
  };

  Context() {
    epoll_fd = epoll_create1(0);
    if (epoll_fd < 0) {
      throw std::runtime_error("Can not create epoll");
    }
  }

  ~Context() { close(epoll_fd); }

  void run() {
    for (;;) {
      int ntds = epoll_wait(epoll_fd, event_buf.data(), event_buf.size(), -1);
      if (ntds < 0) {
        throw std::runtime_error("Can not wait epoll events");
      }
      for (int i = 0; i != ntds; ++i) {
        auto handler = fd_handlers[event_buf[i].data.fd];
        if (!handler) {
          std::cerr << "There are no handler for fd " +
                           std::to_string(event_buf[i].data.fd) +
                           ". Seem to be bug\n"
                    << std::endl;
          continue;
        }
        (*handler)(event_buf[i]);
      }
    }
  }

  bool add(int fd, uint32_t events, std::shared_ptr<Handler> handler) {
    epoll_event ev{};
    ev.events = events;
    ev.data.fd = fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev) != -1) {
      fd_handlers[fd] = std::move(handler);
      return true;
    }
    return false;
  }

  bool mod(int fd, uint32_t events) {
    epoll_event ev{};
    ev.events = events;
    ev.data.fd = fd;
    return epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &ev) != -1;
  }

  bool remove(int fd, uint32_t events) {
    epoll_event ev{};
    ev.events = events;
    ev.data.fd = fd;
    fd_handlers.erase(fd);
    return epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, &ev) != -1;
  }

private:
  std::map<int, std::shared_ptr<Handler>> fd_handlers;
  std::array<epoll_event, kMaxEvents> event_buf;

  int epoll_fd;
};

class BufferedHandler : public std::enable_shared_from_this<BufferedHandler>,
                        public Context::Handler {
public:
  explicit BufferedHandler(Context *io, int fd_in, int fd_out)
      : Context::Handler(io),
        fd_in_(fd_in), fd_out_(fd_out) { }

  explicit BufferedHandler(Context *io, int fd) : BufferedHandler(io, fd, fd) { }

  ~BufferedHandler() override {
    close(fd_in_);
    if (fd_in_ != fd_out_) {
      close(fd_out_);
    }
  }

  void operator()(epoll_event ev) final {
    if (stopped) {
      return;
    }
    if (ev.events & EPOLLERR) {
      std::cerr << "Error received client io - client " << fd_in_ << " " << fd_out_ << std::endl;
      return fail();
    }
    if (ev.events & EPOLLOUT && write_buf_.size() != to_write_from_) {
      if (write() < 0) {
        std::cerr << "Can not write to client " << fd_out_ << std::endl;
        return fail();
      }
      if (write_buf_.size() == to_write_from_) {
        write_buf_.clear();
        to_write_from_ = 0;
        on_write();
      }
      return;
    }
    if (ev.events & EPOLLIN) {
      if (write_buf_.size() != to_write_from_) {
        std::cerr << "Read but some need to be written - client " << fd_in_ << std::endl;
        return fail();
      }
      auto rd = ::read(fd_in_, read_buf_, read_buf_size_);
      if (rd <= 0) {
        std::cerr << "Can not read from client " << fd_in_ << std::endl;
        return fail();
      }
      on_read(read_buf_, rd);
    } else {
      std::cerr << "Incorrect event: " << ev.events << "; to_write: " << write_buf_.size() - to_write_from_ << std::endl;
    }
  }

  void write(std::string_view buf) {
    if (stopped) {
      return;
    }
    write_buf_ += buf;
  }

  bool sync() {
    if (stopped) {
      return false;
    }
    if (write_buf_.empty()) {
      return false;
    }
    if (write() < 0) {
      std::cerr << "Can not write to client " << fd_out_ << std::endl;
      fail();
      return true;
    }
    if (write_buf_.size() == to_write_from_) {
      write_buf_.clear();
      to_write_from_ = 0;
      on_write();
    } else if (last_event_ != EPOLLOUT) {
      bool err = false;
      if (fd_in_ != fd_out_) {
        err = !(last_event_ == 0 || io->remove(fd_in_, last_event_));
        last_event_ = EPOLLOUT;
        err = err || !io->add(fd_out_, last_event_, this->shared_from_this());
      } else if (last_event_ == 0) {
        io->add(fd_out_, last_event_ = EPOLLOUT, this->shared_from_this());
      } else {
        err = !io->mod(fd_out_, last_event_ = EPOLLOUT);
      }
      if (err) {
        std::cerr << "Can not change mod " << fd_in_ << " " << fd_out_ << "\n";
        fail();
        return true;
      }
    }
    return true;
  }

  void read(char * buf, size_t size) {
    if (stopped) {
      return;
    }
    read_buf_ = buf;
    read_buf_size_ = size;
    if (last_event_ != EPOLLIN) {
      bool err = false;
      if (fd_in_ != fd_out_) {
        err = !(last_event_ == 0 || io->remove(fd_out_, last_event_));
        last_event_ = EPOLLIN;
        err = err || !io->add(fd_in_, last_event_, this->shared_from_this());
      } else if (last_event_ == 0) {
        io->add(fd_in_, last_event_ = EPOLLIN, this->shared_from_this());
      } else {
        err = !io->mod(fd_in_, last_event_ = EPOLLIN);
      }
      if (err) {
        std::cerr << "Can not change mod " << fd_in_ << " " << fd_out_ << "\n";
        return fail();
      }
    }
  }

  void stop() {
    last_event_ = 0;
    stopped = true;
    if (!io->remove(fd_in_, last_event_)) {
      std::cerr << "Can not remove client io - client " << fd_in_ << std::endl;
    }
    if (fd_in_ != fd_out_) {
      if (!io->remove(fd_out_, last_event_)) {
        std::cerr << "Can not remove client io - client " << fd_out_ << std::endl;
      }
    }
  }

  virtual void on_read(const char *buf, size_t size) = 0;
  virtual void on_write() = 0;
  virtual void on_fail() = 0;

private:
  ssize_t write() {
    auto wr = ::write(fd_out_, write_buf_.data() + to_write_from_, write_buf_.size() - to_write_from_);
    if (wr > 0) {
      to_write_from_ += wr;
    }
    return wr;
  }

  void fail() {
    stop();
    on_fail();
  }

  std::string write_buf_;
  size_t to_write_from_{};
  char * read_buf_;
  size_t read_buf_size_;

  int fd_in_;
  int fd_out_;
  uint32_t last_event_{};

  bool stopped = false;
};

}

#endif // FTP_IO_HPP
