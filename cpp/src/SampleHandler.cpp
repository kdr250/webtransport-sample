#include "SampleHandler.h"
#include <proxygen/lib/utils/Logging.h>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/split.hpp>
#include <string>

namespace quic::samples
{
    proxygen::HTTPTransactionHandler* Dispatcher::getRequestHandler(proxygen::HTTPMessage* message)
    {
        DCHECK(message);
        auto path = message->getPathAsStringPiece();
        LOG(INFO) << "getRequestHandler! path=" << path;
        if (path == "/" || path == "/echo")
        {
            return new EchoHandler(params);
        }
        if (boost::algorithm::starts_with(path, "/webtransport/devious-baton"))
        {
            return new DeviousBatonHandler(params, folly::EventBaseManager::get()->getEventBase());
        }

        return new DummyHandler(params);
    }

    void DeviousBatonHandler::onHeadersComplete(
        std::unique_ptr<proxygen::HTTPMessage> message) noexcept
    {
        VLOG(10) << "WebtransportHandler::" << __func__;
        message->dumpMessage(2);

        if (message->getMethod() != proxygen::HTTPMethod::CONNECT)
        {
            LOG(ERROR) << "Method not supported! method=" << message->getMethodString();
            proxygen::HTTPMessage response;
            response.setVersionString(getHttpVersion());
            response.setStatusCode(400);
            response.setStatusMessage("ERROR");
            response.setWantsKeepalive(false);

            transaction->sendHeaders(response);
            transaction->sendEOM();
            transaction = nullptr;
            return;
        }

        VLOG(2) << "Received CONNECT request for " << message->getPathAsStringPiece() << " at: "
                << std::chrono::duration_cast<std::chrono::microseconds>(
                       std::chrono::steady_clock::now().time_since_epoch())
                       .count();

        auto status       = 500;
        auto webTransport = transaction->getWebTransport();
        if (webTransport)
        {
            devious.emplace(webTransport,
                            devious::DeviousBaton::Mode::SERVER,
                            [this](proxygen::WebTransport::StreamReadHandle* readHandle)
                            {
                                readHandle->awaitNextRead(eventBase,
                                                          [this](auto readHandle, auto streamData)
                                                          {
                                                              readHandler(readHandle,
                                                                          std::move(streamData));
                                                          });
                            });

            auto responseCode = devious->onRequest(*message);
            if (responseCode)
            {
                status = 200;
            }
            else
            {
                status = responseCode.error();
            }
        }

        // Send the response to the original get request
        proxygen::HTTPMessage response;
        response.setVersionString(getHttpVersion());
        response.setStatusCode(status);
        response.setIsChunked(true);

        if (status / 100 == 2)
        {
            response.getHeaders().add("sec-webtransport-http3-draft", "draft02");
            response.setWantsKeepalive(true);
        }
        else
        {
            response.setWantsKeepalive(false);
            devious.reset();
        }
        response.dumpMessage(4);
        transaction->sendHeaders(response);

        if (devious)
        {
            devious->start();
        }
        else
        {
            transaction->sendEOM();
            transaction = nullptr;
        }
    }

    void DeviousBatonHandler::onWebTransportBidiStream(
        proxygen::HTTPCodec::StreamID id,
        proxygen::WebTransport::BidiStreamHandle stream) noexcept
    {
        VLOG(4) << "Neew Bidi Stream=" << id;
        stream.readHandle->awaitNextRead(eventBase,
                                         [this](auto readHandle, auto streamData)
                                         {
                                             readHandler(readHandle, std::move(streamData));
                                         });
    }

    void DeviousBatonHandler::onWebTransportUniStream(
        proxygen::HTTPCodec::StreamID id,
        proxygen::WebTransport::StreamReadHandle* readHandle) noexcept
    {
    }

    void DeviousBatonHandler::onWebTransportSessionClose(folly::Optional<uint32_t> error) noexcept
    {
        VLOG(4) << "Session Close error="
                << (error ? folly::to<std::string>(*error) : std::string("none"));
    }

    void DeviousBatonHandler::onDatagram(std::unique_ptr<folly::IOBuf> datagram) noexcept
    {
        VLOG(4) << "DeviousBatonHandler::" << __func__;
    }

    void DeviousBatonHandler::onBody(std::unique_ptr<folly::IOBuf> body) noexcept
    {
        VLOG(4) << "DeviousBatonHandler::" << __func__;
        VLOG(3) << proxygen::IOBufPrinter::printHexFolly(body.get(), true);
        folly::io::Cursor cursor(body.get());
        auto leftToParse = body->computeChainDataLength();
        while (leftToParse > 0)
        {
            auto typeRes = quic::decodeQuicInteger(cursor, leftToParse);
            if (!typeRes)
            {
                LOG(ERROR) << "Failed to decode capsule type";
                return;
            }

            auto [type, typeLen] = typeRes.value();
            leftToParse -= typeLen;

            auto capsuleLengthRes = quic::decodeQuicInteger(cursor, leftToParse);
            if (!capsuleLengthRes)
            {
                LOG(ERROR) << "Failed to decode capsule length: type=" << type;
                return;
            }

            auto [capsuleLength, capsuleLengthLen] = capsuleLengthRes.value();
            leftToParse -= capsuleLengthLen;
            if (capsuleLength > leftToParse)
            {
                LOG(ERROR) << "Not echough data for capsule: type=" << type
                           << " length=" << capsuleLength;
                return;
            }
        }
    }

    void DeviousBatonHandler::onEOM() noexcept
    {
        VLOG(4) << "DeviousBatonHandler::" << __func__;
        if (transaction && !transaction->isEgressEOMSeen())
        {
            transaction->sendEOM();
        }
    }

    void DeviousBatonHandler::onError(const proxygen::HTTPException& error) noexcept
    {
        VLOG(4) << "DeviousBatonHandler::onError error=" << error.what();
    }

    void DeviousBatonHandler::readHandler(proxygen::WebTransport::StreamReadHandle* readHandle,
                                          folly::Try<proxygen::WebTransport::StreamData> streamData)
    {
        if (streamData.hasException())
        {
            VLOG(4) << "read error=" << streamData.exception().what();
        }
        else
        {
            VLOG(4) << "read data id =" << readHandle->getID();
            devious->onStreamData(readHandle->getID(),
                                  streams[readHandle->getID()],
                                  std::move(streamData->data),
                                  streamData->fin);
            if (!streamData->fin)
            {
                readHandle->awaitNextRead(eventBase,
                                          [this](auto readHandle, auto streamData)
                                          {
                                              readHandler(readHandle, std::move(streamData));
                                          });
            }
        }
    }
}  // namespace quic::samples
