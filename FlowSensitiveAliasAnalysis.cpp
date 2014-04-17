//===- FlowSensitiveAliasAnalysis.cpp - Semi-Sparse Flow Sensitive Pointer Analysis-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Add description of current file
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "flowsensitive-aa"
#include "SEG.h"
#include "llvm/Analysis/Passes.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/CFG.h"
#include "llvm/Analysis/CallGraph.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Module.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/CallGraph.h"
#include "llvm/Pass.h"
#include "llvm/Target/TargetLibraryInfo.h"
#include "llvm/Support/Debug.h"
#include <set>
#include <map>
#include <algorithm>

using namespace llvm;


namespace {



class FlowSensitiveAliasAnalysis : public ModulePass, public AliasAnalysis {
private:
	std::map<Function*, SEG*> Func2SEG;
	std::map<const Value*, unsigned> Value2Int;
	std::vector<Function*> FuncWorkList;

	virtual void getAnalysisUsage(AnalysisUsage &AU) const {
		AU.addRequired<AliasAnalysis>();
		AU.addRequired<TargetLibraryInfo>();
		AU.addRequired<CallGraph>();	
		//FIXME Don't need LoopInfo
		AU.addRequired<LoopInfo>();
	}

	/// constructSEG - construct sparse evaluation graph for each function
	/// and insert mapping between function and corresponding seg to Func2SEG.
	void constructSEG(Module &M);	

	/// initializeValueMap - give all values(global variable, function, local statement) 
	/// a unique unsigned integer, and insert into Value2Int.
	void initializeValueMap(Module &M);

	/// initializeFuncWorkList - insert all functions(including declaration) into
	/// FuncWorkList.
	void initializeFuncWorkList(Module &M);

	/// printValueMap - print out debug information of value mapping.
	void printValueMap();

public:
	static char ID;
	FlowSensitiveAliasAnalysis() : ModulePass(ID){
		initializeFlowSensitiveAliasAnalysisPass(*PassRegistry::getPassRegistry());
	}

	virtual void initializePass() {
		InitializeAliasAnalysis(this);
 	}

	virtual bool runOnModule(Module &M);

//copy from noaa
    virtual AliasResult alias(const Location &LocA, const Location &LocB) {
      return MayAlias;
    }

    virtual ModRefBehavior getModRefBehavior(ImmutableCallSite CS) {
      return UnknownModRefBehavior;
    }
    virtual ModRefBehavior getModRefBehavior(const Function *F) {
      return UnknownModRefBehavior;
    }

    virtual bool pointsToConstantMemory(const Location &Loc,
                                        bool OrLocal) {
      return false;
    }
    virtual ModRefResult getModRefInfo(ImmutableCallSite CS,
                                       const Location &Loc) {
      return ModRef;
    }
    virtual ModRefResult getModRefInfo(ImmutableCallSite CS1,
                                       ImmutableCallSite CS2) {
      return ModRef;
    }

    /// getAdjustedAnalysisPointer - This method is used when a pass implements
    /// an analysis interface through multiple inheritance.  If needed, it
    /// should override this to adjust the this pointer as needed for the
    /// specified pass info.
    virtual void *getAdjustedAnalysisPointer(const void *ID) {
      if (ID == &AliasAnalysis::ID)
        return (AliasAnalysis*)this;
      return this;
    }
//copy from noaa -- end

};

} // end of namespace

bool FlowSensitiveAliasAnalysis::runOnModule(Module &M){
	constructSEG(M);
	initializeValueMap(M);
	initializeFuncWorkList(M);
	printValueMap();
	return false;
}

