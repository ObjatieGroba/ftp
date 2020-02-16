#define TESTS

#include <map>
#include <memory>
#include <vector>
#include <algorithm>
#include <thread>

#include <signal.h>
#include <sys/time.h>

#include "tools.hpp"

template <class Func>
class MyThread {
public:
    MyThread(Func func) : t(func) { }

    ~MyThread() {
        t.join();
    }

private:
    std::thread t;
};

struct Result {
    std::string error;
    unsigned code;

    Result(std::string error_) : error(std::move(error_)) { }

    Result(unsigned code_) : code(code_) { }
};

class FTPClient {
public:
    static const std::map<std::string, std::vector<int>> allowed_codes;
    static const std::map<std::string, std::vector<int>> allowed_second_codes;

    static bool check_code(const std::string &command, unsigned code) {
        auto it = allowed_codes.find(command);
        if (it == allowed_codes.end()) {
            std::cerr << "Warning, there are no command " << command << std::endl;
            return true;
        }
        const auto &codes = it->second;
        if (codes.empty()) {
            auto first = code / 100;
            auto second = code - first * 100;
            second /= 10;
            return 1 <= first && first <= 5 && second <= 5;
        }
        if (code == 530 || code == 502) {
            return true;
        }
        auto it2 = std::find(codes.begin(), codes.end(), code);
        return it2 != codes.end();
    }

    static bool check_second_code(const std::string &command, unsigned code) {
        auto it = allowed_second_codes.find(command);
        if (it == allowed_second_codes.end()) {
            // std::cerr << "Warning, there are no second command " << command << std::endl;
            return check_code(command, code);
        }
        const auto &codes = it->second;
        auto it2 = std::find(codes.begin(), codes.end(), code);
        return it2 != codes.end();
    }

    FTPClient(int fd) : in(fd), out(fd) {
        set_timeout_fd(fd, SO_SNDTIMEO, 1);
        set_timeout_fd(fd, SO_RCVTIMEO, 1);
        in.dismiss();
        auto result = run("ON_START", false);
        if (!result.error.empty()) {
            throw std::runtime_error(result.error);
        }
        while ((result.code / 100) == 1) {
            result = run("ON_START", false);
            if (!result.error.empty()) {
                throw std::runtime_error(result.error);
            }
        }
    }

    Result run(std::string command, bool send = true, const std::string &args = "", std::string *output = nullptr) {
        try {
            if (send) {
                out << command;
                if (!args.empty()) {
                    out << ' ' << args;
                }
                out << "\r\n";
                out.flush();
            }
            auto s = read_till_end(in);
            if (s.size() <= 3) {
                return {"Too little answer"};
            }
            for (unsigned i = 0; i != 3; ++i) {
                if (!isdigit(s[i])) {
                    return {"Return code should contains only integers"};
                }
            }
            if (s[3] != ' ' && s[3] != '-') {
                return {"Bad reply format"};
            }
            if (output) {
                *output = s;
            }
            // std::cout << command << '\n' << s << std::endl;
            unsigned code = (s[0] - '0') * 100 + (s[1] - '0') * 10 + (s[2] - '0');
            std::string wait_till = s.substr(0, 4);
            wait_till[3] = ' ';
            std::string data;
            while (s.substr(0, wait_till.size()) != wait_till) {
                s = read_till_end(in);
                if (output) {
                    *output += s;
                }
                // std::cout << s << std::endl;
            }
            std::transform(command.begin(), command.end(), command.begin(), [](char c){ return toupper(c); });
            if (send) {
                if (!check_code(command, code)) {
                    return {"Not allowed code " + std::to_string(code) + " on " + command};
                }
            } else {
                if (!check_second_code(command, code)) {
                    return {"Not allowed code " + std::to_string(code) + " on " + command};
                }
            }
            // std::cout << command << ' ' << code << std::endl;
            return {code};
        } catch (const std::exception &e) {
            return {std::string(e.what())};
        }
    }

    FDIStream in;
    FDOStream out;
};

const std::map<std::string, std::vector<int>> FTPClient::allowed_codes = {
        {"USER", {230, 530, 500, 501, 421, 331, 332}},
        {"PASS", {230, 202, 530, 500, 501, 503, 421, 332}},
        {"ACCT", {230, 202, 530, 500, 501, 503, 421}},
        {"CWD", {250, 500, 501, 502, 421, 530, 550}},
        {"CDUP", {200, 500, 501, 502, 421, 530, 550}},
        {"SMNT", {202, 250, 500, 501, 502, 421, 530, 550}},

        {"REIN", {120, 220, 421, 500, 502}},
        {"QUIT", {221, 500}},

        {"PORT", {200, 500, 501, 421, 530}},
        {"PASV", {227, 500, 501, 502, 421, 530}},
        {"MODE", {200, 500, 501, 504, 421, 530}},
        {"TYPE", {200, 500, 501, 504, 421, 530}},
        {"STRU", {200, 500, 501, 504, 421, 530}},

        {"ALLO", {200, 202, 500, 501, 504, 421, 530}},
        {"REST", {350, 500, 501, 502, 421, 530}},
        {"STOR", {125, 150, 532, 450, 452, 553, 500, 501, 421, 530}},
        {"STOU", {125, 150, 532, 450, 452, 553, 500, 501, 421, 530}},
        {"RETR", {125, 150, 450, 550, 500, 501, 421, 530}},
        {"LIST", {125, 150, 450, 500, 501, 502, 421, 530}},
        {"NLST", {125, 150, 450, 500, 501, 502, 421, 530}},
        {"APPE", {125, 150, 532, 450, 550, 452, 553, 500, 501, 502, 421, 530}},
        {"RNFR", {450, 550, 500, 501, 502, 421, 530, 350}},
        {"RNTO", {250, 532, 553, 500, 501, 502, 503, 421, 530}},
        {"DELE", {250, 450, 550, 500, 501, 502, 421, 530}},
        {"RMD", {250, 500, 501, 502, 421, 530, 550}},
        {"MKD", {257, 500, 501, 502, 421, 530, 550}},
        {"PWD", {257, 500, 501, 502, 421, 550}},
        {"ABOR", {225, 226, 500, 501, 502, 421}},

        {"SYST", {215, 500, 501, 502, 421}},
        {"SYST", {211, 212, 213, 450, 500, 501, 502, 421, 530}},
        {"HELP", {211, 214, 500, 501, 502, 421}},

        {"SITE", {200, 202, 500, 501, 530}},
        {"NOOP", {200, 500, 421}},

        {"THISFUNCDOESNOTEXISTS", {}}
};

