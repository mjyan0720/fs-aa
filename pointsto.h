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

typedef std::map<const llvm::Function*, std::list<llvm::SEGNode*>*> WorkList;

void check(int rc) ;
void pointsToInit(unsigned int nodes, unsigned int cachesize, unsigned int domainsize);
void pointsToFinalize();
std::vector<unsigned int> *pointsto(bdd b);
// Preprocess functions perform all static variable lookups for these nodes
int preprocessAlloc(llvm::SEGNode *sn, std::map<const llvm::Value*,unsigned int> *im);
int preprocessCopy(llvm::SEGNode *sn,  std::map<const llvm::Value*,unsigned int> *im);
int preprocessLoad(llvm::SEGNode *sn,  std::map<const llvm::Value*,unsigned int> *im);
int preprocessStore(llvm::SEGNode *sn, std::map<const llvm::Value*,unsigned int> *im);
int preprocessCall(llvm::SEGNode *sn,  std::map<const llvm::Value*,unsigned int> *im);
int preprocessRet(llvm::SEGNode *sn,   std::map<const llvm::Value*,unsigned int> *im);

int processAlloc(bdd *tpts, llvm::SEGNode *sn, WorkList* swkl); 
int processCopy(bdd *tpts,  llvm::SEGNode *sn, WorkList* swkl); 
int processLoad(bdd *tpts,  llvm::SEGNode *sn, WorkList* swkl); 
int processStore(bdd *tpts, llvm::SEGNode *sn, WorkList* swkl); 
int processCall(bdd *tpts,  llvm::SEGNode *sn, WorkList* swkl);
int processRet(bdd *tpts,   llvm::SEGNode *sn, WorkList* swkl);

void propogateTopLevel(bdd *oldtpts, bdd *newpart, llvm::SEGNode *sn, WorkList *swkl, const llvm::Function *f);
void propogateAddrTaken(llvm::SEGNode *sn, WorkList *swkl, const llvm::Function *f);

void propogateTopLevel(unsigned int f, unsigned int k);
void propogateAddrTaken(unsigned int f, unsigned int k); 
typedef std::pair<unsigned int, unsigned int> callsite_t;
void updateWorklist1(unsigned int f,bool changed);
bool updateFunEntry(unsigned int f, bdd filtk);  
std::vector<unsigned int> *funParams(unsigned int f);  
std::vector<callsite_t> *funCallsites(unsigned int f); 
bool assignedCall(unsigned int); 

#endif
