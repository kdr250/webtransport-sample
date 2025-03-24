#include "HQServer.h"

#include <quic/common/udpsocket/FollyQuicAsyncUDPSocket.h>
#include <ostream>
#include <string>

#include <folly/io/async/EventBaseLocal.h>
#include <proxygen/lib/http/session/HQDownstreamSession.h>
#include <quic/server/QuicSharedUDPSocketFactory.h>

#include "FizzContext.h"
#include "H1QDownstreamSession.h"
#include "HQLoggerHelper.h"

using fizz::server::FizzServerContext;
using quic::QuicServerTransport;

namespace
{
    using namespace quic::samples;
    using namespace proxygen;

    /**
     * HQSessionController creates new HQSession objects
     *
     * Each HQSessionController object can create only a single session
     * object. TODO: consider changing it.
     *
     * Once the created session object finishes (and detaches), the
     * associated HQController is destroyed. There is no need to
     * explicitly keep track of these objects.
     */
    class HQSessionController :
        public proxygen::HTTPSessionController,
        public proxygen::HTTPSessionBase::InfoCallback
    {
    public:
        using StreamData = std::pair<folly::IOBufQueue, bool>;

        explicit HQSessionController(
            const MyHQServerParams& params,
            const HTTPTransactionHandlerProvider& provider,
            std::function<void(HQSession*)> onTransportReadyFunction = nullptr);

        ~HQSessionController() override = default;

        void onTransportReady(proxygen::HTTPSessionBase* /* session */) override;
        void onTransportReady(const proxygen::HTTPSessionBase&) override {}

        void onDestroy(const proxygen::HTTPSessionBase& /* session */) override;

        proxygen::HTTPTransactionHandler* getRequestHandler(
            proxygen::HTTPTransaction& /* transaction */,
            proxygen::HTTPMessage* message) override;

        proxygen::HTTPTransactionHandler* getParseErrorHandler(
            proxygen::HTTPTransaction* /* transaction */,
            const proxygen::HTTPException& /* error */,
            const folly::SocketAddress& /* localAddress */) override;

        proxygen::HTTPTransactionHandler* getTransactionTimeoutHandler(
            proxygen::HTTPTransaction* /* transaction */,
            const folly::SocketAddress& /* localAddress */) override;

        void attachSession(proxygen::HTTPSessionBase* /* session */) override;

        // The controller instance will be destroyed after this call
        void detachSession(const proxygen::HTTPSessionBase* /* session */) override;

    private:
        // The owning session.
        // NOTE: this must be a plain pointer to avoid circular references
        proxygen::HQSession* session = nullptr;

        // Provider of HTTPTransactionHandler, owned by HQServerTransportFactory
        const HTTPTransactionHandlerProvider& httpTransactionHandlerProvider;
        std::function<void(HQSession*)> onTransportReadyFn;
        uint64_t sessionCount = 0;
    };

    HQSessionController::HQSessionController(
        const MyHQServerParams& params,
        const HTTPTransactionHandlerProvider& provider,
        std::function<void(HQSession*)> onTransportReadyFunction) :
        httpTransactionHandlerProvider(provider), onTransportReadyFn(onTransportReadyFunction)
    {
    }

    void HQSessionController::onTransportReady(HTTPSessionBase* /*session*/)
    {
        if (onTransportReadyFn)
        {
            onTransportReadyFn(session);
        }
    }

    void HQSessionController::onDestroy(const proxygen::HTTPSessionBase&) {}

    proxygen::HTTPTransactionHandler* HQSessionController::getRequestHandler(
        proxygen::HTTPTransaction&,
        proxygen::HTTPMessage* message)
    {
        return httpTransactionHandlerProvider(message);
    }

    HTTPTransactionHandler* FOLLY_NULLABLE
        HQSessionController::getParseErrorHandler(proxygen::HTTPTransaction*,
                                                  const proxygen::HTTPException&,
                                                  const folly::SocketAddress&)
    {
        return nullptr;
    }

    HTTPTransactionHandler* FOLLY_NULLABLE
        HQSessionController::getTransactionTimeoutHandler(proxygen::HTTPTransaction*,
                                                          const folly::SocketAddress&)
    {
        return nullptr;
    }

    void HQSessionController::attachSession(proxygen::HTTPSessionBase*)
    {
        ++sessionCount;
    }

    void HQSessionController::detachSession(const proxygen::HTTPSessionBase*)
    {
        if (--sessionCount == 0)
        {
            delete this;
        }
    }

}  // namespace

namespace quic::samples
{
    HQServer::HQServer(MyHQServerParams hqParams,
                       HTTPTransactionHandlerProvider httpTransactionHandlerProvider,
                       std::function<void(proxygen::HQSession*)> onTransportReadyFn) :
        HQServer(
            std::move(hqParams),
            std::make_unique<HQServerTransportFactory>(params,
                                                       std::move(httpTransactionHandlerProvider),
                                                       std::move(onTransportReadyFn)))
    {
    }

    HQServer::HQServer(MyHQServerParams hqParams,
                       std::unique_ptr<quic::QuicServerTransportFactory> factory) :
        params(std::move(hqParams))
    {
        params.transportSettings.datagramConfig.enabled = true;
        server = quic::QuicServer::createQuicServer(params.transportSettings);

        server->setBindV6Only(false);
        server->setCongestionControllerFactory(
            std::make_shared<ServerCongestionControllerFactory>());

        server->setQuicServerTransportFactory(std::move(factory));
        server->setQuicUDPSocketFactory(std::make_unique<QuicSharedUDPSocketFactory>());
        server->setHealthCheckToken("health");
        server->setSupportedVersion(params.quicVersions);
        server->setFizzContext(createFizzServerContext(params));

        if (params.rateLimitPerThread)
        {
            server->setRateLimit(
                [rateLimitPerThread = params.rateLimitPerThread.value()]()
                {
                    return rateLimitPerThread;
                },
                1s);
        }
    }

