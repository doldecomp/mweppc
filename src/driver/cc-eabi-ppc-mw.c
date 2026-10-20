// The static target's identity and plugin registration (Wii's "cc-eppc-mw").
#include "driver/cc-eabi-ppc-mw.h"
#include "driver/cc-eabi-ppc.h"
#include "driver/CWPlugins.h"
#include "driver/libimp-eabi-ppc.h"
#include "driver/oslib.h"

void GetStaticTarget(OSType* cpu, OSType* os) {
    *cpu = 'ePPC';
    *os = 'EABI';
}

void GetStaticPluginType(OSType* language, OSType* plugintype) {
    *language = Lang_Any;
    *plugintype = CWDROPINCOMPILERTYPE;
}

void GetStaticParserPluginType(OSType* style) {
    *style = 'Seep';
}

int RegisterStaticTargetPlugins(void) {
    return RegisterStaticCompilerPlugin() && RegisterStaticLibImporterPlugin();
}

int RegisterStaticTargetResources(void) {
    return RegisterCompilerResources() && RegisterLibImporterResources();
}
