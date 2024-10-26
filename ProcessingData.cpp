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

// Multi-threaded word count implementation for each file
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

// Simulate the child process functionality for multiprocessing
int simulate_child_process(const std::string& filepath, HANDLE pipe_write) {
    auto word_count = word_count_multi_thread(filepath);
    int count = word_count.size();

    // Write the word count to the pipe
    DWORD bytes_written;
    if (!WriteFile(pipe_write, &count, sizeof(count), &bytes_written, nullptr) || bytes_written != sizeof(count)) {
        std::cerr << "Failed to write to pipe in child process.\n";
        return 1;
    }

    return count;
}

// Function to process files with multiprocessing
int process_with_multiprocessing(const std::vector<std::string>& file_list) {
    HANDLE pipe_read, pipe_write;
    SECURITY_ATTRIBUTES sa = { sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE };
    if (!CreatePipe(&pipe_read, &pipe_write, &sa, 0)) {
        std::cerr << "Failed to create pipe.\n";
        exit(1);
    }

    int total_word_count = 0;
    std::cout << "[Multiprocessing]\n";
    for (const auto& filepath : file_list) {
        // Create a separate thread to simulate a child process
        auto child_thread = CreateThread(nullptr, 0, [](LPVOID lp_param) -> DWORD {
            auto* args = static_cast<std::pair<std::string, HANDLE>*>(lp_param);
            return simulate_child_process(args->first, args->second);
            }, new std::pair<std::string, HANDLE>(filepath, pipe_write), 0, nullptr);

        if (child_thread == nullptr) {
            std::cerr << "Failed to create child thread for file: " << filepath << "\n";
            exit(1);
        }

        // Wait for the "child" thread to finish
        WaitForSingleObject(child_thread, INFINITE);
        CloseHandle(child_thread);

        // Read the word count data from the pipe
        DWORD bytes_read;
        int word_count;
        if (!ReadFile(pipe_read, &word_count, sizeof(word_count), &bytes_read, nullptr) || bytes_read != sizeof(word_count)) {
            std::cerr << "Failed to read from pipe.\n";
            exit(1);
        }
        std::cout << "Word count for " << filepath << ": " << word_count << "\n";
        total_word_count += word_count;
    }

    CloseHandle(pipe_read);
    CloseHandle(pipe_write);

    return total_word_count;
}

// Function to measure performance of multithreading for a list of files
int process_with_multithreading(const std::vector<std::string>& file_list) {
    int total_word_count = 0;
    std::cout << "[Multithreading]\n";
    for (const auto& filepath : file_list) {
        auto word_count = word_count_multi_thread(filepath);
        int file_word_count = word_count.size();
        std::cout << "Word count for " << filepath << ": " << file_word_count << "\n";
        total_word_count += file_word_count;
    }
    return total_word_count;
}

// Function to display resource usage
void display_resource_usage(const std::string& label) {
    PROCESS_MEMORY_COUNTERS mem_counters;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &mem_counters, sizeof(mem_counters))) {
        std::cout << "[" << label << " Resource Usage]\n";
        std::cout << "Peak working set size: " << mem_counters.PeakWorkingSetSize / 1024 << " KB\n";
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

        std::cout << "User CPU time: " << user_seconds << " seconds\n";
        std::cout << "Kernel CPU time: " << kernel_seconds << " seconds\n";
    }
    else {
        std::cerr << "Failed to get CPU time data.\n";
    }
}

// Main function to compare multithreading vs multiprocessing
int main() {
    std::vector<std::string> file_list = { "C:/CS 472(OP SYS)/calgary/bib", "C:/CS 472(OP SYS)/calgary/paper1",
                                          "C:/CS 472(OP SYS)/calgary/paper2", "C:/CS 472(OP SYS)/calgary/progc",
                                          "C:/CS 472(OP SYS)/calgary/progl", "C:/CS 472(OP SYS)/calgary/progp",
                                          "C:/CS 472(OP SYS)/calgary/trans" };

    // Measure time for multithreading
    auto start_time = std::chrono::high_resolution_clock::now();
    int total_word_count_multithreading = process_with_multithreading(file_list);
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration_multithreading = std::chrono::duration<double>(end_time - start_time).count();
    std::cout << "\nElapsed time for multithreading: " << duration_multithreading << " seconds\n";
    display_resource_usage("Multithreading");

    // Measure time for multiprocessing
    start_time = std::chrono::high_resolution_clock::now();
    int total_word_count_multiprocessing = process_with_multiprocessing(file_list);
    end_time = std::chrono::high_resolution_clock::now();
    auto duration_multiprocessing = std::chrono::duration<double>(end_time - start_time).count();
    std::cout << "\nElapsed time for multiprocessing: " << duration_multiprocessing << " seconds\n";
    display_resource_usage("Multiprocessing");

    // Sum of elapsed times
    double total_elapsed_time = duration_multithreading + duration_multiprocessing;
    std::cout << "\nTotal elapsed time (multithreading + multiprocessing): " << total_elapsed_time << " seconds\n";

    // Total word count (should be the same for both approaches)
    int combined_word_count = total_word_count_multithreading; // or total_word_count_multiprocessing
    std::cout << "Total word count for all files combined: " << combined_word_count << "\n";

    return 0;
}