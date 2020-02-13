#include <iostream>
#include <array>
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
#include <arpa/inet.h>
#include <wait.h>
#include <security/pam_appl.h>
#include <security/pam_misc.h>


#include <algorithm>
#include <sys/stat.h>

#include "server.hpp"
#include "tools.hpp"

template <class F>
class Operation {
public:
    friend class FTP;

    Operation() = default;

    virtual ~Operation() = default;

    virtual bool operator()(F &f) = 0;

    virtual bool process(FDOStream &control, int fd, ModeType mode) {
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
        if (f.data_connect.is_done()) {
            SingleLine(f.out, 502) << "No active data connection.";
        }
        f.data_connect.kill();
        if (f.data_connect.is_ready()) {
            SingleLine(f.out, 225) << "Aborted successfully.";
            return true;
        }
        SingleLine(f.out, 226) << "Aborted successfully.";
        return true;
    }
};

template <class F>
class Type : public Operation<F> {
public:
    bool operator()(F &f) override {
        auto type = read_till_end(f.in);
        if (!type.empty()) {
            type[0] = toupper(type[0]);
        }
        if (type.size() > 1) {
            type[1] = toupper(type[1]);
        }
        if (type.size() > 2) {
            type[2] = toupper(type[2]);
        }
        if (type == "AN") {
            SingleLine(f.out, 200) << "OK.";
            return true;
        }
        if (type == "A") {
            SingleLine(f.out, 200) << "OK.";
            return true;
        }
        if (type == "L 8") {
            SingleLine(f.out, 200) << "OK.";
            return true;
        }
        SingleLine(f.out, 504) << "Only 8bit ASCII non-print supported, not " + type + ".";
        return true;
    }
};

template <class F>
class Mode : public Operation<F> {
public:
    bool operator()(F &f) override {
        auto type = read_till_end(f.in);
        if (!type.empty()) {
            type[0] = toupper(type[0]);
        }
        if (type == "S") {
            f.mode = ModeType::Stream;
            SingleLine(f.out, 200) << "OK.";
            return true;
        }
        if (type == "B") {
            f.mode = ModeType::Block;
            SingleLine(f.out, 200) << "OK.";
            return true;
        }
        if (type == "C") {
            f.mode = ModeType::Compressed;
            SingleLine(f.out, 200) << "OK.";
            return true;
        }
        SingleLine(f.out, 500) << "Unknown mode.";
        return true;
    }
};

template <class F>
class Stru : public Operation<F> {
public:
    bool operator()(F &f) override {
        auto type = read_till_end(f.in);
        if (!type.empty()) {
            type[0] = toupper(type[0]);
        }
        if (type == "F") {  /// File
            SingleLine(f.out, 200) << "OK.";
            return true;
        }
        if (type == "R") {  /// Record. @TODO
            SingleLine(f.out, 504) << "Not OK.";
            return true;
        }
        if (type == "P") {  /// Page
            SingleLine(f.out, 504) << "Not OK.";
            return true;
        }
        SingleLine(f.out, 500) << "Unknown structure.";
        return true;
    }
};

template <class F>
class CDUp : public Operation<F> {
public:
    bool operator()(F &f) override {
        if (!read_till_end(f.in).empty()) {
            SingleLine(f.out, 501) << "Arguments not expected.";
            return true;
        }
        chdir("..");
        SingleLine(f.out, 200) << "OK.";
        return true;
    }
};

template <class F>
class CWD : public Operation<F> {
public:
    bool operator()(F &f) override {
        path = read_till_end(f.in);
        if (path.empty()) {
            SingleLine(f.out, 501) << "Path should be specified.";
            return true;
        }
        int status;
        if (!f.run_without_data_connect(this, &status)) {
            SingleLine(f.out, 421) << "Internal error";
            return true;
        }
        if (status != 0) {
            SingleLine(f.out, 550) << "Incorrect path.";
            return true;
        }
        if (chdir(path.c_str()) != 0) {
            SingleLine(f.out, 550) << "Incorrect path.";
            return true;
        }
        SingleLine(f.out, 250) << "OK.";
        return true;
    }

    bool process(FDOStream&, int, ModeType) override {
        /// Kostyil'. Checking that has access by uid
        if (chdir(path.c_str()) != 0) {
            exit(1);
        }
        exit(0);
    }

    std::string path;
};

