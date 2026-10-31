#include "galaxy/Persistence.hpp"
#include <iostream>
#include <exception>
int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "Usage: galaxy_recover_copy <corrupt-source.sqlite> <new-copy.sqlite>\n"
                     "Source is opened read-only. Destination MUST NOT exist.\n";
        return 2;
    }
    try {
        galaxy::PersistentWorld::recover_copy(argv[1], argv[2]);
        std::cout << "VERIFIED: recovered state saved to new database copy: " << argv[2] << "\n"
                     "The original database was not modified.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "RECOVERY REFUSED: " << e.what() << "\n";
        return 1;
    }
}
