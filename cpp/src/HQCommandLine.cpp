#include "HQCommandLine.h"

#include <folly/io/async/EventBaseManager.h>
#include <folly/portability/GFlags.h>
#include <proxygen/lib/http/SynchronizedLruQuicPskCache.h>
#include <proxygen/lib/transport/PersistentQuicPskCache.h>
#include <quic/QuicConstants.h>

#include "CurlClient.h"

DEFINE_string(host, "127.0.0.1", "HQ server hostname/IP");
DEFINE_int32(port, 6666, "HQ server port");
DEFINE_int32(threads, 1, "QUIC Server threads, 0 = nCPUs");
DEFINE_int32(h2port, 6667, "HTTP/2 server port");
DEFINE_string(local_address, "", "Local Address to bind to. Client only. Format should be ip:port");
DEFINE_string(mode, "server", "Mode to run in: 'client' or 'server'");
DEFINE_string(body, "", "Filename to read from for POST requests");
DEFINE_string(path,
              "/",
              "(HQClient) url-path to send the request to, "
              "or a comma separated list of paths to fetch in parallel");
DEFINE_int32(num_requests, 1, "How many requests to issue to the URL(s) specified in <path>");
DEFINE_string(connect_to_address,
              "",
              "(HQClient) Override IP address to connect to instead of "
              "resolving the host field");
DEFINE_int32(connect_timeout, 2000, "(HQClient) connect timeout in ms");
DEFINE_string(httpversion, "1.1", "HTTP version string");
DEFINE_string(protocol, "", "HQ protocol version e.g. h3-29 or hq-fb-05");
DEFINE_int64(quic_version, 0, "QUIC version to use. 0 is default");
DEFINE_bool(use_version, true, "Use set QUIC version as first version");
// DEFINE_string(logdir, "/tmp/logs", "Directory to store connection logs");
DEFINE_string(logdir, "./", "Directory to store connection logs");
DEFINE_string(outdir, "", "Directory to store responses");
DEFINE_bool(log_response, true, "Whether to log the response content to stderr");
DEFINE_bool(log_response_headers, false, "Whether to log the response headers to stderr");
DEFINE_bool(log_run_time,
            false,
            "Whether to log the duration for which the client/server was running");
DEFINE_bool(sequential,
            false,
            "Whether to make requests sequentially or in parallel when "
            "multiple paths are provided");
DEFINE_string(gap_ms, "0", "Comma separated list of gaps in ms between requests");
DEFINE_string(congestion, "cubic", "newreno/cubic/bbr/none");
DEFINE_int32(conn_flow_control, 1024 * 1024 * 10, "Connection flow control");
DEFINE_int32(stream_flow_control, 256 * 1024, "Stream flow control");
DEFINE_int32(max_receive_packet_size,
             quic::kDefaultUDPReadBufferSize,
             "Max UDP packet size Quic can receive");
DEFINE_int64(rate_limit, -1, "Connection rate limit per second per thread");

DEFINE_uint32(num_gro_buffers, quic::kDefaultNumGROBuffers, "Number of GRO buffers");

DEFINE_int32(txn_timeout, 120000, "HTTP Transaction Timeout");
DEFINE_string(headers, "", "List of N=V headers separated by ,");
DEFINE_bool(pacing, false, "Whether to enable pacing on HQServer");
DEFINE_int32(pacing_timer_tick_interval_us, 200, "Pacing timer resolution");
DEFINE_string(psk_file, "", "Cache file to use for QUIC psks");
DEFINE_bool(early_data, false, "Whether to use 0-rtt");
DEFINE_uint32(quic_batching_mode,
              static_cast<uint32_t>(quic::QuicBatchingMode::BATCHING_MODE_NONE),
              "QUIC batching mode");
DEFINE_uint32(quic_batch_size,
              quic::kDefaultQuicMaxBatchSize,
              "Maximum number of packets that can be batched in Quic");
