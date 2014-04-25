#define DEBUG_TYPE "flowsensitive-aa"
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

// Global to track size of POINTSTO bdd
static unsigned int POINTSTO_MAX = 0;
static bddPair* LPAIR            = NULL;
static bddPair* RPAIR            = NULL;

#define out2in(b)        bdd_replace(b,LPAIR)
#define VALIDIDX1(i)     assert(i < POINTSTO_MAX)
#define VALIDIDX2(i,j)   assert(i < POINTSTO_MAX && j < POINTSTO_MAX)

void pointsToInit(unsigned int nodes, unsigned int cachesize, unsigned int domainsize) {
	int domain[2];
	domain[0] = domain[1] = POINTSTO_MAX = domainsize;
	bdd_init(nodes,cachesize);
	assert(fdd_extdomain(domain,2) >= 0);
	LPAIR = bdd_newpair();
	RPAIR = bdd_newpair();
	assert(fdd_setpair(LPAIR,1,0) >= 0);
	assert(fdd_setpair(RPAIR,0,1) >= 0);
}

void pointsToFinalize() {
	bdd_freepair(LPAIR);
	bdd_freepair(RPAIR);
	bdd_done();
}

bool pointsTo(bdd rel, unsigned int v1, unsigned int v2) {
	assert(v1 <= POINTSTO_MAX && v2 <= POINTSTO_MAX);
	return bdd_sat(rel & fdd_ithvar(0,v1) & fdd_ithvar(1,v2));
}

void printBDD(unsigned int max, std::map<unsigned int,std::string*> *lt, bdd b) {
	unsigned int i, j;
	bool empty = true;
	for (i=0;i<max;++i) {
		for (j=0;j<max;++j) {
			if (bdd_sat(b & fdd_ithvar(0,i) & fdd_ithvar(1,j))) {
				empty = false;
				std::string *s1,*s2;
				// check if these strings have a non-null mapping
				s1 = lt != NULL && lt->count(i) > 0 ? lt->at(i) : NULL;
				s2 = lt != NULL && lt->count(j) > 0 ? lt->at(j) : NULL;
				// print out first elt
				if (s1 != NULL) dbgs() << *s1 << " -> ";
				else if (lt != NULL) dbgs() << "NOT FOUND: " << i << " -> ";
				else dbgs() << i << " -> ";
				// print out second elt
				if (s2 != NULL) dbgs() << *s2 << "\n";
				else if (lt != NULL) dbgs() << "NOT FOUND: " << j << "\n";
				else dbgs() << j << "\n";
			}
		}
	}
	if (empty) dbgs() << "EMPTY\n";
}

void printBDD(unsigned int max, bdd b) { printBDD(max,NULL,b); }

// append node to list if not present, return true if append occurred
template <class T>
bool inline appendIfAbsent(std::list<T> *wkl, T elt) {
	typename std::list<T>::iterator i = std::find(wkl->begin(), wkl->end(), elt);
	if (i == wkl->end()) wkl->push_back(elt);
	return i == wkl->end();
}

