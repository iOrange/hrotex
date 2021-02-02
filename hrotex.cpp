#include <iostream>
#include <vector>
#include <string>
#include <filesystem>

namespace fs = std::filesystem;

#define scast static_cast
#define rcast reinterpret_cast


#include <Windows.h>


#pragma pack(push, 1)
struct HROTPakHeader {
    uint32_t    magic;      // "HROT"
    uint32_t    tocOffset;
    uint32_t    tocSize;
};

struct HROTTocRecord {      // 128 bytes each
    char        name[120];
    uint32_t    offset;
    uint32_t    size;
};
#pragma pack(pop)


struct MappedWinFile {
    HANDLE          hFile;
    HANDLE          hMemory;
    size_t          size;
    const uint8_t*  ptr;
};

// WinApi is weird and sometimes can return you a null handle instead of invalid
constexpr bool WinHandleValid(HANDLE h) {
    return h && INVALID_HANDLE_VALUE != h;
}

inline void WinHandleClose(HANDLE& h) {
    if (WinHandleValid(h)) {
        ::CloseHandle(h);
        h = INVALID_HANDLE_VALUE;
    }
}

static bool MapWinFile(const fs::path& path, MappedWinFile& outFile) {
    bool result = false;

    HANDLE hFile, hMemory = INVALID_HANDLE_VALUE;
    hFile = ::CreateFileW(path.wstring().c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

    if (WinHandleValid(hFile)) {
        LARGE_INTEGER length;
        ::GetFileSizeEx(hFile, &length);

        hMemory = ::CreateFileMappingW(hFile, nullptr, PAGE_READONLY, length.HighPart, length.LowPart, nullptr);
        if (WinHandleValid(hMemory)) {
            const uint8_t* data = rcast<uint8_t*>(::MapViewOfFile(hMemory, FILE_MAP_READ, 0, 0, length.QuadPart));
            if (data) {
                outFile.hFile = hFile;
                outFile.hMemory = hMemory;
                outFile.size = length.QuadPart;
                outFile.ptr = data;

                result = true;
            }
        }
    }

    if (!result) {
        WinHandleClose(hMemory);
        WinHandleClose(hFile);
    }

    return result;
}

static void UnmapWinFile(MappedWinFile& file) {
    if (file.ptr) {
        ::UnmapViewOfFile(file.ptr);
        file.ptr = nullptr;
    }

    WinHandleClose(file.hMemory);
    WinHandleClose(file.hFile);

    file.size = 0;
}

static bool DumpToWinFile(const fs::path& path, const void* data, const size_t dataLength) {
    bool result = false;

    HANDLE hFile = ::CreateFileW(path.wstring().c_str(), GENERIC_WRITE, FILE_SHARE_WRITE, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (WinHandleValid(hFile)) {
        const DWORD bytesToWrite = scast<DWORD>(dataLength);
        DWORD bytesWritten = 0;
        result = ::WriteFile(hFile, data, bytesToWrite, &bytesWritten, nullptr) && (bytesWritten == bytesToWrite);
        ::CloseHandle(hFile);
    }

    return result;
}


int wmain(int argc, wchar_t** argv) {
    fs::path inputPack(argv[1]);
    fs::path outputPath(argv[2]);

    MappedWinFile file;
    if (MapWinFile(inputPack, file)) {
        const HROTPakHeader* hdr = rcast<const HROTPakHeader*>(file.ptr);
        const HROTTocRecord* toc = rcast<const HROTTocRecord*>(file.ptr + hdr->tocOffset);

        const size_t numTocRecords = hdr->tocSize / sizeof(HROTTocRecord);
        for (size_t i = 0; i < numTocRecords; ++i) {
            fs::path fileName(toc[i].name);

            std::wcout << L"Extracting " << fileName << ",  size = " << toc[i].size << " bytes" << std::endl;
            DumpToWinFile(outputPath / fileName, file.ptr + toc[i].offset, toc[i].size);
        }

        UnmapWinFile(file);
    }

    return 0;
}
