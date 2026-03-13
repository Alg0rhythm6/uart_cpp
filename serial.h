/*
 * @Author: alg0rhythm6 hahahahhh6@outlook.com
 * @Date: 2026-03-13 01:05:48
 * @LastEditors: alg0rhythm6 hahahahhh6@outlook.com
 * @LastEditTime: 2026-03-13 16:55:50
 * @FilePath: \uart\serial.h
 * @Description: Declaration of the myserial::Serial class.
 *               A lightweight, dependency-free Linux serial port library built on
 *               POSIX termios. Exposes enums for baud rate, data bits, stop bits,
 *               parity and flow control; synchronous read/write with timeout;
 *               and an asynchronous background read thread driven by callbacks.
 * 
 * Copyright (c) 2026 by alg0rhythm6, All Rights Reserved. 
 */


#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <functional>
#include <thread>
#include <atomic>
#include <termios.h>

namespace myserial {

/**
 * @brief Linux serial port communication library
 *
 * Features:
 *  - Configurable baud rate, data bits, stop bits, parity
 *  - Blocking / non-blocking read & write
 *  - Timeout control
 *  - Hardware / software flow control
 *  - Buffer flushing
 */
class Serial {
public:
    // ── Enum definitions ──────────────────────────────────────────────────────

    enum class BaudRate : speed_t {
        BR_50      = B50,
        BR_75      = B75,
        BR_110     = B110,
        BR_134     = B134,
        BR_150     = B150,
        BR_200     = B200,
        BR_300     = B300,
        BR_600     = B600,
        BR_1200    = B1200,
        BR_1800    = B1800,
        BR_2400    = B2400,
        BR_4800    = B4800,
        BR_9600    = B9600,
        BR_19200   = B19200,
        BR_38400   = B38400,
        BR_57600   = B57600,
        BR_115200  = B115200,
        BR_230400  = B230400,
        BR_460800  = B460800,
        BR_921600  = B921600,
    };

    enum class DataBits {
        DB_5 = 5,
        DB_6 = 6,
        DB_7 = 7,
        DB_8 = 8,
    };

    enum class StopBits {
        SB_ONE,   ///< 1 stop bit
        SB_TWO,   ///< 2 stop bits
    };

    enum class Parity {
        None,
        Odd,
        Even,
    };

    enum class FlowControl {
        None,
        Hardware,   ///< RTS/CTS
        Software,   ///< XON/XOFF
    };

    // ── Callback types ─────────────────────────────────────────────────────────
    using DataCallback  = std::function<void(std::vector<uint8_t>)>;
    using ErrorCallback = std::function<void(const std::string&)>;

    // ── Configuration struct ──────────────────────────────────────────────────

    struct Config {
        BaudRate    baudRate    = BaudRate::BR_115200;
        DataBits    dataBits    = DataBits::DB_8;
        StopBits    stopBits    = StopBits::SB_ONE;
        Parity      parity      = Parity::None;
        FlowControl flowControl = FlowControl::None;
        int         readTimeout = 1000; ///< Read timeout in milliseconds; -1 = block indefinitely
    };

    // ── Lifecycle ─────────────────────────────────────────────────────────────

    Serial();
    // Note: = Config{} default argument is avoided due to GCC nested-class
    // default-initializer limitations. Two overloads are used instead.
    explicit Serial(const std::string& port);
    explicit Serial(const std::string& port, const Config& config);
    ~Serial();

    // Copy is deleted; move is allowed.
    Serial(const Serial&)            = delete;
    Serial& operator=(const Serial&) = delete;
    Serial(Serial&& other) noexcept;
    Serial& operator=(Serial&& other) noexcept;

    // ── Connection control ────────────────────────────────────────────────────

    /**
     * @brief Open the serial port
     * @param port   Device path, e.g. "/dev/ttyS0" or "/dev/ttyUSB0"
     * @param config Serial configuration
     * @return true on success
     */
    bool open(const std::string& port);
    bool open(const std::string& port, const Config& config);

