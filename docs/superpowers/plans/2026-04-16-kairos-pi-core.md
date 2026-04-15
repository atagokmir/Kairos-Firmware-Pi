# Kairos Pi 5 Core Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Raspberry Pi 5 üzerinde çalışan, Pico 2W'den serial CYCLE verileri okuyup UCL/LCL 3-sigma anomaly detection yapan, LVGL ve MQTT için genişletilebilir çok-thread'li C++ uygulaması.

**Architecture:** SerialThread → CycleQueue (mutex+condvar) → StatsThread → SharedState (shared_mutex). Her thread tek sorumluluk; LVGL/MQTT gelince sadece yeni .cpp eklenir.

**Tech Stack:** C++20, pthreads, POSIX termios, Google Test (FetchContent), CMake 3.20+

---

## Task 1: Project Scaffold

**Files:**
- Create: `CMakeLists.txt`
- Create: `tests/CMakeLists.txt`
- Create: `kairos.conf`

- [ ] **Step 1: Root CMakeLists.txt oluştur**

```cmake
# CMakeLists.txt
cmake_minimum_required(VERSION 3.20)
project(kairos VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Shared library (main + tests tarafından kullanılır)
add_library(kairos_lib STATIC
    src/config.cpp
    src/rolling_window.cpp
    src/logger.cpp
    src/serial_thread.cpp
    src/stats_thread.cpp
)
target_include_directories(kairos_lib PUBLIC src)
target_link_libraries(kairos_lib PUBLIC pthread)

# Main executable
add_executable(kairos src/main.cpp)
target_link_libraries(kairos PRIVATE kairos_lib)

# Google Test via FetchContent
include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()
add_subdirectory(tests)
```

- [ ] **Step 2: tests/CMakeLists.txt oluştur**

```cmake
# tests/CMakeLists.txt
add_executable(kairos_tests
    test_config.cpp
    test_rolling_window.cpp
    test_logger.cpp
    test_cycle_queue.cpp
)
target_link_libraries(kairos_tests
    PRIVATE kairos_lib GTest::gtest_main
)
include(GoogleTest)
gtest_discover_tests(kairos_tests)
```

- [ ] **Step 3: kairos.conf oluştur**

```ini
# kairos.conf — Kairos default configuration
port=/dev/ttyACM0
window_size=100
min_samples=50
summary_interval=50
log_path=/var/log/kairos/kairos.log
anomaly_threshold=3.0
```

- [ ] **Step 4: Placeholder kaynak dosyalarını oluştur (derleme için)**

`src/config.cpp` içeriği:
```cpp
#include "config.hpp"
```

`src/rolling_window.cpp` içeriği:
```cpp
#include "rolling_window.hpp"
```

`src/logger.cpp` içeriği:
```cpp
#include "logger.hpp"
```

`src/serial_thread.cpp` içeriği:
```cpp
#include "serial_thread.hpp"
```

`src/stats_thread.cpp` içeriği:
```cpp
#include "stats_thread.hpp"
```

`src/main.cpp` içeriği:
```cpp
int main() { return 0; }
```

Ve tüm header'lar için boş `#pragma once` dosyaları oluştur:
- `src/config.hpp`
- `src/rolling_window.hpp`
- `src/logger.hpp`
- `src/cycle_queue.hpp`
- `src/shared_state.hpp`
- `src/serial_thread.hpp`
- `src/stats_thread.hpp`

Her biri sadece:
```cpp
#pragma once
```

- [ ] **Step 5: cmake configure**

```bash
mkdir -p build && cd build && cmake .. -DCMAKE_BUILD_TYPE=Debug
```

Expected: `-- Configuring done` ve `-- Build files have been written to: .../build`

---

## Task 2: Config

**Files:**
- Modify: `src/config.hpp`
- Modify: `src/config.cpp`
- Create: `tests/test_config.cpp`

- [ ] **Step 1: Failing test yaz**

