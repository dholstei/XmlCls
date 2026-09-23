#include "Error.h"

extern "C" {

CErrorPtr CreateCError(ErrLvl level, const char* msg, const char* data){
    CErrorPtr e = (CErrorPtr)malloc(sizeof(CError));
    if (!e) return nullptr;
    e->level = level;
    e->msg = msg ? strdup(msg) : nullptr;
    e->data = data ? strdup(data) : nullptr;
    return e;
}

void FreeCError(CErrorPtr e){
    if (e) {
        if (e->msg) free(e->msg);
        if (e->data) free(e->data);
        free(e);
    }
}

CErrorPtr ConvertToCError(const Error* e) {
    if (!e) return nullptr;
    return CreateCError((ErrLvl) e->level, e->msg.c_str(), e->data.c_str());
}

}