#ifndef FTP_BLOCKSTREAM_HPP
#define FTP_BLOCKSTREAM_HPP

#include "fdstream.hpp"

template <class Stream, size_t BufMaxSize=((1u<<16u) - 1u)>
class ModeBlockBuf : public std::streambuf {
public:
    explicit ModeBlockBuf(Stream *stream) : stream_(stream) { }

    ~ModeBlockBuf() final = default;

    int dismiss() {
        return dynamic_cast<FDBuf*>(stream_->rdbuf())->dismiss();
    }

    int get_fd() const {
        return dynamic_cast<FDBuf*>(stream_->rdbuf())->get_fd();
    }

protected:
    void write_block(char descriptor = 64) {
        *stream_ << descriptor;
        *stream_ << static_cast<char>(static_cast<unsigned>(out_size_) >> 8u);
        *stream_ << static_cast<char>(out_size_ % 0xFFu);
        if (out_size_ != 0) {
            std::copy(out_buf_.begin(), out_buf_.begin() + out_size_, std::ostreambuf_iterator<char>(*stream_));
        }
        out_size_ = 0;
        stream_->flush();
    }

    int overflow (int c) final {
        throw std::runtime_error("Not implemented");
    }

    int sync() final {
        write_block();
        return 0;
    }

    std::streamsize xsputn(const char* s, std::streamsize num_) final {
        unsigned long num = num_;
        while (num > 0) {
            auto can_write = std::min(out_buf_.size() - out_size_, num);
            if (can_write > 0) {
                memcpy(out_buf_.data() + out_size_, s, can_write);
                s += can_write;
                num -= can_write;
                out_size_ += can_write;
            }
            if (out_size_ == out_buf_.size()) {
                write_block(0);
            }
        }
        return num_;
    }

    void read_block() {
        int descriptor = stream_->get();
        if (descriptor == EOF) {
            is_EOF = true;
            in_cur_ = 0;
            in_size_ = 0;
            return;
        }
        unsigned char s1, s2;
        s1 = stream_->get();
        s2 = stream_->get();
        if (descriptor != 0) {
            is_EOF = true;
        }
        unsigned size = (static_cast<unsigned>(s1) << 8u) + s2;
        in_cur_ = 0;
        in_size_ = 0;
        while (size != 0) {
            int c = stream_->get();
            if (c == EOF) {
                is_EOF = true;
                return;
            }
            in_buf_[in_size_++] = c;
            --size;
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
                read_block();
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
        read_block();
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
        read_block();
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
    bool is_EOF = false;
};

class ModeBlockOStream : public std::ostream {
protected:
    FDIOStream out_;
    ModeBlockBuf<std::iostream> buf_;
public:
    explicit ModeBlockOStream(int fd) : std::ostream(nullptr), out_(fd), buf_(&out_) {
        rdbuf(&buf_);
    }

    int dismiss() {
        return buf_.dismiss();
    }

    int get_fd() const {
        return buf_.get_fd();
    }
};

class ModeBlockIStream : public std::istream {
protected:
    FDIOStream in_;
    ModeBlockBuf<std::iostream> buf_;
public:
    explicit ModeBlockIStream(int fd) : std::istream(nullptr), in_(fd), buf_(&in_) {
        rdbuf(&buf_);
    }

    int dismiss() {
        return buf_.dismiss();
    }

    int get_fd() const {
        return buf_.get_fd();
    }
};


#endif //FTP_BLOCKSTREAM_HPP
