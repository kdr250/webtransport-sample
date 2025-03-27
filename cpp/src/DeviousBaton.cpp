#include "DeviousBaton.h"
#include <quic/codec/QuicInteger.h>

using namespace proxygen;

namespace
{
    std::unique_ptr<folly::IOBuf> makeBatonMessage(uint64_t padLen, uint8_t baton)
    {
        auto buffer = folly::IOBuf::create(padLen + 9);
        folly::io::Appender cursor(buffer.get(), 1);
        quic::encodeQuicInteger(padLen,
                                [&](auto value)
                                {
                                    cursor.writeBE(value);
                                });
        memset(buffer->writableTail(), 'a', padLen);
        buffer->append(padLen);
        buffer->writableTail()[0] = baton;
        buffer->append(1);
        return buffer;
    }

    constexpr uint64_t kStreamPadLen      = 2000;
    constexpr uint64_t kDatagramPadLen    = 1000;
    constexpr uint64_t kMaxParallelBatons = 10;
}  // namespace

namespace devious
{
    folly::Expected<folly::Unit, uint16_t> DeviousBaton::onRequest(
        const proxygen::HTTPMessage& request)
    {
        // Validate request
        if (request.getMethod() != HTTPMethod::CONNECT)
        {
            LOG(ERROR) << "Invalid method=" << request.getMethodString();
            return folly::makeUnexpected(uint16_t(400));
        }
        if (request.getPathAsStringPiece() != "/webtransport/devious-baton")
        {
            LOG(ERROR) << "Invalid path=" << request.getPathAsStringPiece();
            return folly::makeUnexpected(uint16_t(404));
        }

        // Validate query params, and get count of batons
        try
        {
            auto queryParams = request.getQueryParams();
            uint64_t count   = 1;
            for (auto [key, value] : queryParams)
            {
                if (key == "version")
                {
                    if (folly::to<uint64_t>(value) != 0)
                    {
                        throw std::runtime_error("Unsupported version");
                    }
                }
                if (key == "count")
                {
                    count = folly::to<uint64_t>(value);
                    if (count > kMaxParallelBatons)
                    {
                        throw std::runtime_error("Exceed max parallel batons");
                    }
                }
                if (key == "baton")
                {
                    batons.push_back(folly::to<uint8_t>(value));
                    if (batons.back() == 0)
                    {
                        throw std::runtime_error("Invalid starting baton = 0");
                    }
                }
            }

            for (auto i = batons.size(); i < count; ++i)
            {
                batons.push_back(255 - i);
            }

            activeBatons = count;

            return folly::unit;
        }
        catch (const std::exception& error)
        {
            LOG(ERROR) << "Invalid query parameters: " << error.what();
            return folly::makeUnexpected(uint16_t(404));
        }

        return folly::makeUnexpected(uint16_t(500));
    }

    void DeviousBaton::start()
    {
        for (auto baton : batons)
        {
            auto handle = webTransport->createUniStream();
            if (!handle)
            {
                webTransport->closeSession(uint32_t(BatonSessionError::DA_YAMN));
            }

            auto id = handle.value()->getID();
            webTransport->writeStreamData(id,                                      // id
                                          makeBatonMessage(kStreamPadLen, baton),  // data
                                          true,                                    // fin
                                          nullptr                                  // Callback
            );
        }
    }

    HTTPMessage DeviousBaton::makeRequest(uint64_t version,
                                          uint64_t count,
                                          std::vector<uint8_t> batons)
    {
        HTTPMessage request;
        request.setMethod(HTTPMethod::CONNECT);
        request.setHTTPVersion(1, 1);
        request.setUpgradeProtocol("webtransport");
        request.setURL("/webtransport/devious-baton");
        request.setQueryParam("version", folly::to<std::string>(version));
        request.setQueryParam("count", folly::to<std::string>(count));
        for (auto baton : batons)
        {
            request.setQueryParam("baton", folly::to<std::string>(baton));
        }
        activeBatons = count;
        return request;
    }

    void DeviousBaton::onStreamData(uint64_t streamId,
                                    BatonMessageState& state,
                                    std::unique_ptr<folly::IOBuf> data,
                                    bool fin)
    {
        if (state.state == BatonMessageState::DONE)
        {
            // can only be a FIN
            if (data && data->computeChainDataLength() > 0)
            {
                closeSession(100);
            }
            return;
        }

        auto response = onBatonMessageData(state, std::move(data), fin);
        if (response.hasError())
        {
            closeSession(uint32_t(response.error()));
            return;
        }

        if (state.state == BatonMessageState::DONE)
        {
            MessageSource arrivedOn;
            if (streamId & 0x2)
            {
                arrivedOn = UNI;
            }
            else if (bool(streamId & 0x01) == (mode == Mode::SERVER))
            {
                arrivedOn = SELF_BIDI;
            }
            else
            {
                arrivedOn = PEER_BIDI;
            }

            auto who = onBatonMessage(streamId, arrivedOn, state.baton);
            if (who.hasError())
            {
                closeSession(uint32_t(who.error()));
                return;
            }

            onBatonFinished(*who, false);
        }
    }