DEFINE_string(cert, "", "Certificate file path");
DEFINE_string(key, "", "Private key file path");
DEFINE_string(client_auth_mode, "none", "Client authentication mode");
DEFINE_string(qlogger_path,
              "",
              "Path to the directory where qlog files"
              "will be written. File is called <CID>.qlog");
DEFINE_bool(pretty_json, true, "Whether to use pretty json for QLogger output");
DEFINE_bool(connect_udp, false, "Whether or not to use connected udp sockets");
DEFINE_uint32(max_cwnd_mss, quic::kLargeMaxCwndInMss, "Max cwnd in unit of mss");
DEFINE_bool(migrate_client,
            false,
            "(HQClient) Should the HQClient make two sets of requests and "
            "switch sockets in the middle.");
DEFINE_bool(use_inplace_write, false, "Transport use inplace packet build and socket writing");

DEFINE_bool(send_knob_frame,
            false,
            "Send a Knob Frame to the peer when a QUIC connection is "
            "established successfully");

DEFINE_string(transport_knobs,
              "",
              "If send_knob_frame is set, this is the default transport knobs"
              " sent to peer");
DEFINE_bool(use_ack_receive_timestamps,
            false,
            "Replace the ACK frame with ACK_RECEIVE_TIMESTAMPS frame"
            "which carries the received packet timestamps");
DEFINE_uint32(max_ack_receive_timestamps_to_send,
              quic::kMaxReceivedPktsTimestampsStored,
              "Controls how many packet receieve timestamps the peer should send");
DEFINE_uint32(advertise_extended_ack_features,
              0,
              "Advertise ACK_EXTENDED frame support to the peer. The following"
              "bitwise values can be ORed together:"
              "bit 1 - ECN support"
              "bit 2 - Receive timestamps support"
              "Example: 3 means both ECN and receive timestamps are supported");
DEFINE_uint32(enable_extended_ack_features,
              0,
              "Replace the ACK frame with ACK_EXTENDED when supported by the "
              "peer. The following bitwise values can be ORed together:"
              "bit 1 - ECN support"
              "bit 2 - Receive timestamps support"
              "Example: 3 means both ECN and receive timestamps are supported");
DEFINE_bool(initiate_key_updates, false, "Whether to initiate periodic key updates");
DEFINE_uint32(key_update_interval,
              quic::kDefaultKeyUpdatePacketCountInterval,
              "Number of packets to be sent before initiating a key update (if "
              "initiate_key_updates is true)");
DEFINE_bool(writer_backpressure,
            false,
            "Enable backpressure in the batch writer. Only for non-batched writer");
DEFINE_bool(use_l4s_ecn, false, "Whether to use L4S for ECN marking");
DEFINE_bool(read_ecn, false, "Whether to read and echo ecn marking from ingress packets");
DEFINE_uint32(dscp, 0, "DSCP value to use for outgoing packets");

namespace quic::samples
{
    std::ostream& operator<<(std::ostream& outStream, const HQMode& mode)
    {
        outStream << "mode=";
        switch (mode)
        {
            case HQMode::CLIENT:
                outStream << "client";
                break;

            case HQMode::SERVER:
                outStream << "server";
                break;

            default:
                outStream << "unknown (value =" << static_cast<uint32_t>(mode) << ")";
                break;
        }
        return outStream;
    }

    void initializeCommonSettings(HQToolParams& hqParams);
    void initializeTransportSettings(HQToolParams& hqParams);

    void initializeHttpClientSettings(HQToolClientParams& hqParams);
    void initializeHttpServerSettings(HQToolServerParams& hqParams);

    void initializeQLogSettings(MyHQBaseParams& hqParams);
    void initializeFizzSettings(MyHQBaseParams& hqParams);

    HQInvalidParams validate(const HQToolParams& params);

