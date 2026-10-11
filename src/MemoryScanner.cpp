#include "MemoryScanner.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <cwchar>
#include <limits>

namespace {
SIZE_T ValueSize(ValueType type) {
    switch (type) {
    case ValueType::Byte: return sizeof(uint8_t);
    case ValueType::Int16: return sizeof(int16_t);
    case ValueType::Int32: case ValueType::Float: return sizeof(uint32_t);
    case ValueType::Int64: case ValueType::Double: return sizeof(uint64_t);
    }
    return 0;
}

bool ParseValue(ValueType type, const std::wstring& text, unsigned char* out) {
    wchar_t* end = nullptr;
    errno = 0;
    switch (type) {
    case ValueType::Byte: {
        const unsigned long long value = std::wcstoull(text.c_str(), &end, 0);
        if (errno || end == text.c_str() || *end || value > UINT8_MAX) return false;
        const uint8_t v = static_cast<uint8_t>(value);
        std::memcpy(out, &v, sizeof(v)); return true;
    }
    case ValueType::Int16: {
        const long long value = std::wcstoll(text.c_str(), &end, 0);
        if (errno || end == text.c_str() || *end || value < INT16_MIN || value > INT16_MAX) return false;
        const int16_t v = static_cast<int16_t>(value);
        std::memcpy(out, &v, sizeof(v)); return true;
    }
    case ValueType::Int32: {
        const long long value = std::wcstoll(text.c_str(), &end, 0);
        if (errno || end == text.c_str() || *end || value < INT32_MIN || value > INT32_MAX) return false;
        const int32_t v = static_cast<int32_t>(value);
        std::memcpy(out, &v, sizeof(v)); return true;
    }
    case ValueType::Int64: {
        const long long value = std::wcstoll(text.c_str(), &end, 0);
        if (errno || end == text.c_str() || *end) return false;
        const int64_t v = static_cast<int64_t>(value);
        std::memcpy(out, &v, sizeof(v)); return true;
    }
    case ValueType::Float: {
        const float value = std::wcstof(text.c_str(), &end);
        if (errno || end == text.c_str() || *end || !std::isfinite(value)) return false;
        std::memcpy(out, &value, sizeof(value)); return true;
    }
    case ValueType::Double: {
        const double value = std::wcstod(text.c_str(), &end);
        if (errno || end == text.c_str() || *end || !std::isfinite(value)) return false;
        std::memcpy(out, &value, sizeof(value)); return true;
    }
    }
    return false;
}

uint64_t PackValue(ValueType type, const unsigned char* bytes) {
    uint64_t value = 0;
    std::memcpy(&value, bytes, ValueSize(type));
    return value;
}

bool IsReadable(const MEMORY_BASIC_INFORMATION& mbi) {
    if (mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return false;
    const DWORD protection = mbi.Protect & 0xff;
    return protection == PAGE_READONLY || protection == PAGE_READWRITE ||
        protection == PAGE_WRITECOPY || protection == PAGE_EXECUTE_READ ||
        protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY;
}

bool IsWritable(const MEMORY_BASIC_INFORMATION& mbi) {
    if (mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return false;
    const DWORD protection = mbi.Protect & 0xff;
    return protection == PAGE_READWRITE || protection == PAGE_WRITECOPY ||
        protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY;
}

bool IsCancelled(const std::atomic_bool* flag) {
    return flag && flag->load(std::memory_order_relaxed);
}

bool IsGreater(ValueType type, uint64_t current, uint64_t old) {
    switch (type) {
    case ValueType::Byte: return static_cast<uint8_t>(current) > static_cast<uint8_t>(old);
    case ValueType::Int16: return static_cast<int16_t>(current) > static_cast<int16_t>(old);
    case ValueType::Int32: return static_cast<int32_t>(current) > static_cast<int32_t>(old);
    case ValueType::Int64: return static_cast<int64_t>(current) > static_cast<int64_t>(old);
    case ValueType::Float: {
        float a{}, b{}; const uint32_t x = static_cast<uint32_t>(current), y = static_cast<uint32_t>(old);
        std::memcpy(&a, &x, sizeof(a)); std::memcpy(&b, &y, sizeof(b));
        return std::isfinite(a) && std::isfinite(b) && a > b;
    }
    case ValueType::Double: {
        double a{}, b{}; std::memcpy(&a, &current, sizeof(a)); std::memcpy(&b, &old, sizeof(b));
        return std::isfinite(a) && std::isfinite(b) && a > b;
    }
    }
    return false;
}

bool Matches(ValueType type, ScanMode mode, uint64_t current, uint64_t previous,
             uint64_t exactValue) {
    switch (mode) {
    case ScanMode::Exact: return current == exactValue;
    case ScanMode::Changed: return current != previous;
    case ScanMode::Unchanged: return current == previous;
    case ScanMode::Increased: return IsGreater(type, current, previous);
    case ScanMode::Decreased: return IsGreater(type, previous, current);
    }
    return false;
}
}

std::vector<ScanResult> MemoryScanner::ScanFirst(DWORD pid, ValueType type,
    const std::wstring& input, size_t maxResults, const std::atomic_bool* cancelFlag,
    std::wstring& error) {
    std::vector<ScanResult> results;
    const SIZE_T valueSize = ValueSize(type);
    unsigned char wanted[sizeof(uint64_t)]{};
    if (!valueSize || !ParseValue(type, input, wanted)) {
        error = L"Enter a valid value for the selected type.";
        return results;
    }

    HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!process) {
        error = L"Could not open this process for reading. Check access permissions.";
        return results;
    }

    SYSTEM_INFO systemInfo{};
    GetSystemInfo(&systemInfo);
    uintptr_t address = reinterpret_cast<uintptr_t>(systemInfo.lpMinimumApplicationAddress);
    const uintptr_t maxAddress = reinterpret_cast<uintptr_t>(systemInfo.lpMaximumApplicationAddress);
    std::vector<unsigned char> buffer(1024 * 1024);
    MEMORY_BASIC_INFORMATION mbi{};

    while (address < maxAddress && results.size() < maxResults && !IsCancelled(cancelFlag)) {
        if (!VirtualQueryEx(process, reinterpret_cast<LPCVOID>(address), &mbi, sizeof(mbi))) break;
        const uintptr_t base = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
        if (mbi.RegionSize > std::numeric_limits<uintptr_t>::max() - base) break;
        const uintptr_t regionEnd = base + mbi.RegionSize;
        if (regionEnd <= address) break;

        if (IsReadable(mbi)) {
            uintptr_t cursor = base;
            while (cursor < regionEnd && results.size() < maxResults && !IsCancelled(cancelFlag)) {
                const SIZE_T request = static_cast<SIZE_T>(std::min<uintptr_t>(buffer.size(), regionEnd - cursor));
                SIZE_T bytesRead = 0;
                ReadProcessMemory(process, reinterpret_cast<LPCVOID>(cursor), buffer.data(), request, &bytesRead);
                for (SIZE_T offset = 0; offset + valueSize <= bytesRead && results.size() < maxResults;
                     offset += valueSize) {
                    if (std::memcmp(buffer.data() + offset, wanted, valueSize) == 0) {
                        results.push_back({cursor + offset, PackValue(type, buffer.data() + offset)});
                    }
                }
                if (!bytesRead) break;
                cursor += bytesRead;
            }
        }
        address = regionEnd;
    }
    CloseHandle(process);
    if (IsCancelled(cancelFlag)) error = L"Scan cancelled.";
    return results;
}

std::vector<ScanResult> MemoryScanner::ScanNext(DWORD pid, ValueType type,
    const std::vector<ScanResult>& previous, ScanMode mode, const std::wstring& input,
    size_t maxResults, const std::atomic_bool* cancelFlag, std::wstring& error) {
    std::vector<ScanResult> results;
    uint64_t exactValue = 0;
    unsigned char wanted[sizeof(uint64_t)]{};
    const SIZE_T valueSize = ValueSize(type);
    if (!valueSize || (mode == ScanMode::Exact && !ParseValue(type, input, wanted))) {
        error = L"Enter a valid value for the selected type.";
        return results;
    }
    exactValue = PackValue(type, wanted);

    HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!process) {
        error = L"Could not open this process for reading. Check access permissions.";
        return results;
    }
    results.reserve((std::min)(previous.size(), maxResults));
    for (size_t i = 0; i < previous.size() && results.size() < maxResults; ++i) {
        if ((i & 0xff) == 0 && IsCancelled(cancelFlag)) break;
        unsigned char bytes[sizeof(uint64_t)]{};
        SIZE_T bytesRead = 0;
        if (!ReadProcessMemory(process, reinterpret_cast<LPCVOID>(previous[i].address),
                               bytes, valueSize, &bytesRead) || bytesRead != valueSize) continue;
        const uint64_t current = PackValue(type, bytes);
        if (Matches(type, mode, current, previous[i].value, exactValue)) {
            results.push_back({previous[i].address, current});
        }
    }
    CloseHandle(process);
    if (IsCancelled(cancelFlag)) error = L"Scan cancelled.";
    return results;
}