template <class F>
class RMD : public Operation<F> {
public:
    bool operator()(F &f) override {
        path = read_till_end(f.in);
        if (path.empty()) {
            SingleLine(f.out, 501) << "Path should be specified.";
            return true;
        }
        if (!f.run_without_data_connect(this)) {
            SingleLine(f.out, 421) << "Internal error";
            return true;
        }
        return true;
    }

    bool process(FDOStream &out, int, ModeType) override {
        if (!check_folder_exists_access(path)) {
            SingleLine(out, 550) << "Incorrect path.";
            return true;
        }
        std::ostringstream ss{};
        run_command("rm -r " + path, ss);
        SingleLine(out, 250) << "OK.";
        return true;
    }

    std::string path;
};

template <class F>
class MKD : public Operation<F> {
public:
    bool operator()(F &f) override {
        path = read_till_end(f.in);
        if (path.empty()) {
            SingleLine(f.out, 501) << "Path should be specified.";
            return true;
        }
        if (!f.run_without_data_connect(this)) {
            SingleLine(f.out, 421) << "Internal error";
            return true;
        }
        return true;
    }

    bool process(FDOStream &out, int, ModeType) override {
        if (check_folder_exists_access(path)) {
            SingleLine(out, 550) << "Path already exists.";
            return true;
        }
        if (mkdir(path.c_str(), 0600) != 0) {
            SingleLine(out, 550) << "No access.";
            return true;
        }
        SingleLine(out, 257) << "OK.";
        return true;
    }

    std::string path;
};

template <class F>
class Dele : public Operation<F> {
public:
    bool operator()(F &f) override {
        path = read_till_end(f.in);
        if (path.empty()) {
            SingleLine(f.out, 501) << "Path should be specified.";
            return true;
        }
        if (!f.run_without_data_connect(this)) {
            SingleLine(f.out, 421) << "Internal error";
            return true;
        }
        return true;
    }

    bool process(FDOStream &out, int, ModeType) override {
        if (!check_file_read_access(path)) {
            SingleLine(out, 550) << "Incorrect path.";
            return true;
        }
        if (!check_file_write_access(path)) {
            SingleLine(out, 550) << "Incorrect path.";
            return true;
        }
        std::ostringstream ss{};
        run_command("rm " + path, ss);
        SingleLine(out, 250) << "OK.";
        return true;
    }

    std::string path;
};

template <class F>
class Port : public Operation<F> {
public:
    bool operator()(F &f) override {
        if (!f.data_connect.is_done()) {
            read_till_end(f.in);
            SingleLine(f.out, 500) << "Already running other";
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
            SingleLine(f.out, 501) << "Bad format";
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
            return true;
        }
        unsigned ip = (h1 << 24u) + (h2 << 16u) + (h3 << 8u) + h4;
        unsigned port = (p1 << 8u) + p2;
        std::cerr << "Conn to " << ip << ' ' << port << std::endl;
        if (!f.data_connect.set_active(ip, port)) {
            SingleLine(f.out, 500) << "Internal error.";
            return true;
        }
        SingleLine(f.out, 200) << "Success.";
        return true;
    }
};

template <class F>
class Pasv : public Operation<F> {
public:
    bool operator()(F &f) override {
        if (!f.data_connect.is_done()) {
            read_till_end(f.in);
            SingleLine(f.out, 500) << "Already running other";
            return true;
        }
        unsigned port = 10000;// + rand() % 10;
        std::cerr << "Listen on " << port << std::endl;
        try {
            Server<void> server(f.settings.bind_host, port, 1);
            if (!f.data_connect.set_passive(std::move(server))) {
                SingleLine(f.out, 500) << "Internal error.";
                return true;
            }
        } catch (const std::exception &e) {
            std::cerr << e.what();
            SingleLine(f.out, 500) << "Internal error";
            return true;
        }
        auto addr = f.settings.bind_host;
        std::transform(addr.begin(), addr.end(), addr.begin(),
                       [](unsigned char c){ return c == '.' ? ',' : c; });
        SingleLine(f.out, 227) << "Passive mode (" + addr + ","
                               << (port >> 8u) << ',' << (port & ((1u << 8u) - 1)) << ")";
        return true;
    }
};

template <class F>
class List : public Operation<F> {
public:
    List(std::string command_="ls -l", std::string postfix_="")
        : command(std::move(command_))
        , postfix(std::move(postfix_)){
        command.push_back(' ');
    }

