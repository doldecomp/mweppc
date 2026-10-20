#include "driver/CLProj.h"
#include "driver/CLTarg.h"
#include "driver/MsDos.h"
#include "driver/oslib.h"

Boolean Proj_Initialize(Project* this) {
    this->targets = NULL;
    OS_GetCWD(&this->projectDirectory.path);
    OS_MakeNameSpec("", &this->projectDirectory.name);
    return TRUE;
}

Boolean Proj_Terminate(Project* this) {
    OS_ASSERT(25, this != NULL);

    Targets_Term(this->targets);
    return TRUE;
}
