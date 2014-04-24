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

using namespace llvm;

bool FlowSensitiveAliasAnalysis::runOnModule(Module &M){
	// build SEG
	constructSEG(M);
	// build caller map
	initializeCallerMap(&getAnalysis<CallGraph>());
	// initialize value maps
	LocationCount = initializeValueMap(M);
	// printValueMap();
	Int2Str = reverseMap(&Value2Int);
	printReverseMap(Int2Str);
	// initialize bdd library
	pointsToInit(1000,10000,LocationCount);
	// initialize worklists
	initializeFuncWorkList(M);
	// do algorithm	
	doAnalysis(M);
	// done
	dbgs()<<"Analysis Done\n";
	return false;
}

void FlowSensitiveAliasAnalysis::constructSEG(Module &M) {
	for (Module::iterator mi=M.begin(), me=M.end(); mi!=me; ++mi) {
		const Function * f = &*mi;
		// build SEG
		SEG *seg = new SEG(f);
		// extend our inst node map by this SEG
		seg->extendInstNodeMap(&Inst2Node);
		DEBUG(seg->dump());
		Func2SEG.insert( std::pair<const Function*, SEG*>(f, seg) );
	}
}

// build caller map used in return processing
void FlowSensitiveAliasAnalysis::initializeCallerMap(CallGraph *cg) {
	std::map<const Instruction*,SEGNode*>::iterator elt;
	// for each node in the call graph
	for (CallGraph::iterator cit = cg->begin(); cit != cg->end(); ++cit) {
		// get a calling function
		const Function *caller = cit->first;
		CallGraphNode *cn = cit->second;
		// for each callee
		for (CallGraphNode::iterator nit = cn->begin(); nit != cn->end(); ++nit) {
			// extract the call instruction and callee function
			// TODO: what do NULL instructions mean; handle this later
			Value *v = nit->first;
			if (v == NULL) continue;
			assert(isa<CallInst>(v) || isa<InvokeInst>(v));
			Instruction *i = cast<Instruction>(v);
			const Function *callee = nit->second->getFunction();
			// add callee to map if it is not present
			if (Func2Calls.count(callee) == 0) {
				Func2Calls.insert(std::pair<const Function*,CallerEntry*>(callee,new CallerEntry()));
			}
			// add SEGNode for this call, if it exists
			elt = Inst2Node.find(i);
			if (elt != Inst2Node.end()) {
				dbgs() << "CALLERMAP: added static call from " << caller->getName() << " to " << callee->getName() << "\n";
				// insert RetData for this call
				Func2Calls.at(callee)->Calls.insert(std::pair<const Function*,RetData*>(caller,new RetData(&Value2Int,elt->second)));
			}
		}
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
			// If the call instruction deosn't define new variable
			// don't assign id for it.
			if(isa<CallInst>(inst)){
				Value *v = inst->getOperand(0);
				if(isa<Function>(v))
					continue;	
			}
#ifdef ENABLE_OPT_1
			if(sn->singleCopy()){
				SingleCopySNs.push_back(sn);
				DEBUG(dbgs()<<"skip:\t"<<*sn<<"\n");
				continue;
			}
#endif
			chk = Value2Int.insert( std::pair<const Value*, unsigned>(inst, id++) );
			assert( chk.second && "Value Id should be unique");
			// give the allocated location an anonymous id
			if(isa<AllocaInst>(inst)) id++;
		}
#ifdef ENABLE_OPT_1
		for(std::vector<SEGNode *>::iterator vi=SingleCopySNs.begin(), ve=SingleCopySNs.end(); vi!=ve; ++vi){
			SEGNode *sn = *vi;
			DEBUG(sn->dump());
			const Instruction *inst = sn->getInstruction();
			const Value *from = sn->getSource();
			assert( from!=NULL && "must has a source value");
			DEBUG(from->dump());
			std::map<const Value*, unsigned>::iterator mi = Value2Int.find(from);
			assert( mi!=Value2Int.end() && "right hand side of copy instruction has not been added into value map");
			DEBUG(dbgs()<<"assign "<<mi->second<<" to\t"<<*sn<<"\n");
			chk = Value2Int.insert( std::pair<const Value*, unsigned>(inst, mi->second) );
			assert( chk.second && "Value Id should be unique");
		}
		seg->pruneSingleCopy(SingleCopySNs);
#endif
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
		// assign singleCopy same id as its source, already processed
#ifdef ENABLE_OPT_1
		if(isa<ReturnInst>(inst) | sn->singleCopy())
#else
		if(isa<ReturnInst>(inst))
#endif
			continue;
		stmtList->push_back(sn);
	}
	StmtWorkList.insert( std::pair<Function*, StmtList*>(F, stmtList) );
}