    /**
     * @brief Close the serial port
     */
    void close();

    /**
     * @brief Returns true if the port is open
     */
    bool isOpen() const;

    // ── Read / Write ──────────────────────────────────────────────────────────

    /**
     * @brief Write raw bytes
     * @return Number of bytes written, or < 0 on error
     */
    ssize_t write(const uint8_t* data, size_t length);

    /**
     * @brief Write a string
     * @return Number of bytes written, or < 0 on error
     */
    ssize_t write(const std::string& data);

    /**
     * @brief Write a byte vector
     * @return Number of bytes written, or < 0 on error
     */
    ssize_t write(const std::vector<uint8_t>& data);

    /**
     * @brief Read into a buffer
     * @param buffer  Output buffer
     * @param maxLen  Maximum bytes to read
     * @return Bytes read; 0 = timeout; < 0 = error
     */
    ssize_t read(uint8_t* buffer, size_t maxLen);

    /**
     * @brief Read into a vector
     * @param maxLen  Maximum bytes to read
     * @return Byte vector; empty on timeout or error
     */
    std::vector<uint8_t> read(size_t maxLen);

    /**
     * @brief Read until a delimiter byte or timeout
     * @param delimiter Terminating byte (e.g. '\n')
     * @param maxLen    Maximum byte count
     * @return Byte vector including the delimiter
     */
    std::vector<uint8_t> readUntil(uint8_t delimiter, size_t maxLen = 4096);

    /**
     * @brief Read one line (terminated by '\n')
     */
    std::string readLine(size_t maxLen = 4096);

    // ── Buffer & status ───────────────────────────────────────────────────────

    /**
     * @brief Returns the number of bytes available in the receive buffer
     */
    int available() const;

    /**
     * @brief Discard the receive buffer
     */
    void flushInput();

    /**
     * @brief Wait until the transmit buffer is fully sent
     */
    void flushOutput();

    /**
     * @brief Discard both receive and transmit buffers
     */
    void flush();

    // ── Runtime configuration ─────────────────────────────────────────────────

    bool setConfig(const Config& config);
    bool setBaudRate(BaudRate baud);
    bool setTimeout(int milliseconds); ///< Read timeout (ms); -1 = block indefinitely

    const Config&      getConfig() const { return config_; }
    const std::string& getPort()   const { return port_; }

    // ── Read thread ───────────────────────────────────────────────────────────

    /**
     * @brief Start a background read thread that blocks indefinitely
     *        and fires callbacks on each received chunk
     * @param onData   Called from the read thread when data arrives
     * @param onError  Called from the read thread on errors (may be nullptr)
     * @param bufSize  Maximum bytes per read call
     */
    void startReadThread(DataCallback onData,
                         ErrorCallback onError = nullptr,
                         size_t bufSize = 4096);

    /**
     * @brief Stop the background read thread (blocks until it exits)
     */
    void stopReadThread();

    /**
     * @brief Returns true if the read thread is running
     */
    bool isReadThreadRunning() const;

    // ── Error information ─────────────────────────────────────────────────────

    /**
     * @brief Returns a description of the last error
     */
    const std::string& lastError() const { return lastError_; }

private:
    int         fd_        = -1;
    std::string port_;
    Config      config_;
    std::string lastError_;

    // ── Private read-thread members ───────────────────────────────────────────
    std::thread       readThread_;
    std::atomic<bool> running_    {false};
    DataCallback      dataCb_;
    ErrorCallback     errorCb_;
    int               stopPipe_[2] {-1, -1}; ///< self-pipe: write end triggers shutdown
    size_t            readBufSize_ {4096};

    bool applyConfig();
    void setError(const std::string& msg);
    bool waitReadable(int timeoutMs) const;
    void readThreadFunc();
};

} // namespace myserial