`tests/test_config.cpp`:
```cpp
#include "config.hpp"
#include <gtest/gtest.h>
#include <fstream>

TEST(ConfigTest, DefaultValues) {
    Config cfg;
    EXPECT_EQ(cfg.port, "/dev/ttyACM0");
    EXPECT_EQ(cfg.window_size, 100u);
    EXPECT_EQ(cfg.min_samples, 50u);
    EXPECT_EQ(cfg.summary_interval, 50u);
    EXPECT_EQ(cfg.log_path, "/var/log/kairos/kairos.log");
    EXPECT_DOUBLE_EQ(cfg.anomaly_threshold, 3.0);
}

TEST(ConfigTest, LoadFromFile) {
    const std::string path = "/tmp/kairos_test.conf";
    {
        std::ofstream f(path);
        f << "port=/dev/ttyUSB0\n"
          << "window_size=200\n"
          << "min_samples=75\n"
          << "summary_interval=25\n"
          << "log_path=/tmp/kairos_test.log\n"
          << "anomaly_threshold=2.5\n";
    }
    Config cfg = load_config_file(path);
    EXPECT_EQ(cfg.port, "/dev/ttyUSB0");
    EXPECT_EQ(cfg.window_size, 200u);
    EXPECT_EQ(cfg.min_samples, 75u);
    EXPECT_EQ(cfg.summary_interval, 25u);
    EXPECT_EQ(cfg.log_path, "/tmp/kairos_test.log");
    EXPECT_DOUBLE_EQ(cfg.anomaly_threshold, 2.5);
}

TEST(ConfigTest, MissingFileUsesDefaults) {
    Config cfg = load_config_file("/nonexistent/path/kairos.conf");
    EXPECT_EQ(cfg.port, "/dev/ttyACM0");
}

TEST(ConfigTest, CommentsAndEmptyLinesIgnored) {
    const std::string path = "/tmp/kairos_comments.conf";
    {
        std::ofstream f(path);
        f << "# this is a comment\n"
          << "\n"
          << "port=/dev/ttyUSB1\n"
          << "# another comment\n";
    }
    Config cfg = load_config_file(path);
    EXPECT_EQ(cfg.port, "/dev/ttyUSB1");
    EXPECT_EQ(cfg.window_size, 100u);  // default
}

TEST(ConfigTest, ApplyCLIArgs) {
    Config cfg;
    const char* args[] = {
        "kairos", "--port", "/dev/ttyUSB1",
        "--window", "50", "--threshold", "2.0"
    };
    apply_args(cfg, 7, const_cast<char**>(args));
    EXPECT_EQ(cfg.port, "/dev/ttyUSB1");
    EXPECT_EQ(cfg.window_size, 50u);
    EXPECT_DOUBLE_EQ(cfg.anomaly_threshold, 2.0);
}

TEST(ConfigTest, CLIDoesNotOverrideUnspecified) {
    Config cfg;
    const char* args[] = {"kairos", "--port", "/dev/ttyUSB2"};
    apply_args(cfg, 3, const_cast<char**>(args));
    EXPECT_EQ(cfg.port, "/dev/ttyUSB2");
    EXPECT_EQ(cfg.window_size, 100u);  // unchanged default
}
```

- [ ] **Step 2: Test'in derlenemediğini doğrula**

```bash
cd build && make kairos_tests 2>&1 | head -20
```

Expected: `config.hpp` içinde `Config` struct ve fonksiyon bulunamadı hatası.

- [ ] **Step 3: config.hpp yaz**

```cpp
// src/config.hpp
#pragma once
#include <string>
#include <cstddef>

struct Config {
    std::string port              = "/dev/ttyACM0";
    std::size_t window_size       = 100;
    std::size_t min_samples       = 50;
    std::size_t summary_interval  = 50;
    std::string log_path          = "/var/log/kairos/kairos.log";
    double      anomaly_threshold = 3.0;
};

// INI dosyasından yükle. Eksik key'ler default değer kullanır.
// Dosya bulunamazsa default Config döner.
Config load_config_file(const std::string& path);

// CLI argümanlarını mevcut cfg'ye uygula.
// --port, --window, --min-samples, --summary-interval, --log, --threshold desteklenir.
void apply_args(Config& cfg, int argc, char* argv[]);
```

- [ ] **Step 4: config.cpp yaz**

```cpp
// src/config.cpp
#include "config.hpp"
#include <fstream>
#include <sstream>
#include <string>
#include <cctype>

static std::string trim(const std::string& s) {
    std::size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start])))
        ++start;
    std::size_t end = s.size();
    while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1])))
        --end;
    return s.substr(start, end - start);
}

Config load_config_file(const std::string& path) {
    Config cfg;
    std::ifstream f(path);
    if (!f.is_open()) return cfg;

    std::string line;
    while (std::getline(f, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = trim(line.substr(0, eq));
        std::string val = trim(line.substr(eq + 1));

        if      (key == "port")             cfg.port              = val;
        else if (key == "window_size")      cfg.window_size       = std::stoul(val);
        else if (key == "min_samples")      cfg.min_samples       = std::stoul(val);
        else if (key == "summary_interval") cfg.summary_interval  = std::stoul(val);
        else if (key == "log_path")         cfg.log_path          = val;
        else if (key == "anomaly_threshold")cfg.anomaly_threshold = std::stod(val);
    }
    return cfg;
}

void apply_args(Config& cfg, int argc, char* argv[]) {
    for (int i = 1; i + 1 < argc; ++i) {
        std::string arg = argv[i];
        std::string val = argv[i + 1];
        if      (arg == "--port")             { cfg.port              = val;              ++i; }
        else if (arg == "--window")           { cfg.window_size       = std::stoul(val); ++i; }
        else if (arg == "--min-samples")      { cfg.min_samples       = std::stoul(val); ++i; }
        else if (arg == "--summary-interval") { cfg.summary_interval  = std::stoul(val); ++i; }
        else if (arg == "--log")              { cfg.log_path          = val;              ++i; }
        else if (arg == "--threshold")        { cfg.anomaly_threshold = std::stod(val);  ++i; }
    }
}
```

- [ ] **Step 5: Test'leri çalıştır**

```bash
cd build && make kairos_tests && ./tests/kairos_tests --gtest_filter="ConfigTest*"
```

Expected:
```
[==========] Running 6 tests from 1 test suite.
[  PASSED  ] 6 tests.
```

- [ ] **Step 6: Commit**

```bash
git add CMakeLists.txt tests/CMakeLists.txt kairos.conf src/config.hpp src/config.cpp tests/test_config.cpp
git commit -m "feat: add Config struct with INI parser and CLI arg override"
```

---

## Task 3: RollingWindow

