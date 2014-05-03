#include <set>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include "llvm/Pass.h"
#include "llvm/Analysis/Passes.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Debug.h"

using namespace llvm;

class AliasPrinter : public ModulePass {

	// alias analysis used to print out which value pairs alias
	AliasAnalysis *AA;
	// set stores every global LLVM value
	std::set<const Value*> G;
	// map stores every value defined in a function
	std::map<const Function*,std::set<const Value*>*> F;
	// set which stores everything
	std::set<const Value*> W;
	// output file name
	FILE *O;
	// do we only do alias checking in the same function?
	bool wholeProg;

	// require an alias analysis pass
	virtual void getAnalysisUsage(AnalysisUsage &AU) const {
		AU.addRequired<AliasAnalysis>();
	}

	// print out alias analysis info
	virtual bool runOnModule(Module &M) {
		// get alias information
		AA = &getAnalysis<AliasAnalysis>();
		// build value vector
		enumerateValues(M);
		// print out alias results here
		if (wholeProg) printInterFunctionAliases();
		else printIntraFunctionAliases();
		// cleanup memory
		cleanup(); // does not modify other results
		return true;
	}

	void cleanup() {
		std::map<const Function*,std::set<const Value*>*>::iterator fi,fe;
		// flush the output buffer
		dbgs().flush();
		// clear sets we used
		G.clear();
		for (fi = F.begin(), fe = F.end(); fi != fe; ++fi)
			delete fi->second;
		F.clear();
		// close the file we opened
		if (O != NULL) fclose(O);
	}

	void enumerateValues(Module &M) {
		// enumerate globals, add to value set
		for(Module::global_iterator gi=M.global_begin(), ge=M.global_end(); gi!=ge; ++gi) {
			G.insert(&*gi);
			W.insert(&*gi);
		}
		// enumerate each function, add to value set
		for (Module::iterator fi=M.begin(), fe=M.end(); fi!=fe; ++fi) {
			G.insert(&*fi);
			W.insert(&*fi);
			std::set<const Value*> *V = new std::set<const Value*>();
			F.insert(std::pair<const Function*,std::set<const Value*>*>(&*fi,V));
			// enumerate each function argument, add to value set
			for(Function::const_arg_iterator ai=fi->arg_begin(), ae=fi->arg_end(); ai!=ae; ++ai) {
				V->insert(&*ai);
				W.insert(&*ai);
			}
			// enumerate each basic block
			for (Function::iterator bi = fi->begin(), be = fi->end(); bi != be; ++bi)
				// enumerate each instruction in a block, add to value set
				for (BasicBlock::iterator ii = bi->begin(), ie = bi->end(); ii != ie; ++ii) {
					V->insert(&*ii);
					W.insert(&*ii);
				}
		}
	}

	// print aliasable values through the whole program
	#define VOID(v) (*v)->getType()->isVoidTy()
	void printInterFunctionAliases() {
		std::set<const Value*>::iterator i1,i2,ve;
		// exploit symmetric nature of relation, only print out one side
		ve = W.end();
		for (i1 = W.begin(); i1 != ve; ++i1) {
			for (i2 = i1; i2 != ve; ++i2) {
				// skip this is values are void or if they are identical
				if (*i1 == *i2 || VOID(i1) || VOID(i2)) continue;
				printMapping(AA->alias(*i1,*i2),*i1,*i2);
			}
		}
	}

	// print aliasable values per function
	void printIntraFunctionAliases() {
		std::map<const Function*,std::set<const Value*>*>::iterator fi,fe;
		std::set<const Value*>::iterator i1,i2,ve,ge;
		ge = G.end();
		// iterate over each function
		for (fi = F.begin(), fe = F.end(); fi != fe; ++fi) {
			ve = fi->second->end();
			// iterate other the values in each function, do they alias:
			for (i1 = fi->second->begin(); i1 != ve; ++i1) {
				// other function values?
				for (i2 = i1; i2 != ve; ++i2) {
					if (*i1 == *i2 || VOID(i1) || VOID(i2)) continue;
					printMapping(AA->alias(*i1,*i2),*i1,*i2);
				}
				// global values?
				for (i2 = G.begin(); i2 != ge; ++i2)
					if (*i1 == *i2 || VOID(i1) || VOID(i2)) continue;
					printMapping(AA->alias(*i1,*i2),*i1,*i2);
			}
		}
		// check if globals alias each other
		ge = G.end();
		for (i1 = G.begin(); i1 != ge; ++i1) {
			for (i2 = i1; i2 != ge; ++i2) {
					if (*i1 == *i2 || VOID(i1) || VOID(i2)) continue;
					printMapping(AA->alias(*i1,*i2),*i1,*i2);
			}
		}
	}

	// print out an alias mapping
	void printMapping(AliasAnalysis::AliasResult res, const Value *v1, const Value *v2) {
		// print out alias result
		switch (res) {
			case AliasAnalysis::NoAlias:      dbgs() << "NONE    : "; break;
			case AliasAnalysis::MayAlias:     dbgs() << "MAY     : "; break;
			case AliasAnalysis::MustAlias:    dbgs() << "MUST    : "; break;
			case AliasAnalysis::PartialAlias: dbgs() << "PARTIAL : "; break;
		}
		// print out value names for debugging purposes
		printValue(v1);
		dbgs() << " : ";
		printValue(v2);
		dbgs() << "\n";
	}

	// print a single value's name out
	void printValue(const Value *v) {
		if (isa<Instruction>(v)) {
			dbgs() << cast<Instruction>(v)->getParent()->getParent()->getName() << "_" << v->getName();
		} else if (isa<Argument>(v)) {
			dbgs() << cast<Argument>(v)->getParent()->getName() << "_" << v->getName();
		} else if (isa<GlobalValue>(v)) {
			dbgs() << "GLOBAL_" << v->getName();
		} else {
			assert(false && "Unknown Value Type");
		}
	}

	// add code to do initialize this module
	public:
		static char ID;
		AliasPrinter() : ModulePass(ID) {
			// get ouput file
			char *oname = getenv("FSAA_OUTPUT");
			if (oname != NULL) O = fopen(oname,"w");
			else O = NULL;
			// check whether we operate on the whole program at once
			wholeProg = getenv("FSAA_WHOLEPROG") != NULL;
		}
};

char AliasPrinter::ID = 0;
static RegisterPass<AliasPrinter> X("paa", "Print Alias Analysis Results", true, true);

/* vim: set ts=2 sw=2 : */
