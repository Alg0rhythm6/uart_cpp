/**
 * @file serial.h
 * @brief Declares a POSIX termios-based Linux serial port class.
 * @author alg0rhythm6
 * @date 2026-03-13
 *
 * This file provides serial port configuration, synchronous I/O, buffer
 * management, and callback-driven asynchronous reading.
 */

#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <termios.h>
#include <thread>
#include <vector>

/**
 * @namespace myserial
 * @brief Contains serial communication types and interfaces.
 */
namespace myserial {

/**
 * @class Serial
 * @brief Provides Linux serial port communication.
 *
 * Serial wraps the POSIX termios API and supports runtime configuration,
 * synchronous I/O with timeouts, and a background read thread driven by
 * data and error callbacks.
 *
 * @note Serial objects cannot be copied, but they can be moved.
 * @note Unless otherwise stated, call lastError() after a method returns
 *       `false` or a negative value to obtain an error description.
 */
class Serial {
public:
    /** @brief Supported serial port baud rates. */
    enum class BaudRate : speed_t {
        BR_50      = B50,      ///< 50 baud.
        BR_75      = B75,      ///< 75 baud.
        BR_110     = B110,     ///< 110 baud.
        BR_134     = B134,     ///< 134 baud.
        BR_150     = B150,     ///< 150 baud.
        BR_200     = B200,     ///< 200 baud.
        BR_300     = B300,     ///< 300 baud.
        BR_600     = B600,     ///< 600 baud.
        BR_1200    = B1200,    ///< 1200 baud.
        BR_1800    = B1800,    ///< 1800 baud.
        BR_2400    = B2400,    ///< 2400 baud.
        BR_4800    = B4800,    ///< 4800 baud.
        BR_9600    = B9600,    ///< 9600 baud.
        BR_19200   = B19200,   ///< 19200 baud.
        BR_38400   = B38400,   ///< 38400 baud.
        BR_57600   = B57600,   ///< 57600 baud.
        BR_115200  = B115200,  ///< 115200 baud.
        BR_230400  = B230400,  ///< 230400 baud.
        BR_460800  = B460800,  ///< 460800 baud.
        BR_921600  = B921600,  ///< 921600 baud.
    };

    /** @brief Number of data bits per character. */
    enum class DataBits {
        DB_5 = 5, ///< 5 data bits.
        DB_6 = 6, ///< 6 data bits.
        DB_7 = 7, ///< 7 data bits.
        DB_8 = 8, ///< 8 data bits.
    };

    /** @brief Number of stop bits per character. */
    enum class StopBits {
        SB_ONE, ///< One stop bit.
        SB_TWO, ///< Two stop bits.
    };

    /** @brief Parity mode. */
    enum class Parity {
        None, ///< No parity.
        Odd,  ///< Odd parity.
        Even, ///< Even parity.
    };

    /** @brief Flow control mode. */
    enum class FlowControl {
        None,     ///< No flow control.
        Hardware, ///< RTS/CTS hardware flow control.
        Software, ///< XON/XOFF software flow control.
    };

    /**
     * @brief Callback invoked when data is received.
     * @param data Data received by the background read thread.
     *
     * The callback executes on the background read thread.
     */
    using DataCallback = std::function<void(std::vector<uint8_t>)>;

    /**
     * @brief Callback invoked when a background read error occurs.
     * @param message Error description.
     *
     * The callback executes on the background read thread.
     */
    using ErrorCallback = std::function<void(const std::string&)>;

    /**
     * @struct Config
     * @brief Serial port communication settings.
     */
    struct Config {
        BaudRate    baudRate    = BaudRate::BR_115200; ///< Baud rate. Defaults to 115200.
        DataBits    dataBits    = DataBits::DB_8;       ///< Data bits. Defaults to 8.
        StopBits    stopBits    = StopBits::SB_ONE;     ///< Stop bits. Defaults to one.
        Parity      parity      = Parity::None;         ///< Parity mode. Defaults to none.
        FlowControl flowControl = FlowControl::None;    ///< Flow control. Defaults to none.
        int         readTimeout = 1000;                 ///< Read timeout in milliseconds; -1 blocks indefinitely.
    };

    /** @brief Constructs a Serial object without opening a port. */
    Serial();

    /**
     * @brief Opens a serial port using the default configuration.
     * @param port Device path, such as `/dev/ttyS0` or `/dev/ttyUSB0`.
     * @throws std::runtime_error If the port cannot be opened or configured.
     */
    explicit Serial(const std::string& port);

    /**
     * @brief Opens a serial port using the specified configuration.
     * @param port Device path.
     * @param config Serial port configuration.
     * @throws std::runtime_error If the port cannot be opened or configured.
     */
    explicit Serial(const std::string& port, const Config& config);

    /** @brief Stops the background read thread and closes the port. */
    ~Serial();

    Serial(const Serial&)            = delete; ///< Copy construction is disabled.
    Serial& operator=(const Serial&) = delete; ///< Copy assignment is disabled.

    /**
     * @brief Move-constructs a Serial object.
     * @param other Object whose resources will be transferred.
     *
     * The background read thread of `other` is stopped before ownership of the
     * serial port file descriptor is transferred.
     */
    Serial(Serial&& other) noexcept;

    /**
     * @brief Move-assigns a Serial object.
     * @param other Object whose resources will be transferred.
     * @return A reference to this object.
     */
    Serial& operator=(Serial&& other) noexcept;

    /**
     * @brief Opens a serial port using the default configuration.
     * @param port Device path.
     * @return `true` on success; otherwise `false`.
     */
    bool open(const std::string& port);

    /**
     * @brief Opens a serial port using the specified configuration.
     * @param port Device path.
     * @param config Serial port configuration.
     * @return `true` on success; otherwise `false`.
     *
     * Any currently open port is closed first.
     */
    bool open(const std::string& port, const Config& config);