bool FlowSensitiveAliasAnalysis::propagateTopLevel(bdd *oldtpts, bdd *newpart, SEGNode *sn) {
	const Function *f = sn->getParent()->getFunction();
	std::list<SEGNode*> *wkl = StmtWorkList.at(f);
	bool changed = false;
	// if old and new are different, add all users to worklist
	if (*oldtpts != (*oldtpts | *newpart)){
		dbgs() << "PROPAGATE TOPLEVEL FOR:\t"<<*sn<<"\n";
		// only append to worklist if absent
		for(SEGNode::const_user_iterator i = sn->user_begin(); i != sn->user_end(); ++i)
			if (appendIfAbsent<SEGNode*>(wkl,*i)) {
				changed = true;
				dbgs() << "TOPLEVEL: APPENDED " << **i << " TO " << f->getName() << "'S WORKLIST\n";
			}
	}
	// update old
	*oldtpts = *oldtpts | *newpart;
	// return true if the worklist was changed
	return changed;
}

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
			dbgs()<<"PROPAGATE ADDRTAKEN FOR:\t"<<*sn<<"\n";
			if (appendIfAbsent<SEGNode*>(wkl,succ)) {
				changed = true;
				dbgs() << "ADDRTAKEN: APPENDED " << **i << " TO " << f->getName() << "'S WORKLIST\n";
			}
			succ->setInSet(newink);
		}
	}
	// return true if the worklist was changed
	return changed;
}

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
	const PHINode *phi = cast<PHINode>(sn->getInstruction());
	std::vector<unsigned int> *ArgIds = new std::vector<unsigned int>();
	std::vector<bdd> *StaticData = new std::vector<bdd>();
	bdd argset = bdd_false();
	unsigned int id;
	// store static argument data
	for (User::const_op_iterator oit = phi->op_begin(); oit != phi->op_end(); ++oit) {
		// if argument out-of-range, store id 0
		Value *v = oit->get();
		//v->dump();
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
		StaticData->push_back(fdd_ithvar(0,sn->getId()));
		StaticData->push_back(argset);
		StaticData->push_back(fdd_ithset(0));
	// otherwise, store constant (x points everywhere)
	} else StaticData->push_back(fdd_ithvar(0,sn->getId()) & fdd_ithset(1));
	// assign data
	sn->setStaticData(StaticData); 
	return 0;
}

int FlowSensitiveAliasAnalysis::preprocessLoad(SEGNode *sn) {
	const LoadInst *ld = cast<LoadInst>(sn->getInstruction());
	std::vector<unsigned int> *ArgIds = new std::vector<unsigned int>();
	std::vector<bdd> *StaticData = new std::vector<bdd>();
	const Value *v = ld->getPointerOperand();
	// check if argument is defined
	sn->setDefined(Value2Int.count(v) != 0);
	// store static argument id, or zero if it is out-of-range
	ArgIds->push_back(sn->getDefined() ? Value2Int.at(v) : 0);
	sn->setArgIds(ArgIds);
	// store static bdd data
	if (sn->getDefined()) {
	StaticData->push_back(fdd_ithvar(0,sn->getId()));
	StaticData->push_back(fdd_ithvar(0,ArgIds->at(0)));
	StaticData->push_back(fdd_ithset(0));
	// if load argument is out-of-range, it can point anywhere
	} else StaticData->push_back(fdd_ithvar(0,sn->getId()) & fdd_ithset(1));
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
	
	//sn->dump();
	//dbgs()<<"Defined:\t"<<(int)(sn->getDefined())<<"\n";
	// store ids for argument values, or 0 for undefined
	ArgIds->push_back(pd ? Value2Int.at(p) : 0);
	ArgIds->push_back(vd ? Value2Int.at(v) : 0);
	sn->setArgIds(ArgIds);
	// store bdds for corresponding values, or everything for undefined
	StaticData->push_back(pd ? fdd_ithvar(0,ArgIds->at(0)) : fdd_ithset(0));
	StaticData->push_back(vd ? fdd_ithvar(0,ArgIds->at(1)) : fdd_ithset(1));
	sn->setStaticData(StaticData); 
	return 0;
}

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
	// print debugging information
/*	if (bdd_unsat(newpts))
		llvm::dbgs() << "empty copy result\n";
	else
		llvm::dbgs() << "not empty\n";
*/	// store new top-level points-to set
	propagateTopLevel(tpts,&newpts,sn);
	// propagate addr taken
	sn->setOutSet(sn->getInSet());
	propagateAddrTaken(sn);
	return 0;
}

int FlowSensitiveAliasAnalysis::processLoad(bdd *tpts, SEGNode *sn) {
	bdd bddx, bddy, topy, ky, qt, newpts;
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
	// else, x points everywhere
	} else newpts = sn->getStaticData()->at(0);
	// extend top pts
	propagateTopLevel(tpts,&newpts,sn);
	// propagate addr taken
	sn->setOutSet(sn->getInSet());
	propagateAddrTaken(sn);
	return 0;
}

