#include "llvm/Support/Debug.h"
#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <cassert>
#include <map>
#include <set>
#include <utility>
#include <vector>
#include "bdd.h"
#include "fdd.h"
#include "FSAAnalysis.h"
#include "llvm/IR/Instructions.h"

/*
 * TODO: over the course of evaluation, we may discover that a node
 *       points to 0 (i.e. everything); in this case, we should
 *       never process it again, and default to some simpler case
 */

/*
 * Different Instructions:
 *   Standard Instructions:
 *     1) alloca type, num
 *     2) copy type ([val,label])*
 *     3) load type ptr
 *     4) store type val, type ptr
 *     5) call rettype (funtype) funptr(ty arg)
 *     6) ret type val
 *   Cast Instructions:
 *     1) bitcast ty1 val to ty2       - interprets one type as another (both ptrs or not ptrs)
 *     2) ptrtoint ty1 val to ty2      - converts a ptr type io int type
 *     3) inttoptr ty1 val to ty2      - converts an int type to ptr type
 *     4) trunc ty1 val to ty2         - truncates an int type to smaller int type
 *     5) zext ty1 val to ty2          - zero-extends ty1 to ty2
 *     6) sext ty1 val to ty2          - sign-extends ty1 to ty2
 *     7) fptrunc ty1 val to ty2       - truncates a fp ty1 to ty2
 *     8) fpext ty1 val to ty2         - extends a fp ty1 to ty2
 *     9) fptoui ty1 val to ty2        - converts a fp value of ty1 to unsigned ty2
 *    10) fptosi ty1 val to ty2        - converts a fp value of ty1 to signed ty2
 *    11) uitofp ty1 val to ty2        - converts unsigned ty1 to fp ty2
 *    12) sitofp ty1 val to ty2        - converts signed ty1 to fp ty2
 *    13) addrspacecast ty1 val to ty2 - converts ptr ty1 to ptr ty2 in different addressspace
 * What do we handle? How do we handle undefined values?
 *
 * Simple boolean:
 *    alloca should never be undefined
 *    copy arguments may be undefined, if so, ret aliases everything
 *    load ptr may be undefined, if so, ret aliases everything
 *    ret value may be undefined
 *
 * Boolean vector:
 *    store both ptrs may be undefined
 *    call func ptr and arguments may be undefined
 *      - each undefined argument must be processed individually
 *
 */
/*
 * Typing for allsat callbacks:
 * void allsatcallback(char* varset,int size)
 *
 * fdd_ithvar(i,j) is only true for the one value j at position i;
 *                 it is false for any other positioned value and
 *                 and other value at position i
 *
 * The following should be equivalent if bdd2 is a conjunction of literals:
 * bdd_restrict(bdd1,bdd2) = bdd_relprod(bdd1,bdd2,bdd_support(bdd2))
 *
 * abstraction layer for general relations?
 * use global variable to determine number of dimensions, etc...
 */

// append node to list if not present, return true if append occurred
template <class T>
bool inline appendIfAbsent(std::list<T> *wkl, T elt) {
	typename std::list<T>::iterator i = std::find(wkl->begin(), wkl->end(), elt);
	if (i == wkl->end()) wkl->push_back(elt);
	return i == wkl->end();
}

// propagate top level without strong update
bool FlowSensitiveAliasAnalysis::propagateTopLevel(bdd *oldtpts, bdd *newpart, SEGNode *sn) {
	bdd tmp = bdd_true();
	return propagateTopLevel(oldtpts,newpart,&tmp,sn);
}

#undef  DEBUG_TYPE
#define DEBUG_TYPE "fsaa-propagatetoplevel"
// propagate top level with strong update
bool FlowSensitiveAliasAnalysis::propagateTopLevel(bdd *oldtpts, bdd *newpart, bdd* update, SEGNode *sn) {
	const Function *f = sn->getParent()->getFunction();
	std::list<SEGNode*> *wkl = StmtWorkList.at(f);
	bool changed = false;
	DEBUG(dbgs() << "NEWPTS:\n"; printBDD(LocationCount,Int2Str,*newpart));
	// if old and new are different, add all users to worklist
	if (*oldtpts != ((*oldtpts | *newpart) & *update)) {
		DEBUG(dbgs() << "PROPAGATE TOPLEVEL FOR: "<<*sn<<"\n");
		// only append to worklist if absent
		for(SEGNode::const_user_iterator i = sn->user_begin(); i != sn->user_end(); ++i)
			if (appendIfAbsent<SEGNode*>(wkl,*i)) {
				changed = true;
				DEBUG(dbgs() << "TOPLEVEL: APPENDED " << **i << " TO " << f->getName() << "'S WORKLIST\n");
			}
	}
#undef  DEBUG_TYPE
#define DEBUG_TYPE "fsaa-strongupdate"
	// update old
	*oldtpts = (*oldtpts | *newpart) & *update;
	if (*update != bdd_true()) {
		DEBUG(dbgs() << "STRONG UPDATE:\n");
		DEBUG(printBDD(LocationCount,Int2Str,*oldtpts));
	}
	// return true if the worklist was changed
	return changed;
}

