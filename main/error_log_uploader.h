#pragma once

#include <esp_err.h>

class ErrorLogUploader {
public:
    static constexpr const char* kErrorLogFile = "/sdcard/err.txt";

    static esp_err_t UploadErrorLog();
    static void EnableErrorLoggingToSD();
    static void DisableErrorLoggingToSD();
};
