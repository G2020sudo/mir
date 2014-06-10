/*
 * Copyright © 2012 Canonical Ltd.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License version 3,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Authored by: Alan Griffiths <alan@octopull.co.uk>
 */

#include "mir_socket_rpc_channel.h"
#include "rpc_report.h"

#include "mir/protobuf/google_protobuf_guard.h"
#include "../surface_map.h"
#include "../mir_surface.h"
#include "../display_configuration.h"
#include "../lifecycle_control.h"

#include "mir_protobuf.pb.h"  // For Buffer frig
#include "mir_protobuf_wire.pb.h"

#include <boost/bind.hpp>
#include <endian.h>

#include <stdexcept>


namespace mcl = mir::client;
namespace mclr = mir::client::rpc;

namespace
{
std::chrono::milliseconds const timeout(200);
}

mclr::MirProtobufRpcChannel::MirProtobufRpcChannel(
    std::unique_ptr<mclr::Transport> transport,
    std::shared_ptr<mcl::SurfaceMap> const& surface_map,
    std::shared_ptr<DisplayConfiguration> const& disp_config,
    std::shared_ptr<RpcReport> const& rpc_report,
    std::shared_ptr<LifecycleControl> const& lifecycle_control) :
    rpc_report(rpc_report),
    pending_calls(rpc_report),
    transport{std::move(transport)},
    surface_map(surface_map),
    display_configuration(disp_config),
    lifecycle_control(lifecycle_control),
    disconnected(false)
{
    this->transport->register_data_received_notification(std::bind(&mclr::MirProtobufRpcChannel::on_message_available,
                                                                   this));
}

mclr::MirProtobufRpcChannel::~MirProtobufRpcChannel()
{
}


void mclr::MirProtobufRpcChannel::notify_disconnected()
{
    if (!disconnected.exchange(true))
    {
        lifecycle_control->call_lifecycle_event_handler(mir_lifecycle_connection_lost);
    }
    pending_calls.force_completion();
}


template<class MessageType>
void mclr::MirProtobufRpcChannel::receive_any_file_descriptors_for(MessageType* response)
{
    if (response)
    {
        response->clear_fd();

        if (response->fds_on_side_channel() > 0)
        {
            std::vector<int32_t> fds(response->fds_on_side_channel());
            transport->receive_file_descriptors(fds);
            for (auto &fd: fds)
                response->add_fd(fd);

            rpc_report->file_descriptors_received(*response, fds);
        }
        response->clear_fds_on_side_channel();
    }
}

void mclr::MirProtobufRpcChannel::receive_file_descriptors(google::protobuf::Message* response,
    google::protobuf::Closure* complete)
{
    if (!disconnected.load())
    {
        auto const message_type = response->GetTypeName();

        mir::protobuf::Surface* surface = nullptr;
        mir::protobuf::Buffer* buffer = nullptr;
        mir::protobuf::Platform* platform = nullptr;
        mir::protobuf::SocketFD* socket_fd = nullptr;

        if (message_type == "mir.protobuf.Buffer")
        {
            buffer = static_cast<mir::protobuf::Buffer*>(response);
        }
        else if (message_type == "mir.protobuf.Surface")
        {
            surface = static_cast<mir::protobuf::Surface*>(response);
            if (surface && surface->has_buffer())
                buffer = surface->mutable_buffer();
        }
        else if (message_type == "mir.protobuf.Screencast")
        {
            auto screencast = static_cast<mir::protobuf::Screencast*>(response);
            if (screencast && screencast->has_buffer())
                buffer = screencast->mutable_buffer();
        }
        else if (message_type == "mir.protobuf.Platform")
        {
            platform = static_cast<mir::protobuf::Platform*>(response);
        }
        else if (message_type == "mir.protobuf.Connection")
        {
            auto connection = static_cast<mir::protobuf::Connection*>(response);
            if (connection && connection->has_platform())
                platform = connection->mutable_platform();
        }
        else if (message_type == "mir.protobuf.SocketFD")
        {
            socket_fd = static_cast<mir::protobuf::SocketFD*>(response);
        }

        receive_any_file_descriptors_for(surface);
        receive_any_file_descriptors_for(buffer);
        receive_any_file_descriptors_for(platform);
        receive_any_file_descriptors_for(socket_fd);
    }
    complete->Run();
}

void mclr::MirProtobufRpcChannel::on_message_available()
{
    while (transport->data_available())
    {
        mir::protobuf::wire::Result result;
        try
        {
            uint16_t message_size;
            transport->receive_data(&message_size, sizeof(uint16_t));
            message_size = be16toh(message_size);

            body_bytes.resize(message_size);
            transport->receive_data(body_bytes.data(), message_size);

            rpc_report->result_receipt_succeeded(result);
        }
        catch (std::exception const& x)
        {
            rpc_report->result_receipt_failed(x);
            throw;
        }

        try
        {
            for (int i = 0; i != result.events_size(); ++i)
            {
                process_event_sequence(result.events(i));
            }

            if (result.has_id())
            {
                pending_calls.complete_response(result);
            }
        }
        catch (std::exception const& x)
        {
            rpc_report->result_processing_failed(result, x);
            // Eat this exception as it doesn't affect rpc
        }
    }
}