    HQToolParamsBuilderFromCmdline::HQToolParamsBuilderFromCmdline(initializer_list initializerList)
    {
        // Save the values of the flags, so that changing flags values is safe
        gflags::FlagSaver saver;

        for (auto& [key, value] : initializerList)
        {
            LOG(INFO) << "Overriding HQToolParams " << key << " to " << value;
            gflags::SetCommandLineOptionWithMode(key.c_str(),
                                                 value.c_str(),
                                                 gflags::FlagSettingMode::SET_FLAGS_VALUE);
        }

        hqParams.logdir     = FLAGS_logdir;
        hqParams.logRuntime = FLAGS_log_run_time;

        initializeCommonSettings(hqParams);
        initializeTransportSettings(hqParams);

        switch (hqParams.mode)
        {
            case HQMode::CLIENT:
                initializeHttpClientSettings(boost::get<HQToolClientParams>(hqParams.params));
                break;

            case HQMode::SERVER:
                initializeHttpServerSettings(boost::get<HQToolServerParams>(hqParams.params));
                break;

            default:
                break;
        }

        initializeQLogSettings(hqParams.baseParams());
        initializeFizzSettings(hqParams.baseParams());

        for (auto& error : validate(hqParams))
        {
            invalidParams_.push_back(error);
        }
    }

    bool HQToolParamsBuilderFromCmdline::valid() const noexcept
    {
        return invalidParams_.empty();
    }

    const HQInvalidParams& HQToolParamsBuilderFromCmdline::invalidParams() const noexcept
    {
        return invalidParams_;
    }

    HQToolParams HQToolParamsBuilderFromCmdline::build() noexcept
    {
        return hqParams;
    }

    /*
    * Initiazliation and validation functions.
    */
    void initializeCommonSettings(HQToolParams& hqParams)
    {
        // General section
        if (FLAGS_mode == "server")
        {
            CHECK(FLAGS_local_address.empty()) << "local_address only allowed in client mode";
            hqParams.setMode(HQMode::SERVER);
            hqParams.logprefix             = "server";
            auto& serverParams             = boost::get<HQToolServerParams>(hqParams.params);
            serverParams.host              = FLAGS_host;
            serverParams.port              = FLAGS_port;
            serverParams.httpServerThreads = FLAGS_threads;
            serverParams.localAddress =
                folly::SocketAddress(serverParams.host, serverParams.port, true);
        }
        else if (FLAGS_mode == "client")
        {
            hqParams.setMode(HQMode::CLIENT);
            hqParams.logprefix = "client";
            auto& clientParams = boost::get<HQToolClientParams>(hqParams.params);
            clientParams.host  = FLAGS_host;
            clientParams.port  = FLAGS_port;
            if (FLAGS_connect_to_address.empty())
            {
                clientParams.remoteAddress =
                    folly::SocketAddress(clientParams.host, clientParams.port, true);
            }
            else
            {
                clientParams.remoteAddress =
                    folly::SocketAddress(FLAGS_connect_to_address, clientParams.port, false);
            }
            if (!FLAGS_local_address.empty())
            {
                clientParams.localAddress = folly::SocketAddress();
                clientParams.localAddress->setFromLocalIpPort(FLAGS_local_address);
            }
            clientParams.outdir = FLAGS_outdir;
        }
    }

