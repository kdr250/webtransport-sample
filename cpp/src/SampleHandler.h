#pragma once

#include <folly/Conv.h>
#include <folly/futures/Future.h>
#include <folly/io/IOBuf.h>
#include <proxygen/lib/http/HTTPException.h>
#include <proxygen/lib/http/HTTPMessage.h>
#include <algorithm>
#include <chrono>
#include <climits>
#include <cmath>
#include <functional>
#include <mutex>
#include <random>
#include <vector>

#include <folly/File.h>
#include <folly/FileUtil.h>
#include <folly/Format.h>
#include <folly/Memory.h>
#include <folly/Random.h>
#include <folly/ThreadLocal.h>
#include <folly/executors/GlobalExecutor.h>
#include <folly/io/async/AsyncTimeout.h>
#include <folly/io/async/EventBaseManager.h>
#include <proxygen/lib/http/session/HTTPTransaction.h>
#include <proxygen/lib/utils/SafePathUtils.h>

#include "DeviousBaton.h"
#include "HQServer.h"

namespace quic::samples
{
    /**
     * The Dispatcher object is responsible for spawning
     * new request handlers, based on the path.
     */
    struct HandlerParams
    {
        std::string protocol;
        uint16_t port;
        std::string httpVersion;

        HandlerParams(std::string proto, uint16_t po, std::string version) :
            protocol(proto), port(po), httpVersion(version)
        {
        }
    };

    class Dispatcher
    {
    public:
        explicit Dispatcher(HandlerParams handlerParams) : params(std::move(handlerParams)) {}

        proxygen::HTTPTransactionHandler* getRequestHandler(proxygen::HTTPMessage* message);

    private:
        HandlerParams params;
    };

    class BaseSampleHandler : public proxygen::HTTPTransactionHandler
    {
    public:
        BaseSampleHandler() = delete;

        explicit BaseSampleHandler(const HandlerParams& handlerParams) : params(handlerParams) {}

        void setTransaction(proxygen::HTTPTransaction* txn) noexcept override
        {
            transaction = txn;
        }

        void detachTransaction() noexcept override
        {
            delete this;
        }

        void onChunkHeader(size_t /*length*/) noexcept override {}

        void onChunkComplete() noexcept override {}

        void onTrailers(std::unique_ptr<proxygen::HTTPHeaders> /*trailers*/) noexcept override {}

        void onUpgrade(proxygen::UpgradeProtocol /*protocol*/) noexcept override {}

        void onEgressPaused() noexcept override {}

        void onEgressResumed() noexcept override {}

        void maybeAddAltSvcHeader(proxygen::HTTPMessage& msg) const
        {
            if (params.protocol.empty() || params.port == 0)
            {
                return;
            }
            msg.getHeaders().add(proxygen::HTTP_HEADER_ALT_SVC,
                                 fmt::format("{}=\":{}\"; ma=3600", params.protocol, params.port));
        }

        static const std::string& getH1QFooter()
        {
            // clang-format off
            static const std::string footer(
                "==============\n"
                " Hello World! \n"
                "==============\n"
            );
            // clang-format on
            return footer;
        }

        static uint32_t getQueryParamAsNumber(std::unique_ptr<proxygen::HTTPMessage>& msg,
                                              const std::string& name,
                                              uint32_t defValue) noexcept
        {
            return folly::tryTo<uint32_t>(msg->getQueryParam(name)).value_or(defValue);
        }

    protected:
        [[nodiscard]] const std::string& getHttpVersion() const
        {
            return params.httpVersion;
        }

        proxygen::HTTPMessage createHttpResponse(uint16_t status, std::string_view message)
        {
            proxygen::HTTPMessage resp;
            resp.setVersionString(getHttpVersion());
            resp.setStatusCode(status);
            resp.setStatusMessage(message);
            return resp;
        }

        proxygen::HTTPTransaction* transaction = nullptr;
        const HandlerParams& params;
    };

    using random_bytes_engine =
        std::independent_bits_engine<std::default_random_engine, CHAR_BIT, unsigned char>;

    class EchoHandler : public BaseSampleHandler
    {
    public:
        explicit EchoHandler(const HandlerParams& params) : BaseSampleHandler(params) {}

        EchoHandler() = delete;

        void onHeadersComplete(std::unique_ptr<proxygen::HTTPMessage> message) noexcept override
        {
            VLOG(10) << "EchoHandler::onHeadersComplete";
            proxygen::HTTPMessage response;
            VLOG(10) << "Seting http-version to " << getHttpVersion();
            sendFooter = (message->getHTTPVersion() == proxygen::HTTPMessage::kHTTPVersion09);

            response.setVersionString(getHttpVersion());
            response.setStatusCode(200);
            response.setStatusMessage("Ok");

            message->getHeaders().forEach(
                [&](const std::string& header, const std::string& value)
                {
                    response.getHeaders().add(folly::to<std::string>("x-echo-", header), value);
                });

            response.setWantsKeepalive(true);

            maybeAddAltSvcHeader(response);

            transaction->sendHeaders(response);
        }

        void onBody(std::unique_ptr<folly::IOBuf> chain) noexcept override
        {
            VLOG(10) << "EchoHandler::onBody";
            transaction->sendBody(std::move(chain));
        }

        void onEOM() noexcept override
        {
            VLOG(10) << "EchoHandler::onEOM";
            if (sendFooter)
            {
                auto& footer = getH1QFooter();
                transaction->sendBody(folly::IOBuf::copyBuffer(footer.data(), footer.length()));
            }
            transaction->sendEOM();
        }

        void onError(const proxygen::HTTPException& /* error */) noexcept override
        {
            transaction->sendAbort();
        }

    private:
        bool sendFooter = false;
    };

