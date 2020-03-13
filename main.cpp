#include <iostream>
#include <fstream>
#include <vector>
#include <cstring>
#include <algorithm>
#include <cmath>

struct Edge {
    double weight;
    unsigned from;
    unsigned to;

    bool operator<(const Edge &other) {
        return std::tie(from, to) < std::tie(other.from, other.to);
    }
};

class Graph {
public:
    Graph(unsigned vertexes_, std::vector<Edge> edges) : edges_(std::move(edges)),
                                                         ids_(vertexes_ + 1),
                                                         vertexes(vertexes_) {
        std::sort(edges_.begin(), edges_.end());
        unsigned cur = 0;
        for (unsigned vid = 0; vid != vertexes; ++vid) {
            while (cur != edges_.size() && edges_[cur].from == vid) {
                ++cur;
            }
            ids_[vid + 1] = cur;
        }
    }

    auto getEdges(unsigned vertex) const {
        return edges_.begin() + ids_[vertex];
    }

    const auto& getEdges() const {
        return edges_;
    }

private:
    struct AlwaysTrue {
        bool operator()(unsigned) {
            return true;
        }
        bool operator()(unsigned,unsigned) {
            return true;
        }
    };

public:
    /// Dijkstra Algorithm
    template <class VertexFilter = AlwaysTrue, class EdgeFilter = AlwaysTrue>
    std::optional<double> find_min_path(unsigned from, unsigned to, std::vector<unsigned> &path_reversed,
                                        VertexFilter vfilter = AlwaysTrue{}, EdgeFilter efilter = AlwaysTrue{}) const {
        path_reversed.clear();
        std::vector<unsigned> parents(vertexes, std::numeric_limits<unsigned>::max());
        std::vector<std::tuple<double, unsigned, unsigned>> dist_heap{{0, from, from}};
        while (!dist_heap.empty()) {
            std::pop_heap(dist_heap.begin(), dist_heap.end(), std::greater<>{});
            auto [dist, vertex, parent] = dist_heap.back();
            dist_heap.pop_back();
            if (parents[vertex] != std::numeric_limits<unsigned>::max()) {
                continue;
            }
            parents[vertex] = parent;

            if (vertex == to) {
                /// Path found
                path_reversed.push_back(to);
                do {
                    to = parents[to];
                    path_reversed.push_back(to);
                } while (parents[to] != to);
                return {dist};
            }
            for (auto edge = getEdges(vertex); edge != getEdges(vertex + 1); ++edge) {
                if (!vfilter(edge->to) || !efilter(vertex, edge->to)) {
                    continue;
                }
                dist_heap.emplace_back(dist + edge->weight, edge->to, vertex);
                std::push_heap(dist_heap.begin(), dist_heap.end(), std::greater<>{});
            }
        }
        /// No path
        return {};
    }

private:
    std::vector<Edge> edges_;
    std::vector<unsigned> ids_;

public:
    unsigned vertexes;
};

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
    double lon{};
    double lat{};
    unsigned id{};
};