const std::map<std::string, std::vector<int>> FTPClient::allowed_second_codes = {
        {"REIN", {220}},

        {"STOR", {110, 226, 250, 425, 426, 451, 551, 552}},
        {"STOU", {110, 226, 250, 425, 426, 451, 551, 552}},
        {"RETR", {110, 226, 250, 425, 426, 451}},
        {"LIST", {226, 250, 425, 426, 451}},
        {"NLST", {226, 250, 425, 426, 451}},
        {"APPE", {110, 226, 250, 425, 426, 451, 551, 552}},

        {"ON_START", {120, 220, 421}}
};

class Test {
protected:
    Test(std::string myip_, unsigned ip_, unsigned port_)
        : myip(std::move(myip_)), port_arg(myip), myport(9998), ip(ip_), port(port_) {
        std::transform(port_arg.begin(), port_arg.end(), port_arg.begin(), [](char c) { return c == '.' ? ',' : c; });
        port_arg.push_back(',');
        port_arg += std::to_string(myport >> 8u);
        port_arg.push_back(',');
        port_arg += std::to_string(myport & ((1u << 8u) - 1));
    };

    Result test_all_codes(FTPClient &client) const {
        for (auto &&cmd : FTPClient::allowed_codes) {
            if (cmd.first == "PASV") {
                continue;
            }
            if (cmd.first == "QUIT") {
                continue;
            }
            if (cmd.first == "REIN") {
                continue;
            }
            if (cmd.first == "THISFUNCDOESNOTEXISTS") {
                continue;
            }
            {
                auto result = client.run(cmd.first);
                if (!result.error.empty()) {
                    return result;
                }
                while ((result.code / 100) == 1) {
                    result = client.run(cmd.first, false);
                    if (!result.error.empty()) {
                        return result;
                    }
                }
            }
            {
                auto result = client.run(cmd.first, true, "test");
                if (!result.error.empty()) {
                    return result;
                }
                while ((result.code / 100) == 1) {
                    result = client.run(cmd.first, false);
                    if (!result.error.empty()) {
                        return result;
                    }
                }
            }
        }
        auto result = client.run("QUIT", true, "test");
        if (!result.error.empty()) {
            return result;
        }
        result = client.run("QUIT");
        if (!result.error.empty()) {
            return result;
        }
        return {0};
    }

    std::string myip;
    std::string port_arg;
    unsigned myport;
    unsigned ip;
    unsigned port;

public:
    virtual ~Test() = default;
    virtual bool operator()(bool enable_output) const = 0;
};

#define test(name) \
if (enable_output) {\
    std::cerr << "TEST " << name << std::endl;\
}\

bool check_res_code(int code, std::vector<int> codes) {
    for (auto c : codes) {
        if (c == code) {
            return true;
        }
    }
    return false;
}

std::string print(std::vector<int> codes) {
    std::string res;
    for (auto code : codes) {
        res += std::to_string(code) + ',';
    }
    res.pop_back();
    return std::move(res);
}

#define req_code(ccodes...) \
if (!check_res_code(res.code, {ccodes})) {\
    if (enable_output) {\
        std::cerr << "Not valid code " << res.code << " expected " << print({ccodes}) << std::endl;\
    }\
    return false;\
}\

#define open_server() \
int fd = open_connection(ip, port);\
if (fd == -1) {\
    if (enable_output) {\
        std::cerr << "Can not open connection" << std::endl;\
    }\
    return false;\
}\
FTPClient client(fd);\
Result res(0);\

#define open_server_auth(name, pass) \
open_server()\
res = client.run("USER", true, name); \
    if (!res.error.empty()) {\
        if (enable_output) {\
            std::cerr << res.error << std::endl;\
        }\
    return false;\
}\
if (res.code == 331) {\
    res = client.run("PASS", true, pass);\
    if (!res.error.empty()) {\
        if (enable_output) {\
            std::cerr << res.error << std::endl;\
        }\
        return false;\
    }\
}\
req_code(230)

#define test_run(func) \
res = func;\
if (!res.error.empty()) {\
    if (enable_output) {\
        std::cerr << res.error << std::endl;\
    }\
    return false;\
}\

#define test_run_while_100(func_name, args)\
test_run(client.run(func_name, true, args))\
while (res.error.empty() && (res.code / 100) == 1) {\
    res = client.run(func_name, false);\
}\
if (!res.error.empty()) {\
    if (enable_output) {\
        std::cerr << res.error << std::endl;\
    }\
    return false;\
}\

#define require_eq(a, etalon, text)\
if (a != etalon) {\
    if (enable_output) {\
        std::cerr << text << std::endl;\
    }\
    return false;\
}

#define require_neq(a, etalon, text)\
if (a == etalon) {\
    if (enable_output) {\
        std::cerr << text << std::endl;\
    }\
    return false;\
}

class MinimalTest : public Test {
public:
    MinimalTest(std::string myip, unsigned ip, unsigned port) : Test(std::move(myip), ip, port) { }

