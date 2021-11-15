#include <Windows.h>
#include <stdio.h>

#include <string>

#include "Log.h"

FILE *f = NULL;

// not the best logpath... but it's fine for now
#define LOGPATH "C:\\export_tracer_log\\"
#define LOGFILE LOGPATH "export_tracer_output.";

// log a message with a prefix in brackets
void log_msg(const char *prefix, const char *func, const char *fmt, ...)
{
    char buffer[512] = { 0 };
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer) - 1, fmt, args);
    va_end(args);
    printf("[%s] %s%s%s\n", prefix, func ? func : "", func ? "(): " : "", buffer);
    fprintf(f, "[%s] %s%s%s\n", prefix, func ? func : "", func ? "(): " : "", buffer);
    fflush(f);
}

// initialize a console for logging
bool initialize_log()
{
    FILE *con = NULL;
    if (!AllocConsole() || freopen_s(&con, "CONOUT$", "w", stdout) != 0) {
        error("Failed to initialize console");
        return false;
    }
    CreateDirectory(LOGPATH, NULL);
    std::string log_file = LOGFILE;
    log_file += std::to_string(GetCurrentProcessId());
    log_file += ".log";
    if (fopen_s(&f, log_file.c_str(), "w") != 0 || !f) {
        error("Failed to open output file");
        return false;
    }
    success("Initialized console");
    return true;
}
