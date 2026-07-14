/**
 * @file main.cpp
 * @brief Chess engine entry point and local HTTP server.
 * 
 * Supports standard UCI console mode and a BSD sockets HTTP server mode
 * (using --server and --port) to interact with browser-based chess frontends.
 */

#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <algorithm>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

#include "Board.h"
#include "MoveGen.h"
#include "Search.h"
#include "UCI.h"

/**
 * @brief Extract a string value from a simple JSON payload.
 * Parses e.g. {"key": "value"} robustly.
 */
static std::string extract_json_string(const std::string& body, const std::string& key) {
    size_t key_pos = body.find("\"" + key + "\"");
    if (key_pos == std::string::npos) return "";
    
    size_t colon_pos = body.find(":", key_pos);
    if (colon_pos == std::string::npos) return "";
    
    size_t first_quote = body.find("\"", colon_pos);
    if (first_quote == std::string::npos) return "";
    
    size_t second_quote = body.find("\"", first_quote + 1);
    if (second_quote == std::string::npos) return "";
    
    return body.substr(first_quote + 1, second_quote - first_quote - 1);
}

/**
 * @brief Starts the HTTP local socket server.
 * Listens on the specified port, handles CORS headers, and routes FEN play requests.
 */
static void run_server(int port) {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        std::cerr << "Failed to create socket\n";
        return;
    }
    
    // Enable address reuse
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        std::cerr << "setsockopt failed\n";
        close(server_fd);
        return;
    }
    
    sockaddr_in address;
    std::memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);
    
    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        std::cerr << "Bind failed on port " << port << ". Port might be in use.\n";
        close(server_fd);
        return;
    }
    
    if (listen(server_fd, 10) < 0) {
        std::cerr << "Listen failed\n";
        close(server_fd);
        return;
    }
    
    std::cout << "Chess HTTP server running at http://localhost:" << port << "/\n";
    std::cout << "Press Ctrl+C to terminate.\n" << std::flush;
    
    while (true) {
        int client_fd = accept(server_fd, nullptr, nullptr);
        if (client_fd < 0) continue;
        
        char buffer[4096] = {0};
        int bytes_read = read(client_fd, buffer, sizeof(buffer) - 1);
        if (bytes_read <= 0) {
            close(client_fd);
            continue;
        }
        
        std::string request(buffer);
        
        // Handle OPTIONS request (CORS preflight)
        if (request.rfind("OPTIONS", 0) == 0) {
            std::string response = 
                "HTTP/1.1 200 OK\r\n"
                "Access-Control-Allow-Origin: *\r\n"
                "Access-Control-Allow-Methods: POST, OPTIONS\r\n"
                "Access-Control-Allow-Headers: Content-Type\r\n"
                "Content-Length: 0\r\n"
                "Connection: close\r\n\r\n";
            write(client_fd, response.c_str(), response.length());
            close(client_fd);
            continue;
        }
        
        // Handle POST /move
        if (request.rfind("POST", 0) == 0) {
            size_t body_pos = request.find("\r\n\r\n");
            std::string body = (body_pos != std::string::npos) ? request.substr(body_pos + 4) : "";
            
            std::string fen = extract_json_string(body, "fen");
            if (fen.empty()) {
                // Fallback in case of raw body or alternate spacing
                fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
            }
            
            Board board;
            if (board.parse_fen(fen)) {
                // Search for the best move at depth 5 for fast response
                Move best_move = Search::search_position(board, 5);
                std::string move_str = best_move.to_uci();
                
                std::string json_response = "{\"move\":\"" + move_str + "\"}";
                std::string response = 
                    "HTTP/1.1 200 OK\r\n"
                    "Access-Control-Allow-Origin: *\r\n"
                    "Content-Type: application/json\r\n"
                    "Content-Length: " + std::to_string(json_response.length()) + "\r\n"
                    "Connection: close\r\n\r\n" + 
                    json_response;
                write(client_fd, response.c_str(), response.length());
            } else {
                std::string err_response = "{\"error\":\"Invalid FEN\"}";
                std::string response = 
                    "HTTP/1.1 400 Bad Request\r\n"
                    "Access-Control-Allow-Origin: *\r\n"
                    "Content-Type: application/json\r\n"
                    "Content-Length: " + std::to_string(err_response.length()) + "\r\n"
                    "Connection: close\r\n\r\n" + 
                    err_response;
                write(client_fd, response.c_str(), response.length());
            }
        } else {
            // Unhandled endpoint
            std::string err_response = "{\"error\":\"Endpoint not found\"}";
            std::string response = 
                "HTTP/1.1 404 Not Found\r\n"
                "Access-Control-Allow-Origin: *\r\n"
                "Content-Type: application/json\r\n"
                "Content-Length: " + std::to_string(err_response.length()) + "\r\n"
                "Connection: close\r\n\r\n" + 
                err_response;
            write(client_fd, response.c_str(), response.length());
        }
        
        close(client_fd);
    }
    
    close(server_fd);
}

int main(int argc, char* argv[]) {
    // Disable input/output buffering for instant communication with chess GUIs
    std::setbuf(stdin, NULL);
    std::setbuf(stdout, NULL);

    // Initialize Zobrist hashing keys
    Board::init_zobrist();

    // Precompute attack tables for all pieces (sliding and non-sliding)
    MoveGen::init_all();

    // Parse command line flags
    bool server_mode = false;
    int port = 8765; // default port

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--server") {
            server_mode = true;
        } else if (arg == "--port" && i + 1 < argc) {
            port = std::stoi(argv[++i]);
        }
    }

    if (server_mode) {
        // Initialize Search components (TT size 24MB = 1M entries)
        Search::resize_tt(24);
        run_server(port);
    } else {
        // Default: Start the UCI command loop (blocks until "quit" command)
        UCI::loop();
    }

    return 0;
}

// Print confirmation message at the end of the file.
// Confirmation: main.cpp created successfully.