    folly::Expected<folly::Unit, BatonSessionError> DeviousBaton::onBatonMessageData(
        BatonMessageState& state,
        std::unique_ptr<folly::IOBuf> data,
        bool fin)
    {
        state.bufQueue.append(std::move(data));
        folly::io::Cursor cursor(state.bufQueue.front());
        uint64_t consumed = 0;
        bool underflow    = false;
        switch (state.state)
        {
            case BatonMessageState::PAD_LEN:
            {
                auto padLen = quic::decodeQuicInteger(cursor);
                if (!padLen)
                {
                    underflow = true;
                    break;
                }
                consumed += padLen->second;
                state.paddingRemaining = padLen->first;
                state.state            = BatonMessageState::PAD;
                [[fallthrough]];
            }

            case BatonMessageState::PAD:
            {
                auto skipped = cursor.skipAtMost(state.paddingRemaining);
                state.paddingRemaining -= skipped;
                consumed += skipped;
                if (state.paddingRemaining > 0)
                {
                    underflow = true;
                    break;
                }
                state.state = BatonMessageState::BATON;
                [[fallthrough]];
            }

            case BatonMessageState::BATON:
            {
                if (cursor.isAtEnd())
                {
                    underflow = true;
                    break;
                }
                state.baton = cursor.read<uint8_t>();
                LOG(INFO) << "Parsed baton=" << uint64_t(state.baton);
                consumed += 1;
                state.state = BatonMessageState::DONE;
                [[fallthrough]];
            }

            case BatonMessageState::DONE:
            {
                if (!state.bufQueue.empty() && !cursor.isAtEnd())
                {
                    return folly::makeUnexpected(BatonSessionError::BRUH);
                }
            }
        }

        if (underflow && fin)
        {
            return folly::makeUnexpected(BatonSessionError::BRUH);
        }

        state.bufQueue.trimStartAtMost(consumed);
        return folly::unit;
    }

    void DeviousBaton::onBatonFinished(WhoFinished who, bool reset)
    {
        if (who == WhoFinished::NO_ONE)
        {
            return;
        }
        if (activeBatons == 0)
        {
            closeSession(uint32_t(BatonSessionError::BRUH));
            return;
        }

        --activeBatons;
        if (reset)
        {
            ++resetBatons;
        }
        else
        {
            ++finishedBatons;
        }

        if (activeBatons == 0 && who == WhoFinished::PEER)
        {
            if (finishedBatons > 0)
            {
                closeSession(folly::none);
            }
            else
            {
                closeSession(uint32_t(BatonSessionError::GAME_OVER));
            }
        }
    }

    folly::Expected<DeviousBaton::WhoFinished, BatonSessionError>
        DeviousBaton::onBatonMessage(uint64_t inStreamId, MessageSource arrivedOn, uint8_t baton)
    {
        if (baton % 7 == ((mode == Mode::SERVER) ? 0 : 1))
        {
            LOG(INFO) << "Sending datagram on baton=" << uint64_t(baton);
            webTransport->sendDatagram(makeBatonMessage(kDatagramPadLen, baton));
        }
        if (baton == 0)
        {
            return PEER;
        }

        uint64_t outStreamId = 0;
        switch (arrivedOn)
        {
            case UNI:
            {
                auto response = webTransport->createBidiStream();
                if (!response)
                {
                    return folly::makeUnexpected(BatonSessionError::DA_YAMN);
                }
                outStreamId = response->writeHandle->getID();
                startReadFn(response->readHandle);
                break;
            }

            case PEER_BIDI:
                outStreamId = inStreamId;
                break;

            case SELF_BIDI:
            {
                auto response = webTransport->createUniStream();
                if (!response)
                {
                    return folly::makeUnexpected(BatonSessionError::DA_YAMN);
                }
                outStreamId = response.value()->getID();
                break;
            }
        }

        webTransport->writeStreamData(outStreamId,                                 // id
                                      makeBatonMessage(kStreamPadLen, baton + 1),  // data
                                      true,                                        // fin
                                      nullptr                                      // Callback
        );

        if (baton + 1 == 0)
        {
            return WhoFinished::SELF;
        }
        return WhoFinished::NO_ONE;
    }

}  // namespace devious