#include "HQServerModule.h"
#include <proxygen/lib/http/session/HQSession.h>
#include "H2Server.h"
#include "HQServer.h"
#include "SampleHandler.h"

using namespace proxygen;

namespace
{
    void sendKnobFrame(HQSession* session, const folly::StringPiece str)
    {
        if (str.empty())
        {
            return;
        }
        uint64_t knobSpace = 0x00000001;
        uint64_t knobId    = 200;
        quic::Buf buf(folly::IOBuf::create(str.size()));
        memcpy(buf->writableData(), str.data(), str.size());
        buf->append(str.size());
        VLOG(10) << "Sending Knob Frame to peer. KnobSpace: " << std::hex << knobSpace
                 << ", KnobId: " << std::dec << knobId << ", Knob Blob: " << str;
        const auto knobSent = session->sendKnob(0x00000001, 200, std::move(buf));
        if (knobSent.hasError())
        {
            LOG(ERROR) << "Failed to send Knob frame to peer. Received error: " << knobSent.error();
        }
    }
}  // namespace

namespace quic::samples
{
    void startServer(const HQToolServerParams& params,
                     std::unique_ptr<quic::QuicTransportStatsCallbackFactory>&& statsFactory)
    {
        // Run H2 server in a separate thread
        Dispatcher dispatcher(
            HandlerParams(params.protocol, params.port, params.httpVersion.canonical));
        auto dispatchFn = [&dispatcher](proxygen::HTTPMessage* request)
        {
            return dispatcher.getRequestHandler(request);
        };
        auto h2server = H2Server::run(params, dispatchFn);

        // Run HQ Server
        std::function<void(HQSession*)> onTransportReadyFn;
        if (params.sendKnobFrame)
        {
            onTransportReadyFn = [](HQSession* session)
            {
                sendKnobFrame(session, ("Hello, World from Server!"));
            };
        }
        HQServer server(params, dispatchFn, std::move(onTransportReadyFn));
        if (statsFactory)
        {
            server.setStatsFactory(std::move(statsFactory));
        }

        server.start();
        // Wait until the quic server initializes
        server.getAddress();
        h2server.join();
        server.stop();
    }
}  // namespace quic::samples
