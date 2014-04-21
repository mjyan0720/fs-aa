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

// given a b which is a set in FDD1, find all elements in it
std::vector<unsigned int> *pointsto(bdd b) {
	std::vector<unsigned int> *v;
	v = new std::vector<unsigned int>();
	// check if each element is in the set
	for (unsigned int i = 0; i < POINTSTO_MAX; i++) {
		int cnt = bdd_satcount(restrictIn(b,i));
		if (cnt >= 1) v->push_back(i);
		// if set has mainly small values, add:
		// if (bdd_satcount(b - fdd_ithvar(0,i)) = 0) break;
	}
	return v;
}

void propagateTopLevel(bdd *oldtpts, bdd *newpart, SEGNode *sn, WorkList* swkl, const Function *f) {
	std::list<SEGNode*> *wkl = swkl->at(f);
	// if old and new are different, add all users to worklist
	if (*oldtpts != (*oldtpts | *newpart)){
		dbgs()<<"propagate for:\t"<<*sn<<"\n";
		for(SEGNode::const_user_iterator i = sn->user_begin(); i != sn->user_end(); ++i){
			std::list<SEGNode*>::iterator I = std::find(wkl->begin(), wkl->end(), *i);
			if(I==wkl->end())
				wkl->push_back(*i);
		}
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
		// add if changed
		if (oldink != newink){
			dbgs()<<"propagate for:\t"<<*sn<<"\n";
			std::list<SEGNode*>::iterator I = std::find(wkl->begin(), wkl->end(), *i);
			if(I==wkl->end())
				wkl->push_back(succ);
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
		// if argument out-of-range, store id 0i
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
	if(newpts == bdd_false())
		llvm::dbgs()<<"empty copy result\n";
	else
		llvm::dbgs()<<"not empty\n";
// store new top-level points-to set
	propagateTopLevel(tpts,&newpts,sn,swkl,sn->getParent()->getFunction());
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
		outkpts = bdd_apply(sn->getInSet(),topx,bddop_diff);
	// else weak update
	else outkpts = sn->getInSet();
	// return modified outkpts
	sn->setOutSet(outkpts | (topx & topy));
	propagateAddrTaken(sn,swkl,sn->getParent()->getFunction());
	return 0;
}

/*
 * TODO: We need to handle the case of function pointers.
 *       Thus, one of the following should occur:
 *         1) The function is constant (not a pointer) and the params are passed in via arguments
 *         2) The function is a pointer, the param arguments are NULL, and then each function
 *            pointed to is discovered dynamically and a set of function ids is returned
 *       
 */
bdd processCall(bdd tpts, bdd inkpts, bdd global, unsigned int x, unsigned int f, bool ptr, std::vector<unsigned int> *args) {
	std::vector<unsigned int>::iterator fit, pit;
	std::vector<unsigned int> *funcs, *params;
	std::vector<bdd>::iterator ait;
	std::vector<bdd> *argpts;
	bdd filtk, bddargs, outkpts;
	unsigned int size;
	VALIDIDX2(x,f);
	size = args->size();
	bddargs = bddfalse;
	funcs = new std::vector<unsigned int>();
	argpts = new std::vector<bdd>(); 
	// compute every call argument OR'd together
	// and where call arguments point
	for (pit = args->begin(); pit != args->end(); ++pit) {
		VALIDIDX1(*pit);
		bddargs = bddargs | fdd_ithvar(0,*pit);
		argpts->push_back(restrictIn(tpts,*pit));
	}
	// filter input by reachable from global or call arg
	filtk = inkpts & (global | bddargs);
	// find out which function(s) f refers to
	if (!ptr) funcs->push_back(f);
	else {
		free(funcs);
		funcs = pointsto(restrictIn(tpts,f));
	}
	// for each potential callee
	for (fit = funcs->begin(); fit != funcs->end(); ++fit) {
		// get the call parameters
		params = funParams(*fit);
		assert(params->size() == size);
		// for each parameter and argument
		for (pit = params->begin(), ait = argpts->begin(); pit != params->end(); ++pit, ++ait) {
			VALIDIDX1(*pit);
			// update parameter to point where argument points to
			tpts = tpts | (fdd_ithvar(0,*pit) & *ait);
			// propagate top level edges
			propagateTopLevel(*fit,*pit);
		}
		// update worklists and function entry node
		updateWorklist1(*fit,updateFunEntry(*fit,filtk));
	}
	// propogate address taken TODO: fix f and k
	propagateAddrTaken(0,0);
	// update outset TODO: store it
	outkpts = outkpts | (inkpts - filtk);
	return tpts;
}

unsigned int processRet(unsigned int f, unsigned int k, bdd tpts, unsigned int x) {
	std::vector<callsite_t> *callsites;
	std::vector<callsite_t>::iterator cit;
	// get callsites
	callsites = funCallsites(f);
	// for each callsite
	for (cit = callsites->begin(); cit != callsites->end(); ++cit) {
		// TODO: get ref to SEG node for each callsite
		// propogate AddrTaken
		propagateAddrTaken(cit->first,cit->second);
		// if this call is an assignment: r = f( ... )
		if (assignedCall(cit->second)) {
			// tpts = tpts | (fdd_ithvar(0,r) & restrictIn(tpts,x));
			propagateTopLevel(cit->first,cit->second);
		}
		// if statement worklist changed TODO: finish this
		// if (statementWorklistChanged(cit->first())) {
		//}
	}
	return 0;
}

/*
int main(void) { bdd tpts,kpts,opts;
	set<unsigned int> vars;
	vars.insert(1);
	vars.insert(2);
	vars.insert(4);
	pointsToInit(1000,1000,10);
	tpts = bdd_false();
	tpts = processAlloc(tpts,1,1);
	tpts = processAlloc(tpts,2,2);
	tpts = processAlloc(tpts,4,4);
	tpts = processCopy(tpts,3,&vars);
	fdd_print(tpts);
	//
	tpts = bdd_false();
	kpts = bdd_false();
	tpts = processAlloc(tpts,0,1);
	tpts = processAlloc(tpts,0,2);
	tpts = processAlloc(tpts,0,3);
	kpts = processAlloc(kpts,1,5);
	kpts = processAlloc(kpts,2,6);
	kpts = processAlloc(kpts,3,7);
	kpts = processAlloc(kpts,4,0);
	fdd_print(tpts);
	tpts = processLoad(tpts,kpts,9,0);
	fdd_print(tpts);
	//
	tpts = bdd_false();
	kpts = bdd_false();
	tpts = processAlloc(tpts,1,3);
	tpts = processAlloc(tpts,3,5);
	tpts = processAlloc(tpts,3,6);
	tpts = processAlloc(tpts,3,7);
	kpts = processAlloc(kpts,1,2);
	kpts = processAlloc(kpts,1,5);
	kpts = processAlloc(kpts,2,6);
	kpts = processAlloc(kpts,3,8);
	fdd_print(kpts);
	kpts = processStore(tpts,kpts,1,3);	
	fdd_print(kpts);
	//
	tpts = bdd_false();
	tpts = processAlloc(tpts,1,3);
	pointsToFinalize();
}
*/
