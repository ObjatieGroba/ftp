#include <arpa/inet.h>
#include <iostream>
#include <netinet/in.h>
#include <sstream>
#include <sys/fcntl.h>

#include "io.hpp"
#include "tools.hpp"

size_t process_api_response(std::string_view buf);
size_t process_console_command(std::string_view buf);

class AsyncAPI : public IO::BufferedHandler {
public:
  AsyncAPI(IO::Context* io, int fd) : IO::BufferedHandler(io, fd) { }

  void run() {
    write("subscribe\n");
    write("get users\n");
    write("get board\n");
    sync();
  }

  void on_read(const char *, size_t size) final {
    read_to += size;
    while (read_from != read_to) {
      auto rd = ::process_api_response(std::string_view(buf.data() + read_from, read_to - read_from));
      read_from += rd;
      if (sync()) {
        return;
      }
      if (rd == 0) {
        break;
      }
    }
    continue_read();
  }

  void on_write() final {
    on_read(nullptr, 0);
  }

  void on_fail() final {
    std::cerr << "Server closed connection\n" << std::endl;
    exit(0);
  };

private:
  void continue_read() {
    compress_read_buf();
    if (buf.size() == read_to) {
      throw std::runtime_error("Buffer overflow");
    }
    read(buf.data() + read_to, buf.size() - read_to);
  }

  void compress_read_buf() {
    if (read_from == read_to) {
      read_from = read_to = 0;
    } else if (read_from != 0) {
      std::copy(buf.data() + read_from, buf.data() + read_to, buf.data());
      read_to -= read_from;
      read_from = 0;
    }
  }

  std::array<char, 1024> buf;
  size_t read_from = 0;
  size_t read_to = 0;
};


class Console : public IO::BufferedHandler {
public:
  Console(IO::Context* io) : IO::BufferedHandler(io, fileno(stdin), fileno(stdout)) {
    int fd_in = fileno(stdin);
    int in_flags = fcntl(fd_in, F_GETFL, 0);
    fcntl(fd_in, F_SETFL, in_flags | O_NONBLOCK);
    int fd_out = fileno(stdout);
    int out_flags = fcntl(fd_out, F_GETFL, 0);
    fcntl(fd_out, F_SETFL, out_flags | O_NONBLOCK);
  }

  void run() {
    write("\033c");
    sync();
  }

  void on_read(const char *, size_t size) final {
    read_to += size;
    while (read_from != read_to) {
      auto rd = ::process_console_command(std::string_view(buf.data() + read_from, read_to - read_from));
      read_from += rd;
      if (sync()) {
        return;
      }
      if (rd == 0) {
        break;
      }
    }
    continue_read();
  }

  void on_write() final {
    on_read(nullptr, 0);
  }

  void on_fail() final {
    exit(0);
  };

private:
  void continue_read() {
    compress_read_buf();
    if (buf.size() == read_to) {
      throw std::runtime_error("Buffer overflow");
    }
    read(buf.data() + read_to, buf.size() - read_to);
  }

  void compress_read_buf() {
    if (read_from == read_to) {
      read_from = read_to = 0;
    } else if (read_from != 0) {
      std::copy(buf.data() + read_from, buf.data() + read_to, buf.data());
      read_to -= read_from;
      read_from = 0;
    }
  }

  std::array<char, 1024> buf;
  size_t read_from = 0;
  size_t read_to = 0;
};

std::shared_ptr<AsyncAPI> api;
std::shared_ptr<Console> console;

constexpr auto kWidth = 25;
constexpr auto kHeight = 10;

std::array<char, kWidth * kHeight> board{};

unsigned console_width = 70;

std::string last_response;
std::vector<std::string> active_users;

