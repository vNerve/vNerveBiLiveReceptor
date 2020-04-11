#include "supervisor_session.h"

#include "simple_worker_proto.h"

#include <boost/asio/detail/socket_ops.hpp>

namespace vNerve::bilibili::worker_supervisor
{
supervisor_session::supervisor_session(config::config_t config)
    : _config(config),
      _connection(config,
                  std::bind(&supervisor_session::on_supervisor_message,
                               shared_from_this(), std::placeholders::_1, std::placeholders::_2))
{
    send_worker_ready();
}

void supervisor_session::send_worker_ready()
{
    /*auto msg = zmsg_new();
    unsigned char* payload = new unsigned char[5];
    payload[0] = worker_ready_code;
    *(payload + 1) = boost::asio::detail::socket_ops::host_to_network_long((*_config)["max-rooms"].as<int>());

    zmsg_addmem(msg, payload, sizeof(payload));
    _connection.publish_msg(msg);*/
}

void supervisor_session::on_supervisor_message(unsigned char* msg, size_t len)
{
    /*if (zmsg_size(msg) != 1)
        return;
    auto frame = zmsg_pop(msg);
    auto payload = zframe_data(frame);
    auto payload_len = zframe_size(frame);

    if (payload_len < 5)
    {
        return;
        // todo log?
    }

    auto op_code = payload[0];
    switch (op_code)
    {
    case assign_room_code:
    {
    }
    break;
    case unassign_room_code:
    {
    }
    break;
    default:
        break;
    }*/
}

void supervisor_session::on_data(unsigned char* data, size_t len,
                                 zmq_memory_deleter deleter)
{
    /*auto frame = zframe_frommem(data, len, deleter, data);
    auto msg = zmsg_new();
    zmsg_prepend(msg, &frame);
    _connection.publish_msg(msg);
    */
}
}
