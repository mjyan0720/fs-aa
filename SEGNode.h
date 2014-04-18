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


#ifndef LLVM_FSAA_SEGNODE_H
#define LLVM_FSAA_SEGNODE_H

#include "llvm/ADT/GraphTraits.h"
#include "llvm/ADT/ilist.h"
#include "llvm/IR/Instruction.h"
#include "llvm/Support/DataTypes.h"
#include "llvm/Analysis/LoopInfo.h"
#include "bdd.h"
#include <set>

namespace llvm {

class SEGNode;
class SEG;

class SEGNode : public ilist_node<SEGNode>{
private:
	/// Inst - each node of SEG is one instruction
	const Instruction *Inst;

	/// IsnPnode - Indicate whether this node is preserving node or not
	/// if it neither defines nor uses pointer information, it's false
	bool IsnPnode;
	
	/// Parent - Indicate SEG this node resides in.
	SEG *Parent;

	/// Predecessors/Successors - keep track of the predecessor / successor
	/// nodes
	std::set<SEGNode *> Predecessors;
	std::set<SEGNode *> Successors;
	std::set<SEGNode *> Users;
	std::set<SEGNode *> Defs;

	/// In and Out Points-To Sets as BDDs
  bdd in, out;

	/// Identifier of this SEGNode in the BDD
	unsigned int id;
public:
	SEGNode() { }

	explicit SEGNode(SEG *parent);

	explicit SEGNode(const Instruction *inst, SEG *parent);
	
	~SEGNode();
	
	friend class SEG;

	/// getInstruction - Return LLVM Instruction the node contains
	const Instruction *getInstruction() const { return Inst; }
	bool	isnPnode() { return IsnPnode; }
	SEG *getParent() { return Parent; }

	/// SEG-CFG iterators
	typedef std::set<SEGNode *>::iterator	pred_iterator;
	typedef std::set<SEGNode *>::const_iterator	const_pred_iterator;
	typedef std::set<SEGNode *>::iterator	succ_iterator;
	typedef std::set<SEGNode *>::const_iterator	const_succ_iterator;
	typedef std::set<SEGNode *>::iterator	user_iterator;
	typedef std::set<SEGNode *>::const_iterator	const_user_iterator;
	typedef std::set<SEGNode *>::iterator	def_iterator;
	typedef std::set<SEGNode *>::const_iterator	const_def_iterator;

	pred_iterator		pred_begin()	{ return Predecessors.begin(); }
	const_pred_iterator	pred_begin() const { return Predecessors.begin(); }
	pred_iterator		pred_end()	{ return Predecessors.end();	}
	const_pred_iterator 	pred_end() const { return Predecessors.end();	}
	unsigned		pred_size() const { return (unsigned)Predecessors.size(); }
	succ_iterator		succ_begin()	{ return Successors.begin();	}
	const_succ_iterator	succ_begin() const { return Successors.begin(); }
	succ_iterator		succ_end()	{ return Successors.end();	}
	const_succ_iterator	succ_end() const { return Successors.end();	}
	unsigned                succ_size() const { return (unsigned)Successors.size(); }
	user_iterator		user_begin()	{ return Users.begin(); 	}
	const_user_iterator	user_begin() const { return Users.begin();	}
	user_iterator		user_end()	{ return Users.end();		}
	const_user_iterator	user_end() const { return Users.end();		}
        unsigned                user_size() const { return (unsigned)Users.size(); }
	def_iterator		def_begin()	{ return Defs.begin(); 	}
	const_def_iterator	def_begin() const { return Defs.begin();	}
	def_iterator		def_end()	{ return Defs.end();		}
	const_def_iterator	def_end() const  { return Defs.end();		}
        unsigned                def_size() const { return (unsigned)Defs.size(); }

private:
	/// addPredecessor - Add pred as a predecessor of this SEGNode.
	/// Successors list of pred is automatically updated.
	/// No duplicated SEG-CFG edges.
	void addPredecessor(SEGNode *pred);

	/// removePredecessor - Remove pred from the predecessor list of 
	/// this SEGNode. The Successor list of pred is automatically updated.
	void removePredecessor(SEGNode *pred);

	/// replacePredecessor - Replace predecessor Old with New.
	/// Successor list of both Old and New are updated.
	//void replacePredecessor(SEGNode *Old, SEGNode *New);

