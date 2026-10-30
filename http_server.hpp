#ifndef FORENSICS_HTTP_SERVER_HPP
#define FORENSICS_HTTP_SERVER_HPP

#include "forensics/simulator.hpp"

#include <string>

namespace forensics {

class HttpServer {
public:
    HttpServer(Simulator& simulator, const std::string& web_root, int port);

    int run();

private:
    Simulator& simulator_;
    std::string web_root_;
    int port_;
};

} // namespace forensics

#endif