void FlowSensitiveAliasAnalysis::constructSEG(Module &M) {
	for(Module::iterator mi=M.begin(), me=M.end(); mi!=me; ++mi){
                Function * f = &*mi;
               	//FIXME: Don't need LoopInfo to initialize SEG
		//Because SCCIterator is used to detect SCC, LoopInfo not
		//used in SEG construction in the latest version.
		//Delete LI attribute in SEGNode, modify construct function.
		if(f->isDeclaration())
                        continue;
		f->dump();
                LoopInfo *LI = &getAnalysis<LoopInfo>(*f);
		SEG *seg = new SEG(f, LI);
		seg->dump();
		Func2SEG.insert( std::pair<Function*, SEG*>(f, seg) );
        }
}

void FlowSensitiveAliasAnalysis::initializeValueMap(Module &M){
	unsigned id = 0;
	std::pair<std::map<const Value*, unsigned>::iterator, bool> chk;
	/// map global variables
	for(Module::global_iterator mi=M.global_begin(), me=M.global_end(); mi!=me; ++mi){
		const GlobalVariable *v = &*mi;
		chk = Value2Int.insert( std::pair<const Value*, unsigned>(v, id++) );
		assert( chk.second && "Value Id should be unique");
	}
	/// map functions
	for(Module::iterator mi=M.begin(), me=M.end(); mi!=me; ++mi){
		const Function *f = &*mi;
		chk = Value2Int.insert( std::pair<const Value*, unsigned>(f, id++) );
		assert( chk.second && "Value Id should be unique");
		/// map arguments
		for(Function::const_arg_iterator ai=f->arg_begin(), ae=f->arg_end(); ai!=ae; ++ai){
			const Argument *a = &*ai;
			chk = Value2Int.insert( std::pair<const Value*, unsigned>(a, id++) );
			assert( chk.second && "Value Id should be unique");
		}
	}
	/// map local statements
	for(std::map<Function*, SEG*>::iterator mi=Func2SEG.begin(), me=Func2SEG.end(); mi!=me; ++mi){
		SEG *seg = mi->second;
		for(SEG::iterator sni=seg->begin(), sne=seg->end(); sni!=sne; ++sni){
			SEGNode *sn = &*sni;
			if(sn->isnPnode()==false)
				continue;
			const Instruction *inst = sn->getInstruction();
			chk = Value2Int.insert( std::pair<const Value*, unsigned>(inst, id++) );
			if(isa<AllocaInst>(inst))
				id++;
			assert( chk.second && "Value Id should be unique");	
		}
	}
}


void FlowSensitiveAliasAnalysis::initializeFuncWorkList(Module &M){
	for(Module::iterator mi=M.begin(), me=M.end(); mi!=me; ++mi){
                Function * f = &*mi;
		FuncWorkList.push_back(f);
	}
}

void FlowSensitiveAliasAnalysis::printValueMap(){
	dbgs()<<"ValueMap : \n";
	for(std::map<const Value*, unsigned>::iterator mi=Value2Int.begin(), me=Value2Int.end(); mi!=me; ++mi){
		dbgs()<<mi->first->getName()<<" --> "<<mi->second<<"\n";
	}
}

/// Register this pass
char FlowSensitiveAliasAnalysis::ID = 0;
static RegisterPass<FlowSensitiveAliasAnalysis> X("fs-aa", "Semi-sparse Flow Sensitive Pointer Analysis",
	                  false, false);

ModulePass *llvm::createFlowSensitiveAliasAnalysisPass() { return new FlowSensitiveAliasAnalysis(); }

//INITIALIZE_AG_PASS_BEGIN(FlowSensitiveAliasAnalysis, AliasAnalysis,
//			"flowsensitive-aa", "Semi-sparse Flow Sensitive Pointer Analysis",
//			false, true, false)
//INITIALIZE_AG_DEPENDENCY(CallGraph)
//INITIALIZE_PASS_DEPENDENCY(TargetLibraryInfo)
//INITIALIZE_PASS_DEPENDENCY(LoopInfo)
//INITIALIZE_AG_PASS_END(FlowSensitiveAliasAnalysis, AliasAnalysis,
//                        "flowsensitive-aa", "Semi-sparse Flow Sensitive Pointer Analysis",
//                        false, true, false)
