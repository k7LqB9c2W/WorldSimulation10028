#pragma once
#include <filesystem>
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

// Explorer/shortcuts may start with an unrelated working directory. Only switch
// when it lacks a world, so explicit project/config workflows keep their paths.
inline void locateRuntimeAssets(const char* argv0) {
    namespace fs = std::filesystem;
    if (fs::exists("assets/images/map.png")) return;
    fs::path executable;
#ifdef _WIN32
    wchar_t path[32768]{};
    const DWORD n=GetModuleFileNameW(nullptr,path,32768);
    if (n && n<32768) executable=fs::path(path);
#else
    if (argv0) executable=fs::absolute(argv0);
#endif
    if (!executable.empty() && fs::exists(executable.parent_path()/"assets/images/map.png")) {
        fs::current_path(executable.parent_path());
        return;
    }
    throw std::runtime_error("Cannot find assets/images/map.png beside the executable or in the working directory.");
}
