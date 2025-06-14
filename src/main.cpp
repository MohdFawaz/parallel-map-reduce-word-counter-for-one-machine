//////Instrumenting our pipeline with wall clock timers and adding comments

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
#include <cctype>      // for std::isalnum, std::tolower
#include <chrono>      // for timing

// ————————————————————————————————————————————————————————
// 1. Map phase: count words in [startLine, endLine)
// ————————————————————————————————————————————————————————
void countWordsInChunk(
    const std::vector<std::string>& allLines,
    std::size_t startLine,
    std::size_t endLine,
    std::unordered_map<std::string, std::size_t>& localCounts)
{
    // Go through each line in our chunk
    for (std::size_t i = startLine; i < endLine; ++i) {
        const std::string& line = allLines[i];
        std::string word;

        // Build words character by character
        for (char ch : line) {
            if (std::isalnum(static_cast<unsigned char>(ch))) {
                // Add letter/digit to current word (lowercased)
                word += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            }
            else if (!word.empty()) {
                // Non‐alnum ends a word: count it, then clear
                localCounts[word] += 1;
                word.clear();
            }
        }
        // Last word on the line (if any)
        if (!word.empty()) {
            localCounts[word] += 1;
        }
    }
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: wordcount <input_filename>\n";
        return 1;
    }

    // ————————————————————————————————————————————————————————
    // 2. Read entire file into memory
    // ————————————————————————————————————————————————————————
    std::ifstream inputFile(argv[1]);
    if (!inputFile.is_open()) {
        std::cerr << "Error opening file: " << argv[1] << "\n";
        return 1;
    }
    std::vector<std::string> lines;
    for (std::string line; std::getline(inputFile, line); ) {
        lines.push_back(std::move(line));
    }
    inputFile.close();

    // ————————————————————————————————————————————————————————
    // 3. Decide number of threads and chunk size
    // ————————————————————————————————————————————————————————
    unsigned int threadCount = std::thread::hardware_concurrency();
    if (threadCount == 0) threadCount = 1;

    std::size_t totalLines     = lines.size();
    std::size_t linesPerThread = (totalLines + threadCount - 1) / threadCount;

    // ————————————————————————————————————————————————————————
    // Start total timer
    // ————————————————————————————————————————————————————————
    auto totalStart = std::chrono::high_resolution_clock::now();

    // ————————————————————————————————————————————————————————
    // 4. Launch map threads and time the Map phase
    // ————————————————————————————————————————————————————————
    std::vector<std::unordered_map<std::string, std::size_t>> perThreadCounts(threadCount);
    std::vector<std::thread> mapWorkers;

    auto mapStart = std::chrono::high_resolution_clock::now();
    for (unsigned int t = 0; t < threadCount; ++t) {
        std::size_t start = t * linesPerThread;
        std::size_t end   = std::min(start + linesPerThread, totalLines);
        if (start >= end) break;
        mapWorkers.emplace_back(
            countWordsInChunk,
            std::cref(lines),
            start,
            end,
            std::ref(perThreadCounts[t])
        );
    }
    // Wait for map threads to finish
    for (auto& w : mapWorkers) {
        w.join();
    }
    auto mapEnd = std::chrono::high_resolution_clock::now();

    // ————————————————————————————————————————————————————————
    // 5. Parallel Merge (Shuffle & Reduce) and time it
    // ————————————————————————————————————————————————————————
    std::unordered_map<std::string, std::size_t> globalCounts;
    unsigned int mergeThreadCount = threadCount;
    unsigned int stripeCount      = mergeThreadCount;

    // Prepare fine‐grained locks (one per stripe)
    std::vector<std::mutex> stripeLocks(stripeCount);

    auto mergeStart = std::chrono::high_resolution_clock::now();
    // Worker lambda does the merging
    auto mergeWorker = [&](unsigned int workerId) {
        for (unsigned int i = 0; i < perThreadCounts.size(); ++i) {
            if (i % mergeThreadCount != workerId) continue;
            for (const auto& kv : perThreadCounts[i]) {
                const std::string& word = kv.first;
                std::size_t cnt         = kv.second;
                // Pick a stripe by hashing the word
                std::size_t h = std::hash<std::string>{}(word);
                unsigned int stripeIndex = h % stripeCount;
                // Lock just that stripe, update global count
                std::lock_guard<std::mutex> guard(stripeLocks[stripeIndex]);
                globalCounts[word] += cnt;
            }
        }
    };

    // Launch merge threads
    std::vector<std::thread> mergeWorkers;
    for (unsigned int w = 0; w < mergeThreadCount; ++w) {
        mergeWorkers.emplace_back(mergeWorker, w);
    }
    for (auto& w : mergeWorkers) {
        w.join();
    }
    auto mergeEnd = std::chrono::high_resolution_clock::now();

    // ————————————————————————————————————————————————————————
    // 6. Sort alphabetically and print results
    // ————————————————————————————————————————————————————————
    std::vector<std::pair<std::string, std::size_t>> sortedWords;
    sortedWords.reserve(globalCounts.size());
    for (const auto& kv : globalCounts) {
        sortedWords.emplace_back(kv.first, kv.second);
    }
    std::sort(
        sortedWords.begin(),
        sortedWords.end(),
        [](auto const& a, auto const& b) {
            return a.first < b.first;
        }
    );

    std::cout << "\n=== Final Word Counts (A → Z) ===\n";
    for (auto const& p : sortedWords) {
        std::cout << p.first << " -> " << p.second << "\n";
    }

    // ————————————————————————————————————————————————————————
    // 7. Report timings in milliseconds
    // ————————————————————————————————————————————————————————
    auto totalEnd = std::chrono::high_resolution_clock::now();
    auto mapMs   = std::chrono::duration_cast<std::chrono::milliseconds>(mapEnd   - mapStart).count();
    auto mergeMs = std::chrono::duration_cast<std::chrono::milliseconds>(mergeEnd - mergeStart).count();
    auto totMs   = std::chrono::duration_cast<std::chrono::milliseconds>(totalEnd - totalStart).count();

    std::cout << "\n--- Timing (ms) ---\n"
              << "Map:   " << mapMs   << "\n"
              << "Merge: " << mergeMs << "\n"
              << "Total: " << totMs   << "\n";

    return 0;
}