void FlowSensitiveAliasAnalysis::preprocessFunction(const Function *f) {
	SEG* seg = Func2SEG.at(f);

	if(seg->isDeclaration())
		return;
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
	globalValueNames = bdd_false();
	for(Module::global_iterator mi=M.global_begin(), me=M.global_end(); mi!=me; ++mi) {
		// add them to toplevel points-to set
		GlobalVariable *v = &*mi;
		assert(Value2Int.find(v)!=Value2Int.end() && "global is not assigned an ID");
		processGlobal(Value2Int.at(v), &TopLevelPTS);
		// add them to global variable pointer set
		globalValueNames = globalValueNames | fdd_ithvar(0,Value2Int.at(v));
	}
	// iterate through each function and each worklist
	// FIXME: shouldn't setup analysis by traversing stmtlist
	// some instruction hasn't been added to stmtlist
	// such as singleCopy, return
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
			// set SEGNode id if exists in Value Map
			if (Value2Int.find(sn->getInstruction())!=Value2Int.end())
				sn->setId(Value2Int[sn->getInstruction()]);
			// set SEGNode type and perform preprocessing
			// FIXME: type is not needed in SEGNode 
			if (isa<AllocaInst>(i)) {
				preprocessAlloc(sn);
			} else if (isa<PHINode>(i)) {
				preprocessCopy(sn);
			} else if (isa<LoadInst>(i)) {
				preprocessLoad(sn);
			} else if (isa<StoreInst>(i)) {
				preprocessStore(sn);
			} else if (isa<CallInst>(i)) {
				preprocessCall(sn);
			} else if (isa<ReturnInst>(i)) {
				// preprocessRet(sn,&Value2Int);
			}  else if (isa<GetElementPtrInst>(i)) {
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
	
      DEBUG(dbgs()<<"TOPLEVEL:\n"; printBDD(LocationCount,Int2Str,TopLevelPTS));
      // DEBUG(std::cout<<std::endl);
	
			dbgs()<<"Processing :\t"<<*sn<<"\t"<<sn->getInstruction()->getOpcodeName()<<"\t"<<isa<CallInst>(sn->getInstruction())<<"\n";
			// DEBUG(fdd_printset(TopLevelPTS));

			switch(sn->getInstruction()->getOpcode()) {
				case Instruction::Alloca:	processAlloc(&TopLevelPTS,sn); break;
				case Instruction::PHI:		processCopy(&TopLevelPTS,sn);  break;
				case Instruction::Load:		processLoad(&TopLevelPTS,sn);  break;
				case Instruction::Store:	processStore(&TopLevelPTS,sn); break;
				case Instruction::Call:   processCall(&TopLevelPTS,sn,&Int2Func,&Func2SEG,globalValueNames); break;
				case Instruction::Ret:
				//if it's self-copy instruction, don't need process instruction itself;
				//propagateAddrTaken if has successors
				//only has one definition, so it won't be merge point for top, don't need
				//to propagateTop.
				case Instruction::GetElementPtr:
				case Instruction::BitCast:
				case Instruction::Invoke:	break;//do nothing for test;
				default: assert(false && "Out of bounds Instr Type");
			}

			// print out sets
			DEBUG(dbgs()<<"NODE INSET:\n"; printBDD(LocationCount,Int2Str,sn->getInSet()));
			DEBUG(dbgs()<<"NODE OUTSET:\n"; printBDD(LocationCount,Int2Str,sn->getOutSet()));
		}
	}
  DEBUG(dbgs()<<"TOPLEVEL:\n"; printBDD(LocationCount,Int2Str,TopLevelPTS));
  // DEBUG(std::cout<<std::endl);
}


/// Register this pass
char FlowSensitiveAliasAnalysis::ID = 0;
static RegisterPass<FlowSensitiveAliasAnalysis> X("fs-aa", "Semi-sparse Flow Sensitive Pointer Analysis",
                                                  false, false);
ModulePass *llvm::createFlowSensitiveAliasAnalysisPass() { return new FlowSensitiveAliasAnalysis(); }
