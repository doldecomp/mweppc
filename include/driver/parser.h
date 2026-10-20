#ifndef DRIVER_PARSER_H
#define DRIVER_PARSER_H

typedef struct ParserTool ParserTool;
typedef struct PrefDataPanel PrefDataPanel;

enum {
    PRAGMA_FLAGS_ONOFF,
    PRAGMA_FLAGS_REVERSED
};

enum {
    PR_UNSET = 0,
    PR_ON = 1,
    PR_OFF = 2,
    PR_AUTO = 3,
    PR_RESET = 4
};

typedef struct Pragma {
    void* value;
    const char* pragma;
    int flags;
} Pragma;

#endif