    bool operator()(bool enable_output) const final {
        {
            test("Common")
            open_server_auth("anonymous", "anonymous")

            test_run( client.run("TYPE", true, "A"))
            req_code(200)
            test_run( client.run("MODE", true, "S"))
            req_code(200)
            test_run( client.run("STRU", true, "F"))
            req_code(200)

            /// Any case should be allowed
            test_run( client.run("Type", true, "an"))
            req_code(200)
            test_run( client.run("mOde", true, "s"))
            req_code(200)
            test_run( client.run("stRU", true, "f"))
            req_code(200)

            test_run( client.run("NOOP"))
            req_code(200)

            /// Extra
            client.out << "NO";
            client.out.flush();
            client.out << "OP\r\n";
            client.out.flush();
            test_run(client.run("NOOP", false))
            req_code(200)

            client.out << "TYPE\nA" << "\r\n";
            client.out.flush();
            test_run(client.run("TYPE", false))
            req_code(500, 501)

            test_run( client.run("NOOP"))
            req_code(200)
        }
        {
            test("Relogin")
            open_server_auth("anonymous", "anonymous")

            test_run(client.run("USER", true, "abcde"))
            req_code(230)
            test_run(client.run("USER", true, "anonymous"))
            req_code(230)
            test_run(client.run("USER", true, ""))
            req_code(500, 501)
        }
        {
            test("Data send and recv")
            open_server_auth("anonymous", "anonymous")

            const std::string filename = "test_file_001";
            const std::string filename2 = "test_file_001 2";
            Server<int> server(myip, myport);


            /// Simple write + read
            const std::string text = "abcdefghi\n";
            {
                MyThread t([&]() {
                    if (!server.run_one([&](int fd) {
                        set_timeout_fd(fd, SO_SNDTIMEO, 1);
                        FDOStream out(fd);
                        out << text;
                    })) {
                        if (enable_output) {
                            std::cerr << "Server run fail\n";
                        }
                    }
                });
                test_run(client.run("PORT", true, port_arg))
                req_code(200)
                test_run_while_100("STOR", filename)
                req_code(226, 250)
            }

            std::string response;
            {
                MyThread t([&]() {
                    if (!server.run_one([&](int fd) {
                        set_timeout_fd(fd, SO_RCVTIMEO, 1);
                        FDIStream in(fd);
                        std::copy(std::istreambuf_iterator<char>(in),
                                  std::istreambuf_iterator<char>(),
                                  std::back_inserter(response)
                        );
                    })) {
                        if (enable_output) {
                            std::cerr << "Server run fail\n";
                        }
                    }
                });
                test_run(client.run("PORT", true, port_arg))
                req_code(200)
                test_run_while_100("RETR", filename)
                req_code(226, 250)
            }
            require_eq(response, text, "Expected equal file simple")


            /// Check that no one file exists, spaces in filenames
            const std::string another_file_text{'1', '2', '3', '\0', '5', '6'};
            {
                MyThread t([&]() {
                    if (!server.run_one([&](int fd) {
                        set_timeout_fd(fd, SO_SNDTIMEO, 1);
                        FDOStream out(fd);
                        out << another_file_text;
                    })) {
                        if (enable_output) {
                            std::cerr << "Server run fail\n";
                        }
                    }
                });
                test_run(client.run("PORT", true, port_arg))
                req_code(200)
                test_run_while_100("STOR", filename2)
                req_code(226, 250)
            }

            response.clear();
            {
                MyThread t([&]() {
                    if (!server.run_one([&](int fd) {
                        set_timeout_fd(fd, SO_RCVTIMEO, 1);
                        FDIStream in(fd);
                        std::copy(std::istreambuf_iterator<char>(in),
                                  std::istreambuf_iterator<char>(),
                                  std::back_inserter(response)
                        );
                    })) {
                        if (enable_output) {
                            std::cerr << "Server run fail\n";
                        }
                    }
                });
                test_run(client.run("PORT", true, port_arg))
                req_code(200)
                test_run_while_100("RETR", filename2)
                req_code(226, 250)
            }
            require_eq(response, another_file_text, "Expected equal file with zero byte")

            response.clear();
            {
                MyThread t([&]() {
                    if (!server.run_one([&](int fd) {
                        set_timeout_fd(fd, SO_RCVTIMEO, 1);
                        FDIStream in(fd);
                        std::copy(std::istreambuf_iterator<char>(in),
                                  std::istreambuf_iterator<char>(),
                                  std::back_inserter(response)
                        );
                    })) {
                        if (enable_output) {
                            std::cerr << "Server run fail\n";
                        }
                    }
                });
                test_run(client.run("PORT", true, port_arg))
                req_code(200)
                test_run_while_100("RETR", filename)
                req_code(226, 250)
            }
            require_eq(response, text, "Expected equal simple file after another")


            /// Check empty file write
            const std::string empty_text{};
            {
                MyThread t([&]() {
                    if (!server.run_one([&](int fd) {
                        set_timeout_fd(fd, SO_SNDTIMEO, 1);
                        FDOStream out(fd);
                        out << empty_text;
                    })) {
                        if (enable_output) {
                            std::cerr << "Server run fail\n";
                        }
                    }
                });
                test_run(client.run("PORT", true, port_arg))
                req_code(200)
                test_run_while_100("STOR", filename)
                req_code(226, 250)
            }

            response.clear();
            {
                MyThread t([&]() {
                    if (!server.run_one([&](int fd) {
                        set_timeout_fd(fd, SO_RCVTIMEO, 1);
                        FDIStream in(fd);
                        std::copy(std::istreambuf_iterator<char>(in),
                                  std::istreambuf_iterator<char>(),
                                  std::back_inserter(response)
                        );
                    })) {
                        if (enable_output) {
                            std::cerr << "Server run fail\n";
                        }
                    }
                });
                test_run(client.run("PORT", true, port_arg))
                req_code(200)
                test_run_while_100("RETR", filename)
                req_code(226, 250)
            }
            require_eq(response, empty_text, "Expected equal empty file")


            /// Not existed file RETR
            test_run(client.run("PORT", true, port_arg))
            req_code(200)
            test_run_while_100("RETR", "THISFILECANNOTBEEXISTED")
            req_code(450, 550)
        }
        {
            test("PORT args")
            open_server_auth("anonymous", "anonymous")

            test_run(client.run("PORT", true, ""))
            req_code(500, 501)

            test_run(client.run("PORT", true, "256,1,1,1,1,1"))
            req_code(501)

            test_run(client.run("PORT", true, "1,1,1,1,1,256"))
            req_code(501)

            test_run(client.run("PORT", true, "-1,1,1,1,1,1"))
            req_code(501)

            test_run(client.run("PORT", true, "a,b,c,d,e,f"))
            req_code(501)

            test_run(client.run("PORT", true, "1,2,3,4,5,"))
            req_code(501)

            test_run(client.run("PORT", true, "1,1,1,,1,1"))
            req_code(501)

            test_run(client.run("PORT", true, "1,1,1,1,1,1 haha"))
            req_code(501)


            const std::string filename = "test_file_011";
            Server<int> server(myip, myport);

            /// It should be possible to change port
            test_run(client.run("PORT", true, "1,2,3,4,5,6"))
            req_code(200)

            const std::string text = "this is file text\r\nit's work\n";
            {
                MyThread t([&]() {
                    if (!server.run_one([&](int fd) {
                        set_timeout_fd(fd, SO_SNDTIMEO, 1);
                        FDOStream out(fd);
                        out << text;
                    })) {
                        if (enable_output) {
                            std::cerr << "Server run fail\n";
                        }
                    }
                });
                test_run(client.run("PORT", true, port_arg))
                req_code(200)
                test_run_while_100("STOR", filename)
                req_code(226, 250)
            }

            std::string response;
            {
                MyThread t([&]() {
                    if (!server.run_one([&](int fd) {
                        set_timeout_fd(fd, SO_RCVTIMEO, 1);
                        FDIStream in(fd);
                        std::copy(std::istreambuf_iterator<char>(in),
                                  std::istreambuf_iterator<char>(),
                                  std::back_inserter(response)
                        );
                    })) {
                        if (enable_output) {
                            std::cerr << "Server run fail\n";
                        }
                    }
                });
                test_run(client.run("PORT", true, port_arg))
                req_code(200)
                test_run_while_100("RETR", filename)
                req_code(226, 250)
            }
            require_eq(response, text, "Expected equal file simple")
        }
        {
            test("Without login")
            open_server()

            test_run(test_all_codes(client))
        }
        {
            test("After anon login")
            open_server_auth("anonymous", "anonymous")

            test_run(test_all_codes(client))
        }
        {
            test("Not existed function")
            open_server_auth("anonymous", "anonymous")

            test_run( client.run("THISFUNCDOESNOTEXISTS"))
            test_run( client.run("THISFUNCDOESNOTEXISTS", true, "test test"))
        }
        return true;
    }
};


