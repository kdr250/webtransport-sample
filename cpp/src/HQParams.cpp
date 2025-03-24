#include "HQParams.h"

namespace quic::samples
{
    bool MyHTTPVersion::parse(const std::string& versionString)
    {
        version = versionString;
        if (version.length() == 1)
        {
            major     = folly::to<uint16_t>(version);
            minor     = 0;
            canonical = folly::to<std::string>(major, ".", minor);
            return true;
        }

        std::string delimiter = ".";
        std::size_t position  = version.find(delimiter);
        if (position == std::string::npos)
        {
            LOG(ERROR) << "Invalid http-version string: " << version << ", defaulting to HTTP/1.1";
            major     = 1;
            minor     = 1;
            canonical = folly::to<std::string>(major, ".", minor);
            return false;
        }

        try
        {
            std::string majorVer = version.substr(0, position);
            std::string minorVer = version.substr(position + delimiter.length());
            major                = folly::to<uint16_t>(majorVer);
            minor                = folly::to<uint16_t>(minorVer);
            canonical            = folly::to<std::string>(major, ".", minor);
            return true;
        }
        catch (const folly::ConversionError&)
        {
            LOG(ERROR) << "Invalid http-version string: " << version << ", defaulting to HTTP/1.1";
            major     = 1;
            minor     = 1;
            canonical = folly::to<std::string>(major, ".", minor);
            return false;
        }
    }

    std::ostream& operator<<(std::ostream& outStream, const MyHTTPVersion& version)
    {
        outStream << "http-version=" << version.major << "/" << version.minor
                  << " (orig=" << version.version << ", canonical=" << version.canonical << ")";

        return outStream;
    }
}  // namespace quic::samples
