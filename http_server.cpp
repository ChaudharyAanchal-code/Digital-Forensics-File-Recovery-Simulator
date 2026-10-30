#include "forensics/http_server.hpp"

#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET socket_handle;
static const socket_handle invalid_socket_handle = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int socket_handle;
static const socket_handle invalid_socket_handle = -1;
#endif

namespace forensics {
namespace {

struct HttpRequest {
    std::string method;
    std::string path;
    std::string body;
};

struct HttpResponse {
    int status;
    std::string content_type;
    std::string body;
};

void close_socket(socket_handle socket) {
#ifdef _WIN32
    closesocket(socket);
#else
    close(socket);
#endif
}

bool send_all(socket_handle socket, const std::string& data) {
    std::string::size_type sent = 0;
    while (sent < data.size()) {
#ifdef _WIN32
        const int result = send(socket, data.data() + sent,
                                static_cast<int>(data.size() - sent), 0);
#else
        const ssize_t result = send(socket, data.data() + sent,
                                    data.size() - sent, 0);
#endif
        if (result <= 0) {
            return false;
        }
        sent += static_cast<std::string::size_type>(result);
    }
    return true;
}

bool parse_request(socket_handle socket, HttpRequest& request) {
    const std::size_t max_request_size = 65536;
    std::string data;
    char buffer[4096];
    std::string::size_type header_end = std::string::npos;
    std::size_t content_length = 0;

    while (header_end == std::string::npos) {
#ifdef _WIN32
        const int received = recv(socket, buffer, sizeof(buffer), 0);
#else
        const ssize_t received = recv(socket, buffer, sizeof(buffer), 0);
#endif
        if (received <= 0) {
            return false;
        }
        data.append(buffer, static_cast<std::string::size_type>(received));
        if (data.size() > max_request_size) {
            return false;
        }
        header_end = data.find("\r\n\r\n");
    }

    const std::string headers = data.substr(0, header_end);
    std::istringstream lines(headers);
    std::string request_line;
    if (!std::getline(lines, request_line)) {
        return false;
    }
    if (!request_line.empty() && request_line[request_line.size() - 1] == '\r') {
        request_line.erase(request_line.size() - 1);
    }
    std::istringstream request_parts(request_line);
    if (!(request_parts >> request.method >> request.path)) {
        return false;
    }

    std::string header;
    while (std::getline(lines, header)) {
        if (!header.empty() && header[header.size() - 1] == '\r') {
            header.erase(header.size() - 1);
        }
        const std::string prefix = "Content-Length:";
        if (header.compare(0, prefix.size(), prefix) == 0) {
            std::istringstream length(header.substr(prefix.size()));
            if (!(length >> content_length) || content_length > max_request_size) {
                return false;
            }
        }
    }

    const std::string::size_type body_start = header_end + 4;
    while (data.size() - body_start < content_length) {
#ifdef _WIN32
        const int received = recv(socket, buffer, sizeof(buffer), 0);
#else
        const ssize_t received = recv(socket, buffer, sizeof(buffer), 0);
#endif
        if (received <= 0) {
            return false;
        }
        data.append(buffer, static_cast<std::string::size_type>(received));
        if (data.size() > max_request_size) {
            return false;
        }
    }
    request.body = data.substr(body_start, content_length);
    return true;
}

std::string json_string(const std::string& value) {
    std::ostringstream result;
    result << '"';
    for (std::string::const_iterator it = value.begin(); it != value.end(); ++it) {
        const unsigned char character = static_cast<unsigned char>(*it);
        switch (character) {
        case '"':
            result << "\\\"";
            break;
        case '\\':
            result << "\\\\";
            break;
        case '\b':
            result << "\\b";
            break;
        case '\f':
            result << "\\f";
            break;
        case '\n':
            result << "\\n";
            break;
        case '\r':
            result << "\\r";
            break;
        case '\t':
            result << "\\t";
            break;
        default:
            if (character < 0x20) {
                const char hex[] = "0123456789abcdef";
                result << "\\u00" << hex[character >> 4] << hex[character & 0x0f];
            } else {
                result << *it;
            }
        }
    }
    result << '"';
    return result.str();
}

std::string file_json(const FileRecord& file) {
    std::ostringstream json;
    json << "{\"id\":" << json_string(file.id)
         << ",\"name\":" << json_string(file.name)
         << ",\"type\":" << json_string(file.type)
         << ",\"sizeBytes\":" << file.size_bytes
         << ",\"createdAt\":" << json_string(file.created_at)
         << ",\"modifiedAt\":" << json_string(file.modified_at)
         << ",\"deletedAt\":" << json_string(file.deleted_at)
         << ",\"simulatedOffset\":" << file.simulated_offset
         << ",\"status\":" << json_string(Simulator::status_name(file.status))
         << '}';
    return json.str();
}

std::string files_json(const std::vector<FileRecord>& files) {
    std::ostringstream json;
    json << '[';
    for (std::vector<FileRecord>::const_iterator it = files.begin();
         it != files.end(); ++it) {
        if (it != files.begin()) {
            json << ',';
        }
        json << file_json(*it);
    }
    json << ']';
    return json.str();
}

HttpResponse json_response(int status, const std::string& body) {
    HttpResponse response = { status, "application/json; charset=utf-8", body };
    return response;
}

HttpResponse error_response(int status, const std::string& message) {
    return json_response(status, "{\"success\":false,\"message\":" +
                         json_string(message) + "}");
}

bool read_web_file(const std::string& path, std::string& content) {
    std::ifstream input(path.c_str(), std::ios::in | std::ios::binary);
    if (!input.is_open()) {
        return false;
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    if (!input.good() && !input.eof()) {
        return false;
    }
    content = buffer.str();
    return true;
}

HttpResponse handle_request(const HttpRequest& request, Simulator& simulator,
                            const std::string& web_root) {
    if (request.method == "GET" && request.path == "/api/files") {
        return json_response(200, files_json(simulator.list_files()));
    }
    if (request.method == "GET" && request.path == "/api/scan") {
        return json_response(200, files_json(simulator.scan_deleted_files()));
    }
    if (request.method == "GET" && request.path == "/api/report") {
        return json_response(200, "{\"report\":" +
                             json_string(simulator.generate_report()) + "}");
    }
    if (request.path.compare(0, 11, "/api/files/") == 0) {
        const std::string file_path = request.path.substr(11);
        const std::string::size_type action_separator = file_path.find('/');
        const std::string id = file_path.substr(0, action_separator);
        if (id.empty()) {
            return error_response(400, "A file ID is required.");
        }
        if (request.method == "GET" && action_separator == std::string::npos) {
            FileRecord file;
            const OperationResult result = simulator.inspect_file(id, file);
            if (!result.success) {
                return error_response(404, result.message);
            }
            return json_response(200, file_json(file));
        }
        if (request.method == "POST" && action_separator != std::string::npos) {
            const std::string action = file_path.substr(action_separator + 1);
            OperationResult result;
            if (action == "delete") {
                result = simulator.mark_deleted(id);
            } else if (action == "recover") {
                result = simulator.recover_file(id);
            } else {
                return error_response(404, "Unknown file action.");
            }
            if (!result.success) {
                return error_response(400, result.message);
            }
            FileRecord file;
            const OperationResult inspection = simulator.inspect_file(id, file);
            if (!inspection.success) {
                return error_response(500, inspection.message);
            }
            return json_response(200, "{\"success\":true,\"message\":" +
                                 json_string(result.message) + ",\"file\":" +
                                 file_json(file) + "}");
        }
    }
    if (request.method != "GET") {
        return error_response(405, "Only GET and supported POST requests are allowed.");
    }

    std::string relative_path;
    std::string content_type;
    if (request.path == "/" || request.path == "/index.html") {
        relative_path = "index.html";
        content_type = "text/html; charset=utf-8";
    } else if (request.path == "/app.css") {
        relative_path = "app.css";
        content_type = "text/css; charset=utf-8";
    } else if (request.path == "/app.js") {
        relative_path = "app.js";
        content_type = "application/javascript; charset=utf-8";
    } else {
        return error_response(404, "Page or API route not found.");
    }

    std::string content;
    if (!read_web_file(web_root + "/" + relative_path, content)) {
        return error_response(500, "Could not read web asset '" + relative_path + "'.");
    }
    HttpResponse response = { 200, content_type, content };
    return response;
}

bool send_response(socket_handle socket, const HttpResponse& response) {
    const char* reason = "OK";
    switch (response.status) {
    case 200:
        reason = "OK";
        break;
    case 400:
        reason = "Bad Request";
        break;
    case 404:
        reason = "Not Found";
        break;
    case 405:
        reason = "Method Not Allowed";
        break;
    default:
        reason = "Internal Server Error";
        break;
    }

    std::ostringstream headers;
    headers << "HTTP/1.1 " << response.status << ' ' << reason << "\r\n"
            << "Content-Type: " << response.content_type << "\r\n"
            << "Content-Length: " << response.body.size() << "\r\n"
            << "Cache-Control: no-store\r\n"
            << "X-Content-Type-Options: nosniff\r\n"
            << "Content-Security-Policy: default-src 'self'; style-src 'self'; "
               "script-src 'self'; connect-src 'self'; img-src 'self' data:\r\n"
            << "Connection: close\r\n\r\n";
    return send_all(socket, headers.str() + response.body);
}

} // namespace

HttpServer::HttpServer(Simulator& simulator, const std::string& web_root, int port)
    : simulator_(simulator), web_root_(web_root), port_(port) {
}

int HttpServer::run() {
#ifdef _WIN32
    WSADATA winsock_data;
    if (WSAStartup(MAKEWORD(2, 2), &winsock_data) != 0) {
        std::cerr << "Could not initialize Windows sockets.\n";
        return 1;
    }
#endif

    socket_handle server = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server == invalid_socket_handle) {
        std::cerr << "Could not create the local HTTP server socket.\n";
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }

