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
#include "bdd.h"
#include "fdd.h"
#include "pointsto.h"
#include "SEG.h"
#include <set>
#include <map>
#include <list>
#include <algorithm>

using namespace llvm;

namespace {

class FlowSensitiveAliasAnalysis : public ModulePass, public AliasAnalysis {
private:
	typedef std::list<SEGNode*> StmtList;

	/// Func2SEG - mapping from Function to corresponding SEG
	std::map<const Function*, SEG*> Func2SEG;

	/// Value2Int - mapping from Value(Global Variable, Function, Local
	/// Statement are included) to unique Id
	std::map<const Value*, unsigned> Value2Int;

	/// FuncWorkList - Functions need to be processed
	std::vector<Function*> FuncWorkList;
	/// StmtWorkList - the main algorithm iterate on it.
	/// For each function, keep a statement list to work on for it.
	std::map<const Function*, StmtList*> StmtWorkList;

	/// LocationCount - the total number of top variable and address-taken variable
	unsigned LocationCount;

	/// top level points to graph
	bdd TopLevelPTS;

	virtual void getAnalysisUsage(AnalysisUsage &AU) const {
		AU.addRequired<AliasAnalysis>();
		AU.addRequired<TargetLibraryInfo>();
		AU.addRequired<CallGraph>();
	}

	/// constructSEG - construct sparse evaluation graph for each function
	/// and insert mapping between function and corresponding seg to Func2SEG.
	void constructSEG(Module &M);

	/// initializeValueMap - give all values(global variable, function, local statement)
	/// a unique unsigned integer, and insert into Value2Int.
	/// Return the total number of locations used to encode bdd.
	unsigned initializeValueMap(Module &M);

	/// initializeFuncWorkList - insert all functions(including declaration) into
	/// FuncWorkList.
	/// Meanwhile call initializeStmtWorkList for each function.
	void initializeFuncWorkList(Module &M);

	/// initializeStmtWorkList - insert all SEGNode(statements) into StmtList
	void initializeStmtWorkList(Function *F);

	/// doAnalysis - performs actual analysis algorithm
	void doAnalysis(Module &M);

	/// setupAnalysis - initializes analysis datastructures
	void setupAnalysis(Module &M);

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

	virtual bool pointsToConstantMemory(const Location &Loc, bool OrLocal) {
		return false;
	}

	virtual ModRefResult getModRefInfo(ImmutableCallSite CS, const Location &Loc) {
		return ModRef;
	}

