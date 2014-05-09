//===- FlowSensitiveAliasAnalysis.cpp - Semi-Sparse Flow Sensitive Pointer Analysis-===// //
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
#include "FSAAnalysis.h"
#include "llvm/ADT/Statistic.h"

STATISTIC(Functions,   "Functions: The # of functions in the module");
STATISTIC(UninitLoads, "Uninit Loads: The # of uninitialized loads in the module");
STATISTIC(LoadAgain,   "Nodes Forced By Load: The # of nodes processed again due to uninitialized loads");

using namespace llvm;

#undef  DEBUG_TYPE
#define DEBUG_TYPE "fsaa-preprocess"
bool FlowSensitiveAliasAnalysis::runOnModule(Module &M){
	// build SEG
	constructSEG(M);
	// initialize value maps
	LocationCount = initializeValueMap(M);
	// initialize bdd library
	pointsToInit(30000000,1000000,LocationCount);
	// build caller map
	initializeCallerMap(&getAnalysis<CallGraph>());
	DEBUG(printValueMap());
	Int2Str = reverseMap(&Value2Int);
	DEBUG(printReverseMap(Int2Str));
	// initialize worklists
	initializeFuncWorkList(M);
	// do algorithm
	doAnalysis(M);
	// done
	dbgs()<<"Analysis Done\n";
	return false;
}

#undef  DEBUG_TYPE
#define DEBUG_TYPE "fsaa-seg"
void FlowSensitiveAliasAnalysis::constructSEG(Module &M) {
	for (Module::iterator mi=M.begin(), me=M.end(); mi!=me; ++mi) {
		Functions ++;
		const Function * f = &*mi;
		// build SEG
		SEG *seg = new SEG(f);
		// extend our inst node map by this SEG
		seg->extendInstNodeMap(&Inst2Node);
		// DEBUG(seg->dump());
		Func2SEG.insert( std::pair<const Function*, SEG*>(f, seg) );
	}
}

#undef  DEBUG_TYPE
#define DEBUG_TYPE "fsaa-callermap"
void FlowSensitiveAliasAnalysis::addCaller(const Instruction *callInst, const Function *callee) {
	// add SEGNode for this call, if it exists
	std::map<const Instruction*,SEGNode*>::iterator elt;
	elt = Inst2Node.find(callInst);
	if (elt == Inst2Node.end()) {
		DEBUG(dbgs() << "CALLER: " << *callInst << " NOT FOUND\n");
		return;
	}
	addCaller(elt->second,callee);
}

void FlowSensitiveAliasAnalysis::addCaller(SEGNode *callInst, const Function *callee) {
	const Function *caller = callInst->getParent()->getFunction();
	// if callee is NULL, this is an indirect call; we will add it later
	if (callee == NULL) {
		DEBUG(dbgs() << "NULL CALLEE\n");
		return;
	}
	// add callee to map if it is not present
	if (Func2Calls.count(callee) == 0) {
		Func2Calls.insert(std::pair<const Function*,CallerEntry*>(callee,new CallerEntry()));
	}
	// add callInst to callee's internal map, insert RetData for this call
	DEBUG(dbgs() << "CALL FROM " << caller->getName() << " TO " << callee->getName() << "\n");
	Func2Calls.at(callee)->Calls.insert(std::pair<const Function*,RetData*>(caller,new RetData(&Value2Int,callInst)));
}

// build caller map used in return processing
void FlowSensitiveAliasAnalysis::initializeCallerMap(CallGraph *cg) {
	std::map<const Instruction*,SEGNode*>::iterator elt;
	// for each node in the call graph
	for (CallGraph::iterator cit = cg->begin(); cit != cg->end(); ++cit) {
		// get a calling function
		CallGraphNode *cn = cit->second;
		// for each callee
		for (CallGraphNode::iterator nit = cn->begin(); nit != cn->end(); ++nit) {
			// extract the call instruction and callee function
			// TODO: what do NULL instructions mean; handle this later
			Value *v = nit->first;
			if (v == NULL) {
				DEBUG(dbgs() << "NULL CALLER\n");
				continue;
			}
			assert(isa<CallInst>(v) || isa<InvokeInst>(v));
			Instruction *i = cast<Instruction>(v);
			const Function *callee = nit->second->getFunction();
			// add the caller
			addCaller(i,callee);
		}
	}
}

