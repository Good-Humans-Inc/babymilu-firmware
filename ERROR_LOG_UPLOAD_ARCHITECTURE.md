# Error Log Upload Behavior

This document describes the current firmware behavior for `/sdcard/err.txt`.

## Current Code

Primary files:

- `main/error_log_uploader.cc`
- `main/error_log_uploader.h`
- `main/application.cc`
- `main/boards/common/wifi_board.cc`
- `main/boards/echoear/echoear.cc`

## Runtime Flow

1. At startup, after network initialization work begins, `Application` calls
   `ErrorLogUploader::UploadErrorLog()`.
2. The uploader reads `/sdcard/err.txt` if present.
3. The file is uploaded as `multipart/form-data` with:
   - `device_id`: MAC address from `SystemInfo::GetMacAddress()`
   - `timestamp`: UTC ISO-8601 timestamp
   - `file`: `err.txt`
4. The request is sent to the Cloud Run URL defined in
   `ErrorLogUploader::UPLOAD_URL`.
5. On HTTP 200, the local `/sdcard/err.txt` file is deleted.
6. The firmware then enables an ESP log hook so future warnings/errors are
   appended to `/sdcard/err.txt` for the next boot.

## Log Capture

`ErrorLogVprintfHook` preserves normal console logging and also writes warning
and error messages to SD card when:

- error logging is enabled, and
- the SD card is mounted.

The implementation uses a mutex and recursion guard to avoid hook re-entry.

## Size And Upload Format

- Read limit: 100 KB.
- Upload type: `multipart/form-data`.
- Auth header: `X-API-Key`.
- Transfer mode: chunked.

## Current Caveats

- The API key and upload URL are compiled constants.
- Test warning/error messages are emitted after enabling logging, so a fresh
  `err.txt` may contain test entries.
- Upload is best-effort; failure leaves the file for a future attempt.
- The code assumes SD card availability for log persistence.

## Server Expectations

The server should accept a multipart POST containing `device_id`, `timestamp`,
and `file`. A non-200 response is treated as upload failure.
