#include "serial_thread.hpp"
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <string>
#include <thread>
#include <chrono>

static void configure_serial(int fd) {
    struct termios tty{};
    tcgetattr(fd, &tty);
    cfmakeraw(&tty);
    tty.c_cc[VMIN]  = 0;  // non-blocking read
    tty.c_cc[VTIME] = 1;  // 100ms timeout — fast command response
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
                        CycleQueue&        cycle_queue,
                        CommandQueue&      cmd_queue,
                        Logger&            logger,
                        std::atomic<bool>& running) {
    int backoff_s = 1;

    while (running.load()) {
        int fd = ::open(cfg.port.c_str(), O_RDWR | O_NOCTTY);
        if (fd < 0) {
            logger.warn("Cannot open " + cfg.port +
                        ". Retrying in " + std::to_string(backoff_s) + "s...");
            for (int i = 0; i < backoff_s * 10 && running.load(); ++i)
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            backoff_s = std::min(backoff_s * 2, 30);
            continue;
        }

        configure_serial(fd);
        backoff_s = 1;
        logger.info("Serial port opened: " + cfg.port);

        if (cfg.autostart) {
            ::write(fd, "START\n", 6);
            logger.info("Serial → Pico: START (autostart)");
        }

        std::string line;
        char        buf[1];

        while (running.load()) {
            // Drain outgoing command queue before each read
            std::string cmd;
            while (cmd_queue.try_pop(cmd)) {
                ::write(fd, cmd.c_str(), cmd.size());
                logger.info("Serial → Pico: " + cmd.substr(0, cmd.size()-1));
            }

            ssize_t n = ::read(fd, buf, 1);
            if (n < 0) {
                logger.warn("Read error on " + cfg.port + ". Reconnecting...");
                break;
            }
            if (n == 0) continue;  // 100ms timeout — loop back to check cmd_queue

            if (buf[0] == '\n') {
                uint32_t cycle = 0;
                if (parse_cycle_line(line, cycle)) {
                    cycle_queue.push(cycle);
                } else if (!line.empty()) {
                    logger.warn("Malformed line: " + line);
                }
                line.clear();
            } else if (buf[0] != '\r') {
                line += buf[0];
            }
        }

        // Clean shutdown: notify Pico to stop before closing port
        if (!running.load()) {
            ::write(fd, "STOP\n", 5);
            logger.info("Serial → Pico: STOP");
        }
        ::close(fd);
    }

    cycle_queue.stop();  // Unblock stats_thread so it can exit
}
