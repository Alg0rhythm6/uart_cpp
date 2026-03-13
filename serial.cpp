#include "serial.h"

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <utility>

namespace myserial {

// ─────────────────────────────────────────────────────────────────────────────
// Constructor / Destructor
// ─────────────────────────────────────────────────────────────────────────────

Serial::Serial() = default;

Serial::Serial(const std::string& port)
    : Serial(port, Config{})
{}

Serial::Serial(const std::string& port, const Config& config)
{
    if (!open(port, config))
        throw std::runtime_error("Serial: failed to open " + port + " : " + lastError_);
}

Serial::~Serial()
{
    close();
}

Serial::Serial(Serial&& other) noexcept
    : fd_(other.fd_)
    , port_(std::move(other.port_))
    , config_(other.config_)
    , lastError_(std::move(other.lastError_))
    , readBufSize_(other.readBufSize_)
{
    // The read thread holds a pointer to 'this' and cannot be transferred directly;
    // stop it safely first.
    other.stopReadThread();
    other.fd_ = -1;
}

Serial& Serial::operator=(Serial&& other) noexcept
{
    if (this != &other) {
        stopReadThread();
        close();
        fd_          = other.fd_;
        port_        = std::move(other.port_);
        config_      = other.config_;
        lastError_   = std::move(other.lastError_);
        readBufSize_ = other.readBufSize_;
        // Stop the source thread before transferring ownership.
        other.stopReadThread();
        other.fd_ = -1;
    }
    return *this;
}

// ─────────────────────────────────────────────────────────────────────────────
// Connection control
// ─────────────────────────────────────────────────────────────────────────────

bool Serial::open(const std::string& port)
{
    return open(port, Config{});
}

bool Serial::open(const std::string& port, const Config& config)
{
    if (isOpen()) close();

    // O_NOCTTY : do not make the port the controlling terminal
    // O_NDELAY : non-blocking open (avoids hanging on CLOCAL / carrier detect)
    fd_ = ::open(port.c_str(), O_RDWR | O_NOCTTY | O_NDELAY);
    if (fd_ < 0) {
        setError(std::string("open() failed: ") + std::strerror(errno));
        return false;
    }

    // Restore blocking I/O (timeout is controlled via select())
    if (::fcntl(fd_, F_SETFL, 0) < 0) {
        setError(std::string("fcntl() failed: ") + std::strerror(errno));
        ::close(fd_);
        fd_ = -1;
        return false;
    }

    port_   = port;
    config_ = config;

    if (!applyConfig()) {
        ::close(fd_);
        fd_ = -1;
        return false;
    }
    return true;
}

void Serial::close()
{
    stopReadThread(); // Stop the read thread safely before closing
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
    port_.clear();
}

bool Serial::isOpen() const
{
    return fd_ >= 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// Internal: apply termios configuration
// ─────────────────────────────────────────────────────────────────────────────

bool Serial::applyConfig()
{
    struct termios tty{};
    if (::tcgetattr(fd_, &tty) < 0) {
        setError(std::string("tcgetattr() failed: ") + std::strerror(errno));
        return false;
    }

    // ── Baud rate ─────────────────────────────────────────────────────────────
    speed_t speed = static_cast<speed_t>(config_.baudRate);
    ::cfsetispeed(&tty, speed);
    ::cfsetospeed(&tty, speed);

    // ── Raw mode base settings ────────────────────────────────────────────────
    tty.c_cflag |=  (CLOCAL | CREAD); // local connection, enable receiver
    tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG); // raw mode
    tty.c_iflag &= ~(IXON | IXOFF | IXANY);         // disable software flow control
    tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK |
                     ISTRIP | INLCR | IGNCR | ICRNL); // no special character processing
    tty.c_oflag &= ~OPOST;  // raw output

    // ── Data bits ─────────────────────────────────────────────────────────────
    tty.c_cflag &= ~CSIZE;
    switch (config_.dataBits) {
        case DataBits::DB_5: tty.c_cflag |= CS5; break;
        case DataBits::DB_6: tty.c_cflag |= CS6; break;
        case DataBits::DB_7: tty.c_cflag |= CS7; break;
        case DataBits::DB_8:
        default:             tty.c_cflag |= CS8; break;
    }

    // ── Stop bits ─────────────────────────────────────────────────────────────
    if (config_.stopBits == StopBits::SB_TWO)
        tty.c_cflag |= CSTOPB;
    else
        tty.c_cflag &= ~CSTOPB;

