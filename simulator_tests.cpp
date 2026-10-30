#include "forensics/simulator.hpp"

#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "Test failed: " << message << "\n";
        std::exit(1);
    }
}

} // namespace

int main() {
    forensics::Simulator simulator;
    require(simulator.list_files().size() == 4, "sample files are initialized");
    require(simulator.scan_deleted_files().size() == 2,
            "scan returns only deleted files");

    forensics::FileRecord file;
    forensics::OperationResult result = simulator.inspect_file("F002", file);
    require(result.success && file.name == "field-photo.jpg",
            "inspection returns file metadata");

    result = simulator.mark_deleted("F001");
    require(result.success, "active file can be marked deleted");
    require(simulator.scan_deleted_files().size() == 3,
            "scan includes newly deleted file");

    result = simulator.recover_file("F002");
    require(result.success, "deleted file can be marked recovered");
    require(simulator.scan_deleted_files().size() == 2,
            "recovered file is no longer listed as deleted");

    result = simulator.recover_file("F002");
    require(!result.success, "already recovered file cannot be recovered twice");
    result = simulator.inspect_file("missing", file);
    require(!result.success, "unknown file ID returns an explicit error");

    const std::string report = simulator.generate_report();
    require(report.find("virtual storage only") != std::string::npos,
            "report identifies the simulated environment");
    require(report.find("No physical storage was accessed") != std::string::npos,
            "report includes the safety disclaimer");

    const std::string database_path = "forensics_simulator_test.db";
    const std::string temporary_path = database_path + ".tmp";
    const std::string invalid_database_path = "forensics_simulator_invalid_test.db";
    std::remove(database_path.c_str());
    std::remove(temporary_path.c_str());
    std::remove(invalid_database_path.c_str());

    forensics::Simulator persistent_simulator;
    result = persistent_simulator.open_database(database_path);
    require(result.success, "opening a missing database creates sample data");
    result = persistent_simulator.recover_file("F002");
    require(result.success, "database persists a recovery operation: " + result.message);

    forensics::Simulator reloaded_simulator;
    result = reloaded_simulator.open_database(database_path);
    require(result.success, "existing database can be loaded");
    require(reloaded_simulator.scan_deleted_files().size() == 1,
            "deleted scan reflects persisted recovery state");
    result = reloaded_simulator.inspect_file("F002", file);
    require(result.success && file.status == forensics::FileStatus::Recovered,
            "reloaded database preserves file status");

    {
        std::ofstream invalid_database(invalid_database_path.c_str());
        invalid_database << "not a simulator database\n";
    }
    forensics::Simulator invalid_simulator;
    result = invalid_simulator.open_database(invalid_database_path);
    require(!result.success, "invalid database format returns an explicit error");
    std::remove(invalid_database_path.c_str());
    std::remove(database_path.c_str());
    std::remove(temporary_path.c_str());

    std::cout << "All simulator tests passed.\n";
    return 0;
}
