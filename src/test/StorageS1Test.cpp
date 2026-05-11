#include <iostream>
#include <filesystem>
#include <cstring>
#include "storage/object/StorageCommon.h"
#include "storage/manager/FileManager.h"

using namespace storage;

bool test_createEmptyPage() {
    const auto path = std::filesystem::path("data") / "test_s1.trd";
    std::filesystem::remove(path);

    std::string page(4096, '\0');
    DataPageHeader hdr;
    hdr.pageId = 1;
    hdr.freeStart = kDataPageHeader;
    hdr.freeEnd = kDataPageSize;
    hdr.slotCount = 0;
    hdr.flags = 0;
    std::memcpy(page.data(), &hdr, sizeof(DataPageHeader));

    if (!FileManager::writePage(path, 1, page)) {
        std::cerr << "FAIL: writePage failed" << std::endl;
        return false;
    }

    std::string loaded = FileManager::readPage(path, 1);
    if (loaded.empty()) {
        std::cerr << "FAIL: readPage returned empty" << std::endl;
        return false;
    }

    DataPageHeader loadedHdr;
    std::memcpy(&loadedHdr, loaded.data(), sizeof(DataPageHeader));

    if (loadedHdr.pageId != 1)     { std::cerr << "FAIL: pageId=" << loadedHdr.pageId << std::endl; return false; }
    if (loadedHdr.slotCount != 0)  { std::cerr << "FAIL: slotCount=" << loadedHdr.slotCount << std::endl; return false; }
    if (loadedHdr.freeStart != kDataPageHeader) { std::cerr << "FAIL: freeStart=" << loadedHdr.freeStart << std::endl; return false; }
    if (loadedHdr.freeEnd != kDataPageSize)     { std::cerr << "FAIL: freeEnd=" << loadedHdr.freeEnd << std::endl; return false; }

    std::filesystem::remove(path);
    return true;
}

bool test_writeAndReadMultiplePages() {
    const auto path = std::filesystem::path("data") / "test_s1_multi.trd";
    std::filesystem::remove(path);

    for (std::uint32_t pg = 1; pg <= 3; ++pg) {
        std::string page(4096, '\0');
        DataPageHeader hdr;
        hdr.pageId = pg;
        hdr.freeStart = kDataPageHeader;
        hdr.freeEnd = kDataPageSize;
        hdr.slotCount = 0;
        std::memcpy(page.data(), &hdr, sizeof(DataPageHeader));
        if (!FileManager::writePage(path, pg, page)) {
            std::cerr << "FAIL: writePage pg=" << pg << " failed" << std::endl;
            return false;
        }
    }

    for (std::uint32_t pg = 1; pg <= 3; ++pg) {
        std::string loaded = FileManager::readPage(path, pg);
        if (loaded.empty()) {
            std::cerr << "FAIL: readPage pg=" << pg << " empty" << std::endl;
            return false;
        }
        DataPageHeader hdr;
        std::memcpy(&hdr, loaded.data(), sizeof(DataPageHeader));
        if (hdr.pageId != pg) {
            std::cerr << "FAIL: pg=" << pg << " pageId=" << hdr.pageId << std::endl;
            return false;
        }
    }

    std::filesystem::remove(path);
    return true;
}

bool test_fileSeekCorrectness() {
    const auto path = std::filesystem::path("data") / "test_s1_seek.trd";
    std::filesystem::remove(path);

    // Write page 5 directly (skipping pages 1-4)
    std::string page(4096, '\0');
    DataPageHeader hdr;
    hdr.pageId = 5;
    hdr.freeStart = 100;
    hdr.freeEnd = 4000;
    hdr.slotCount = 3;
    std::memcpy(page.data(), &hdr, sizeof(DataPageHeader));

    if (!FileManager::writePage(path, 5, page)) {
        std::cerr << "FAIL: writePage pg=5 failed" << std::endl;
        return false;
    }

    // Read page 5 back
    std::string loaded = FileManager::readPage(path, 5);
    DataPageHeader loadedHdr;
    std::memcpy(&loadedHdr, loaded.data(), sizeof(DataPageHeader));

    if (loadedHdr.pageId != 5)     { std::cerr << "FAIL seek: pageId=" << loadedHdr.pageId << std::endl; return false; }
    if (loadedHdr.freeStart != 100) { std::cerr << "FAIL seek: freeStart=" << loadedHdr.freeStart << std::endl; return false; }
    if (loadedHdr.slotCount != 3)  { std::cerr << "FAIL seek: slotCount=" << loadedHdr.slotCount << std::endl; return false; }

    // Verify pages 1-4 are empty/inaccessible
    std::string empty = FileManager::readPage(path, 1);
    if (!empty.empty()) {
        DataPageHeader ehdr;
        std::memcpy(&ehdr, empty.data(), sizeof(DataPageHeader));
        if (ehdr.pageId == 1) { std::cerr << "FAIL: page 1 should not exist" << std::endl; return false; }
    }

    std::filesystem::remove(path);
    return true;
}

int main() {
    std::filesystem::create_directories("data");
    bool ok = true;
    ok = test_createEmptyPage() && ok;
    ok = test_writeAndReadMultiplePages() && ok;
    ok = test_fileSeekCorrectness() && ok;
    if (ok) std::cout << "S1 PASSED" << std::endl;
    else    std::cout << "S1 FAILED" << std::endl;
    return ok ? 0 : 1;
}
