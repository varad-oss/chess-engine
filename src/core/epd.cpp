#include "epd.h"

#include <cctype>
#include <charconv>
#include <fstream>
#include <sstream>
#include <string>

namespace chess {

bool load_perft_suite(const std::string& path, std::vector<PerftCase>& out, std::string* error) {
    std::ifstream in(path);
    if (!in) {
        if (error) *error = "cannot open " + path;
        return false;
    }
    std::string line;
    int line_no = 0;
    while (std::getline(in, line)) {
        ++line_no;
        if (line.empty() || line[0] == '#') continue;
        std::istringstream fields(line);
        PerftCase c;
        std::string chunk;
        std::getline(fields, c.fen, ';');
        while (!c.fen.empty() && std::isspace(static_cast<unsigned char>(c.fen.back()))) c.fen.pop_back();
        while (std::getline(fields, chunk, ';')) {
            std::istringstream op(chunk);
            std::string tag;
            std::uint64_t nodes = 0;
            int depth = 0;
            const bool ok = (op >> tag >> nodes) && tag.size() >= 2 && tag[0] == 'D' &&
                            std::from_chars(tag.data() + 1, tag.data() + tag.size(), depth).ptr ==
                                tag.data() + tag.size();
            if (!ok || depth < 1) {
                if (error) *error = path + ':' + std::to_string(line_no) + ": bad depth entry";
                return false;
            }
            c.depths.emplace_back(depth, nodes);
        }
        out.push_back(std::move(c));
    }
    return true;
}

}  // namespace chess
