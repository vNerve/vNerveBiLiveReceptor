#pragma once

#include "config.h"
#include "profiler.h"

//#define BOOST_SPIRIT_DEBUG
#include <iostream>
#include <exception>
#include <boost/spirit/include/qi.hpp>
#include <boost/fusion/adapted/struct/adapt_struct.hpp>
#include <boost/fusion/include/adapt_struct.hpp>
#include <boost/fusion/mpl.hpp>
#include <boost/phoenix.hpp>

namespace vNerve::bilibili::command
{
namespace qi = boost::spirit::qi;
namespace ast
{
namespace cmd
{
struct level
{
    std::string target_level;
};

struct set
{
    std::string name;
    std::string value;
};

struct exit
{
};
}  // namespace cmd

template <class... T>
using basic_command = boost::variant<cmd::level, cmd::set, cmd::exit, T...>;
template <class... T>
using basic_commands = std::vector<basic_command<T...>>;
}  // namespace ast

}  // namespace vNerve::bilibili::command

#ifdef BOOST_PP_VARIADICS
BOOST_FUSION_ADAPT_STRUCT(vNerve::bilibili::command::ast::cmd::level, target_level)
BOOST_FUSION_ADAPT_STRUCT(vNerve::bilibili::command::ast::cmd::set, name, value)
BOOST_FUSION_ADAPT_STRUCT(vNerve::bilibili::command::ast::cmd::exit)
#endif

namespace vNerve::bilibili::command
{
template <class It, class... ExtCmd>
struct basic_command_grammar : qi::grammar<It, ast::basic_commands<ExtCmd...>()>
{
protected:
    using skipper = qi::space_type;
    using commands_type = ast::basic_commands<ExtCmd...>;
    using command_type = ast::basic_command<ExtCmd...>;
    qi::rule<It, commands_type(), skipper> script;
    qi::rule<It, command_type(), skipper> command;

    qi::rule<It, ast::cmd::level(), skipper> level;
    qi::rule<It, ast::cmd::set(), skipper> set;
    qi::rule<It, ast::cmd::exit(), skipper> exit;

    qi::rule<It, commands_type()> start;
    qi::rule<It, std::string(), qi::space_type, qi::locals<char>> quoted_string, any_string;

    template <class ExtCmdPack>
    void enable_extra_commands(ExtCmdPack const& extra_commands)
    {
        command = level | set | exit | extra_commands.copy();
    }

public:
    basic_command_grammar()
        : basic_command_grammar::base_type(start)
    {
        using namespace qi;
        start = skip(qi::space)[script];
        script = command % ";";
        command = level | set | exit;

        quoted_string =
               omit   [ char_("'\"")[_a = _1] ]
            >> no_skip[ *(char_ - char_(_a))  ]
            >> lit(_a);
        //quoted_string %= lexeme['"' >> +(char_ - '"') >> '"'];

        any_string = quoted_string | lexeme [+~char_("; ")];

        level = "level" >> any_string;
        set = "set" >> any_string >> any_string;
        exit = "exit" >> attr(ast::cmd::exit{});

        on_error<fail>(start,
                       std::cerr << boost::phoenix::val("[cmd_handler] Invalid input:") << boost::phoenix::construct<std::string>(qi::_3, qi::_2) << std::endl);
        BOOST_SPIRIT_DEBUG_NODE(quoted_string);
        BOOST_SPIRIT_DEBUG_NODE(any_string);
    }
};

class exit_requested_exception final : public std::exception
{
};

struct command_handler : boost::static_visitor<>
{
    config::config_dynamic_linker* config_linker;

    explicit command_handler(config::config_dynamic_linker* config_linker)
        : config_linker(config_linker)
    {
    }

    template <typename T>
    void operator()(T) const
    {
        std::cerr << "INTERNAL ERROR: No command handle provided. Report this to the developer!" << std::endl;
        spdlog::error("INTERNAL ERROR: No command handler provided. Report this to the developer!");
        abort();
    }
};

template <>
inline void command_handler::operator()<ast::cmd::level>(ast::cmd::level level_ast) const
{
    auto level = spdlog::level::from_str(level_ast.target_level);
    if (level == spdlog::level::off && level_ast.target_level != "off")
    {
        std::cerr << "[cmd_handler] Unknown logging level " << level_ast.target_level << " !" << std::endl;
        spdlog::error("[cmd_handler] Unknown logging level {} !", level_ast.target_level);
    }
    else
    {
        spdlog::set_level(level);
        if (level != spdlog::level::off)
            spdlog::log(level, "[cmd_handler] Set logging level to {}", spdlog::level::to_string_view(level));
    }
}

template <>
inline void command_handler::operator()<ast::cmd::set>(ast::cmd::set level) const
{
    config_linker->update_config(level.name, level.value);
}

template <>
inline void command_handler::operator()<ast::cmd::exit>(ast::cmd::exit) const
{
    throw exit_requested_exception();
}

using handle_command_iterator = std::string_view::const_iterator;

template <class Grammar, class... ExtCmd>
void handle_command(std::string_view input, config::config_dynamic_linker* config_linker)
{
    VN_PROFILE_SCOPED(HandleConsoleCommand)
    auto start = input.begin();
    auto end = input.end();
    ast::basic_commands<ExtCmd...> commands;

    bool matched = qi::parse(
        start, end,
        Grammar(),
        commands);

    if (!matched)
    {
        std::cerr << "[cmd_handler] Invalid input: " << input << std::endl;
        spdlog::error("[cmd_handler] Invalid input: {}", input);
        return;
    }
    if (start != end)
    {
        spdlog::error("[cmd_handler] Invalid input: {}", input);
        std::cerr << "[cmd_handler] Invalid input: " << input << " ! Stopped at " << std::string(start, end) << std::endl;
        return;
    }

    auto handler = command_handler(config_linker);
    for (auto const& cmd : commands)
        boost::apply_visitor(handler, cmd);
}
}  // namespace vNerve::bilibili::command