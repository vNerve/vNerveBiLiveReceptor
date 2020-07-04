#pragma once

#include "command_handler.h"

namespace vNerve::bilibili::command
{
namespace ast
{
namespace cmd
{
struct refresh_room_list
{
};
}  // namespace cmd
}  // namespace ast

template <>
inline void command_handler::operator()<ast::cmd::refresh_room_list>(ast::cmd::refresh_room_list) const
{
    // TODO impl
    return;
}

void handle_command_sv(std::string_view input, config::config_dynamic_linker* config_linker);
}