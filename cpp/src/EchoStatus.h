#pragma once
#include <cstdint>

namespace EchoService
{
    /**
     * Just some dummy class containing request count. Since we keep
     * one instance of this in each class, there is no need of
     * synchronization
     */
    class EchoStatus
    {
    public:
        ~EchoStatus() {}

        void recordRequest()
        {
            ++reqCount;
        }

        uint64_t getRequestCount()
        {
            return reqCount;
        }

    private:
        uint64_t reqCount = 0;
    };
}  // namespace EchoService