**Files:**
- Modify: `src/rolling_window.hpp`
- Modify: `src/rolling_window.cpp`
- Create: `tests/test_rolling_window.cpp`

- [ ] **Step 1: Failing test yaz**

`tests/test_rolling_window.cpp`:
```cpp
#include "rolling_window.hpp"
#include <gtest/gtest.h>
#include <cmath>

TEST(RollingWindowTest, EmptyWindow) {
    RollingWindow w(5);
    EXPECT_EQ(w.size(), 0u);
    EXPECT_EQ(w.capacity(), 5u);
    EXPECT_FALSE(w.is_ready(1));
}

TEST(RollingWindowTest, PushAndSize) {
    RollingWindow w(5);
    w.push(1000);
    EXPECT_EQ(w.size(), 1u);
    w.push(2000);
    EXPECT_EQ(w.size(), 2u);
}

TEST(RollingWindowTest, DoesNotExceedCapacity) {
    RollingWindow w(3);
    w.push(100); w.push(200); w.push(300);
    EXPECT_EQ(w.size(), 3u);
    w.push(400);  // en eski (100) silinir
    EXPECT_EQ(w.size(), 3u);
}

TEST(RollingWindowTest, MeanOfIdenticalValues) {
    RollingWindow w(5);
    w.push(1000000); w.push(1000000); w.push(1000000);
    EXPECT_DOUBLE_EQ(w.mean(), 1000000.0);
}

TEST(RollingWindowTest, MeanOfMixedValues) {
    RollingWindow w(4);
    w.push(1000); w.push(2000); w.push(3000); w.push(4000);
    EXPECT_DOUBLE_EQ(w.mean(), 2500.0);
}

TEST(RollingWindowTest, SigmaZeroForIdentical) {
    RollingWindow w(5);
    w.push(1000); w.push(1000); w.push(1000);
    EXPECT_DOUBLE_EQ(w.sigma(), 0.0);
}

TEST(RollingWindowTest, SigmaKnownValues) {
    // Values: 2, 4, 4, 4, 5, 5, 7, 9 → mean=5, sigma=2
    RollingWindow w(8);
    for (uint32_t v : {2u, 4u, 4u, 4u, 5u, 5u, 7u, 9u}) w.push(v);
    EXPECT_NEAR(w.mean(), 5.0, 1e-9);
    EXPECT_NEAR(w.sigma(), 2.0, 1e-9);
}

TEST(RollingWindowTest, UCLandLCL) {
    RollingWindow w(8);
    for (uint32_t v : {2u, 4u, 4u, 4u, 5u, 5u, 7u, 9u}) w.push(v);
    // mean=5, sigma=2, threshold=3.0 → UCL=11, LCL=-1
    EXPECT_NEAR(w.ucl(3.0), 11.0, 1e-9);
    EXPECT_NEAR(w.lcl(3.0), -1.0, 1e-9);
}

TEST(RollingWindowTest, IsReadyThreshold) {
    RollingWindow w(10);
    EXPECT_FALSE(w.is_ready(5));
    for (int i = 0; i < 4; ++i) w.push(1000);
    EXPECT_FALSE(w.is_ready(5));
    w.push(1000);
    EXPECT_TRUE(w.is_ready(5));
}

TEST(RollingWindowTest, RollingMeanAfterWrap) {
    // Window size 3: push 100, 200, 300, 400 (400 replaces 100)
    // After wrap: values = [400, 200, 300] → mean = 300
    RollingWindow w(3);
    w.push(100); w.push(200); w.push(300); w.push(400);
    EXPECT_NEAR(w.mean(), 300.0, 1e-9);
}
```

- [ ] **Step 2: Test'in derlenemediğini doğrula**

```bash
cd build && make kairos_tests 2>&1 | head -10
```

Expected: `RollingWindow` undefined.

- [ ] **Step 3: rolling_window.hpp yaz**

```cpp
// src/rolling_window.hpp
#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>

class RollingWindow {
public:
    explicit RollingWindow(std::size_t capacity);

    void        push(uint32_t value);
    std::size_t size()     const;
    std::size_t capacity() const;
    bool        is_ready(std::size_t min_samples) const;
    double      mean()     const;
    double      sigma()    const;               // population std dev
    double      ucl(double threshold) const;   // mean + threshold*sigma
    double      lcl(double threshold) const;   // mean - threshold*sigma

private:
    std::vector<uint32_t> buffer_;
    std::size_t           head_     = 0;
    std::size_t           count_    = 0;
    std::size_t           capacity_;
};
```

- [ ] **Step 4: rolling_window.cpp yaz**