#undef  DEBUG_TYPE
#define DEBUG_TYPE "fsaa-valuemap"
unsigned FlowSensitiveAliasAnalysis::initializeValueMap(Module &M){
	unsigned id = 1;
	std::pair<std::map<const Value*, unsigned>::iterator, bool> chk;
	/// map global variables
	for(Module::global_iterator mi=M.global_begin(), me=M.global_end(); mi!=me; ++mi) {
		const GlobalVariable *v = &*mi;
		chk = Value2Int.insert( std::pair<const Value*, unsigned>(v, id++) );
		// each global variable is a pointer, assign another id for what it points to
		id++;
		assert(chk.second && "Value Id should be unique");
	}
	/// map functions
	for(Module::iterator mi=M.begin(), me=M.end(); mi!=me; ++mi) {
		const Function *f = &*mi;
		/// extra id for each function
		chk = Value2Int.insert( std::pair<const Value*, unsigned>(f, id++) );
		id++;
		assert(chk.second && "Value Id should be unique");
		/// map arguments
		for(Function::const_arg_iterator ai=f->arg_begin(), ae=f->arg_end(); ai!=ae; ++ai) {
			const Argument *a = &*ai;
			chk = Value2Int.insert( std::pair<const Value*, unsigned>(a, id++) );
			// give the location the argument points to an anonymous id
			id++;
			assert(chk.second && "Value Id should be unique");
		}
	}
	/// map local statements
	for(std::map<const Function*, SEG*>::iterator mi=Func2SEG.begin(), me=Func2SEG.end(); mi!=me; ++mi) {
		SEG *seg = mi->second;
#ifdef ENABLE_OPT_1
		std::vector<SEGNode *> SingleCopySNs;
		SingleCopySNs.clear();
#endif
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
			// if return value is void, I don't care about it
			if(isa<CallInst>(inst) && inst->getType()->isVoidTy()){
					continue;
			}
#ifdef ENABLE_OPT_1
			if(sn->singleCopy() && !sn->undefSource()){
				SingleCopySNs.push_back(sn);
				DEBUG(dbgs()<<"SKIP SINGLECOPY:\t"<<*sn<<"\n");
				continue;
			}
#endif
			chk = Value2Int.insert( std::pair<const Value*, unsigned>(inst, id++) );
			assert(chk.second && "Value Id should be unique");
			// give the allocated location an anonymous id
			if(isa<AllocaInst>(inst)) id++;
		}
#ifdef ENABLE_OPT_1
		for(std::vector<SEGNode *>::iterator vi=SingleCopySNs.begin(), ve=SingleCopySNs.end(); vi!=ve; ++vi){
			SEGNode *sn = *vi;
			DEBUG(sn->dump());
			const Instruction *inst = sn->getInstruction();
			const Value *from = sn->getSource();
			assert(from!=NULL && "must has a source value");
			DEBUG(from->dump());
			std::map<const Value*, unsigned>::iterator mi = Value2Int.find(from);
			assert(mi!=Value2Int.end() && "right hand side of copy instruction has not been added into value map");
			DEBUG(dbgs() << "VALUEMAP: ASSIGN " << mi->second << " TO " << *sn << "\n");
			chk = Value2Int.insert( std::pair<const Value*, unsigned>(inst, mi->second) );
			assert( chk.second && "Value Id should be unique");
		}
		seg->pruneSingleCopy(SingleCopySNs);
#endif
	}
	return id;
}

#undef  DEBUG_TYPE
#define DEBUG_TYPE "fsaa-worklist"
void FlowSensitiveAliasAnalysis::initializeFuncWorkList(Module &M){
	for(Module::iterator mi=M.begin(), me=M.end(); mi!=me; ++mi) {
		Function * f = &*mi;
		FuncWorkList.push_back(f);
		initializeStmtWorkList(f);
	}
}