    // ── Parity ────────────────────────────────────────────────────────────────
    switch (config_.parity) {
        case Parity::Odd:
            tty.c_cflag |=  PARENB;
            tty.c_cflag |=  PARODD;
            tty.c_iflag |=  INPCK;
            break;
        case Parity::Even:
            tty.c_cflag |=  PARENB;
            tty.c_cflag &= ~PARODD;
            tty.c_iflag |=  INPCK;
            break;
        case Parity::None:
        default:
            tty.c_cflag &= ~PARENB;
            tty.c_iflag &= ~INPCK;
            break;
    }

    // ── Hardware flow control ─────────────────────────────────────────────────
    if (config_.flowControl == FlowControl::Hardware)
        tty.c_cflag |= CRTSCTS;
    else
        tty.c_cflag &= ~CRTSCTS;

    if (config_.flowControl == FlowControl::Software) {
        tty.c_iflag |= (IXON | IXOFF);
    }

    // ── Read timeout (VMIN / VTIME) ──────────────────────────────────────────
    // Timeout is controlled via select(); set VMIN=0, VTIME=0 here.
    tty.c_cc[VMIN]  = 0;
    tty.c_cc[VTIME] = 0; // delegated to select()

    if (::tcsetattr(fd_, TCSANOW, &tty) < 0) {
        setError(std::string("tcsetattr() failed: ") + std::strerror(errno));
        return false;
    }
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Write
// ─────────────────────────────────────────────────────────────────────────────

ssize_t Serial::write(const uint8_t* data, size_t length)
{
    if (!isOpen()) { setError("Port is not open"); return -1; }

    ssize_t total = 0;
    while (static_cast<size_t>(total) < length) {
        ssize_t n = ::write(fd_, data + total,
                            length - static_cast<size_t>(total));
        if (n < 0) {
            if (errno == EINTR) continue; // interrupted by signal, retry
            setError(std::string("write() failed: ") + std::strerror(errno));
            return -1;
        }
        total += n;
    }
    return total;
}

ssize_t Serial::write(const std::string& data)
{
    return write(reinterpret_cast<const uint8_t*>(data.data()), data.size());
}

ssize_t Serial::write(const std::vector<uint8_t>& data)
{
    return write(data.data(), data.size());
}

// ─────────────────────────────────────────────────────────────────────────────
// Helpers: wait for data (select with timeout)
// ─────────────────────────────────────────────────────────────────────────────

bool Serial::waitReadable(int timeoutMs) const
{
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(fd_, &readfds);

    if (timeoutMs < 0) {
        // Block indefinitely
        int ret = ::select(fd_ + 1, &readfds, nullptr, nullptr, nullptr);
        return ret > 0;
    }

    struct timeval tv{};
    tv.tv_sec  = timeoutMs / 1000;
    tv.tv_usec = (timeoutMs % 1000) * 1000;
    int ret = ::select(fd_ + 1, &readfds, nullptr, nullptr, &tv);
    return ret > 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// Read
// ─────────────────────────────────────────────────────────────────────────────

ssize_t Serial::read(uint8_t* buffer, size_t maxLen)
{
    if (!isOpen()) { setError("Port is not open"); return -1; }
    if (maxLen == 0) return 0;

    if (!waitReadable(config_.readTimeout)) {
        return 0; // timeout
    }

    ssize_t n;
    do {
        n = ::read(fd_, buffer, maxLen);
    } while (n < 0 && errno == EINTR);

    if (n < 0) {
        setError(std::string("read() failed: ") + std::strerror(errno));
        return -1;
    }
    return n;
}

std::vector<uint8_t> Serial::read(size_t maxLen)
{
    std::vector<uint8_t> buf(maxLen);
    ssize_t n = read(buf.data(), maxLen);
    if (n <= 0) return {};
    buf.resize(static_cast<size_t>(n));
    return buf;
}

std::vector<uint8_t> Serial::readUntil(uint8_t delimiter, size_t maxLen)
{
    if (!isOpen()) { setError("Port is not open"); return {}; }

    std::vector<uint8_t> result;
    result.reserve(64);
    uint8_t byte = 0;

    while (result.size() < maxLen) {
        if (!waitReadable(config_.readTimeout)) break; // exit on timeout

        ssize_t n;
        do { n = ::read(fd_, &byte, 1); } while (n < 0 && errno == EINTR);

        if (n <= 0) break;
        result.push_back(byte);
        if (byte == delimiter) break;
    }
    return result;
}

std::string Serial::readLine(size_t maxLen)
{
    auto vec = readUntil('\n', maxLen);
    return std::string(vec.begin(), vec.end());
}

// ─────────────────────────────────────────────────────────────────────────────
// Buffer & status
// ─────────────────────────────────────────────────────────────────────────────

int Serial::available() const
{
    if (!isOpen()) return 0;
    int bytes = 0;
    ::ioctl(fd_, FIONREAD, &bytes);
    return bytes;
}

void Serial::flushInput()
{
    if (isOpen()) ::tcflush(fd_, TCIFLUSH);
}

void Serial::flushOutput()
{
    if (isOpen()) ::tcdrain(fd_); // wait for all output to be transmitted
}

void Serial::flush()
{
    if (isOpen()) ::tcflush(fd_, TCIOFLUSH);
}

// ─────────────────────────────────────────────────────────────────────────────
// Runtime configuration
// ─────────────────────────────────────────────────────────────────────────────

bool Serial::setConfig(const Config& config)
{
    config_ = config;
    if (!isOpen()) return true; // takes effect on next open()
    return applyConfig();
}

bool Serial::setBaudRate(BaudRate baud)
{
    config_.baudRate = baud;
    if (!isOpen()) return true;
    return applyConfig();
}

bool Serial::setTimeout(int milliseconds)
{
    config_.readTimeout = milliseconds;
    return true; // no need to reapply termios
}

// ─────────────────────────────────────────────────────────────────────────────
// Error handling
// ─────────────────────────────────────────────────────────────────────────────

void Serial::setError(const std::string& msg)
{
    lastError_ = msg;
}

// ─────────────────────────────────────────────────────────────────────────────
// Read thread
// ─────────────────────────────────────────────────────────────────────────────

void Serial::startReadThread(DataCallback onData, ErrorCallback onError, size_t bufSize)
{
    if (running_.load()) return; // already running
    if (!isOpen()) { setError("Port is not open; cannot start read thread"); return; }

    dataCb_      = std::move(onData);
    errorCb_     = std::move(onError);
    readBufSize_ = bufSize;

    if (::pipe(stopPipe_) < 0) {
        setError(std::string("pipe() failed: ") + std::strerror(errno));
        return;
    }

    running_.store(true);
    readThread_ = std::thread(&Serial::readThreadFunc, this);
}

void Serial::stopReadThread()
{
    if (!running_.load()) return;

    running_.store(false);

    // Write one byte to the self-pipe to wake up select()
    if (stopPipe_[1] >= 0) {
        uint8_t dummy = 0;
        [[maybe_unused]] ssize_t r = ::write(stopPipe_[1], &dummy, 1);
    }

    if (readThread_.joinable())
        readThread_.join();

    if (stopPipe_[0] >= 0) { ::close(stopPipe_[0]); stopPipe_[0] = -1; }
    if (stopPipe_[1] >= 0) { ::close(stopPipe_[1]); stopPipe_[1] = -1; }
}

bool Serial::isReadThreadRunning() const
{
    return running_.load();
}

void Serial::readThreadFunc()
{
    std::vector<uint8_t> buf(readBufSize_);

    while (running_.load()) {
        int maxfd = std::max(fd_, stopPipe_[0]) + 1;

        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(fd_, &readfds);
        FD_SET(stopPipe_[0], &readfds);

        // Block indefinitely until fd_ or stopPipe_[0] becomes readable
        int ret = ::select(maxfd, &readfds, nullptr, nullptr, nullptr);
        if (ret < 0) {
            if (errno == EINTR) continue;
            if (errorCb_)
                errorCb_(std::string("select() failed: ") + std::strerror(errno));
            break;
        }

        // stopPipe readable -> shutdown signal received, exit loop
        if (FD_ISSET(stopPipe_[0], &readfds)) break;

        // Serial port readable -> read and invoke callback
        if (FD_ISSET(fd_, &readfds)) {
            ssize_t n;
            do { n = ::read(fd_, buf.data(), buf.size()); }
            while (n < 0 && errno == EINTR);

            if (n < 0) {
                if (errorCb_)
                    errorCb_(std::string("read() failed: ") + std::strerror(errno));
                break;
            }
            if (n > 0 && dataCb_)
                dataCb_(std::vector<uint8_t>(buf.begin(), buf.begin() + n));
        }
    }

    running_.store(false);
}

} // namespace myserial
