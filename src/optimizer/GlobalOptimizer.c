#include "compiler/GlobalOptimizer.h"
#include "compiler/AddPropagation.h"
#include "compiler/CMangler.h"
#include "compiler/CodeGen.h"
#include "compiler/CodeMotion.h"
#include "compiler/ConstantPropagation.h"
#include "compiler/CopyPropagation.h"
#include "compiler/CParser.h"
#include "compiler/LoadDeletion.h"
#include "compiler/LoopDetection.h"
#include "compiler/LoopOptimization.h"
#include "compiler/PCodeListing.h"
#include "compiler/Peephole.h"
#include "compiler/StrengthReduction.h"
#include "compiler/UseDefChains.h"
#include "compiler/ValueNumbering.h"
#include "compiler/VectorArraysToRegs.h"

static void optimizelevel2(Object* func) {
    removecommonsubexpressions(func, TRUE);
    if (removedcommonsubexpressions && copts.debuglisting) {
        pclistblocks(CMangler_GetLinkName(func)->name, "AFTER CSE");
    }

    propagatecopyinstructions(func, TRUE);
    propagateaddinstructions(func);
}

static void optimizelevel3(Object* func) {
    if (copts.peephole) {
        if (copts.schedule_factor == 0 && copts.optimizationlevel > 1) {
            peepholemergeblocks(func, FALSE);
        }

        peepholeoptimizeforward(func);
        if (copts.debuglisting) {
            pclistblocks(CMangler_GetLinkName(func)->name, "AFTER PEEPHOLE FORWARD");
        }
    }

    removecommonsubexpressions(func, FALSE);
    if (removedcommonsubexpressions && copts.debuglisting) {
        pclistblocks(CMangler_GetLinkName(func)->name, "AFTER VALUE NUMBERING");
    }

    propagatecopyinstructions(func, FALSE);
    propagatedcopies = FALSE;
    propagateaddinstructions(func);

    findloopsinflowgraph();
    if (loopsinflowgraph) {
        computeusedefchains(TRUE);
        analyzeloopsinflowgraph();
        moveloopinvariantcode();
        if (movedloopinvariantcode && copts.debuglisting) {
            pclistblocks(CMangler_GetLinkName(func)->name, "AFTER CODE MOTION");
        }

        computeusedefchains(FALSE);
        analyzeForCountableLoops(loopsinflowgraph);
        strengthreduceloops();
        if (strengthreducedloops) {
            if (copts.debuglisting) {
                pclistblocks(CMangler_GetLinkName(func)->name, "AFTER STRENGTH REDUCTION");
            }
            propagatecopyinstructions(func, TRUE);
        }

        optimizeloops();
        if (optimizedloops) {
            if (copts.debuglisting) {
                pclistblocks(CMangler_GetLinkName(func)->name, "AFTER LOOP TRANSFORMATIONS");
            }
            propagatecopyinstructions(func, TRUE);
            propagateaddinstructions(func);
        }
    }

    if (!propagatedcopies) {
        propagatecopyinstructions(func, TRUE);
        if (propagatedcopies && copts.debuglisting) {
            pclistblocks(CMangler_GetLinkName(func)->name, "AFTER COPY PROPAGATION");
        }
    }

    propagateconstants();
    if (propagatedconstants) {
        if (copts.debuglisting) {
            pclistblocks(CMangler_GetLinkName(func)->name, "AFTER CONSTANT PROPAGATION");
        }

        deletedeadloads(func);
        propagateaddinstructions(func);
    }

    if (copts.peephole) {
        if (copts.schedule_factor == 0 && copts.optimizationlevel > 1) {
            peepholemergeblocks(func, FALSE);
        }

        peepholeoptimizeforward(func);
        if (copts.debuglisting) {
            pclistblocks(CMangler_GetLinkName(func)->name, "AFTER PEEPHOLE FORWARD 2");
        }
    }

    removecommonsubexpressions(func, TRUE);
    if (removedcommonsubexpressions) {
        if (copts.debuglisting) {
            pclistblocks(CMangler_GetLinkName(func)->name, "AFTER VALUE NUMBERING 2");
        }
        propagatecopyinstructions(func, TRUE);
    }
}

