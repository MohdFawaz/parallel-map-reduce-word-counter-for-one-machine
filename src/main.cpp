//This code implements corrected mutexed prints so they show up in the right order and format
//reserve size was changed and it fixed the issue, I changed it from 1 million to 4 million


// src/main.cpp

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <thread>
#include <unordered_map>
#include <mutex>
#include <functional>  // for std::hash
#include <algorithm>   // for std::min, std::sort
#include <cctype>      // for std::isalpha, std::tolower
#include <chrono>      // for timing
//#include <locale>      // for Unicode locale support delete
static std::mutex ioMutex;  

// ————————————————————————————————————————————————————————
// 1. Map phase: count words in [startLine, endLine)
//    now skips digits, keeps Finnish letters and hyphens
// ————————————————————————————————————————————————————————
void countWordsInChunk(
    const std::vector<std::string>& allLines,
    std::size_t startLine,
    std::size_t endLine,
    std::unordered_map<std::string, std::size_t>& localCounts)
{
    {
    std::lock_guard<std::mutex> lg(ioMutex);
    auto tid = std::this_thread::get_id();
    std::cout << "[Map] thread " << tid
          << " handling lines " << startLine
          << "–" << endLine << "\n";
    }
    for (std::size_t i = startLine; i < endLine; ++i) {
        const std::string& line = allLines[i];
        std::string word;
        for (char ch : line) {
            unsigned char uch = static_cast<unsigned char>(ch);
            if ((std::isalpha(uch) || uch >= 0x80)  // only letters (ASCII or Finnish)
                && ch != '-'                        // no hyphens
               // && uch != 0xA0                       // no NBSP     //delete
                && !std::isspace(uch)               // no spaces
            ) {
                word += ch;
            }
            else if (!word.empty()) {
                localCounts[word]++;
                word.clear();
            }
        }
        if (!word.empty()) {
            localCounts[word]++;
        }
    }
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: wordcount <input_filename>\n";
        return 1;
    }

    // enable Unicode-aware classification (e.g. NBSP as space)
    //std::locale::global(std::locale(""));                       //delete

    // decide number of threads for map + merge
    unsigned int threadCount = std::thread::hardware_concurrency();
    // unsigned int threadCount = 4;
    if (threadCount == 0) threadCount = 1;

    // start total timer
    auto totalStart = std::chrono::high_resolution_clock::now();
    // start map timer
    auto mapStart = std::chrono::high_resolution_clock::now();

    // ————————————————————————————————————————————————————————
    // Read & process file in batches of BATCH_SIZE lines
    // ————————————————————————————————————————————————————————
    const size_t BATCH_SIZE = 100000;  // lines per batch
    std::ifstream inputFile(argv[1]);
    if (!inputFile) {
        std::cerr << "Error opening file: " << argv[1] << "\n";
        return 1;
    }

    // prepare per-thread local maps and reserve
    std::vector<std::unordered_map<std::string, std::size_t>> perThreadCounts(threadCount);
    for (auto& m : perThreadCounts)
        m.reserve(BATCH_SIZE / 10);

    // prepare globalCounts + merge infrastructure
    std::unordered_map<std::string, std::size_t> globalCounts;
    globalCounts.reserve(4'000'000);  // estimate unique words
    unsigned int stripeCount = threadCount;
    std::vector<std::mutex> stripeLocks(stripeCount);

    // merge worker for parallel merge into globalCounts
    auto mergeWorker = [&](unsigned int workerId) {
        {
        std::lock_guard<std::mutex> lg(ioMutex);
        auto tid = std::this_thread::get_id();
        std::cout << "[Merge] worker " << workerId
                  << " (thread " << tid << ") starting\n";
        }
        for (unsigned int i = 0; i < perThreadCounts.size(); ++i) {
            if (i % threadCount != workerId) continue;
            for (auto const& kv : perThreadCounts[i]) {
                std::size_t h = std::hash<std::string>{}(kv.first);
                unsigned int idx = h % stripeCount;
                std::lock_guard<std::mutex> guard(stripeLocks[idx]);
                globalCounts[kv.first] += kv.second;
            }
        }
    };

    std::vector<std::string> batch;
    batch.reserve(BATCH_SIZE);
    std::string line;
    std::vector<std::thread> mapWorkers;

    // streaming batches
    while (std::getline(inputFile, line)) {
        batch.push_back(std::move(line));
        if (batch.size() == BATCH_SIZE) {
            // Map phase on this batch
            std::size_t totalLines = batch.size();
            std::size_t linesPerThread = (totalLines + threadCount - 1) / threadCount;

            for (auto& m : perThreadCounts) {
                m.clear();
                m.reserve(linesPerThread / 10);
            }
            mapWorkers.clear();
            for (unsigned int t = 0; t < threadCount; ++t) {
                std::size_t start = t * linesPerThread;
                std::size_t end   = std::min(start + linesPerThread, totalLines);
                if (start >= end) break;
                mapWorkers.emplace_back(
                    countWordsInChunk,
                    std::cref(batch),
                    start, end,
                    std::ref(perThreadCounts[t])
                );
            }
            for (auto& w : mapWorkers) w.join();
            batch.clear();

            // Parallel Merge phase for this batch
            std::vector<std::thread> mergeWorkers;
            for (unsigned int w = 0; w < threadCount; ++w)
                mergeWorkers.emplace_back(mergeWorker, w);
            for (auto& w : mergeWorkers) w.join();
        }
    }

    // leftover batch
    if (!batch.empty()) {
        std::size_t totalLines = batch.size();
        std::size_t linesPerThread = (totalLines + threadCount - 1) / threadCount;
        for (auto& m : perThreadCounts) {
            m.clear();
            m.reserve(linesPerThread / 10);
        }
        mapWorkers.clear();
        for (unsigned int t = 0; t < threadCount; ++t) {
            std::size_t start = t * linesPerThread;
            std::size_t end   = std::min(start + linesPerThread, totalLines);
            if (start >= end) break;
            mapWorkers.emplace_back(
                countWordsInChunk,
                std::cref(batch),
                start, end,
                std::ref(perThreadCounts[t])
            );
        }
        for (auto& w : mapWorkers) w.join();

        std::vector<std::thread> mergeWorkers;
        for (unsigned int w = 0; w < threadCount; ++w)
            mergeWorkers.emplace_back(mergeWorker, w);
        for (auto& w : mergeWorkers) w.join();
    }

    inputFile.close();
    // end map timer
    auto mapEnd = std::chrono::high_resolution_clock::now();

    // ————————————————————————————————————————————————————————
    // 6. Sort alphabetically and write final output
    // ————————————————————————————————————————————————————————
    std::vector<std::pair<std::string, std::size_t>> sortedWords;
    sortedWords.reserve(globalCounts.size());
    for (auto const& kv : globalCounts)
        sortedWords.emplace_back(kv.first, kv.second);
    std::sort(sortedWords.begin(), sortedWords.end(),
        [](auto const& a, auto const& b){ return a.first < b.first; });

    std::ofstream outputFile("output.txt");
    if (!outputFile) {
        std::cerr << "Error: could not open output.txt for writing\n";
        return 1;
    }
    outputFile << "=== Final Word Counts (A → Z) ===\n";
    for (auto const& p : sortedWords)
        outputFile << p.first << " -> " << p.second << "\n";
    outputFile.close();

    // ————————————————————————————————————————————————————————
    // 7. Report timings
    // ————————————————————————————————————————————————————————
    auto totalEnd = std::chrono::high_resolution_clock::now();
    auto mapUs = std::chrono::duration_cast<std::chrono::microseconds>(mapEnd - mapStart).count();
    auto totUs = std::chrono::duration_cast<std::chrono::microseconds>(totalEnd - totalStart).count();

    std::cout << "\n--- Timing (µs) ---\n"
              << "Map:   " << mapUs   << "\n"
              << "Total: " << totUs   << "\n";

    return 0;
}
