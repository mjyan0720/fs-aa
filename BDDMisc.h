#ifndef BDDMISC_H
#define BDDMISC_H

#include <map>
#include <string>
#include "bdd.h"
#include "fdd.h"

#define bdd_sat(b)   ((b) != bdd_false())
#define bdd_unsat(b) ((b) == bdd_false())

// Library initialization and finalization
void pointsToInit(unsigned int nodes, unsigned int cachesize, unsigned int domainsize);
void pointsToFinalize();

// Helper functions
bool pointsTo(bdd b, unsigned int v1, unsigned int v2);
void printBDD(unsigned int max, bdd b);
void printBDD(unsigned int max, std::map<unsigned int,std::string*> *lt, bdd b);

#endif /* BDDMISC_H */
