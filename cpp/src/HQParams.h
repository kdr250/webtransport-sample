#pragma once

#include <initializer_list>
#include <map>
#include <string>
#include <vector>

#include <fizz/server/FizzServerContext.h>
#include <folly/Optional.h>
#include <folly/SocketAddress.h>
#include <proxygen/lib/http/HTTPHeaders.h>
#include <proxygen/lib/http/HTTPMethod.h>
#include <proxygen/lib/http/session/HQSession.h>
#include <quic/QuicConstants.h>
#include <quic/fizz/client/handshake/QuicPskCache.h>
#include <quic/state/TransportSettings.h>

namespace quic::samples
{
    struct MyHTTPVersion
    {
        std::string version   = "1.1";
        std::string canonical = "http/1.1";
        uint16_t major        = 1;
        uint16_t minor        = 1;

        bool parse(const std::string& versionString);
    };

    std::ostream& operator<<(std::ostream& outStream, const MyHTTPVersion& version);

    /**
     * Params for both clients and servers
     */
    struct MyHQBaseParams
    {
        // Transport section
        std::string host;
        uint16_t port = 0;
        folly::Optional<folly::SocketAddress> localAddress;
        std::vector<quic::QuicVersion> quicVersions = {
            quic::QuicVersion::MVFST,
            quic::QuicVersion::MVFST_ALIAS,
            quic::QuicVersion::MVFST_EXPERIMENTAL,
            quic::QuicVersion::MVFST_EXPERIMENTAL3,
            quic::QuicVersion::QUIC_V1,
            quic::QuicVersion::QUIC_V1_ALIAS,
            quic::QuicVersion::QUIC_V1_ALIAS2,
        };
        std::vector<std::string> supportedAlpns = {
            proxygen::kH3,
            proxygen::kHQ,
            proxygen::kH3FBCurrentDraft,
            proxygen::kH3AliasV1,
            proxygen::kH3AliasV2,
            proxygen::kH3CurrentDraft,
            proxygen::kHQCurrentDraft,
        };
        quic::TransportSettings transportSettings;
        std::string congestionControlName;
        std::optional<quic::CongestionControlType> congestionControl;
        bool sendKnobFrame = false;

        // HTTP section
        std::string protocol = "h3";
        MyHTTPVersion httpVersion;

        std::chrono::milliseconds txnTimeout = std::chrono::seconds(5);

        // QLogger section
        std::string qLoggerPath;
        bool prettyJson = false;

        // Fizz options
        std::string certificateFilePath;
        std::string keyFilePath;
        std::string pskFilePath;
        std::shared_ptr<quic::QuicPskCache> pskCache;
        fizz::server::ClientAuthMode clientAuth = fizz::server::ClientAuthMode::None;

        // Transport knobs;
        std::string transportKnobs;
    };

    struct MyHQServerParams : public MyHQBaseParams
    {
        size_t serverThreads = 0;
        std::string ccpConfig;
        folly::Optional<int64_t> rateLimitPerThread;
    };

    struct MyHQInvalidParam
    {
        std::string name;
        std::string value;
        std::string errorMessage;
    };

    using HQInvalidParams = std::vector<MyHQInvalidParam>;

}  // namespace quic::samples
