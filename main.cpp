#include <algorithm>
#include <memory>
#include <optional>
#include <thread>
#include <chrono>

#include <sys/stat.h>
#include <security/pam_appl.h>
#include <security/pam_misc.h>
#include <wait.h>

#include "server.hpp"
#include "tools.hpp"

template <class F>
class Operation {
public:
    friend class FTP;

    Operation() = default;

    virtual ~Operation() = default;

    virtual bool operator()(F &f, std::string arg) = 0;

    virtual bool process(FDOStream &control, int fd, ModeType mode) {
        throw std::runtime_error("Empty process");
    }
};

template <class F>
class Noop : public Operation<F> {
public:
    bool operator()(F &f, std::string arg) override {
        if (!arg.empty()) {
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
    bool operator()(F &f, std::string arg) override {
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
    bool operator()(F &f, std::string arg) override {
        if (!arg.empty()) {
            SingleLine(f.out, 500) << "Syntax error. Extra data found.";
            return true;
        }
        SingleLine(f.out, 221) << "Bye";
        return false;
    }
};

template <class F>
class Abort : public Operation<F> {
public:
    bool operator()(F &f, std::string arg) override {
        if (!arg.empty()) {
            SingleLine(f.out, 500) << "Syntax error. Extra data found.";
            return true;
        }
        if (f.data_connect.is_done()) {
            SingleLine(f.out, 502) << "No active data connection.";
            return true;
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
    bool operator()(F &f, std::string type) override {
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
    bool operator()(F &f, std::string type) override {
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
    bool operator()(F &f, std::string type) override {
        if (!type.empty()) {
            type[0] = toupper(type[0]);
        }
        if (type == "F") {  /// File
            SingleLine(f.out, 200) << "OK.";
            return true;
        }
        if (type == "R") {  /// Record
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
    bool operator()(F &f, std::string arg) override {
        if (!arg.empty()) {
            SingleLine(f.out, 501) << "Arguments not expected.";
            return true;
        }
        full_working_path = f.settings.full_working_path;
        int status;
        if (!f.run_without_data_connect(this, &status)) {
            SingleLine(f.out, 421) << "Internal error";
            return true;
        }
        if (status != 0) {
            SingleLine(f.out, 550) << "No access.";
            return true;
        }
        chdir("..");
        SingleLine(f.out, 200) << "OK.";
        return true;
    }


    bool process(FDOStream&, int, ModeType) override {
        /// Kostyil'. Checking that has access by uid
        if (!check_folder_exists_access("..", full_working_path)) {
            exit(1);
        }
        if (chdir("..") != 0) {
            exit(1);
        }
        exit(0);
    }

    std::string full_working_path;
};

template <class F>
class CWD : public Operation<F> {
public:
    bool operator()(F &f, std::string arg) override {
        path = f.fix_abs_path(std::move(arg));
        if (path.empty()) {
            SingleLine(f.out, 501) << "Path should be specified.";
            return true;
        }
        full_working_path = f.settings.full_working_path;
        int status;
        if (!f.run_without_data_connect(this, &status)) {
            SingleLine(f.out, 421) << "Internal error";
            return true;
        }
        if (status != 0) {
            SingleLine(f.out, 550) << "No access.";
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
        if (!check_folder_exists_access(path, full_working_path)) {
            exit(1);
        }
        if (chdir(path.c_str()) != 0) {
            exit(1);
        }
        exit(0);
    }

    std::string path;
    std::string full_working_path;
};

template <class F>
class RMD : public Operation<F> {
public:
    bool operator()(F &f, std::string arg) override {
        path = f.fix_abs_path(std::move(arg));
        if (path.empty()) {
            SingleLine(f.out, 501) << "Path should be specified.";
            return true;
        }
        working_directory = f.settings.full_working_path;
        if (!f.run_without_data_connect(this)) {
            SingleLine(f.out, 421) << "Internal error";
            return true;
        }
        return true;
    }

    bool process(FDOStream &out, int, ModeType) override {
        if (!check_folder_exists_access(path, working_directory)) {
            SingleLine(out, 550) << "Incorrect path.";
            return true;
        }
        std::ostringstream ss{};
        run_command("rm -r '" + path + "'", ss);
        SingleLine(out, 250) << "OK.";
        return true;
    }

    std::string path;
    std::string working_directory;
};

template <class F>
class MKD : public Operation<F> {
public:
    bool operator()(F &f, std::string arg) override {
        path = f.fix_abs_path(std::move(arg));
        if (path.empty()) {
            SingleLine(f.out, 501) << "Path should be specified.";
            return true;
        }
        working_directory = f.settings.full_working_path;
        if (!f.run_without_data_connect(this)) {
            SingleLine(f.out, 421) << "Internal error";
            return true;
        }
        return true;
    }

    bool process(FDOStream &out, int, ModeType) override {
        if (check_folder_exists_access(path, working_directory)) {
            SingleLine(out, 550) << "Path already exists.";
            return true;
        }
        if (mkdir(path.c_str(), 0700) != 0) {
            SingleLine(out, 550) << "No access.";
            return true;
        }
        SingleLine(out, 257) << "OK.";
        return true;
    }

    std::string path;
    std::string working_directory;
};

template <class F>
class Dele : public Operation<F> {
public:
    bool operator()(F &f, std::string arg) override {
        path = f.fix_abs_path(std::move(arg));
        if (path.empty()) {
            SingleLine(f.out, 501) << "Path should be specified.";
            return true;
        }
        working_directory = f.settings.full_working_path;
        if (!f.run_without_data_connect(this)) {
            SingleLine(f.out, 421) << "Internal error";
            return true;
        }
        return true;
    }

    bool process(FDOStream &out, int, ModeType) override {
        if (!check_file_write_access(path, working_directory)) {
            SingleLine(out, 550) << "Incorrect path.";
            return true;
        }
        std::ostringstream ss{};
        run_command("rm '" + path + "'", ss);
        SingleLine(out, 250) << "OK.";
        return true;
    }

    std::string path;
    std::string working_directory;
};

template <class F>
class Port : public Operation<F> {
public:
    bool operator()(F &f, std::string arg) override {
        std::istringstream in(arg);
        if (!f.data_connect.is_ready() && !f.data_connect.is_done()) {
            SingleLine(f.out, 500) << "Already running other";
            return true;
        }
        if (!f.data_connect.clear()) {
            SingleLine(f.out, 500) << "Internal error.";
            return true;
        }
        unsigned h1, h2, h3, h4, p1, p2;
        if (!isdigit(in.peek())) {
            SingleLine(f.out, 501) << "Bad format.";
            return true;
        }
        if (!(in >> h1)) {
            SingleLine(f.out, 501) << "Bad format.";
            return true;
        }
        if (in.peek() != ',') {
            SingleLine(f.out, 501) << "Bad format.";
            return true;
        }
        in.get();
        if (!(in >> h2)) {
            SingleLine(f.out, 501) << "Bad format.";
            return true;
        }
        if (in.peek() != ',') {
            SingleLine(f.out, 501) << "Bad format.";
            return true;
        }
        in.get();
        if (!(in >> h3)) {
            SingleLine(f.out, 501) << "Bad format.";
            return true;
        }
        if (in.peek() != ',') {
            SingleLine(f.out, 501) << "Bad format.";
            return true;
        }
        in.get();
        if (!(in >> h4)) {
            SingleLine(f.out, 501) << "Bad format.";
            return true;
        }
        if (in.peek() != ',') {
            SingleLine(f.out, 501) << "Bad format";
            return true;
        }
        in.get();
        if (!(in >> p1)) {
            SingleLine(f.out, 501) << "Bad format.";
            return true;
        }
        if (in.peek() != ',') {
            SingleLine(f.out, 501) << "Bad format.";
            return true;
        }
        in.get();
        if (!(in >> p2)) {
            SingleLine(f.out, 501) << "Bad format.";
            return true;
        }
        if (h1 >= 256 || h2 >= 256 || h3 >= 256 || h4 >= 256 || p1 >= 256 || p2 >= 256) {
            SingleLine(f.out, 501) << "Bad format.";
            return true;
        }
        if (in.get() != EOF) {
            SingleLine(f.out, 501) << "Bad format. Extra data found.";
            return true;
        }
        unsigned ip = (h1 << 24u) + (h2 << 16u) + (h3 << 8u) + h4;
        unsigned port = (p1 << 8u) + p2;
        // std::cerr << "Conn to " << ip << ' ' << port << std::endl;
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
    bool operator()(F &f, std::string arg) override {
        if (!arg.empty()) {
            SingleLine(f.out, 501) << "Arguments not expected.";
            return true;
        }
        if (!f.data_connect.is_ready() && !f.data_connect.is_done()) {
            SingleLine(f.out, 500) << "Already running other";
            return true;
        }
        if (!f.data_connect.clear()) {
            SingleLine(f.out, 500) << "Internal error.";
            return true;
        }
        unsigned port = 10000 + rand() % 10;
        // std::cerr << "Listen on " << port << std::endl;
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
    explicit List(std::string command_="ls -l", std::string postfix_="")
        : command(std::move(command_))
        , postfix(std::move(postfix_)){
        command.push_back(' ');
    }

    bool operator()(F &f, std::string arg) override {
        path = f.fix_abs_path(std::move(arg));
        if (!f.check_data_connect()) {
            return true;
        }
        if (!check_folder_exists_access(path.empty() ? "." : path, f.settings.full_working_path)) {
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
        std::string cmd = command + (!path.empty() ? ("'" + path + "'") : "") + postfix;
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
    bool operator()(F &f, std::string arg) override {
        path = f.fix_abs_path(std::move(arg));
        if (!f.check_data_connect()) {
            return true;
        }
        if (path.empty()) {
            SingleLine(f.out, 501) << "Path should be specified.";
            return true;
        }
        if (!check_file_read_access(path, f.settings.full_working_path)) {
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
            result = run_command("cat '" + path + "'", out);
        } else if (mode == ModeType::Block) {
            ModeBlockOStream out(fd);
            result = run_command("cat '" + path + "'", out);
        } else {
            ModeCompressedOStream out(fd);
            result = run_command("cat '" + path + "'", out);
        }
        if (result) {
            SingleLine(control, 226) << "Success.";
        }
        return result;
    }

    std::string path;
};

template <class F>
class Stor : public Operation<F> {
public:
    explicit Stor(int mode = O_CREAT) : filemode(mode) { }

    bool operator()(F &f, std::string arg) override {
        path = f.fix_abs_path(std::move(arg));
        if (!f.check_data_connect()) {
            return true;
        }
        if (path.empty()) {
            SingleLine(f.out, 501) << "Path should be specified.";
            return true;
        }
        if (!check_file_write_access(path, f.settings.full_working_path, filemode)) {
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
    bool operator()(F &f, std::string arg) override {
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

int function_conversation(int /*num_msg*/, const struct pam_message **/*msg*/,
                          struct pam_response **resp, void */*appdata_ptr*/) {
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
    bool operator()(F &f, std::string password) override {
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
    bool operator()(F &f, std::string arg) override {
        if (arg.empty()) {
            SingleLine(f.out, 500) << "Expected name of user.";
            return true;
        }
        f.username = std::move(arg);
        if (!f.settings.need_login) {
            SingleLine(f.out, 230) << "Success.";
            f.add_user_functions();
            return true;
        }
        SingleLine(f.out, 331) << "Need password.";
        f.set_clear_functions();
        f.functions["PASS"] = std::make_unique<Pass<F>>();
        return true;
    }
};

template <class F>
class StaticOperation : public Operation<F> {
public:
    StaticOperation(int code_, std::string text_) : code(code_), text(std::move(text_)) { }

    bool operator()(F &f, std::string arg) override {
        SingleLine(f.out, code) << text;
        return true;
    }

    int code;
    std::string text;
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
            return open_connection(ip, port);
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
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
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
                SingleLine(control, 451) << "Can not open data connection";
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

    bool clear() {
        if (state == State::kExecution) {
            return false;
        }
        server.reset();
        state = State::kNone;
        return true;
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
        std::string full_working_path;
        std::string bind_host;
        std::map<std::string, std::string> passes;
        bool need_login;
    };

    explicit FTP(int fd, const Settings &settings_) : in(fd), out(fd), settings(settings_) {
        in.dismiss();
        if (!set_timeout_fd(fd, SO_RCVTIMEO)) {
            close(fd);
            throw std::runtime_error("Can not set rcv timeout");
        }
        if (!set_timeout_fd(fd, SO_SNDTIMEO)) {
            close(fd);
            throw std::runtime_error("Can not set snd timeout");
        }
        set_clear_functions();
        default_function = std::make_unique<StaticOperation<FTP>>(530, "Please log in.");
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
        functions["STOR"] = std::make_unique<Stor<FTP>>();

        functions["CDUP"] = std::make_unique<CDUp<FTP>>();
        functions["CWD"] = std::make_unique<CWD<FTP>>();
        functions["APPE"] = std::make_unique<Stor<FTP>>(O_CREAT | O_APPEND);
        functions["DELE"] = std::make_unique<Dele<FTP>>();
        functions["RMD"] = std::make_unique<RMD<FTP>>();
        functions["MKD"] = std::make_unique<MKD<FTP>>();
        functions["NLST"] = std::make_unique<List<FTP>>("ls -1");

        functions["SLEEP"] = std::make_unique<Sleep<FTP>>();

        default_function = std::make_unique<StaticOperation<FTP>>(502, "No such command.");
    }

    std::string fix_abs_path(std::string path) {
        if (path.empty() || path[0] != '/') {
            return std::move(path);
        }
        return settings.full_working_path + path;
    }

    void set_clear_functions() {
        functions.clear();
        functions["USER"] = std::make_unique<User<FTP>>();
        functions["HELP"] = std::make_unique<Help<FTP>>();
        functions["QUIT"] = std::make_unique<Quit<FTP>>();
        functions["NOOP"] = std::make_unique<Noop<FTP>>();
    }

    bool check_data_connect() {
        if (data_connect.is_ready()) {
            return true;
        }
        if (data_connect.is_done()) {
            SingleLine(out, 125) << "Kostyil'.";
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
        if (chdir(settings.full_working_path.c_str()) != 0) {
            std::cerr << "Directory set initial failed\n";
            exit(1);
        }
        if (rand() % 2 == 0) {
            SingleLine(out, 120) << "Wait a bit.";
        }
        SingleLine(out, 220) << "Igor Mineev Server Ready.";
        std::string command;
        while (in >> command) {
            // Skip space after command;
            bool is_end = true;
            if (in.peek() == ' ') {
                in.get();
                is_end = false;
            }
            auto arg = read_till_end(in);
            if (is_end && !arg.empty()) {
                SingleLine(out, 500) << "Bad command format.";
                continue;
            }
            std::transform(command.begin(), command.end(), command.begin(),
                           [](unsigned char c){ return std::toupper(c); });
            try {
                auto it = functions.find(command);
                if (it != functions.end()) {
                    if (!(*it->second)(*this, std::move(arg))) {
                        break;
                    }
                    continue;
                }
                (*default_function)(*this, std::move(arg));
            } catch (const std::exception &e) {
                std::cerr << e.what() << std::endl;
                break;
            }
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

void empty(int /*signum*/) { }

std::string get_full_path(const std::string &path) {
#ifdef PATH_MAX
    auto path_max = PATH_MAX;
#else
    auto path_max = pathconf(path.c_str(), _PC_PATH_MAX);
    if (path_max <= 0) {
        path_max = 4096;
    }
#endif
    std::vector<char> buf(path_max + 1);
    char *full_path = realpath(path.c_str(), buf.data());
    if (full_path == nullptr) {
        /// Can not check path
        return {};
    }
    return {full_path};
}

int main() {
    signal(SIGPIPE, empty);

    FTP::Settings settings{};

    settings.full_working_path = get_full_path(parse_env_req("HW1_DIRECTORY"));
    auto user_passes = parse_env("HW1_USERS");
    settings.bind_host = parse_env_req("HW1_HOST");
    auto bind_port = parse_env_req("HW1_PORT");
    auto is_disabled = parse_env("HW1_AUTH_DISABLED");

    auto [passes, need_login] = read_db(user_passes, is_disabled);
    settings.passes = std::move(passes);
    settings.need_login = need_login;

    if (settings.full_working_path.empty() ||
        !check_folder_exists_access(settings.full_working_path, settings.full_working_path)) {
        std::cerr << "No access to dir " << settings.full_working_path << std::endl;
        return 1;
    }

    Server<FTP>(settings.bind_host, strtoul(bind_port.c_str(), nullptr, 10)).run(settings);
}
