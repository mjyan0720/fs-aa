#ifndef EXTRA_H
#define EXTRA_H

#include "llvm/IR/Instruction.h"
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
struct RetData : public ExtraData {
		std::vector<llvm::SEGNode*> callsites;
};

#endif /* EXTRA_H */
