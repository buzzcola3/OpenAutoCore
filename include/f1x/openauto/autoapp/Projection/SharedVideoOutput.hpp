/*
*  This file is part of openauto project.
*  Copyright (C) 2025 buzzcola3 (Samuel Betak)
*
*  openauto is free software: you can redistribute it and/or modify
*  it under the terms of the GNU General Public License as published by
*  the Free Software Foundation; either version 3 of the License, or
*  (at your option) any later version.

*  openauto is distributed in the hope that it will be useful,
*  but WITHOUT ANY WARRANTY; without even the implied warranty of
*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*  GNU General Public License for more details.
*
*  You should have received a copy of the GNU General Public License
*  along with openauto. If not, see <http://www.gnu.org/licenses/>.
*/

#pragma once

#include <boost/noncopyable.hpp>
#include <boost/asio/io_context.hpp>
#include <f1x/openauto/autoapp/Projection/VideoOutput.hpp>
#include <f1x/openauto/autoapp/Projection/SequentialBuffer.hpp>

namespace f1x
{
namespace openauto
{
namespace autoapp
{
namespace projection
{

/**
 * @class SharedVideoOutput
 * @brief A video sink that pushes raw H.264 frames into a shared buffer.
 *
 * This class removes all Qt dependencies for video playback, acting as a simple
 * conduit between the Android Auto video stream and an in-memory buffer.
 */
class SharedVideoOutput: public VideoOutput, private boost::noncopyable
{
public:
    /**
     * @brief Constructor.
     * @param io_context The Boost.Asio io_context needed to initialize the internal buffer.
     * @param configuration The application configuration pointer.
     */
    SharedVideoOutput(boost::asio::io_context& io_context, configuration::IConfiguration::Pointer configuration);

    // IVideoOutput interface implementation
    bool open() override;
    bool init() override;
    void write(uint64_t timestamp, const aasdk::common::DataConstBuffer& buffer) override;
    void stop() override;

    /**
     * @brief Provides access to the internal buffer for consumer components.
     * @return A reference to the SequentialBuffer.
     */
    SequentialBuffer& getBuffer();

private:
    SequentialBuffer videoBuffer_;
};

}
}
}
}