void drow_board(bool clear_user_input) {
  console->write("\033[1;1H");
  for (int i = 0; i != kWidth * kHeight; i += kWidth) {
    for (int j = 0; j != kWidth; ++j) {
      console->write(board[i + j]);
    }
    for (int j = kWidth; j < console_width; ++j) {
      console->write(' ');
    }
    console->write('\n');
  }
  for (int i = 0; i < active_users.size() + 1 && i < kHeight; ++i) {
    console->write("\033[" + std::to_string(i + 1) + ";" + std::to_string(kWidth + 3) + "H");
    if (i == 0) {
      console->write("Active users:");
    } else {
      console->write(active_users[i - 1]);
    }
  }
  console->write("\033[" + std::to_string(kHeight + 1) + ";1H");
  for (int j = 0; j < console_width; ++j) {
    console->write(' ');
  }
  console->write('\n');
  console->write(last_response);
  for (int j = last_response.size(); j < console_width; ++j) {
    console->write(' ');
  }
  console->write('\n');
  if (clear_user_input) {
    for (int j = 0; j < console_width; ++j) {
      console->write(' ');
    }
    console->write('\r');
  }
}

size_t process_api_response(std::string_view buf) {
  if (buf.empty()) {
    std::cerr << "Zero size" << std::endl;
    return 0;
  }
  size_t i = 0;
  for (; i != buf.size(); ++i) {
    if (buf[i] == '\n') {
      break;
    }
  }
  if (i == buf.size()) {
    /// Not full command. Command ends with \n
    return 0;
  }
  std::string_view command = buf.substr(0, i);
  if (command.substr(0, 4) == "set ") {
    std::istringstream ss(std::string(command.substr(4)));
    unsigned i;
    unsigned j;
    if ((ss >> i) && (ss >> j)) {
      if (i >= kHeight && j >= kWidth) {
        // Out of range
        return command.size() + 1;
      }
      if (ss.peek() != ' ') {
        // Bad format
        return command.size() + 1;
      }
      ss.ignore();
      char c = ss.peek();
      if (!isprint(c) || c == '\n' || c == '\r') {
        // Unsupported character
        return command.size() + 1;
      }
      board[i * kWidth + j] = c;
    }
  } else if (command.substr(0, 7) == "board: ") {
    size_t i = 7;
    for (auto && elem : board) {
      elem = command[i++];
    }
  } else if (command.substr(0, 7) == "users: ") {
    active_users.clear();
    std::istringstream ss(std::string(command.substr(7)));
    std::string name;
    while (ss >> name) {
      active_users.push_back(std::move(name));
    }
  } else if (command.substr(0, 6) == "login ") {
    std::istringstream ss(std::string(command.substr(6)));
    std::string name;
    ss >> name;
    active_users.push_back(std::move(name));
  } else if (command.substr(0, 7) == "logout ") {
    std::istringstream ss(std::string(command.substr(7)));
    std::string name;
    ss >> name;
    if (!active_users.empty()) {
      if (active_users.back() == name) {
        active_users.pop_back();
      } else {
        for (size_t i = 0; i != active_users.size() - 1; ++i) {
          if (active_users[i] == name) {
            active_users[i] = active_users.back();
            active_users.pop_back();
            break;
          }
        }
      }
    }
  } else {
    last_response = command;
  }
  drow_board(false);
  console->sync();
  return command.size() + 1;
}

size_t process_console_command(std::string_view buf) {
  if (buf.empty()) {
    std::cerr << "Zero size" << std::endl;
    return 0;
  }
  size_t i = 0;
  for (; i != buf.size(); ++i) {
    if (buf[i] == '\n') {
      break;
    }
  }
  if (i == buf.size()) {
    /// Not full command. Command ends with \n
    return 0;
  }
  api->write(buf.substr(0, i + 1));
  api->sync();
  drow_board(true);
  return i + 1;
}


void help() {
  std::cout << "Usage: ./client host port\n";
  exit(0);
}

int main(int argc, char ** argv) {
  if (argc < 3) {
    help();
  }
  for (int i = 0; i < argc - 1; ++i) {
    if (std::string_view(argv[i]) == "--help") {
      help();
    }
  }

  std::string host = argv[1];
  std::string port = argv[2];

  int conn = open_connection(htonl(inet_addr(host.c_str())), strtoul(port.c_str(), nullptr, 10));
  if (conn == -1) {
    std::cerr << "Can not open connection" << std::endl;
    exit(1);
  }

  IO::Context io;

  console = std::make_shared<Console>(&io);
  console->run();
  api = std::make_shared<AsyncAPI>(&io, conn);
  api->run();

  io.run();
}