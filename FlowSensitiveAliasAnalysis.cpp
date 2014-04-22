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

std::map<unsigned int,std::string*> *INV_MAP;
LLVMContext *CONTEXT;

std::map<unsigned int,std::string*> *reverseMap(std::map<const Value*,unsigned int> *m) {
	std::pair<std::map<unsigned int,std::string*>::iterator,bool> ret;
	std::map<unsigned int,std::string *> *inv = new std::map<unsigned int,std::string*>();
	// build inverse map (also check map is 1-to-1)
	for (std::map<const Value*,unsigned int>::iterator it = m->begin(); it != m->end(); ++it) {
		const Value *v = it->first;
		ret = inv->insert(std::pair<unsigned int,std::string*>(it->second,new std::string(it->first->getName().data())));
		assert(ret.second);
		// if this is an alloca inst, add name for hidden inst
		if (isa<AllocaInst>(v)) { 
			std::string *name = new std::string(std::string(v->getName()) + "_HEAP");
			ret = inv->insert(std::pair<unsigned int,std::string*>(it->second+1,name)); 
			assert(ret.second);
		} else if (isa<GlobalVariable>(v)) {
			std::string *name = new std::string(std::string(v->getName()) + "_VALUE");
			ret = inv->insert(std::pair<unsigned int,std::string*>(it->second+1,name)); 
			assert(ret.second);
		} else if (isa<Function>(v)) {
			std::string *name = new std::string(std::string(v->getName()) + "_FUNCTION");
			ret = inv->insert(std::pair<unsigned int,std::string*>(it->second+1,name)); 
			assert(ret.second);
		}
	}
  // store everything value 
  inv->insert(std::pair<unsigned int,std::string*>(0,new std::string("everything")));
	return inv;
}

void print_handler(std::ostream &o, int var) {
	o << INV_MAP->at((unsigned int)var);
}

void file_handler(FILE* f, int var) {
  fprintf(f,"%s",INV_MAP->at((unsigned int)var)->c_str());
}

void printReverseMap(std::map<unsigned int,std::string*> *m) {
	for (std::map<unsigned int,std::string*>::iterator it = m->begin(); it != m->end(); ++it) {
		dbgs() << it->first << " : " << *(it->second) << "\n";
	}
}

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
	std::list<const Function*> FuncWorkList;

	/// StmtWorkList - the main algorithm iterate on it.
	/// For each function, keep a statement list to work on for it.
	std::map<const Function*, StmtList*> StmtWorkList;

	/// Int2Func - keeps a mapping from ints to functions, need for (pre)processCall
	std::map<unsigned int,const Function*> Int2Func;

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

  /// printBDD
  void printBDD(bdd b);

	void preprocessFunction(const Function *f);

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

	AliasResult aliasCheck(unsigned int v1, unsigned int v2) {
		assert(v1 <= LocationCount && v2 <= LocationCount);
		bdd test = bdd_restrict(TopLevelPTS,fdd_ithvar(0,v1)) & bdd_restrict(TopLevelPTS,fdd_ithvar(0,v2));
		if (bdd_sat(test)) {
			if (bdd_satcount(test) == 1.0) return MustAlias;
			else return MayAlias;
		} else return NoAlias;
	}

	virtual AliasResult alias(const Location &LocA, const Location &LocB) {
/*	  std::map<const Value*, unsigned>::iterator ret1, ret2;
		const Value *v1, *v2;
		unsigned int l1, l2;
		v1 = LocA.Ptr;
		v2 = LocB.Ptr;
		ret1 = Value2Int.find(v1);
		ret2 = Value2Int.find(v2);
		l1 = ret1 == Value2Int.end() ? 0 : ret1->second;
		l2 = ret2 == Value2Int.end() ? 0 : ret2->second;	
		if (l1 == 0 && l2 == 0) return NoAlias;
		else if (l1 != 0 && pointsTo(*TopLevelPTS,l1,0)) return MayAlias;
		else if (l2 != 0 && pointsTo(*TopLevelPTS,l2,0)) return MayAlias;
		else return aliasCheck(l1,l2);
*/
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
	printValueMap();

	CONTEXT = &M.getContext();
	INV_MAP = reverseMap(&Value2Int);
	printReverseMap(INV_MAP);
	
	pointsToInit(1000,10000,LocationCount);

	initializeFuncWorkList(M);
	
	doAnalysis(M);
	dbgs()<<"Analysis Done\n";
	return false;
}

