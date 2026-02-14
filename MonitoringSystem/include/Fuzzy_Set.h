#ifndef FUZZY_SET_H
#define FUZZY_SET_H

#include <Fuzzy.h>

/* ================= FUZZY SETS STRUCTURE ================= */
struct FuzzySets
{
    FuzzySet *TC;
    FuzzySet *TN;
    FuzzySet *TH;
    FuzzySet *KC;
    FuzzySet *KM;
    FuzzySet *KT;
    FuzzySet *PA;
    FuzzySet *PN;
    FuzzySet *PB;
    FuzzySet *OFF_P;
    FuzzySet *ON_P;
    FuzzySet *OFF_H;
    FuzzySet *ON_H;
};

/* ================= MEMBERSHIP FUNCTION ================= */
inline FuzzySets setupFuzzySystem(Fuzzy *fuzzy)
{
    FuzzySets sets;

    // Temperature
    FuzzyInput *temperature = new FuzzyInput(1);
    sets.TC = new FuzzySet(0, 20, 23, 25);
    sets.TN = new FuzzySet(25, 27, 27, 30);
    sets.TH = new FuzzySet(30, 32, 34, 35);
    temperature->addFuzzySet(sets.TC);
    temperature->addFuzzySet(sets.TN);
    temperature->addFuzzySet(sets.TH);
    fuzzy->addFuzzyInput(temperature);

    // Turbidity (0-100)
    FuzzyInput *turbidity = new FuzzyInput(2);
    sets.KC = new FuzzySet(0, 10, 20, 25);
    sets.KM = new FuzzySet(25, 35, 40, 50);
    sets.KT = new FuzzySet(50, 60, 80, 100);
    turbidity->addFuzzySet(sets.KC);
    turbidity->addFuzzySet(sets.KM);
    turbidity->addFuzzySet(sets.KT);
    fuzzy->addFuzzyInput(turbidity);

    // pH
    FuzzyInput *ph = new FuzzyInput(3);
    sets.PA = new FuzzySet(4, 5, 6, 7);
    sets.PN = new FuzzySet(6.5, 7.5, 7.5, 8.5);
    sets.PB = new FuzzySet(8, 9, 10, 11);
    ph->addFuzzySet(sets.PA);
    ph->addFuzzySet(sets.PN);
    ph->addFuzzySet(sets.PB);
    fuzzy->addFuzzyInput(ph);

    // Pump
    FuzzyOutput *pump = new FuzzyOutput(1);
    sets.OFF_P = new FuzzySet(0, 0, 0.4, 0.6);
    sets.ON_P = new FuzzySet(0.4, 0.6, 1, 1);
    pump->addFuzzySet(sets.OFF_P);
    pump->addFuzzySet(sets.ON_P);
    fuzzy->addFuzzyOutput(pump);

    // Heater
    FuzzyOutput *heater = new FuzzyOutput(2);
    sets.OFF_H = new FuzzySet(0, 0, 0.4, 0.6);
    sets.ON_H = new FuzzySet(0.4, 0.6, 1, 1);
    heater->addFuzzySet(sets.OFF_H);
    heater->addFuzzySet(sets.ON_H);
    fuzzy->addFuzzyOutput(heater);

    return sets;
}

#endif
