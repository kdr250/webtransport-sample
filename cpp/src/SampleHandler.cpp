#include "SampleHandler.h"

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

        // FIXME
        LOG(ERROR) << "Undefined path...";
        exit(EXIT_FAILURE);
    }
}  // namespace quic::samples