    bool operator()(F &f) override {
        path = read_till_end(f.in);
        if (!f.check_data_connect()) {
            return true;
        }
        if (!check_folder_exists_access(path.empty() ? "." : path)) {
            SingleLine(f.out, 450) << "No such folder.";
            return true;
        }
        if (!f.data_connect.process(f.out.get_fd(), this, f.mode)) {
            /// Kostyil
            SingleLine(f.out, 150) << "No ways to leave.";
            SingleLine(f.out, 451) << "No ways to live.";
            return true;
        }
        SingleLine(f.out, 150) << "Successfully started.";
        return true;
    }

    bool process(FDOStream &control, int fd, ModeType mode) override {
        std::string cmd = command + path + postfix;
        bool result;
        if (mode == ModeType::Stream) {
            FDOStream out(fd);
            result = run_command(cmd, out);
        } else if (mode == ModeType::Block) {
            ModeBlockOStream out(fd);
            result = run_command(cmd, out);
        } else {
            ModeCompressedOStream out(fd);
            result = run_command(cmd, out);
        }
        if (result) {
            SingleLine(control, 226) << "Success";
        }
        return result;
    }

    std::string path;
    std::string command;
    std::string postfix;
};

template <class F>
class Retr : public Operation<F> {
public:
    bool operator()(F &f) override {
        path = read_till_end(f.in);
        if (!f.check_data_connect()) {
            return true;
        }
        if (path.empty()) {
            SingleLine(f.out, 501) << "Path should be specified.";
            return true;
        }
        if (!check_file_read_access(path)) {
            SingleLine(f.out, 550) << "No access.";
            return true;
        }
        if (!f.data_connect.process(f.out.get_fd(), this, f.mode)) {
            /// Kostyil
            SingleLine(f.out, 150) << "No ways to leave.";
            SingleLine(f.out, 451) << "No ways to live.";
            return true;
        }
        SingleLine(f.out, 150) << "Successfully started.";
        return true;
    }

    bool process(FDOStream &control, int fd, ModeType mode) override {
        bool result;
        if (mode == ModeType::Stream) {
            FDOStream out(fd);
            result = run_command("cat " + path, out);
        } else if (mode == ModeType::Block) {
            ModeBlockOStream out(fd);
            result = run_command("cat " + path, out);
        } else {
            ModeCompressedOStream out(fd);
            result = run_command("cat " + path, out);;
        }
        if (result) {
            SingleLine(control, 226) << "Success.";
        }
        return result;
    }

    std::string path;
};

template <class F>
class iStor : public Operation<F> {
public:
    iStor(int mode = O_CREAT) : filemode(mode) { }

    bool operator()(F &f) override {
        path = read_till_end(f.in);
        if (!f.check_data_connect()) {
            return true;
        }
        if (path.empty()) {
            SingleLine(f.out, 501) << "Path should be specified.";
            return true;
        }
        if (!check_file_write_access(path, filemode)) {
            SingleLine(f.out, 550) << "No access.";
            return true;
        }
        if (!f.data_connect.process(f.out.get_fd(), this, f.mode)) {
            /// Kostyil
            SingleLine(f.out, 150) << "No ways to leave.";
            SingleLine(f.out, 451) << "No ways to live.";
            return true;
        }
        SingleLine(f.out, 150) << "Successfully started.";
        return true;
    }

    bool process(FDOStream &control, int fd, ModeType mode) override {
        bool result;
        if (mode == ModeType::Stream) {
            FDIStream in(fd);
            result = write_file(path, filemode, in);
        } else if (mode == ModeType::Block) {
            ModeBlockIStream in(fd);
            result = write_file(path, filemode, in);
        } else {
            ModeCompressedIStream in(fd);
            result = write_file(path, filemode, in);
        }
        if (result) {
            SingleLine(control, 226) << "Success.";
        }
        return result;
    }

    std::string path;
    int filemode;
};

template <class F>
class Sleep : public Operation<F> {
public:
    bool operator()(F &f) override {
        read_till_end(f.in);
        if (!f.check_data_connect()) {
            return true;
        }
        if (!f.data_connect.process(f.out.get_fd(), this, f.mode)) {
            /// Kostyil
            SingleLine(f.out, 150) << "No ways to leave.";
            SingleLine(f.out, 451) << "No ways to live.";
            return true;
        }
        SingleLine(f.out, 150) << "Successfully started.";
        return true;
    }

    bool process(FDOStream &control, int fd, ModeType mode) override {
        bool result;
        if (mode == ModeType::Stream) {
            FDOStream out(fd);
            result = run_command("sleep 20", out);
        } else if (mode == ModeType::Block) {
            ModeBlockOStream out(fd);
            result = run_command("sleep 20", out);
        } else {
            ModeCompressedOStream out(fd);
            result = run_command("sleep 20", out);
        }
        if (result) {
            SingleLine(control, 226) << "Success.";
        }
        return result;
    }
};

