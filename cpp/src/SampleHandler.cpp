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
        if (boost::algorithm::starts_with(path, "/push"))
        {
            return new ServerPushHandler(params);
        }
        if (path == "/test")
        {
            return new TestHandler(params, folly::EventBaseManager::get()->getEventBase());
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

    void ServerPushHandler::onHeadersComplete(
        std::unique_ptr<proxygen::HTTPMessage> message) noexcept
    {
        VLOG(10) << "ServerPushHandler::" << __func__;
        message->dumpMessage(2);
        path = message->getPath();

        if (message->getMethod() != proxygen::HTTPMethod::GET)
        {
            LOG(ERROR) << "Method not supported";
            sendErrorResponse("bad request...");
            return;
        }

        VLOG(2) << "Received GET request for " << path << " at: "
                << std::chrono::duration_cast<std::chrono::microseconds>(
                       std::chrono::steady_clock::now().time_since_epoch())
                       .count();

        std::string gPushResponseBody;
        std::vector<std::string> pathPieces;
        std::string copiedPath = path;
        boost::split(pathPieces, copiedPath, boost::is_any_of("/"));
        int responseSize = 0;
        int numResponses = 1;

        if (pathPieces.size() > 2)
        {
            auto sizeFromPath = folly::tryTo<int>(pathPieces[2]);
            responseSize      = sizeFromPath.value_or(0);
            if (responseSize != 0)
            {
                VLOG(2) << "Requested a response size of " << responseSize;
                gPushResponseBody = std::string(responseSize, 'a');
            }
        }

        if (pathPieces.size() > 3)
        {
            auto numResponseFromPath = folly::tryTo<int>(pathPieces[3]);
            numResponses             = numResponseFromPath.value_or(1);
            VLOG(2) << "Requested a repeat count of " << numResponses;
        }

        for (int i = 0; i < numResponses; ++i)
        {
            VLOG(2) << "Sending push text " << i << "/" << numResponses;

            // Create a URL for the pushed resource
            auto pushedResourceUrl = folly::to<std::string>(message->getURL(), "/", "pushed", i);

            // Create a pushed transaction and handler
            auto pushedTransaction = transaction->newPushedTransaction(&pushTransactionHandler);

            if (!pushedTransaction)
            {
                LOG(ERROR) << "Could not create push transaction: stop pushing";
                break;
            }

            proxygen::WebTransport* webTransport = pushedTransaction->getWebTransport();
            webTransport->awaitBidiStreamCredit();

            // Send a promise for the pushed resource
            sendPushPromise(pushedTransaction, pushedResourceUrl);

            // Send the push response
            sendPushResponse(pushedTransaction, pushedResourceUrl, gPushResponseBody, true);
        }

        // Send the response to the original get request
        sendOkResponse("I AM THE REQUEST RESPONSE AND I AM RESPONSIBLE", true);
    }

    void ServerPushHandler::onBody(std::unique_ptr<folly::IOBuf> chain) noexcept
    {
        VLOG(10) << "ServerPushHandler::" << __func__ << " - ignoring";
    }

    void ServerPushHandler::onError(const proxygen::HTTPException& error) noexcept
    {
        VLOG(10) << "ServerPushHandler::onError error=" << error.what();
    }

    void ServerPushHandler::onEOM() noexcept
    {
        VLOG(10) << "ServerPushHandler::" << __func__ << " - ignoring";
    }

    void ServerPushHandler::sendPushPromise(proxygen::HTTPTransaction* pushTransaction,
                                            const std::string& pushedResourceUrl)
    {
        VLOG(10) << "ServerPushHandler::" << __func__;
        proxygen::HTTPMessage promise;
        promise.setMethod("GET");
        promise.setURL(pushedResourceUrl);
        promise.setVersionString(getHttpVersion());
        promise.setIsChunked(true);
        transaction->sendHeaders(promise);

        VLOG(2) << "Sent push promise for " << pushedResourceUrl << " at: "
                << std::chrono::duration_cast<std::chrono::microseconds>(
                       std::chrono::steady_clock::now().time_since_epoch())
                       .count();
    }

    void ServerPushHandler::sendErrorResponse(const std::string& body)
    {
        proxygen::HTTPMessage resp = createHttpResponse(400, "ERROR");
        resp.setWantsKeepalive(false);
        transaction->sendHeaders(resp);
        transaction->sendBody(folly::IOBuf::copyBuffer(body));
        transaction->sendEOM();
    }

    void ServerPushHandler::sendPushResponse(proxygen::HTTPTransaction* pushTransaction,
                                             const std::string& pushedResourceUrl,
                                             const std::string& pushedResourceBody,
                                             bool eom)
    {
        VLOG(10) << "ServerPushHandler::" << __func__;
        proxygen::HTTPMessage response = createHttpResponse(200, "OK");
        response.setWantsKeepalive(true);
        response.setIsChunked(true);
        pushTransaction->sendHeaders(response);

        std::string responseStr =
            "I AM THE PUSHED RESPONSE AND I AM NOT RESPONSIBLE: " + pushedResourceBody;

        pushTransaction->sendBody(folly::IOBuf::copyBuffer(responseStr));

        VLOG(2) << "Sent push response for " << pushedResourceUrl << " at: "
                << std::chrono::duration_cast<std::chrono::microseconds>(
                       std::chrono::steady_clock::now().time_since_epoch())
                       .count();

        if (eom)
        {
            pushTransaction->sendEOM();
            VLOG(2) << "Sent EOM for " << pushedResourceUrl << " at: "
                    << std::chrono::duration_cast<std::chrono::microseconds>(
                           std::chrono::steady_clock::now().time_since_epoch())
                           .count();
        }
    }

    void ServerPushHandler::sendOkResponse(const std::string& body, bool eom)
    {
        VLOG(10) << "ServerPushHandler::" << __func__ << ": sending " << body.length() << " bytes";
        proxygen::HTTPMessage resp = createHttpResponse(200, "OK");
        resp.setWantsKeepalive(true);
        resp.setIsChunked(true);
        transaction->sendHeaders(resp);
        transaction->sendBody(folly::IOBuf::copyBuffer(body));
        if (eom)
        {
            transaction->sendEOM();
        }
    }

    void TestHandler::onHeadersComplete(std::unique_ptr<proxygen::HTTPMessage> message) noexcept
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
            status = 200;
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
        }
        response.dumpMessage(4);
        transaction->sendHeaders(response);
    }

    void TestHandler::onWebTransportBidiStream(
        proxygen::HTTPCodec::StreamID id,
        proxygen::WebTransport::BidiStreamHandle stream) noexcept
    {
        VLOG(4) << "New Bidi Stream=" << id;
        stream.readHandle->awaitNextRead(
            eventBase,
            [this, stream](auto readHandle, auto streamData)
            {
                readHandler(stream.writeHandle, readHandle, std::move(streamData));
            });
    }

    void TestHandler::onWebTransportUniStream(
        proxygen::HTTPCodec::StreamID id,
        proxygen::WebTransport::StreamReadHandle* readHandle) noexcept
    {
        VLOG(4) << "New Uni Stream=" << id;
        auto webTransport        = transaction->getWebTransport();
        auto writeHandleExpected = webTransport->createUniStream();
        if (writeHandleExpected.hasError())
        {
            LOG(ERROR) << "Create Unistream Error!";
            return;
        }
        auto writeHandle = writeHandleExpected.value();
        readHandle->awaitNextRead(eventBase,
                                  [this, writeHandle](auto readHandle, auto streamData)
                                  {
                                      readHandler(writeHandle, readHandle, std::move(streamData));
                                  });
    }

    void TestHandler::onWebTransportSessionClose(folly::Optional<uint32_t> error) noexcept
    {
        VLOG(4) << "Session Close error="
                << (error ? folly::to<std::string>(*error) : std::string("none"));
    }

    void TestHandler::onDatagram(std::unique_ptr<folly::IOBuf> datagram) noexcept
    {
        VLOG(4) << "TestHandler::" << __func__;
        auto webTransport = transaction->getWebTransport();
        webTransport->sendDatagram(datagram->clone());
    }

    void TestHandler::onBody(std::unique_ptr<folly::IOBuf> body) noexcept
    {
        VLOG(4) << "TestHandler::" << __func__;
        VLOG(3) << proxygen::IOBufPrinter::printHexFolly(body.get(), true);
    }

    void TestHandler::onEOM() noexcept
    {
        VLOG(4) << "TestHandler::" << __func__;
        if (transaction && !transaction->isEgressEOMSeen())
        {
            transaction->sendEOM();
        }
    }

    void TestHandler::onError(const proxygen::HTTPException& error) noexcept
    {
        VLOG(4) << "TestHandler::onError error=" << error.what();
    }

    void TestHandler::readHandler(proxygen::WebTransport::StreamWriteHandle* writeHandle,
                                  proxygen::WebTransport::StreamReadHandle* readHandle,
                                  folly::Try<proxygen::WebTransport::StreamData> streamData)
    {
        if (streamData.hasException())
        {
            VLOG(4) << "read error=" << streamData.exception().what();
        }
        else
        {
            VLOG(4) << "read data id =" << readHandle->getID();

            if (!streamData->fin)
            {
                readHandle->awaitNextRead(
                    eventBase,
                    [this, writeHandle](auto readHandle, auto streamData)
                    {
                        readHandler(writeHandle, readHandle, std::move(streamData));
                    });
            }
            else
            {
                LOG(INFO) << "read finish!";
                writeHandle->writeStreamData(std::move(streamData->data), true, nullptr);
                LOG(INFO) << "write finish!";
            }
        }
    }
}  // namespace quic::samples