#undef  DEBUG_TYPE
#define DEBUG_TYPE "fsaa-propagateaddrtaken"
// propagate address taken
bool FlowSensitiveAliasAnalysis::propagateAddrTaken(SEGNode *sn) {
	bdd oldink, newink;
	const Function *f = sn->getParent()->getFunction();
	std::list<SEGNode*> *wkl = StmtWorkList.at(f);
	bool changed = false;
	// add all changed successors to the worklist
	for(SEGNode::const_succ_iterator i = sn->succ_begin(); i != sn->succ_end(); ++i) {
		SEGNode *succ = *i;
		// get old and new in sets
		oldink = succ->getInSet();
		newink = oldink | sn->getOutSet();
		// append to worklist if inset changed and not already in worklist
		if (oldink != newink){
			DEBUG(dbgs()<<"PROPAGATE ADDRTAKEN FOR: "<<*sn<<"\n");
			if (appendIfAbsent<SEGNode*>(wkl,succ)) {
				changed = true;
				DEBUG(dbgs() << "ADDRTAKEN: APPENDED " << **i << " TO " << f->getName() << "'S WORKLIST\n");
			}
			succ->setInSet(newink);
		}
	}
	// return true if the worklist was changed
	return changed;
}

#undef  DEBUG_TYPE
#define DEBUG_TYPE "fsaa-preprocess"
// NOTE: alloc should never have undefined arguments
int FlowSensitiveAliasAnalysis::preprocessAlloc(SEGNode *sn) {
	std::vector<unsigned int> *ArgIds = new std::vector<unsigned int>();
	std::vector<bdd> *StaticData = new std::vector<bdd>();
	// store argument ids
	ArgIds->push_back(sn->getId()+1);
	sn->setArgIds(ArgIds);
	// store static bdd data
	StaticData->push_back(fdd_ithvar(0,sn->getId()) & fdd_ithvar(1,ArgIds->at(0)));
	sn->setStaticData(StaticData);
	return 0;
}

int FlowSensitiveAliasAnalysis::preprocessCopy(SEGNode *sn) {
	const Instruction *inst = sn->getInstruction();
	std::vector<unsigned int> *ArgIds = new std::vector<unsigned int>();
	std::vector<bdd> *StaticData = new std::vector<bdd>();
	bdd argset = bdd_false();
	unsigned int id;
	// store static argument data
	for (User::const_op_iterator oit = inst->op_begin(); oit != inst->op_end(); ++oit) {
		// if argument out-of-range, store id 0
		Value *v = oit->get();
		if (Value2Int.count(v) != 0){
			id = Value2Int.at(v);
		} else { id = 0; sn->setDefined(false); }
		VALIDIDX1(id);
		ArgIds->push_back(id);
		argset |= fdd_ithvar(0,id);
	}
	sn->setArgIds(ArgIds);
	// if arguments are defined, store data to perform relprod
	if (sn->getDefined()) {
		// defined value name
		StaticData->push_back(fdd_ithvar(0,sn->getId()));
		// inst value names for each incoming branch
		StaticData->push_back(argset);
		// variables we are quantifying over
		StaticData->push_back(fdd_ithset(0));
	// otherwise, store constant (x points everywhere)
	} else StaticData->push_back(fdd_ithvar(0,sn->getId()));
	// assign data
	sn->setStaticData(StaticData);
	return 0;
}

