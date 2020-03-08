#include <iostream>
#include <fstream>
#include <vector>
#include <cstring>

template <class T>
class Matrix {
    std::vector<T> data;
    size_t n;

    struct InnerIndexer {
        Matrix<T> &m;
        size_t i;

        const T& operator[](size_t j) const {
            return m.data[i * m.n + j];
        }

        T& operator[](size_t j) {
            return m.data[i * m.n + j];
        }
    };

public:
    Matrix(size_t n_, T t = T()) : data(n_ * n_, t), n(n_) { }

    InnerIndexer operator[](size_t i) const {
        return InnerIndexer{*this, i};
    }
};

struct Node {
    std::string label;
    double lon;
    double lat;
    unsigned id;
};

bool strip_cmp(std::string s, std::string pattern) {
    size_t from = 0;
    while (from < s.size() && isspace(s[from])) {
        ++from;
    }
    size_t to = s.size();
    while (to > from && isspace(s[to - 1])) {
        --to;
    }
    if (pattern.size() != to - from) {
        return false;
    }
    size_t i = 0;
    while (from != to) {
        if (pattern[i++] != s[from++]) {
            return false;
        }
    }
    return true;
}

int main(int argc, char ** argv) {
    std::string filename;
    unsigned source;
    unsigned destination;
    bool reserve = false;
    for (int i = 1; i != argc; ++i) {
        if (strcmp(argv[i], "-r") == 0) {
            reserve = true;
        } else if (strcmp(argv[i], "-t") == 0) {
            filename = argv[++i];
        } else if (strcmp(argv[i], "-s") == 0) {
            source = atol(argv[++i]);
        } else if (strcmp(argv[i], "-d") == 0) {
            destination = atol(argv[++i]);
        } else {
            std::cerr << "Unknown argument " << argv[i] << std::endl;
            return 1;
        }
    }

    std::ifstream in;
    in.open(filename);
    if (!in.is_open()) {
        std::cerr << "Can not read data from " << filename << std::endl;
        return 1;
    }

    std::vector<Node> nodes;
    std::string line(256, '\0');
    if (in.getline(line.data(), line.size() - 1); strip_cmp(line, "graph [")) {
        std::cerr << "Invalid file format" << std::endl;
        return 1;
    }
    std::string label;
    while (in.getline(line.data(), line.size() - 1)) {
        if (strip_cmp(line, "node [")) {
            Node node;
            in >> label;
            while (strip_cmp(label, "]")) {
                if (label == "id") {
                    if (!(in >> node.id)) {
                        std::cerr << "Invalid file format" << std::endl;
                        return 1;
                    }
                } else if (label == "Longitude") {
                    if (!(in >> node.lon)) {
                        std::cerr << "Invalid file format" << std::endl;
                        return 1;
                    }
                } else if (label == "Latitude") {
                    if (!(in >> node.lat)) {
                        std::cerr << "Invalid file format" << std::endl;
                        return 1;
                    }
                } else if (label == "label") {
                    in.ignore(1);
                    if (!(in >> node.label)) {
                        std::cerr << "Invalid file format" << std::endl;
                        return 1;
                    }
                    node.label.resize(node.label.size() - 1);
                }
                in.getline(line.data(), line.size() - 1);
                in >> label;
            }
            if (node.id < nodes.size()) {
                nodes.resize(node.id);
            }
            nodes[node.id] = std::move(node);
        } else if (strip_cmp(line, "edge [")) {
            break;
        }
    }

    Matrix<double> delays(nodes.size());

    while (strip_cmp(line, "edge [")) {
        unsigned from;
        unsigned to;
        while (strip_cmp(label, "]")) {
            if (label == "http") {
                in >> node.id;
            } else if (label == "label") {
                in.ignore(1);
                in >> node.label;
                node.label.resize(node.label.size() - 1);
            }
            in.getline(line.data(), line.size() - 1);
            in >> label;
        }
        if (!in.getline(line.data(), line.size() - 1)) {
            break;
        }
    }

    Matrix<unsigned> parents(nodes.size());


}
