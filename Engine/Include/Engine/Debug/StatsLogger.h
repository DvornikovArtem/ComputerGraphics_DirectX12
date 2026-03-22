#pragma once

#include <string>
#include <vector>
#include <fstream>
#include <mutex>

class StatsLogger
{
public:
    static StatsLogger* GetInstance();

    void RecordFrameTime(float mspf);
    void GenerateReport(const std::wstring& deviceName1, const std::wstring& deviceName2, bool multiGPUMode);
    void Shutdown();
    int GetNumLogs();

private:
    StatsLogger() = default;
    ~StatsLogger() = default;
    StatsLogger(const StatsLogger&) = delete;
    StatsLogger& operator=(const StatsLogger&) = delete;

    static StatsLogger* mInstance;
    std::vector<float> mFrameTimes;
    std::mutex mMutex;
};