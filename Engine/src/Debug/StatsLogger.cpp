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

void StatsLogger::RecordFrameTime(float mspf)
{
    std::lock_guard<std::mutex> lock(mMutex);
    mFrameTimes.push_back(mspf);
}

void StatsLogger::GenerateReport(const std::wstring& deviceName1, const std::wstring& deviceName2, bool multiGPUMode)
{
    std::lock_guard<std::mutex> lock(mMutex);

    std::ofstream reportFile(multiGPUMode ? "Report_DualGPU.txt" : "Report_SingleGPU.txt");

    if (multiGPUMode) reportFile << "Dual GPU Mode\n";
    else reportFile << "Single GPU Mode\n";

    reportFile << std::string(deviceName1.begin(), deviceName1.end()) << "\n";

    if (multiGPUMode) reportFile << std::string(deviceName2.begin(), deviceName2.end()) << "\n";
    else reportFile << "NONE\n";
    reportFile << mFrameTimes.size() << "\n";
    for (float mspf : mFrameTimes) reportFile << std::fixed << std::setprecision(6) << mspf << "\n";

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
