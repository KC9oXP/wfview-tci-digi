// Stand-in for TQSL in the end-to-end test: writes its arguments (one per line) and the
// ADIF file it was given to $FAKE_TQSL_LOG, prints a TQSL-style final status line on stderr
// and exits with $FAKE_TQSL_STATUS (default 0).
// Build: g++ -std=c++17 -O2 tests/fake_tqsl.cpp -o fake_tqsl   (cl /EHsc on Windows)
#define _CRT_SECURE_NO_WARNINGS
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

int main(int argc, char **argv)
{
    const char *log = std::getenv("FAKE_TQSL_LOG");
    const char *st = std::getenv("FAKE_TQSL_STATUS");
    const int status = st ? std::atoi(st) : 0;
    if (log)
    {
        std::ofstream out(log, std::ios::app | std::ios::binary); // \n, not \r\n, on Windows too
        out << "=== run\n";
        for (int i = 1; i < argc; ++i)
            out << "arg: " << argv[i] << "\n";
        if (argc > 1)
        {
            std::ifstream in(argv[argc - 1]);
            std::stringstream ss;
            ss << in.rdbuf();
            out << "file: " << ss.str() << "\n";
        }
    }
    std::fprintf(stderr, "Signing using Callsign KC9OXP, DXCC Entity UNITED STATES OF AMERICA\n");
    std::fprintf(stderr, "Final Status: %s(%d)\n", status == 0 ? "Success" : "Error", status);
    return status;
}