```cpp
// src/rolling_window.cpp
#include "rolling_window.hpp"
#include <cmath>
#include <cassert>

RollingWindow::RollingWindow(std::size_t capacity)
    : buffer_(capacity, 0u), capacity_(capacity) {
    assert(capacity > 0);
}

void RollingWindow::push(uint32_t value) {
    buffer_[head_] = value;
    head_ = (head_ + 1) % capacity_;
    if (count_ < capacity_) ++count_;
}

std::size_t RollingWindow::size()     const { return count_; }
std::size_t RollingWindow::capacity() const { return capacity_; }

bool RollingWindow::is_ready(std::size_t min_samples) const {
    return count_ >= min_samples;
}

double RollingWindow::mean() const {
    if (count_ == 0) return 0.0;
    double sum = 0.0;
    // count_ <= capacity_; buffer_[0..count_-1] geçerli (dolmamışsa)
    // buffer_ tamamen dolmuşsa tüm elemanlar geçerli
    for (std::size_t i = 0; i < count_; ++i)
        sum += static_cast<double>(buffer_[i]);
    return sum / static_cast<double>(count_);
}

double RollingWindow::sigma() const {
    if (count_ < 2) return 0.0;
    double m  = mean();
    double sq = 0.0;
    for (std::size_t i = 0; i < count_; ++i) {
        double d = static_cast<double>(buffer_[i]) - m;
        sq += d * d;
    }
    return std::sqrt(sq / static_cast<double>(count_));
}

double RollingWindow::ucl(double threshold) const {
    return mean() + threshold * sigma();
}

double RollingWindow::lcl(double threshold) const {
    return mean() - threshold * sigma();
}
```

- [ ] **Step 5: Test'leri çalıştır**

```bash
cd build && make kairos_tests && ./tests/kairos_tests --gtest_filter="RollingWindowTest*"
```

Expected:
```
[==========] Running 10 tests from 1 test suite.
[  PASSED  ] 10 tests.
```

- [ ] **Step 6: Commit**

```bash
git add src/rolling_window.hpp src/rolling_window.cpp tests/test_rolling_window.cpp
git commit -m "feat: add RollingWindow with circular buffer and 3-sigma stats"
```

---

## Task 4: Logger

**Files:**
- Modify: `src/logger.hpp`
- Modify: `src/logger.cpp`
- Create: `tests/test_logger.cpp`

- [ ] **Step 1: Failing test yaz**

`tests/test_logger.cpp`:
```cpp
#include "logger.hpp"
#include <gtest/gtest.h>
#include <fstream>
#include <string>

static std::string read_last_line(const std::string& path) {
    std::ifstream f(path);
    std::string line, last;
    while (std::getline(f, line)) last = line;
    return last;
}

TEST(LoggerTest, CreatesLogFile) {
    const std::string path = "/tmp/kairos_test_info.log";
    std::remove(path.c_str());
    { Logger log(path, false); log.info("hello"); }
    std::ifstream f(path);
    EXPECT_TRUE(f.good());
}

TEST(LoggerTest, InfoContainsLevelAndMessage) {
    const std::string path = "/tmp/kairos_test_info2.log";
    std::remove(path.c_str());
    { Logger log(path, false); log.info("test message"); }
    std::string line = read_last_line(path);
    EXPECT_NE(line.find("[INFO]"),        std::string::npos);
    EXPECT_NE(line.find("test message"),  std::string::npos);
}

TEST(LoggerTest, WarnContainsLevel) {
    const std::string path = "/tmp/kairos_test_warn.log";
    std::remove(path.c_str());
    { Logger log(path, false); log.warn("port lost"); }
    std::string line = read_last_line(path);
    EXPECT_NE(line.find("[WARN]"),    std::string::npos);
    EXPECT_NE(line.find("port lost"), std::string::npos);
}

TEST(LoggerTest, AnomalyContainsAllFields) {
    const std::string path = "/tmp/kairos_test_anomaly.log";
    std::remove(path.c_str());
    { Logger log(path, false); log.anomaly(1250000, 1000000.0, 40000.0, 1120000.0, 880000.0); }
    std::string line = read_last_line(path);
    EXPECT_NE(line.find("[ANOMALY]"), std::string::npos);
    EXPECT_NE(line.find("1250000"),   std::string::npos);
    EXPECT_NE(line.find("1000000"),   std::string::npos);
    EXPECT_NE(line.find("1120000"),   std::string::npos);
    EXPECT_NE(line.find("880000"),    std::string::npos);
}

TEST(LoggerTest, SummaryContainsAllFields) {
    const std::string path = "/tmp/kairos_test_summary.log";
    std::remove(path.c_str());
    { Logger log(path, false); log.summary(150, 1000200.0, 39800.0, 1119600.0, 880800.0, 2); }
    std::string line = read_last_line(path);
    EXPECT_NE(line.find("[SUMMARY]"), std::string::npos);
    EXPECT_NE(line.find("150"),       std::string::npos);
    EXPECT_NE(line.find("2"),         std::string::npos);
}

TEST(LoggerTest, FallbackToStdoutIfDirMissing) {
    EXPECT_NO_THROW({
        Logger log("/nonexistent_dir_xyz/kairos.log", false);
        log.info("should not crash");
    });
}

TEST(LoggerTest, AppendNotOverwrite) {
    const std::string path = "/tmp/kairos_test_append.log";
    std::remove(path.c_str());
    { Logger log(path, false); log.info("first"); }
    { Logger log(path, false); log.info("second"); }
    std::ifstream f(path);
    int count = 0;
    std::string line;
    while (std::getline(f, line)) ++count;
    EXPECT_EQ(count, 2);
}
```

- [ ] **Step 2: logger.hpp yaz**