int FlowSensitiveAliasAnalysis::preprocessLoad(SEGNode *sn) {
	const LoadInst *ld = cast<LoadInst>(sn->getInstruction());
	std::vector<unsigned int> *ArgIds = new std::vector<unsigned int>();
	std::vector<bdd> *StaticData = new std::vector<bdd>();
	const Value *v = ld->getPointerOperand();
	DEBUG(dbgs() << "LOAD FROM: " << v->getName() << "\n");
	// check if argument is defined
	sn->setDefined(Value2Int.count(v) != 0);
	// store static argument id, or zero if it is out-of-range
	ArgIds->push_back(sn->getDefined() ? Value2Int.at(v) : 0);
	sn->setArgIds(ArgIds);
	// store static bdd data
	if (sn->getDefined()) {
		DEBUG(dbgs() << "DEFINED LOAD: " << Value2Int.at(v) << "\n");
		// defined var name
		StaticData->push_back(fdd_ithvar(0,sn->getId()));
		// value we are loading from name
		StaticData->push_back(fdd_ithvar(0,ArgIds->at(0)));
		// variables we are quantifying over
		StaticData->push_back(fdd_ithset(0));
	// if load argument is out-of-range, it can point anywhere
	} else {
		DEBUG(dbgs() << "UNDEFINED LOAD\n");
		StaticData->push_back(fdd_ithvar(0,sn->getId()));
	}
	sn->setStaticData(StaticData);
	// check validity of these guys? VALIDIDX2(x,y);
	return 0;
}

int FlowSensitiveAliasAnalysis::preprocessStore(SEGNode *sn) {
	const StoreInst *sr = cast<StoreInst>(sn->getInstruction());
	std::vector<unsigned int> *ArgIds = new std::vector<unsigned int>();
	std::vector<bdd> *StaticData = new std::vector<bdd>();
	const Value *p,*v;
	bool pd, vd;
	p = sr->getPointerOperand();
	v = sr->getValueOperand();
	// check if arguments are defined
	pd = Value2Int.count(p) != 0;
	vd = Value2Int.count(v) != 0;
	sn->setDefined(pd & vd);
	// store ids for argument values, or 0 for undefined
	ArgIds->push_back(pd ? Value2Int.at(p) : 0);
	ArgIds->push_back(vd ? Value2Int.at(v) : 0);
	sn->setArgIds(ArgIds);
	// store bdds for corresponding values, or everything for undefined
	StaticData->push_back(pd ? fdd_ithvar(0,ArgIds->at(0)) : bdd_true());
	StaticData->push_back(vd ? fdd_ithvar(0,ArgIds->at(1)) : bdd_true());
	sn->setStaticData(StaticData);
	return 0;
}

#undef  DEBUG_TYPE
#define DEBUG_TYPE "fsaa-alloc"
int FlowSensitiveAliasAnalysis::processAlloc(bdd *tpts, SEGNode *sn) {
	bdd alloc;
	// add pair to top-level pts
	alloc = sn->getStaticData()->at(0);
	propagateTopLevel(tpts,&alloc,sn);
	// propagate addr taken
	sn->setOutSet(sn->getInSet());
	propagateAddrTaken(sn);
	return 0;
}

#undef  DEBUG_TYPE
#define DEBUG_TYPE "fsaa-copy"
int FlowSensitiveAliasAnalysis::processCopy(bdd *tpts, SEGNode *sn) {
	bdd bddx, vs, qt, newpts;
	// if defined, x points to quantifying over bdd + vs choices for all v values
	if (sn->getDefined()) {
		bddx = sn->getStaticData()->at(0);
		vs   = sn->getStaticData()->at(1);
		qt   = sn->getStaticData()->at(2);
		newpts = bddx & bdd_relprod(*tpts,vs,qt);
	}
	// else, x points everywhere
	else
		newpts = sn->getStaticData()->at(0);
	// store new top-level points-to set
	propagateTopLevel(tpts,&newpts,sn);
	// propagate addr taken
	sn->setOutSet(sn->getInSet());
	propagateAddrTaken(sn);
	return 0;
}

