#pragma once

#include <memory>

#include <quic/logging/FileQLogger.h>
#include <quic/logging/QLogger.h>

/**
 * Allows adding FileQLogger objects to transport, which will output logs
 * prior to destrution
 */
namespace quic::samples
{
    class HQLoggerHelper : public ::quic::FileQLogger
    {
    public:
        HQLoggerHelper(const std::string& /* path */, bool /* pretty */, quic::VantagePoint);

        ~HQLoggerHelper() override;

    private:
        std::string outputPath_;
        bool pretty_;
    };
}  // namespace quic::samples
