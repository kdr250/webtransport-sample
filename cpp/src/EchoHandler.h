#pragma once

#include <folly/Memory.h>
#include <proxygen/httpserver/RequestHandler.h>

namespace proxygen
{
    class ResponseHandler;
}

namespace EchoService
{
    class EchoStatus;

    class EchoHandler : public proxygen::RequestHandler
    {
    public:
        explicit EchoHandler(EchoStatus* echoStatus);

        void onRequest(std::unique_ptr<proxygen::HTTPMessage> request) noexcept override;

        void onBody(std::unique_ptr<folly::IOBuf> body) noexcept override;

        void onEOM() noexcept override;

        void onUpgrade(proxygen::UpgradeProtocol protocol) noexcept override;

        void requestComplete() noexcept override;

        void onError(proxygen::ProxygenError error) noexcept override;

    private:
        EchoStatus* const status = nullptr;
    };
}  // namespace EchoService
