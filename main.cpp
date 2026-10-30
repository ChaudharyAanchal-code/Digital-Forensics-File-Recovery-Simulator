#include "forensics/http_server.hpp"

#include <iostream>

int main() {
    forensics::Simulator simulator;
    const forensics::OperationResult database_result =
        simulator.open_database("simulator.db");
    if (!database_result.success) {
        std::cerr << "Database error: " << database_result.message << "\n";
        return 1;
    }
    std::cout << "This educational application operates on simulated data only.\n"
              << database_result.message << "\n";
    forensics::HttpServer server(simulator, "web", 8080);
    return server.run();
}