```cpp
// src/logger.hpp
#pragma once
#include <string>
#include <mutex>
#include <fstream>
#include <cstdint>

class Logger {
public:
    // log_path: dosya yolu. stdout_enabled: ekrana da yaz.
    explicit Logger(const std::string& log_path, bool stdout_enabled = true);

    void info(const std::string& msg);
    void warn(const std::string& msg);
    void anomaly(uint32_t cycle, double mean, double sigma, double ucl, double lcl);
    void summary(uint64_t count, double mean, double sigma,
                 double ucl, double lcl, uint64_t anomaly_count);

private:
    void        write(const std::string& level, const std::string& msg);
    std::string timestamp();

    std::mutex   mtx_;
    std::ofstream file_;
    bool          stdout_enabled_;
};
```

- [ ] **Step 3: logger.cpp yaz**

```cpp
// src/logger.cpp
#include "logger.hpp"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <filesystem>

Logger::Logger(const std::string& log_path, bool stdout_enabled)
    : stdout_enabled_(stdout_enabled) {
    try {
        auto dir = std::filesystem::path(log_path).parent_path();
        if (!dir.empty()) std::filesystem::create_directories(dir);
    } catch (...) {}

    file_.open(log_path, std::ios::app);
    if (!file_.is_open()) {
        std::cerr << "[WARN] Cannot open log file: " << log_path
                  << ". Stdout only.\n";
    }
}

std::string Logger::timestamp() {
    auto now = std::chrono::system_clock::now();
    auto t   = std::chrono::system_clock::to_time_t(now);
    std::ostringstream ss;
    ss << std::put_time(std::localtime(&t), "[%Y-%m-%d %H:%M:%S]");
    return ss.str();
}

void Logger::write(const std::string& level, const std::string& msg) {
    std::string line = timestamp() + " [" + level + "] " + msg + "\n";
    std::lock_guard<std::mutex> lock(mtx_);
    if (stdout_enabled_) std::cout << line << std::flush;
    if (file_.is_open()) { file_ << line; file_.flush(); }
}

void Logger::info(const std::string& msg) { write("INFO",    msg); }
void Logger::warn(const std::string& msg) { write("WARN",    msg); }

void Logger::anomaly(uint32_t cycle, double mean, double sigma,
                     double ucl, double lcl) {
    std::ostringstream ss;
    ss << "cycle="  << cycle                       << "\xc2\xb5s"
       << "  mean=" << static_cast<uint64_t>(mean) << "\xc2\xb5s"
       << "  UCL="  << static_cast<uint64_t>(ucl)  << "\xc2\xb5s"
       << "  LCL="  << static_cast<uint64_t>(lcl)  << "\xc2\xb5s"
       << "  sigma="<< static_cast<uint64_t>(sigma) << "\xc2\xb5s";
    write("ANOMALY", ss.str());
}

void Logger::summary(uint64_t count, double mean, double sigma,
                     double ucl, double lcl, uint64_t anomaly_count) {
    std::ostringstream ss;
    ss << "count="     << count
       << "  mean="    << static_cast<uint64_t>(mean)  << "\xc2\xb5s"
       << "  sigma="   << static_cast<uint64_t>(sigma) << "\xc2\xb5s"
       << "  UCL="     << static_cast<uint64_t>(ucl)   << "\xc2\xb5s"
       << "  LCL="     << static_cast<uint64_t>(lcl)   << "\xc2\xb5s"
       << "  anomalies=" << anomaly_count;
    write("SUMMARY", ss.str());
}
```

- [ ] **Step 4: Test'leri çalıştır**

```bash
cd build && make kairos_tests && ./tests/kairos_tests --gtest_filter="LoggerTest*"
```

Expected:
```
[==========] Running 7 tests from 1 test suite.
[  PASSED  ] 7 tests.
```

- [ ] **Step 5: Commit**

```bash
git add src/logger.hpp src/logger.cpp tests/test_logger.cpp
git commit -m "feat: add thread-safe Logger with stdout and file output"
```

---

## Task 5: CycleQueue + SharedState

**Files:**
- Modify: `src/cycle_queue.hpp`
- Modify: `src/shared_state.hpp`
- Create: `tests/test_cycle_queue.cpp`

- [ ] **Step 1: Failing test yaz**

`tests/test_cycle_queue.cpp`:
```cpp
#include "cycle_queue.hpp"
#include <gtest/gtest.h>
#include <thread>
#include <chrono>

TEST(CycleQueueTest, PushAndPop) {
    CycleQueue q;
    q.push(12345u);
    uint32_t val = 0;
    bool ok = q.wait_and_pop(val);
    EXPECT_TRUE(ok);
    EXPECT_EQ(val, 12345u);
}

TEST(CycleQueueTest, MultipleValues) {
    CycleQueue q;
    q.push(1u); q.push(2u); q.push(3u);
    uint32_t v;
    q.wait_and_pop(v); EXPECT_EQ(v, 1u);
    q.wait_and_pop(v); EXPECT_EQ(v, 2u);
    q.wait_and_pop(v); EXPECT_EQ(v, 3u);
}

TEST(CycleQueueTest, StopReturnsFalse) {
    CycleQueue q;
    // stop() öncesi pop bloklanır, stop() sonrası false döner
    std::thread stopper([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        q.stop();
    });
    uint32_t v = 0;
    bool ok = q.wait_and_pop(v);
    EXPECT_FALSE(ok);
    stopper.join();
}

TEST(CycleQueueTest, StopDrainsRemainingItems) {
    // stop() çağrılmış olsa bile kuyrukta veri varsa döner
    CycleQueue q;
    q.push(42u);
    q.stop();
    uint32_t v = 0;
    bool ok = q.wait_and_pop(v);
    EXPECT_TRUE(ok);
    EXPECT_EQ(v, 42u);
    // Şimdi boş ve stopped → false
    ok = q.wait_and_pop(v);
    EXPECT_FALSE(ok);
}
```