std::vector<bool> MemoryScanner::WriteValues(DWORD pid, ValueType type,
    const std::vector<uintptr_t>& addresses, const std::wstring& input,
    uint64_t& writtenValue, std::vector<uint64_t>& previousValues,
    std::vector<bool>& previousValuesRead, std::wstring& error) {
    std::vector<bool> succeeded(addresses.size(), false);
    previousValues.assign(addresses.size(), 0);
    previousValuesRead.assign(addresses.size(), false);
    unsigned char value[sizeof(uint64_t)]{};
    const SIZE_T valueSize = ValueSize(type);
    if (!valueSize || !ParseValue(type, input, value)) {
        error = L"Enter a valid value for the selected type.";
        return succeeded;
    }
    writtenValue = PackValue(type, value);

    HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION | PROCESS_VM_WRITE,
        FALSE, pid);
    if (!process) {
        error = L"Could not open this process for writing. Windows denied access.";
        return succeeded;
    }
    HANDLE readProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);

    for (size_t i = 0; i < addresses.size(); ++i) {
        MEMORY_BASIC_INFORMATION mbi{};
        if (!VirtualQueryEx(process, reinterpret_cast<LPCVOID>(addresses[i]), &mbi, sizeof(mbi)) ||
            !IsWritable(mbi)) continue;
        const uintptr_t base = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
        if (addresses[i] < base || mbi.RegionSize > std::numeric_limits<uintptr_t>::max() - base ||
            valueSize > base + mbi.RegionSize - addresses[i]) continue;
        unsigned char oldBytes[sizeof(uint64_t)]{};
        SIZE_T bytesRead = 0;
        if (readProcess && ReadProcessMemory(readProcess, reinterpret_cast<LPCVOID>(addresses[i]),
                oldBytes, valueSize, &bytesRead) && bytesRead == valueSize) {
            previousValues[i] = PackValue(type, oldBytes);
            previousValuesRead[i] = true;
        }
        SIZE_T bytesWritten = 0;
        succeeded[i] = WriteProcessMemory(process, reinterpret_cast<LPVOID>(addresses[i]),
            value, valueSize, &bytesWritten) && bytesWritten == valueSize;
    }
    if (readProcess) CloseHandle(readProcess);
    CloseHandle(process);
    return succeeded;
}

