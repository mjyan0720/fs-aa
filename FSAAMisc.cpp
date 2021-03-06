#include "FSAAnalysis.h"
#include <string>

#undef  DEBUG_TYPE
#define DEBUG_TYPE "fsaa-reversevaluemap"

// macros to make reverseMap function more readable
#define insertName(m,r,f,s)                                             \
  do {                                                                  \
    ((r) = (m)->insert(std::pair<unsigned int,std::string*>((f),(s)))); \
  } while(0)
#define ss(s) std::string(s)

// build reverseMap (for debugging purposes)
std::map<unsigned int,std::string*> *reverseMap(std::map<const Value*,unsigned int> *m) {
	std::pair<std::map<unsigned int,std::string*>::iterator,bool> ret;
	std::map<unsigned int,std::string *> *inv = new std::map<unsigned int,std::string*>();
	std::string *name;
	std::string prename;
	char buf[100];
	unsigned int anon = 1;
	// build inverse map (also check map is 1-to-1)
	for (std::map<const Value*,unsigned int>::iterator it = m->begin(); it != m->end(); ++it) {
		const Value *v = it->first;
		unsigned int id = it->second;
		// if value is anonymous, give it a numeric name
		if (v->getName().size() == 0) {
			assert(snprintf(buf,100,"%d",anon++) < 100);
			prename = ss(buf);
		} else prename = v->getName();
		// if value is an instruction or argument, add it's function's parent name
		if (isa<Instruction>(v))
			name = new ss(ss(cast<Instruction>(v)->getParent()->getParent()->getName())+"_"+ss(prename));
		else if (isa<Argument>(v))
			name = new ss(ss(cast<Argument>(v)->getParent()->getName())+"_"+ss(prename));
		else
			name = new ss(prename);
/*#ifdef ENABLE_OPT_1
		// in the opt1 version, they are not assigned an id, they share the id with
		// source value used at right hand side
		// insert will fail.
		if (isa<GetElementPtrInst>(v) | isa<CastInst>(v))
			continue;
#endif*/
		DEBUG(v->dump());
		// add hidden names for each value type that has hidden values
		if (isa<AllocaInst>(v)) {
			insertName(inv,ret,id,name);
			insertName(inv,ret,id+1,new ss(*name + "__HEAP"));
		} else if (isa<GlobalVariable>(v)) {
			insertName(inv,ret,id,name);
			insertName(inv,ret,id+1,new ss(*name + "__VALUE"));
		} else if (isa<Function>(v)) {
			insertName(inv,ret,id,name);
			insertName(inv,ret,id+1,new ss(*name + "__FUNCTION"));
		} else if (isa<Argument>(v)) {
			insertName(inv,ret,id,name);
			insertName(inv,ret,id+1,new ss(*name + "__ARGUMENT"));
		// otherwise, store regular name only
		} else {
			insertName(inv,ret,id,name);
		}
	}
	// store everything value
	insertName(inv,ret,0,new ss("EVERYTHING"));
	return inv;
}

void printReverseMap(std::map<unsigned int,std::string*> *m) {
	dbgs() << "REVERSEVALUEMAP:\n";
	for (std::map<unsigned int,std::string*>::iterator it = m->begin(); it != m->end(); ++it) {
		dbgs() << it->first << " : " << *(it->second) << "\n";
	}
}

void FlowSensitiveAliasAnalysis::printValueMap(){
	dbgs()<<"VALUEMAP:\n";
	for(std::map<const Value*, unsigned>::iterator mi=Value2Int.begin(), me=Value2Int.end(); mi!=me; ++mi){
		dbgs()<<mi->first->getName()<<" --> "<<mi->second<<"\n";
	}
}