void FlowSensitiveAliasAnalysis::constructSEG(Module &M) {
	for(Module::iterator mi=M.begin(), me=M.end(); mi!=me; ++mi) {
		const Function * f = &*mi;
		SEG *seg = new SEG(f);
		DEBUG(seg->dump());
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
		// each global variable is a pointer, assign another id for what it points to
		id ++;
		assert( chk.second && "Value Id should be unique");
	}
	/// map functions
	for(Module::iterator mi=M.begin(), me=M.end(); mi!=me; ++mi) {
		const Function *f = &*mi;
		/// extra id for each function
		chk = Value2Int.insert( std::pair<const Value*, unsigned>(f, id++) );
		id++;
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
			// If the call instruction deosn't define new variable
			// don't assign id for it.
			if(isa<CallInst>(inst)){
				Value *v = inst->getOperand(0);
				if(isa<Function>(v))
					continue;	
			}		
			chk = Value2Int.insert( std::pair<const Value*, unsigned>(inst, id++) );
			assert( chk.second && "Value Id should be unique");
			// give the allocated location an anonymous id
			if(isa<AllocaInst>(inst)) id++;
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
  // DEBUG(F->dump());
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

void FlowSensitiveAliasAnalysis::printBDD(bdd b) {
	unsigned int i, j;
	for (i=0;i<LocationCount;++i) {
		for (j=0;j<LocationCount;++j) {
			if (bdd_sat(b & fdd_ithvar(0,i) & fdd_ithvar(1,j)))
			//if (bdd_satcount(b & (fdd_ithvar(0,i) & fdd_ithvar(1,j))) > 0.0) 
				dbgs() << *((*INV_MAP)[i]) << " -> " << *((*INV_MAP)[j]) << "\n";
			//else
			//	dbgs() << "None\n";
		}
	}
}

void FlowSensitiveAliasAnalysis::preprocessFunction(const Function *f) {
	SEG* seg = Func2SEG.at(f);
	SEGNode *entry = seg->getEntryNode();
	std::vector<bdd> *StaticData = new std::vector<bdd>();
	// add to Int2Func mapping
	Int2Func.insert(std::pair<unsigned int,const Function *>(Value2Int.at(f),f));
	// for each parameter, add it's hidden pair to the points-to set
	for(Function::const_arg_iterator ai=f->arg_begin(), ae=f->arg_end(); ai!=ae; ++ai) {
		unsigned int argid = Value2Int.at(&*ai);
		bdd arg = fdd_ithvar(0,argid);
		// add points-to pair to Top points-to set
		TopLevelPTS = TopLevelPTS | (arg & fdd_ithvar(1,argid+1));
		// add argument to static data
		StaticData->push_back(arg);	
	}	
	// set static data for node
	entry->setStaticData(StaticData);
}

void processGlobal(unsigned int id, bdd *tpts) {
  *tpts = *tpts | (fdd_ithvar(0,id) & fdd_ithvar(1,id+1));
}

void FlowSensitiveAliasAnalysis::setupAnalysis(Module &M) {
	// preprocess all global variables
	bdd gvarpts = bdd_false();
	for(Module::global_iterator mi=M.global_begin(), me=M.global_end(); mi!=me; ++mi) {
		// add them to toplevel points-to set
		GlobalVariable *v = &*mi;
		assert(Value2Int.find(v)!=Value2Int.end() && "global is not assigned an ID");
		processGlobal(Value2Int.at(v), &TopLevelPTS);
		// add them to global variable pointer set
		gvarpts = gvarpts | fdd_ithvar(0,Value2Int.at(v));
	}
	// iterate through each function and each worklist
	std::map<const Function*, StmtList*>::iterator list_iter;
	std::list<SEGNode*>::iterator stmt_iter;
	for (list_iter = StmtWorkList.begin(); list_iter != StmtWorkList.end(); ++list_iter) {
		// preprocess this function header
		preprocessFunction(list_iter->first);
		StmtList* stmtList = list_iter->second;
		// preprocess each instruction in the function
		for (stmt_iter = stmtList->begin(); stmt_iter != stmtList->end(); ++stmt_iter) {
			SEGNode *sn = *stmt_iter;
			const Instruction *i = sn->getInstruction();
			// set SEGNode id if not StoreInst
			if (!isa<StoreInst>(i)) sn->setId(Value2Int[sn->getInstruction()]);
			// set SEGNode type and perform preprocessing
			// FIXME: type is not needed in SEGNode 
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
				preprocessCall(sn,&Value2Int,gvarpts);
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

	// setup analysis
	TopLevelPTS = bdd_false();
	setupAnalysis(M);

	// iterate through each function and each worklist
	while(!FuncWorkList.empty()){
		const Function *f = FuncWorkList.front();
		FuncWorkList.pop_front();
		StmtList *stmtList = StmtWorkList.find(f)->second;
		while (!stmtList->empty()) {
			SEGNode *sn = stmtList->front();
			stmtList->pop_front();
	
      // DEBUG(printBDD(TopLevelPTS));
      // DEBUG(std::cout<<std::endl);
	
			dbgs()<<"Processing :\t"<<*sn<<"\t"<<sn->getInstruction()->getOpcodeName()<<"\t"<<isa<CallInst>(sn->getInstruction())<<"\n";
			// DEBUG(fdd_printset(TopLevelPTS));
			switch(sn->getInstruction()->getOpcode()) {
				case Instruction::Alloca:	processAlloc(&TopLevelPTS,sn,&StmtWorkList); break;
				case Instruction::PHI:		processCopy(&TopLevelPTS,sn,&StmtWorkList);  break;
				case Instruction::Load:		processLoad(&TopLevelPTS,sn,&StmtWorkList);  break;
				case Instruction::Store:	processStore(&TopLevelPTS,sn,&StmtWorkList); break;
				case Instruction::Call:   processCall(&TopLevelPTS,sn,&StmtWorkList,&FuncWorkList,&Int2Func,&Func2SEG); break;
				case Instruction::Ret:
				case Instruction::GetElementPtr:
				case Instruction::Invoke:	break;//do nothing for test;
				default: assert(false && "Out of bounds Instr Type");
			}

			// print out sets
			// DEBUG(dbgs()<<"NODE INSET:\n"; printBDD(sn->getInSet()));
			// DEBUG(dbgs()<<"NODE OUTSET:\n"; printBDD(sn->getOutSet()));
		}
	}
  // DEBUG(printBDD(TopLevelPTS));
  // DEBUG(std::cout<<std::endl);
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
