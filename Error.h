#pragma once
#include <functional>
#include <cstdio>
#include <string>

enum lvl {NOERR, INFO, WARN, ERR};

typedef struct Error {
    lvl level;
    std::string msg;
    std::string data;

} *ErrorPtr;

inline std::string LvlStr(lvl level) {
    switch(level){
        case NOERR: return "NOERR";
        case INFO:  return "INFO";
        case WARN:  return "WARN";
        case ERR:   return "ERROR";
        default:    return "UNKNOWN";
    }
}

// --- Default handlers (printf-based) ------------------------------

inline std::function<void(const Error*)> g_handle_err_handler =
    [](const Error* e) {
        if (e && (e->level > NOERR))
            std::printf("%s: %s\nDATA: %s\n", LvlStr(e->level).c_str(), e->msg.c_str(), e->data.c_str());
    };

// Helpers
inline bool IsErr(const Error* e) {
    return e && e->level > NOERR;
}

// Convenient builder (your existing MsgErr)
inline std::string MsgErr(const Error* e) {
    return e ? e->msg : "";
}

#define MSG_ERR(e)  do { g_handle_err_handler(e); } while(0)