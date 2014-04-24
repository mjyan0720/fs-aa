#ifndef EXTRA_H
#define EXTRA_H

#include "llvm/IR/Instruction.h"
#include "SEGNode.h"
#include "bdd.h"

namespace llvm {
	class SEGNode;
}

struct ExtraData {
	virtual ~ExtraData() {}
};

struct CallData : public ExtraData {
		bool isPtr;                                   // is this call a function pointer
		bool isDefinedFunc;                           // is this function defined in our value map
		unsigned int funcId;                          // what is the id of this function in the value map (0 if undefined)
		bdd  funcName;                                // what is the bdd representing this function (unused if undefined)
		bdd  argset;                                  // a bdd representing all argument names (a1 | a2 | a3 ... )
		llvm::Type *funcType;                         // the type of this function (note all called functions are pointers)
		std::vector<const llvm::Function*> *targets;  // the possible targets of this call
		CallData() {
			targets = NULL;
		}
};

#define NO_RET    0
#define UNDEF_RET 1
#define DEF_RET   2
struct RetData {
	llvm::SEGNode *retInst; // stores SEGNode for this call
	unsigned int retStatus; // stores NO_RET, UNDEF_RET, or DEF_RET
	                        // NO_RET : call doesn't save ret, UNDEF_RET : call saves, but not defined, DEF_RET : call saves and defined
	bdd retName;            // stores bdd name for saved return value
	RetData(std::map<const llvm::Value*,unsigned> *im, llvm::SEGNode *sn) {
		retInst = sn;
		const llvm::Instruction *i = sn->getInstruction();
		if (i->getType()->isVoidTy()) {
			retStatus = NO_RET;
			retName   = bdd_false();
		} else if (im->count(i)) {
			retStatus = DEF_RET;
			retName   = fdd_ithvar(0,im->at(i));
		} else {
			retStatus = UNDEF_RET;
			retName   = fdd_ithset(0);
		}
	}
};

#endif /* EXTRA_H */
