#ifndef FSAANALYSIS_H
#define FSAANALYSIS_H

//#define DEBUG_TYPE "flowsensitive-aa"
#include "llvm/Analysis/Passes.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/CFG.h"
#include "llvm/Analysis/CallGraph.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/CallGraph.h"
#include "llvm/Pass.h"
#include "llvm/Target/TargetLibraryInfo.h"
#include "llvm/Support/Debug.h"
#include "bdd.h"
#include "fdd.h"
#include "SEG.h"
#include "BDDMisc.h"
#include "Extra.h"
#include <set>
#include <map>
#include <list>
#include <algorithm>

using namespace llvm;


typedef std::vector<SEGNode*> NodeVec;
typedef std::list<SEGNode*> StmtList;
struct CallerEntry {
	std::map<const Function*,RetData*> Calls;
};
typedef std::map<const Function*,CallerEntry*> CallerMap;
typedef std::map<const Function*, std::list<SEGNode*>*> WorkList;

class FlowSensitiveAliasAnalysis : public ModulePass, public AliasAnalysis {
private:

	/// Func2SEG - mapping from Function to corresponding SEG
	std::map<const Function*, SEG*> Func2SEG;

	/// Value2Int - mapping from Value(Global Variable, Function, Local
	/// Statement are included) to unique Id
	std::map<const Value*, unsigned> Value2Int;

	/// Int2Str - for debugging purposes
	std::map<unsigned int,std::string*> *Int2Str;

	/// FuncWorkList - Functions need to be processed
	std::list<const Function*> FuncWorkList;

	/// StmtWorkList - the main algorithm iterate on it.
	/// For each function, keep a statement list to work on for it.
	std::map<const Function*, StmtList*> StmtWorkList;

	/// Int2Func - keeps a mapping from ints to functions, need for (pre)processCall
	std::map<unsigned int,const Function*> Int2Func;

	/// Inst2Node - keeps mapping from Instruction * to SEGNode *
	InstNodeMap Inst2Node;

	/// Func2Calls - keeps mapping from Function * to SEGNode * callers
	CallerMap Func2Calls;

	/// LocationCount - the total number of top variable and address-taken variable
	unsigned LocationCount;

	/// top level points to graph
	bdd TopLevelPTS;

	/// global values name set
	bdd globalValueNames;

	/// names of load instructions (at the end, unitialized loads point to everything)
	bdd loadNames;

	/// names of constant values
	bdd constantNames;

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

	/// initializeCallerMap - build Function to SEGNode* map for return
	void initializeCallerMap(CallGraph *C);

	/// addCaller - invoked to add callers to caller map
	void addCaller(const Instruction *i, const Function *f);
	void addCaller(SEGNode *c, const Function *f);

	/// handleUnitializedLoads - make these loads point everywhere
	void handleUninitializedLoads();

	/// doAnalysis - performs actual analysis algorithm
	void doAnalysis(Module &M);

	/// setupAnalysis - initializes analysis datastructures void setupAnalysis(Module &M);
	void setupAnalysis(Module &M);

	/// printValueMap - print out debug information of value mapping.
	void printValueMap();

	/// add Int2Func mapping, build default points-to set for arguments
	void preprocessFunction(const Function *f);

public:
	static char ID;
	FlowSensitiveAliasAnalysis() : ModulePass(ID){
		//initializeFlowSensitiveAliasAnalysisPass(*PassRegistry::getPassRegistry());
	}

	virtual void initializePass() {
		InitializeAliasAnalysis(this);
	}

	virtual bool runOnModule(Module &M);

	//copy from noaa

	AliasResult aliasCheck(unsigned int v1, unsigned int v2) {
		assert(v1 <= LocationCount && v2 <= LocationCount);
		// get intersection of two points-to sets
		bdd test = bdd_restrict(TopLevelPTS,fdd_ithvar(0,v1)) & bdd_restrict(TopLevelPTS,fdd_ithvar(0,v2));
		// if they intersect, they may alias
		if (bdd_sat(test)) {
			// if they both point to a single value, they must alias
			if (bdd_satcount(test) == 1.0) return MustAlias;
			else return MayAlias;
		// otherwise, they don't alias
		} else return NoAlias;
	}

