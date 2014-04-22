#ifndef EXTRA_H
#define EXTRA_H

#include "llvm/IR/Instruction.h"
#include "bdd.h"

namespace llvm {
	class SEGNode;
}

class ExtraData {};
class CallData : public ExtraData {
	public:
		bool isPtr;
		bool isDefined;
	  std::vector<const llvm::Function*> *targets;
	  std::map<const llvm::Function*, std::vector<bdd>*> *targetParams;
};
class RetData  : public ExtraData {
	public:
		std::vector<llvm::SEGNode*> callsites;
};

#endif /* EXTRA_H */
