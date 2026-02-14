#ifndef FUZZY_RULE_H
#define FUZZY_RULE_H

#include <Fuzzy.h>
#include "Fuzzy_Set.h"

/* =========================================================
   HELPER FUNCTION : MEMBUAT 1 RULE
   ========================================================= */
inline void addRule(Fuzzy *fuzzy, int &ruleCount,
                    FuzzySet *temp, FuzzySet *turb, FuzzySet *ph,
                    FuzzySet *pump, FuzzySet *heater)
{
    // IF Temperature AND Turbidity
    FuzzyRuleAntecedent *ifTempTurb = new FuzzyRuleAntecedent();
    ifTempTurb->joinWithAND(temp, turb);

    // AND pH
    FuzzyRuleAntecedent *ifAll = new FuzzyRuleAntecedent();
    ifAll->joinWithAND(ifTempTurb, ph);

    // THEN Pump & Heater
    FuzzyRuleConsequent *thenAll = new FuzzyRuleConsequent();
    thenAll->addOutput(pump);
    thenAll->addOutput(heater);

    fuzzy->addFuzzyRule(new FuzzyRule(ruleCount++, ifAll, thenAll));
}

/* =========================================================
   27 RULE MAMDANI
   ========================================================= */
inline void setupRules(Fuzzy *fuzzy, FuzzySets &sets)
{
    int r = 1;

    // ===================== TEMPERATURE COLD =====================
    addRule(fuzzy, r, sets.TC, sets.KC, sets.PA, sets.OFF_P, sets.ON_H); // R1
    addRule(fuzzy, r, sets.TC, sets.KC, sets.PN, sets.OFF_P, sets.ON_H); // R2
    addRule(fuzzy, r, sets.TC, sets.KC, sets.PB, sets.ON_P, sets.ON_H);  // R3
    addRule(fuzzy, r, sets.TC, sets.KM, sets.PA, sets.ON_P, sets.ON_H);  // R4
    addRule(fuzzy, r, sets.TC, sets.KM, sets.PN, sets.ON_P, sets.ON_H);  // R5
    addRule(fuzzy, r, sets.TC, sets.KM, sets.PB, sets.ON_P, sets.ON_H);  // R6
    addRule(fuzzy, r, sets.TC, sets.KT, sets.PA, sets.ON_P, sets.ON_H);  // R7
    addRule(fuzzy, r, sets.TC, sets.KT, sets.PN, sets.ON_P, sets.ON_H);  // R8
    addRule(fuzzy, r, sets.TC, sets.KT, sets.PB, sets.ON_P, sets.ON_H);  // R9

    // ===================== TEMPERATURE NORMAL =====================
    addRule(fuzzy, r, sets.TN, sets.KC, sets.PA, sets.OFF_P, sets.OFF_H); // R10
    addRule(fuzzy, r, sets.TN, sets.KC, sets.PN, sets.OFF_P, sets.OFF_H); // R11
    addRule(fuzzy, r, sets.TN, sets.KC, sets.PB, sets.ON_P, sets.OFF_H);  // R12
    addRule(fuzzy, r, sets.TN, sets.KM, sets.PA, sets.ON_P, sets.OFF_H);  // R13
    addRule(fuzzy, r, sets.TN, sets.KM, sets.PN, sets.ON_P, sets.OFF_H);  // R14
    addRule(fuzzy, r, sets.TN, sets.KM, sets.PB, sets.ON_P, sets.OFF_H);  // R15
    addRule(fuzzy, r, sets.TN, sets.KT, sets.PA, sets.ON_P, sets.OFF_H);  // R16
    addRule(fuzzy, r, sets.TN, sets.KT, sets.PN, sets.ON_P, sets.OFF_H);  // R17
    addRule(fuzzy, r, sets.TN, sets.KT, sets.PB, sets.ON_P, sets.OFF_H);  // R18

    // ===================== TEMPERATURE HOT =====================
    addRule(fuzzy, r, sets.TH, sets.KC, sets.PA, sets.OFF_P, sets.OFF_H); // R19
    addRule(fuzzy, r, sets.TH, sets.KC, sets.PN, sets.OFF_P, sets.OFF_H); // R20
    addRule(fuzzy, r, sets.TH, sets.KC, sets.PB, sets.ON_P, sets.OFF_H);  // R21
    addRule(fuzzy, r, sets.TH, sets.KM, sets.PA, sets.ON_P, sets.OFF_H);  // R22
    addRule(fuzzy, r, sets.TH, sets.KM, sets.PN, sets.ON_P, sets.OFF_H);  // R23
    addRule(fuzzy, r, sets.TH, sets.KM, sets.PB, sets.ON_P, sets.OFF_H);  // R24
    addRule(fuzzy, r, sets.TH, sets.KT, sets.PA, sets.ON_P, sets.OFF_H);  // R25
    addRule(fuzzy, r, sets.TH, sets.KT, sets.PN, sets.ON_P, sets.OFF_H);  // R26
    addRule(fuzzy, r, sets.TH, sets.KT, sets.PB, sets.ON_P, sets.OFF_H);  // R27
}

#endif
