#include "grpc_updater.h"

namespace vNerve::bilibili::info
{
vtuber_info_updater::vtuber_info_updater(
    std::shared_ptr<boost::program_options::variables_map> options,
    vtuber_info_update_callback callback)
    : _guard(_context.get_executor()),
      _timer(std::make_unique<boost::asio::deadline_timer>(_context)),
      _update_interval_min((*options)["grpc-update-interval"].as<int>()),
      _options(options),
      _callback(callback)
{
    auto endpoint = (*options)["grpc-server"].as<std::string>()
    + ":"
    + std::to_string((*options)["grpc-server-port"].as<int>());

    _channel = CreateChannel(endpoint, grpc::InsecureChannelCredentials());
    _stub = Bilibili::NewStub(_channel);

    _thread =
        boost::thread(boost::bind(&boost::asio::io_context::run, &_context));

    _timer->expires_from_now(boost::posix_time::seconds(1));
    _timer->async_wait(
        boost::bind(&vtuber_info_updater::on_timer_tick, this,
                    boost::asio::placeholders::error));
}

vtuber_info_updater::~vtuber_info_updater()
{
    _timer->cancel();
    _guard.reset();
    _context.stop();
}

void vtuber_info_updater::reschedule_timer()
{
    _timer->expires_from_now(boost::posix_time::minutes(_update_interval_min));
    _timer->async_wait(boost::bind(&vtuber_info_updater::on_timer_tick, this,
                                   boost::asio::placeholders::error));
}

void vtuber_info_updater::on_timer_tick(const boost::system::error_code& err)
{
    if (err)
    {
        if (err.value() == boost::asio::error::operation_aborted)
        {
            return;  // closing socket.
        }
        // TODO logging
    }

    grpc::ClientContext client_context;
    GetAllBilibiliInfosRequest request;
    BilibiliCollection result;
    auto status = _stub->GetAllBilibiliInfos(&client_context, request, &result);
    if (!status.ok())
    {
        //logging
        auto msg = status.error_message();
        reschedule_timer();
        return;
    }

    auto siz = result.bilibili_infos_size();
    auto room_ids = std::vector<int>();
    for (int i = 0; i < siz; i++)
    {
        auto room_id = result.bilibili_infos(i).room_id();
        if (room_id == 0)
            continue;
        room_ids.push_back(room_id);
    }

    _callback(room_ids);

    reschedule_timer();
}

}
