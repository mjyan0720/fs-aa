#include "llvm/Analysis/Passes.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/CFG.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Module.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/CallGraph.h"
#include "llvm/Pass.h"
#include "llvm/Target/TargetLibraryInfo.h"
#include "sspt.h"
#include <set>
#include <map>
#include <algorithm>
#include <vector>

using namespace llvm;

// get value - int map from SEG graph
std::map<Value*,unsigned int> *initializeValueMap(SEGsTy *SEGs, unsigned int *alloca_vals) {
  std::pair<std::map<Value*,unsigned int>::iterator,bool> chk;
  unsigned int id = 0, s = 0;
  std::map<Value*,unsigned int> *idmap = new std::map<Value*,unsigned int>();
  // for each function, get it's SEG graph
  for(SEGsTy::iterator si=SEGs->begin(), se=SEGs->end(); si!=se; ++si){
    Function *f = si->first;
    SEGGraphTy * seg = si->second;
    // insert function into the idmap, check duplicates
    chk = idmap->insert(std::pair<Value*,unsigned int>(f,id++));
    assert(chk.second);
    // for each seg node, insert it's value into the idmap, check duplicates
    for(SEGGraphTy::iterator gi=seg->begin(), ge=seg->end(); gi!=ge; ++gi){
      Value* v = gi->first;
      chk = idmap->insert(std::pair<Value*,unsigned int>(v,id++));
      assert(chk.second && "Should be unique");
      // if the node is an AllocaInst, add a second value for it's allocated part
      if (isa<AllocaInst>(v)) {
        s++;
        id++;
      }
    }
  }
  // return the allocated values size
  *alloca_vals = s;
  // return the idmap
  return idmap;
}

// initialize function worklist
std::vector<Function*> *initializeFuncWorklist(SEGsTy *SEGs) {
  std::vector<Function*>* funcWorklist;
  // create empty worklist
  funcWorklist = new std::vector<Function*>();
  // append each function to the worklist
  for (SEGsTy::iterator si=SEGs->begin(), se=SEGs->end(); si!=se; ++si)
    funcWorklist->push_back(si->first);
  // return the worklist
  return funcWorklist;
}

// initialize worklists for each statement
std::map<Function*,std::vector<Value*>*> *initializeStmtWorklist(SEGsTy *SEGs) {
  std::map<Function*,std::vector<Value*>*> *stmtWorklist;
  std::vector<Value*> *subWorklist;
  // create empty stmt worklist
  stmtWorklist = new std::map<Function*,std::vector<Value*>*>();
  // for each function's SEG graph
  for(SEGsTy::iterator si=SEGs->begin(), se=SEGs->end(); si!=se; ++si){
    // get the function and graph
    Function *f = si->first;
    SEGGraphTy * seg = si->second;
    // insert an empty worklist for this function
    subWorklist = new std::vector<Value*>();
    stmtWorklist->insert(std::pair<Function*,std::vector<Value*>*>(f,subWorklist));
    // for each node in the SEG graph
    for(SEGGraphTy::iterator gi=seg->begin(), ge=seg->end(); gi!=ge; ++gi){
      // extract the value for this node
      Value* v = gi->first;
      // check if this is a def node (all nodes are except Return nodes)
      if (!isa<ReturnInst>(v)) subWorklist->push_back(v);
    }
  }
  // return the populated lists
  return stmtWorklist;
}

// how do worklists look?
void doAlgorithm(SEGsTy *SEGs) {
  std::map<Value*,unsigned int> *idmap;
  std::vector<Function*> *funcWorklist;
  std::map<Function*,std::vector<Value*>*> *stmtWorklist;
  std::vector<Value*> *subWorklist;
  SEGGraphTy *seg;
  SEGInfo *si;
  Function *F;
  Value *k;
  bdd tpts;
  unsigned int alloca_vals, kid;
  // initialize algorithm
  idmap = initializeValueMap(SEGs,&alloca_vals);
  funcWorklist = initializeFuncWorklist(SEGs); 
  stmtWorklist = initializeStmtWorklist(SEGs);
  pointsToInit(1000,1000,idmap->size()+alloca_vals);  
  tpts = bdd_false();
  // while function worklist not empty
  while (!funcWorklist->empty()) {
    // get function, worklist, and SEGGraph
    F = funcWorklist->back();
    funcWorklist->pop_back();
    subWorklist = stmtWorklist->at(F);
    seg = SEGs->at(F);
    // while the function's worklist is not empty
    while (!subWorklist->empty()) {
      // get next work item, SEG node, and id
      k = subWorklist->back();
      subWorklist->pop_back();
      si = seg->at(k);
      kid = idmap->at(k);
      // check type of node and run appropriate function
      // TODO: write and insert propogation functions
      if(isa<AllocaInst>(k)) { 
        tpts = processAlloc(tpts,kid,kid+1); // note: allocaInst's alloca is defined as it's id+1
      } else if (isa<PHINode>(k)) {
        // get phi nodes arguments
        PHINode *phi = cast<PHINode>(k);
        std::set<unsigned int> vars = std::set<unsigned int>();
        for (User::op_iterator oit = phi->op_begin(); oit != phi->op_end(); ++oit)
          vars.insert(idmap->at(*oit));
        // process node
        tpts = processCopy(tpts,kid,&vars);
      } else if (isa<LoadInst>(k)) {
        LoadInst *ld = cast<LoadInst>(k);
        tpts = processLoad(tpts,si->in,kid,idmap->at(ld->getPointerOperand()));
      } else if (isa<StoreInst>(k)) {
        StoreInst *sr = cast<StoreInst>(k);
        processStore(tpts,si->in,idmap->at(sr->getPointerOperand()),idmap->at(sr->getValueOperand()));
      } else if (isa<CallInst>(k)) {
        assert(0 && "Not Implemented yet");
      } else if (isa<ReturnInst>(k)) {
        assert(0 && "Not Implemented yet");
      } else assert(0 && "Invalid Node Type");
    }
  }
  // do post-algorithm clean-up
  pointsToFinalize();
}