class DirTest : public Test {
public:
    DirTest(std::string myip, unsigned ip, unsigned port) : Test(std::move(myip), ip, port) {}

    bool operator()(bool enable_output) const final {
        {
            test("CWD NLST CDUP RMD MKD")
            open_server_auth("anonymous", "anonymous")
            Server<int> server(myip, myport);

            test_run( client.run("RMD", true, "my dir"))
            req_code(250, 550)
            test_run( client.run("MKD", true, "my dir"))
            req_code(257)
            std::string current;
            {
                MyThread t([&]() {
                    if (!server.run_one([&](int fd) {
                        set_timeout_fd(fd, SO_RCVTIMEO, 1);
                        FDIStream in(fd);
                        std::copy(std::istreambuf_iterator<char>(in),
                                  std::istreambuf_iterator<char>(),
                                  std::back_inserter(current)
                        );
                    })) {
                        if (enable_output) {
                            std::cerr << "Server run fail\n";
                        }
                    }
                });
                test_run(client.run("PORT", true, port_arg))
                req_code(200)
                test_run_while_100("NLST", "")
                req_code(226, 250)
            }
            test_run( client.run("CWD", true, "my dir"))
            req_code(250)
            std::string other;
            {
                MyThread t([&]() {
                    if (!server.run_one([&](int fd) {
                        set_timeout_fd(fd, SO_RCVTIMEO, 1);
                        FDIStream in(fd);
                        std::copy(std::istreambuf_iterator<char>(in),
                                  std::istreambuf_iterator<char>(),
                                  std::back_inserter(other)
                        );
                    })) {
                        if (enable_output) {
                            std::cerr << "Server run fail\n";
                        }
                    }
                });
                test_run(client.run("PORT", true, port_arg))
                req_code(200)
                test_run_while_100("NLST", "")
                req_code(226, 250)
            }
            require_neq(current, other, "Directory not changed")
            test_run( client.run("CDUP"))
            req_code(200)
            std::string next;
            {
                MyThread t([&]() {
                    if (!server.run_one([&](int fd) {
                        set_timeout_fd(fd, SO_RCVTIMEO, 1);
                        FDIStream in(fd);
                        std::copy(std::istreambuf_iterator<char>(in),
                                  std::istreambuf_iterator<char>(),
                                  std::back_inserter(next)
                        );
                    })) {
                        if (enable_output) {
                            std::cerr << "Server run fail\n";
                        }
                    }
                });
                test_run(client.run("PORT", true, port_arg))
                req_code(200)
                test_run_while_100("NLST", "")
                req_code(226, 250)
            }
            require_eq(current, next, "Directory changed")
            test_run( client.run("RMD", true, "my dir"))
            req_code(250)
            std::string after_remove;
            {
                MyThread t([&]() {
                    if (!server.run_one([&](int fd) {
                        set_timeout_fd(fd, SO_RCVTIMEO, 1);
                        FDIStream in(fd);
                        std::copy(std::istreambuf_iterator<char>(in),
                                  std::istreambuf_iterator<char>(),
                                  std::back_inserter(after_remove)
                        );
                    })) {
                        if (enable_output) {
                            std::cerr << "Server run fail\n";
                        }
                    }
                });
                test_run(client.run("PORT", true, port_arg))
                req_code(200)
                test_run_while_100("NLST", "")
                req_code(226, 250)
            }
            require_neq(after_remove, next, "Directory changed")
        }
        {
            test("NLST APPE DELE")

            open_server_auth("anonymous", "anonymous")
            Server<int> server(myip, myport);
            const std::string filename = "test_file appe";

            test_run(client.run("DELE", true, filename))
            req_code(250, 450, 550)

            const std::string text1 = "12345678\n";
            {
                MyThread t([&]() {
                    if (!server.run_one([&](int fd) {
                        set_timeout_fd(fd, SO_SNDTIMEO, 1);
                        FDOStream out(fd);
                        out << text1;
                    })) {
                        if (enable_output) {
                            std::cerr << "Server run fail\n";
                        }
                    }
                });
                test_run(client.run("PORT", true, port_arg))
                req_code(200)
                test_run_while_100("APPE", filename)
                req_code(226, 250)
            }

            const std::string text2 = "abcdefghik\n";
            {
                MyThread t([&]() {
                    if (!server.run_one([&](int fd) {
                        set_timeout_fd(fd, SO_SNDTIMEO, 1);
                        FDOStream out(fd);
                        out << text2;
                    })) {
                        if (enable_output) {
                            std::cerr << "Server run fail\n";
                        }
                    }
                });
                test_run(client.run("PORT", true, port_arg))
                req_code(200)
                test_run_while_100("APPE", filename)
                req_code(226, 250)
            }

            std::string response;
            {
                MyThread t([&]() {
                    if (!server.run_one([&](int fd) {
                        set_timeout_fd(fd, SO_RCVTIMEO, 1);
                        FDIStream in(fd);
                        std::copy(std::istreambuf_iterator<char>(in),
                                  std::istreambuf_iterator<char>(),
                                  std::back_inserter(response)
                        );
                    })) {
                        if (enable_output) {
                            std::cerr << "Server run fail\n";
                        }
                    }
                });
                test_run(client.run("PORT", true, port_arg))
                req_code(200)
                test_run_while_100("RETR", filename)
                req_code(226, 250)
            }
            require_eq(response, text1 + text2, "Expected equal file simple")

            test_run(client.run("DELE", true, filename))
            req_code(250)
        }
        {
            test("In dir file from another client")
            Server<int> server(myip, myport);
            const std::string dirname = "dir";
            const std::string filename = "file";
            const std::string text = "somerandomtext\n";

            {
                open_server_auth("anonymous", "anonymous")
                test_run(client.run("RMD", true, dirname))
                req_code(250, 550)
                test_run(client.run("MKD", true, dirname))
                req_code(257)
                test_run( client.run("CWD", true, dirname))
                req_code(250)

                {
                    MyThread t([&]() {
                        if (!server.run_one([&](int fd) {
                            set_timeout_fd(fd, SO_SNDTIMEO, 1);
                            FDOStream out(fd);
                            out << text;
                        })) {
                            if (enable_output) {
                                std::cerr << "Server run fail\n";
                            }
                        }
                    });
                    test_run(client.run("PORT", true, port_arg))
                    req_code(200)
                    test_run_while_100("STOR", filename)
                    req_code(226, 250)
                }
            }
            {
                open_server_auth("anonymous", "anonymous")
                std::string response;
                {
                    MyThread t([&]() {
                        if (!server.run_one([&](int fd) {
                            set_timeout_fd(fd, SO_RCVTIMEO, 1);
                            FDIStream in(fd);
                            std::copy(std::istreambuf_iterator<char>(in),
                                      std::istreambuf_iterator<char>(),
                                      std::back_inserter(response)
                            );
                        })) {
                            if (enable_output) {
                                std::cerr << "Server run fail\n";
                            }
                        }
                    });
                    test_run(client.run("PORT", true, port_arg))
                    req_code(200)
                    test_run_while_100("RETR", dirname + '/' + filename)
                    req_code(226, 250)
                }
                require_eq(response, text, "Expected equal file simple")


                test_run(client.run("DELE", true, dirname))
                req_code(450, 550)
                {
                    test_run(client.run("PORT", true, port_arg))
                    req_code(200)
                    test_run_while_100("RETR", dirname)
                    req_code(450, 550)
                }
            }
        }
        return true;
    }
};


