//===-- llvm/lib/Analysis/FlowSensitiveAA/SEG.cpp ------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// description here
//
//===----------------------------------------------------------------------===//

#include "SEGNode.h"
#include "SEG.h"
#include "llvm/Support/LeakDetector.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Support/Debug.h"
#include "llvm/ADT/SCCIterator.h"

#undef  DEBUG_TYPE
#define DEBUG_TYPE "fsaa-seg"

using namespace llvm;

SEGNode::SEGNode(SEG *parent) : Inst(NULL), Parent(parent) {
	IsnPnode = false;
	Defined = true;
	AddrTaken = true;
	Extra = NULL;
#ifdef ENABLE_OPT_1
	SingleCopy = false;
	Source = NULL;
	UndefSource=true;
#endif
}

SEGNode::SEGNode(const Instruction * inst, SEG *parent) : Inst(inst), Parent(parent) {
	IsnPnode = isa<AllocaInst>(inst) | isa<PHINode>(inst)  | isa<LoadInst>(inst) |
			isa<StoreInst>(inst)  | isa<CallInst>(inst) | isa<ReturnInst>(inst) |
			isa<GetElementPtrInst>(inst) | isa<CastInst>(inst) | isa<InvokeInst>(inst);
	Defined = true;
	AddrTaken = isa<LoadInst>(inst) | isa<StoreInst>(inst)  | isa<CallInst>(inst) | 
		    isa<ReturnInst>(inst) | isa<InvokeInst>(inst);
	Extra = NULL;
#ifdef ENABLE_OPT_1
	SingleCopy = isa<GetElementPtrInst>(inst) | isa<CastInst>(inst);
	Source = NULL;
	UndefSource=true;
#endif
}

SEGNode::~SEGNode() {
	if(ArgIds!=NULL)
		delete ArgIds;
	if(StaticData!=NULL)
		delete StaticData;
	if(Extra!=NULL)
		delete Extra;
}

void SEGNode::addPredecessor(SEGNode *pred) {
	Predecessors.insert(pred);
}

void SEGNode::removePredecessor(SEGNode *pred) {
	pred_iterator I = std::find(Predecessors.begin(), Predecessors.end(), pred);
	assert( I != Predecessors.end() && "Pred is not a predecessor of this SEGNode");
	Predecessors.erase(I);
}

void SEGNode::transferPredecessor(SEGNode *from) {
	if(this == from)
		return;

	while(from->pred_size()!=0){
		SEGNode *Pred = *from->pred_begin();
		Pred->addSuccessor(this);
		Pred->removeSuccessor(from);
	}
}

void SEGNode::addSuccessor(SEGNode *succ) {
	Successors.insert(succ);
	succ->addPredecessor(this);
}

void SEGNode::removeSuccessor(SEGNode *succ) {
	succ_iterator I = std::find(Successors.begin(), Successors.end(), succ);
	assert(I != Successors.end() && "Succ is not a successor of this SEGNode");
	Successors.erase(I);
	succ->removePredecessor(this);
}

void SEGNode::transferSuccessor(SEGNode *from) {
	if(this == from)
		return;

	while(from->succ_size()!=0){
		SEGNode *Succ = *from->succ_begin();
		addSuccessor(Succ);
		from->removeSuccessor(Succ);
	}
}

void SEGNode::addDef(SEGNode *def){
	Defs.insert(def);
}

void SEGNode::addUser(SEGNode *user) {
	Users.insert(user);
	user->addDef(this);
}

void SEGNode::removeDef(SEGNode *def){
	def_iterator I = std::find(Defs.begin(), Defs.end(), def);
	assert( I!=Defs.end() && "Def is not a definition of this SEGNode");
	Defs.erase(I);
}

void SEGNode::removeUser(SEGNode *user) {
	user_iterator I = std::find(Users.begin(), Users.end(), user);
	assert(I != Users.end() && "User is not a user of this SEGNode");
	Users.erase(I);
	user->removeDef(this);
}

void SEGNode::eraseFromParent() {
	assert( getParent() && "Not embedded in a function!");
	assert( pred_size()==0 && "Has predecessors!");
	assert( succ_size()==0 && "Has successors!");
	//remove def and use chain
	while(def_size()!=0){
		SEGNode *def = *this->def_begin();
		def->removeUser(this);
	}
	while(user_size()!=0){
		SEGNode *user = *this->user_begin();
		this->removeUser(user);
	}
	getParent()->erase(this);
}


void SEGNode::dump() const {
	if(Inst==NULL)
		dbgs()<<"ENTRY\n";
	else
		Inst->dump();

	//printout predecessor
	dbgs()<<"Predecessor("<<Predecessors.size()<<"):\n";
	for(pred_iterator pi=Predecessors.begin(), pe=Predecessors.end(); pi!=pe; ++pi){
		SEGNode *sn = *pi;
		dbgs()<<*sn<<"\n";
	}

	dbgs()<<"Successors("<<Successors.size()<<"):\n";
	for(succ_iterator si=Successors.begin(), se=Successors.end(); si!=se; ++si){
		SEGNode *sn = *si;
		dbgs()<<*sn<<"\n";
	}
}

raw_ostream &llvm::operator<<(raw_ostream &OS, const SEGNode &SN) {
	const Instruction *I = SN.getInstruction();
	if(I==NULL)
		OS<<"ENTRY";
	else
		I->print(OS);
	return OS;
}
