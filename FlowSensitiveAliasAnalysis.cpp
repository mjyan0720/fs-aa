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
//#include "llvm/Analysis/CallGraph.h"
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
//#include <vector>

using namespace llvm;


namespace {



class FlowSensitiveAliasAnalysis : public ModulePass, public AliasAnalysis {
private:
	std::map<Function*, SEG*> SEGs;

	LoopInfo * LI;

/*	std::set<SEGNode*> WorkList;//inter-procedure variable in graph calculation
        

	void initializeSEG(Module &M);
	void constructSEG(Function *F);//entry function to construct SEG Grpah for each function
	void splitBlock(Function *F);
	bool applyT2(SEGNode *x, SEG *seg);
	bool applyT4(SEGNode *v, SEG *seg);
	void pruneUse(SEGGraphTy * seg);
*/
	virtual void getAnalysisUsage(AnalysisUsage &AU) const {
		AU.addRequired<AliasAnalysis>();
		AU.addRequired<TargetLibraryInfo>();
		AU.addRequired<CallGraph>();	
		AU.addRequired<LoopInfo>();
		//AU.addRequired<TargetTransformInfo>();
	}

public:
		static char ID;
		FlowSensitiveAliasAnalysis() : ModulePass(ID){
			initializeFlowSensitiveAliasAnalysisPass(*PassRegistry::getPassRegistry());
		}

		virtual void initializePass() {
			InitializeAliasAnalysis(this);
 		}

		virtual bool runOnModule(Module &M);
	//	virtual AliasResult alias(const Location &LocA, const Location &LocB);
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

/*    virtual void deleteValue(Value *V) {}
    virtual void copyValue(Value *From, Value *To) {}
    virtual void addEscapingUse(Use &U) {}
  */  
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
	DEBUG(errs()<<"begin to initialize SEG\n");

