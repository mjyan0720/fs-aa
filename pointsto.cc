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
#include "pointsto.h"
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

using namespace llvm;

static unsigned int POINTSTO_MAX      = 0;
static bddPair* LPAIR                 = NULL;
static bddPair* RPAIR                 = NULL;
#define out2in(b)        bdd_replace(b,LPAIR)
#define in2out(b)        bdd_replace(b,RPAIR)
#define fdd_print(b)     do { fdd_printset(b); puts(""); } while(0)
#define fdd_prints(s,b)  do { printf("%s: ",s); fdd_printset(b); puts(""); } while(0)
#define bdd_print(b)     do { bdd_printset(b); puts(""); } while(0)
#define VALIDIDX1(i)     assert(i < POINTSTO_MAX)
#define VALIDIDX2(i,j)   assert(i < POINTSTO_MAX && j < POINTSTO_MAX)
#define restrictIn(b,i)  bdd_restrict(b,fdd_ithvar(0,i))
#define restrictOut(b,i) bdd_restrict(b,fdd_ithvar(1,i))

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

void propagateTopLevel(bdd *oldtpts, bdd *newpart, SEGNode *sn, WorkList* swkl, const Function *f) {
	std::list<SEGNode*> *wkl = swkl->at(f);
	// if old and new are different, add all users to worklist
	if (*oldtpts != (*oldtpts | *newpart)){
		dbgs()<<"propagate for:\t"<<*sn<<"\n";
		// only append to worklist if absent
		for(SEGNode::const_user_iterator i = sn->user_begin(); i != sn->user_end(); ++i)
			appendIfAbsent<SEGNode*>(wkl,*i);
	}
	// update old
	*oldtpts = *oldtpts | *newpart;
}

void propagateAddrTaken(SEGNode *sn, WorkList* swkl, const Function *f) {
	bdd oldink, newink;
	std::list<SEGNode*> *wkl = swkl->at(f);
	// add all changed successors to the worklist
	for(SEGNode::const_succ_iterator i = sn->succ_begin(); i != sn->succ_end(); ++i) {
		SEGNode *succ = *i;
		// get old and new in sets 
		oldink = succ->getInSet();
		newink = oldink | sn->getOutSet();
		// append to worklist if inset changed and not already in worklist
		if (oldink != newink){
			dbgs()<<"propagate for:\t"<<*sn<<"\n";
			appendIfAbsent<SEGNode*>(wkl,succ);
			succ->setInSet(newink);
		}
	}
}

