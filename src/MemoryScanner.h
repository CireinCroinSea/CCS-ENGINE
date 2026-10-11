#pragma once

#include <windows.h>
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

enum class ValueType { Byte, Int16, Int32, Int64, Float, Double };
enum class ScanMode { Exact, Changed, Unchanged, Increased, Decreased };

struct ScanResult {
    uintptr_t address{};
    uint64_t value{};
};

class MemoryScanner {
public:
    static std::vector<ScanResult> ScanFirst(DWORD pid, ValueType type, const std::wstring& input,
        size_t maxResults, const std::atomic_bool* cancelFlag, std::wstring& error);
    static std::vector<ScanResult> ScanNext(DWORD pid, ValueType type,
        const std::vector<ScanResult>& previous, ScanMode mode, const std::wstring& input,
        size_t maxResults, const std::atomic_bool* cancelFlag, std::wstring& error);
    static std::vector<bool> WriteValues(DWORD pid, ValueType type,
        const std::vector<uintptr_t>& addresses, const std::wstring& input,
        uint64_t& writtenValue, std::vector<uint64_t>& previousValues,
        std::vector<bool>& previousValuesRead, std::wstring& error);
    static std::vector<uintptr_t> FindDirectPointers(DWORD pid, uintptr_t target,
        size_t maxResults, const std::atomic_bool* cancelFlag, std::wstring& error);
    static std::vector<uintptr_t> ScanBytePattern(DWORD pid, const std::vector<int>& pattern,
        size_t maxResults, const std::atomic_bool* cancelFlag, std::wstring& error);
    static bool ReadMemory(DWORD pid, uintptr_t address, size_t size,
        std::vector<unsigned char>& bytes, std::wstring& error);
    static bool WriteMemory(DWORD pid, uintptr_t address, const std::vector<unsigned char>& bytes,
        std::vector<unsigned char>& oldBytes, std::wstring& error);
};

