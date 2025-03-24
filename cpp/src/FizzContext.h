#pragma once

#include <fizz/client/FizzClientContext.h>
#include <fizz/server/FizzServerContext.h>
#include <wangle/ssl/SSLContextConfig.h>
#include "HQParams.h"

namespace quic::samples
{

    using FizzServerContextPtr = std::shared_ptr<const fizz::server::FizzServerContext>;

    using FizzClientContextPtr = std::shared_ptr<fizz::client::FizzClientContext>;

    FizzServerContextPtr createFizzServerContext(const MyHQServerParams& params);

    FizzClientContextPtr createFizzClientContext(const MyHQBaseParams& params, bool earlyData);

    wangle::SSLContextConfig createSSLContext(const MyHQBaseParams& params);
}  // namespace quic::samples
