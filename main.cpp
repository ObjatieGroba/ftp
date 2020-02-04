#include <iostream>
#include <array>
#include <algorithm>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <fstream>

#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <csignal>
#include <wait.h>
#include <security/pam_appl.h>
#include <security/pam_misc.h>

constexpr unsigned BUF_SIZE = 4096;

template <class Handler>
class Server {
public:
    Server(uint16_t port, int queue_size = 5) : sock_(socket(AF_INET, SOCK_STREAM, 0)) {
        if (sock_ == -1) {
            throw std::runtime_error("Can not create socket");
        }А
        int enable = 1;
        if (setsockopt(sock_, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)) == -1) {
            close(sock_);
            throw std::runtime_error("Can not set reusable");
        }
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
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

    void run() {
        int user_sock;
        while ((user_sock = accept(sock_, nullptr, nullptr)) != -1) {
            try {
                Handler(user_sock).run();
            } catch (const std::exception& e) {
                std::cerr << e.what() << '\n';
            }
        }
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

class FDBuf : public std::streambuf {
public:
    explicit FDBuf (int fd) : fd_(fd) { }

    ~FDBuf() final {
        if (need_close) {
            close(fd_);
        }
    }

    int dismiss() {
        need_close = false;
        return fd_;
    }

    int get_fd() const {
        return fd_;
    }

protected:
    int overflow (int c) final {
        throw std::runtime_error("Not implemented");
    }

    int sync() final {
        if (out_size_== 0) {
            return 0;
        }
        if (send(fd_, out_buf_.data(), out_size_, 0) != out_size_) {
            throw std::runtime_error("Can not write to fd");
        }
        out_size_ = 0;
        return 0;
    }

    std::streamsize xsputn(const char* s, std::streamsize num_) final {
        unsigned long num = num_;
        while (num > 0) {
            auto can_write = std::min(out_buf_.size() - out_size_, num);
            if (can_write > 0) {
                memcpy(out_buf_.data() + out_size_, s, can_write);
                s += can_write;
                num -= can_write;
                out_size_ += can_write;
            }
            if (out_size_ == out_buf_.size()) {
                if (send(fd_, out_buf_.data(), out_size_, 0) != out_size_) {
                    throw std::runtime_error("Can not write to fd");
                }
                out_size_ = 0;
            }
        }
        return num_;
    }

    std::streamsize xsgetn(char* s, std::streamsize num_) final {
        int num = num_;
        while (num > 0) {
            if (in_cur_ < in_size_) {
                auto can_read = std::min(in_size_ - in_cur_, num);
                if (can_read > 0) {
                    memcpy(s, in_buf_.data() + in_cur_, can_read);
                    s += can_read;
                    num -= can_read;
                    in_cur_ += can_read;
                }
            } else {
                in_size_ = recv(fd_, in_buf_.data(), in_buf_.size(), 0);
                in_cur_ = 0;
                if (in_size_ == -1) {
                    throw std::runtime_error("Can not read from fd");
                }
            }
        }
        return num_;
    }

    int underflow() final {
        if (in_cur_ != in_size_) {
            return in_buf_[in_cur_];
        }
        in_size_ = recv(fd_, in_buf_.data(), in_buf_.size(), 0);
        in_cur_ = 0;
        if (in_size_ == -1) {
            throw std::runtime_error("Can not read from fd");
        }
        return in_buf_[in_cur_];
    }

    int uflow() final {
        if (in_cur_ != in_size_) {
            return in_buf_[in_cur_++];
        }
        in_size_ = recv(fd_, in_buf_.data(), in_buf_.size(), 0);
        in_cur_ = 0;
        if (in_size_ == -1) {
            throw std::runtime_error("Can not read from fd");
        }
        if (in_size_ == 0) {
            return EOF;
        }
        return in_buf_[in_cur_++];
    }

    std::streamsize showmanyc() final {
        throw std::runtime_error("Not implemented");
    }

    int pbackfail(int c) final {
        throw std::runtime_error("Not implemented");
    }

private:
    int fd_;
    bool need_close = true;
    int in_cur_ = 0, in_size_ = 0, out_size_ = 0;
    std::array<char, BUF_SIZE> in_buf_{};
    std::array<char, BUF_SIZE> out_buf_{};
};

class FDOStream : public std::ostream {
protected:
    FDBuf buf_;
public:
    explicit FDOStream (int fd) : std::ostream(nullptr), buf_(fd) {
        rdbuf(&buf_);
    }

    int dismiss() {
        return buf_.dismiss();
    }

    int get_fd() const {
        return buf_.get_fd();
    }
};

class FDIStream : public std::istream {
protected:
    FDBuf buf_;
public:
    explicit FDIStream (int fd) : std::istream(nullptr), buf_(fd) {
        rdbuf(&buf_);
    }

    int dismiss() {
        return buf_.dismiss();
    }

    int get_fd() const {
        return buf_.get_fd();
    }
};

bool set_timeout_fd(int fd, int type, int seconds=30) {
    struct timeval timeout{seconds, 0};
    return setsockopt(fd, SOL_SOCKET, type, (char *)&timeout, sizeof(timeout)) != -1;
}

bool run_command(const std::string &cmd, std::ostream &out) {
    {
        FDOStream *s;
        if ((s = dynamic_cast<FDOStream *>(&out)) != nullptr) {
            if (!set_timeout_fd(s->get_fd(), SO_SNDTIMEO)) {
                return false;
            }
        }
    }
    std::array<char, 4096> buf{};
    FILE *file;
    if ((file = popen(cmd.c_str(), "r")) != nullptr) {
        size_t read;
        while ((read = fread(buf.data(), sizeof(char), sizeof(buf) / sizeof(char), file)) != 0) {
            out << std::string_view(buf.data(), read);
        }
        out.flush();
        pclose(file);
        return true;
    }
    return false;
}

bool write_file(const std::string &filename, FDIStream &in) {
    if (!set_timeout_fd(in.get_fd(), SO_RCVTIMEO)) {
        return false;
    }
    std::ofstream file;
    file.open(filename, std::ios_base::out);
    if (!file) {
        return false;
    }
    std::copy(std::istreambuf_iterator<char>(in),
              std::istreambuf_iterator<char>(),
              std::ostreambuf_iterator<char>(file)
    );
    file.close();
    return true;
}

class SingleLine {
public:
    SingleLine(FDOStream &out_, int code) : out(out_) {
        out << code << ' ';
    }

    ~SingleLine() {
        out << "\r\n";
        out.flush();
    }

    template <class T>
    SingleLine& operator<<(const T &t) {
        out << t;
        return *this;
    }

    FDOStream &out;
};

struct NewLine{ };
struct LastLine{ };

class MultiLine {
public:
    MultiLine(FDOStream &out_, int code_) : out(out_), code(code_) {
        out << code << '-';
    }

    ~MultiLine() {
        out << "\r\n";
        out.flush();
    }

    MultiLine& operator<<(NewLine) {
        out << "\r\n";
        return *this;
    }

    MultiLine& operator<<(LastLine) {
        out << code << ' ';
        return *this;
    }

    template <class T>
    MultiLine& operator<<(const T &t) {
        out << t;
        return *this;
    }

    FDOStream &out;
    int code;
};

/// Read till \r\n
static std::string read_till_end(FDIStream &in) {
    in.clear();
    std::string res;
    int c;
    bool prev_correct = false;
    while ((c = in.get()) != EOF) {
        if (c == '\r') {
            prev_correct = true;
        } else if (c == '\n' && prev_correct) {
            return res;
        } else {
            if (prev_correct) {
                res.push_back('\r');
                prev_correct = false;
            }
            res.push_back(c);
        }
    }
    return res;
}

template <class F>
class Operation {
public:
    friend class FTP;

    Operation() = default;

    virtual ~Operation() = default;

    virtual bool operator()(F &f) = 0;

    virtual bool process(int fd) {
        throw std::runtime_error("Empty process");
    }
};

template <class F>
class Noop : public Operation<F> {
public:
    bool operator()(F &f) override {
        if (!read_till_end(f.in).empty()) {
            MultiLine(f.out, 500) << "Syntax error. Extra data found." << NewLine()
                                  << "      ,~~.    " << NewLine()
                                  << "     (  9 )-_," << NewLine()
                                  << "(\\___ )=='-'  " << NewLine()
                                  << " \\ .   ) )    " << NewLine()
                                  << "  \\ `-' /     " << NewLine()
                                  << "   `~j-'      " << NewLine()
                                  << "     \"=:      " << NewLine()
                                  << "--------------" << NewLine()
                                  << LastLine() << "Krya krya";
            return true;
        }
        SingleLine(f.out, 200) << "OK.";
        return true;
    }
};

template <class F>
class Help : public Operation<F> {
public:
    bool operator()(F &f) override {
        read_till_end(f.in);
        auto out = MultiLine(f.out, 214);
        out << "You can use following queries:" << NewLine{};
        int cnt = 5;
        for (auto &&it : f.functions) {
            out << it.first;
            if (cnt-- == 0) {
                cnt = 5;
                out << NewLine{};
            } else {
                out << ' ';
            }
        }
        if (cnt != 5) {
            out << NewLine{};
        }
        out << LastLine{} << "Have a nice day dude!";
        return true;
    }
};

template <class F>
class Quit : public Operation<F> {
public:
    bool operator()(F &f) override {
        if (!read_till_end(f.in).empty()) {
            SingleLine(f.out, 500) << "Syntax error. Extra data found.";
            return true;
        }
        SingleLine(f.out, 221) << "Bye";
        return false;
    }
};

template <class F>
class LoginNeed : public Operation<F> {
public:
    bool operator()(F &f) override {
        read_till_end(f.in);
        SingleLine(f.out, 530) << "Please log in.";
        return true;
    }
};

template <class F>
class NoFunc : public Operation<F> {
public:
    bool operator()(F &f) override {
        read_till_end(f.in);
        SingleLine(f.out, 502) << "No such command.";
        return true;
    }
};

template <class F>
class Abort : public Operation<F> {
public:
    bool operator()(F &f) override {
        read_till_end(f.in);
        if (f.data_connect.is_ready()) {
            f.data_connect.abort();
            SingleLine(f.out, 225) << "Aborted successfully.";
            return true;
        }
        if (f.data_connect.is_done()) {
            SingleLine(f.out, 502) << "No active data connection.";
        }
        SingleLine(f.out, 226) << "Aborted successfully.";
        return true;
    }
};

template <class F>
class Port : public Operation<F> {
public:
    bool operator()(F &f) override {
        if (!f.data_connect.is_done()) {
            read_till_end(f.in);
            SingleLine(f.out, 530) << "Already running other";
            return true;
        }
        unsigned h1, h2, h3, h4, p1, p2;
        if (!(f.in >> h1)) {
            read_till_end(f.in);
            SingleLine(f.out, 501) << "Bad format.";
            return true;
        }
        if (f.in.peek() != ',') {
            read_till_end(f.in);
            SingleLine(f.out, 501) << "Bad format.";
            return true;
        }
        f.in.get();
        if (!(f.in >> h2)) {
            read_till_end(f.in);
            SingleLine(f.out, 501) << "Bad format.";
            return true;
        }
        if (f.in.peek() != ',') {
            read_till_end(f.in);
            SingleLine(f.out, 501) << "Bad format.";
            return true;
        }
        f.in.get();
        if (!(f.in >> h3)) {
            read_till_end(f.in);
            SingleLine(f.out, 501) << "Bad format.";
            return true;
        }
        if (f.in.peek() != ',') {
            read_till_end(f.in);
            SingleLine(f.out, 501) << "Bad format.";
            return true;
        }
        f.in.get();
        if (!(f.in >> h4)) {
            read_till_end(f.in);
            SingleLine(f.out, 501) << "Bad format.";
            return true;
        }
        if (f.in.peek() != ',') {
            read_till_end(f.in);
            SingleLine(f.out, 530) << "Bad format";
            return true;
        }
        f.in.get();
        if (!(f.in >> p1)) {
            read_till_end(f.in);
            SingleLine(f.out, 501) << "Bad format.";
            return true;
        }
        if (f.in.peek() != ',') {
            read_till_end(f.in);
            SingleLine(f.out, 501) << "Bad format.";
            return true;
        }
        f.in.get();
        if (!(f.in >> p2)) {
            read_till_end(f.in);
            SingleLine(f.out, 501) << "Bad format.";
            return true;
        }
        if (!read_till_end(f.in).empty()) {
            SingleLine(f.out, 501) << "Bad format. Extra data found.";
        }
        unsigned ip = (h1 << 24u) + (h2 << 16u) + (h3 << 8u) + h4;
        unsigned port = (p1 << 8u) + p2;
        if (!f.create_data_connect_out(ip, port)) {
            SingleLine(f.out, 500) << "Connection error.";
            return true;
        }
        SingleLine(f.out, 200) << "Success.";
        return true;
    }
};

/// TODO From here

template <class F>
class Pasv : public Operation<F> {
public:
    bool operator()(F &f) override {
        if (!f.data_connect.is_done()) {
            read_till_end(f.in);
            SingleLine(f.out, 530) << "Already running other";
            return true;
        }
        unsigned port = 10000;// + rand() % 10;
        std::cout << port << std::endl;
        SingleLine(f.out, 227) << "Passive mode (0,0,0,0,"
                           << (port >> 8u) << ',' << (port & ((1u << 8u) - 1)) << ")";
        if (!f.create_data_connect_in(port)) {
            SingleLine(f.out, 530) << "ERROR";
            return true;
        }
        SingleLine(f.out, 227) << "Success";
        return true;
    }
};

template <class F>
class List : public Operation<F> {
public:
    bool operator()(F &f) override {
        path = read_till_end(f.in);
        if (!f.check_data_connect()) {
            return true;
        }
        if (!f.data_connect.process(this)) {
            SingleLine(f.out, 0) << ":(";
            return true;
        }
        SingleLine(f.out, 0) << "Success";
        return true;
    }

    bool process(int fd) override {
        FDOStream out(fd);
        if (path.empty()) {
            return run_command("ls -la ", out);
        }
        return run_command("ls -la " + path, out);
    }

    std::string path;
};

template <class F>
class Retr : public Operation<F> {
public:
    bool operator()(F &f) override {
        path = read_till_end(f.in);
        if (!f.check_data_connect()) {
            return true;
        }
        if (!f.data_connect.is_ready()) {
            SingleLine(f.out, 0) << "Open connection firstly by PASV or PORT.";
            return true;
        }
        if (path.empty()) {
            SingleLine(f.out, 0) << "Path should be specified";
            return true;
        }
        if (!f.data_connect.process(this)) {
            SingleLine(f.out, 0) << ":(";
            return true;
        }
        SingleLine(f.out, 0) << "Success";
        return true;
    }

    bool process(int fd) override {
        FDOStream out(fd);
        return run_command("cat " + path, out);
    }

    std::string path;
};

template <class F>
class Stor : public Operation<F> {
public:
    bool operator()(F &f) override {
        path = read_till_end(f.in);
        if (!f.check_data_connect()) {
            return true;
        }
        if (path.empty()) {
            SingleLine(f.out, 0) << "Path should be specified";
            return true;
        }
        if (!f.data_connect.process(this)) {
            SingleLine(f.out, 0) << ":(";
            return true;
        }
        SingleLine(f.out, 0) << "Success";
        return true;
    }

    bool process(int fd) override {
        FDIStream in(fd);
        return write_file(path, in);
    }

    std::string path;
};

template <class F>
class Sleep : public Operation<F> {
public:
    bool operator()(F &f) override {
        auto path = read_till_end(f.in);
        if (!f.check_data_connect()) {
            return true;
        }
        if (!f.data_connect.process(this)) {
            SingleLine(f.out, 0) << ":(";
            return true;
        }
        SingleLine(f.out, 0) << "Success";
        return true;
    }

    bool process(int fd) override {
        FDOStream out(fd);
        return run_command("sleep 20", out);
    }
};

std::map<std::string, std::string> passes;

void read_db(const std::string &filename) {
    passes.clear();
    std::ifstream file;
    file.open(filename, std::ios_base::in);
    std::string line;
    std::getline(file, line);
    while (!file.eof()) {
        std::getline(file, line);
        if (line.back() == '\n') {
            line.pop_back();
        }
        auto pos = line.find('\t');
        if (line.find('\t', pos + 1) != std::string::npos) {
            throw std::runtime_error("Bad file format");
        }
        passes[line.substr(0, pos)] = line.substr(pos + 1);
    }
}

// Global? Yes, it is.
pam_response *reply{};

int function_conversation(int num_msg, const struct pam_message **msg,
                          struct pam_response **resp, void *appdata_ptr) {
    *resp = reply;
    return PAM_SUCCESS;
}

template <class F>
class Pass : public Operation<F> {
    bool check_password(const std::string &user, const std::string &pass) const {
        if (user == "anonymous") {
            return true;
        }
        auto it = passes.find(user);
        if (it == passes.end()) {
            return false;
        }
        return it->second == pass;
    }

    bool check_password_pam(const std::string &user_id, std::string &pass) const {
        if (user_id == "anonymous") {
            return true;
        }
        auto it = passes.find(user_id);
        if (it == passes.end()) {
            return false;
        }
        std::stringstream ss;
        run_command("getent passwd " + user_id + " | cut -d: -f1", ss);
        auto user = ss.str();
        std::cout << user << std::endl;
        user[user.size() - 1] = '\0';

        pam_handle_t *local_auth_handle = nullptr;
        const pam_conv local_conversation = { function_conversation, nullptr };
        int retval = pam_start("common-auth", user.c_str(), &local_conversation, &local_auth_handle);
        if (retval != PAM_SUCCESS) {
            return false;
        }
        reply = (pam_response*)malloc(sizeof(*reply));
        reply->resp = (char*)malloc(pass.size() + 1);
        memcpy(reply->resp, pass.data(), pass.size());
        reply->resp[pass.size()] = '\0';
        reply->resp_retcode = 0;
        retval = pam_authenticate(local_auth_handle, 0);
        pam_end(local_auth_handle, retval);
        return retval == PAM_SUCCESS;
    }

public:
    bool operator()(F &f) override {
        auto password = read_till_end(f.in);
        if (check_password_pam(f.username, password)) {
            f.functions.erase("PASS");
            f.functions["PORT"] = std::make_unique<Port<F>>();
            f.functions["PASV"] = std::make_unique<Pasv<F>>();
            f.functions["ABOR"] = std::make_unique<Abort<F>>();

            f.functions["NOOP"] = std::make_unique<Noop<F>>();
            f.functions["LIST"] = std::make_unique<List<F>>();
            f.functions["RETR"] = std::make_unique<Retr<F>>();
            f.functions["STOR"] = std::make_unique<Stor<F>>();
            f.functions["SLEEP"] = std::make_unique<Sleep<F>>();
            f.default_function = std::make_unique<NoFunc<F>>();
            SingleLine(f.out, 230) << "Success.";
        }
        SingleLine(f.out, 530) << "Access denied.";
        return true;
    }
};

template <class F>
class User : public Operation<F> {
public:
    bool operator()(F &f) override {
        f.username = read_till_end(f.in);
        SingleLine(f.out, 331) << "Need password";
        f.functions.clear();
        f.functions["PASS"] = std::make_unique<Pass<F>>();
        f.functions["USER"] = std::make_unique<User<F>>();
        f.functions["HELP"] = std::make_unique<Help<F>>();
        f.functions["QUIT"] = std::make_unique<Quit<F>>();
        return true;
    }
};

template <class F>
class DataConnect {
    enum class State {
        kNone = 0,
        kReadyIn,
        kReadyOut,
        kExecution,
    };

    int uid = -1;
    unsigned ip{};
    unsigned port{};
    State state = State::kNone;

    int child_;
public:
    DataConnect() = default;

    bool is_ready() {
        return state != State::kNone;
    }

    bool process(Operation<F>* op) {
        /// @TODO uid
        if (!is_ready()) {
            throw std::runtime_error("Not valid process");
        }
        auto pid = fork();
        if (pid < 0) {
            return false;
        }
        if (pid == 0) {
            if (uid != -1) {
                if (setuid(uid) != 0) {
                    exit(5);
                }
            }
            conn_ = open_connection();
            if (!op->process(conn_)) {
                std::cerr << "Internal error" << std::endl;
                exit(1);
            }
            exit(0);
        } else {
            child_ = pid;
            state = State::kExecution;
        }
        return true;
    }
};

template <class F>
class DataConnectOld {
public:
    DataConnect() = default;

    explicit DataConnect(int conn) : conn_(conn) { };

    DataConnect& operator=(DataConnect &&other) noexcept {
        kill();
        if (conn_ != -1) {
            close(conn_);
        }
        conn_ = other.conn_;
        other.conn_ = -1;
        child_ = other.child_;
        other.child_ = -1;
        if (other.server_) {
            server_ = std::move(other.server_.value());
        }
        return *this;
    }

    ~DataConnect() {
        kill();
        if (conn_ != -1) {
            close(conn_);
        }
    }

    bool is_ready() {
        return conn_ != -1;
    }

    bool is_done() {
        if (conn_ != -1) {
            return false;
        }
        if (child_ == -1) {
            return true;
        }
        int status;
        std::cout << waitpid(child_, &status, WNOHANG) << ' ' << status << std::endl;
        std::cout << ::kill(child_, 0) << ' ' << child_ << std::endl;
        return ::kill(child_, 0) != 0;
    }

    void kill() {
        if (child_ == -1) {
            return;
        }
        int status;
        std::cout << ::kill(child_, SIGABRT) << '-' << child_ << std::endl;
        std::cout << waitpid(child_, &status, 0) << '-' << status << std::endl;
    }

    bool abort() {
        if (conn_ != -1) {
            close(conn_);
            conn_ = -1;
            server_.reset();
            kill();
            return true;
        }
        kill();
        return false;
    }

    void set_server(Server<int> &&s) {
        server_ = std::move(s);
    }

private:
    int conn_ = -1;
    pid_t child_ = -1;
    std::optional<Server<int>> server_{};
};

class FTP {
public:
    explicit FTP(int fd) : in(fd), out(fd) {
        in.dismiss();
        struct timeval timeout{30, 0};
        if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout)) == -1) {
            close(fd);
            throw std::runtime_error("Can not set rcv timeout");
        }
        if (setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, (char *)&timeout, sizeof(timeout)) == -1) {
            close(fd);
            throw std::runtime_error("Can not set snd timeout");
        }
        functions["USER"] = std::make_unique<User<FTP>>();
        functions["HELP"] = std::make_unique<Help<FTP>>();
        functions["QUIT"] = std::make_unique<Quit<FTP>>();
        default_function = std::make_unique<LoginNeed<FTP>>();
    }

    bool create_data_connect_out(unsigned ip, unsigned port) {
        int sock;
        if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
            return false;
        }
        struct sockaddr_in serv_addr{};
        serv_addr.sin_family = AF_INET;
        serv_addr.sin_port = htons(port);
        serv_addr.sin_addr.s_addr = htonl(ip);
        if (connect(sock, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
            close(sock);
            return false;
        }
        data_connect = DataConnect<FTP>{sock};
        return true;
    }

    bool create_data_connect_in(unsigned port);

    bool check_data_connect() {
        if (data_connect.is_ready()) {
            return true;
        }
        if (data_connect.is_done()) {
            SingleLine(out, 425) << "Open data connection firstly by PASV or PORT.";
            return false;
        }
        SingleLine(out, 125) << "Data connection already open; transferring started.";
        return false;
    }

