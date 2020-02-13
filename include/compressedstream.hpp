#ifndef FTP_COMPRESSEDSTREAM_HPP
#define FTP_COMPRESSEDSTREAM_HPP

#include "fdstream.hpp"

template <class Stream, size_t BufMaxSize=((1u<<7u) - 1u)>
class ModeCompressBuf : public std::streambuf {
public:
    explicit ModeCompressBuf(Stream *stream) : stream_(stream) { }

    ~ModeCompressBuf() final = default;

    int dismiss() {
        return dynamic_cast<FDBuf*>(stream_->rdbuf())->dismiss();
    }

    int get_fd() const {
        return dynamic_cast<FDBuf*>(stream_->rdbuf())->get_fd();
    }

protected:
    void write_repeated() {
        if (out_buf_[0] == ' ') {
            unsigned char c = 0xC0u + out_repeat_ + 1;
            *stream_ << c;
            out_repeat_ = 0;
            return;
        }
        unsigned char size = 0x80u + out_repeat_ + 1;
        *stream_ << size << out_buf_[0];
        out_repeat_ = 0;
    }

    void write_symbol(char c) {
        if (out_size_ == 0) {
            out_buf_[out_size_++] = c;
            return;
        }
        if (out_size_ == 1) {
            if (out_buf_[0] == c) {
                ++out_repeat_;
                if (out_repeat_ == (1u << 6u)) {
                    write_repeated();
                }
                return;
            } else if (out_repeat_ != 0) {
                write_repeated();
                out_buf_[0] = c;
                return;
            }
        }
        if (out_size_ == out_buf_.size()) {
            *stream_ << static_cast<unsigned char>(out_size_);
            std::copy(out_buf_.begin(), out_buf_.begin() + out_size_, std::ostreambuf_iterator<char>(*stream_));
            out_size_ = 0;
        }
        out_buf_[out_size_++] = c;
    }

    int overflow(int c) final {
        throw std::runtime_error("Not implemented");
    }

    int sync() final {
        if (out_size_ == 0) {
            *stream_ << '\0' << '\x40';
            stream_->flush();
            return 0;
        }
        if (out_size_ == 1) {
            if (out_repeat_ != 0) {
                write_repeated();
                out_size_ = 0;
                *stream_ << '\0' << '\x40';
                stream_->flush();
                return 0;
            }
        }
        *stream_ << static_cast<unsigned char>(out_size_);
        std::copy(out_buf_.begin(), out_buf_.begin() + out_size_, std::ostreambuf_iterator<char>(*stream_));
        out_size_ = 0;
        *stream_ << '\0' << '\x40';
        stream_->flush();
        return 0;
    }

    std::streamsize xsputn(const char* s, std::streamsize num_) final {
        unsigned long num = num_;
        while (num > 0) {
            write_symbol(*s);
            ++s;
            --num;
        }
        return num_;
    }

    void read_next() {
        int descriptor2 = stream_->get();
        if (descriptor2 == EOF) {
            is_EOF = true;
            in_cur_ = 0;
            in_size_ = 0;
            return;
        }
        auto descriptor = static_cast<unsigned>(static_cast<unsigned char>(static_cast<char>(descriptor2)));
        std::cerr << descriptor << std::endl;
        if (descriptor == 0) {
            stream_->get();
            is_EOF = true;
            in_cur_ = 0;
            in_size_ = 0;
            return;
        }
        in_cur_ = 0;
        in_size_ = 0;
        if ((descriptor & 0x80u) == 0) {
            while (descriptor > 0) {
                int c = stream_->get();
                if (c == EOF) {
                    is_EOF = true;
                    return;
                }
                in_buf_[in_size_++] = c;
                --descriptor;
            }
            return;
        }
        if ((descriptor & 0x40u) == 0) {
            descriptor -= 0x80u;
            int c = stream_->get();
            if (c == EOF) {
                is_EOF = true;
                return;
            }
            while (descriptor > 0) {
                in_buf_[in_size_++] = c;
                --descriptor;
            }
        }
        descriptor -= 0xC0u;
        while (descriptor > 0) {
            in_buf_[in_size_++] = ' ';
            --descriptor;
        }
    }

    std::streamsize xsgetn(char* s, std::streamsize num_) final {
        int num = num_;
        while (num > 0) {
            if (in_cur_ < in_size_) {
                auto can_read = std::min(in_size_ - in_cur_, num);
                if (can_read > 0) {
                    memcpy(s, in_buf_.data() + in_cur_, can_read);
                    s += can_read;
                    num -= can_read;
                    in_cur_ += can_read;
                }
            } else {
                if (is_EOF) {
                    return num_ - num;
                }
                read_next();
            }
        }
        return num_;
    }

    int underflow() final {
        if (in_cur_ != in_size_) {
            return in_buf_[in_cur_];
        }
        if (is_EOF) {
            return EOF;
        }
        read_next();
        if (in_size_ == 0) {
            return EOF;
        }
        return in_buf_[in_cur_];
    }

    int uflow() final {
        if (in_cur_ != in_size_) {
            return in_buf_[in_cur_++];
        }
        if (is_EOF) {
            return EOF;
        }
        read_next();
        if (in_size_ == 0) {
            return EOF;
        }
        return in_buf_[in_cur_++];
    }

    std::streamsize showmanyc() final {
        throw std::runtime_error("Not implemented");
    }

    int pbackfail(int c) final {
        throw std::runtime_error("Not implemented");
    }

private:
    Stream *stream_;
    std::array<char, BufMaxSize> in_buf_{};
    std::array<char, BufMaxSize> out_buf_{};
    int in_cur_ = 0, in_size_ = 0, out_size_ = 0;
    int out_repeat_ = 0;
    bool is_EOF = false;
};

class ModeCompressedOStream : public std::ostream {
protected:
    FDIOStream out_;
    ModeCompressBuf<std::iostream> buf_;
public:
    explicit ModeCompressedOStream(int fd) : std::ostream(nullptr), out_(fd), buf_(&out_) {
        rdbuf(&buf_);
    }

    int dismiss() {
        return buf_.dismiss();
    }

    int get_fd() const {
        return buf_.get_fd();
    }
};

class ModeCompressedIStream : public std::istream {
protected:
    FDIOStream in_;
    ModeCompressBuf<std::iostream> buf_;
public:
    explicit ModeCompressedIStream(int fd) : std::istream(nullptr), in_(fd), buf_(&in_) {
        rdbuf(&buf_);
    }

    int dismiss() {
        return buf_.dismiss();
    }

    int get_fd() const {
        return buf_.get_fd();
    }
};

#endif //FTP_COMPRESSEDSTREAM_HPP
