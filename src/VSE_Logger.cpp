#include "VSE_Logger.h"

namespace VSE
{
    std::shared_ptr<spdlog::logger> Log::s_CoreLogger;

    void Log::Init()
    {
        // Example pattern: [Timestamp] [Logger Name] [Log Level] Message
        // For more patterns: https://github.com/gabime/spdlog/wiki/3.-Custom-formatting
        spdlog::set_pattern("%^[%Y-%m-%d %H:%M:%S.%e] [%n] [%l] %v%$");

        s_CoreLogger = spdlog::stdout_color_mt("VSE_CORE");
        s_CoreLogger->set_level(spdlog::level::trace); // Set default log level

        VSE_CORE_INFO("VSE Core Logging Initialized");
    }
}