std::vector<uintptr_t> MemoryScanner::FindDirectPointers(DWORD pid, uintptr_t target,
    size_t maxResults, const std::atomic_bool* cancelFlag, std::wstring& error) {
    std::vector<uintptr_t> results;
    HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!process) {
        error = L"Could not open this process for reading. Check access permissions.";
        return results;
    }
    SYSTEM_INFO systemInfo{};
    GetSystemInfo(&systemInfo);
    uintptr_t address = reinterpret_cast<uintptr_t>(systemInfo.lpMinimumApplicationAddress);
    const uintptr_t maxAddress = reinterpret_cast<uintptr_t>(systemInfo.lpMaximumApplicationAddress);
    std::vector<unsigned char> buffer(1024 * 1024);
    MEMORY_BASIC_INFORMATION mbi{};
    while (address < maxAddress && results.size() < maxResults && !IsCancelled(cancelFlag)) {
        if (!VirtualQueryEx(process, reinterpret_cast<LPCVOID>(address), &mbi, sizeof(mbi))) break;
        const uintptr_t base = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
        if (mbi.RegionSize > std::numeric_limits<uintptr_t>::max() - base) break;
        const uintptr_t regionEnd = base + mbi.RegionSize;
        if (regionEnd <= address) break;
        if (IsReadable(mbi)) {
            uintptr_t cursor = base;
            while (cursor < regionEnd && results.size() < maxResults && !IsCancelled(cancelFlag)) {
                const SIZE_T request = static_cast<SIZE_T>((std::min)(buffer.size(), regionEnd - cursor));
                SIZE_T bytesRead = 0;
                ReadProcessMemory(process, reinterpret_cast<LPCVOID>(cursor), buffer.data(), request, &bytesRead);
                if (bytesRead >= sizeof(uintptr_t)) {
                    for (SIZE_T offset = 0; offset + sizeof(uintptr_t) <= bytesRead && results.size() < maxResults; ++offset) {
                        uintptr_t candidate{};
                        std::memcpy(&candidate, buffer.data() + offset, sizeof(candidate));
                        if (candidate == target) results.push_back(cursor + offset);
                    }
                }
                if (bytesRead <= sizeof(uintptr_t) - 1) break;
                cursor += bytesRead - (sizeof(uintptr_t) - 1);
            }
        }
        address = regionEnd;
    }
    CloseHandle(process);
    if (IsCancelled(cancelFlag)) error = L"Pointer search cancelled.";
    return results;
}

