#include "bdd.h"
#include "fdd.h"
#include "llvm/Support/Debug.h"
#include <map>
#include <cassert>

using namespace std;

#define bdd_sat(b)   ((b) != bdd_false())

// Global to track size of POINTSTO bdd
unsigned int POINTSTO_MAX = 0;
bddPair* LPAIR            = NULL;
bddPair* RPAIR            = NULL;

void pointsToInit(unsigned int nodes, unsigned int cachesize, unsigned int domainsize) {
	int errc;
	int domain[2];
	// initialize bdd library
	assert(!bdd_isrunning());
	errc = bdd_init(nodes,cachesize);
	if (errc < 0) llvm::dbgs() << bdd_errstring(errc) << "\n";
	assert(bdd_isrunning());
	// add necessary bdd variables
	domain[0] = domain[1] = POINTSTO_MAX = domainsize;
	errc = fdd_extdomain(domain,2);
	if (errc < 0) llvm::dbgs() << bdd_errstring(errc) << "\n";
	assert(errc >= 0);
	// build bdd pairs
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

// print out a single points-to mapping from Value named i to Valued named j
void printMapping(map<unsigned int,string*> *lt, int i, int j) {
	string *s1,*s2;
	// check if these strings have a non-null mapping
	s1 = lt != NULL && lt->count(i) > 0 ? lt->at(i) : NULL;
	s2 = lt != NULL && lt->count(j) > 0 ? lt->at(j) : NULL;
	// print out first elt
	if (s1 != NULL) DEBUG(llvm::dbgs() << *s1 << " -> ");
	else if (lt != NULL) DEBUG(llvm::dbgs() << "NOT FOUND: " << i << " -> ");
	else DEBUG(llvm::dbgs() << i << " -> ");
	// print out second elt
	if (s2 != NULL) DEBUG(llvm::dbgs() << *s2 << "\n");
	else if (lt != NULL) DEBUG(llvm::dbgs() << "NOT FOUND: " << j << "\n");
	else DEBUG(llvm::dbgs() << j << "\n");
}

// print out a whole BDD; note that this will be very slow for large BDDs
void printBDD(unsigned int max, map<unsigned int,string*> *lt, bdd b) {
	unsigned int i, j;
	bool empty = true;
	for (i=0;i<max;++i) {
		// if variable points everywhere, just print points to everything
		if (bdd_sat(b & fdd_ithvar(0,i) & fdd_ithvar(1,0))) {
			empty = false;
			printMapping(lt,i,0);
			continue;
		}
		// otherwise, find out where it actually points
		for (j=1;j<max;++j) {
			if (bdd_sat(b & fdd_ithvar(0,i) & fdd_ithvar(1,j))) {
				empty = false;
				printMapping(lt,i,j);
			}
		}
	}
	// if set is is empty, print empty
	if (empty) DEBUG(llvm::dbgs() << "EMPTY\n");
}

void printBDD(unsigned int max, bdd b) { printBDD(max,NULL,b); }