class PassiveTest : public Test {
public:
    PassiveTest(std::string myip, unsigned ip, unsigned port) : Test(std::move(myip), ip, port) {}

    static bool parse_addr(std::string addr, unsigned &ip, unsigned &port) {
        std::istringstream in(addr);
        unsigned h1, h2, h3, h4, p1, p2;
        if (!isdigit(in.peek())) {
            return false;
        }
        if (!(in >> h1)) {
            return false;
        }
        if (in.peek() != ',') {
            return false;
        }
        in.get();
        if (!(in >> h2)) {
            return false;
        }
        if (in.peek() != ',') {
            return false;
        }
        in.get();
        if (!(in >> h3)) {
            return false;
        }
        if (in.peek() != ',') {
            return false;
        }
        in.get();
        if (!(in >> h4)) {
            return false;
        }
        if (in.peek() != ',') {
            return false;
        }
        in.get();
        if (!(in >> p1)) {
            return false;
        }
        if (in.peek() != ',') {
            return false;
        }
        in.get();
        if (!(in >> p2)) {
            return false;
        }
        if (h1 >= 256 || h2 >= 256 || h3 >= 256 || h4 >= 256 || p1 >= 256 || p2 >= 256) {
            return false;
        }
        if (in.get() != EOF) {
            return false;
        }
        ip = (h1 << 24u) + (h2 << 16u) + (h3 << 8u) + h4;
        port = (p1 << 8u) + p2;
        return true;
    }