bool strip_cmp(std::string s, std::string pattern) {
    size_t from = 0;
    while (from < s.size() && isspace(s[from])) {
        ++from;
    }
    size_t to = s.size();
    while (to > from && (s[to-1] == '\0' || isspace(s[to - 1]))) {
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

double calc_distance(const Node &f, const Node &s) {
    double dLat = (f.lat - s.lat) *
                  M_PI / 180.0;
    double dLon = (f.lon - s.lon) *
                  M_PI / 180.0;

    double lat_f_rad = (f.lat) * M_PI / 180.0;
    double lat_s_rad = (s.lat) * M_PI / 180.0;

    double a = pow(sin(dLat / 2), 2) +
               pow(sin(dLon / 2), 2) *
               cos(lat_f_rad) * cos(lat_s_rad);
    constexpr auto kRad = 6371;
    double c = 2 * asin(sqrt(a));
    return std::abs(kRad * c);
}

int main(int argc, char ** argv) {
    std::string filename;
    std::optional<unsigned> source;
    std::optional<unsigned> destination;
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
    if (!in.good()) {
        std::cerr << "Can not read data from " << filename << std::endl;
        return 1;
    }
    if (filename.substr(filename.size() - 4, 4) == ".gml") {
        filename.resize(filename.size() - 4);
    }

    std::vector<Node> nodes;
    {
        std::string fline;
        if (std::getline(in, fline); !strip_cmp(fline, "graph [")) {
            std::cerr << "Invalid file format" << std::endl;
            return 1;
        }
    }
    std::string label;
    std::vector<Edge> edges;
    for (std::string line; std::getline(in, line);) {
        if (strip_cmp(line, "node [")) {
            Node node;
            in >> label;
            while (!strip_cmp(label, "]")) {
                if (label == "label") {
                    if (!std::getline(in, node.label)) {
                        std::cerr << "Invalid file format" << std::endl;
                        return 1;
                    }
                    in >> label;
                }
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
                }
                line.clear();
                if (!std::getline(in, line)) {
                    std::cerr << "Invalid file format" << std::endl;
                    return 1;
                }
                in >> label;
            }
            if (node.id <= nodes.size()) {
                nodes.resize(node.id + 1);
            }
            nodes[node.id] = std::move(node);
        } else if (strip_cmp(line, "edge [")) {
            while (strip_cmp(line, "edge [")) {
                unsigned from{};
                unsigned to{};
                in >> label;
                while (!strip_cmp(label, "]")) {
                    if (label == "target") {
                        in >> to;
                    } else if (label == "source") {
                        in >> from;
                    }
                    line.clear();
                    if (!std::getline(in, line)) {
                        std::cerr << "Invalid file format" << std::endl;
                        return 1;
                    }
                    in >> label;
                }
                double dist = calc_distance(nodes[from], nodes[to]);
                edges.push_back(Edge{dist, from, to});
                edges.push_back(Edge{dist, to, from});
                if (!std::getline(in, line)) {
                    std::cerr << "Invalid file format" << std::endl;
                    return 1;
                }
                line.clear();
                if (!std::getline(in, line)) {
                    std::cerr << "Invalid file format" << std::endl;
                    return 1;
                }
            }
            break;
        }
    }
    in.close();

    Graph graph(nodes.size(), std::move(edges));
    constexpr auto kDelayPerKM = 4.83;

    {
        std::ofstream out;
        out.open(filename + "_topo.csv");
        if (!out.good()) {
            std::cerr << "Can not create topo file" << std::endl;
            return 1;
        }
        out << "Node 1 (id),Node 1 (label),Node 1 (longitude),Node 1 (latitude),"
               "Node 2 (id),Node 2 (label),Node 2 (longitude),Node 2 (latitude),"
               "Distance (km), Delay (mks)\n";
        for (auto && edge : graph.getEdges()) {
            auto && from = nodes[edge.from];
            auto && to = nodes[edge.to];
            out << from.id << ',' << from.label << ',' << from.lon << ',' << from.lat << ',';
            out << to.id << ',' << to.label << ',' << to.lon << ',' << to.lat << ',';
            out << edge.weight << ',' << edge.weight * kDelayPerKM << '\n';
        }
        out.close();
    }

    {
        std::ofstream out;
        out.open(filename + "_routes.csv");
        if (!out.good()) {
            std::cerr << "Can not create routes file" << std::endl;
            return 1;
        }
        out << "Node 1 (id),Node 2 (id),Path type,Path,Delay (mks)\n";

        unsigned from = 0;
        unsigned end_from = nodes.size() - 1;
        if (source) {
            from = source.value();
            end_from = from + 1;
        }
        std::vector<unsigned> path;

        for (; from != end_from; ++from) {
            unsigned to = from + 1;
            unsigned end_to = nodes.size();
            if (destination) {
                to = destination.value();
                end_to = to + 1;
            }
            for (; to != end_to; ++to) {
                out << from << ',' << to << ",main,";
                auto dist = graph.find_min_path(from, to, path);
                if (!dist) {
                    out << "no,\n";
                    continue;
                }
                out << "\"[" << *path.rbegin();
                for (auto it = path.rbegin() + 1; it != path.rend(); ++it) {
                    out << "," << *it;
                }
                out << "]\"," << dist.value() * kDelayPerKM << '\n';
                if (!reserve) {
                    continue;
                }
                out << from << ',' << to << ",reserve,";
                std::vector<bool> vfilter(nodes.size(), true);
                std::vector<unsigned> efilter(nodes.size(), std::numeric_limits<unsigned>::max());
                auto prev = to;
                for (auto elem : path) {
                    if (elem != prev) {
                        efilter[elem] = prev;
                        prev = elem;
                    }
                    vfilter[elem] = false;
                }
                vfilter[from] = true;
                vfilter[to] = true;
                dist = graph.find_min_path(from, to, path,
                                           [&](unsigned v) { return vfilter[v]; },
                                           [&](unsigned u, unsigned v) { return efilter[u] != v && efilter[v] != u; });
                unsigned allow_idx = 0;
                for (;!dist && allow_idx < vfilter.size(); ++allow_idx) {
                    if (vfilter[allow_idx]) {
                        continue;
                    }
                    vfilter[allow_idx] = true;
                    dist = graph.find_min_path(from, to, path,
                                               [&](unsigned v) { return vfilter[v]; },
                                               [&](unsigned u, unsigned v) { return efilter[u] != v && efilter[v] != u; });
                    vfilter[allow_idx] = false;
                }
                if (!dist) {
                    out << "no,\n";
                    continue;
                }
                out << "\"[" << *path.rbegin();
                for (auto it = path.rbegin() + 1; it != path.rend(); ++it) {
                    out << "," << *it;
                }
                out << "]\"," << dist.value() * kDelayPerKM << '\n';
            }
        }
    }
}
