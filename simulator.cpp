#include "forensics/simulator.hpp"

#include <cerrno>
#include <cstdio>
#include <fstream>
#include <set>
#include <sstream>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace forensics {
namespace {

const char* const database_header = "FORENSICS_SIMULATOR_DB_V1";

OperationResult failure(const std::string& message) {
    OperationResult result;
    result.success = false;
    result.message = message;
    return result;
}

OperationResult success(const std::string& message) {
    OperationResult result;
    result.success = true;
    result.message = message;
    return result;
}

std::string escape_field(const std::string& value) {
    std::string escaped;
    for (std::string::const_iterator it = value.begin(); it != value.end(); ++it) {
        switch (*it) {
        case '\\':
            escaped += "\\\\";
            break;
        case '\t':
            escaped += "\\t";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        default:
            escaped += *it;
            break;
        }
    }
    return escaped;
}

bool unescape_field(const std::string& value, std::string& result) {
    result.clear();
    for (std::string::size_type i = 0; i < value.size(); ++i) {
        if (value[i] != '\\') {
            result += value[i];
            continue;
        }
        if (++i == value.size()) {
            return false;
        }
        switch (value[i]) {
        case '\\':
            result += '\\';
            break;
        case 't':
            result += '\t';
            break;
        case 'n':
            result += '\n';
            break;
        case 'r':
            result += '\r';
            break;
        default:
            return false;
        }
    }
    return true;
}

std::vector<std::string> split_fields(const std::string& line) {
    std::vector<std::string> fields;
    std::string::size_type start = 0;
    while (true) {
        const std::string::size_type separator = line.find('\t', start);
        fields.push_back(line.substr(start, separator - start));
        if (separator == std::string::npos) {
            break;
        }
        start = separator + 1;
    }
    return fields;
}

bool parse_number(const std::string& text, std::uint64_t& value) {
    std::istringstream input(text);
    if (!(input >> value)) {
        return false;
    }
    return input.peek() == std::char_traits<char>::eof();
}

bool parse_file_record(const std::string& line, FileRecord& file) {
    const std::vector<std::string> fields = split_fields(line);
    if (fields.size() != 9) {
        return false;
    }

    std::string id;
    std::string name;
    std::string type;
    std::string created_at;
    std::string modified_at;
    std::string deleted_at;
    if (!unescape_field(fields[0], id) ||
        !unescape_field(fields[1], name) ||
        !unescape_field(fields[2], type) ||
        !unescape_field(fields[4], created_at) ||
        !unescape_field(fields[5], modified_at) ||
        !unescape_field(fields[6], deleted_at) ||
        !parse_number(fields[3], file.size_bytes) ||
        !parse_number(fields[7], file.simulated_offset) ||
        id.empty() || name.empty()) {
        return false;
    }

    if (fields[8] == "Active") {
        file.status = FileStatus::Active;
    } else if (fields[8] == "Deleted") {
        file.status = FileStatus::Deleted;
    } else if (fields[8] == "Recovered") {
        file.status = FileStatus::Recovered;
    } else {
        return false;
    }

    file.id = id;
    file.name = name;
    file.type = type;
    file.created_at = created_at;
    file.modified_at = modified_at;
    file.deleted_at = deleted_at;
    return true;
}

std::string serialize_file_record(const FileRecord& file) {
    std::ostringstream line;
    line << escape_field(file.id) << '\t'
         << escape_field(file.name) << '\t'
         << escape_field(file.type) << '\t'
         << file.size_bytes << '\t'
         << escape_field(file.created_at) << '\t'
         << escape_field(file.modified_at) << '\t'
         << escape_field(file.deleted_at) << '\t'
         << file.simulated_offset << '\t'
         << Simulator::status_name(file.status);
    return line.str();
}

} // namespace