std::vector<uintptr_t> MemoryScanner::ScanBytePattern(DWORD pid, const std::vector<int>& pattern,
    size_t maxResults, const std::atomic_bool* cancelFlag, std::wstring& error) {
    std::vector<uintptr_t> results;
    if (pattern.empty() || pattern.size() > 256) {
        error = L"Byte pattern must contain between 1 and 256 bytes.";
        return results;
    }
    HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!process) {
        error = L"Could not open this process for reading. Check access permissions.";
        return results;
    }
    SYSTEM_INFO systemInfo{};
    GetSystemInfo(&systemInfo);
    uintptr_t address = reinterpret_cast<uintptr_t>(systemInfo.lpMinimumApplicationAddress);
    const uintptr_t maxAddress = reinterpret_cast<uintptr_t>(systemInfo.lpMaximumApplicationAddress);
    std::vector<unsigned char> buffer(1024 * 1024);
    MEMORY_BASIC_INFORMATION mbi{};
    while (address < maxAddress && results.size() < maxResults && !IsCancelled(cancelFlag)) {
        if (!VirtualQueryEx(process, reinterpret_cast<LPCVOID>(address), &mbi, sizeof(mbi))) break;
        const uintptr_t base = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
        if (mbi.RegionSize > std::numeric_limits<uintptr_t>::max() - base) break;
        const uintptr_t regionEnd = base + mbi.RegionSize;
        if (regionEnd <= address) break;
        if (IsReadable(mbi)) {
            uintptr_t cursor = base;
            while (cursor < regionEnd && results.size() < maxResults && !IsCancelled(cancelFlag)) {
                const SIZE_T request = static_cast<SIZE_T>((std::min)(buffer.size(), regionEnd - cursor));
                SIZE_T bytesRead = 0;
                ReadProcessMemory(process, reinterpret_cast<LPCVOID>(cursor), buffer.data(), request, &bytesRead);
                if (bytesRead >= pattern.size()) {
                    const SIZE_T lastStart = bytesRead - pattern.size();
                    for (SIZE_T offset = 0; offset <= lastStart && results.size() < maxResults; ++offset) {
                        bool matches = true;
                        for (size_t i = 0; i < pattern.size(); ++i) {
                            if (pattern[i] >= 0 && buffer[offset + i] != static_cast<unsigned char>(pattern[i])) {
                                matches = false;
                                break;
                            }
                        }
                        if (matches) results.push_back(cursor + offset);
                    }
                }
                if (bytesRead <= pattern.size() - 1) break;
                cursor += bytesRead - (pattern.size() - 1);
            }
        }
        address = regionEnd;
    }
    CloseHandle(process);
    if (IsCancelled(cancelFlag)) error = L"Byte scan cancelled.";
    return results;
}