#undef  DEBUG_TYPE
#define DEBUG_TYPE "fsaa-load"
int FlowSensitiveAliasAnalysis::processLoad(bdd *tpts, SEGNode *sn) {
	bdd bddx, bddy, topy, ky, qt, newpts;
	// debugging info
	DEBUG(dbgs() << "LOAD INSET:\n"; printBDD(LocationCount,Int2Str,sn->getInSet()));
	// if defined, do standard lookup
	if (sn->getDefined()) {
		bddx = sn->getStaticData()->at(0);
		bddy = sn->getStaticData()->at(1);
		qt   = sn->getStaticData()->at(2);
		// get PTop(y)
		topy = out2in(bdd_restrict(*tpts,bddy));
		// get PK(PTop(y))
		ky   = bdd_relprod(sn->getInSet(),topy,qt);
		newpts = bddx & ky;
		// if newpts is empty, it means we are loading from an uninitialized value: then x -> everywhere
		// if topy -> everywhere, then load result x should point to everywhere
		if (bdd_sat(topy & fdd_ithvar(0,0))) {
			DEBUG(dbgs() << "Top(y) points everywhere\n");
			newpts = bddx;
		}
	// else, x points everywhere
	} else newpts = sn->getStaticData()->at(0);
	// extend top pts
	propagateTopLevel(tpts,&newpts,sn);
	// propagate addr taken
	sn->setOutSet(sn->getInSet());
	propagateAddrTaken(sn);
	return 0;
}

#undef  DEBUG_TYPE
#define DEBUG_TYPE "fsaa-store"
int FlowSensitiveAliasAnalysis::processStore(bdd *tpts, SEGNode *sn) {
	bdd bddx, bddy, topx, topy, outkpts, oldtpts;
	// lookup where x points, get PTop(x)
	if (sn->getArgIds()->at(0)) {
		bddx = sn->getStaticData()->at(0);
		topx = out2in(bdd_restrict(*tpts,bddx));
	} else topx = sn->getStaticData()->at(0);
	// lookup where y points, get PTop(y)
	if (sn->getArgIds()->at(1)) {
		bddy = sn->getStaticData()->at(1);
		topy = bdd_restrict(*tpts,bddy);
	} else topy = sn->getStaticData()->at(1);
	// if storing to unique memory location, strong update
	if (sn->getArgIds()->at(0) && bdd_satcount(bddx & *tpts & bdd_not(fdd_ithvar(1,0))) == 1.0)
		outkpts = bdd_apply(sn->getInSet(),topx,bddop_diff);
	// else weak update
	else outkpts = sn->getInSet();
	// return modified outkpts
	sn->setOutSet(outkpts | (topx & topy));
	propagateAddrTaken(sn);
	// if we are storing everywhere
	if (bdd_sat(fdd_ithvar(0,0) & topx)) {
		// return 1 if we are storing everything, everywhere
		if (bdd_sat(fdd_ithvar(1,0) & topy)) return 1;
#ifdef ENABLE_UNDEFSTORE
		// update toplevel
		bool changed;
		oldtpts = *tpts;
		*tpts = *tpts | topy;
		changed = oldtpts != *tpts;
		// for each function, propagate
		for (std::map<const Function*, SEG*>::iterator mi=Func2SEG.begin(), me=Func2SEG.end(); mi!=me; ++mi) {
			// get SEG entry node
			if (mi->second->isDeclaration()) continue;
			SEGNode *entry = mi->second->getEntryNode();
			// update every entry node's outset
			entry->setInSet(entry->getIntSet() | topy);
			entry->setOutSet(entry->getInSet());
			// propagate addr taken and toplevel
			if (!propagateAddrTaken(entry) || changed)
				appendIfAbsent<SEGNode*>(StmtWorklist->at(mi->first),*i);
		}
#endif
	}
	return 0;
}

#undef  DEBUG_TYPE
#define DEBUG_TYPE "fsaa-preprocess"
// TODO: reachability is too strict; get closure over points-to
// ret bdd with pairs that originate either from an argument or a global variable
bdd genFilterSet(bdd inset, bdd gvarpts, bdd argset) {
	// if filter can point anywhere, return whole inset
	bdd filter = (argset | gvarpts) & inset;
	if (bdd_sat(filter & fdd_ithvar(1,0))) return inset;
	else return filter;
}

// get set of functions whose type matches this calls type
bdd FlowSensitiveAliasAnalysis::matchingFunctions(const Value *funCall) {
	std::map<unsigned int,const Function *>::iterator fp;
	const Type *callType = funCall->getType()->getPointerElementType();
	bdd funs = bdd_false();
	for (fp = Int2Func.begin(); fp != Int2Func.end(); ++fp)
		if (fp->second->getType() == callType)
			funs |= fdd_ithvar(0,fp->first);
	return funs;
}

