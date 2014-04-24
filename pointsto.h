#ifndef POINTSTO_H
#define POINTSTO_H

#include <map>
#include <set>
#include <list>
#include "llvm/IR/Instruction.h" 
#include "bdd.h"
#include "fdd.h"
#include "SEGNode.h"
#include "SEG.h"

#define bdd_sat(b)   ((b) != bdd_false())
#define bdd_unsat(b) ((b) == bdd_false())

typedef std::map<const llvm::Function*, std::list<llvm::SEGNode*>*> WorkList;

// Library initialization and finalization
void pointsToInit(unsigned int nodes, unsigned int cachesize, unsigned int domainsize);
void pointsToFinalize();

// Helper functions
bool pointsTo(bdd b, unsigned int v1, unsigned int v2);
void printBDD(unsigned int max, bdd b);
void printBDD(unsigned int max, std::map<unsigned int,std::string*> *lt, bdd b);

// Preprocess functions perform all static variable lookups for these nodes
int preprocessAlloc(llvm::SEGNode *sn, std::map<const llvm::Value*,unsigned int> *im);
int preprocessCopy(llvm::SEGNode *sn,  std::map<const llvm::Value*,unsigned int> *im);
int preprocessLoad(llvm::SEGNode *sn,  std::map<const llvm::Value*,unsigned int> *im);
int preprocessStore(llvm::SEGNode *sn, std::map<const llvm::Value*,unsigned int> *im);
int preprocessCall(llvm::SEGNode *sn,  std::map<const llvm::Value*,unsigned int> *im);
int preprocessRet(llvm::SEGNode *sn,   std::map<const llvm::Value*,unsigned int> *im);

// Main process functions propagate pointer information through the BDDs
int processAlloc(bdd *tpts, llvm::SEGNode *sn, WorkList* swkl); 
int processCopy(bdd *tpts,  llvm::SEGNode *sn, WorkList* swkl); 
int processLoad(bdd *tpts,  llvm::SEGNode *sn, WorkList* swkl); 
int processStore(bdd *tpts, llvm::SEGNode *sn, WorkList* swkl); 
int processCall(bdd *tpts, llvm::SEGNode *sn, WorkList* swkl, std::list<const llvm::Function*> *fwkl,
                std::map<unsigned int,const llvm::Function*> *fm, std::map<const llvm::Function *,llvm::SEG*> *sm,
                bdd gvarpts);
int processRet(bdd *tpts,   llvm::SEGNode *sn, WorkList* swkl);

// Propagation functions automate pushing BDD changes through the SEG and worklists
bool propagateTopLevel(bdd *oldtpts, bdd *newpart, llvm::SEGNode *sn, WorkList *swkl, const llvm::Function *f);
bool propagateAddrTaken(llvm::SEGNode *sn, WorkList *swkl, const llvm::Function *f);

#endif
