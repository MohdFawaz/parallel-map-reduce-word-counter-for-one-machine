/*
mapping phase
Steps: 
Read & Store All Lines (Loads the entire text file into memory as a vector<string>.)
Determine Thread Count & Chunk Size (We split the total number of lines evenly, rounding up)
Spawn One Thread per Chunk (Each thread runs countWordsInChunk(...) on its own slice of lines)
Map Phase: Local Counting
*/



#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <thread>
#include <unordered_map>
#include <cctype>   // for std::isalnum and std::tolower
#include <algorithm> // for std::min

// Processes lines [startLine, endLine) and counts words into localCounts
void countWordsInChunk(const std::vector<std::string>& allLines,
                       std::size_t startLine,
                       std::size_t endLine,
                       std::unordered_map<std::string, std::size_t>& localCounts)
{
    for (std::size_t lineIndex = startLine; lineIndex < endLine; ++lineIndex) {
        const std::string& line = allLines[lineIndex];
        std::string currentWord;
        for (char ch : line) {
            if (std::isalnum(static_cast<unsigned char>(ch))) {
                currentWord += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            }
            else if (!currentWord.empty()) {
                localCounts[currentWord] += 1;
                currentWord.clear();
            }
        }
        if (!currentWord.empty()) {
            localCounts[currentWord] += 1;
        }
    }
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: wordcount <input_filename>\n";
        return 1;
    }

    // 1. Read file into memory
    std::ifstream inputFile(argv[1]);
    if (!inputFile.is_open()) {
        std::cerr << "Error opening file: " << argv[1] << "\n";
        return 1;
    }
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(inputFile, line)) {
        lines.push_back(line);
    }
    inputFile.close();

    // 2. Determine number of threads and chunk size
    unsigned int threadCount = std::thread::hardware_concurrency();
    if (threadCount == 0) threadCount = 1;
    std::size_t totalLines = lines.size();
    std::size_t linesPerThread = (totalLines + threadCount - 1) / threadCount;

    // 3. Prepare per-thread maps and launch threads
    std::vector<std::unordered_map<std::string, std::size_t>> perThreadCounts(threadCount);
    std::vector<std::thread> workers;
    for (unsigned int t = 0; t < threadCount; ++t) {
        std::size_t start = t * linesPerThread;
        std::size_t end   = std::min(start + linesPerThread, totalLines);
        if (start >= end) break;
        workers.emplace_back(
            countWordsInChunk,
            std::cref(lines),
            start,
            end,
            std::ref(perThreadCounts[t])
        );
    }

    // 4. Wait for all threads to finish
    for (auto& worker : workers) {
        worker.join();
    }

    // Debug: print each thread's local counts
    for (std::size_t t = 0; t < perThreadCounts.size(); ++t) {
        std::cout << "Thread " << t << " counted:\n";
        for (auto& pair : perThreadCounts[t]) {
            std::cout << "  " << pair.first << " -> " << pair.second << "\n";
        }
        std::cout << "-----------------\n";
    }

    return 0;
}
