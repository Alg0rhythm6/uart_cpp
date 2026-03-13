/**
 * example.cpp — myserial::Serial library usage examples
 *
 * Demonstrates the following features:
 *   1. Basic configuration, open and close
 *   2. Synchronous write
 *   3. Synchronous read with timeout
 *   4. Background read thread (async receive + frame parsing)
 *   5. Graceful shutdown via Ctrl+C signal handling
 */

#include "serial.h"

#include <iostream>
#include <iomanip>
#include <cstdio>
#include <cstring>
#include <atomic>
#include <mutex>
#include <thread>
#include <chrono>
#include <csignal>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// Example 1: Synchronous read/write (no read thread)
// ─────────────────────────────────────────────────────────────────────────────

void example_sync()
{
    std::cout << "\n=== Example 1: Synchronous Read/Write ===\n";

    myserial::Serial ser;

    myserial::Serial::Config cfg;
    cfg.baudRate    = myserial::Serial::BaudRate::BR_115200;
    cfg.dataBits    = myserial::Serial::DataBits::DB_8;
    cfg.stopBits    = myserial::Serial::StopBits::SB_ONE;
    cfg.parity      = myserial::Serial::Parity::None;
    cfg.flowControl = myserial::Serial::FlowControl::None;
    cfg.readTimeout = 1000; // read timeout: 1000 ms

    if (!ser.open("/dev/ttyUSB0", cfg)) {
        std::cerr << "Open failed: " << ser.lastError() << '\n';
        return;
    }

    // Write a string
    ser.write("Hello Device!\r\n");

    // Synchronously read one line (terminated by '\n', up to 256 bytes)
    std::string line = ser.readLine(256);
    std::cout << "Received: " << line << '\n';

    ser.close();
}

// ─────────────────────────────────────────────────────────────────────────────
// Example 2: Background read thread + parsing "P:<pos>,S:<spd>" frames
// ─────────────────────────────────────────────────────────────────────────────

void example_read_thread()
{
    std::cout << "\n=== Example 2: Background Read Thread (position/speed frame parsing) ===\n";

    // Shared data (atomic variables, lock-free)
    std::atomic<bool>  g_running{true};
    std::atomic<float> g_pos{0.0f};
    std::atomic<float> g_spd{0.0f};
    std::mutex         cout_mtx;

    // Ctrl+C signal -> set exit flag
    static std::atomic<bool>* s_running = &g_running;
    std::signal(SIGINT,  [](int) { s_running->store(false); });
    std::signal(SIGTERM, [](int) { s_running->store(false); });

    myserial::Serial::Config cfg;
    cfg.baudRate = myserial::Serial::BaudRate::BR_921600;

    myserial::Serial ser;
    if (!ser.open("/dev/ttyCH341USB0", cfg)) {
        std::cerr << "Open failed: " << ser.lastError() << '\n';
        return;
    }
    ser.flushInput();

    // ── Start the read thread ─────────────────────────────────────────────────
    ser.startReadThread(
        // onData callback: called asynchronously from the read thread
        [&g_pos, &g_spd, &cout_mtx](std::vector<uint8_t> raw) {
            std::string str(raw.begin(), raw.end());

            float pos = 0.0f, spd = 0.0f;
            if (std::sscanf(str.c_str(), "P:%f,S:%f", &pos, &spd) == 2) {
                // Atomic write; main thread reads these values
                g_pos.store(pos, std::memory_order_relaxed);
                g_spd.store(spd, std::memory_order_relaxed);

                std::lock_guard<std::mutex> lk(cout_mtx);
                std::cout << std::fixed << std::setprecision(4)
                          << "[RX] pos=" << pos << "  spd=" << spd << '\n';
            } else {
                // Unknown format — print raw string as-is
                std::lock_guard<std::mutex> lk(cout_mtx);
                std::cout << "[RX RAW] " << str;
            }
        },
        // onError callback: called from the read thread on internal errors
        [&cout_mtx](const std::string& err) {
            std::lock_guard<std::mutex> lk(cout_mtx);
            std::cerr << "[Read thread error] " << err << '\n';
        }
    );

    std::cout << "Read thread started. Press Ctrl+C to exit.\n";

    // ── Main thread: consume the latest data every 500 ms ─────────────────────
    while (g_running.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        // Load the latest values written by the read thread
        float pos = g_pos.load(std::memory_order_relaxed);
        float spd = g_spd.load(std::memory_order_relaxed);

        // Use pos/spd for control logic, logging, etc.
        std::lock_guard<std::mutex> lk(cout_mtx);
        std::cout << "[Main] pos=" << pos << "  spd=" << spd << '\n';
    }

    // ── Graceful shutdown ─────────────────────────────────────────────────────
    std::cout << "\nStopping read thread...\n";
    ser.stopReadThread(); // Block until the read thread fully exits
    ser.close();
    std::cout << "Port closed.\n";
}

// ─────────────────────────────────────────────────────────────────────────────
// Example 3: Send a raw byte array
// ─────────────────────────────────────────────────────────────────────────────

void example_write_bytes()
{
    std::cout << "\n=== Example 3: Send Raw Bytes ===\n";

    myserial::Serial ser("/dev/ttyUSB0"); // Open in constructor; throws on failure

    std::vector<uint8_t> frame = {0xAA, 0x01, 0x02, 0x03, 0xFF};
    ssize_t n = ser.write(frame);
    std::cout << "Sent " << n << " bytes\n";

    ser.close();
}

// ─────────────────────────────────────────────────────────────────────────────
// Entry point
// ─────────────────────────────────────────────────────────────────────────────

int main()
{
    // Uncomment the example you want to run
    // example_sync();
    example_read_thread();
    // example_write_bytes();
    return 0;
}