int FlowSensitiveAliasAnalysis::preprocessCall(SEGNode *sn) {
	std::vector<unsigned int> *ArgIds = new std::vector<unsigned int>();
	std::vector<bdd> *StaticData = new std::vector<bdd>();
	CallData *cd = new CallData();
	bool isCall = true;
	const CallInst *ci = NULL;
	const InvokeInst *ii = NULL;
	const Value *funv = NULL;
	const Function *fun = NULL;
	unsigned int argnum = 0;
	Value *arg;
	// check whether an instruction is a call or invoke
	if (isa<CallInst>(sn->getInstruction())) {
		ci = cast<CallInst>(sn->getInstruction());
		funv = ci->getCalledValue();
		fun = ci->getCalledFunction();
		argnum = ci->getNumArgOperands();
	} else {
		isCall = false;
		ii = cast<InvokeInst>(sn->getInstruction());
		funv = ii->getCalledValue();
		fun = ii->getCalledFunction();
		argnum = ii->getNumArgOperands();
	}
	unsigned int id;
	// check if this function is a pointer and if it is defined
	cd->isPtr = (fun == NULL);
	DEBUG(dbgs() << "CALL INST: " << *funv << "\n");
	cd->isDefinedFunc = fun != NULL && Value2Int.count(funv) != 0;
	// if function called is defined, store it's name
	if (cd->isDefinedFunc) {
		DEBUG(dbgs() << "CALL DEFINED\n");
		cd->funcId = Value2Int.at(funv);
		cd->funcName = fdd_ithvar(0,cd->funcId);
	// otherwise, store every possible function it could point to
	} else {
		DEBUG(dbgs() << "CALL UNDEFINED\n");
		cd->funcId = 0;
		cd->funcName = matchingFunctions(funv);
		sn->setDefined(false);
	}
	// iterate through instruction arguments, set argids, generate static data for arguments
	for (unsigned int i = 0; i < argnum; i++) {
		// if argument out-of-range, store id 0
		arg = isCall ? ci->getArgOperand(i) : ii->getArgOperand(i);
		if (Value2Int.count(arg) != 0) id = Value2Int.at(arg);
		else { id = 0; sn->setDefined(false); }
		VALIDIDX1(id);
		ArgIds->push_back(id);
		StaticData->push_back(fdd_ithvar(0,id));
	}
	sn->setArgIds(ArgIds);
	// if this function is not a pointer, statically compute it
	if (!cd->isPtr) {
		std::vector<const Function*> *targets = new std::vector<const Function*>();
		targets->push_back(fun);
		cd->targets = targets;
	}
	// statically compute argset for filtering purposes
	bdd argset = bdd_false();
	for (unsigned int i = 0; i < StaticData->size(); i++)
		argset = argset | StaticData->at(i);
	cd->argset = argset;
	// store type of this called function
	cd->funcType = funv->getType()->getPointerElementType();
	// set static data
	sn->setStaticData(StaticData);
	// set extra data
	sn->setExtraData(cd);
	return 0;
}

#undef  DEBUG_TYPE
#define DEBUG_TYPE "fsaa-call"
// return a vector of Function* representing where a function points
std::vector<const Function*> *
FlowSensitiveAliasAnalysis::computeTargets(bdd *tpts, SEGNode* funNode, int funId, bdd funName, Type *funType)
{
	std::vector<const Function*> *targets = new std::vector<const Function*>();
	std::map<unsigned int,const Function *>::iterator fmit;
	bdd fpts;
	// if function is defined and doesn't point everywhere, compute it's points-to set
	if (funId && !pointsTo(*tpts,funId,0)) {
		DEBUG(dbgs() << "FUN IS DEFINED!\n");
		fpts = bdd_restrict(*tpts,funName);
	}
	// otherwise, it can point everywhere
	else {
		DEBUG(dbgs() << "FUN IS UNDEFINED!\n");
		fpts = *tpts;
	}
	// find which functions pointer points-to and types agree, add to targets
	DEBUG(dbgs() << "FPTS\n"; printBDD(LocationCount,Int2Str,fpts));
	for (fmit = Int2Func.begin(); fmit != Int2Func.end(); ++fmit) {
		// get potential target information
		int targetId = fmit->first;
		bdd targetName = fdd_ithvar(1,targetId);
		const Function* target = fmit->second;
		const Type *targetType = target->getFunctionType();
		DEBUG(dbgs() << "CHECK FUNTYPE: "; funType->dump(); dbgs() << " TARGET: " << target->getName() << " : "; targetType->dump(); dbgs() << "\n");
		// check if function pointer points to target
		if (bdd_sat(fpts & targetName)) {
			// if so, check if their types match
			if (targetType == funType) {
				DEBUG(dbgs() << "TARGET ADDED\n");
				// add target function to targest list
				targets->push_back(target);
				// add target function to caller map with this node as it's caller
				DEBUG(dbgs() << "ATTEMPTING TO ADD CALLER\n");
				addCaller(funNode,target);
			// otherwise, fail (may change this later)
			} else
				DEBUG(dbgs() << "TARGET TYPE MISMATCH\n");
			// NOTE: if this would be a bad call, C semantics is undefined and we don't care
			// else assert(false && "Types should agree");
		// pointer does NOT point to target
		} else DEBUG(dbgs() << "NOT IN POINTS-TO SET\n");
	}
	return targets;
}

