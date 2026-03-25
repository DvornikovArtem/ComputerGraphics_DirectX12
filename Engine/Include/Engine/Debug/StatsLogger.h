#pragma once

#include <string>
#include <vector>
#include <fstream>
#include <mutex>
#include <windows.h>
#include <intrin.h>
#include <comdef.h>
#include <wbemidl.h>
#include <sysinfoapi.h>
#include <psapi.h>

#pragma comment(lib, "wbemuuid.lib")
#pragma comment(lib, "psapi.lib")

class StatsLogger
{
public:
    static StatsLogger* GetInstance();

    void RecordTotalMspf(float mspf);
    void RecordPrimaryGPUMspf(float mspf);
    void GenerateReport(const std::wstring& deviceName1, const std::wstring& deviceName2, bool multiGPUMode, bool swappedDevices, bool CopyTest, int CopyTestType);
    void Shutdown();
    int GetNumLogs();


private:
    StatsLogger() = default;
    ~StatsLogger() = default;
    StatsLogger(const StatsLogger&) = delete;
    StatsLogger& operator=(const StatsLogger&) = delete;

    std::string StatsLogger::GetCPUName();
    uint64_t StatsLogger::GetTotalRAMMB();

    static StatsLogger* mInstance;
    std::vector<float> mFrameTimes;
    std::vector<float> mPrimaryGPUMspfs;
    std::mutex mMutex;
};