// NOTE: alloc should never have undefined arguments
int preprocessAlloc(SEGNode *sn, std::map<const Value*,unsigned int> *im) {
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

int preprocessCopy(SEGNode *sn, std::map<const Value*,unsigned int> *im) {
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
		if (im->count(v) != 0){
			id = im->at(v);
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

int preprocessLoad(SEGNode *sn, std::map<const Value*,unsigned int> *im) {
	const LoadInst *ld = cast<LoadInst>(sn->getInstruction());
	std::vector<unsigned int> *ArgIds = new std::vector<unsigned int>();
	std::vector<bdd> *StaticData = new std::vector<bdd>();
	const Value *v = ld->getPointerOperand();
	// check if argument is defined
	sn->setDefined(im->count(v) != 0);
	// store static argument id, or zero if it is out-of-range
	ArgIds->push_back(sn->getDefined() ? im->at(v) : 0);
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

int preprocessStore(SEGNode *sn, std::map<const Value*,unsigned int> *im) {
	const StoreInst *sr = cast<StoreInst>(sn->getInstruction());
	std::vector<unsigned int> *ArgIds = new std::vector<unsigned int>();
	std::vector<bdd> *StaticData = new std::vector<bdd>();
	const Value *p,*v;
	bool pd, vd;
	p = sr->getPointerOperand();
	v = sr->getValueOperand();
	// check if arguments are defined
	pd = im->count(p) != 0;
	vd = im->count(v) != 0;
	sn->setDefined(pd & vd);
	
	sn->dump();
	dbgs()<<"Defined:\t"<<(int)(sn->getDefined())<<"\n";
	// store ids for argument values, or 0 for undefined
	ArgIds->push_back(pd ? im->at(p) : 0);
	ArgIds->push_back(vd ? im->at(v) : 0);
	sn->setArgIds(ArgIds);
	// store bdds for corresponding values, or everything for undefined
	StaticData->push_back(pd ? fdd_ithvar(0,ArgIds->at(0)) : fdd_ithset(0));
	StaticData->push_back(vd ? fdd_ithvar(0,ArgIds->at(1)) : fdd_ithset(1));
	sn->setStaticData(StaticData); 
	return 0;
}

int processAlloc(bdd *tpts, SEGNode *sn, WorkList* swkl) {
	bdd alloc;
	// add pair to top-level pts
	alloc = sn->getStaticData()->at(0);
	propagateTopLevel(tpts,&alloc,sn,swkl,sn->getParent()->getFunction());
	// propagate addr taken
	sn->setOutSet(sn->getInSet());
	propagateAddrTaken(sn,swkl,sn->getParent()->getFunction());
	return 0;
}

int processCopy(bdd *tpts, SEGNode *sn, WorkList* swkl) {
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
	if (bdd_unsat(newpts))
		llvm::dbgs() << "empty copy result\n";
	else
		llvm::dbgs() << "not empty\n";
	// store new top-level points-to set
	propagateTopLevel(tpts,&newpts,sn,swkl,sn->getParent()->getFunction());
	// propagate addr taken
	sn->setOutSet(sn->getInSet());
	propagateAddrTaken(sn,swkl,sn->getParent()->getFunction());
	return 0;
}

int processLoad(bdd *tpts, SEGNode *sn, WorkList *swkl) {
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
	propagateTopLevel(tpts,&newpts,sn,swkl,sn->getParent()->getFunction());
	// propagate addr taken
	sn->setOutSet(sn->getInSet());
	propagateAddrTaken(sn,swkl,sn->getParent()->getFunction());
	return 0;
}

int processStore(bdd *tpts, SEGNode *sn, WorkList* swkl) {
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
	propagateAddrTaken(sn,swkl,sn->getParent()->getFunction());
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

int preprocessCall(SEGNode *sn, std::map<const Value*,unsigned int> *im) {
	std::vector<unsigned int> *ArgIds = new std::vector<unsigned int>();
	std::vector<bdd> *StaticData = new std::vector<bdd>();
	CallData *cd = new CallData();
	const CallInst *ci = cast<CallInst>(sn->getInstruction());
	const Value *funv = ci->getCalledValue();	
	const Function *fun = ci->getCalledFunction();
	unsigned int id;
	// check if this function is a pointer and if it is defined
	cd->isPtr = (fun == NULL);
	cd->isDefinedFunc = im->count(funv) != 0;
	// if function called is defined, store it's name
	if (cd->isDefinedFunc) {
		cd->funcId = im->at(funv);
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
		if (im->count(v) != 0) id = im->at(v); 
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

int processCall(bdd *tpts,
                SEGNode *sn,
                WorkList* swkl,
                std::list<const Function*> *fwkl,
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
	filter = genFilterSet(sn->getInSet(),gvarpts,cd->argset);

	dbgs() << "FUNTYPE: " << (*ft) << "\n";
	// if func is pointer, dynamically compute its targets
	if (cd->isPtr) {
		targets = new std::vector<const Function*>();
		// if function is defined and doesn't point everywhere, compute it's points-to set
		if (fv && !pointsTo(*tpts,fv,0)) fpts = bdd_restrict(*tpts,fn);
		// otherwise, 
		else fpts = bdd_restrict(*tpts,fdd_ithset(0));
		// find which functions pointer points-to and types agree, add to targets
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
		assert(!cd->targets->at(0)->isDeclaration());
		targets = cd->targets;
	}

	dbgs() << "ENUMERATE TARGETS\n";
	// foreach target
	for (fit = targets->begin(); fit != targets->end(); ++fit) {
		// get entry node and params, perform sanity checks
		dbgs() << "TARGET: " << *(*fit) << "\n";
		SEGNode *entry = sm->at(*fit)->getEntryNode();
		params = entry->getStaticData();
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
			// TODO: do I need to change the propagatation function?
			propagateTopLevel(tpts,&newpts,entry,swkl,*fit);
		}

		// get SEG entry node's inset
		std::list<SEGNode*>* wkl = swkl->at(*fit);
		bdd inset = entry->getInSet();
		// if inset has changed and worklist has changed, reinsert entry node in worklist
		if (inset != (inset | filter) && appendIfAbsent<SEGNode*>(wkl,entry))
			appendIfAbsent<const Function*>(fwkl,*fit);
		// set target function's entry node's inset
		entry->setInSet(inset | filter);
	}	

	// set outset to inset - filter
	sn->setOutSet(sn->getInSet() - filter);
	// propagate address taken
	propagateAddrTaken(sn,swkl,sn->getParent()->getFunction());
	return 0;
}

int preprocessRet(SEGNode *sn, std::map<const Value*,unsigned int> *im) {
	return 0;
}

int processRet(bdd *tpts, SEGNode *sn, WorkList* swkl) {
	// iterate through callsite list (list of segnodes)
	// for each segnode:
		// set the inset of callsite to include my outset
		// propagateAddrTaken on caller
		// if callsite stores a value (not a voidTy)
			// update value to point to returned value in tpts
			// propagateTopLevel on caller
		// if caller's worklist changed, reinsert caller in worklist
	return 0;
}
