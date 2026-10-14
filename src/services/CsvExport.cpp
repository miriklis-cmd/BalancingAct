// CsvExport.cpp - moved unchanged out of main.cpp in v0.10.4 (split stage 4a).
#include "CsvExport.h"

#include <algorithm>
#include <initializer_list>
#include <iomanip>
#include <map>
#include <sstream>
#include <vector>

#include "../data/AppData.h"
#include "../data/FishBalanceWin32IO.h"

// CsvField() lives in FishBalanceCore.h (unchanged logic).

bool ExportToCsv(const std::wstring& path, std::wstring* outError) {
    std::string content;
    unsigned char bom[3] = { 0xEF, 0xBB, 0xBF }; // UTF-8 BOM, so Excel reads accents/symbols correctly
    content.append(reinterpret_cast<char*>(bom), 3);

    auto writeLine = [&](const std::wstring& line) {
        content += WToUtf8(line) + "\r\n";
    };
    auto csvRow = [&](std::initializer_list<std::wstring> fields) {
        std::wstring line;
        bool first = true;
        for (auto& fld : fields) {
            if (!first) line += L",";
            line += CsvField(fld);
            first = false;
        }
        writeLine(line);
    };

    writeLine(L"Fish Balance Manager Export");
    SYSTEMTIME st;
    GetLocalTime(&st);
    std::wstringstream ss;
    ss << L"Generated " << std::setfill(L'0')
       << std::setw(4) << st.wYear << L"-" << std::setw(2) << st.wMonth << L"-" << std::setw(2) << st.wDay
       << L" " << std::setw(2) << st.wHour << L":" << std::setw(2) << st.wMinute;
    writeLine(ss.str());
    writeLine(L"");

    writeLine(L"Entries");
    csvRow({ L"Date", L"Supplier", L"Species", L"Kgs", L"Price", L"Total", L"Notes" });
    for (auto& e : g_entries)
        csvRow({ e.date, e.supplier, e.product, FormatKg(e.kgs), FormatNum(e.price), FormatNum(e.Total()), e.notes });
    writeLine(L"");

    writeLine(L"Overview by Supplier");
    csvRow({ L"Supplier", L"Kgs", L"Total" });
    {
        std::vector<std::wstring> keys;
        std::map<std::wstring, double> kgsSum, amtSum;
        for (auto& e : g_entries) {
            if (kgsSum.find(e.supplier) == kgsSum.end()) { kgsSum[e.supplier] = 0; amtSum[e.supplier] = 0; keys.push_back(e.supplier); }
            kgsSum[e.supplier] += e.kgs;
            amtSum[e.supplier] += e.Total();
        }
        std::sort(keys.begin(), keys.end());
        double gk = 0, ga = 0;
        for (auto& k : keys) { csvRow({ k, FormatKg(kgsSum[k]), FormatNum(amtSum[k]) }); gk += kgsSum[k]; ga += amtSum[k]; }
        csvRow({ L"GRAND TOTAL", FormatKg(gk), FormatNum(ga) });
    }
    writeLine(L"");

    writeLine(L"By Species (all suppliers)");
    csvRow({ L"Species", L"Kgs", L"Total", L"Avg Price", L"Highest Price", L"Lowest Price" });
    {
        std::vector<std::wstring> keys;
        std::map<std::wstring, double> kgsSum, amtSum, priceSum, priceMax, priceMin;
        std::map<std::wstring, int> priceCount;
        for (auto& e : g_entries) {
            if (kgsSum.find(e.product) == kgsSum.end()) {
                kgsSum[e.product] = 0; amtSum[e.product] = 0; priceSum[e.product] = 0; priceCount[e.product] = 0;
                priceMax[e.product] = e.price; priceMin[e.product] = e.price;
                keys.push_back(e.product);
            }
            kgsSum[e.product] += e.kgs;
            amtSum[e.product] += e.Total();
            priceSum[e.product] += e.price;
            priceCount[e.product]++;
            if (e.price > priceMax[e.product]) priceMax[e.product] = e.price;
            if (e.price < priceMin[e.product]) priceMin[e.product] = e.price;
        }
        std::sort(keys.begin(), keys.end());
        double gk = 0, ga = 0;
        for (auto& k : keys) {
            double avg = priceCount[k] > 0 ? priceSum[k] / priceCount[k] : 0;
            csvRow({ k, FormatKg(kgsSum[k]), FormatNum(amtSum[k]), FormatNum(avg), FormatNum(priceMax[k]), FormatNum(priceMin[k]) });
            gk += kgsSum[k]; ga += amtSum[k];
        }
        csvRow({ L"GRAND TOTAL", FormatKg(gk), FormatNum(ga), L"", L"", L"" });
    }
    writeLine(L"");

    writeLine(L"Breakdown (Supplier > Species > Price)");
    csvRow({ L"Supplier", L"Species", L"Price", L"Kgs", L"Total" });
    {
        auto data = BuildBreakdownData(g_entries);
        double grandKgs = 0, grandAmt = 0;
        for (auto& sg : data) {
            for (auto& pg : sg.products) {
                for (auto& pl : pg.prices)
                    csvRow({ sg.supplier, pg.species, FormatNum(pl.price), FormatKg(pl.kgs), FormatNum(pl.amt) });
                csvRow({ sg.supplier, pg.species + L" Total", L"", FormatKg(pg.totalKgs), FormatNum(pg.totalAmt) });
            }
            csvRow({ sg.supplier, L"SUPPLIER TOTAL", L"", FormatKg(sg.totalKgs), FormatNum(sg.totalAmt) });
            grandKgs += sg.totalKgs;
            grandAmt += sg.totalAmt;
        }
        csvRow({ L"", L"GRAND TOTAL", L"", FormatKg(grandKgs), FormatNum(grandAmt) });
    }

    return WriteFileAtomicUtf8(path, content, outError);
}
