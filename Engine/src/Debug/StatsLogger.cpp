#include "Engine/Debug/StatsLogger.h"

#include <iostream>
#include <sstream>
#include <iomanip>

StatsLogger* StatsLogger::mInstance = nullptr;

StatsLogger* StatsLogger::GetInstance()
{
    if (mInstance == nullptr) mInstance = new StatsLogger();
    return mInstance;
}

void StatsLogger::RecordTotalMspf(float mspf)
{
    std::lock_guard<std::mutex> lock(mMutex);
    mFrameTimes.push_back(mspf);
}

void StatsLogger::RecordPrimaryGPUMspf(float mspf)
{
    std::lock_guard<std::mutex> lock(mMutex);
    mPrimaryGPUMspfs.push_back(mspf);
}

void StatsLogger::GenerateReport(const std::wstring& deviceName1, const std::wstring& deviceName2, bool multiGPUMode, bool swappedDevices, bool CopyTest, int CopyTestType)
{
    std::lock_guard<std::mutex> lock(mMutex);

    std::string FileName = "Report_SingleGPU.txt";
    if (multiGPUMode) FileName = "Report_DualGPU.txt";
    if (swappedDevices) FileName = "Report_DualGPU_SwappedDevices.txt";

    if (CopyTest)
    {
        FileName = "Report_CopyTest_";
        switch (CopyTestType)
        {
        case 0:
            FileName += "ZeroLoad";
            break;
        case 1:
            FileName += "CopyFromLocal";
            break;
        case 2:
            FileName += "CopyFromShared";
            break;
        }
        if (swappedDevices) FileName += "_Swapped";
    }


    std::ofstream reportFile(FileName);

    if (multiGPUMode) reportFile << "Dual GPU Mode\n";
    else reportFile << "Single GPU Mode\n";

    reportFile << "CPU: " + GetCPUName() << "\n";
    reportFile << "Total RAM: " + std::to_string(GetTotalRAMMB()) << " MB" << "\n";

    reportFile << "Primary GPU: " + std::string(deviceName1.begin(), deviceName1.end()) << "\n";
    reportFile << "Secondary GPU: ";
    if (multiGPUMode) reportFile << std::string(deviceName2.begin(), deviceName2.end()) << "\n";
    else reportFile << "NONE\n";

    reportFile << mFrameTimes.size() << "\n";
    for (float mspf : mFrameTimes) reportFile << std::fixed << std::setprecision(6) << mspf << "\n";

    if (multiGPUMode && !CopyTest)
    {
        reportFile << mPrimaryGPUMspfs.size() << "\n";
        for (float mspf : mPrimaryGPUMspfs) reportFile << std::fixed << std::setprecision(6) << mspf << "\n";
    }

    reportFile.close();
}

void StatsLogger::Shutdown()
{
    if (mInstance)
    {
        delete mInstance;
        mInstance = nullptr;
    }
}

int StatsLogger::GetNumLogs()
{
    return mFrameTimes.size();
}

std::string StatsLogger::GetCPUName()
{
    std::string cpuName = "Unknown";

    int cpuInfo[4] = { 0 };
    __cpuid(cpuInfo, 0x80000000);

    if ((unsigned int)cpuInfo[0] >= 0x80000004)
    {
        char brand[49] = { 0 };
        __cpuid(cpuInfo, 0x80000002);
        memcpy(brand, cpuInfo, sizeof(cpuInfo));
        __cpuid(cpuInfo, 0x80000003);
        memcpy(brand + 16, cpuInfo, sizeof(cpuInfo));
        __cpuid(cpuInfo, 0x80000004);
        memcpy(brand + 32, cpuInfo, sizeof(cpuInfo));

        cpuName = std::string(brand);

        // Trim whitespace
        size_t end = cpuName.find_last_not_of(" \t\n\r\f\v");
        if (end != std::string::npos)
            cpuName = cpuName.substr(0, end + 1);
    }

    return cpuName;
}

uint64_t StatsLogger::GetTotalRAMMB()
{
    MEMORYSTATUSEX memoryStatus;
    memoryStatus.dwLength = sizeof(memoryStatus);
    GlobalMemoryStatusEx(&memoryStatus);

    return memoryStatus.ullTotalPhys / (1024 * 1024);
}
