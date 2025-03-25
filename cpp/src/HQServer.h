#pragma once

#include <iostream>
#include <string>

#include <folly/io/async/EventBaseLocal.h>
#include <proxygen/lib/http/session/HTTPTransaction.h>
#include <quic/server/QuicHandshakeSocketHolder.h>
#include <quic/server/QuicServer.h>

#include "HQParams.h"

namespace proxygen
{
    class HQSession;
}

namespace quic::samples
{
    using HTTPTransactionHandlerProvider =
        std::function<proxygen::HTTPTransactionHandler*(proxygen::HTTPMessage*)>;

    class HQServer
    {
    public:
        HQServer(MyHQServerParams params,
                 HTTPTransactionHandlerProvider httpTransactionHandlerProvider,
                 std::function<void(proxygen::HQSession*)> onTransportReadyFn = nullptr);

        HQServer(MyHQServerParams params,
                 std::unique_ptr<quic::QuicServerTransportFactory> factory);

        // Starts the QUIC transport in background thread
        void start();

        // Returns the listening address of the server
        // NOTE: can block until the server has started
        const folly::SocketAddress getAddress() const;

        std::vector<folly::EventBase*> getWorkerEventBase() const noexcept
        {
            return server->getWorkerEvbs();
        }

        // Stops both the QUIC transport and the HTTP server handling loop
        void stop();

        // Sets/unsets "reject connections" flag on the QUIC server
        void rejectNewConnections(bool reject);

        void setStatsFactory(
            std::unique_ptr<quic::QuicTransportStatsCallbackFactory>&& statsFactory)
        {
            CHECK(server);
            server->setTransportStatsCallbackFactory(std::move(statsFactory));
        }

    private:
        MyHQServerParams params;
        std::shared_ptr<quic::QuicServer> server;
    };

    class ScopedHQServer
    {
    public:
        static std::unique_ptr<ScopedHQServer> start(const MyHQServerParams& params,
                                                     HTTPTransactionHandlerProvider handlerProvider)
        {
            return std::make_unique<ScopedHQServer>(params, std::move(handlerProvider));
        }

        ScopedHQServer(MyHQServerParams params, HTTPTransactionHandlerProvider handlerProvider) :
            server(std::move(params), std::move(handlerProvider))
        {
            server.start();
        }

        ~ScopedHQServer()
        {
            server.stop();
        }

        [[nodiscard]] const folly::SocketAddress getAddress() const
        {
            return server.getAddress();
        }

    private:
        HQServer server;
    };

    class HQServerTransportFactory :
        public quic::QuicServerTransportFactory,
        private quic::QuicHandshakeSocketHolder::Callback
    {
    public:
        explicit HQServerTransportFactory(
            const MyHQServerParams& params,
            HTTPTransactionHandlerProvider httpTransactionHandlerProvider,
            std::function<void(proxygen::HQSession*)> onTransportReadyFunction);

        ~HQServerTransportFactory() override = default;

        // Create new quic server transport
        quic::QuicServerTransport::Ptr make(
            folly::EventBase* eventBase,
            std::unique_ptr<quic::FollyAsyncUDPSocketAlias> socket,
            const folly::SocketAddress& /* peerAddr */,
            quic::QuicVersion quicVersion,
            std::shared_ptr<const fizz::server::FizzServerContext> context) noexcept override;

        using AlpnHandlerFn =
            std::function<void(std::shared_ptr<quic::QuicSocket>, wangle::ConnectionManager*)>;

        void addAlpnHandler(const std::vector<std::string>& alpns, const AlpnHandlerFn& handler)
        {
            for (auto& alpn : alpns)
            {
                alpnHandlers[alpn] = handler;
            }
        }

    private:
        void onQuicTransportReady(std::shared_ptr<quic::QuicSocket> quicSocket) override;

        void onConnectionSetupError(std::shared_ptr<quic::QuicSocket> quicSocketm,
                                    quic::QuicError code) override;

        wangle::ConnectionManager* getConnectionManager(folly::EventBase* eventBase);

        void handleHQAlpn(std::shared_ptr<quic::QuicSocket> quicSocket,
                          wangle::ConnectionManager* connectionManager);

        // Configuration params
        const MyHQServerParams& params;
        // Provider of HTTPTransactionHandler
        HTTPTransactionHandlerProvider httpTransactionHandlerProvider;
        std::function<void(proxygen::HQSession*)> onTransportReadyFn;
        folly::EventBaseLocal<wangle::ConnectionManager::UniquePtr> connectionManager;
        std::map<std::string, AlpnHandlerFn> alpnHandlers;
    };
}  // namespace quic::samples
