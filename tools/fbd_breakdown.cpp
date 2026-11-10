// Prints the app's OWN Breakdown + totals for a .fbd file (uses the real core: ParseFbdContent,
// BuildBreakdownData, ParseSumExprStrict) as tab-separated lines, for tools/compare_to_excel.py.
//   g++ -std=c++17 -I src/core tools/fbd_breakdown.cpp -o fbd_breakdown && ./fbd_breakdown file.fbd
#include "FishBalanceCore.h"
#include <fstream>
#include <iostream>
static std::string N(const std::wstring& w) { std::string s; for (wchar_t c : w) s += c < 128 ? (char)c : '?'; return s; }
int main(int argc, char** argv) {
    if (argc < 2) { std::cerr << "usage: fbd_breakdown file.fbd\n"; return 2; }
    std::ifstream in(argv[1], std::ios::binary);
    std::string raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::wstring w; for (unsigned char c : raw) w += (wchar_t)c;
    FbdLoadResult r = ParseFbdContent(w);
    if (!r.ok) { std::cerr << "LOAD FAILED line " << r.errorLine << ": " << N(r.errorMessage) << "\n"; return 1; }
    double entered = 0;
    for (auto& e : r.entries) entered += e.Total();
    for (auto& s : BuildBreakdownData(r.entries))
        for (auto& p : s.products)
            for (auto& pl : p.prices)
                std::printf("P\t%s\t%s\t%.4f\t%.4f\t%.4f\n", N(s.supplier).c_str(), N(p.species).c_str(), pl.price, pl.kgs, pl.amt);
    SumParseResult d = ParseSumExprStrict(r.debtor), c = ParseSumExprStrict(r.cash);
    std::printf("T\t%.4f\t%.4f\t%.4f\t%zu\n", d.ok ? d.value : 0.0, c.ok ? c.value : 0.0, entered, r.entries.size());
}