// propagate points-to information from caller to callee
void FlowSensitiveAliasAnalysis::processTarget(bdd *tpts, SEGNode *callNode, bdd filter, const Function *target) {
	std::vector<bdd> *params, *call_args;
	unsigned int paramId, argId, argsize;
	bdd paramName, argName, kill, newpts;
	bool varargs;
	// get necessary data
	SEGNode *entry = Func2SEG.at(target)->getEntryNode();
	params = entry->getStaticData();
	call_args = callNode->getStaticData();
	varargs = target->isVarArg();
	// debugging calls
	DEBUG(dbgs() << "TARGET: " << target->getName() << "\n");
	assert(params != NULL && call_args != NULL);
	assert(params->size() == call_args->size() ||
		(varargs && params->size() <= call_args->size()));
	// for each argument (except varargs, which we don't support)
	argsize = varargs ? params->size() - 1 : params->size();
	for (unsigned int i = 0; i < argsize; i++) {
		// get necessary data
		paramName = params->at(i);
		argName = call_args->at(i);
		argId = callNode->getArgIds()->at(i);
		paramId = entry->getArgIds()->at(i);
		// if argument is defined, add p -> Top(a)
		// and stong update to delete p -> p__argument
		if (argId != 0) {
			DEBUG(dbgs() << "KILL: " << paramId+1 << "\n");
			newpts = paramName & bdd_restrict(*tpts,argName);
			kill = bdd_not(paramName & fdd_ithvar(1,paramId+1));
		}
		// else, add p -> everything
		else {
			newpts = paramName;
			kill = bdd_true();
		}
		// propagate top level for callee
		propagateTopLevel(tpts,&newpts,&kill,entry);
	}
	// get SEG entry node's inset
	entry->setInSet(entry->getInSet() | filter);
	entry->setOutSet(entry->getInSet());
	// propagate using address taken on entry node
	if (propagateAddrTaken(entry))
			appendIfAbsent<const Function*>(&FuncWorkList,target);
}

int FlowSensitiveAliasAnalysis::processCall(bdd *tpts, SEGNode *sn) {
	// declare some variables we need
	std::vector<const Function*>::iterator target;
	std::vector<const Function*> *targets;
	CallData *cd;
	bdd filter;
	// setup some data we need
	// filter = genFilterSet(sn->getInSet(),globalValueNames,cd->argset);
	filter = sn->getInSet();
	cd = static_cast<CallData*>(sn->getExtraData());
	// debugging calls
	// DEBUG(dbgs() << "BDD FILTER:\n");
	// DEBUG(printBDD(POINTSTO_MAX,filter));
	DEBUG(dbgs() << "FUNTYPE: " << *(cd->funcType) << "\n");
	// if func is pointer, dynamically compute its targets
	if (cd->isPtr)
		targets = computeTargets(tpts,sn,cd->funcId,cd->funcName,cd->funcType);
	// else get its targets statically
	else {
		DEBUG(dbgs() << "NOT PTR\n");
		// TODO: if it's declaration, return value points everywhere, propagate both
		// can use function to get more information like whether pass by reference
		// or return by reference. check llvm doc for more detail.
		if (cd->targets->at(0)->isDeclaration()) return 0;
		targets = cd->targets;
	}
	DEBUG(dbgs() << "ENUMERATE TARGETS\n");
	// process all computed targets
	for (target = targets->begin(); target != targets->end(); ++target)
		processTarget(tpts,sn,filter,*target);
	// set outset to inset - filter, then propagate
	sn->setOutSet(sn->getInSet() - filter);
	propagateAddrTaken(sn);
	return 0;
}