// Global? Yes, it is.
pam_response *reply{};

int function_conversation(int num_msg, const struct pam_message **msg,
                          struct pam_response **resp, void *appdata_ptr) {
    *resp = reply;
    return PAM_SUCCESS;
}

template <class F>
class Pass : public Operation<F> {
    bool check_password(const std::string &user, const std::string &pass, F &f) const {
        f.data_connect.set_uid(-1);
        f.uid = -1;
        if (user == "anonymous") {
            return true;
        }
        auto it = f.settings.passes.find(user);
        if (it == f.settings.passes.end()) {
            return false;
        }
        return it->second == pass;
    }

    bool check_password_pam(const std::string &user_id, std::string &pass, F &f) const {
        f.data_connect.set_uid(-1);
        f.uid = -1;
        if (user_id == "anonymous") {
            return true;
        }
        auto it = f.settings.passes.find(user_id);
        if (it == f.settings.passes.end()) {
            return false;
        }
        if (!std::all_of(user_id.begin(), user_id.end(), [](char c){ return isdigit(c); })) {
            return false;
        }
        auto uid = std::stoi(user_id, nullptr, 10);
        if (uid < 0) {
            return false;
        }
        std::stringstream ss;
        run_command("getent passwd " + user_id + " | cut -d: -f1", ss);
        auto user = ss.str();
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
        if (retval == PAM_SUCCESS) {
            // Update user info
            f.username = user;
            f.data_connect.set_uid(uid);
            f.uid = uid;
        }
        return retval == PAM_SUCCESS;
    }

public:
    bool operator()(F &f) override {
        auto password = read_till_end(f.in);
        if (check_password_pam(f.username, password, f)) {
            f.functions.erase("PASS");
            f.add_user_functions();
            SingleLine(f.out, 230) << "Success.";
            return true;
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
        kExecution
    };

    uid_t uid = std::numeric_limits<decltype(uid)>::max();
    unsigned ip{};
    unsigned port{};
    State state = State::kNone;
    std::optional<Server<void>> server{};

    pid_t child_ = -1;

    int open_data_connection() {
        if (state == State::kReadyOut) {
            int sock;
            if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
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
        if (state == State::kReadyIn) {
             return server->accept_one();
        }
        return -1;
    }

public:
    DataConnect() = default;

    DataConnect(DataConnect &&other) = delete;
    DataConnect(const DataConnect &other) = delete;
    DataConnect& operator=(DataConnect &&other) = delete;
    DataConnect& operator=(const DataConnect &other) = delete;

    ~DataConnect() {
        kill();
    }

    void kill() {
        server.reset();
        if (child_ == -1) {
            return;
        }
        int st;
        ::kill(child_, SIGABRT);
        waitpid(child_, &st, 0);
        state = State::kNone;
        child_ = -1;
    }

    bool is_ready() {
        return state == State::kReadyIn || state == State::kReadyOut;
    }

    bool is_done() {
        if (state == State::kNone) {
            return true;
        }
        if (is_ready()) {
            return false;
        }
        int status;
        waitpid(child_, &status, WNOHANG);
        if (::kill(child_, 0) != 0) {
            state = State::kNone;
            return true;
        }
        return false;
    }

    bool process(int fd_control, Operation<F>* op, ModeType mode) {
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
            int user = open_data_connection();
            FDOStream control(fd_control);
            if (user == -1) {
                SingleLine(control, 425) << "Can not open data connection";
                exit(6);
            }
            if (!op->process(control, user, mode)) {
                SingleLine(control, 451) << "Internal Error";
                exit(1);
            }
            exit(0);
        } else {
            child_ = pid;
            state = State::kExecution;
            server.reset();
        }
        return true;
    }

    void set_uid(uid_t uid_) {
        uid = uid_;
    }

    bool set_passive(Server<void> &&s) {
        if (state != State::kNone) {
            return false;
        }
        server = std::move(s);
        state = State::kReadyIn;
        return true;
    }

    bool set_active(unsigned ip_, unsigned port_) {
        if (state != State::kNone) {
            return false;
        }
        ip = ip_;
        port = port_;
        state = State::kReadyOut;
        return true;
    }
};

class FTP {
public:
    struct Settings {
        std::string default_dir;
        std::string bind_host;
        std::map<std::string, std::string> passes;
        bool need_login;
    };