static void optimizelevel4(Object* func) {
    if (copts.peephole) {
        if (copts.schedule_factor == 0 && copts.optimizationlevel > 1) {
            peepholemergeblocks(func, FALSE);
        }

        peepholeoptimizeforward(func);
        if (copts.debuglisting) {
            pclistblocks(CMangler_GetLinkName(func)->name, "AFTER PEEPHOLE FORWARD");
        }
    }

    removecommonsubexpressions(func, FALSE);
    if (removedcommonsubexpressions && copts.debuglisting) {
        pclistblocks(CMangler_GetLinkName(func)->name, "AFTER VALUE NUMBERING");
    }

    propagatecopyinstructions(func, FALSE);
    propagatedcopies = FALSE;
    propagateaddinstructions(func);

    findloopsinflowgraph();
    if (loopsinflowgraph) {
        computeusedefchains(TRUE);
        analyzeloopsinflowgraph();
        moveloopinvariantcode();
        if (movedloopinvariantcode && copts.debuglisting) {
            pclistblocks(CMangler_GetLinkName(func)->name, "AFTER CODE MOTION");
        }

        computeusedefchains(FALSE);
        analyzeForCountableLoops(loopsinflowgraph);
        strengthreduceloops();
        if (strengthreducedloops) {
            if (copts.debuglisting) {
                pclistblocks(CMangler_GetLinkName(func)->name, "AFTER STRENGTH REDUCTION");
            }
            propagatecopyinstructions(func, TRUE);
            propagatedcopies = FALSE;
        }

        optimizeloops();
        if (optimizedloops) {
            if (copts.debuglisting) {
                pclistblocks(CMangler_GetLinkName(func)->name, "AFTER LOOP TRANSFORMATIONS");
            }
            propagatecopyinstructions(func, TRUE);
            propagateaddinstructions(func);
        }
    }

    if (!propagatedcopies) {
        propagatecopyinstructions(func, TRUE);
    }

    propagateconstants();
    if (propagatedconstants) {
        if (copts.debuglisting) {
            pclistblocks(CMangler_GetLinkName(func)->name, "AFTER CONSTANT PROPAGATION");
        }

        deletedeadloads(func);
        if (propagatedconstants) {
            propagatecopyinstructions(func, TRUE);
        }
        propagateaddinstructions(func);

        if (optimizedloop_full_unroll) {
            changearraytoregisters();
            if (optimizedloop_trans_regs && copts.debuglisting) {
                pclistblocks(CMangler_GetLinkName(func)->name, "AFTER ARRAY => REGISTER TRANSFORM");
                propagateconstants();
                if (copts.debuglisting) {
                    pclistblocks(CMangler_GetLinkName(func)->name, "AFTER CONSTANT PROPAGATION 2");
                }
                if (propagatedconstants) {
                    propagatecopyinstructions(func, TRUE);
                }
            }
        }
    }

    if (copts.peephole) {
        if (copts.schedule_factor == 0 && copts.optimizationlevel > 1) {
            peepholemergeblocks(func, FALSE);
        }

        peepholeoptimizeforward(func);
        if (copts.debuglisting) {
            pclistblocks(CMangler_GetLinkName(func)->name, "AFTER PEEPHOLE FORWARD 2");
        }
    }

    removecommonsubexpressions(func, TRUE);
    if (removedcommonsubexpressions) {
        if (copts.debuglisting) {
            pclistblocks(CMangler_GetLinkName(func)->name, "AFTER VALUE NUMBERING 2");
        }
        propagatecopyinstructions(func, TRUE);
    }

    if (has_altivec_arrays && vectorarraystoregs()) {
        if (copts.debuglisting) {
            pclistblocks(CMangler_GetLinkName(func)->name, "AFTER VECTOR ARRAY CONVERSION");
        }
        propagatecopyinstructions(func, FALSE);
        propagatecopyinstructions(func, TRUE);
    }

    findloopsinflowgraph();
    if (!loopsinflowgraph) {
        return;
    }

    computeusedefchains(TRUE);
    analyzeloopsinflowgraph();
    moveloopinvariantcode();
    if (movedloopinvariantcode && copts.debuglisting) {
        pclistblocks(CMangler_GetLinkName(func)->name, "AFTER CODE MOTION 2");
    }

    removecommonsubexpressions(func, TRUE);
    if (removedcommonsubexpressions) {
        if (copts.debuglisting) {
            pclistblocks(CMangler_GetLinkName(func)->name, "AFTER VALUE NUMBERING 3");
        }
        propagatecopyinstructions(func, TRUE);
    }
}

void globallyoptimizepcode(Object* func) {
    if (copts.debuglisting) {
        pclistblocks(CMangler_GetLinkName(func)->name, "BEFORE GLOBAL OPTIMIZATION");
    }

    if (copts.optimizationlevel == 2 || (has_catch_blocks && copts.optimizationlevel > 2)) {
        optimizelevel2(func);
    } else if (copts.optimizationlevel == 3) {
        optimizelevel3(func);
    } else if (copts.optimizationlevel == 4) {
        optimizelevel4(func);
    }
}
