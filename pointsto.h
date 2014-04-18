#include <map>
#include <set>
#include "llvm/IR/Instruction.h"
#include "bdd.h"
#include "fdd.h"
#include "SEGNode.h"

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

int processAlloc(bdd *tpts, llvm::SEGNode *sn); 
int processCopy(bdd *tpts,  llvm::SEGNode *sn); 
int processLoad(bdd *tpts,  llvm::SEGNode *sn); 
int processStore(bdd *tpts, llvm::SEGNode *sn); 
int processCall(bdd *tpts,  llvm::SEGNode *sn);
int processRet(bdd *tpts,   llvm::SEGNode *sn);

typedef std::pair<unsigned int, unsigned int> callsite_t;
void propogateTopLevel(unsigned int f, unsigned int k);
void propogateAddrTaken(unsigned int f, unsigned int k); 
void updateWorklist1(unsigned int f,bool changed);
bool updateFunEntry(unsigned int f, bdd filtk);  
std::vector<unsigned int> *funParams(unsigned int f);  
std::vector<callsite_t> *funCallsites(unsigned int f); 
bool assignedCall(unsigned int); 