void FlowSensitiveAliasAnalysis::initializeStmtWorkList(Function *F){
	DEBUG(dbgs() << "WORKLIST FOR: " << F->getName() << "\n");
	assert( Func2SEG.find(F)!=Func2SEG.end() && "seg doesn't exist");
	SEG *seg = Func2SEG.find(F)->second;
	StmtList *stmtList = new StmtList;
	for(SEG::iterator si=seg->begin(), se=seg->end(); si!=se; ++si) {
		SEGNode *sn = &*si;
		const Instruction *inst = sn->getInstruction();
		// Return Instruction doesn't define a variable,
		// don't initialize worklist with it.
		// assign singleCopy same id as its source, already processed
#ifdef ENABLE_OPT_1
		if(isa<ReturnInst>(inst) | (sn->singleCopy() && !sn->undefSource()))
#else
		if(isa<ReturnInst>(inst))
#endif
			continue;
		stmtList->push_back(sn);
	}
	StmtWorkList.insert(std::pair<Function*, StmtList*>(F, stmtList));
}

#undef  DEBUG_TYPE
#define DEBUG_TYPE "fsaa-preprocess"
void FlowSensitiveAliasAnalysis::preprocessFunction(const Function *f) {
	SEG* seg = Func2SEG.at(f);
	// don't need to preprocess declarations
	if (seg->isDeclaration()) return;
	SEGNode *entry = seg->getEntryNode();
	std::vector<bdd> *StaticData = new std::vector<bdd>();
	std::vector<unsigned int> *ArgIds = new std::vector<unsigned int>();
	unsigned int fid = Value2Int.at(f);
	// add to Int2Func mapping
	Int2Func.insert(std::pair<unsigned int,const Function *>(fid+1,f));
	// function's hidden pair to points-to set
	TopLevelPTS = TopLevelPTS | (fdd_ithvar(0,fid) & fdd_ithvar(1,fid+1));
	// for each parameter, add it's hidden pair to the points-to set
	for(Function::const_arg_iterator ai=f->arg_begin(), ae=f->arg_end(); ai!=ae; ++ai) {
		unsigned int argid = Value2Int.at(&*ai);
		bdd arg = fdd_ithvar(0,argid);
		// add argument id to argids
		ArgIds->push_back(argid);
		// add points-to pair to Top points-to set
		TopLevelPTS = TopLevelPTS | (arg & fdd_ithvar(1,argid+1));
		// add argument to static data
		StaticData->push_back(arg);
	}
	// set argids and static data for node
	entry->setArgIds(ArgIds);
	entry->setStaticData(StaticData);
}

void FlowSensitiveAliasAnalysis::preprocessGlobal(unsigned int id, bdd *tpts) {
	*tpts = *tpts | (fdd_ithvar(0,id) & fdd_ithvar(1,id+1));
}

// recurse through nested constants to find an underlying value
const Value *unwindConstant(const Constant *c) {
	Instruction *i;
	// handle constant expr case
	if (isa<ConstantExpr>(c)) {
		i = cast<ConstantExpr>(const_cast<Constant*>(c))->getAsInstruction();
		// if this instruction is a cast, recurse on it's first operand
		if (i->isCast())
			unwindConstant(cast<Constant>(i->getOperand(0)));
		// if it is a GEP, get it's pointer
		else if (isa<GetElementPtrInst>(i))
			unwindConstant(cast<Constant>(cast<GetElementPtrInst>(i)->getPointerOperand()));
	// handle function or global variable case case
	} else if (isa<Function>(c) || isa<GlobalVariable>(c)) {
			return c;
	}
	// we don't know how to handle this instruction; exit
	return NULL;
}

// process simple constant expressions in global declarations
void FlowSensitiveAliasAnalysis::processGlobal(unsigned int id, bdd *tpts, GlobalVariable *g) {
	unsigned int cid;
	const Value *v;
	// if this global has no initializer, ignore it
	if (!g->hasInitializer()) return;
	// get the value from this global's constant expression
	v = unwindConstant(g->getInitializer());
	// if the value is not in the value map, ignore it
	if (v == NULL || !Value2Int.count(v)) return;
	// otherwise, update the BDD so the global points to the constant value
	DEBUG(dbgs() << "GLOBAL: MAP FROM " << g->getName() << " -> " << v->getName() << "\n";);
	cid = Value2Int.at(v);
	*tpts = (*tpts | (fdd_ithvar(0,id) & fdd_ithvar(1,cid))) &
		bdd_not(fdd_ithvar(0,id) & fdd_ithvar(1,id+1));
}

