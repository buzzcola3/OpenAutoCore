#pragma once

#include <gmock/gmock.h>
#include <USB/IUSBWrapper.hpp>
#include <Error/Error.hpp>


namespace aasdk
{
namespace usb
{
namespace ut
{

class ConnectedAccessoriesEnumeratorPromiseHandlerMock
{
public:
    MOCK_METHOD1(onResolve, void(bool result));
    MOCK_METHOD1(onReject, void(const error::Error& e));
};

}
}
}
