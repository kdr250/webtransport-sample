#include "EchoHandler.h"

#include <folly/portability/GFlags.h>
#include <proxygen/httpserver/RequestHandler.h>
#include <proxygen/httpserver/ResponseBuilder.h>

#include "EchoStatus.h"

using namespace proxygen;

DEFINE_bool(request_number, true, "Include request sequence number in response");

namespace EchoService
{
    EchoHandler::EchoHandler(EchoStatus* echoStatus) : status(echoStatus) {}

    void EchoHandler::onRequest(std::unique_ptr<proxygen::HTTPMessage> request) noexcept
    {
        // you own logic
        status->recordRequest();

        // create response
        ResponseBuilder builder(downstream_);
        builder.status(200, "OK");
        if (FLAGS_request_number)
        {
            builder.header("Request-Number", folly::to<std::string>(status->getRequestCount()));
        }
        request->getHeaders().forEach(
            [&](std::string& name, std::string& value)
            {
                builder.header(folly::to<std::string>("x-echo-", name), value);
            });
        builder.send();
    }

    void EchoHandler::onBody(std::unique_ptr<folly::IOBuf> body) noexcept
    {
        ResponseBuilder(downstream_).body(std::move(body)).send();
    }

    void EchoHandler::onEOM() noexcept
    {
        ResponseBuilder(downstream_).sendWithEOM();
    }

    void EchoHandler::onUpgrade(proxygen::UpgradeProtocol protocol) noexcept
    {
        // EchoHandler doesn't support upgrades
    }

    void EchoHandler::requestComplete() noexcept
    {
        delete this;
    }

    void EchoHandler::onError(proxygen::ProxygenError error) noexcept
    {
        delete this;
    }
}  // namespace EchoService
