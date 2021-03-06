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

using namespace std;

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

void check(int rc) {
  if (rc < 0) {
    puts(bdd_errstring(rc));
    exit(1);
  } else {
    printf("Value: %d\n",rc);
  }
}

void pointsToInit(unsigned int nodes, unsigned int cachesize, unsigned int domainsize) {
  int domain[2];
  domain[0] = domain[1] = POINTSTO_MAX = domainsize;
  assert(domainsize >= 0);
  bdd_init(nodes,cachesize);
  assert(fdd_extdomain(domain,2) >= 0);
  LPAIR = bdd_newpair();
  RPAIR = bdd_newpair();
  check(fdd_setpair(LPAIR,1,0));
  check(fdd_setpair(RPAIR,0,1));
}

void pointsToFinalize() {
  bdd_freepair(LPAIR);
  bdd_freepair(RPAIR);
  bdd_done();
}

// given a b which is a set in FDD1, find all elements in it
vector<unsigned int> *pointsto(bdd b) {
  vector<unsigned int> *v;
  v = new vector<unsigned int>();
  // check if each element is in the set
  for (unsigned int i = 0; i < POINTSTO_MAX; i++) {
    int cnt = bdd_satcount(restrictIn(b,i));
    if (cnt >= 1) v->push_back(i);
    // if set has mainly small values, add:
    // if (bdd_satcount(b - fdd_ithvar(0,i)) = 0) break;
  }
  return v;
}

bdd processAlloc(bdd tpts, int v1, int v2) {
  bdd alloc;
  VALIDIDX2(v1,v2);
  alloc = fdd_ithvar(0,v1) & fdd_ithvar(1,v2);
  return tpts | alloc;
}

bdd processCopy(bdd tpts, int x, set<int> vars) {
  bdd vs, vtpts;
  VALIDIDX1(x);
  // build up union of all v possibilities
  vs = bdd_false();
  for(set<int>::iterator elt = vars.begin(); elt != vars.end(); ++elt) {
    VALIDIDX1(*elt);
    vs = vs | fdd_ithvar(0,*elt);
  }
  // quantify over original bdd + vs choices for all v values
  vtpts = bdd_relprod(tpts,vs,fdd_ithset(0));
  // extend top tpts with (x -> vtpts)
  tpts = tpts | (fdd_ithvar(0,x) & vtpts);
  return tpts;
}

bdd processLoad(bdd tpts, bdd kpts, int x, int y) {
  bdd topy, ky, extpts;
  VALIDIDX2(x,y);
  // get PTop(y)
  topy = out2in(restrictIn(tpts,y));
  // get PK(PTop(y))
  ky   = bdd_relprod(kpts,topy,fdd_ithset(0));
  // extend top pts
  tpts = tpts | (fdd_ithvar(0,x) & ky);
  return tpts;
}

bdd processStore(bdd tpts, bdd inkpts, int x, int y) {
  bdd bddx, bddy, topx, topy, outkpts;
  VALIDIDX2(x,y);
  // get x and y BDDs
  bddx = fdd_ithvar(0,x);
  bddy = fdd_ithvar(0,y);
  // get PTop(y)
  topy = bdd_restrict(tpts,bddy);
  // get PTop(x)
  topx = out2in(bdd_restrict(tpts,bddx));
  // if only 1 satisfying assignment then strong update
  if (bdd_satcount(bddx & tpts) == 1.0)
    outkpts = bdd_apply(inkpts,topx,bddop_diff);
  // else weak update
  else outkpts = inkpts;
  // return modified outkpts
  return outkpts | (topx & topy);
}

void propogateTopLevel(int f, int k) { } 
void propogateAddrTaken(int f, int k) { }
void updateWorklist1(int f,bool changed) { }
bool updateFunEntry(int f, bdd filtk) { return true; }
vector<unsigned int> *funParams(int f) { return new vector<unsigned int>(); }
//
typedef pair<unsigned int, unsigned int> callsite_t;
vector<callsite_t> *funCallsites(int f) { return new vector<callsite_t>(); }
bool assignedCall(unsigned int) { return true; }


/*
 * TODO: We need to handle the case of function pointers.
 *       Thus, one of the following should occur:
 *         1) The function is constant (not a pointer) and the params are passed in via arguments
 *         2) The function is a pointer, the param arguments are NULL, and then each function
 *            pointed to is discovered dynamically and a set of function ids is returned
 *       
 */
bdd processCall(bdd tpts, bdd inkpts, bdd global, int x, int f, bool ptr, vector<unsigned int> *args) {
  vector<unsigned int>::iterator fit, pit;
  vector<unsigned int> *funcs, *params;
  vector<bdd>::iterator ait;
  vector<bdd> *argpts;
  bdd filtk, bddargs, outkpts;
  unsigned int size;
  VALIDIDX2(x,f);
  size = args->size();
  bddargs = bddfalse;
  funcs = new vector<unsigned int>();
  argpts = new vector<bdd>(); 
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
      // propogate top level edges
      propogateTopLevel(*fit,*pit);
    }
    // update worklists and function entry node
    updateWorklist1(*fit,updateFunEntry(*fit,filtk));
  }
  // propogate address taken TODO: fix f and k
  propogateAddrTaken(0,0);
  // update outset TODO: store it
  outkpts = outkpts | (inkpts - filtk);
  return tpts;
}

int processRet(int f, int k, bdd tpts, int x) {
  vector<callsite_t> *callsites;
  vector<callsite_t>::iterator cit;
  // get callsites
  callsites = funCallsites(f);
  // for each callsite
  for (cit = callsites->begin(); cit != callsites->end(); ++cit) {
    // TODO: get ref to SEG node for each callsite
    // propogate AddrTaken
    propogateAddrTaken(cit->first,cit->second);
    // if this call is an assignment: r = f( ... )
    if (assignedCall(cit->second)) {
      // tpts = tpts | (fdd_ithvar(0,r) & restrictIn(tpts,x));
      propogateTopLevel(cit->first,cit->second);
    }
    // if statement worklist changed TODO: finish this
    // if (statementWorklistChanged(cit->first())) {
    //}
  }
  return 0;
}

int main(void) { bdd tpts,kpts,opts;
  set<int> vars;
  vars.insert(1);
  vars.insert(2);
  vars.insert(4);
  pointsToInit(1000,1000,10);
	/*
  tpts = bdd_false();
  tpts = processAlloc(tpts,1,1);
  tpts = processAlloc(tpts,2,2);
  tpts = processAlloc(tpts,4,4);
  //tpts = processCopy(tpts,3,vars);
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
	*/
	tpts = bdd_false();
	kpts = bdd_false();
	tpts = processAlloc(tpts,1,2);
	tpts = processAlloc(tpts,3,4);
	//tpts = processAlloc(tpts,3,7);
	kpts = processStore(tpts,kpts,1,3);	
	tpts = processLoad(tpts,kpts,5,1);
	fdd_print(tpts);
}
