#ifndef BACKGROUND_TASK_H
#define BACKGROUND_TASK_H

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <mutex>
#include <list>
#include <condition_variable>
#include <atomic>
#include <chrono>

class BackgroundTask {
public:
    BackgroundTask(uint32_t stack_size = 4096 * 2, const char* task_name = "background_task");
    ~BackgroundTask();

    void Schedule(std::function<void()> callback);
    void WaitForCompletion();
    bool WaitForCompletion(uint32_t timeout_ms);

private:
    std::mutex mutex_;
    std::list<std::function<void()>> background_tasks_;
    std::condition_variable condition_variable_;
    TaskHandle_t background_task_handle_ = nullptr;
    std::atomic<size_t> active_tasks_{0};
    const char* task_name_ = "background_task";

    void BackgroundTaskLoop();
};

#endif