void FlowSensitiveAliasAnalysis::initializeGlobals(Module &M) {
	// preprocess all global variables
	globalValueNames = bdd_false();
	for (Module::global_iterator mi=M.global_begin(), me=M.global_end(); mi!=me; ++mi) {
		// add them to toplevel points-to set
		GlobalVariable *v = &*mi;
		assert(Value2Int.find(v)!=Value2Int.end() && "global is not assigned an ID");
		preprocessGlobal(Value2Int.at(v), &TopLevelPTS);
		// add them to global variable pointer set
		globalValueNames = globalValueNames | fdd_ithvar(0,Value2Int.at(v));
	}
	// process all global variables with constant expressions
	for (Module::global_iterator mi=M.global_begin(), me=M.global_end(); mi!=me; ++mi)
		processGlobal(Value2Int.at(&*mi),&TopLevelPTS,&*mi);
	// propagate global pts to each function's entry node
	bdd GlobalPTS = TopLevelPTS & globalValueNames;
	for (std::map<const Function*, SEG*>::iterator mi=Func2SEG.begin(), me=Func2SEG.end(); mi!=me; ++mi) {
		// get SEG entry node
		if (mi->second->isDeclaration()) continue;
		SEGNode *entry = mi->second->getEntryNode();
		// setup entry node inset and outset
		entry->setInSet(GlobalPTS);
		entry->setOutSet(GlobalPTS);
		// propagate global data
		propagateAddrTaken(entry);
	}
}

#undef  DEBUG_TYPE
#define DEBUG_TYPE "fsaa-toplevel"
// NOTE: only use this function after the regular analysis
// make loads that point nowhere, point everywhere
void FlowSensitiveAliasAnalysis::handleUninitializedLoads() {
	// get loads that point no-where
	bdd initializedLoads = bdd_exist(TopLevelPTS & loadNames,fdd_ithset(1));
	bdd uninitializedLoads = bdd_not(initializedLoads) & loadNames;
	// print debugging information
	DEBUG(dbgs() << "UNINIT LOADS\n"; printBDD(LocationCount,Int2Str,uninitializedLoads););
	// make uninitialized loads point everywhere
	TopLevelPTS |= uninitializedLoads & fdd_ithvar(1,0);
}

void FlowSensitiveAliasAnalysis::setupAnalysis(Module &M) {
	// intialize points-to sets for globals, propagate to each function's entry node
	initializeGlobals(M);
	// iterate through each function and each node
	for (std::map<const Function*, SEG*>::iterator mi=Func2SEG.begin(), me=Func2SEG.end(); mi!=me; ++mi) {
		// preprocess functions
		preprocessFunction(mi->first);
		// add function names to constants list
		constantNames |= fdd_ithvar(0,Value2Int.at(mi->first));
		// preprocess every node in SEG
		SEG *seg = mi->second;
		for(SEG::iterator sni=seg->begin(), sne=seg->end(); sni!=sne; ++sni) {
			SEGNode *sn = &*sni;
			const Instruction *i = sn->getInstruction();
			// set SEGNode id if exists in Value Map
			if (Value2Int.find(sn->getInstruction())!=Value2Int.end())
				sn->setId(Value2Int[sn->getInstruction()]);
			// perform preprocessing on SEGNode
			if (isa<AllocaInst>(i)) {
				preprocessAlloc(sn);
			} else if (isa<PHINode>(i)) {
				preprocessCopy(sn);
			} else if (isa<LoadInst>(i)) {
				// add this load's name to loadNames
				loadNames |= fdd_ithvar(0,Value2Int.at(sn->getInstruction()));
				preprocessLoad(sn);
			} else if (isa<StoreInst>(i)) {
				preprocessStore(sn);
			} else if (isa<CallInst>(i) || isa<InvokeInst>(i)) {
				preprocessCall(sn);
			} else if (isa<ReturnInst>(i)) {
				preprocessRet(sn);
			} else if (isa<CastInst>(i) || isa<GetElementPtrInst>(i)) {
#ifndef ENABLE_OPT_1
				// treat as copy
				preprocessCopy(sn);
#else
				if(sn->undefSource()){
					DEBUG(dbgs() << "PREPROCESS UNDEF SINGLE COPY:" << *sn << "\n");
					preprocessUndef(sn);
				}
#endif
			} else if (!sn->isnPnode()) {
				// do nothing
			} else {
				assert(false && "Unknown instruction");
			}
		}
	}
}