	SEG *seg;
        for(Module::iterator mi=M.begin(), me=M.end(); mi!=me; mi++){
                Function * f = &*mi;
//                DEBUG(errs()<<"construct SEG\n");
                //DEBUG(f->dump());
                if(f->isDeclaration())
                        continue;
		DEBUG(f->dump());
                LI = &getAnalysis<LoopInfo>(*f);
                seg = new SEG(f, LI);
		seg->dump();
        }
	//DEBUG(errs()<<*seg);
//	LI = &getAnalysis<LoopInfo>();
//	initializeSEG(M);
//	DEBUG(errs()<<"print SEG:"<<SEGs.size()<<"\n");
//	printSEG();
	return false;
}
/*
void FlowSensitiveAliasAnalysis::printSEG()
{
	for(SEGsTy::iterator si=SEGs.begin(), se=SEGs.end(); si!=se; ++si){
		Function * f = si->first;
		SEGGraphTy * seg = si->second;
		DEBUG(f->dump());
		for(SEGGraphTy::iterator gi=seg->begin(), ge=seg->end(); gi!=ge; ++gi){
			Value* v = gi->first;
			SEGInfo * vInfo = gi->second;
			DEBUG(v->dump());
			DEBUG(errs()<<"successors:"<<vInfo->successor.size()<<"\n");
			for(ValueSet::iterator succi=vInfo->successor.begin(), succe=vInfo->successor.end(); succi!=succe; ++succi){
				DEBUG((*succi)->dump());
			}
			DEBUG(errs()<<"users:"<<vInfo->userChain.size()<<"\n");
			for(std::set<User*>::iterator useri=vInfo->userChain.begin(), usere=vInfo->userChain.end(); useri!=usere; ++useri){
				DEBUG((*useri)->dump());
			}
			DEBUG(errs()<<"-------------------\n");
		}
	}	
}

void FlowSensitiveAliasAnalysis::initializeSEG(Module &M)
{
	for(Module::iterator mi=M.begin(), me=M.end(); mi!=me; mi++){
		Function * f = &*mi;
		DEBUG(errs()<<"construct SEG\n");
		//DEBUG(f->dump());
		if(f->isDeclaration())
			continue;
		LI = &getAnalysis<LoopInfo>(*f);
		constructSEGGraph(f);
	}
}

AliasAnalysis::AliasResult FlowSensitiveAliasAnalysis::alias(const Location &LocA, const Location &LocB)
{
	//assert(AA && "AA didn't call InitializeAliasAnalysis in its run method!");
	return MayAlias;
}


bool FlowSensitiveAliasAnalysis::checkPnode(Instruction* I)
{
	if(isa<AllocaInst>(I) || isa<PHINode>(I) || isa<LoadInst>(I) || isa<StoreInst>(I) || isa<CallInst>(I) || isa<ReturnInst>(I))
		return false;
	else
		return true;
}

SEGGraphTy * FlowSensitiveAliasAnalysis::splitBlock(Function * F)
{
	SEGGraphTy * seg = new SEGGraphTy();
	SEGs.insert( std::pair<Function*, SEGGraphTy*>(F, seg));
	DEBUG(errs()<<"insert 1 function to SEGs\n");
	WorkList.clear();
	SCCElement.clear();
	for(Function::iterator bbi=F->begin(), bbe=F->end(); bbi!=bbe; ++bbi){
		BasicBlock* blk = &(*bbi);
		for(BasicBlock::iterator insti=blk->begin(), inste=blk->end(); insti!=inste; ++insti){
			Instruction * I = &(*insti);//blk is the parent basic block of I
			SEGInfo * info = new SEGInfo();// = new SEGInfo();
			info->IsPnode = checkPnode(I);
			if(info->IsPnode){
				WorkList.insert(I);//push preserve nodes into worklist
			}
			//set up predecessors for I
			//if it's the first instruction in current block, its predecessors are the
			//union of blk's predecessors' terminate instructions.
			//else set its predecessor the previous instruction in blk.
			if(I == &blk->front()){
				for(pred_iterator PI=pred_begin(blk), PE=pred_end(blk); PI!=PE; ++PI){
					info->predecessor.insert(&(*PI)->back());
				}	
			} else {
				--insti;
				info->predecessor.insert(&*insti);
				++insti;
			}
			//set up successors for I
			//if it's the last instruction in current block, its successors are the 
			//union of blk's successors' begining instructions.
			//else set its successor the next instruction in blck.
			if(I == &blk->back()){
				for(succ_iterator SI=succ_begin(blk), SE=succ_end(blk); SI!=SE; ++SI){
					info->successor.insert(&(*SI)->front());
				}
			} else {
				++insti;
				info->successor.insert(&*insti);
				--insti;
			}
			seg->insert( std::pair<Value*, SEGInfo*>(I, info) );
		}
	}

	return seg;
}

bool FlowSensitiveAliasAnalysis::applyT4(Value* v, SEGGraphTy* seg){
	SCCElement.clear();
	//Value* v = *vi;
	SEGInfo * vInfo = seg->find(v)->second;
	//LoopInfo *LI = &getAnalysis<LoopInfo>();
	if(!LI->getLoopFor(cast<Instruction>(v)->getParent()))
		return false;
	Visited.clear();
	DEBUG(errs()<<"begin detect SCC\n");
	if(detectSCC(v, v, SCCElement, seg)){//find a SCC
		for(std::set<Value*>::iterator si=SCCElement.begin(), se=SCCElement.end(); si!=se; ++si){
			Value* scc = *si;
			SEGInfo * sccInfo = seg->find(scc)->second;
			vInfo->predecessor.insert(sccInfo->predecessor.begin(), sccInfo->predecessor.end());
			vInfo->successor.insert(sccInfo->successor.begin(), sccInfo->successor.end());
			for(std::set<Value*>::iterator prei=sccInfo->predecessor.begin(), pree=sccInfo->predecessor.end(); prei!=pree; ++prei){
				Value* sccPre = *prei;
				SEGInfo * sccPreInfo = seg->find(sccPre)->second;
				sccPreInfo->successor.insert(v);
				sccPreInfo->successor.erase(scc);
			}
			for(std::set<Value*>::iterator succi=sccInfo->successor.begin(), succe=sccInfo->successor.end(); succi!=succe; ++succi){
				Value* sccSucc = *succi;
				SEGInfo * sccSuccInfo = seg->find(sccSucc)->second;
				sccSuccInfo->predecessor.insert(v);
				sccSuccInfo->predecessor.erase(scc);
			}
		}
		for(ValueSet::iterator vi=SCCElement.begin(), ve=SCCElement.end(); vi!=ve; ++vi){
			Value* sccv = *vi;
			vInfo->predecessor.erase(sccv);
			vInfo->successor.erase(sccv);
			DEBUG(errs()<<"delete : ");
			DEBUG(sccv->dump());
			WorkList.erase(sccv);
			seg->erase(sccv);
		}
		return true;
	}
	return false;
}

void FlowSensitiveAliasAnalysis::constructSEGGraph(Function* F)
{
	SEGGraphTy * seg = splitBlock(F);

	//set def use for all instructions
	bool change = true;
	while(change==true && !WorkList.empty()){
		change = false;
		for(std::set<Value*>::iterator vi=WorkList.begin(); vi!=WorkList.end(); ){
			Value * v = *vi;
			++vi;
			DEBUG(errs()<<"check\t("<< WorkList.size() <<") : ");
			DEBUG(v->dump());
			if(applyT2(v, seg)){
				WorkList.erase(v);
				DEBUG(errs()<<"apply T2 : ");
				DEBUG(v->dump());
				change = true;
			}
		}

		for(std::set<Value*>::iterator vi=WorkList.begin(), ve=WorkList.end(); vi!=ve; ++vi){
			if(applyT4(*vi, seg)){
				DEBUG(errs()<<"apply T4 : ");
				DEBUG((*vi)->dump());
				change = true;
				break;
			}
		}
	}	
	pruneUse(seg);
}

void FlowSensitiveAliasAnalysis::pruneUse(SEGGraphTy * seg)
{
	for(std::map<Value*, SEGInfo*>::iterator segi=seg->begin(), sege=seg->end(); segi!=sege; ++segi){
		Value* v = segi->first;
		DEBUG(errs()<<"prune def-use for : ");
		DEBUG(v->dump());
		SEGInfo * vInfo = segi->second;
		for(Value::use_iterator ui=v->use_begin(), ue=v->use_end(); ui!=ue; ++ui){
			User* vUser = *ui;
			if(seg->find(vUser)==seg->end())
				continue;
			SEGInfo * uInfo = seg->find(vUser)->second;
			if(uInfo->IsPnode)
				continue;
			vInfo->userChain.insert(vUser);
		}
	}
}

bool FlowSensitiveAliasAnalysis::applyT2(Value* v, SEGGraphTy * seg){
	SEGInfo * vInfo = seg->find(v)->second;
	if(!vInfo->IsPnode)
		return false;
	if(vInfo->predecessor.size()!=1)
		return false;
	Value * pre = *vInfo->predecessor.begin();
	SEGInfo * preInfo = seg->find(pre)->second;
	preInfo->successor.insert(vInfo->successor.begin(), vInfo->successor.end());
	preInfo->successor.erase(v);
	for(std::set<Value*>::iterator si=vInfo->successor.begin(), se=vInfo->successor.end(); si!=se; ++si){
		SEGInfo * succInfo = seg->find(*si)->second;
		succInfo->predecessor.insert(pre);
		succInfo->predecessor.erase(v);
	}
	//TODO delete v from seggraph
	seg->erase(v);
	return true;
}

bool FlowSensitiveAliasAnalysis::detectSCC(Value* start, Value* current, ValueSet & sccElement, SEGGraphTy * seg){
	DEBUG(errs()<<"**check ");
	DEBUG(current->dump());
	DEBUG(errs()<<"**");
	for(std::set<Value*>::iterator si=seg->find(current)->second->successor.begin(), se=seg->find(current)->second->successor.end(); si!=se; ++si){
	//	DEBUG(errs()<<"**");
		Value* x = *si;
		SEGInfo * xInfo = seg->find(x)->second;
		if(!xInfo->IsPnode || x==current)
			continue;
		//LoopInfo *LI = &getAnalysis<LoopInfo>();
		if(!LI->getLoopFor(cast<Instruction>(x)->getParent()))
			continue;
	//	if(getOutermostLoop(LI, x)!=getOutermostLoop(LI, start))
	//		continue;
			
		if(x!=start && Visited.find(x)!=Visited.end())
			continue;

		Visited.insert(x);

		if(x==start)
			return true;

		

		if(detectSCC(start, x, sccElement, seg)){
			sccElement.insert(x);
			//DEBUG(errs()<<"get one SCC\n");
			return true;
		}
		
		Visited.erase(x);
	}
	return false;
}
*/
//register this pass
char FlowSensitiveAliasAnalysis::ID = 0;
//INITIALIZE_AG_PASS_BEGIN(FlowSensitiveAliasAnalysis, AliasAnalysis,
//			"flowsensitive-aa", "Semi-sparse Flow Sensitive Pointer Analysis",
//			false, true, false)
//INITIALIZE_AG_DEPENDENCY(CallGraph)
//INITIALIZE_PASS_DEPENDENCY(TargetLibraryInfo)
//INITIALIZE_PASS_DEPENDENCY(LoopInfo)
//INITIALIZE_AG_PASS_END(FlowSensitiveAliasAnalysis, AliasAnalysis,
//                        "flowsensitive-aa", "Semi-sparse Flow Sensitive Pointer Analysis",
//                        false, true, false)
static RegisterPass<FlowSensitiveAliasAnalysis> X("fs-aa", "Semi-sparse Flow Sensitive Pointer Analysis",
	                  false, false);

ModulePass *llvm::createFlowSensitiveAliasAnalysisPass() { return new FlowSensitiveAliasAnalysis(); }


//FIXME change FlowSensitiveAliasAnalysis ==> FlowSensitiveAA
//TODO comment