#undef  DEBUG_TYPE
#define DEBUG_TYPE "fsaa-preprocess"
int FlowSensitiveAliasAnalysis::preprocessRet(SEGNode *sn) {
	Value *ret = cast<ReturnInst>(sn->getInstruction())->getReturnValue();
	sn->setArgIds(new std::vector<unsigned int>());
	sn->setStaticData(new std::vector<bdd>());
	// get my returned value name or 0 if undefined
	// store bdd for returned value
	if (Value2Int.count(ret)) {
		sn->getArgIds()->push_back(Value2Int.at(ret));
		sn->getStaticData()->push_back(fdd_ithvar(0,sn->getArgIds()->at(0)));
	} else {
		sn->getArgIds()->push_back(0);
		sn->getStaticData()->push_back(bdd_true());
	}
	return 0;
}

#undef  DEBUG_TYPE
#define DEBUG_TYPE "fsaa-ret"
int FlowSensitiveAliasAnalysis::processRet(bdd *tpts, SEGNode *sn) {
	std::map<const Function*,RetData*>::iterator cit;
	std::map<const Function*,RetData*> *Calls;
	bdd retpts;
	// move in to out
	sn->setOutSet(sn->getInSet());
	// find out where returned value points
	if (sn->getArgIds()->at(0)) {
		DEBUG(dbgs() << "RET VALUE: DEFINED\n");
		retpts = bdd_restrict(*tpts,sn->getStaticData()->at(0));
	} else {
		DEBUG(dbgs() << "RET VALUE: UNDEFINED\n");
		retpts = sn->getStaticData()->at(0);
	}
	// return if we have no calls
	if (!Func2Calls.count(sn->getParent()->getFunction())) {
		DEBUG(dbgs() << "RET NO CALLS!\n");
		return 0;
	}
	// get call site list and iterate through it
	Calls = &Func2Calls.at(sn->getParent()->getFunction())->Calls;
	for (cit = Calls->begin(); cit != Calls->end(); ++cit) {
		bool changed = false;
		RetData *rd = cit->second;
		SEGNode *callInst = rd->callInst;
		const Function *caller = callInst->getParent()->getFunction();
		DEBUG(dbgs() << "RET: Call " << *callInst << " from " << caller->getName() << "\n");
		// append my outset to caller's outset
		// DEBUG(printBDD(LocationCount,Int2Str,sn->getOutSet()));
		callInst->setOutSet(callInst->getOutSet() | sn->getOutSet());
		// propagate addr taken and record if worklist changed
		changed = propagateAddrTaken(callInst) || changed;
		// if callsite stores a value, propagate on top level
		if (rd->callStatus != NO_SAVE) {
			DEBUG(dbgs() << "RET: Caller saves\n");
			bdd newpts = rd->saveName & retpts;
			changed = propagateTopLevel(tpts,&newpts,callInst) || changed;
		} else DEBUG(dbgs() << "RET: Caller doesn't save\n");
		// if caller's worklist changed, reinsert caller in worklist
		if (changed) appendIfAbsent<const Function*>(&FuncWorkList,caller);
	}
	return 0;
}

#undef  DEBUG_TYPE
#define DEBUG_TYPE "fsaa-preprocess"
int FlowSensitiveAliasAnalysis::preprocessUndef(SEGNode *sn) {
	sn->setArgIds(new std::vector<unsigned int>());
	sn->setStaticData(new std::vector<bdd>());
	// add id for this value
	sn->getArgIds()->push_back(Value2Int.at(sn->getInstruction()));
	// add id -> everywhere
	sn->getStaticData()->push_back(fdd_ithvar(0,sn->getArgIds()->at(0)));
	return 0;
}

#undef  DEBUG_TYPE
#define DEBUG_TYPE "fsaa-undef"
int FlowSensitiveAliasAnalysis::processUndef(bdd *tpts, SEGNode *sn) {
	// move in to out
	sn->setOutSet(sn->getInSet());
	// add id -> everywhere to tpts and propagate
	bdd newpts = sn->getStaticData()->at(0);
	propagateTopLevel(tpts,&newpts,sn);
	// propagate address taken info
	propagateAddrTaken(sn);
	return 0;
}