- [ ] **Step 2: cycle_queue.hpp yaz**

```cpp
// src/cycle_queue.hpp
#pragma once
#include <queue>
#include <mutex>
#include <condition_variable>
#include <cstdint>

class CycleQueue {
public:
    void push(uint32_t value) {
        {
            std::lock_guard<std::mutex> lock(mtx_);
            queue_.push(value);
        }
        cv_.notify_one();
    }

    // Değer gelene kadar bloklar. false döndüyse: stop() çağrıldı ve kuyruk boş.
    bool wait_and_pop(uint32_t& value) {
        std::unique_lock<std::mutex> lock(mtx_);
        cv_.wait(lock, [&] { return !queue_.empty() || stopped_; });
        if (queue_.empty()) return false;
        value = queue_.front();
        queue_.pop();
        return true;
    }

    // Tüm bekleyen wait_and_pop çağrılarını uyandır ve false döndürt.
    void stop() {
        {
            std::lock_guard<std::mutex> lock(mtx_);
            stopped_ = true;
        }
        cv_.notify_all();
    }

private:
    std::queue<uint32_t>    queue_;
    std::mutex              mtx_;
    std::condition_variable cv_;
    bool                    stopped_ = false;
};
```

- [ ] **Step 3: shared_state.hpp yaz**

```cpp
// src/shared_state.hpp
#pragma once
#include <shared_mutex>
#include <cstdint>

// Tüm thread'ler tarafından okunan/yazılan istatistik durumu.
// Yazıcılar (StatsThread): unique_lock kullanır.
// Okuyucular (DisplayThread, MQTTThread): shared_lock kullanır.
struct SharedState {
    mutable std::shared_mutex mtx;

    double   mean          = 0.0;
    double   sigma         = 0.0;
    double   ucl           = 0.0;
    double   lcl           = 0.0;
    uint32_t last_cycle    = 0;
    uint64_t cycle_count   = 0;
    uint64_t anomaly_count = 0;
    bool     warming_up    = true;
};
```

- [ ] **Step 4: Test'leri çalıştır**

```bash
cd build && make kairos_tests && ./tests/kairos_tests --gtest_filter="CycleQueueTest*"
```

Expected:
```
[==========] Running 4 tests from 1 test suite.
[  PASSED  ] 4 tests.
```

- [ ] **Step 5: Tüm testleri çalıştır**

```bash
cd build && ./tests/kairos_tests
```

Expected:
```
[  PASSED  ] N tests.
```

- [ ] **Step 6: Commit**

```bash
git add src/cycle_queue.hpp src/shared_state.hpp tests/test_cycle_queue.cpp
git commit -m "feat: add CycleQueue with condvar and SharedState with RW-lock"
```

---

## Task 6: SerialThread

**Files:**
- Modify: `src/serial_thread.hpp`
- Modify: `src/serial_thread.cpp`

> Not: Bu task Pi 5 (Linux) üzerinde derlenir. macOS'ta `termios.h` var ama `cfmakeraw` davranışı farklı olabilir. Unit test yok — integration test Task 9'da.

- [ ] **Step 1: serial_thread.hpp yaz**

```cpp
// src/serial_thread.hpp
#pragma once
#include <atomic>
#include "config.hpp"
#include "cycle_queue.hpp"
#include "logger.hpp"

// SerialThread ana fonksiyonu. std::thread ile çalıştırılır.
// running = false olunca döngüden çıkar.
void serial_thread_func(const Config& cfg,
                        CycleQueue&   queue,
                        Logger&       logger,
                        std::atomic<bool>& running);
```

- [ ] **Step 2: serial_thread.cpp yaz**

```cpp
// src/serial_thread.cpp
#include "serial_thread.hpp"
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <string>
#include <thread>
#include <chrono>
#include <stdexcept>

static void configure_serial(int fd) {
    struct termios tty{};
    tcgetattr(fd, &tty);
    cfmakeraw(&tty);
    tty.c_cc[VMIN]  = 0;   // non-blocking read
    tty.c_cc[VTIME] = 10;  // 1 saniye timeout (0.1s * 10)
    tcsetattr(fd, TCSANOW, &tty);
}

static bool parse_cycle_line(const std::string& line, uint32_t& out) {
    if (line.rfind("CYCLE ", 0) != 0) return false;
    try {
        unsigned long val = std::stoul(line.substr(6));
        if (val == 0) return false;
        out = static_cast<uint32_t>(val);
        return true;
    } catch (...) {
        return false;
    }
}

void serial_thread_func(const Config&      cfg,
                        CycleQueue&        queue,
                        Logger&            logger,
                        std::atomic<bool>& running) {
    int backoff_s = 1;

    while (running.load()) {
        int fd = ::open(cfg.port.c_str(), O_RDWR | O_NOCTTY);
        if (fd < 0) {
            logger.warn("Cannot open " + cfg.port +
                        ". Retrying in " + std::to_string(backoff_s) + "s...");
            std::this_thread::sleep_for(std::chrono::seconds(backoff_s));
            backoff_s = std::min(backoff_s * 2, 30);
            continue;
        }

        configure_serial(fd);
        backoff_s = 1;
        logger.info("Serial port opened: " + cfg.port);

        std::string line;
        char        buf[1];

        while (running.load()) {
            ssize_t n = ::read(fd, buf, 1);
            if (n < 0) {
                logger.warn("Read error on " + cfg.port + ". Reconnecting...");
                break;
            }
            if (n == 0) continue;  // timeout, tekrar dene

            if (buf[0] == '\n') {
                // Satır tamamlandı
                uint32_t cycle = 0;
                if (parse_cycle_line(line, cycle)) {
                    queue.push(cycle);
                } else if (!line.empty()) {
                    logger.warn("Malformed line: " + line);
                }
                line.clear();
            } else if (buf[0] != '\r') {
                line += buf[0];
            }
        }

        ::close(fd);
    }
}
```

