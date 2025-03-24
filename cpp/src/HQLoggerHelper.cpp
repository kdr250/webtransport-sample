#include "HQLoggerHelper.h"

using namespace quic::samples;

HQLoggerHelper::HQLoggerHelper(const std::string& path,
                               bool pretty,
                               quic::VantagePoint vantagePoint) :
    quic::FileQLogger(vantagePoint, quic::kHTTP3ProtocolType, path, pretty, false /* streaming */),
    outputPath_(path), pretty_(pretty)
{
}

HQLoggerHelper::~HQLoggerHelper()
{
    try
    {
        outputLogsToFile(outputPath_, pretty_);
    }
    catch (...)
    {
    }
}