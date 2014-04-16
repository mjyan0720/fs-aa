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

#define DEBUG_TYPE "flowsensitive-aa"
#include "SEG.h"
#include "llvm/Support/LeakDetector.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Support/Debug.h"
#include "llvm/ADT/SCCIterator.h"


using namespace llvm;


SEG::SEG(const Function *fn, const LoopInfo *li) : Fn(fn), LI(li){
	IsDeclaration = fn->isDeclaration();
	initialize();
	applyTransformation();
}

void SEG::dump() const {
	dbgs()<<"SEG("<<this->size()<<")\n";
        for(SEG::const_iterator sni=this->begin(), sne=this->end(); sni!=sne; ++sni){
		const SEGNode *sn = &*sni;
                for(SEGNode::succ_iterator succi=sn->succ_begin(), succe=sn->succ_end(); succi!=succe; ++succi)
                       dbgs()<<*sni<<" --> "<<**succi<<"\n";
	}
}



void SEG::initialize() {
	std::map<const Instruction*, SEGNode*> inst2sn;
	for(Function::const_iterator bbi=Fn->begin(), bbe=Fn->end(); bbi!=bbe; ++bbi){
		const BasicBlock *blk = &(*bbi);
		for(BasicBlock::const_iterator insti=blk->begin(), inste=blk->end(); insti!=inste; ++insti){
			const Instruction *I = &(*insti);
			SEGNode *sn = new SEGNode(I, this);
			this->insert(sn);
			inst2sn.insert( std::pair<const Instruction*, SEGNode*>(I, sn) );
		}	
	}

	for(SEG::iterator sni=this->begin(), sne=this->end(); sni!=sne; ++sni){
		SEGNode *sn = &*sni;
		const Instruction *I = sn->getInstruction();
		const BasicBlock *blk = I->getParent();
		if(I == &(blk->back())){
			for(succ_const_iterator succBlki=succ_begin(blk), succBlke=succ_end(blk); succBlki!=succBlke; ++succBlki){
				const BasicBlock *succBlk = *succBlki;
				BasicBlock::const_iterator succInstPos = succBlk->begin();
				const Instruction *succInst = &*succInstPos;
				assert(inst2sn.find(succInst)!=inst2sn.end() && "successor instruction is not in seg");
				SEGNode *succSN = inst2sn.find(succInst)->second;
				sn->addSuccessor(succSN);
				DEBUG(errs()<<"Set edges: "<<*sn<<" --> "<<*succSN<<"\n");
			}
		} else {
			const Instruction *succInst = blk->getInstList().getNext(I);
			//BasicBlock::iterator instPos = std::find(blk->begin, blk->end(), *I);
			//Instruction *succInst = &*(++instPos);
			assert(inst2sn.find(succInst)!=inst2sn.end() && "successor instruction is not in seg");
			SEGNode *succSN = inst2sn.find(succInst)->second;
			sn->addSuccessor(succSN);
			DEBUG(errs()<<"Set edges: "<<*sn<<" --> "<<*succSN<<"\n");
		}
		for(Value::const_use_iterator usei=I->use_begin(), usee=I->use_end(); usei!=usee; ++usei){
			const Instruction *useInst = dyn_cast<Instruction>(*usei);
			assert( useInst!=NULL && "user must be an instruction");
			assert(inst2sn.find(useInst)!=inst2sn.end() && "successor instruction is not in seg");
			SEGNode *useSN = inst2sn.find(useInst)->second;
			sn->addUser(useSN);
			DEBUG(errs()<<"Set uses: "<<*sn<<" --> "<<*useSN<<"\n");
		}
	}
	DEBUG(
	for(SEG::iterator sni=this->begin(), sne=this->end(); sni!=sne; ++sni)
		for(SEGNode::succ_iterator succi=(&*sni)->succ_begin(), succe=(&*sni)->succ_end(); succi!=succe; ++succi)
			errs()<<*sni<<" --> "<<**succi<<"\n"
	);

}

void SEG::applyTransformation(){
	std::set<SEGNode*> WorkList;
	for(SEG::iterator sni=this->begin(), sne=this->end(); sni!=sne; ++sni){
		SEGNode *sn = &*sni;
		if(sn->isnPnode()==false){
			WorkList.insert(sn);
		}
	}
	bool change = true;
	while( change==true && !WorkList.empty()){
		change = false;
		//applyT2
/*		for(std::set<SEGNode*>::iterator wli=WorkList.begin(), wle=WorkList.end(); wli!=wle; ++wli){
			SEGNode *sn = *wli;
			//remove self-loop
			SEGNode::succ_iterator I = std::find(sn->succ_begin(), sn->succ_end(), sn);
			if(I!=sn->succ_end()){
				sn->removeSuccessor(sn);
			}
			if(sn->pred_size()==1){
				DEBUG(errs()<<"Apply T2 : "<<*sn<<"\n");
				SEGNode *pred = *(sn->pred_begin());
				pred->transferSuccessor(sn);
				pred->removeSuccessor(sn);
				sn->eraseFromParent();
				change = true;
			}
		}
*/		//applyT4
		for(scc_iterator<SEG*> I=scc_begin(this), E=scc_end(this); I != E; ++I){
			std::vector<SEGNode *> &SCC = *I;
			assert( !SCC.empty() && "SCC with no instructions");
		
			if(SCC.size()==1)//it's a single node, not connected part
				continue;
			
			bool applicable = true;
			for(unsigned i=0, e=SCC.size(); i!=e; ++i){
				if(SCC[i]->isnPnode()==true){
					applicable = false;
					break;
				}
			}

			if(applicable){//all nodes in SCC are Pnodes
				DEBUG(errs()<<"Apply T4("<<SCC.size()<<") : \n");
				DEBUG(
					for(unsigned i=0, e=SCC.size(); i!=e; ++i)
						errs()<<"  "<<*SCC[i]<<"\n"
				);
				//keep the first one as header
				SEGNode *header = SCC[0];
				for(unsigned i=1, e=SCC.size(); i!=e; ++i){
					SEGNode *sn = SCC[i];
					header->transferSuccessor(sn);
					header->transferPredecessor(sn);
					sn->eraseFromParent();
					SEGNode::succ_iterator I = std::find(header->succ_begin(), header->succ_end(), header);
					assert( I!=header->succ_end() && "not SCC");
					header->removeSuccessor(header);
				}
				change = true;
				break;				
			}
		}

	}

}

SEG::~SEG() {
	LeakDetector::removeGarbageObject(this);
}

/*
vim SEG::viewSEG() const {
#ifndef NDEBUG
	ViewGraph(this, "mf" + Fn->getName());
#else
	errs() << "MachineFunction::viewCFG is only available in debug builds on "
		<< "systems with Graphviz or gv!\n";
#endif // NDEBUG
}
*/