void FlowSensitiveAliasAnalysis::doAnalysis(Module &M) {
	int ret = 0;
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
			// debugging statements
#undef  DEBUG_TYPE
#define DEBUG_TYPE "fsaa-toplevel"
			DEBUG(dbgs()<<"TOPLEVEL:\n"; printBDD(LocationCount,Int2Str,TopLevelPTS));
			DEBUG(dbgs()<<"Processing :\t"<<*sn<<"\t"<<sn->getInstruction()->getOpcodeName()<<"\t"<<isa<CallInst>(sn->getInstruction())<<"\n");
			// if this is a preserving node, just propagateAddrTaken
			if (!sn->isnPnode()) {
				propagateAddrTaken(sn);
				continue;
			}
#ifdef ENABLE_OPT_1
			// if this a copy of a node, ignore it
			if(sn->singleCopy() && sn->undefSource()){
				processUndef(&TopLevelPTS, sn);
				continue;
			}
#endif
			// otherwise, do standard processing
			switch(sn->getInstruction()->getOpcode()) {
				case Instruction::Alloca: ret = processAlloc(&TopLevelPTS,sn); break;
				case Instruction::PHI:	  ret = processCopy(&TopLevelPTS,sn);  break;
				case Instruction::Load:	  ret = processLoad(&TopLevelPTS,sn);  break;
				case Instruction::Store:  ret = processStore(&TopLevelPTS,sn); break;
				case Instruction::Invoke:
				case Instruction::Call:   ret = processCall(&TopLevelPTS,sn);  break;
				case Instruction::Ret:    ret = processRet(&TopLevelPTS,sn);   break;
				//if it's self-copy instruction, don't need process instruction itself;
				//propagateAddrTaken if has successors
				//only has one definition, so it won't be merge point for top, don't need
				//to propagateTop.
				case Instruction::GetElementPtr:
				// convert instructions
				case Instruction::Trunc:
				case Instruction::ZExt:
				case Instruction::SExt:
				case Instruction::FPTrunc:
				case Instruction::FPExt:
				case Instruction::FPToUI:
				case Instruction::FPToSI:
				case Instruction::UIToFP:
				case Instruction::SIToFP:
				case Instruction::IntToPtr:
				case Instruction::PtrToInt:
				// case Instruction::AddrSpaceCast:
				// end of convert instructions
				case Instruction::BitCast:
#ifdef ENABLE_OPT_1
					propagateAddrTaken(sn);
#else
					ret = processCopy(&TopLevelPTS,sn);
#endif
					break;
				default: assert(false && "Out of bounds Instr Type");
			}
			// if ret is non-zero, stop
			if (ret) {
				dbgs() << "EVERYTHING ALIASES\n";
				break;
			}
			// print out sets
#undef  DEBUG_TYPE
#define DEBUG_TYPE "fsaa-addrtaken"
			DEBUG(dbgs()<<"NODE INSET:\n"; printBDD(LocationCount,Int2Str,sn->getInSet()));
			DEBUG(dbgs()<<"NODE OUTSET:\n"; printBDD(LocationCount,Int2Str,sn->getOutSet()));
		}
	}
	// handle uninitialized loads
	handleUninitializedLoads();
	// print ouf final points-to set
#undef  DEBUG_TYPE
#define DEBUG_TYPE "fsaa-result"
	DEBUG(dbgs()<<"\nFINAL:\n"; printBDD(LocationCount,Int2Str,TopLevelPTS));
	DEBUG(std::cout<<std::endl);
}

/// Register this pass
char FlowSensitiveAliasAnalysis::ID = 0;
static RegisterPass<FlowSensitiveAliasAnalysis> X("fs-aa", "Semi-sparse Flow Sensitive Pointer Analysis",
                                                  false, false);
ModulePass *llvm::createFlowSensitiveAliasAnalysisPass() { return new FlowSensitiveAliasAnalysis(); }
