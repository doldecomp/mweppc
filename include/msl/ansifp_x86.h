#ifndef MSL_ANSIFP_X86_H
#define MSL_ANSIFP_X86_H

struct decform {
    char style;
    char unused;
    short digits;
};
typedef struct decform decform;
struct decimal {
    char sgn;
    char unused;
    short exp;
    struct {
        unsigned char length;
        unsigned char text[32];
        unsigned char unused;
    } sig;
};
typedef struct decimal decimal;

#endif
