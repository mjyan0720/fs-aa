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

#define NO_SAVE    0
#define UNDEF_SAVE 1
#define DEF_SAVE   2
struct RetData {
	llvm::SEGNode *callInst; // stores SEGNode for this call
	unsigned int callStatus; // stores NO_SAVE, UNDEF_SAVE, or DEF_SAVE
	                         // NO_SAVE : call doesn't save ret, UNDEF_SAVE : call saves, but not defined, DEF_SAVE : call saves and defined
	bdd saveName;             // stores bdd name for saved return value
	RetData(std::map<const llvm::Value*,unsigned> *im, llvm::SEGNode *sn) {
		callInst = sn;
	 	const llvm::Instruction *i = sn->getInstruction();
		// TODO: is this the right check to see if the return value is unused
		if (i->getType()->isVoidTy()) {
			callStatus = NO_SAVE;
			saveName   = bdd_false();
		} else if (im->count(i)) {
			callStatus = DEF_SAVE;
			saveName   = fdd_ithvar(0,im->at(i));
		} else {
			callStatus = UNDEF_SAVE;
			saveName   = fdd_ithset(0);
		}
	}
};

#endif /* EXTRA_H */
