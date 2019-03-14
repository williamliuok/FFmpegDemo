//
// Created by Administrator on 2019/1/25.
//

#ifndef MVGBUILD_ANDROIDBUF_H
#define MVGBUILD_ANDROIDBUF_H
#include <iostream>
#include <streambuf>
#include <android/log.h>

class AndroidBuf : public std::streambuf {
    enum {
        BUFFER_SIZE = 255,
    };

public:
    AndroidBuf();

    ~AndroidBuf();

protected:
    virtual int_type overflow(int_type c) {
        if (c != EOF) {
            *pptr() = c;
            pbump(1);
        }
        flush_buffer();
        return c;
    }

    virtual int sync() {
        flush_buffer();
        return 0;
    }

private:
    int flush_buffer();

private:
    char buffer_[BUFFER_SIZE + 1];
};


#endif //MVGBUILD_ANDROIDBUF_H