	virtual AliasResult alias(const Location &LocA, const Location &LocB) {
		std::map<const Value*, unsigned>::iterator ret1, ret2;
		const Value *v1, *v2;
		unsigned int l1, l2;
		const Type *t1, *t2;
		bool p1, p2, c1, c2;
		// get these location's values, types, indices in BDD, and if they are constants
		v1 = LocA.Ptr;
		v2 = LocB.Ptr;
		t1 = v1->getType();
		t2 = v2->getType();
		p1 = t1->isPtrOrPtrVectorTy() || t1->isVectorTy();
		p2 = t2->isPtrOrPtrVectorTy() || t2->isVectorTy();
		ret1 = Value2Int.find(v1);
		ret2 = Value2Int.find(v2);
		l1 = ret1 == Value2Int.end() ? 0 : ret1->second;
		l2 = ret2 == Value2Int.end() ? 0 : ret2->second;
		c1 = l1 ? bdd_sat(constantNames & fdd_ithvar(0,l1)) : false;
		c2 = l2 ? bdd_sat(constantNames & fdd_ithvar(0,l2)) : false;
		// if they are the same value, they must alias
		if (v1 == v2) return MustAlias;
		// if they are both constants or not pointers, they won't alias (they are different)
		if ((c1 || !p1) && (c2 || !p2)) return NoAlias;
		// if everything -> everything, they may alias
		if (pointsTo(TopLevelPTS,0,0)) return MayAlias;
		// if the two locations are not mapped, they won't alias (they are different)
		if (l1 == 0 && l2 == 0) return NoAlias;
		// if either value points everywhere, they may alias
		else if (l1 != 0 && pointsTo(TopLevelPTS,l1,0)) return MayAlias;
		else if (l2 != 0 && pointsTo(TopLevelPTS,l2,0)) return MayAlias;
		// if one is a constant or pointer, check if the other points to it
		if ((c1 || !p1) && pointsTo(TopLevelPTS,l2,l1)) return MayAlias;
		if ((c2 || !p2) && pointsTo(TopLevelPTS,l1,l2)) return MayAlias;
		// otherwise, check if their points-to sets overlap
		return aliasCheck(l1,l2);
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

	// Preprocess functions perform all static variable lookups for these nodes
	int preprocessAlloc(llvm::SEGNode *sn);
	int preprocessCopy(llvm::SEGNode *sn);
	int preprocessLoad(llvm::SEGNode *sn);
	int preprocessStore(llvm::SEGNode *sn);
	int preprocessCall(llvm::SEGNode *sn);
	int preprocessRet(llvm::SEGNode *sn);

	// Main process functions propagate pointer information through the BDDs
	int processAlloc(bdd *tpts, llvm::SEGNode *sn);
	int processCopy(bdd *tpts,  llvm::SEGNode *sn);
	int processLoad(bdd *tpts,  llvm::SEGNode *sn);
	int processStore(bdd *tpts, llvm::SEGNode *sn);
	int processCall(bdd *tpts,  llvm::SEGNode *sn);
	int processRet(bdd *tpts,   llvm::SEGNode *sn);

	// helper functions for process call
	std::vector<const Function*> *computeTargets(bdd *tpts, SEGNode *sn, int funId, bdd funName, Type *funType);
	void processTarget(bdd *tpts, SEGNode *funNode, bdd filter, const Function *target);
	bdd matchingFunctions(const Value *funCall);

	// Propagation functions automate pushing BDD changes through the SEG and worklists
	bool propagateTopLevel(bdd *oldtpts, bdd *newpart, llvm::SEGNode *sn);
	bool propagateTopLevel(bdd *oldtpts, bdd *newpart, bdd *update, llvm::SEGNode *sn);
	bool propagateAddrTaken(llvm::SEGNode *sn);

	// Process global variables
	void preprocessGlobal(unsigned int id, bdd *tpts);
	void processGlobal(unsigned int id, bdd *tpts, GlobalVariable *g);
	void initializeGlobals(Module &M);

	// Process undef values
	int preprocessUndef(llvm::SEGNode *sn);
	int processUndef(bdd *tpts, llvm::SEGNode *sn);
};

std::map<unsigned int,std::string*> *reverseMap(std::map<const Value*,unsigned int> *m);
void printReverseMap(std::map<unsigned int,std::string*> *m);

#endif /* FSAANALYSIS_H */