    void HQServer::start()
    {
        folly::SocketAddress localAddress;
        if (params.localAddress)
        {
            localAddress = *params.localAddress;
        }
        else
        {
            localAddress.setFromLocalPort(params.port);
        }
        server->start(localAddress, params.serverThreads);
    }

    const folly::SocketAddress HQServer::getAddress() const
    {
        server->waitUntilInitialized();
        const auto& boundAddress = server->getAddress();
        LOG(INFO) << "HQ Server started at: " << boundAddress.describe();
        return boundAddress;
    }

    void HQServer::stop()
    {
        server->shutdown();
    }

    void HQServer::rejectNewConnections(bool reject)
    {
        server->rejectNewConnections(
            [reject]()
            {
                return reject;
            });
    }

    HQServerTransportFactory::HQServerTransportFactory(
        const MyHQServerParams& hqParams,
        HTTPTransactionHandlerProvider provider,
        std::function<void(proxygen::HQSession*)> onTransportReadyFunction) :
        params(hqParams), httpTransactionHandlerProvider(std::move(provider)),
        onTransportReadyFn(std::move(onTransportReadyFunction))
    {
        alpnHandlers[kHQ] = [this](std::shared_ptr<quic::QuicSocket> quicSocket,
                                   wangle::ConnectionManager* connectionManager)
        {
            quicSocket->setConnectionSetupCallback(nullptr);
            return new H1QDownstreamSession(
                std::move(quicSocket),
                new HQSessionController(params, httpTransactionHandlerProvider, onTransportReadyFn),
                connectionManager);
        };
    }
    quic::QuicServerTransport::Ptr HQServerTransportFactory::make(
        folly::EventBase* eventBase,
        std::unique_ptr<quic::FollyAsyncUDPSocketAlias> socket,
        const folly::SocketAddress&,
        quic::QuicVersion quicVersion,
        std::shared_ptr<const fizz::server::FizzServerContext> context) noexcept
    {
        auto transport = quic::QuicHandshakeSocketHolder::makeServerTransport(eventBase,
                                                                              std::move(socket),
                                                                              std::move(context),
                                                                              this);

        if (!params.qLoggerPath.empty())
        {
            transport->setQLogger(std::make_shared<HQLoggerHelper>(params.qLoggerPath,
                                                                   params.prettyJson,
                                                                   quic::VantagePoint::Server));
        }

        return transport;
    }

    void HQServerTransportFactory::onQuicTransportReady(
        std::shared_ptr<quic::QuicSocket> quicSocket)
    {
        auto alpn = quicSocket->getAppProtocol();
        auto iter = alpnHandlers.end();
        if (alpn)
        {
            iter = alpnHandlers.find(*alpn);
        }
        auto quicEventBase          = quicSocket->getEventBase();
        folly::EventBase* eventBase = nullptr;
        if (quicEventBase)
        {
            eventBase =
                quicEventBase->getTypedEventBase<quic::FollyQuicEventBase>()->getBackingEventBase();
        }

        if (iter == alpnHandlers.end())
        {
            // by default, it's HTTP3
            handleHQAlpn(std::move(quicSocket), getConnectionManager(eventBase));
        }
        else
        {
            iter->second(std::move(quicSocket), getConnectionManager(eventBase));
        }
    }

    void HQServerTransportFactory::onConnectionSetupError(
        std::shared_ptr<quic::QuicSocket> quicSocketm,
        quic::QuicError code)
    {
        LOG(ERROR) << "Failed to accept QUIC connection: " << code.message;
    }

    wangle::ConnectionManager* HQServerTransportFactory::getConnectionManager(
        folly::EventBase* eventBase)
    {
        auto connectionManagerPtrPtr      = connectionManager.get(*eventBase);
        wangle::ConnectionManager* result = nullptr;
        if (connectionManagerPtrPtr)
        {
            result = (*connectionManagerPtrPtr).get();
        }
        else
        {
            auto& connectionManagerPtrRef = connectionManager.emplace(
                *eventBase,
                wangle::ConnectionManager::makeUnique(eventBase, params.txnTimeout));
            result = connectionManagerPtrRef.get();
        }
        return result;
    }

    void HQServerTransportFactory::handleHQAlpn(std::shared_ptr<quic::QuicSocket> quicSocket,
                                                wangle::ConnectionManager* connMgr)
    {
        wangle::TransportInfo transportInfo;

        auto controller =
            new HQSessionController(params, httpTransactionHandlerProvider, onTransportReadyFn);

        auto session = new HQDownstreamSession(connMgr->getDefaultTimeout(),
                                               controller,
                                               transportInfo,
                                               controller);

        quicSocket->setConnectionSetupCallback(session);
        quicSocket->setConnectionCallback(session);

        session->setSocket(std::move(quicSocket));
        session->setEgressSettings({
            {proxygen::SettingsId::ENABLE_CONNECT_PROTOCOL, 1},
            {proxygen::SettingsId::_HQ_DATAGRAM_DRAFT_8, 1},
            {proxygen::SettingsId::_HQ_DATAGRAM, 1},
            {proxygen::SettingsId::_HQ_DATAGRAM_RFC, 1},
            {proxygen::SettingsId::ENABLE_WEBTRANSPORT, 1},
        });
        session->startNow();
        session->onTransportReady();
    }
}  // namespace quic::samples
