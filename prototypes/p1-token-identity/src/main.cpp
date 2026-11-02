// Builds the token, edits its diameter, and prints the rebuild report.
// Usage: p1_token [output.stl]
#include "token.hpp"

#include <BRepMesh_IncrementalMesh.hxx>
#include <StlAPI_Writer.hxx>

#include <iomanip>
#include <iostream>

using namespace meshify::p1;

static void printReport(const char* title, const BuildResult& r) {
    std::cout << "== " << title << " (diameter " << r.params.diameter << " mm)\n";
    for (const auto& s : r.steps) {
        std::cout << "  " << std::left << std::setw(14) << s.step << std::setw(12) << toString(s.status)
                  << s.message << "\n";
    }
    std::cout << "  valid solid: " << (r.valid ? "yes" : "no") << ", volume " << std::fixed
              << std::setprecision(2) << r.volume << " mm^3, named faces "
              << r.model.faces.size() << ", unnamed faces " << r.model.unnamedFaceCount() << "\n";
}

int main(int argc, char** argv) {
    TokenDocument doc(TokenParams{});
    printReport("Version 0", doc.current());

    TokenParams smaller = doc.current().params;
    smaller.diameter = 30.0;
    printReport("Version 1: diameter 40 -> 30", doc.edit(smaller));

    TokenParams noRecess = smaller;
    noRecess.recessEnabled = false;
    printReport("Version 2: recess suppressed", doc.edit(noRecess));

    doc.restore(0);
    printReport("Restored version 0", doc.current());

    if (argc > 1) {
        // 0.01 mm chord tolerance, matching the brief's example export setting.
        BRepMesh_IncrementalMesh mesh(doc.current().model.shape, 0.01, false, 0.1);
        StlAPI_Writer writer;
        writer.ASCIIMode() = false;
        if (!writer.Write(doc.current().model.shape, argv[1])) {
            std::cerr << "could not write " << argv[1] << "\n";
            return 1;
        }
        std::cout << "wrote " << argv[1] << "\n";
    }
    return 0;
}