    bool operator()(bool enable_output) const final {
        {
            test("PASV")
            open_server_auth("anonymous", "anonymous")

            const std::string filename = "test_file_passive_001";

            /// Simple write + read
            const std::string text = "abcdefghi\n";
            std::string addr;

            test_run(client.run("PASV", true, "", &addr))
            req_code(227)
            auto pos = addr.find('(');
            if (pos == std::string::npos) {
                /// There are no way to parse address
                /// I do not know such format
                /// :(((
                return true;
            }
            auto pos_end = addr.find(')', pos);
            if ((pos_end == std::string::npos) || (pos_end <= pos + 1)) {
                /// There are no way to parse address
                /// I do not know such format
                /// :(((
                return true;
            }
            unsigned ip, port;
            if (!parse_addr(addr.substr(pos + 1, pos_end - pos - 1), ip, port)) {
                /// There are no way to parse address
                /// I do not know such format
                /// :(((
                return true;
            }
            {
                MyThread t([&]() {
                    int fd = open_connection(ip, port);
                    if (fd == -1) {
                        if (enable_output) {
                            std::cerr << "Server run fail\n";
                        }
                        return;
                    }
                    set_timeout_fd(fd, SO_SNDTIMEO, 1);
                    FDOStream out(fd);
                    out << text;
                });
                test_run_while_100("STOR", filename)
                req_code(226, 250)
            }

            test_run(client.run("PASV", true, "", &addr))
            req_code(227)
            pos = addr.find('(');
            if (pos == std::string::npos) {
                /// There are no way to parse address
                /// I do not know such format
                /// :(((
                return true;
            }
            pos_end = addr.find(')', pos);
            if (pos_end == std::string::npos) {
                /// There are no way to parse address
                /// I do not know such format
                /// :(((
                return true;
            }
            if (!parse_addr(addr.substr(pos + 1, pos_end), ip, port)) {
                /// There are no way to parse address
                /// I do not know such format
                /// :(((
                return true;
            }
            std::string response;
            {
                MyThread t([&]() {
                    int fd = open_connection(ip, port);
                    if (fd == -1) {
                        if (enable_output) {
                            std::cerr << "Server run fail\n";
                        }
                        return;
                    }
                    set_timeout_fd(fd, SO_RCVTIMEO, 1);
                    FDIStream in(fd);
                    std::copy(std::istreambuf_iterator<char>(in),
                              std::istreambuf_iterator<char>(),
                              std::back_inserter(response)
                    );
                });
                test_run_while_100("RETR", filename)
                req_code(226, 250)
            }
            require_eq(response, text, "Expected equal file simple")
        }
        return true;
    }
};


class AuthTest : public Test {
public:
    AuthTest(std::optional<std::string> users_, std::string myip, unsigned ip, unsigned port)
        : users(users_), Test(std::move(myip), ip, port) {
    }

    bool operator()(bool enable_output) const final {
        if (!users) {
            throw std::runtime_error("No file with passes");
        }
        passes = std::get<0>(read_db(users, {}));
        {
            test("All login")
            open_server_auth("anonymous", "anonymous")
            for (auto [name, pass] : passes) {
                res = client.run("USER", true, name);
                if (!res.error.empty()) {
                    if (enable_output) {
                        std::cerr << res.error << std::endl;
                    }
                    return false;
                }
                if (res.code != 331) {
                    if (enable_output) {
                        std::cerr << "PASS should be required" << std::endl;
                    }
                    return false;
                }
                res = client.run("PASS", true, pass);
                if (!res.error.empty()) {
                    if (enable_output) {
                        std::cerr << res.error << std::endl;
                    }
                    return false;
                }
                req_code(230)
            }
        }
        {
            test("No login")
            open_server()

            test_run(client.run("MODE", true, "S"))
            req_code(530)

            res = client.run("USER", true, passes.begin()->first);
            if (!res.error.empty()) {
                if (enable_output) {
                    std::cerr << res.error << std::endl;
                }
                return false;
            }
            if (res.code != 331) {
                if (enable_output) {
                    std::cerr << "PASS should be required" << std::endl;
                }
                return false;
            }

            test_run(client.run("MODE", true, "S"))
            req_code(530)

            res = client.run("PASS", true, passes.begin()->second);
            if (!res.error.empty()) {
                if (enable_output) {
                    std::cerr << res.error << std::endl;
                }
                return false;
            }
            req_code(230)

            test_run(client.run("MODE", true, "S"))
            req_code(200)
        }
        return true;
    }

    mutable std::map<std::string, std::string> passes;
    std::optional<std::string> users;
};


class ModeBlockTest : public Test {
public:
    ModeBlockTest(std::string myip, unsigned ip, unsigned port) : Test(std::move(myip), ip, port) {}

