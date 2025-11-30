#pragma once

namespace buzz::autoapp {

namespace transport_internal {

class Transport {
public:
    Transport();
    ~Transport();

    Transport(const Transport&) = delete;
    Transport& operator=(const Transport&) = delete;
};

}  // namespace transport_internal

namespace Transport = transport_internal;

}  // namespace buzz::autoapp
