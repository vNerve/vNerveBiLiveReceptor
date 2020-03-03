#include <iostream>
#include <boost/filesystem.hpp>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

int main() {
    boost::filesystem::path full_path(boost::filesystem::current_path());
    std::cout << "Current path is : " << full_path << std::endl;
    
    auto j2 = R"(
    {
        "happy": true,
        "pi": 3.141
    }
    )"_json;
    std::cout << j2["pi"] << std::endl;
}