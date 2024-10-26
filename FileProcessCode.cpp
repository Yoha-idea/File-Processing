#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <windows.h>
#include <iomanip>
#include <psapi.h>

// Define constants
const int NUM_THREADS = 4;

// Mutex for protecting shared word count data
std::mutex count_mutex;

// Structure for passing thread-specific data
struct ThreadArgs {
    std::string* segment;
    std::unordered_map<std::string, int>* global_map;
};

// Thread function to count word frequencies
DWORD WINAPI word_count_thread(LPVOID args) {
    auto* thread_args = static_cast<ThreadArgs*>(args);
    std::string* segment = thread_args->segment;
    auto* global_map = thread_args->global_map;
    std::string current_word;
    std::unordered_map<std::string, int> local_map;

    for (char ch : *segment) {
        if (isalpha(ch)) {
            current_word += tolower(ch);
        }
        else if (!current_word.empty()) {
            ++local_map[current_word];
            current_word.clear();
        }
    }
    if (!current_word.empty()) {
        ++local_map[current_word];
    }

    // Lock shared data and update global map
    std::lock_guard<std::mutex> guard(count_mutex);
    for (const auto& entry : local_map) {
        (*global_map)[entry.first] += entry.second;
    }

    return 0;
}

// Single-threaded word count for reference
std::unordered_map<std::string, int> word_count_single_thread(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file) {
        std::cerr << "Failed to open file: " << filepath << std::endl;
        return {};
    }
    std::unordered_map<std::string, int> word_map;
    std::string word, text;
    text.assign((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    for (char ch : text) {
        if (isalpha(ch)) {
            word += tolower(ch);
        }
        else if (!word.empty()) {
            ++word_map[word];
            word.clear();
        }
    }
    return word_map;
}

// Multi-threaded word count implementation
std::unordered_map<std::string, int> word_count_multi_thread(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file) {
        std::cerr << "Failed to open file: " << filepath << std::endl;
        return {};
    }
    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    size_t segment_size = content.length() / NUM_THREADS;
    std::vector<std::string> segments(NUM_THREADS);
    std::vector<HANDLE> threads(NUM_THREADS);
    std::unordered_map<std::string, int> total_count;
    ThreadArgs thread_args[NUM_THREADS];

    // Split content into segments
    for (int i = 0; i < NUM_THREADS; ++i) {
        segments[i] = content.substr(i * segment_size, segment_size);
        thread_args[i] = { &segments[i], &total_count };
        threads[i] = CreateThread(nullptr, 0, word_count_thread, &thread_args[i], 0, nullptr);
        if (threads[i] == nullptr) {
            std::cerr << "Failed to create thread " << i << std::endl;
            exit(1);
        }
    }

    // Wait for all threads to complete
    WaitForMultipleObjects(NUM_THREADS, threads.data(), TRUE, INFINITE);
    for (HANDLE thread : threads) {
        CloseHandle(thread);
    }

    return total_count;
}

// Function to extract top N frequent words
std::vector<std::pair<std::string, int>> top_n_frequent_words(const std::unordered_map<std::string, int>& word_map, int top_n = 10) {
    std::vector<std::pair<std::string, int>> frequency_list(word_map.begin(), word_map.end());
    std::sort(frequency_list.begin(), frequency_list.end(), [](const auto& a, const auto& b) {
        return a.second > b.second;
        });
    if (frequency_list.size() > top_n) {
        frequency_list.resize(top_n);
    }
    return frequency_list;
}

// Function to measure resource usage (CPU time, memory)
void display_resource_usage() {
    PROCESS_MEMORY_COUNTERS mem_counters;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &mem_counters, sizeof(mem_counters))) {
        std::cout << "\nResource Usage Information:\n";
        std::cout << "  Peak working set size: " << mem_counters.PeakWorkingSetSize / 1024 << " KB\n";
    }
    else {
        std::cerr << "Failed to get memory usage data.\n";
    }

    FILETIME creation_time, exit_time, kernel_time, user_time;
    if (GetProcessTimes(GetCurrentProcess(), &creation_time, &exit_time, &kernel_time, &user_time)) {
        ULARGE_INTEGER u_time;
        u_time.LowPart = user_time.dwLowDateTime;
        u_time.HighPart = user_time.dwHighDateTime;
        double user_seconds = u_time.QuadPart / 1e7;

        ULARGE_INTEGER k_time;
        k_time.LowPart = kernel_time.dwLowDateTime;
        k_time.HighPart = kernel_time.dwHighDateTime;
        double kernel_seconds = k_time.QuadPart / 1e7;

        std::cout << "  User CPU time: " << user_seconds << " seconds\n";
        std::cout << "  Kernel CPU time: " << kernel_seconds << " seconds\n";
    }
    else {
        std::cerr << "Failed to get CPU time data.\n";
    }
}

int main() {
    // Files to be processed
    std::vector<std::string> file_list = { "C:/CS 472(OP SYS)/calgary/bib", "C:/CS 472(OP SYS)/calgary/paper1", "C:/CS 472(OP SYS)/calgary/paper2", "C:/CS 472(OP SYS)/calgary/progc",
                                          "C:/CS 472(OP SYS)/calgary/progl", "C:/CS 472(OP SYS)/calgary/progp", "C:/CS 472(OP SYS)/calgary/trans" };

    // Measure time for multi-threading
    auto start_time = std::chrono::high_resolution_clock::now();
    for (const auto& filepath : file_list) {
        auto word_count = word_count_multi_thread(filepath);
        auto top_words = top_n_frequent_words(word_count);
        std::cout << "\nTop frequent words in " << filepath << ":\n";
        for (const auto& pair : top_words) {
            std::cout << "  " << std::setw(15) << std::left << pair.first << ": " << pair.second << "\n";
        }
    }
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration<double>(end_time - start_time).count();
    std::cout << "\nElapsed time for multi-threading: " << duration << " seconds\n";

    // Display resource usage
    display_resource_usage();

    return 0;
}
