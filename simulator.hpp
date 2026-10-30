#ifndef FORENSICS_SIMULATOR_HPP
#define FORENSICS_SIMULATOR_HPP

#include <cstdint>
#include <string>
#include <vector>

namespace forensics {

enum class FileStatus {
    Active,
    Deleted,
    Recovered
};

struct FileRecord {
    std::string id;
    std::string name;
    std::string type;
    std::uint64_t size_bytes;
    std::string created_at;
    std::string modified_at;
    std::string deleted_at;
    std::uint64_t simulated_offset;
    FileStatus status;
};

struct OperationResult {
    bool success;
    std::string message;
};

class Simulator {
public:
    Simulator();

    OperationResult open_database(const std::string& path);
    std::vector<FileRecord> list_files() const;
    std::vector<FileRecord> scan_deleted_files() const;
    OperationResult inspect_file(const std::string& id, FileRecord& file) const;
    OperationResult mark_deleted(const std::string& id);
    OperationResult recover_file(const std::string& id);
    std::string generate_report() const;

    static std::string status_name(FileStatus status);

private:
    std::vector<FileRecord> files_;
    std::string database_path_;

    OperationResult save_files(const std::vector<FileRecord>& files) const;
    FileRecord* find_file(const std::string& id);
    const FileRecord* find_file(const std::string& id) const;
};

} // namespace forensics

#endif
