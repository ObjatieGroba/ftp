#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <chrono>

#include "server.hpp"

size_t ClientIDs = 0;

class Client;

void start(size_t client_id, std::shared_ptr<Client> client);
size_t process(size_t client_id, std::string_view buf);
void fail(size_t client_id);

class Client : public IO::BufferedHandler {
public:
  Client(IO::Context* io, int fd_in, int fd_out)
      : IO::BufferedHandler(io, fd_in, fd_out), id(ClientIDs++) { }

  Client(IO::Context* io, int fd) : Client(io, fd, fd) { }

  void run() {
    read(buf.data(), buf.size());
    ::start(id, std::dynamic_pointer_cast<Client>(this->shared_from_this()));
  }

  void on_read(const char *, size_t size) final {
    read_to += size;
    while (read_from != read_to) {
      auto rd = ::process(id, std::string_view(buf.data() + read_from, read_to - read_from));
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
    std::cerr << "Error was :(" << std::endl;
    ::fail(id);
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

public:
  const size_t id;
};


/// Unique users identifier. 0 for anonymous. 1 for console user
typedef size_t UserID;
enum {
  Anonymous = 0,
  Console = 1
};
struct UserInfo {
  UserID id;
  std::string login;
  std::string password;
  bool admin;
  bool banned;
  std::vector<UserID> voted;

  /// Only for session
  time_t last_write{};
};
std::ostream& operator<<(std::ostream &stream, const UserInfo& info) {
  stream << info.id << ' ' << info.login << ' ' << info.password << ' ' << info.admin << ' ' << info.banned << "   ";
  stream << info.voted.size();
  for (auto elem : info.voted) {
    stream << ' ' << elem;
  }
  stream << '\n';
  return stream;
}
std::istream& operator>>(std::istream &stream, UserInfo& info) {
  stream >> info.id >> info.login >> info.password >> info.admin >> info.banned;
  size_t size;
  if (!(stream >> size)) {
    return stream;
  }
  info.voted.resize(size);
  for (auto && elem : info.voted) {
    stream >> elem;
  }
  return stream;
}
struct ExtraInfo {
  std::shared_ptr<Client> conn;
  std::string capcha_answer;
  std::string new_login;
  std::string new_pass;
  bool isSubscribed;
};

std::map<size_t, ExtraInfo> current_users;
std::map<size_t, UserID> current_users_ids;
std::map<UserID, UserInfo> all_users;
std::map<std::string, UserID> login_to_id;

constexpr auto kWidth = 25;
constexpr auto kHeight = 10;

std::array<char, kWidth * kHeight> board{};

std::string board_filename = "board.bin";

time_t last_stored_board_time = 0;

bool store_board() {
  auto now = std::time(nullptr);
  if (now < last_stored_board_time + 60) {
    return true;
  }
  last_stored_board_time = now;
  std::ofstream fout;
  fout.open(board_filename);
  if (!fout.good()) {
    std::cerr << "Can not store board" << std::endl;
    return false;
  }
  for (size_t i = 0; i != board.size(); ++i) {
    fout << board[i];
  }
  return true;
}

void load_board() {
  std::ifstream fin;
  fin.open(board_filename);
  if (!fin.good()) {
    std::cerr << "No board in file" << std::endl;
    board.fill('0');
    store_board();
    return;
  }
  for (size_t i = 0; i != board.size(); ++i) {
    fin >> board[i];
  }
}

std::string users_filename = "users.bin";

bool store_users() {
  std::ofstream fout;
  fout.open(users_filename);
  if (!fout.good()) {
    std::cerr << "Can not store users" << std::endl;
    return false;
  }
  for (auto && [id, info] : all_users) {
    fout << info;
  }
  return true;
}

void load_users() {
  std::ifstream fin;
  fin.open(users_filename);
  if (!fin.good()) {
    std::cerr << "No users in file" << std::endl;
    all_users = {{0, {0, "anonymous", "anonymous", false, false, {}}},
                 {1, {1, "admin", "admin", true, false, {}}}};
    for (auto && [id, info] : all_users) {
      login_to_id[info.login] = id;
    }
    store_users();
    return;
  }
  UserInfo info;
  while (fin >> info) {
    all_users[info.id] = info;
    login_to_id[info.login] = info.id;
  }
}


void broadcast(unsigned self_id, std::string_view msg, bool force = false) {
  for (auto && [id, client] : current_users) {
    if (client.isSubscribed || force) {
      client.conn->write(msg);
      if (id != self_id) {
        client.conn->sync();
      }
    }
  }
}

void start(size_t client_id, std::shared_ptr<Client> client) {
  std::cerr << client_id << " Connected\n";
  current_users[client_id] = {std::move(client), {}, {}, {}, false};
  broadcast(client_id, "login anonymous\n");
}

size_t process(size_t client_id, std::string_view buf) {
  store_board();
  auto user_id = current_users_ids[client_id];
  std::cerr << client_id << " is " << user_id << ", process" << std::endl;
  auto &info = current_users[client_id];
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
  if (!info.capcha_answer.empty()) {
    /// User should type capcha
    if (command == info.capcha_answer) {
      info.capcha_answer.clear();
      auto new_id = all_users.size();
      all_users[new_id] = {new_id, info.new_login, info.new_pass, false, false, {}};
      login_to_id[info.new_login] = new_id;
      info.conn->write("Ok\n");
      store_users();
      return command.size() + 1;
    }
    command = "exit";
    info.conn->write("Bad capcha\n");
  }

  if (command == "exit") {
    std::string msg = "logout "; msg += all_users[user_id].login; msg += '\n';
    broadcast(client_id, msg);
    info.conn->write("Bie\n");
    info.conn->stop();
    info.conn->sync();
    current_users.erase(client_id);
    return 0;
  } else if (command == "get board") {
    info.conn->write("board: ");
    info.conn->write(board.data());
    info.conn->write("\n");
  } else if (command == "get users") {
    info.conn->write("users: ");
    for (auto && [id, client] : current_users) {
      info.conn->write(all_users[current_users_ids[id]].login);
      info.conn->write(" ");
    }
    info.conn->write("\n");
  } else if (command.substr(0, 6) == "login ") {
    std::istringstream ss(std::string(command.substr(6)));
    std::string login;
    std::string password;
    ss >> login >> password;
    auto it = login_to_id.find(login);
    if (it == login_to_id.end() || password != all_users[it->second].password) {
      info.conn->write("Wrong credentials\n");
      return command.size() + 1;
    }
    std::string msg = "logout "; msg += all_users[user_id].login; msg += '\n';
    broadcast(client_id, msg);
    current_users_ids[client_id] = it->second;
    msg = "login "; msg += login; msg += '\n';
    broadcast(client_id, msg);
    info.conn->write("Ok\n");
  } else if (command.substr(0, 4) == "reg ") {
    std::istringstream ss(std::string(command.substr(4)));
    ss >> info.new_login >> info.new_pass;
    auto it = login_to_id.find(info.new_login);
    if (it != login_to_id.end()) {
      info.conn->write("User already exists\n");
      return command.size() + 1;
    }
    if (info.new_login.size() > 10) {
      info.conn->write("Username is too long (>10 chars)\n");
      return command.size() + 1;
    }
    auto x = rand() % 100;
    auto y = rand() % 100;
    std::string capcha = std::to_string(x) + " + " + std::to_string(y);
    info.capcha_answer = "capcha " + std::to_string(x + y);
    info.conn->write("capcha: Please write answer of " + capcha + " with capcha command\n");
  } else if (command.substr(0, 5) == "vote ") {
    if (user_id == Anonymous) {
      info.conn->write("Only logged in users can vote\n");
      return command.size() + 1;
    }
    std::istringstream ss(std::string(command.substr(5)));
    std::string login;
    ss >> login;
    auto it = login_to_id.find(login);
    if (it == login_to_id.end()) {
      info.conn->write("No such user\n");
      return command.size() + 1;
    }
    auto now = std::time(nullptr);
    if (!all_users[user_id].admin && now - all_users[user_id].last_write < 60) {
      info.conn->write("Wait cooldown " + std::to_string(60 - now + all_users[user_id].last_write) + " seconds\n");
      return command.size() + 1;
    }
    auto & voted = all_users[it->second].voted;
    for (auto elem : voted) {
      if (elem == user_id) {
        info.conn->write("Already voted\n");
        return command.size() + 1;
      }
    }
    if (all_users[user_id].banned && !all_users[user_id].admin) { /// Black ban
      info.conn->write("Ok\n");
      return command.size() + 1;
    }
    voted.push_back(user_id);
    if (voted.size() == 2) {
      /// Send msg to user about vote if online
      for (auto && [id, client] : current_users) {
        if (current_users_ids[id] == it->second && client.isSubscribed) {
          client.conn->write("Somebody voted for your ban\n");
          client.conn->sync();
        }
      }
    }
    if (voted.size() * 2 > all_users.size() - 1) {
      all_users[it->second].banned = true;
    }
    info.conn->write("Ok\n");
    store_users();
  } else if (command == "subscribe") {
    info.isSubscribed = true;
    info.conn->write("Ok\n");
  } else if (command == "unsubscribe") {
    info.isSubscribed = false;
    info.conn->write("Ok\n");
  } else if (command.substr(0, 4) == "set ") {
    if (user_id == Anonymous) {
      info.conn->write("Only logged in users can set board\n");
      return command.size() + 1;
    }
    std::istringstream ss(std::string(command.substr(4)));
    unsigned i;
    unsigned j;
    if ((ss >> i) && (ss >> j)) {
      if (i >= kHeight && j >= kWidth) {
        info.conn->write("Out of range\n");
        return command.size() + 1;
      }
      auto now = std::time(nullptr);
      if (!all_users[user_id].admin && now - all_users[user_id].last_write < 60) {
        info.conn->write("Wait cooldown " + std::to_string(60 - now + all_users[user_id].last_write) + " seconds\n");
        return command.size() + 1;
      }
      if (ss.peek() != ' ') {
        info.conn->write("Bad format\n");
        return command.size() + 1;
      }
      ss.ignore();
      char c = ss.peek();
      if (!isprint(c) || c == '\n' || c == '\r') {
        info.conn->write("Unsupported character\n");
        return command.size() + 1;
      }
      all_users[user_id].last_write = now;
      if (!all_users[user_id].banned || all_users[user_id].admin) { /// Black ban
        board[i * kWidth + j] = c;
        broadcast(client_id, "set " + std::to_string(i) + " " +
                             std::to_string(j) + " " + c + '\n');
      }
      info.conn->write("Ok\n");
      return command.size() + 1;
    }
    info.conn->write("Syntax error\n");
  } else if (command.substr(0, 4) == "ban " && all_users[user_id].admin) {
    std::istringstream ss(std::string(command.substr(4)));
    std::string login;
    ss >> login;
    auto it = login_to_id.find(login);
    if (it == login_to_id.end()) {
      info.conn->write("No such user\n");
    } else {
      all_users[it->second].banned = true;
      info.conn->write("Ok\n");
      store_users();
    }
  } else if (command.substr(0, 6) == "unban " && all_users[user_id].admin) {
    std::istringstream ss(std::string(command.substr(6)));
    std::string login;
    ss >> login;
    auto it = login_to_id.find(login);
    if (it == login_to_id.end()) {
      info.conn->write("No such user\n");
    } else {
      all_users[it->second].banned = false;
      info.conn->write("Ok\n");
      store_users();
    }
  } else if (command == "stop server" && all_users[user_id].admin) {
    info.conn->write("Stopped\n");
    broadcast(client_id, "Server closed by administrator\n", true);
    exit(0);
  } else {
    info.conn->write("Unknown command\n");
  }
  return command.size() + 1;
}

void fail(size_t client_id) {
  auto user_id = current_users_ids[client_id];
  std::string msg = "logout "; msg += all_users[user_id].login; msg += '\n';
  broadcast(client_id, msg);
  std::cerr << client_id << " Failed\n";
  current_users.erase(client_id);
}


void help() {
  std::cout << "Usage: ./server host port\n"
      "  Optional arguments:\n"
      "    --board filename   - file for board\n"
      "    --users filename   - file for accounts information\n";
  exit(0);
}


int main(int argc, char ** argv) {
  if (argc < 3) {
    help();
  }
  for (int i = 0; i < argc - 1; ++i) {
    if (std::string_view(argv[i]) == "--board") {
      board_filename = argv[++i];
    } else if (std::string_view(argv[i]) == "--users") {
      users_filename = argv[++i];
    } else if (std::string_view(argv[i]) == "--help") {
      help();
    }
  }

  std::string host = argv[1];
  std::string port = argv[2];

  load_board();
  load_users();

  IO::Context io;

  auto client_handler = [&](int fd, sockaddr_in addr) {
    std::make_shared<Client>(&io, fd)->run();
  };

  {
    int fd_in = fileno(stdin);
    int in_flags = fcntl(fd_in, F_GETFL, 0);
    fcntl(fd_in, F_SETFL, in_flags | O_NONBLOCK);
    int fd_out = fileno(stdout);
    int out_flags = fcntl(fd_out, F_GETFL, 0);
    fcntl(fd_out, F_SETFL, out_flags | O_NONBLOCK);
    auto client = std::make_shared<Client>(&io, fd_in, fd_out);
    client->run();
    current_users_ids[client->id] = Console; // Set console user
  }

  auto server = std::make_shared<Server<decltype(client_handler)>>(&io, host, strtoul(port.c_str(), nullptr, 10), client_handler);
  server->run();

  io.run();
}
