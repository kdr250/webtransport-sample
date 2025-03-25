#pragma once

#include "HQCommandLine.h"

namespace quic::samples
{
    void startServer(
        const HQToolServerParams& params,
        std::unique_ptr<quic::QuicTransportStatsCallbackFactory>&& statsFactory = nullptr);
}
