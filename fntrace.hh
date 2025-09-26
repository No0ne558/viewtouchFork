#ifndef VT_FNTRACE_HH
#define VT_FNTRACE_HH

#include "basic.hh"
#include <string>
#include <chrono>
#include <mutex>
#include <atomic>
#include <memory>
#include <cstdio>

extern int debug_mode;

#define STRSHORT    64
#define STRLENGTH   512   // constant to set the length of a string
#define STRLONG     2048  // 2K string

#ifdef DEBUG
// BackTrace Functions/Data
struct TraceEntry {
    char function[STRLONG];
    char file[STRLENGTH];
    int line;
    std::chrono::steady_clock::time_point timestamp;
    size_t memory_usage;
};

extern TraceEntry BT_Stack[STRLENGTH];
extern std::atomic<int> BT_Depth;
extern std::atomic<int> BT_Track;
extern std::mutex BT_Mutex;

class BackTraceFunction
{
public:
    // Constructor
    BackTraceFunction(const char* func, const char* file, int line)
        : recorded_entry_(false)
    {
        if (BT_Track) {
            std::lock_guard<std::mutex> lock(BT_Mutex);
            const int current_depth = BT_Depth.load();
            if (current_depth < STRLENGTH) {
                TraceEntry& entry = BT_Stack[current_depth];
                BT_Depth.store(current_depth + 1);
                std::snprintf(entry.function, STRLONG, "%s", func);
                std::snprintf(entry.file, STRLENGTH, "%s", file);
                entry.line = line;
                entry.timestamp = std::chrono::steady_clock::now();
                entry.memory_usage = get_current_memory_usage();
                recorded_entry_ = true;
            }
            std::printf("Entering %s (%s:%d)\n", func, file, line);
        }
    }
    
    // Destructor
    virtual ~BackTraceFunction()
    {
        if (BT_Track) {
            std::lock_guard<std::mutex> lock(BT_Mutex);
            if (recorded_entry_ && BT_Depth.load() > 0) {
                BT_Depth.fetch_sub(1);
            }
        }
    }

private:
    size_t get_current_memory_usage() noexcept;
    bool recorded_entry_;
};

#define FnTrace(func) BackTraceFunction _fn_start(func, __FILE__, __LINE__)
#define FnTraceEnable(x) (BT_Track = (x))
void FnPrintTrace(bool include_timing = true, bool include_memory = true);
void FnPrintLast(int depth, bool include_timing = true, bool include_memory = true);
const char* FnReturnLast();
#define LINE() printf("%s:  Got to line %d\n", __FILE__, __LINE__)
#else
#define FnTrace(...)
#define FnTraceEnable(x)
#define FnPrintTrace(...)
#define FnPrintLast(...)
#define FnReturnLast() ""
#define LINE()
#endif

#endif // VT_FNTRACE_HH