	/// transferPredecessor - Transfers all the predecessors of from to
	/// this SEGNode. copy all predecessors to current SEGNode and remove
	/// all predecessors of from.
	void transferPredecessor(SEGNode *from);

	/// addSuccessor - Add succ as a successor of this SEGNode.
	/// Predecessor list of succ is automatically updated.
	/// No duplicated SEG-CFG edges.
	void addSuccessor(SEGNode *succ);

	/// removeSuccessor - Remove succ from the successor list of
	/// this SEGNode. The predecessor list of succ is automatically updated.
	void removeSuccessor(SEGNode *succ);

	/// replaceSuccesor - Replace successor Old with New.
	/// Predecessor list of both Old and new are updated.
	//void replaceSuccessor(SEGNode *Old, SEGNode *New);

	/// transferSuccessor - Transfers all the successors of from to
	/// this SEGNode. copy all successors to current SEGNode and remove all
	/// successors of from. 
	void transferSuccessor(SEGNode *from);

	/// addDef - Add def as a definition of this SEGNode.
	void addDef(SEGNode *def);

	/// addUser - Add user as a user of this SEGNode.
	void addUser(SEGNode *user);

	/// removeDef - Remove def from this SEGNode's def list.
	void removeDef(SEGNode *def);

	/// removeUser - Remove user from this SEGNode's user list.
	void removeUser(SEGNode *user);

	/// removeFromParent - This method unlink this SEGNode from the containing
	/// function, and returns it, but not delete it.
	SEGNode *removeFromParent();

	/// eraseFromParent - This method unlink this SEGNode and delete it.
	void eraseFromParent();

public:
	void dump() const;

	bool operator==(const SEGNode &sn) {
		if(Inst==NULL || sn.getInstruction()==NULL)
			return false;
		else
			return (Inst == sn.getInstruction());
	}

};

raw_ostream& operator<<(raw_ostream &OS, const SEGNode &SN);


//===--------------------------------------------------------------------===//
// GraphTraits specializations for machine basic block graphs (machine-CFGs)
//===--------------------------------------------------------------------===//

// Provide specializations of GraphTraits to be able to treat a
// SEG as a graph of SEGNodes...
//

template <> struct GraphTraits<SEGNode *> {
	typedef SEGNode NodeType;
	typedef SEGNode::succ_iterator ChildIteratorType;

	static NodeType *getEntryNode(SEGNode *SN) { return SN; }
	static inline ChildIteratorType child_begin(NodeType *N) {
		return N->succ_begin();
	}
	static inline ChildIteratorType child_end(NodeType *N) {
		return N->succ_end();
	}
};

template <> struct GraphTraits<const SEGNode *> {
        typedef const SEGNode NodeType;
        typedef SEGNode::succ_iterator ChildIteratorType;

        static NodeType *getEntryNode(const SEGNode *SN) { return SN; }
        static inline ChildIteratorType child_begin(NodeType *N) {
                return N->succ_begin();
        }
        static inline ChildIteratorType child_end(NodeType *N) {
                return N->succ_end();
        }
};

// Provide specializations of GraphTraits to be able to treat a
// SEG as a graph of SEGNodes... and to walk it
// in inverse order.  Inverse order for a function is considered
// to be when traversing the predecessor edges of a MBB
// instead of the successor edges.
//
template <> struct GraphTraits<Inverse<SEGNode*> > {
        typedef SEGNode NodeType;
        typedef SEGNode::pred_iterator ChildIteratorType;

        static NodeType *getEntryNode(Inverse<SEGNode *> G) { return G.Graph; }
        static inline ChildIteratorType child_begin(NodeType *N) {
                return N->pred_begin();
        }
        static inline ChildIteratorType child_end(NodeType *N) {
                return N->pred_end();
        }

};

template <> struct GraphTraits<Inverse<const SEGNode*> > {
        typedef const SEGNode NodeType;
        typedef SEGNode::pred_iterator ChildIteratorType;

        static NodeType *getEntryNode(Inverse<const SEGNode *> G) { return G.Graph; }
        static inline ChildIteratorType child_begin(NodeType *N) {
                return N->pred_begin();
        }
        static inline ChildIteratorType child_end(NodeType *N) {
                return N->pred_end();
        }

};

}//End llvm namespace


#endif