    bool operator()(bool enable_output) const final {
        {
            test("Block Mode small")
            open_server_auth("anonymous", "anonymous")
            test_run(client.run("MODE", true, "B"))

            const std::string filename = "test_file_b";
            Server<int> server(myip, myport);

            const std::string text = "abcdefghi";
            {
                MyThread t([&]() {
                    if (!server.run_one([&](int fd) {
                        set_timeout_fd(fd, SO_SNDTIMEO, 1);
                        ModeBlockOStream out(fd);
                        out << text;
                    })) {
                        if (enable_output) {
                            std::cerr << "Server run fail\n";
                        }
                    }
                });
                test_run(client.run("PORT", true, port_arg))
                req_code(200)
                test_run_while_100("STOR", filename)
                req_code(226, 250)
            }

            std::string response;
            {
                MyThread t([&]() {
                    if (!server.run_one([&](int fd) {
                        set_timeout_fd(fd, SO_RCVTIMEO, 1);
                        ModeBlockIStream in(fd);
                        std::copy(std::istreambuf_iterator<char>(in),
                                  std::istreambuf_iterator<char>(),
                                  std::back_inserter(response)
                        );
                    })) {
                        if (enable_output) {
                            std::cerr << "Server run fail\n";
                        }
                    }
                });
                test_run(client.run("PORT", true, port_arg))
                req_code(200)
                test_run_while_100("RETR", filename)
                req_code(226, 250)
            }
            require_eq(response, text, "Expected equal file simple block")
        }
        {
            test("Block Mode med")
            open_server_auth("anonymous", "anonymous")
            test_run(client.run("MODE", true, "B"))

            const std::string filename = "test_file_b_med";
            Server<int> server(myip, myport);

            std::string text;
            text.resize(10000);
            for (size_t i = 0; i != text.size(); ++i) {
                text[i] = i;
            }
            {
                MyThread t([&]() {
                    if (!server.run_one([&](int fd) {
                        set_timeout_fd(fd, SO_SNDTIMEO, 1);
                        ModeBlockOStream out(fd);
                        out << text;
                    })) {
                        if (enable_output) {
                            std::cerr << "Server run fail\n";
                        }
                    }
                });
                test_run(client.run("PORT", true, port_arg))
                req_code(200)
                test_run_while_100("STOR", filename)
                req_code(226, 250)
            }

            std::string response;
            {
                MyThread t([&]() {
                    if (!server.run_one([&](int fd) {
                        set_timeout_fd(fd, SO_RCVTIMEO, 1);
                        ModeBlockIStream in(fd);
                        std::copy(std::istreambuf_iterator<char>(in),
                                  std::istreambuf_iterator<char>(),
                                  std::back_inserter(response)
                        );
                    })) {
                        if (enable_output) {
                            std::cerr << "Server run fail\n";
                        }
                    }
                });
                test_run(client.run("PORT", true, port_arg))
                req_code(200)
                test_run_while_100("RETR", filename)
                req_code(226, 250)
            }
            require_eq(response, text, "Expected equal file simple block")
        }
        {
            test("Block Mode BIG")
            open_server_auth("anonymous", "anonymous")
            test_run(client.run("MODE", true, "B"))

            const std::string filename = "test_file_b_BIG";
            Server<int> server(myip, myport);

            std::string text;
            text.resize(100000);
            for (size_t i = 0; i != text.size(); ++i) {
                text[i] = i;
            }
            {
                MyThread t([&]() {
                    if (!server.run_one([&](int fd) {
                        set_timeout_fd(fd, SO_SNDTIMEO, 1);
                        ModeBlockOStream out(fd);
                        out << text;
                    })) {
                        if (enable_output) {
                            std::cerr << "Server run fail\n";
                        }
                    }
                });
                test_run(client.run("PORT", true, port_arg))
                req_code(200)
                test_run_while_100("STOR", filename)
                req_code(226, 250)
            }

            std::string response;
            {
                MyThread t([&]() {
                    if (!server.run_one([&](int fd) {
                        set_timeout_fd(fd, SO_RCVTIMEO, 1);
                        ModeBlockIStream in(fd);
                        std::copy(std::istreambuf_iterator<char>(in),
                                  std::istreambuf_iterator<char>(),
                                  std::back_inserter(response)
                        );
                    })) {
                        if (enable_output) {
                            std::cerr << "Server run fail\n";
                        }
                    }
                });
                test_run(client.run("PORT", true, port_arg))
                req_code(200)
                test_run_while_100("RETR", filename)
                req_code(226, 250)
            }
            require_eq(response, text, "Expected equal file simple block")
        }
        return true;
    }
};


class ModeCompressedTest : public Test {
public:
    ModeCompressedTest(std::string myip, unsigned ip, unsigned port) : Test(std::move(myip), ip, port) {}

