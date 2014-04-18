#include <map>
#include <set>
#include "llvm/IR/Instruction.h"
#include "bdd.h"
#include "fdd.h"

void check(int rc) ;
void pointsToInit(unsigned int nodes, unsigned int cachesize, unsigned int domainsize);
void pointsToFinalize();
std::vector<unsigned int> *pointsto(bdd b);

typedef std::pair<unsigned int, unsigned int> callsite_t;

bdd processAlloc(bdd tpts, unsigned int v1, unsigned int v2); 
bdd processCopy(bdd tpts, unsigned int x, std::set<unsigned int> *vars); 
bdd processLoad(bdd tpts, bdd kpts, unsigned int x, unsigned int y); 
bdd processStore(bdd tpts, bdd inkpts, unsigned int x, unsigned int y); 

void propogateTopLevel(unsigned int f, unsigned int k);
void propogateAddrTaken(unsigned int f, unsigned int k); 
void updateWorklist1(unsigned int f,bool changed);
bool updateFunEntry(unsigned int f, bdd filtk);  
std::vector<unsigned int> *funParams(unsigned int f);  
std::vector<callsite_t> *funCallsites(unsigned int f); 
bool assignedCall(unsigned int); 

bdd processCall(bdd tpts, bdd inkpts, bdd global, unsigned int x, unsigned int f, bool ptr, std::vector<unsigned int> *args); 
unsigned int processRet(unsigned int f, unsigned int k, bdd tpts, unsigned int x); 