//
//    bool type() {
//        char t;
//        in >> t;
//        switch (t) {
//            case 'A':
//            case 'I':
//                out << "200 Type changed to " << t << "\r\n";
//            default:
//                out << "000 Error: Unsupported type\r\n";
//                return true;
//        }
//        return false;
//    }

    std::map<std::string, std::unique_ptr<Operation<FTP>>> functions;
    std::unique_ptr<Operation<FTP>> default_function;

    void run() {
        SingleLine(out, 220) << "Igor Mineev Server Ready.";
        std::string command;
        while (in >> command) {
            // Skip space after command;
            if (in.peek() == ' ') {
                in.get();
            }
            std::transform(command.begin(), command.end(), command.begin(),
                           [](unsigned char c){ return std::toupper(c); });
            auto it = functions.find(command);
            if (it != functions.end()) {
                if (!(*it->second)(*this)) {
                    break;
                }
                continue;
            }
            (*default_function)(*this);
        }
    }

    FDIStream in;
    FDOStream out;
    std::string username;
    DataConnect<FTP> data_connect;
};

bool FTP::create_data_connect_in(unsigned port) {
    try {
        Server<int> server(port, 1);
        auto client = server.accept_one();
        if (client < 0) {
            return false;
        }
        data_connect = DataConnect<FTP>{client};
        data_connect.set_server(std::move(server));
        return true;
    } catch (const std::exception &e) {
        return false;
    }
}


int main() {
    read_db("../passes");
    Server<FTP>(8080).run();
}