    /** @brief Stops the background read thread and closes the current port. */
    void close();

    /**
     * @brief Checks whether the serial port is open.
     * @return `true` if the port is open; otherwise `false`.
     */
    bool isOpen() const;

    /**
     * @brief Writes raw bytes to the serial port.
     * @param data Pointer to the data to write.
     * @param length Number of bytes to write.
     * @return Number of bytes written, or a negative value on failure.
     */
    ssize_t write(const uint8_t* data, size_t length);

    /**
     * @brief Writes the raw bytes contained in a string.
     * @param data String to write.
     * @return Number of bytes written, or a negative value on failure.
     */
    ssize_t write(const std::string& data);

    /**
     * @brief Writes a byte vector.
     * @param data Bytes to write.
     * @return Number of bytes written, or a negative value on failure.
     */
    ssize_t write(const std::vector<uint8_t>& data);

    /**
     * @brief Reads received data into a buffer.
     * @param buffer Destination buffer.
     * @param maxLen Maximum number of bytes to read.
     * @return Number of bytes read, 0 on timeout, or a negative value on failure.
     */
    ssize_t read(uint8_t* buffer, size_t maxLen);

    /**
     * @brief Reads received data into a byte vector.
     * @param maxLen Maximum number of bytes to read.
     * @return Received bytes, or an empty vector on timeout, failure, or when
     *         `maxLen` is zero.
     */
    std::vector<uint8_t> read(size_t maxLen);

    /**
     * @brief Reads until a delimiter, timeout, or length limit is reached.
     * @param delimiter Byte that terminates the read operation.
     * @param maxLen Maximum number of bytes to read.
     * @return Bytes read. The delimiter is included if it was received.
     */
    std::vector<uint8_t> readUntil(uint8_t delimiter, size_t maxLen = 4096);

    /**
     * @brief Reads one newline-terminated line.
     * @param maxLen Maximum number of bytes to read.
     * @return Received text. The result includes `\n` if it was received.
     */
    std::string readLine(size_t maxLen = 4096);

    /**
     * @brief Returns the number of bytes immediately available for reading.
     * @return Number of available bytes, or 0 if the port is not open.
     */
    int available() const;

    /** @brief Discards all data in the receive buffer. */
    void flushInput();

    /** @brief Blocks until all data in the transmit buffer has been sent. */
    void flushOutput();

    /** @brief Discards all data in the receive and transmit buffers. */
    void flush();

    /**
     * @brief Updates the complete serial port configuration.
     * @param config New serial port configuration.
     * @return `true` on success; otherwise `false`.
     *
     * If the port is closed, the configuration is stored and applied by the
     * next call to open().
     */
    bool setConfig(const Config& config);

    /**
     * @brief Updates the serial port baud rate.
     * @param baud New baud rate.
     * @return `true` on success; otherwise `false`.
     */
    bool setBaudRate(BaudRate baud);

    /**
     * @brief Sets the timeout used by synchronous read operations.
     * @param milliseconds Timeout in milliseconds; -1 blocks indefinitely.
     * @return Always returns `true`.
     */
    bool setTimeout(int milliseconds);

    /**
     * @brief Returns the current serial port configuration.
     * @return A read-only reference to the current configuration.
     */
    const Config& getConfig() const { return config_; }

    /**
     * @brief Returns the current serial port device path.
     * @return A read-only reference to the device path, or an empty string if
     *         no port is open.
     */
    const std::string& getPort() const { return port_; }

    /**
     * @brief Starts the background read thread.
     * @param onData Callback invoked when data is received.
     * @param onError Callback invoked when a read error occurs; may be empty.
     * @param bufSize Buffer size used by each background read operation.
     *
     * The thread waits indefinitely for serial data until stopReadThread(),
     * close(), or the destructor requests it to stop. Repeated calls do not
     * create additional read threads.
     */
    void startReadThread(DataCallback onData,
                         ErrorCallback onError = nullptr,
                         size_t bufSize = 4096);

    /** @brief Requests the background read thread to stop and waits for it. */
    void stopReadThread();

    /**
     * @brief Checks whether the background read thread is running.
     * @return `true` if the thread is running; otherwise `false`.
     */
    bool isReadThreadRunning() const;

    /**
     * @brief Returns the most recent error description.
     * @return A read-only reference to the most recent error description.
     */
    const std::string& lastError() const { return lastError_; }

private:
    int         fd_ = -1;   ///< Serial port file descriptor; -1 when closed.
    std::string port_;      ///< Current serial port device path.
    Config      config_;    ///< Current serial port configuration.
    std::string lastError_; ///< Most recent error description.

    std::thread       readThread_;           ///< Background read thread.
    std::atomic<bool> running_{false};       ///< Background thread running flag.
    DataCallback      dataCb_;               ///< Data reception callback.
    ErrorCallback     errorCb_;              ///< Background read error callback.
    int               stopPipe_[2]{-1, -1}; ///< Self-pipe used to stop the read thread.
    size_t            readBufSize_{4096};    ///< Buffer size for each background read.

    /**
     * @brief Applies config_ to the open serial port.
     * @return `true` on success; otherwise `false`.
     */
    bool applyConfig();

    /**
     * @brief Stores the most recent error description.
     * @param msg Error description.
     */
    void setError(const std::string& msg);

    /**
     * @brief Waits until the serial port becomes readable.
     * @param timeoutMs Timeout in milliseconds; a negative value waits forever.
     * @return `true` if the port is readable; `false` on timeout or failure.
     */
    bool waitReadable(int timeoutMs) const;

    /** @brief Entry point for the background read thread. */
    void readThreadFunc();
};

} // namespace myserial
