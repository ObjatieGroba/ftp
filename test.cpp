#define TESTS

#include <map>
#include <memory>
#include <vector>
#include <algorithm>
#include <thread>

#include "tools.hpp"

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
            std::cerr << "Warning, there are no second command " << command << std::endl;
            return true;
        }
        const auto &codes = it->second;
        auto it2 = std::find(codes.begin(), codes.end(), code);
        return it2 != codes.end();
    }

    FTPClient(int fd) : in(fd), out(fd) {
        set_timeout_fd(fd, SO_SNDTIMEO, 1);
        set_timeout_fd(fd, SO_RCVTIMEO, 1);
        in.dismiss();
        run("ON_START", false);
    }

    Result run(std::string command, bool send = true, const std::string &args = "") {
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
            std::cout << s << std::endl;
            unsigned code = (s[0] - '0') * 100 + (s[1] - '0') * 10 + (s[2] - '0');
            std::string wait_till = s.substr(0, 4);
            wait_till[3] = ' ';
            std::string data;
            while (s.substr(0, wait_till.size()) != wait_till) {
                s = read_till_end(in);
                std::cout << s << std::endl;
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

private:
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
                auto result = client.run(cmd.first, "test");
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
res = client.run("USER", name); \
    if (!res.error.empty()) {\
        if (enable_output) {\
            std::cerr << res.error << std::endl;\
        }\
    return false;\
}\
if (res.code == 331) {\
    res = client.run("PASS", pass);\
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
            test_run( client.run("mOde", true, "s"))
            req_code(200)

            test_run( client.run("NOOP"))
            req_code(200)

            /// Port change
            test_run( client.run("PORT", true, "1,2,3,4,5,6"))
            req_code(200)
            test_run( client.run("PORT", true, "4,4,4,4,0,80"))
            req_code(200)
        }
        {
            test("Relogin")
            open_server_auth("anonymous", "anonymous")

            test_run(client.run("USER", true, "abcde"))
            req_code(230)
            test_run(client.run("USER", true, "anonymous"))
            req_code(230)
        }
        {
            test("Data send and recv")
            open_server_auth("anonymous", "anonymous")

            const std::string text = "abcde\n";
            Server<int> server(myip, myport);

            std::thread([&]() {
                if (!server.run_one([&](int fd) {
                    set_timeout_fd(fd, SO_SNDTIMEO, 1);
                    FDOStream out(fd);
                    out << text;
                })) {
                    if (enable_output) {
                        std::cerr << "Server run fail\n";
                    }
                }
            }).detach();
            test_run(client.run("PORT", true, port_arg))
            req_code(200)
            test_run_while_100("STOR", "test_file_001")
            req_code(226, 250)

            std::string response;
            std::thread([&]() {
                if (!server.run_one([&](int fd) {
                    set_timeout_fd(fd, SO_RCVTIMEO, 1);
                    FDIStream in(fd);
                    std::copy(std::istreambuf_iterator<char>(in),
                              std::istreambuf_iterator<char>(),
                              std::back_inserter(response)
                    );
                    in >> response;
                })) {
                    if (enable_output) {
                        std::cerr << "Server run fail\n";
                    }
                }
            }).detach();
            test_run(client.run("PORT", true, port_arg))
            req_code(200)
            test_run_while_100("RETR", "test_file_001")
            req_code(226, 250)
            require_eq(response, text, "Expected equal file");
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

int main() {
    auto shost = parse_env_req("HW1_HOST");
    auto sport = parse_env_req("HW1_PORT");
    auto test = parse_env("HW1_TEST");
    auto quiet = parse_env("HW1_QUIET");
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

    unsigned ip = htonl(inet_addr(shost.c_str()));
    unsigned port = strtoul(sport.c_str(), nullptr, 10);

    std::map<std::string, std::unique_ptr<Test>> tests;
    tests["minimal"] = std::make_unique<MinimalTest>(myip, ip, port);

    if (test) {
        const auto &ptest = tests[test.value()];
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
        for (auto&& ptest : tests) {
            if (!(*ptest.second)(enable_output)) {
                std::cout << "fail\n";
                return 1;
            }
        }
    }
    std::cout << "ok\n";
}