- [ ] **Step 3: Derle (unit test yok)**

```bash
cd build && make kairos_lib 2>&1
```

Expected: `[100%] Built target kairos_lib` (hatasız)

- [ ] **Step 4: Commit**

```bash
git add src/serial_thread.hpp src/serial_thread.cpp
git commit -m "feat: add SerialThread with POSIX termios and exponential backoff retry"
```

---

## Task 7: StatsThread

**Files:**
- Modify: `src/stats_thread.hpp`
- Modify: `src/stats_thread.cpp`

- [ ] **Step 1: stats_thread.hpp yaz**

```cpp
// src/stats_thread.hpp
#pragma once
#include <atomic>
#include "config.hpp"
#include "cycle_queue.hpp"
#include "shared_state.hpp"
#include "logger.hpp"

// StatsThread ana fonksiyonu. std::thread ile çalıştırılır.
void stats_thread_func(const Config&      cfg,
                       CycleQueue&        queue,
                       SharedState&       state,
                       Logger&            logger,
                       std::atomic<bool>& running);
```

- [ ] **Step 2: stats_thread.cpp yaz**

```cpp
// src/stats_thread.cpp
#include "stats_thread.hpp"
#include "rolling_window.hpp"
#include <string>

void stats_thread_func(const Config&      cfg,
                       CycleQueue&        queue,
                       SharedState&       state,
                       Logger&            logger,
                       std::atomic<bool>& running) {
    RollingWindow window(cfg.window_size);
    uint64_t cycle_count   = 0;
    uint64_t anomaly_count = 0;

    while (running.load()) {
        uint32_t cycle = 0;
        if (!queue.wait_and_pop(cycle)) break;  // queue durduruldu

        window.push(cycle);
        ++cycle_count;

        if (!window.is_ready(cfg.min_samples)) {
            logger.info("Warming up... samples=" +
                        std::to_string(window.size()) + "/" +
                        std::to_string(cfg.min_samples));
            continue;
        }

        const double mean  = window.mean();
        const double sigma = window.sigma();
        const double ucl   = window.ucl(cfg.anomaly_threshold);
        const double lcl   = window.lcl(cfg.anomaly_threshold);

        if (cycle > ucl || cycle < lcl) {
            ++anomaly_count;
            logger.anomaly(cycle, mean, sigma, ucl, lcl);
        }

        if (cycle_count % cfg.summary_interval == 0) {
            logger.summary(cycle_count, mean, sigma, ucl, lcl, anomaly_count);
        }

        // SharedState güncelle (okuyucuları bloklamadan)
        {
            std::unique_lock lock(state.mtx);
            state.mean          = mean;
            state.sigma         = sigma;
            state.ucl           = ucl;
            state.lcl           = lcl;
            state.last_cycle    = cycle;
            state.cycle_count   = cycle_count;
            state.anomaly_count = anomaly_count;
            state.warming_up    = false;
        }
    }
}
```

- [ ] **Step 3: Derle**

```bash
cd build && make kairos_lib 2>&1
```

Expected: Hatasız derleme.

- [ ] **Step 4: Commit**

```bash
git add src/stats_thread.hpp src/stats_thread.cpp
git commit -m "feat: add StatsThread with RollingWindow, anomaly detection, and periodic summary"
```

---

## Task 8: main.cpp

**Files:**
- Modify: `src/main.cpp`

- [ ] **Step 1: main.cpp yaz**

```cpp
// src/main.cpp
#include <iostream>
#include <thread>
#include <atomic>
#include <csignal>
#include <string>
#include "config.hpp"
#include "cycle_queue.hpp"
#include "shared_state.hpp"
#include "logger.hpp"
#include "serial_thread.hpp"
#include "stats_thread.hpp"

static std::atomic<bool> g_running{true};
static CycleQueue*        g_queue_ptr = nullptr;

static void signal_handler(int) {
    g_running = false;
    if (g_queue_ptr) g_queue_ptr->stop();
}

int main(int argc, char* argv[]) {
    // Config: önce dosya, sonra CLI override
    std::string config_path = "kairos.conf";
    // --config argümanını önce bul
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::string(argv[i]) == "--config") {
            config_path = argv[i + 1];
            break;
        }
    }

    Config cfg = load_config_file(config_path);
    apply_args(cfg, argc, argv);

    Logger       logger(cfg.log_path);
    CycleQueue   queue;
    SharedState  state;
    g_queue_ptr = &queue;

    logger.info("Kairos starting. port="      + cfg.port +
                " window="                    + std::to_string(cfg.window_size) +
                " min_samples="               + std::to_string(cfg.min_samples) +
                " threshold="                 + std::to_string(cfg.anomaly_threshold));

    std::signal(SIGINT,  signal_handler);
    std::signal(SIGTERM, signal_handler);

    std::thread serial_t(serial_thread_func,
                         std::cref(cfg), std::ref(queue),
                         std::ref(logger), std::ref(g_running));

    std::thread stats_t(stats_thread_func,
                        std::cref(cfg), std::ref(queue),
                        std::ref(state), std::ref(logger),
                        std::ref(g_running));

    serial_t.join();
    stats_t.join();

    logger.info("Kairos stopped cleanly.");
    return 0;
}
```