    void initializeTransportSettings(HQToolParams& hqParams)
    {
        MyHQBaseParams& baseParams = hqParams.baseParams();
        if (FLAGS_quic_version != 0)
        {
            auto quicVersion     = static_cast<quic::QuicVersion>(FLAGS_quic_version);
            bool useVersionFirst = FLAGS_use_version;
            if (useVersionFirst)
            {
                baseParams.quicVersions.insert(baseParams.quicVersions.begin(), quicVersion);
            }
            else
            {
                baseParams.quicVersions.push_back(quicVersion);
            }
        }

        if (!FLAGS_protocol.empty())
        {
            baseParams.protocol       = FLAGS_protocol;
            baseParams.supportedAlpns = {baseParams.protocol};
        }

        baseParams.transportSettings.advertisedInitialConnectionFlowControlWindow =
            FLAGS_conn_flow_control;
        baseParams.transportSettings.advertisedInitialBidiLocalStreamFlowControlWindow =
            FLAGS_stream_flow_control;
        baseParams.transportSettings.advertisedInitialBidiRemoteStreamFlowControlWindow =
            FLAGS_stream_flow_control;
        baseParams.transportSettings.advertisedInitialUniStreamFlowControlWindow =
            FLAGS_stream_flow_control;
        baseParams.congestionControlName = FLAGS_congestion;
        baseParams.congestionControl     = quic::congestionControlStrToType(FLAGS_congestion);
        if (baseParams.congestionControl)
        {
            baseParams.transportSettings.defaultCongestionController =
                baseParams.congestionControl.value();
        }
        baseParams.transportSettings.maxRecvPacketSize = FLAGS_max_receive_packet_size;
        baseParams.transportSettings.numGROBuffers_    = FLAGS_num_gro_buffers;
        baseParams.transportSettings.pacingEnabled     = FLAGS_pacing;
        if (baseParams.transportSettings.pacingEnabled)
        {
            baseParams.transportSettings.pacingTickInterval =
                std::chrono::microseconds(FLAGS_pacing_timer_tick_interval_us);
        }
        baseParams.transportSettings.batchingMode =
            quic::getQuicBatchingMode(FLAGS_quic_batching_mode);
        baseParams.transportSettings.maxBatchSize             = FLAGS_quic_batch_size;
        baseParams.transportSettings.enableWriterBackpressure = FLAGS_writer_backpressure;
        if (hqParams.mode == HQMode::CLIENT)
        {
            // There is no good reason to keep the socket around for a drain period
            // for a commandline client
            baseParams.transportSettings.shouldDrain      = false;
            baseParams.transportSettings.attemptEarlyData = FLAGS_early_data;
        }
        baseParams.transportSettings.connectUDP   = FLAGS_connect_udp;
        baseParams.transportSettings.maxCwndInMss = FLAGS_max_cwnd_mss;
        if (hqParams.mode == HQMode::SERVER && FLAGS_use_inplace_write)
        {
            baseParams.transportSettings.dataPathType = quic::DataPathType::ContinuousMemory;
        }
        if (FLAGS_rate_limit >= 0)
        {
            CHECK(hqParams.mode == HQMode::SERVER);
            std::array<uint8_t, kRetryTokenSecretLength> secret;
            folly::Random::secureRandom(secret.data(), secret.size());
            baseParams.transportSettings.retryTokenSecret = secret;
        }
        if (hqParams.mode == HQMode::CLIENT)
        {
            boost::get<HQToolClientParams>(hqParams.params).connectTimeout =
                std::chrono::milliseconds(FLAGS_connect_timeout);
        }
        baseParams.sendKnobFrame = FLAGS_send_knob_frame;
        if (baseParams.sendKnobFrame)
        {
            baseParams.transportSettings.knobs.push_back({kDefaultQuicTransportKnobSpace,
                                                          kDefaultQuicTransportKnobId,
                                                          FLAGS_transport_knobs});
        }
        baseParams.transportSettings.maxRecvBatchSize                = 32;
        baseParams.transportSettings.shouldUseRecvmmsgForBatchRecv   = true;
        baseParams.transportSettings.advertisedInitialMaxStreamsBidi = 100;
        baseParams.transportSettings.advertisedInitialMaxStreamsUni  = 100;

        if (FLAGS_use_ack_receive_timestamps)
        {
            baseParams.transportSettings.maybeAckReceiveTimestampsConfigSentToPeer.assign(
                {.maxReceiveTimestampsPerAck = FLAGS_max_ack_receive_timestamps_to_send,
                 .receiveTimestampsExponent  = kDefaultReceiveTimestampsExponent});
        }
        baseParams.transportSettings.datagramConfig.enabled = true;

        baseParams.transportSettings.initiateKeyUpdate            = FLAGS_initiate_key_updates;
        baseParams.transportSettings.keyUpdatePacketCountInterval = FLAGS_key_update_interval;

        if (FLAGS_use_l4s_ecn)
        {
            baseParams.transportSettings.enableEcnOnEgress                     = true;
            baseParams.transportSettings.useL4sEcn                             = true;
            baseParams.transportSettings.minBurstPackets                       = 1;
            baseParams.transportSettings.experimentalPacer                     = true;
            baseParams.transportSettings.ccaConfig.onlyGrowCwndWhenLimited     = true;
            baseParams.transportSettings.ccaConfig.leaveHeadroomForCwndLimited = true;
        }

        baseParams.transportSettings.readEcnOnIngress = FLAGS_read_ecn;

        baseParams.transportSettings.dscpValue        = FLAGS_dscp;
        baseParams.transportSettings.disableMigration = false;

        baseParams.transportSettings.advertisedExtendedAckFeatures =
            FLAGS_advertise_extended_ack_features;
        baseParams.transportSettings.enableExtendedAckFeatures = FLAGS_enable_extended_ack_features;
    }

