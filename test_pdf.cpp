#include "src/previewer.hpp"
#include <iostream>
#include <fstream>

int main() {
    // Create a dummy pdf file
    std::ofstream dummy("dummy.pdf");
    dummy << "%PDF-1.4 dummy";
    dummy.close();

    std::string result = Previewer::generate_preview("dummy.pdf");
    std::cout << "Preview result:\n" << result << "\n";
    return 0;
}