- [ ] **Step 2: Derle**

```bash
cd build && make kairos 2>&1
```

Expected: `[100%] Built target kairos`

- [ ] **Step 3: Commit**

```bash
git add src/main.cpp
git commit -m "feat: add main.cpp with signal handler, config loading, and thread management"
```

---

## Task 9: Integration Verification (Pi 5'te)

> Bu task Pi 5 üzerinde çalıştırılır. Pico bağlı veya socat ile simüle edilebilir.

- [ ] **Step 1: Pi 5'te derle**

```bash
mkdir -p build && cd build && cmake .. -DCMAKE_BUILD_TYPE=Release && make -j4
```

Expected: `[100%] Built target kairos`

- [ ] **Step 2: Unit testleri çalıştır**

```bash
cd build && ctest --output-on-failure
```

Expected: `100% tests passed`

- [ ] **Step 3: Port retry testı — Pico bağlı değilken**

```bash
./kairos --port /dev/ttyACM0 --log /tmp/kairos_retry.log
```

Expected log:
```
[...] [INFO]  Kairos starting. port=/dev/ttyACM0 ...
[...] [WARN]  Cannot open /dev/ttyACM0. Retrying in 1s...
[...] [WARN]  Cannot open /dev/ttyACM0. Retrying in 2s...
```

`Ctrl+C` ile durdur → `[INFO] Kairos stopped cleanly.`

- [ ] **Step 4: socat ile simüle serial veri (Pico yoksa)**

Terminal 1:
```bash
socat -d -d pty,raw,echo=0 pty,raw,echo=0
# /dev/pts/X ve /dev/pts/Y yazar — X ve Y'yi not al
```

Terminal 2:
```bash
./kairos --port /dev/pts/X --window 10 --min-samples 5 \
         --summary-interval 10 --threshold 2.0 --log /tmp/kairos_sim.log
```

Terminal 3 — normal veriler gönder:
```bash
for i in $(seq 1 60); do
    echo "CYCLE 1000000" > /dev/pts/Y
    sleep 0.05
done
```

Expected: `[INFO] Warming up...` → `[SUMMARY]` her 10 cycle'da bir

- [ ] **Step 5: Anomaly simülasyonu**

```bash
# Önce 5 normal veri
for i in $(seq 1 5); do echo "CYCLE 1000000" > /dev/pts/Y; done
# Anomaly: çok büyük değer (mean=1M, sigma≈0 → threshold=2σ → UCL≈1M)
echo "CYCLE 9999999" > /dev/pts/Y
```

Expected: `[ANOMALY] cycle=9999999µs ...`

- [ ] **Step 6: Graceful shutdown**

`Ctrl+C` → log dosyasında son satır `[INFO] Kairos stopped cleanly.`

```bash
tail -5 /tmp/kairos_sim.log
```

- [ ] **Step 7: Final commit**

```bash
git add .
git commit -m "chore: verify integration — serial retry, stats, anomaly detection, graceful shutdown"
```

---

## Self-Review Sonuçları

### Spec Coverage

| Gereksinim | Task |
|---|---|
| Thread 1: serial okuma | Task 6 |
| Thread 2: UCL/LCL + anomaly | Task 7 |
| Shared state mutex | Task 5 (SharedState) |
| CMakeLists.txt | Task 1 |
| Genişletilebilir mimari | Task 5 (SharedState header), Task 6-7 (bağımsız dosyalar) |
| Rolling window, configurable | Task 3 |
| anomaly_threshold config | Task 2 |
| Stdout + log dosyası | Task 4 |
| Her 50 cycle özet | Task 7 |
| Exponential backoff retry | Task 6 |
| Graceful shutdown | Task 8 |
| min_samples warm-up | Task 7 |
| Log format | Task 4 |

### Type Consistency

- `RollingWindow::ucl(double threshold)` → Task 3'te tanımlandı, Task 7'de `window.ucl(cfg.anomaly_threshold)` olarak çağrıldı ✓
- `CycleQueue::wait_and_pop(uint32_t&)` → Task 5'te tanımlandı, Task 7'de aynı imzayla kullanıldı ✓
- `Logger::anomaly(uint32_t, double, double, double, double)` → Task 4'te tanımlandı, Task 7'de aynı imzayla çağrıldı ✓
- `Config` struct field isimleri tüm task'larda tutarlı ✓

### Placeholders

Hiçbir adımda TBD/TODO yok — tüm adımlar tam kod içeriyor ✓