    void initializeHttpServerSettings(HQToolServerParams& hqParams)
    {
        // HTTP section
        // NOTE: handler factories are assigned by H2Server class
        // before starting.
        hqParams.h2port                = FLAGS_h2port;
        hqParams.localH2Address        = folly::SocketAddress(hqParams.host, hqParams.h2port, true);
        hqParams.httpServerThreads     = FLAGS_threads;
        hqParams.httpServerIdleTimeout = std::chrono::milliseconds(60000);
        hqParams.httpServerShutdownOn  = {SIGINT, SIGTERM};
        hqParams.httpServerEnableContentCompression = false;
        hqParams.h2cEnabled                         = false;
        hqParams.httpVersion.parse(FLAGS_httpversion);
        hqParams.txnTimeout = std::chrono::milliseconds(FLAGS_txn_timeout);
    }  // initializeHttpServerSettings

    void initializeHttpClientSettings(HQToolClientParams& hqParams)
    {
        folly::split(',', FLAGS_path, hqParams.httpPaths);

        if (FLAGS_num_requests > 1)
        {
            std::vector<std::string> multipliedPaths;
            multipliedPaths.reserve(hqParams.httpPaths.size() * FLAGS_num_requests);

            for (int i = 0; i < FLAGS_num_requests; i++)
            {
                std::copy(hqParams.httpPaths.begin(),
                          hqParams.httpPaths.end(),
                          std::back_inserter(multipliedPaths));
            }
            hqParams.httpPaths.swap(multipliedPaths);
        }

        hqParams.httpBody = FLAGS_body;
        hqParams.httpMethod =
            hqParams.httpBody.empty() ? proxygen::HTTPMethod::GET : proxygen::HTTPMethod::POST;

        // parse HTTP headers
        auto httpHeadersString = FLAGS_headers;
        hqParams.httpHeaders   = CurlService::CurlClient::parseHeaders(httpHeadersString);

        // Set the host header
        if (!hqParams.httpHeaders.exists(proxygen::HTTP_HEADER_HOST))
        {
            hqParams.httpHeaders.set(proxygen::HTTP_HEADER_HOST, hqParams.host);
        }

        hqParams.logResponse              = FLAGS_log_response;
        hqParams.logResponseHeaders       = FLAGS_log_response_headers;
        hqParams.sendRequestsSequentially = FLAGS_sequential;
        folly::split(',', FLAGS_gap_ms, hqParams.requestGaps);

        hqParams.earlyData     = FLAGS_early_data;
        hqParams.migrateClient = FLAGS_migrate_client;
        hqParams.txnTimeout    = std::chrono::milliseconds(FLAGS_txn_timeout);
        hqParams.httpVersion.parse(FLAGS_httpversion);
    }  // initializeHttpClientSettings

