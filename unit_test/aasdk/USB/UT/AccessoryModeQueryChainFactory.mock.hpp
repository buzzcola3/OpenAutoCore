#pragma once

#include <gmock/gmock.h>
#include <USB/IAccessoryModeQueryChainFactory.hpp>


namespace aasdk
{
namespace usb
{
namespace ut
{

class AccessoryModeQueryChainFactoryMock: public IAccessoryModeQueryChainFactory
{
public:
    MOCK_METHOD0(create, IAccessoryModeQueryChain::Pointer());
};

}
}
}
