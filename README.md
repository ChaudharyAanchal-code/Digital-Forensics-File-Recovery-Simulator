# Digital Forensics File Recovery Simulator

A small educational project that demonstrates a simulated file-recovery
workflow through a browser-based interface. A C++11 backend manages virtual
file records and serves a user interface built with plain HTML, CSS, and
JavaScript.

> **Important:** This is a simulation, not forensic recovery software. It does
> not inspect disks, access physical storage, or recover actual file contents.
> All file records and recovery operations are virtual.

## Features

- View and search a persistent virtual file inventory.
- Scan the simulated inventory for records marked as deleted.
- Inspect file metadata, including type, size, timestamps, status, and a
  simulated storage offset.
- Simulate deleting an active record or recovering a deleted record.
- Save simulated status changes to a local database file between runs.
- View and download a text investigation report.
- Run the web interface and API together from a local C++ server.

## Technology

- **Backend:** C++11 and the standard library, with platform sockets for HTTP.
- **Frontend:** HTML, CSS, and vanilla JavaScript; no frontend package manager
  or framework is required.
- **Persistence:** A versioned, tab-separated text file (`simulator.db`).
- **Build and tests:** CMake and CTest.

## Project structure

```text
app/
  main.cpp                    Application entry point
include/
  forensics/
    http_server.hpp           Local HTTP server interface
    simulator.hpp             Simulation data types and core API
src/
  http_server.cpp             HTTP server, static assets, and JSON API
  simulator.cpp               Virtual file operations and persistence
web/
  index.html                  Browser interface
  app.css                     Interface styles
  app.js                      UI behavior and API requests
tests/
  simulator_tests.cpp         Core and database behavior tests
CMakeLists.txt                Build configuration
README.md                     Project documentation
```

The simulation core does not depend on the web UI. The HTTP server calls the
core, serves the static files from `web/`, and exposes the same-origin JSON API
used by the browser.

## Requirements

- A C++11-compatible compiler.
- CMake 3.5 or later.
- Windows, Linux, or macOS with the corresponding compiler and socket support.

For Windows, MinGW-w64/GCC or Visual Studio can be used with CMake. The
application uses Winsock on Windows and links against `ws2_32` through the
CMake configuration.

## Build and run

Run the following commands from the **project root** (the directory containing
`CMakeLists.txt` and `web/`).

### Windows PowerShell

```powershell
cmake -S . -B build
cmake --build build
.\build\forensics_simulator.exe
```

### Linux or macOS

```sh
cmake -S . -B build
cmake --build build
./build/forensics_simulator
```

When the server starts, open <http://127.0.0.1:8080/> in a browser. Keep the
terminal running while using the app. Press **Ctrl+C** in that terminal to
stop it.

The server binds to the loopback address (`127.0.0.1`), so it is intended for
local use on the same computer. It serves the UI from the relative `web/`
folder; run the executable from the project root so those assets can be found.

### Build without CMake on Windows (MinGW GCC)

If `g++` is installed, the application can also be compiled directly from the
project root:

```powershell
g++ -std=c++11 -Wall -Wextra -pedantic -Iinclude `
  src\simulator.cpp src\http_server.cpp app\main.cpp `
  -lws2_32 -o simulator.exe
.\simulator.exe
```

## Tests

Build the project, then run the test suite from the project root:

```sh
ctest --test-dir build --output-on-failure
```

The tests cover sample inventory setup, scanning, metadata inspection, valid
and invalid state transitions, report disclaimers, database persistence, and
invalid database formats.

## Using the application

The dashboard displays summary counts and the virtual file inventory.

1. Select **Scan for deleted files** to list records with `Deleted` status.
2. Use search to filter records by name, ID, type, or status.
3. Select **Details** to view the record's simulated metadata.
4. Select **Delete** or **Recover** to change a record's simulated status.
   The interface asks for confirmation before changing it.
5. Select **View report** to view the investigation summary, or download it as
   a text file.

Simulated deletion and recovery update metadata only. They do not create,
remove, restore, or modify any actual files.

## Local API

The browser and backend communicate through these JSON endpoints:

| Method | Endpoint | Purpose |
|---|---|---|
| `GET` | `/api/files` | List all virtual file records |
| `GET` | `/api/scan` | List records currently marked deleted |
| `GET` | `/api/files/{id}` | Return one record's metadata |
| `POST` | `/api/files/{id}/delete` | Mark an active record as deleted |
| `POST` | `/api/files/{id}/recover` | Mark a deleted record as recovered |
| `GET` | `/api/report` | Return the current report as JSON |

Example:

```sh
curl http://127.0.0.1:8080/api/files
curl http://127.0.0.1:8080/api/files/F002
curl -X POST http://127.0.0.1:8080/api/files/F002/recover
curl http://127.0.0.1:8080/api/report
```

File records contain fields such as `id`, `name`, `type`, `sizeBytes`,
`createdAt`, `modifiedAt`, `deletedAt`, `simulatedOffset`, and `status`.
Unsupported transitions and unknown IDs return a JSON error response with a
non-success HTTP status.

## Database and saved data

At startup, the application opens `simulator.db` in its **current working
directory**:

- If the database does not exist, the application creates it with the sample
  virtual inventory.
- If it exists and has a supported format, the application loads its records.
- Simulated deletion and recovery changes are saved automatically.
- If the file is invalid or cannot be accessed, startup reports an error
  instead of silently replacing the database.

The database is a versioned, tab-separated text file that stores simulated
metadata only. It does not contain the contents of the example files or any
recovered data. Keep `simulator.db` to continue a case. Back it up before
manual editing; deleting it starts a new sample case on the next launch.

## Troubleshooting

### The browser cannot connect

Make sure the C++ application is running in a terminal and open
<http://127.0.0.1:8080/>. Check that the server did not report that port 8080
is already in use.

### The page or its assets cannot be found

Start the application from the project root, and make sure the `web/` folder
contains `index.html`, `app.css`, and `app.js`.

### `forensics/simulator.hpp` cannot be found during compilation

Compile from the project root and include the `include` directory. For example:

```powershell
g++ -std=c++11 -Iinclude src\simulator.cpp src\http_server.cpp app\main.cpp `
  -lws2_32 -o simulator.exe
```

### The database cannot be opened

Check that the application can read and write in its current directory and
that `simulator.db` is a valid simulator database. Keep a backup before
changing or removing saved case data.

## Contributing

Contributions and suggestions are welcome. For a change:

1. Keep the simulator educational and restricted to virtual data.
2. Keep the core simulation independent from the browser and HTTP layers.
3. Build the project and run the CTest suite.
4. Describe the behavior changed and how it was tested.

## License

No license has been specified for this project yet. Add a `LICENSE` file before
redistributing or reusing the code so the project's terms are clear.