    void initializeQLogSettings(MyHQBaseParams& hqParams)
    {
        hqParams.qLoggerPath = FLAGS_qlogger_path;
        hqParams.prettyJson  = FLAGS_pretty_json;
    }

    void initializeFizzSettings(MyHQBaseParams& hqParams)
    {
        hqParams.certificateFilePath = FLAGS_cert;
        hqParams.keyFilePath         = FLAGS_key;
        hqParams.pskFilePath         = FLAGS_psk_file;
        if (!FLAGS_psk_file.empty())
        {
            hqParams.pskCache = std::make_shared<proxygen::PersistentQuicPskCache>(
                FLAGS_psk_file,
                wangle::PersistentCacheConfig::Builder()
                    .setCapacity(1000)
                    .setSyncInterval(std::chrono::seconds(1))
                    .build());
        }
        else
        {
            hqParams.pskCache = std::make_shared<proxygen::SynchronizedLruQuicPskCache>(1000);
        }

        if (FLAGS_client_auth_mode == "none")
        {
            hqParams.clientAuth = fizz::server::ClientAuthMode::None;
        }
        else if (FLAGS_client_auth_mode == "optional")
        {
            hqParams.clientAuth = fizz::server::ClientAuthMode::Optional;
        }
        else if (FLAGS_client_auth_mode == "required")
        {
            hqParams.clientAuth = fizz::server::ClientAuthMode::Required;
        }
    }

    HQInvalidParams validate(const HQToolParams& params)
    {
        HQInvalidParams invalidParams;
#define INVALID_PARAM(param, error)                                                        \
    do                                                                                     \
    {                                                                                      \
        MyHQInvalidParam invalid = {.name         = #param,                                \
                                    .value        = folly::to<std::string>(FLAGS_##param), \
                                    .errorMessage = (error)};                              \
        invalidParams.push_back(invalid);                                                  \
    } while (false);

        // Validate the common settings
        if (!(params.mode == HQMode::CLIENT || params.mode == HQMode::SERVER))
        {
            INVALID_PARAM(mode, "only client/server are supported");
        }

        // In the client mode, host/port are required
        if (params.mode == HQMode::CLIENT)
        {
            auto& clientParams = boost::get<HQToolClientParams>(params.params);
            if (clientParams.host.empty())
            {
                INVALID_PARAM(host, "HQClient expected --host");
            }
            if (clientParams.port == 0)
            {
                INVALID_PARAM(port, "HQClient expected --port");
            }
        }

        // Validate the transport section
        if (folly::to<uint16_t>(FLAGS_max_receive_packet_size) < quic::kDefaultUDPSendPacketLen)
        {
            INVALID_PARAM(max_receive_packet_size,
                          folly::to<std::string>("max_receive_packet_size needs to be at least ",
                                                 quic::kDefaultUDPSendPacketLen));
        }

        auto& baseParams = params.baseParams();
        if (!baseParams.congestionControlName.empty())
        {
            if (!baseParams.congestionControl)
            {
                INVALID_PARAM(congestion, "unrecognized congestion control");
            }
        }
        // Validate the HTTP section
        if (params.mode == HQMode::SERVER)
        {
        }

        return invalidParams;
#undef INVALID_PARAM
    }

    const folly::Expected<HQToolParams, HQInvalidParams> initializeParamsFromCmdline(
        HQToolParamsBuilderFromCmdline::initializer_list initial)
    {
        auto builder = std::make_shared<HQToolParamsBuilderFromCmdline>(initial);
        if (builder->valid())
        {
            return builder->build();
        }
        else
        {
            auto errors = builder->invalidParams();
            return folly::makeUnexpected(errors);
        }
    }

}  // namespace quic::samples
