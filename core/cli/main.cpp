#include <iostream>
#include <string_view>

#include "gpia/version.hpp"

int main(int argc, char* argv[])
{
    if (argc == 2 && std::string_view{argv[1]} == "--version")
    {
        std::cout << "gpia-core" << gpia::version() << '\n';
        return 0;
    }

    std::cerr << "usage: gpia-core --version\n";
    return 2;
}