bool MemoryScanner::ReadMemory(DWORD pid, uintptr_t address, size_t size,
    std::vector<unsigned char>& bytes, std::wstring& error) {
    bytes.clear();
    if (!size || size > 4096) {
        error = L"Memory reads are limited to 4096 bytes.";
        return false;
    }
    HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!process) {
        error = L"Could not open this process for reading. Check access permissions.";
        return false;
    }
    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQueryEx(process, reinterpret_cast<LPCVOID>(address), &mbi, sizeof(mbi)) || !IsReadable(mbi)) {
        CloseHandle(process);
        error = L"The selected address is not in readable committed memory.";
        return false;
    }
    const uintptr_t base = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
    if (mbi.RegionSize > std::numeric_limits<uintptr_t>::max() - base || address < base ||
        address >= base + mbi.RegionSize ||
        size > base + mbi.RegionSize - address) {
        CloseHandle(process);
        error = L"The requested range crosses a memory region boundary.";
        return false;
    }
    bytes.resize(size);
    SIZE_T bytesRead = 0;
    const bool ok = ReadProcessMemory(process, reinterpret_cast<LPCVOID>(address), bytes.data(), size, &bytesRead) &&
        bytesRead == size;
    CloseHandle(process);
    if (!ok) {
        bytes.clear();
        error = L"Could not read the complete memory range.";
    }
    return ok;
}

bool MemoryScanner::WriteMemory(DWORD pid, uintptr_t address, const std::vector<unsigned char>& bytes,
    std::vector<unsigned char>& oldBytes, std::wstring& error) {
    oldBytes.clear();
    if (bytes.empty() || bytes.size() > 4096) {
        error = L"Memory writes are limited to 1–4096 bytes.";
        return false;
    }
    HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION | PROCESS_VM_WRITE,
        FALSE, pid);
    if (!process) {
        error = L"Could not open this process for writing. Windows denied access.";
        return false;
    }
    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQueryEx(process, reinterpret_cast<LPCVOID>(address), &mbi, sizeof(mbi)) || !IsWritable(mbi)) {
        CloseHandle(process);
        error = L"The selected address is not in writable committed memory.";
        return false;
    }
    const uintptr_t base = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
    if (mbi.RegionSize > std::numeric_limits<uintptr_t>::max() - base || address < base ||
        address >= base + mbi.RegionSize ||
        bytes.size() > base + mbi.RegionSize - address) {
        CloseHandle(process);
        error = L"The requested range crosses a memory region boundary.";
        return false;
    }
    HANDLE reader = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (reader) {
        oldBytes.resize(bytes.size());
        SIZE_T bytesRead = 0;
        if (!ReadProcessMemory(reader, reinterpret_cast<LPCVOID>(address), oldBytes.data(), bytes.size(), &bytesRead) ||
            bytesRead != bytes.size()) oldBytes.clear();
        CloseHandle(reader);
    }
    SIZE_T bytesWritten = 0;
    const bool ok = WriteProcessMemory(process, reinterpret_cast<LPVOID>(address),
        bytes.data(), bytes.size(), &bytesWritten) && bytesWritten == bytes.size();
    CloseHandle(process);
    if (!ok) error = L"Could not write the complete memory range.";
    return ok;
}

