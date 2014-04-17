//===- /llvm/lib/Analysis/FlowSensitiveAA/SEG.hpp - Semi-Sparse Flow Sensitive Pointer Analysis-===//
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
//===----------------------------------------------------------------------===/


#ifndef LLVM_FSAA_SEG_H
#define LLVM_FSAA_SEG_H
#include "SEGNode.h"
#include "llvm/ADT/GraphTraits.h"
#include "llvm/ADT/ilist.h"
#include "llvm/IR/Instruction.h"
#include "llvm/Support/DataTypes.h"
#include "llvm/Analysis/LoopInfo.h"

#include <set>

namespace llvm {

class SEGNode;
class SEG;

class SEG {
private:
	const Function *Fn;
	bool IsDeclaration;
	const LoopInfo *LI;

	SEGNode *EntryNode;	
	/// List of SEGNode in function
	typedef ilist<SEGNode> SEGNodeListType;
	SEGNodeListType SEGNodes;

	void initialize();
	void applyTransformation();

public:
	SEG(const Function *fn, const LoopInfo *li);
	~SEG();

	/// getFunction - Return LLVM Function this SEG represents for.
	const Function *getFunction() { return Fn; }
	bool isDeclaration()	{ return IsDeclaration; }


	SEGNode *getEntryNode() { return EntryNode; }
	/// viewSEG - this function is used for debugger.
	/// call SEG->viewSEG() and get a ghostview window displaying the
	/// SEG of current function with code for each SEGNode inside.
	//void viewSEG() const;

	/// dump - Print current SEG, useful for debugger use.
	void dump() const;

	/// provide accessors for SEGNode list
	typedef SEGNodeListType::iterator iterator;
	typedef SEGNodeListType::const_iterator const_iterator;

	/// SEGNode accessor funcions
	iterator	begin()		{ return SEGNodes.begin(); }
	const_iterator	begin() const	{ return SEGNodes.begin(); }
	iterator	end()		{ return SEGNodes.end();   }
	const_iterator	end() const	{ return SEGNodes.end();   }

	unsigned	size() const	{ return (unsigned)SEGNodes.size(); }
	bool		empty() const	{ return SEGNodes.empty(); }

	const SEGNode	&front() const	{ return SEGNodes.front(); }
	      SEGNode	&front()	{ return SEGNodes.front(); }
	const SEGNode	&back() const	{ return SEGNodes.back();  }
	      SEGNode	&back() 	{ return SEGNodes.back();  }

	void	insert(SEGNode *SN){
		SEGNodes.push_back(SN);
	}
	
	void	remove(iterator SNI){
		SEGNodes.remove(SNI);
	}

	void	erase(iterator SNI){
		SEGNodes.erase(SNI);
	}
};

//raw_ostream& llvm::operator<<(raw_ostream &OS, const SEG &G);

template <> struct GraphTraits<SEG*> : public GraphTraits<SEGNode*> {
	static NodeType *getEntryNode(SEG *G){
		return &G->front();
	}
	
	//nodes_iterator/begin/end - Allow iteration over all nodes in the graph
	typedef SEG::iterator nodes_iterator;
	static nodes_iterator	nodes_begin(SEG *G)	{ return G->begin(); }
	static nodes_iterator	nodes_end(SEG *G)	{ return G->end();   }
	static unsigned 	size(SEG *G)		{ return G->size();  }
};

template <> struct GraphTraits<const SEG*> : public GraphTraits<const SEGNode*> {
	static NodeType *getEntryNode(const SEG *G){
		return &G->front();
	}

        //nodes_iterator/begin/end - Allow iteration over all nodes in the graph
        typedef SEG::const_iterator nodes_iterator;
        static nodes_iterator   nodes_begin(const SEG *G)     { return G->begin(); }
        static nodes_iterator   nodes_end(const SEG *G)       { return G->end();   }
        static unsigned         size(const SEG *G)            { return G->size();  }
};

template <> struct GraphTraits<Inverse<SEG*> > :
	public GraphTraits<Inverse<SEGNode*> > {
        static NodeType *getEntryNode(Inverse<SEG *> G){
                return &G.Graph->front();
        }
};

template <> struct GraphTraits<Inverse<const SEG*> > :
        public GraphTraits<Inverse<const SEGNode*> > {
        static NodeType *getEntryNode(Inverse<const SEG *> G){
                return &G.Graph->front();
        }
};

}//End llvm namespace


#endif