    bool operator()(bool enable_output) const final {
        {
            test("Compressed Mode small")
            open_server_auth("anonymous", "anonymous")
            test_run(client.run("MODE", true, "C"))

            const std::string filename = "test_file_c";
            Server<int> server(myip, myport);

            const std::string text = "abcdefghi";
            {
                MyThread t([&]() {
                    if (!server.run_one([&](int fd) {
                        set_timeout_fd(fd, SO_SNDTIMEO, 1);
                        ModeCompressedOStream out(fd);
                        out << text;
                    })) {
                        if (enable_output) {
                            std::cerr << "Server run fail\n";
                        }
                    }
                });
                test_run(client.run("PORT", true, port_arg))
                req_code(200)
                test_run_while_100("STOR", filename)
                req_code(226, 250)
            }

            std::string response;
            {
                MyThread t([&]() {
                    if (!server.run_one([&](int fd) {
                        set_timeout_fd(fd, SO_RCVTIMEO, 1);
                        ModeCompressedIStream in(fd);
                        std::copy(std::istreambuf_iterator<char>(in),
                                  std::istreambuf_iterator<char>(),
                                  std::back_inserter(response)
                        );
                    })) {
                        if (enable_output) {
                            std::cerr << "Server run fail\n";
                        }
                    }
                });
                test_run(client.run("PORT", true, port_arg))
                req_code(200)
                test_run_while_100("RETR", filename)
                req_code(226, 250)
            }
            require_eq(response, text, "Expected equal file simple block")
        }
        {
            test("Compressed Mode med")
            open_server_auth("anonymous", "anonymous")
            test_run(client.run("MODE", true, "C"))

            const std::string filename = "test_file_c_med";
            Server<int> server(myip, myport);

            std::string text;
            text.resize(1000);
            for (size_t i = 0; i != 100; ++i) {
                text[i] = 'a';
            }
            for (size_t i = 0; i != 100; ++i) {
                text[i + 100] = ' ';
            }
            for (size_t i = 0; i != 800; ++i) {
                text[i + 200] = 'x';
            }
            {
                MyThread t([&]() {
                    if (!server.run_one([&](int fd) {
                        set_timeout_fd(fd, SO_SNDTIMEO, 1);
                        ModeCompressedOStream out(fd);
                        out << text;
                    })) {
                        if (enable_output) {
                            std::cerr << "Server run fail\n";
                        }
                    }
                });
                test_run(client.run("PORT", true, port_arg))
                req_code(200)
                test_run_while_100("STOR", filename)
                req_code(226, 250)
            }

            std::string response;
            {
                MyThread t([&]() {
                    if (!server.run_one([&](int fd) {
                        set_timeout_fd(fd, SO_RCVTIMEO, 1);
                        ModeCompressedIStream in(fd);
                        std::copy(std::istreambuf_iterator<char>(in),
                                  std::istreambuf_iterator<char>(),
                                  std::back_inserter(response)
                        );
                    })) {
                        if (enable_output) {
                            std::cerr << "Server run fail\n";
                        }
                    }
                });
                test_run(client.run("PORT", true, port_arg))
                req_code(200)
                test_run_while_100("RETR", filename)
                req_code(226, 250)
            }
            require_eq(response, text, "Expected equal file simple block")
        }
        {
            test("Compressed Mode BIG")
            open_server_auth("anonymous", "anonymous")
            test_run(client.run("MODE", true, "C"))

            const std::string filename = "test_file_c_BIG";
            Server<int> server(myip, myport);

            std::string text;
            text.resize(100000);
            for (size_t i = 0; i != text.size(); ++i) {
                text[i] = i;
            }
            {
                MyThread t([&]() {
                    if (!server.run_one([&](int fd) {
                        set_timeout_fd(fd, SO_SNDTIMEO, 1);
                        ModeCompressedOStream out(fd);
                        out << text;
                    })) {
                        if (enable_output) {
                            std::cerr << "Server run fail\n";
                        }
                    }
                });
                test_run(client.run("PORT", true, port_arg))
                req_code(200)
                test_run_while_100("STOR", filename)
                req_code(226, 250)
            }

            std::string response;
            {
                MyThread t([&]() {
                    if (!server.run_one([&](int fd) {
                        set_timeout_fd(fd, SO_RCVTIMEO, 1);
                        ModeCompressedIStream in(fd);
                        std::copy(std::istreambuf_iterator<char>(in),
                                  std::istreambuf_iterator<char>(),
                                  std::back_inserter(response)
                        );
                    })) {
                        if (enable_output) {
                            std::cerr << "Server run fail\n";
                        }
                    }
                });
                test_run(client.run("PORT", true, port_arg))
                req_code(200)
                test_run_while_100("RETR", filename)
                req_code(226, 250)
            }
            require_eq(response, text, "Expected equal file simple block")
        }
        return true;
    }
};

void alarm(int sig) {
    std::cout << "fail" << std::endl;
    exit(0);
}

void set_timer() {
    signal(SIGALRM, alarm);

    struct itimerval tout_val{};
    tout_val.it_interval.tv_sec = 0;
    tout_val.it_interval.tv_usec = 0;
    tout_val.it_value.tv_sec = 9;
    tout_val.it_value.tv_usec = 0;
    setitimer(ITIMER_REAL, &tout_val,0);
}

int main() {
    auto shost = parse_env_req("HW1_HOST");
    auto sport = parse_env_req("HW1_PORT");
    auto test = parse_env("HW1_TEST");
    auto quiet = parse_env("HW1_QUIET");
    auto users = parse_env("HW1_USERS");
    bool enable_output = !quiet || (quiet.value() != "1");

    std::string myip;
    {
        std::ostringstream ss;
        run_command("ip route get " + shost + " | python3 -c \"print(input().split()[-3])\"", ss);
        myip = ss.str();
        while (isspace(myip.back())) {
            myip.pop_back();
        }
    }

    if (enable_output) {
        std::cerr << shost << std::endl;
    }

    unsigned ip = htonl(inet_addr(shost.c_str()));
    unsigned port = strtoul(sport.c_str(), nullptr, 10);

    std::map<std::string, std::unique_ptr<Test>> tests;
    tests["minimal"] = std::make_unique<MinimalTest>(myip, ip, port);
    tests["dir"] = std::make_unique<DirTest>(myip, ip, port);
    tests["passive"] = std::make_unique<PassiveTest>(myip, ip, port);
    tests["trans-mode-block"] = std::make_unique<ModeBlockTest>(myip, ip, port);
    tests["trans-mode-compressed"] = std::make_unique<ModeCompressedTest>(myip, ip, port);
    tests["auth"] = std::make_unique<AuthTest>(users, myip, ip, port);

    if (test) {
        const auto &ptest = tests[test.value()];
        set_timer();
        if (ptest) {
            if (!(*ptest)(enable_output)) {
                std::cout << "fail\n";
                return 1;
            }
        } else {
            std::cerr << "No such test\n";
            return 1;
        }
    } else {
        for (auto&& [name, test] : tests) {
            if (name == "auth") {
                continue;
            }
            if (!(*test)(enable_output)) {
                std::cout << "fail\n";
                return 1;
            }
        }
    }
    std::cout << "ok\n";
}
