#include <map>
#include <set>
#include "llvm/IR/Instruction.h"
#include "bdd.h"
#include "fdd.h"

///SEGInfo - One instance of this structure is stored for every node
///in SEG. Useful in constructing SEG and propogating alias information.
struct SEGInfo{
  //std::set<User*> DefChain;//SSA guarantees all values have one definition
  std::set<llvm::User*> userChain;
  std::set<llvm::Value*> predecessor;
  std::set<llvm::Value*> successor;
  bool IsPnode;//true if the node doesn't change aa information
  bdd in, out;
};

typedef struct SEGInfo SEGInfo;
typedef std::map<llvm::Value*, SEGInfo*> SEGGraphTy;
typedef std::map<llvm::Function*, SEGGraphTy*> SEGsTy;
typedef std::set<llvm::Value*> ValueSet;

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
