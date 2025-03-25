#include <folly/init/Init.h>
#include <folly/portability/GFlags.h>

#include "ConnIdLogger.h"
#include "HQCommandLine.h"
#include "HQServerModule.h"

using namespace quic::samples;

int main(int argc, char* argv[])
{
    auto startTime = std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::steady_clock().now().time_since_epoch())
                         .count();

    // #if FOLLY_HAVE_LIBFLAGS
    // Enable glog logging to stderr by default.
    gflags::SetCommandLineOptionWithMode("logtostderr", "1", gflags::SET_FLAGS_DEFAULT);
    // #endif

    folly::init(&argc, &argv, false);
    int result = 0;

    auto expectedParams = initializeParamsFromCmdline();

    if (expectedParams)
    {
        auto& params = expectedParams.value();
        proxygen::ConnIdLogSink sink(params.logdir, params.logprefix);
        if (sink.isValid())
        {
            AddLogSink(&sink);
        }
        else if (!params.logdir.empty())
        {
            LOG(ERROR) << "Cannot open " << params.logdir;
        }

        switch (params.mode)
        {
            case HQMode::SERVER:
                startServer(boost::get<HQToolServerParams>(params.params));
                break;

            case HQMode::CLIENT:
                // TODO: not yet implemented
                break;

            default:
                LOG(ERROR) << "Unknown mode specified...";
                return EXIT_FAILURE;
        }

        if (params.logRuntime)
        {
            auto runTime = std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::steady_clock().now().time_since_epoch())
                               .count()
                           - startTime;
            LOG(INFO) << "Run time: " << runTime << " ms";
        }
        return result;
    }
    else
    {
        for (auto& param : expectedParams.error())
        {
            LOG(ERROR) << "Invalid param: " << param.name << " " << param.value << " "
                       << param.errorMessage;
        }
        return EXIT_FAILURE;
    }
}