    int reuse_address = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR,
#ifdef _WIN32
               reinterpret_cast<const char*>(&reuse_address),
#else
               &reuse_address,
#endif
               sizeof(reuse_address));

    sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(static_cast<unsigned short>(port_));
    if (bind(server, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
        listen(server, 8) != 0) {
        std::cerr << "Could not start the local HTTP server on port "
                  << port_ << ". The port may already be in use.\n";
        close_socket(server);
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }

    std::cout << "Forensics Simulator is available at http://127.0.0.1:"
              << port_ << "/\n"
              << "Serving UI from '" << web_root_ << "'. Press Ctrl+C to stop.\n";
    std::cout.flush();

    while (true) {
        sockaddr_in client_address;
#ifdef _WIN32
        int address_length = sizeof(client_address);
#else
        socklen_t address_length = sizeof(client_address);
#endif
        socket_handle client = accept(
            server, reinterpret_cast<sockaddr*>(&client_address), &address_length);
        if (client == invalid_socket_handle) {
            std::cerr << "An incoming local connection could not be accepted.\n";
            continue;
        }

        HttpRequest request;
        if (parse_request(client, request)) {
            const HttpResponse response =
                handle_request(request, simulator_, web_root_);
            send_response(client, response);
        } else {
            send_response(client, error_response(400, "Malformed or oversized HTTP request."));
        }
        close_socket(client);
    }
}

} // namespace forensics
