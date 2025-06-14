#pragma once
#include "spdlog/spdlog.h"
#include "spdlog/sinks/stdout_color_sinks.h" // For console output
#include <memory>

namespace VSE
{
    class Log
    {
    public:
        static void Init(); // Call this once at application startup

        static std::shared_ptr<spdlog::logger> &GetCoreLogger() { return s_CoreLogger; }
        // Potentially add GetClientLogger() for editor/Lua specific logs later

    private:
        static std::shared_ptr<spdlog::logger> s_CoreLogger;
    };
}

// Macros for convenient logging
#define VSE_CORE_TRACE(...) ::VSE::Log::GetCoreLogger()->trace(__VA_ARGS__)
#define VSE_CORE_INFO(...) ::VSE::Log::GetCoreLogger()->info(__VA_ARGS__)
#define VSE_CORE_WARN(...) ::VSE::Log::GetCoreLogger()->warn(__VA_ARGS__)
#define VSE_CORE_ERROR(...) ::VSE::Log::GetCoreLogger()->error(__VA_ARGS__)
#define VSE_CORE_CRITICAL(...) ::VSE::Log::GetCoreLogger()->critical(__VA_ARGS__)

// In a .cpp file (e.g., VSE_Logger.cpp)
// std::shared_ptr<spdlog::logger> VSE::Log::s_CoreLogger;
//
// void VSE::Log::Init()
// {
//     spdlog::set_pattern("%^[%T] %n: %v%$"); // Time, logger name, message
//     s_CoreLogger = spdlog::stdout_color_mt("VSE_CORE");
//     s_CoreLogger->set_level(spdlog::level::trace); // Set default log level
//     VSE_CORE_INFO("VSE Logging Initialized");
// }