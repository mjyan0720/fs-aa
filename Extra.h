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
		bool isPtr;
		bool isDefinedFunc;
		llvm::Type *functionType;
		std::vector<const llvm::Function*> *targets;
		CallData() {
			targets = NULL;
		}
};
struct RetData : public ExtraData {
		std::vector<llvm::SEGNode*> callsites;
};

#endif /* EXTRA_H */