	virtual ModRefResult getModRefInfo(ImmutableCallSite CS1, ImmutableCallSite CS2) {
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
	LocationCount = initializeValueMap(M);
	initializeFuncWorkList(M);
	printValueMap();
	doAnalysis(M);
	return false;
}

void FlowSensitiveAliasAnalysis::constructSEG(Module &M) {
	for(Module::iterator mi=M.begin(), me=M.end(); mi!=me; ++mi) {
		const Function * f = &*mi;
		SEG *seg = new SEG(f);
		seg->dump();
		Func2SEG.insert( std::pair<const Function*, SEG*>(f, seg) );
	}
}

unsigned FlowSensitiveAliasAnalysis::initializeValueMap(Module &M){
	unsigned id = 1;
	std::pair<std::map<const Value*, unsigned>::iterator, bool> chk;
	/// map global variables
	for(Module::global_iterator mi=M.global_begin(), me=M.global_end(); mi!=me; ++mi) {
		const GlobalVariable *v = &*mi;
		chk = Value2Int.insert( std::pair<const Value*, unsigned>(v, id++) );
		assert( chk.second && "Value Id should be unique");
	}
	/// map functions
	for(Module::iterator mi=M.begin(), me=M.end(); mi!=me; ++mi) {
		const Function *f = &*mi;
		chk = Value2Int.insert( std::pair<const Value*, unsigned>(f, id++) );
		assert( chk.second && "Value Id should be unique");
		/// map arguments
		for(Function::const_arg_iterator ai=f->arg_begin(), ae=f->arg_end(); ai!=ae; ++ai) {
			const Argument *a = &*ai;
			chk = Value2Int.insert( std::pair<const Value*, unsigned>(a, id++) );
			// give the location the argument points to an anonymous id
			id++;
			assert( chk.second && "Value Id should be unique");
		}
	}
	/// map local statements
	for(std::map<const Function*, SEG*>::iterator mi=Func2SEG.begin(), me=Func2SEG.end(); mi!=me; ++mi) {
		SEG *seg = mi->second;
		for(SEG::iterator sni=seg->begin(), sne=seg->end(); sni!=sne; ++sni) {
			SEGNode *sn = &*sni;
			if(sn->isnPnode()==false)
				continue;
			const Instruction *inst = sn->getInstruction();
			// don't need to give id to store/return inst
			// return inst doesn't create new variable
			// Assume the variable defined by StoreInst has already been assigned an Id
			// in previous allocaInst. Otherwise, the variable is casted from non-pointer
			// variable, which is untractable, then treat it points everywhere.
			if(isa<StoreInst>(inst) | isa<ReturnInst>(inst))
				continue;
			chk = Value2Int.insert( std::pair<const Value*, unsigned>(inst, id++) );
			// give the allocated location an anonymous id
			if(isa<AllocaInst>(inst))
				id++;
			assert( chk.second && "Value Id should be unique");
		}
	}
	return id;
}

void FlowSensitiveAliasAnalysis::initializeFuncWorkList(Module &M){
	for(Module::iterator mi=M.begin(), me=M.end(); mi!=me; ++mi) {
		Function * f = &*mi;
		FuncWorkList.push_back(f);
		initializeStmtWorkList(f);
	}
}

void FlowSensitiveAliasAnalysis::initializeStmtWorkList(Function *F){
	F->dump();
	assert( Func2SEG.find(F)!=Func2SEG.end() && "seg doesn't exist");
	SEG *seg = Func2SEG.find(F)->second;
	StmtList *stmtList = new StmtList;
	for(SEG::iterator si=seg->begin(), se=seg->end(); si!=se; ++si) {
		SEGNode *sn = &*si;
		const Instruction *inst = sn->getInstruction();
		// Return Instruction doesn't define a variable,
		// don't initialize worklist with it.
		if(isa<ReturnInst>(inst))
			continue;
		stmtList->push_back(sn);
	}
	StmtWorkList.insert( std::pair<Function*, StmtList*>(F, stmtList) );
}

void FlowSensitiveAliasAnalysis::setupAnalysis(Module &M) {
	// iterate through each function and each worklist
	std::map<const Function*, StmtList*>::iterator list_iter;
	std::list<SEGNode*>::iterator stmt_iter;
	for (list_iter = StmtWorkList.begin(); list_iter != StmtWorkList.end(); ++list_iter) {
		//Function *f = list_iter->first;
		StmtList* stmtList = list_iter->second;
		for (stmt_iter = stmtList->begin(); stmt_iter != stmtList->end(); ++stmt_iter) {
			SEGNode *sn = *stmt_iter;
			const Instruction *i = sn->getInstruction();
			// set SEGNode id if not StoreInst
			if (!isa<StoreInst>(i)) sn->setId(Value2Int[sn->getInstruction()]);
			// set SEGNode type and perform preprocessing
			if (isa<AllocaInst>(i)) {
				sn->setType(0);
				preprocessAlloc(sn,&Value2Int);
			} else if (isa<PHINode>(i)) {
				sn->setType(1);
				preprocessCopy(sn,&Value2Int);
			} else if (isa<LoadInst>(i)) {
				sn->setType(2);
				preprocessLoad(sn,&Value2Int);
			} else if (isa<StoreInst>(i)) {
				sn->setType(3);
				preprocessStore(sn,&Value2Int);
			}  else if (isa<CallInst>(i)) {
				sn->setType(4);
				// preprocessCall(sn,&Value2Int);
			} else if (isa<ReturnInst>(i)) {
				sn->setType(5);
				// preprocessRet(sn,&Value2Int);
			}  else if (isa<GetElementPtrInst>(i)) {
				sn->setType(6);
				// preprocessGEP(sn,Value2Int);
			}
		}
	}
}

void FlowSensitiveAliasAnalysis::doAnalysis(Module &M) {
	// do SEG initialization
	initializeFuncWorkList(M);
	// setup analysis
	TopLevelPTS = bdd_false();
	setupAnalysis(M);
	// iterate through each function and each worklist
	std::map<const Function*, StmtList*>::iterator list_iter;
	std::vector<SEGNode*>::iterator stmt_iter;
	for (list_iter = StmtWorkList.begin(); list_iter != StmtWorkList.end(); ++list_iter) {
		//Function *f = list_iter->first;
		StmtList* stmtList = list_iter->second;
		while (!stmtList->empty()) {
			SEGNode *sn = stmtList->front();
			stmtList->pop_front();
			switch(sn->getType()) {
				case 0: processAlloc(&TopLevelPTS,sn,&StmtWorkList); break;
				case 1: processCopy(&TopLevelPTS,sn,&StmtWorkList);  break;
				case 2: processLoad(&TopLevelPTS,sn,&StmtWorkList);  break;
				case 3: processStore(&TopLevelPTS,sn,&StmtWorkList); break;
				case 4:
				case 5:
				case 6: break;//do nothing for test;
				//case 4: processCall(&TopLevelPTS,sn);  break;
				//case 5: processRet(&TopLevelPTS,sn);   break;
				//case 6: processGEP(&TopLevelPTS,sn);   break;
				default: assert(false && "Out of bounds Instr Type");
			}
		}
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
//                         "flowsensitive-aa", "Semi-sparse Flow Sensitive Pointer Analysis",
//                         false, true, false)
//INITIALIZE_AG_DEPENDENCY(CallGraph)
//INITIALIZE_PASS_DEPENDENCY(TargetLibraryInfo)
//INITIALIZE_PASS_DEPENDENCY(LoopInfo)
//INITIALIZE_AG_PASS_END(FlowSensitiveAliasAnalysis, AliasAnalysis,
//                        "flowsensitive-aa", "Semi-sparse Flow Sensitive Pointer Analysis",
//                        false, true, false)