Simulator::Simulator() {
    FileRecord report = {
        "F001", "case-notes.txt", "text/plain", 2048,
        "2026-01-12 09:30", "2026-01-14 16:05", "",
        0, FileStatus::Active
    };
    FileRecord image = {
        "F002", "field-photo.jpg", "image/jpeg", 524288,
        "2026-01-13 11:10", "2026-01-13 11:10", "2026-01-20 08:15",
        4096, FileStatus::Deleted
    };
    FileRecord archive = {
        "F003", "evidence-archive.zip", "application/zip", 1048576,
        "2026-01-15 14:25", "2026-01-16 10:40", "2026-01-21 12:00",
        532480, FileStatus::Deleted
    };
    FileRecord log = {
        "F004", "system-log.csv", "text/csv", 8192,
        "2026-01-18 07:00", "2026-01-18 07:30", "",
        1581056, FileStatus::Active
    };

    files_.push_back(report);
    files_.push_back(image);
    files_.push_back(archive);
    files_.push_back(log);
}

OperationResult Simulator::open_database(const std::string& path) {
    if (path.empty()) {
        return failure("Database path cannot be empty.");
    }

    errno = 0;
    std::FILE* existing_file = std::fopen(path.c_str(), "rb");
    if (existing_file == NULL && errno != ENOENT) {
        return failure("Could not access database file '" + path + "'.");
    }
    if (existing_file != NULL) {
        std::fclose(existing_file);
    }

    std::ifstream existing(path.c_str(), std::ios::in | std::ios::binary);
    if (!existing.is_open()) {
        if (errno != ENOENT) {
            return failure("Could not open database file '" + path + "'.");
        }

        database_path_ = path;
        const OperationResult result = save_files(files_);
        if (!result.success) {
            database_path_.clear();
            return result;
        }
        return success("Created database with sample virtual files at '" + path + "'.");
    }

    std::string header;
    if (!std::getline(existing, header) || header != database_header) {
        return failure("Database file has an invalid or unsupported format.");
    }

    std::vector<FileRecord> loaded_files;
    std::set<std::string> file_ids;
    std::string line;
    std::size_t line_number = 1;
    while (std::getline(existing, line)) {
        ++line_number;
        FileRecord file;
        if (!parse_file_record(line, file)) {
            std::ostringstream message;
            message << "Invalid database record on line " << line_number << ".";
            return failure(message.str());
        }
        if (!file_ids.insert(file.id).second) {
            std::ostringstream message;
            message << "Duplicate file ID on database line " << line_number << ".";
            return failure(message.str());
        }
        loaded_files.push_back(file);
    }
    if (existing.bad()) {
        return failure("An error occurred while reading database file '" + path + "'.");
    }

    files_.swap(loaded_files);
    database_path_ = path;
    return success("Loaded " + std::to_string(files_.size()) +
                   " simulated files from '" + path + "'.");
}

std::vector<FileRecord> Simulator::list_files() const {
    return files_;
}

std::vector<FileRecord> Simulator::scan_deleted_files() const {
    std::vector<FileRecord> deleted_files;
    for (std::vector<FileRecord>::const_iterator it = files_.begin();
         it != files_.end(); ++it) {
        if (it->status == FileStatus::Deleted) {
            deleted_files.push_back(*it);
        }
    }
    return deleted_files;
}

OperationResult Simulator::inspect_file(const std::string& id,
                                        FileRecord& file) const {
    const FileRecord* found = find_file(id);
    if (found == NULL) {
        return failure("No simulated file exists with ID '" + id + "'.");
    }

    file = *found;
    return success("File metadata loaded.");
}

OperationResult Simulator::mark_deleted(const std::string& id) {
    FileRecord* file = find_file(id);
    if (file == NULL) {
        return failure("No simulated file exists with ID '" + id + "'.");
    }
    if (file->status != FileStatus::Active) {
        return failure("Only active files can be marked as deleted.");
    }

    const FileRecord previous = *file;
    file->status = FileStatus::Deleted;
    file->deleted_at = "simulated";
    const OperationResult saved = save_files(files_);
    if (!saved.success) {
        *file = previous;
        return saved;
    }
    return success("File marked as deleted in virtual storage.");
}

