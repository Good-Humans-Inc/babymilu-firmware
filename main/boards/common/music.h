#ifndef MUSIC_H
#define MUSIC_H

#include <string>

struct AudioChunk {
    uint8_t* data;
    size_t size;

    AudioChunk() : data(nullptr), size(0) {}
    AudioChunk(uint8_t* d, size_t s) : data(d), size(s) {}
};

class Music {
public:
    virtual ~Music() = default;

    virtual bool Download(const std::string& song_name) = 0;
    virtual bool Play() = 0;
    virtual bool Stop() = 0;
    virtual std::string GetDownloadResult() = 0;

    virtual bool StartStreaming(const std::string& music_url) = 0;
    virtual bool StopStreaming() = 0;
    virtual size_t GetBufferSize() const = 0;
    virtual bool IsDownloading() const = 0;
};

#endif
