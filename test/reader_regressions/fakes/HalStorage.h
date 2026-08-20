#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

class HalFile {
  friend class HalStorage;
  std::fstream stream;
  bool writing = false;

 public:
  HalFile() = default;
  HalFile(HalFile&&) = default;
  HalFile& operator=(HalFile&&) = default;
  HalFile(const HalFile&) = delete;
  HalFile& operator=(const HalFile&) = delete;

  int read(void* buffer, const size_t count) {
    stream.read(static_cast<char*>(buffer), static_cast<std::streamsize>(count));
    return static_cast<int>(stream.gcount());
  }
  size_t write(const void* buffer, const size_t count) {
    stream.write(static_cast<const char*>(buffer), static_cast<std::streamsize>(count));
    return stream ? count : 0;
  }
  bool seekCur(const int64_t offset) {
    stream.clear();
    stream.seekg(offset, std::ios::cur);
    return static_cast<bool>(stream);
  }
  bool close() {
    if (!stream.is_open()) return true;
    stream.close();
    return !stream.fail();
  }
  explicit operator bool() const { return stream.is_open(); }
};

using FsFile = HalFile;

class HalStorage {
 public:
  static HalStorage& getInstance() {
    static HalStorage storage;
    return storage;
  }

  bool exists(const char* path) const { return std::filesystem::exists(path); }
  bool remove(const char* path) const { return std::filesystem::remove(path); }
  bool rename(const char* from, const char* to) const {
    std::error_code error;
    std::filesystem::rename(from, to, error);
    return !error;
  }
  bool mkdir(const char* path, const bool = true) const {
    std::error_code error;
    std::filesystem::create_directories(path, error);
    return !error;
  }
  bool openFileForRead(const char*, const std::string& path, HalFile& file) const {
    file.stream.open(path, std::ios::binary | std::ios::in);
    file.writing = false;
    return file.stream.is_open();
  }
  bool openFileForWrite(const char*, const std::string& path, HalFile& file) const {
    file.stream.open(path, std::ios::binary | std::ios::out | std::ios::trunc);
    file.writing = true;
    return file.stream.is_open();
  }
};

#define Storage HalStorage::getInstance()