    class DeviousBatonHandler : public BaseSampleHandler
    {
    public:
        explicit DeviousBatonHandler(const HandlerParams& params, folly::EventBase* evb) :
            BaseSampleHandler(params), eventBase(evb)
        {
        }

        void onHeadersComplete(std::unique_ptr<proxygen::HTTPMessage> message) noexcept override;

        void onWebTransportBidiStream(
            proxygen::HTTPCodec::StreamID id,
            proxygen::WebTransport::BidiStreamHandle stream) noexcept override;

        void onWebTransportUniStream(
            proxygen::HTTPCodec::StreamID id,
            proxygen::WebTransport::StreamReadHandle* readHandle) noexcept override;

        void onWebTransportSessionClose(folly::Optional<uint32_t> error) noexcept override;

        void onDatagram(std::unique_ptr<folly::IOBuf> datagram) noexcept override;

        void onBody(std::unique_ptr<folly::IOBuf> body) noexcept override;

        void onEOM() noexcept override;

        void onError(const proxygen::HTTPException& error) noexcept override;

        void detachTransaction() noexcept override {}

        void readHandler(proxygen::WebTransport::StreamReadHandle* readHandle,
                         folly::Try<proxygen::WebTransport::StreamData> streamData);

        folly::Optional<devious::DeviousBaton> devious;
        folly::EventBase* eventBase = nullptr;
        std::map<uint64_t, devious::DeviousBaton::BatonMessageState> streams;
    };

    class DummyHandler : public BaseSampleHandler
    {
    public:
        explicit DummyHandler(const HandlerParams& params) : BaseSampleHandler(params) {}

        DummyHandler() = delete;

        void onHeadersComplete(std::unique_ptr<proxygen::HTTPMessage> message) noexcept override
        {
            VLOG(10) << "DummyHandler::onHeadersComplete";
            proxygen::HTTPMessage response;
            VLOG(10) << "Setting http-version to " << getHttpVersion();

            response.setVersionString(getHttpVersion());
            response.setStatusCode(200);
            response.setStatusMessage("Ok");
            response.setWantsKeepalive(true);

            maybeAddAltSvcHeader(response);

            transaction->sendHeaders(response);
            if (message->getMethod() == proxygen::HTTPMethod::GET)
            {
                transaction->sendBody(folly::IOBuf::copyBuffer(kDummyMessage));
            }
        }

        void onBody(std::unique_ptr<folly::IOBuf> /* chain */) noexcept override
        {
            VLOG(10) << "DummyHandler::onBody";
            transaction->sendBody(folly::IOBuf::copyBuffer(kDummyMessage));
        }

        void onEOM() noexcept override
        {
            VLOG(10) << "DummyHandler::onEOM";
            transaction->sendEOM();
        }

        void onError(const proxygen::HTTPException& /* error */) noexcept override
        {
            transaction->sendAbort();
        }

    private:
        const std::string kDummyMessage = folly::to<std::string>("Undefined path...");
    };

    namespace
    {
        constexpr auto kPushFileName = "resources/push.txt";
    }

    class ServerPushHandler : public BaseSampleHandler
    {
        class ServerPushTransactionHandler : public proxygen::HTTPPushTransactionHandler
        {
            void setTransaction(proxygen::HTTPTransaction* trans) noexcept override {}

            void detachTransaction() noexcept override {}

            void onError(const proxygen::HTTPException& error) noexcept override {}

            void onEgressPaused() noexcept override {}

            void onEgressResumed() noexcept override {}
        };

    public:
        explicit ServerPushHandler(const HandlerParams& params) : BaseSampleHandler(params) {}

        void onHeadersComplete(std::unique_ptr<proxygen::HTTPMessage> message) noexcept override;

        void onBody(std::unique_ptr<folly::IOBuf> chain) noexcept override;

        void onError(const proxygen::HTTPException& error) noexcept override;

        void onEOM() noexcept override;

        void detachTransaction() noexcept override {}

    private:
        void sendPushPromise(proxygen::HTTPTransaction* pushTransaction, const std::string& path);

        void sendErrorResponse(const std::string& body);

        void sendPushResponse(proxygen::HTTPTransaction* pushTransaction,
                              const std::string& url,
                              const std::string& body,
                              bool eom);

        void sendOkResponse(const std::string& body, bool eom);

        std::string path;
        ServerPushTransactionHandler pushTransactionHandler;
    };

    class TestHandler : public BaseSampleHandler
    {
    public:
        explicit TestHandler(const HandlerParams& params, folly::EventBase* evb) :
            BaseSampleHandler(params), eventBase(evb)
        {
        }

        void onHeadersComplete(std::unique_ptr<proxygen::HTTPMessage> message) noexcept override;

        void onWebTransportBidiStream(
            proxygen::HTTPCodec::StreamID id,
            proxygen::WebTransport::BidiStreamHandle stream) noexcept override;

        void onWebTransportUniStream(
            proxygen::HTTPCodec::StreamID id,
            proxygen::WebTransport::StreamReadHandle* readHandle) noexcept override;

        void onWebTransportSessionClose(folly::Optional<uint32_t> error) noexcept override;

        void onDatagram(std::unique_ptr<folly::IOBuf> datagram) noexcept override;

        void onBody(std::unique_ptr<folly::IOBuf> body) noexcept override;

        void onEOM() noexcept override;

        void onError(const proxygen::HTTPException& error) noexcept override;

        void detachTransaction() noexcept override {}

        void readHandler(proxygen::WebTransport::StreamWriteHandle* writeHandle,
                         proxygen::WebTransport::StreamReadHandle* readHandle,
                         folly::Try<proxygen::WebTransport::StreamData> streamData,
                         quic::Buf& resultData);

        folly::EventBase* eventBase = nullptr;
    };
}  // namespace quic::samples
