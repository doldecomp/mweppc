#ifndef DRIVER_PARSER_H
#define DRIVER_PARSER_H

// The parser plugin's types; ParserTool and PrefDataPanel are defined in ParserFace.h
typedef struct ParserTool ParserTool;
typedef struct PrefDataPanel PrefDataPanel;

// NOTE: invented names: PRAGMA_FLAGS_ONOFF, PRAGMA_FLAGS_REVERSED
// Pragma.flags: an on/off setting, or one whose sense is reversed
enum {
    PRAGMA_FLAGS_ONOFF,
    PRAGMA_FLAGS_REVERSED
};

// A pragma's setting: a byte-sized enum, so `set ? PR_ON : PR_OFF` is a byte expression
enum {
    PR_UNSET = 0,
    PR_ON = 1,
    PR_OFF = 2,
    PR_AUTO = 3,
    PR_RESET = 4
};

// A pragma the parser turns a setting into (SetupPragmas writes them)
typedef struct Pragma { // NOTE: invented name
    void* value;
    const char* pragma;
    int flags;
} Pragma;

#endif
