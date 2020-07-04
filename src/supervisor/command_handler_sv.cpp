#include "command_handler_sv.h"

namespace vNerve::bilibili::command
{
template<class It>
struct sv_command_grammar : basic_command_grammar<It, ast::cmd::refresh_room_list>
{
private:
    qi::rule<It, ast::cmd::refresh_room_list(), qi::space_type> refresh_room_list;

public:
    sv_command_grammar()
    {
        refresh_room_list = "refresh_room_list" >> boost::spirit::qi::attr(ast::cmd::refresh_room_list{});
        basic_command_grammar<It, ast::cmd::refresh_room_list>::enable_extra_commands(refresh_room_list);
    }
};
void handle_command_sv(std::string_view input, config::config_dynamic_linker* config_linker)
{
    handle_command<sv_command_grammar<handle_command_iterator>, ast::cmd::refresh_room_list>(input, config_linker);
}
}