OperationResult Simulator::recover_file(const std::string& id) {
    FileRecord* file = find_file(id);
    if (file == NULL) {
        return failure("No simulated file exists with ID '" + id + "'.");
    }
    if (file->status != FileStatus::Deleted) {
        return failure("Only deleted files can be recovered.");
    }

    const FileRecord previous = *file;
    file->status = FileStatus::Recovered;
    const OperationResult saved = save_files(files_);
    if (!saved.success) {
        *file = previous;
        return saved;
    }
    return success("File marked as recovered in the simulation.");
}

OperationResult Simulator::save_files(const std::vector<FileRecord>& files) const {
    if (database_path_.empty()) {
        return success("Database is not open; changes are in memory only.");
    }

    const std::string temporary_path = database_path_ + ".tmp";
    bool write_failed = false;
    {
        std::ofstream output(temporary_path.c_str(),
                             std::ios::out | std::ios::binary | std::ios::trunc);
        if (!output.is_open()) {
            return failure("Could not write temporary database file '" +
                           temporary_path + "'.");
        }

        output << database_header << '\n';
        for (std::vector<FileRecord>::const_iterator it = files.begin();
             it != files.end(); ++it) {
            output << serialize_file_record(*it) << '\n';
        }
        output.flush();
        write_failed = !output;
    }
    if (write_failed) {
        std::remove(temporary_path.c_str());
        return failure("Failed while writing database file '" +
                       temporary_path + "'.");
    }

#ifdef _WIN32
    if (!MoveFileExA(temporary_path.c_str(), database_path_.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        std::remove(temporary_path.c_str());
        return failure("Could not replace database file '" + database_path_ + "'.");
    }
#else
    if (std::rename(temporary_path.c_str(), database_path_.c_str()) != 0) {
        std::remove(temporary_path.c_str());
        return failure("Could not save database file '" + database_path_ + "'.");
    }
#endif
    return success("Database saved.");
}

std::string Simulator::generate_report() const {
    std::size_t active_count = 0;
    std::size_t deleted_count = 0;
    std::size_t recovered_count = 0;
    std::uint64_t total_bytes = 0;
    std::ostringstream report;

    for (std::vector<FileRecord>::const_iterator it = files_.begin();
         it != files_.end(); ++it) {
        total_bytes += it->size_bytes;
        switch (it->status) {
        case FileStatus::Active:
            ++active_count;
            break;
        case FileStatus::Deleted:
            ++deleted_count;
            break;
        case FileStatus::Recovered:
            ++recovered_count;
            break;
        }
    }

    report << "DIGITAL FORENSICS SIMULATION REPORT\n"
           << "===================================\n"
           << "Environment: virtual storage only\n"
           << "Files in simulation: " << files_.size() << "\n"
           << "Active: " << active_count << "\n"
           << "Deleted: " << deleted_count << "\n"
           << "Recovered (simulated): " << recovered_count << "\n"
           << "Total simulated size: " << total_bytes << " bytes\n\n"
           << "FILE INVENTORY\n"
           << "--------------\n";

    for (std::vector<FileRecord>::const_iterator it = files_.begin();
         it != files_.end(); ++it) {
        report << it->id << " | " << it->name << " | "
               << status_name(it->status) << " | "
               << it->size_bytes << " bytes\n";
    }

    report << "\nThis report describes simulated data only. "
           << "No physical storage was accessed or modified.\n";
    return report.str();
}

std::string Simulator::status_name(FileStatus status) {
    switch (status) {
    case FileStatus::Active:
        return "Active";
    case FileStatus::Deleted:
        return "Deleted";
    case FileStatus::Recovered:
        return "Recovered";
    }
    return "Unknown";
}

FileRecord* Simulator::find_file(const std::string& id) {
    for (std::vector<FileRecord>::iterator it = files_.begin();
         it != files_.end(); ++it) {
        if (it->id == id) {
            return &(*it);
        }
    }
    return NULL;
}

const FileRecord* Simulator::find_file(const std::string& id) const {
    for (std::vector<FileRecord>::const_iterator it = files_.begin();
         it != files_.end(); ++it) {
        if (it->id == id) {
            return &(*it);
        }
    }
    return NULL;
}

} // namespace forensics
