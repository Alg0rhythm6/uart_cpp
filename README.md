# myserial — Linux C++ Serial Port Library

A lightweight, dependency-free Linux serial port library built on top of POSIX `termios`. Supports synchronous read/write, timeout control, and a background read thread.

---

## Features

| Feature | Details |
|---|---|
| Configurable baud rate | 50 ~ 921600 bps |
| Data bits | 5 / 6 / 7 / 8 |
| Stop bits | 1 / 2 |
| Parity | None / Odd / Even |
| Flow control | None / Hardware (RTS/CTS) / Software (XON/XOFF) |
| Read timeout | Millisecond precision, `-1` for blocking indefinitely |
| Background read thread | Async receive with callbacks, self-pipe graceful shutdown |
| Move semantics | Supports `std::move`, copy is deleted |

---

## File Structure

```
uart/
├── serial.h        # Class declaration, enums, config struct
├── serial.cpp      # Implementation
└── example.cpp     # Usage examples
```

---

## Quick Start

### 1. Build

```bash
g++ -std=c++17 -pthread example.cpp serial.cpp -o uart_example
```

### 2. Configure and Open

```cpp
#include "serial.h"

myserial::Serial::Config cfg;
cfg.baudRate    = myserial::Serial::BaudRate::BR_115200;
cfg.dataBits    = myserial::Serial::DataBits::DB_8;
cfg.stopBits    = myserial::Serial::StopBits::SB_ONE;
cfg.parity      = myserial::Serial::Parity::None;
cfg.flowControl = myserial::Serial::FlowControl::None;
cfg.readTimeout = 1000; // ms, -1 for indefinite blocking

myserial::Serial ser;
if (!ser.open("/dev/ttyUSB0", cfg)) {
    std::cerr << ser.lastError() << '\n';
}
```

Alternatively, open directly in the constructor (throws `std::runtime_error` on failure):

```cpp
myserial::Serial ser("/dev/ttyUSB0", cfg);
```

---

## API Reference

### Connection Control

```cpp
bool open(const std::string& port);
bool open(const std::string& port, const Config& config);
void close();
bool isOpen() const;
```

### Write

```cpp
ssize_t write(const uint8_t* data, size_t length);
ssize_t write(const std::string& data);
ssize_t write(const std::vector<uint8_t>& data);
```

Returns the number of bytes written, or `< 0` on error.

### Synchronous Read

```cpp
ssize_t              read(uint8_t* buffer, size_t maxLen);
std::vector<uint8_t> read(size_t maxLen);
std::vector<uint8_t> readUntil(uint8_t delimiter, size_t maxLen = 4096);
std::string          readLine(size_t maxLen = 4096);
```

- Returns `0` on timeout, `< 0` on error.
- `readUntil` / `readLine` return after the delimiter is found or a timeout occurs.

### Buffer Management

```cpp
int  available() const;  // Number of bytes available in the receive buffer
void flushInput();       // Discard the receive buffer
void flushOutput();      // Wait until the transmit buffer is drained
void flush();            // Discard both receive and transmit buffers
```

### Runtime Configuration

```cpp
bool setConfig(const Config& config);
bool setBaudRate(BaudRate baud);
bool setTimeout(int milliseconds);
```

### Background Read Thread

```cpp
// Start the read thread
void startReadThread(
    DataCallback  onData,              // void(std::vector<uint8_t>)
    ErrorCallback onError = nullptr,   // void(const std::string&)
    size_t        bufSize = 4096
);

void stopReadThread();           // Block until the read thread exits
bool isReadThreadRunning() const;
```

### Error Information

```cpp
const std::string& lastError() const;
```

---

## Example: Background Read Thread with Frame Parsing

```cpp
#include "serial.h"
#include <atomic>
#include <mutex>
#include <iostream>
#include <cstdio>

int main() {
    std::atomic<float> g_pos{0.0f}, g_spd{0.0f};
    std::mutex cout_mtx;

    myserial::Serial::Config cfg;
    cfg.baudRate = myserial::Serial::BaudRate::BR_921600;

    myserial::Serial ser;
    ser.open("/dev/ttyCH341USB0", cfg);
    ser.flushInput();

    ser.startReadThread(
        // onData: called from the read thread on each received chunk
        [&](std::vector<uint8_t> raw) {
            std::string str(raw.begin(), raw.end());
            float pos, spd;
            if (std::sscanf(str.c_str(), "P:%f,S:%f", &pos, &spd) == 2) {
                g_pos.store(pos, std::memory_order_relaxed);
                g_spd.store(spd, std::memory_order_relaxed);
            }
        },
        // onError: called from the read thread on internal errors
        [&](const std::string& err) {
            std::lock_guard<std::mutex> lk(cout_mtx);
            std::cerr << "[Error] " << err << '\n';
        }
    );

    // Main thread consumes the latest values
    while (true) {
        float pos = g_pos.load(std::memory_order_relaxed);
        float spd = g_spd.load(std::memory_order_relaxed);
        // ... control logic ...
    }

    ser.stopReadThread();
    ser.close();
}
```

---

## Thread Safety

| Operation | Thread Safe |
|---|---|
| `write()` | ✅ Safe to call from the main thread |
| `read()` / `readLine()` | ⚠️ Mutually exclusive with the read thread — use one or the other |
| Atomic variables (`g_pos`, `g_spd`) | ✅ Lock-free read/write |
| `std::cout` output | ⚠️ Protect with `std::mutex` |

> Using synchronous `read()` and the background read thread at the same time causes a data race. Choose one approach.

---

## License

MIT License
