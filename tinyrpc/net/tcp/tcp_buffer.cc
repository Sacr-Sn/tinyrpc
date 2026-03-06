#include <string.h>
#include <unistd.h>

#include <tinyrpc/comm/log.h>
#include <tinyrpc/net/tcp/tcp_buffer.h>

namespace tinyrpc {

TcpBuffer::TcpBuffer(int size) { buffer.resize(size); }

TcpBuffer::~TcpBuffer() {}

int TcpBuffer::readAble() { return write_index_ - read_index_; }

int TcpBuffer::writeAble() { return buffer.size() - write_index_; }

int TcpBuffer::readIndex() const { return read_index_; }

int TcpBuffer::writeIndex() const { return write_index_; }

/**
 * 调整缓冲区大小
 */
void TcpBuffer::resizeBuffer(int size) {
    std::vector<char> tmp(size);         // 创建新的临时缓冲区
    int c = std::min(size, readAble());  // 计算需要复制的数据量
    memcpy(&tmp[0], &buffer[read_index_], c);
    buffer.swap(tmp);  // 交换缓冲区
    read_index_ = 0;   // 重置读写索引
    write_index_ = read_index_ + c;
}

void TcpBuffer::writeToBuffer(const char* buf, int size) {
    if (size > writeAble()) {
        int new_size = (int)(1.5 * (write_index_ + size));
        resizeBuffer(new_size);
    }
    memcpy(&buffer[write_index_], buf, size);
    write_index_ += size;
}

void TcpBuffer::readFromBuffer(std::vector<char>& re, int size) {
    if (readAble() == 0) {
        DebugLog << "read buffer empty!";
        return;
    }

    int read_size = readAble() > size ? size : readAble();
    std::vector<char> tmp(read_size);
    memcpy(&tmp[0], &buffer[read_index_], read_size);
    re.swap(tmp);
    read_index_ += read_size;
    adjustBuffer();
}

// 整理缓冲区，避免缓冲区前部出现大量已读空间，提高空间利用率
void TcpBuffer::adjustBuffer() {
    // 当读索引超过缓冲区大小的1/3时
    if (read_index_ > static_cast<int>(buffer.size() / 3)) {
        std::vector<char> new_buffer(buffer.size());
        int count = readAble();
        memcpy(&new_buffer[0], &buffer[read_index_], count);
        buffer.swap(new_buffer);
        write_index_ = count;
        read_index_ = 0;
        new_buffer.clear();
    }
}

int TcpBuffer::getSize() { return buffer.size(); }

void TcpBuffer::clearBuffer() {
    buffer.clear();
    read_index_ = 0;
    write_index_ = 0;
}

// 将读索引向后移index个位置
void TcpBuffer::recycleRead(int index) {
    int j = read_index_ + index;
    DebugLog << "index = " << index << ", read_index_ = " << read_index_;
    if (j > (int)buffer.size()) {
        ErrorLog << "recycleRead error";
        return;
    }
    read_index_ = j;
    DebugLog << "read_index_ = " << read_index_;
    adjustBuffer();
}

// 将写索引向后移index个位置
void TcpBuffer::recycleWrite(int index) {
    int j = write_index_ + index;
    if (j > (int)buffer.size()) {
        ErrorLog << "recycleWrite error";
        return;
    }
    write_index_ = j;
    adjustBuffer();
}

std::string TcpBuffer::getBufferString() {
    std::string re(readAble(), '0');
    memcpy(&re[0], &buffer[read_index_], readAble());
    return re;
}

std::vector<char> TcpBuffer::getBufferVector() { return buffer; }
}  // namespace tinyrpc