int FlowSensitiveAliasAnalysis::processStore(bdd *tpts, SEGNode *sn) {
	bdd bddx, bddy, topx, topy, outkpts;
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
	// if x points uniquely, then strong update
	if (sn->getArgIds()->at(0) && bdd_satcount(bddx & *tpts) == 1.0)
	// TODO: I need to force it to not uniquely point to 0
	// if (sn->getArgIds()->at(0) && bdd_satcount(bddx & bdd_not(fdd_ithvar(0,0)) &  *tpts) == 1.0)
		outkpts = bdd_apply(sn->getInSet(),topx,bddop_diff);
	// else weak update
	else outkpts = sn->getInSet();
	// return modified outkpts
	sn->setOutSet(outkpts | (topx & topy));
	propagateAddrTaken(sn);
	return 0;
}

// ret bdd with pairs that originate either from an argument or a global variable
bdd genFilterSet(bdd inset, bdd gvarpts, bdd argset) {
	// if filter can point anywhere, return whole inset
	bdd filter = (argset | gvarpts) & inset;
	if (bdd_sat(filter & fdd_ithvar(1,0))) return inset;
	else return filter;
}

bdd matchingFunctions(const Value *funCall) {
	return bdd_false();
}

int FlowSensitiveAliasAnalysis::preprocessCall(SEGNode *sn) {
	std::vector<unsigned int> *ArgIds = new std::vector<unsigned int>();
	std::vector<bdd> *StaticData = new std::vector<bdd>();
	CallData *cd = new CallData();
	const CallInst *ci = cast<CallInst>(sn->getInstruction());
	const Value *funv = ci->getCalledValue();	
	const Function *fun = ci->getCalledFunction();
	unsigned int id;
	// check if this function is a pointer and if it is defined
	cd->isPtr = (fun == NULL);
	cd->isDefinedFunc = Value2Int.count(funv) != 0;
	// if function called is defined, store it's name
	if (cd->isDefinedFunc) {
		cd->funcId = Value2Int.at(funv);
		cd->funcName = fdd_ithvar(0,cd->funcId);
	// otherwise, store every possible function it could point to
	} else {
		cd->funcId = 0;
		cd->funcName = matchingFunctions(funv);
		sn->setDefined(false);
	}
	// iterate through instruction arguments, set argids, generate static data for arguments
	for (unsigned int i = 0; i < ci->getNumArgOperands(); i++) {
		// if argument out-of-range, store id 0
		Value *v = ci->getArgOperand(i);
		if (Value2Int.count(v) != 0) id = Value2Int.at(v); 
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

int FlowSensitiveAliasAnalysis::processCall(bdd *tpts,
                SEGNode *sn,
                std::map<unsigned int,const Function*> *fm,
                std::map<const Function *,SEG*> *sm,
                bdd gvarpts) {
	// declare some variables we need
	std::map<unsigned int,const Function *>::iterator fmit;
	std::vector<const Function*>::iterator fit;
	std::vector<const Function*> *targets;
	std::vector<bdd> *sd, *params;
	bdd fpts, fn, filter;
	unsigned int fv;
	CallData *cd;	
	Type *ft;

	//cd = dynamic_cast<CallData*>(sn->getExtraData());
	cd = static_cast<CallData*>(sn->getExtraData());
	fv = cd->funcId;
	sd = sn->getStaticData();
	ft = cd->funcType;
	fn = cd->funcName;
	//filter = genFilterSet(sn->getInSet(),gvarpts,cd->argset);
	filter = sn->getInSet();

	dbgs() << "FUNTYPE: " << (*ft) << "\n";
	// if func is pointer, dynamically compute its targets
	if (cd->isPtr) {
		targets = new std::vector<const Function*>();
		// if function is defined and doesn't point everywhere, compute it's points-to set
		if (fv && !pointsTo(*tpts,fv,0)) fpts = bdd_restrict(*tpts,fn);
		// otherwise, 
		else fpts = bdd_restrict(*tpts,fdd_ithset(0));
		// find which functions pointer points-to and types agree, add to targets
		// TODO: add any new targets to the calls for this function
		for (fmit = fm->begin(); fmit != fm->end(); ++fmit) {
			dbgs() << "TARGETTYPE: " << *(fmit->second->getFunctionType()) << "\n";
			if (bdd_sat(fpts & fdd_ithvar(1,fmit->first))) {
				if (fmit->second->getFunctionType() == ft) {
					dbgs() << "Func added\n";
					targets->push_back(fmit->second);
				} else {
					dbgs() << "Types: " << ft << " and " << fmit->second->getFunctionType() << " do not agree";
				}	
			} else dbgs() << "Does not point to function\n";
		}
	// else get its targets statically
	} else {
		dbgs() << "NOT PTR\n";
		// if this is only a declaration, fail
		// TODO: change this policy to something more robust
		//assert(!cd->targets->at(0)->isDeclaration());
		// TODO: if it's declaration, set return value points to everywhere
		// propagate top/addr, may change arguments
		// can use function to get more information like whether pass by reference
		// or return by reference. check llvm doc for more detail.
		if(cd->targets->at(0)->isDeclaration())
			return 0;
		targets = cd->targets;
	}

	dbgs() << "BDD FILTER:\n";
	printBDD(POINTSTO_MAX,filter);

	dbgs() << "ENUMERATE TARGETS\n";
	// foreach target
	for (fit = targets->begin(); fit != targets->end(); ++fit) {
		// get entry node and params, perform sanity checks
		dbgs() << "TARGET: " << *(*fit) << "\n";
		SEGNode *entry = sm->at(*fit)->getEntryNode();
		params = entry->getStaticData();
		assert(entry->getParent()->getFunction() == *fit && "Unequal functions!");
		assert(params != NULL && sd != NULL);
		assert(params->size() == sd->size());
		// for each argument, add parameter argument pair
		for (unsigned int i = 0; i < sd->size(); i++) {
			bdd param = params->at(i);
			bdd arg = sd->at(i);
			bdd newpts;
			// if argument is defined, add p -> Top(a)
			if (sn->getArgIds()->at(i))
				newpts = param & bdd_restrict(*tpts,arg);
			// else, add p -> everything
			else newpts = param & fdd_ithset(1);
			// propagate top level for callee
			propagateTopLevel(tpts,&newpts,entry);
		}
		// get SEG entry node's inset
		entry->setInSet(entry->getInSet() | filter);
		entry->setOutSet(entry->getInSet());
		// propagate using address taken on entry node
		if (propagateAddrTaken(entry))
				appendIfAbsent<const Function*>(&FuncWorkList,*fit);
	}	

	// set outset to inset - filter
	sn->setOutSet(sn->getInSet() - filter);
	// propagate address taken
	propagateAddrTaken(sn);
	return 0;
}

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
		sn->getStaticData()->push_back(fdd_ithset(1));
	}
	return 0;
}