    explicit FTP(int fd, const Settings &settings_) : in(fd), out(fd), settings(settings_) {
        in.dismiss();
        struct timeval timeout{30, 0};
        if (!set_timeout_fd(fd, SO_RCVTIMEO)) {
            close(fd);
            throw std::runtime_error("Can not set rcv timeout");
        }
        if (!set_timeout_fd(fd, SO_SNDTIMEO)) {
            close(fd);
            throw std::runtime_error("Can not set snd timeout");
        }
        functions["HELP"] = std::make_unique<Help<FTP>>();
        functions["QUIT"] = std::make_unique<Quit<FTP>>();
        if (settings.need_login) {
            functions["USER"] = std::make_unique<User<FTP>>();
            default_function = std::make_unique<LoginNeed<FTP>>();
        } else {
            add_user_functions();
        }
    }

    void add_user_functions() {
        functions["PORT"] = std::make_unique<Port<FTP>>();
        functions["PASV"] = std::make_unique<Pasv<FTP>>();
        functions["ABOR"] = std::make_unique<Abort<FTP>>();

        functions["TYPE"] = std::make_unique<Type<FTP>>();
        functions["MODE"] = std::make_unique<Mode<FTP>>();
        functions["STRU"] = std::make_unique<Stru<FTP>>();

        functions["NOOP"] = std::make_unique<Noop<FTP>>();
        functions["LIST"] = std::make_unique<List<FTP>>("ls -l", " | tail +2");
        functions["RETR"] = std::make_unique<Retr<FTP>>();
        functions["STOR"] = std::make_unique<iStor<FTP>>();

        functions["CDUP"] = std::make_unique<CDUp<FTP>>();
        functions["CWD"] = std::make_unique<CWD<FTP>>();
        functions["APPE"] = std::make_unique<iStor<FTP>>(O_CREAT | O_APPEND);
        functions["DELE"] = std::make_unique<Dele<FTP>>();
        functions["RMD"] = std::make_unique<RMD<FTP>>();
        functions["MKD"] = std::make_unique<MKD<FTP>>();
        functions["NLST"] = std::make_unique<List<FTP>>("ls -1");

        functions["NOOP"] = std::make_unique<Noop<FTP>>();
        functions["SLEEP"] = std::make_unique<Sleep<FTP>>();
        default_function = std::make_unique<NoFunc<FTP>>();
    }

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

    bool run_without_data_connect(Operation<FTP> *op, int *status=nullptr) {
        auto pid = fork();
        if (pid < 0) {
            return false;
        }
        if (pid == 0) {
            if (uid != -1) {
                if (setuid(uid) != 0) {
                    SingleLine(out, 421) << "Internal Error";
                    exit(5);
                }
            }
            if (!op->process(out, -1, ModeType::Stream)) {
                SingleLine(out, 421) << "Internal Error";
                exit(1);
            }
            exit(0);
        }
        int st;
        waitpid(pid, status ? status : &st, 0);
        return true;
    }

    std::map<std::string, std::unique_ptr<Operation<FTP>>> functions;
    std::unique_ptr<Operation<FTP>> default_function;

    void run() {
        if (chdir(settings.default_dir.c_str()) != 0) {
            std::cerr << "Directory set initial failed\n";
            exit(1);
        }
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
        if (!in.good()) {
            SingleLine(out, 421) << "Timeout.";
        }
    }

    FDIStream in;
    FDOStream out;
    std::string username;
    DataConnect<FTP> data_connect;
    uid_t uid = -1;
    ModeType mode = ModeType::Stream;

    const Settings &settings;
};

void empty(int signum) { }

int main(int args, char **argv) {
    signal(SIGPIPE, empty);

    FTP::Settings settings{};

    settings.default_dir = parse_env_req("HW1_DIRECTORY");
    auto user_passes = parse_env_req("HW1_USERS");
    settings.bind_host = parse_env_req("HW1_HOST");
    auto bind_port = parse_env_req("HW1_PORT");
    auto is_disabled = parse_env("HW1_AUTH_DISABLED");

    auto [passes, need_login] = read_db(user_passes, is_disabled);
    settings.passes = std::move(passes);
    settings.need_login = need_login;

    if (!check_folder_exists_access(settings.default_dir)) {
        std::cerr << "No such dir " << settings.default_dir << std::endl;
        return 1;
    }

    Server<FTP>(settings.bind_host, strtol(bind_port.c_str(), nullptr, 10)).run(settings);
}
