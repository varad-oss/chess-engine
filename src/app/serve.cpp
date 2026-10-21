// `serve`: a deliberately small HTTP/1.1 front end for the browser demo.
//
//   POST /move?movetime=MS   body: startpos [moves ...] | fen <fen> [moves ...]
//   GET  /health
//
// The body reuses the UCI "position" grammar, so the engine sees the whole game
// (and therefore repetitions), not just the current FEN. Responses are JSON.
//
// Hardening, since this listens on a socket:
//   * binds to 127.0.0.1 unless --host is given explicitly;
//   * one connection at a time, with receive/send timeouts;
//   * header and body size limits, Content-Length required for bodies;
//   * think time is clamped to [10 ms, --max-movetime];
//   * CORS responses are only granted to local origins (file:// pages send
//     "Origin: null"), so other websites cannot read engine replies.
//
// POSIX sockets only.
#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "app/commands.h"
#include "core/movegen.h"
#include "engine/search.h"
#include "uci/uci.h"

namespace chess {

namespace {

constexpr size_t MAX_HEADER_BYTES = 8 * 1024;
constexpr size_t MAX_BODY_BYTES = 16 * 1024;

struct Request {
    std::string method, path, query, origin, body;
};

struct Response {
    int status = 200;
    std::string body;
};

const char* reason_phrase(int status) {
    switch (status) {
        case 200: return "OK";
        case 204: return "No Content";
        case 400: return "Bad Request";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 408: return "Request Timeout";
        case 413: return "Payload Too Large";
        default: return "Error";
    }
}

std::string json_string(const std::string& s) {
    std::string out = "\"";
    for (const char c : s) {
        if (c == '"' || c == '\\') out += '\\', out += c;
        else if (static_cast<unsigned char>(c) < 0x20) out += ' ';
        else out += c;
    }
    return out + "\"";
}

std::string error_json(const std::string& message) { return "{\"error\":" + json_string(message) + "}"; }

bool is_local_origin(const std::string& origin) {
    return origin == "null" || origin.rfind("http://localhost", 0) == 0 || origin.rfind("http://127.0.0.1", 0) == 0;
}

std::string lowercase(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return s;
}

// Reads and parses one request. Returns an HTTP status: 200 if a request was
// read, otherwise the error to send back (0 = connection closed, send nothing).
int read_request(int fd, Request& req) {
    std::string data;
    char buf[4096];
    size_t header_end = std::string::npos;
    while ((header_end = data.find("\r\n\r\n")) == std::string::npos) {
        if (data.size() > MAX_HEADER_BYTES) return 413;
        const ssize_t n = recv(fd, buf, sizeof(buf), 0);
        if (n == 0) return 0;
        if (n < 0) return errno == EAGAIN || errno == EWOULDBLOCK ? 408 : 0;
        data.append(buf, size_t(n));
    }

    std::istringstream head(data.substr(0, header_end));
    std::string line, version;
    std::getline(head, line);
    std::istringstream request_line(line);
    if (!(request_line >> req.method >> req.path >> version)) return 400;
    if (const size_t q = req.path.find('?'); q != std::string::npos) {
        req.query = req.path.substr(q + 1);
        req.path.resize(q);
    }

    size_t content_length = 0;
    while (std::getline(head, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        const std::string name = lowercase(line.substr(0, colon));
        std::string value = line.substr(colon + 1);
        value.erase(0, value.find_first_not_of(" \t"));
        if (name == "content-length") {
            try {
                content_length = std::stoul(value);
            } catch (...) {
                return 400;
            }
            if (content_length > MAX_BODY_BYTES) return 413;
        } else if (name == "origin") {
            req.origin = value;
        } else if (name == "transfer-encoding") {
            return 400;  // chunked bodies are not supported
        }
    }

    req.body = data.substr(header_end + 4);
    while (req.body.size() < content_length) {
        const ssize_t n = recv(fd, buf, sizeof(buf), 0);
        if (n <= 0) return n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK) ? 408 : 0;
        req.body.append(buf, size_t(n));
    }
    req.body.resize(content_length);
    return 200;
}

void send_response(int fd, const Response& r, const std::string& origin) {
    std::ostringstream out;
    out << "HTTP/1.1 " << r.status << ' ' << reason_phrase(r.status) << "\r\n"
        << "Content-Type: application/json\r\n"
        << "Content-Length: " << r.body.size() << "\r\n"
        << "Connection: close\r\n";
    if (is_local_origin(origin))
        out << "Access-Control-Allow-Origin: " << origin << "\r\n"
            << "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
            << "Access-Control-Allow-Headers: Content-Type\r\n"
            << "Vary: Origin\r\n";
    out << "\r\n" << r.body;
    const std::string data = out.str();
    size_t sent = 0;
    while (sent < data.size()) {
        const ssize_t n = send(fd, data.data() + sent, data.size() - sent, 0);
        if (n <= 0) return;
        sent += size_t(n);
    }
}

std::string game_status(const Position& pos) {
    MoveList moves;
    generate<GenType::All>(pos, moves);
    if (moves.empty()) return pos.in_check() ? "checkmate" : "stalemate";
    if (pos.is_insufficient_material()) return "draw_insufficient_material";
    if (pos.rule50() >= 100) return "draw_fifty_moves";
    if (pos.is_repetition(0)) return "draw_repetition";
    return "ongoing";
}

struct ServerConfig {
    std::string host = "127.0.0.1";
    int port = 8765;
    std::int64_t movetime = 1000;
    std::int64_t max_movetime = 10000;
    int threads = 1;
    int hash = 64;
};

Response handle_move(Engine& engine, const ServerConfig& cfg, const Request& req) {
    std::int64_t movetime = cfg.movetime;
    for (std::istringstream q(req.query); q.good();) {
        std::string kv;
        std::getline(q, kv, '&');
        if (kv.rfind("movetime=", 0) == 0) {
            try {
                movetime = std::stoll(kv.substr(9));
            } catch (...) {
                return {400, error_json("movetime must be an integer (milliseconds)")};
            }
        }
    }
    movetime = std::clamp<std::int64_t>(movetime, 10, cfg.max_movetime);

    std::istringstream body(req.body);
    std::vector<std::string> tokens;
    for (std::string t; body >> t;) tokens.push_back(t);
    Position pos;
    std::string error;
    if (!parse_position(tokens, 0, pos, error)) return {400, error_json(error)};

    const std::string before = game_status(pos);
    if (before != "ongoing") return {200, "{\"bestmove\":null,\"status\":\"" + before + "\"}"};

    SearchLimits limits;
    limits.movetime = movetime;
    const SearchResult r = engine.search(pos, limits);
    const bool mate = is_mate_score(r.score);
    const std::string score = Uci::format_score(r.score);  // "cp 25" or "mate 3"

    Position after = pos;
    after.make_move(r.best_move);
    std::ostringstream out;
    out << "{\"bestmove\":" << json_string(r.best_move.uci()) << ",\"score\":{\"type\":\""
        << (mate ? "mate" : "cp") << "\",\"value\":" << score.substr(score.find(' ') + 1) << "}"
        << ",\"depth\":" << r.depth << ",\"nodes\":" << r.nodes << ",\"time\":" << r.time_ms
        << ",\"status\":\"" << game_status(after) << "\"}";
    return {200, out.str()};
}

Response route(Engine& engine, const ServerConfig& cfg, const Request& req) {
    if (req.method == "OPTIONS") return {204, ""};
    if (req.path == "/health") {
        if (req.method != "GET") return {405, error_json("use GET")};
        return {200, std::string("{\"status\":\"ok\",\"engine\":") + json_string(ENGINE_NAME) + "}"};
    }
    if (req.path == "/move") {
        if (req.method != "POST") return {405, error_json("use POST")};
        return handle_move(engine, cfg, req);
    }
    return {404, error_json("not found")};
}

}  // namespace

// serve [--host H] [--port P] [--movetime MS] [--max-movetime MS] [--threads N] [--hash MB]
int serve_command(const Args& args) {
    ServerConfig cfg;
    try {
        for (size_t i = 1; i < args.size(); ++i) {
            const std::string& a = args[i];
            if (i + 1 >= args.size()) throw std::runtime_error("missing value for " + a);
            const std::string& v = args[++i];
            if (a == "--host") cfg.host = v;
            else if (a == "--port") cfg.port = std::stoi(v);
            else if (a == "--movetime") cfg.movetime = std::stoll(v);
            else if (a == "--max-movetime") cfg.max_movetime = std::stoll(v);
            else if (a == "--threads") cfg.threads = std::stoi(v);
            else if (a == "--hash") cfg.hash = std::stoi(v);
            else throw std::runtime_error("unknown option " + a);
        }
        if (cfg.port < 1 || cfg.port > 65535 || cfg.threads < 1 || cfg.hash < 1 || cfg.max_movetime < 10)
            throw std::runtime_error("option out of range");
    } catch (const std::exception& e) {
        std::cerr << "serve: " << e.what()
                  << "\nusage: chess-engine serve [--host 127.0.0.1] [--port 8765] [--movetime 1000] "
                     "[--max-movetime 10000] [--threads 1] [--hash 64]\n";
        return 2;
    }

    signal(SIGPIPE, SIG_IGN);
    const int server = socket(AF_INET, SOCK_STREAM, 0);
    if (server < 0) {
        std::perror("socket");
        return 1;
    }
    const int yes = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(std::uint16_t(cfg.port));
    if (inet_pton(AF_INET, cfg.host.c_str(), &addr.sin_addr) != 1) {
        std::cerr << "serve: --host must be an IPv4 address\n";
        close(server);
        return 2;
    }
    if (bind(server, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 || listen(server, 16) != 0) {
        std::perror("bind/listen");
        close(server);
        return 1;
    }
    if (cfg.host != "127.0.0.1")
        std::cerr << "warning: listening on " << cfg.host << "; the API is reachable from other machines\n";
    std::cerr << "serving on http://" << cfg.host << ':' << cfg.port << "  (POST /move, GET /health)\n";

    Engine engine;
    engine.set_threads(cfg.threads);
    engine.set_hash_mb(std::size_t(cfg.hash));

    while (true) {
        const int client = accept(server, nullptr, nullptr);
        if (client < 0) {
            if (errno == EINTR) continue;
            std::perror("accept");
            break;
        }
        timeval timeout{5, 0};
        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
        Request req;
        const int status = read_request(client, req);
        if (status == 200) send_response(client, route(engine, cfg, req), req.origin);
        else if (status != 0) send_response(client, {status, error_json(reason_phrase(status))}, req.origin);
        close(client);
    }
    close(server);
    return 1;
}

}  // namespace chess