int FlowSensitiveAliasAnalysis::processRet(bdd *tpts, SEGNode *sn) {
	std::map<const Function*,RetData*>::iterator cit;
	std::map<const Function*,RetData*> *Calls;
	bool changed = false;
	// find out where returned value points
	bdd retpts = sn->getStaticData()->at(0);
	if (sn->getArgIds()->at(0)) retpts = bdd_restrict(*tpts,retpts);
	// get call site list and iterate through it
	Calls = &Func2Calls.at(sn->getParent()->getFunction())->Calls;
	for (cit = Calls->begin(); cit != Calls->end(); ++cit) {
		RetData *rd = cit->second;
		SEGNode *callInst = rd->callInst;
		const Function *caller = callInst->getParent()->getFunction();
		dbgs() << "RET: Call " << *callInst << " from " << caller->getName() << "\n";
		// append my outset to caller's outset
		callInst->setOutSet(callInst->getOutSet() | sn->getOutSet());
		// propagate addr taken and record if worklist changed	
		changed = propagateAddrTaken(callInst) || changed;
		// if callsite stores a value, propagate on top level
		if (rd->callStatus != NO_SAVE) {
			dbgs() << "RET: Caller saves\n";
			bdd newpts = rd->saveName & retpts;
			printBDD(LocationCount,Int2Str,rd->saveName);
			changed = propagateTopLevel(tpts,&newpts,callInst) || changed;
		} else dbgs() << "RET: Caller doesn't save\n";
		// if caller's worklist changed, reinsert caller in worklist
		if (changed) appendIfAbsent<const Function*>(&FuncWorkList,caller);
	}
	return 0;
}
