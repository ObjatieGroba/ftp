#ifndef FTP_TOOLS_HPP
#define FTP_TOOLS_HPP

#include <array>
#include <map>
#include <vector>

#include <unistd.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/stat.h>

#include "streams.hpp"
#include "server.hpp"

bool check_working_directory(const std::string &filename, const std::string &full_working_path) {
#ifdef PATH_MAX
    auto path_max = PATH_MAX;
#else
    auto path_max = pathconf(filename.c_str(), _PC_PATH_MAX);
    if (path_max <= 0) {
        path_max = 4096;
    }
#endif
    std::vector<char> buf(path_max + 1);
    char *full_path = realpath(filename.c_str(), buf.data());
    if (full_path == nullptr) {
        /// Can not check path
        return false;
    }
    return strncmp(full_path, full_working_path.c_str(), full_working_path.size()) == 0;
}

bool check_file_read_access(const std::string &filename, const std::string &full_working_path) {
    int fd = open(filename.c_str(), O_RDONLY);
    if (fd == -1) {
        return false;
    }
    close(fd);
    struct stat st{};
    if (stat(filename.c_str(), &st) != 0) {
        return false;
    }
    if ((st.st_mode & S_IFDIR) != 0) {
        /// It is directory
        return false;
    }
    return check_working_directory(filename, full_working_path);
}

bool check_file_write_access(const std::string &filename, const std::string &full_working_path, int mode=O_CREAT) {
    int fd = open(filename.c_str(), mode, 0600);
    if (fd == -1) {
        return false;
    }
    close(fd);
    struct stat st{};
    if (stat(filename.c_str(), &st) != 0) {
        return false;
    }
    if ((st.st_mode & S_IFDIR) != 0) {
        /// It is directory
        return false;
    }
    return check_working_directory(filename, full_working_path);
}

bool check_folder_exists_access(const std::string &filename, const std::string &full_working_path) {
    int fd = open(filename.c_str(), O_RDONLY | O_DIRECTORY);
    if (fd < 0) {
        return false;
    }
    close(fd);
    return check_working_directory(filename, full_working_path);
}

int open_connection(unsigned ip, unsigned port) {
    int sock;
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        return -1;
    }
    int synRetries = 1;
    if (setsockopt(sock, IPPROTO_TCP, TCP_SYNCNT, &synRetries, sizeof(synRetries)) < 0) {
        close(sock);
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

bool run_command(const std::string &cmd, std::ostream &out) {
    {
        FDOStream *s;
        if ((s = dynamic_cast<FDOStream *>(&out)) != nullptr) {
            if (!set_timeout_fd(s->get_fd(), SO_SNDTIMEO)) {
                return false;
            }
        }
    }
    {
        ModeBlockOStream *s;
        if ((s = dynamic_cast<ModeBlockOStream *>(&out)) != nullptr) {
            if (!set_timeout_fd(s->get_fd(), SO_SNDTIMEO)) {
                return false;
            }
        }
    }
    {
        ModeCompressedOStream *s;
        if ((s = dynamic_cast<ModeCompressedOStream *>(&out)) != nullptr) {
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

bool write_file(const std::string &filename, int mode, std::istream &in) {
    {
        FDIStream *s;
        if ((s = dynamic_cast<FDIStream *>(&in)) != nullptr) {
            if (!set_timeout_fd(s->get_fd(), SO_SNDTIMEO)) {
                return false;
            }
        }
    }
    {
        ModeBlockIStream *s;
        if ((s = dynamic_cast<ModeBlockIStream *>(&in)) != nullptr) {
            if (!set_timeout_fd(s->get_fd(), SO_SNDTIMEO)) {
                return false;
            }
        }
    }
    {
        ModeCompressedIStream *s;
        if ((s = dynamic_cast<ModeCompressedIStream *>(&in)) != nullptr) {
            if (!set_timeout_fd(s->get_fd(), SO_SNDTIMEO)) {
                return false;
            }
        }
    }
    std::ofstream file;
    if (mode & O_APPEND) {
        file.open(filename, std::fstream::out | std::fstream::app);
    } else {
        file.open(filename, std::fstream::out);
    }
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
    if (c == EOF) {
        throw std::runtime_error("Unexpected End Of Line");
    }
    return res;
}

std::tuple<std::map<std::string, std::string>, bool> read_db(const std::optional<std::string> &filename2,
                                                             const std::optional<std::string> &is_disabled) {
    bool need_login = !is_disabled || is_disabled.value() != "1";

    if (!need_login) {
        return {{}, need_login};
    }

    if (!filename2) {
        std::cerr << "No file with passes\n";
        exit(1);
    }

    const auto& filename = filename2.value();
    std::map<std::string, std::string> passes;
    std::ifstream file;
    file.open(filename, std::ios_base::in);
    if (!file) {
        std::cerr << "No file with passes\n";
        exit(1);
    }
    std::string line;
    std::getline(file, line);
    while (!file.eof()) {
        std::getline(file, line);
        if (line.back() == '\n') {
            line.pop_back();
        }
        if (std::all_of(line.begin(), line.end(), [](char c) { return !isalnum(c); })) {
            continue;
        }
        auto pos = line.find('\t');
        if (line.find('\t', pos + 1) != std::string::npos) {
            throw std::runtime_error("Bad file format");
        }
        passes[line.substr(0, pos)] = line.substr(pos + 1);
    }
    return std::make_tuple(std::move(passes), need_login);
}

std::optional<std::string> parse_env(const std::string &name) {
    char * str = getenv(name.c_str());
    if (str == nullptr) {
        return {};
    }
    return std::string(str);
}

std::string parse_env_req(const std::string &name) {
    char * str = getenv(name.c_str());
    if (str == nullptr) {
        std::cerr << "Specify " << name << std::endl;
        exit(1);
    }
    return str;
}


#endif //FTP_TOOLS_HPP
