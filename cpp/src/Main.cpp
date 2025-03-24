#include <folly/init/Init.h>
#include <folly/portability/GFlags.h>

#include "HQCommandLine.h"

using namespace quic::samples;

int main(int argc, char* argv[])
{
    auto startTime = std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::steady_clock().now().time_since_epoch())
                         .count();

#if FOLLY_HAVE_LIBFLAGS
    // Enable glog logging to stderr by default.
    gflags::SetCommandLineOptionWithMode("logtostderr", "1", gflags::SET_FLAGS_DEFAULT);
#endif

    folly::init(&argc, &argv, false);
    int error = 0;

    auto expectedParams = initializeParamsFromCmdline();

    // TODO: not yet implemented
}
