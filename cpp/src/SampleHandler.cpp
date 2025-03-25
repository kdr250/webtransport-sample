#include "SampleHandler.h"

namespace quic::samples
{
    proxygen::HTTPTransactionHandler* Dispatcher::getRequestHandler(proxygen::HTTPMessage* message)
    {
        LOG(INFO) << "getRequestHandler!";
        DCHECK(message);
        auto path = message->getPathAsStringPiece();
        if (path == "/" || path == "/echo")
        {
            return new EchoHandler(params);
        }

        // FIXME
        exit(EXIT_FAILURE);
    }
}  // namespace quic::samples
