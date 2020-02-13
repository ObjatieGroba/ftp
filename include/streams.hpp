#ifndef FTP_STREAMS_HPP
#define FTP_STREAMS_HPP

#include <iostream>
#include <fstream>
#include <sstream>

#include "blockstream.hpp"
#include "compressedstream.hpp"

enum class ModeType {
    Stream,
    Block,
    Compressed
};

#endif //FTP_STREAMS_HPP
