#pragma once

#include <proxygen/httpserver/HTTPServerOptions.h>
#include <boost/variant.hpp>

#include "HQParams.h"

namespace quic::samples
{
    enum class HQMode
    {
        INVALID,
        CLIENT,
        SERVER,
    };

    std::ostream& operator<<(std::ostream& outStream, const HQMode& mode);

    struct HQToolClientParams : public HQBaseParams
    {
        std::string outdir;
        bool logResponse;
        bool logResponseHeaders;

        folly::Optional<folly::SocketAddress> remoteAddress;
        bool earlyData;
        std::chrono::milliseconds connectTimeout;
        proxygen::HTTPHeaders httpHeaders;
        std::string httpBody;
        proxygen::HTTPMethod httpMethod;
        std::vector<std::string> httpPaths;
        bool migrateClient = false;
        bool sendRequestsSequentially;
        std::vector<std::string> requestGaps;
    };

    struct HQToolServerParams : public HQBaseParams
    {
        uint16_t h2port;
        folly::Optional<folly::SocketAddress> localH2Address;
        size_t httpServerThreads;
        std::chrono::milliseconds httpServerIdleTimeout;
        std::vector<int> httpServerShutdownOn;
        bool httpServerEnableContentCompression;
        bool h2cEnabled;
    };

    struct HQToolParams
    {
        void setMode(HQMode hqMode)
        {
            mode = hqMode;
            switch (mode)
            {
                case HQMode::CLIENT:
                    params = HQToolClientParams();
                    break;

                case HQMode::SERVER:
                    params = HQToolServerParams();
                    break;

                default:
                    break;
            }
        }

        [[nodiscard]] const HQBaseParams& baseParams() const
        {
            switch (mode)
            {
                case HQMode::CLIENT:
                    return (HQBaseParams&)boost::get<HQToolClientParams>(params);

                case HQMode::SERVER:
                    return (HQBaseParams&)boost::get<HQToolServerParams>(params);

                default:
                    LOG(FATAL) << "Not initialized...";
                    break;
            }
        }

        HQBaseParams& baseParams()
        {
            switch (mode)
            {
                case HQMode::CLIENT:
                    return (HQBaseParams&)boost::get<HQToolClientParams>(params);

                case HQMode::SERVER:
                    return (HQBaseParams&)boost::get<HQToolServerParams>(params);

                default:
                    LOG(FATAL) << "Not initialized...";
                    break;
            }
        }

        HQMode mode = HQMode::INVALID;
        std::string logprefix;
        std::string logdir;
        bool logRuntime;
        boost::variant<HQToolClientParams, HQToolServerParams> params;
    };

    /**
     * A Builder class for HQToolParams that will build HQToolParams from command
     * line parameters processed by GFlag.
     */
    class HQToolParamsBuilderFromCmdline
    {
    public:
        using value_type       = std::map<std::string, std::string>::value_type;
        using initializer_list = std::initializer_list<value_type>;

        explicit HQToolParamsBuilderFromCmdline(initializer_list initializerList);

        [[nodiscard]] bool valid() const noexcept;

        explicit operator bool() const noexcept
        {
            return valid();
        }

        [[nodiscard]] const HQInvalidParams& invalidParams() const noexcept;

        HQToolParams build() noexcept;

    private:
        HQInvalidParams invalidParams_;
        HQToolParams hqParams;
    };

    // Initialized the parameters from the cmdline flags
    const folly::Expected<HQToolParams, HQInvalidParams> initializeParamsFromCmdline(
        HQToolParamsBuilderFromCmdline::initializer_list initial = {});

    // Output convenience
    std::ostream& operator<<(std::ostream& outStream, HQToolParams& hqParams);

}  // namespace quic::samples