void mclr::MirProtobufRpcChannel::CallMethod(
    const google::protobuf::MethodDescriptor* method,
    google::protobuf::RpcController*,
    const google::protobuf::Message* parameters,
    google::protobuf::Message* response,
    google::protobuf::Closure* complete)
{
    auto const& invocation = invocation_for(method, parameters);

    rpc_report->invocation_requested(invocation);

    std::shared_ptr<google::protobuf::Closure> callback(
        google::protobuf::NewPermanentCallback(this, &MirProtobufRpcChannel::receive_file_descriptors, response, complete));

    // Only save details after serialization succeeds
    pending_calls.save_completion_details(invocation, response, callback);

    // Only send message when details saved for handling response
    send_message(invocation, invocation);
}

void mclr::MirProtobufRpcChannel::send_message(
    mir::protobuf::wire::Invocation const& body,
    mir::protobuf::wire::Invocation const& invocation)
{
    const size_t size = body.ByteSize();
    const unsigned char header_bytes[2] =
    {
        static_cast<unsigned char>((size >> 8) & 0xff),
        static_cast<unsigned char>((size >> 0) & 0xff)
    };

    detail::SendBuffer send_buffer(sizeof header_bytes + size);
    std::copy(header_bytes, header_bytes + sizeof header_bytes, send_buffer.begin());
    body.SerializeToArray(send_buffer.data() + sizeof header_bytes, size);

    try
    {
        transport->send_data(send_buffer);
    }
    catch (std::runtime_error const& err)
    {
        rpc_report->invocation_failed(invocation, err);
        notify_disconnected();
        throw;
    }
    rpc_report->invocation_succeeded(invocation);
}

/*
void mclr::MirProtobufRpcChannel::on_header_read(const boost::system::error_code& error)
{
    if (error)
    {
        // If we've not got a response to a call pending
        // then during shutdown we expect to see "eof"
        if (!pending_calls.empty() || error != boost::asio::error::eof)
        {
            rpc_report->header_receipt_failed(error);
            BOOST_THROW_EXCEPTION(std::runtime_error("Failed to read message header: " + error.message()));
        }

        return;
    }

    read_message();

    boost::asio::async_read(
        socket,
        boost::asio::buffer(header_bytes),
        boost::asio::transfer_exactly(sizeof header_bytes),
        boost::bind(&MirProtobufRpcChannel::on_header_read, this,
            boost::asio::placeholders::error));
}
*/
void mclr::MirProtobufRpcChannel::read_message()
{
    mir::protobuf::wire::Result result;

    try
    {
        const size_t body_size = read_message_header();

        read_message_body(result, body_size);

        rpc_report->result_receipt_succeeded(result);
    }
    catch (std::exception const& x)
    {
        rpc_report->result_receipt_failed(x);
        throw;
    }

    try
    {
        for (int i = 0; i != result.events_size(); ++i)
        {
            process_event_sequence(result.events(i));
        }

        if (result.has_id())
        {
            pending_calls.complete_response(result);
        }
    }
    catch (std::exception const& x)
    {
        rpc_report->result_processing_failed(result, x);
        // Eat this exception as it doesn't affect rpc
    }
}

void mclr::MirProtobufRpcChannel::process_event_sequence(std::string const& event)
{
    mir::protobuf::EventSequence seq;

    seq.ParseFromString(event);

    if (seq.has_display_configuration())
    {
        display_configuration->update_configuration(seq.display_configuration());
    }

    if (seq.has_lifecycle_event())
    {
        lifecycle_control->call_lifecycle_event_handler(seq.lifecycle_event().new_state());
    }

    int const nevents = seq.event_size();
    for (int i = 0; i != nevents; ++i)
    {
        mir::protobuf::Event const& event = seq.event(i);
        if (event.has_raw())
        {
            std::string const& raw_event = event.raw();

            // In future, events might be compressed where possible.
            // But that's a job for later...
            if (raw_event.size() == sizeof(MirEvent))
            {
                MirEvent e;

                // Make a copy to ensure integer fields get correct memory
                // alignment, which is critical on many non-x86
                // architectures.
                memcpy(&e, raw_event.data(), sizeof e);

                rpc_report->event_parsing_succeeded(e);

                surface_map->with_surface_do(e.surface.id,
                    [&e](MirSurface* surface)
                    {
                        surface->handle_event(e);
                    });
            }
            else
            {
                rpc_report->event_parsing_failed(event);
            }
        }
    }
}

size_t mclr::MirProtobufRpcChannel::read_message_header()
{
    const size_t body_size = (header_bytes[0] << 8) + header_bytes[1];
    return body_size;
}

void mclr::MirProtobufRpcChannel::read_message_body(
    mir::protobuf::wire::Result& result,
    size_t const body_size)
{
/*    boost::system::error_code error;
    body_bytes.resize(body_size);
    boost::asio::read(socket, boost::asio::buffer(body_bytes), error);
    if (error)
    {
        BOOST_THROW_EXCEPTION(std::runtime_error("Failed to read message body: " + error.message()));
    }
*/
    result.ParseFromArray(body_bytes.data(), body_size);
}
