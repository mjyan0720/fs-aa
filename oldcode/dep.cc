#include <cassert>
#include <iostream>
#include <vector>
#include "bdd.h"

// DUMMY MAIN
int main(void) { return 0; }

// CODE STARTS HERE
using namespace std;
static vector<unsigned int> *BDD_DATA = NULL;

// PROBLEM: don't know how to fix variable ordering for this to work
// generates a vector of satisfying assignments as integers, but
// integers found are random
void getBddInData(char *vs, int sz) {
  vector<unsigned int> data;
  unsigned int constant, tmp;
  unsigned int one = 1;
  // sz % 2 is 0 because we have two equal size fdd's
  // sz < 32 is a sanity check; we can't represent sizes above it
  assert(sz % 2 == 0 && sz < 32);
  sz = sz / 2;
  constant = 0;
  data = vector<unsigned int>();
  // print out vs
  // set constant parts of assignment
  for (int i = 0; i < sz; i++) {
    if (vs[i] == 1) constant = constant | (one << i);
  } 
  cout << endl;
  // push back initial constant value
  data.push_back(constant);
  // set non-constant parts of assignment
  for (unsigned int i = 0; i < sz; i++) {
    if (vs[i] == -1) {
      int vecsize = data.size();
      // double the vector size (append 1's to half and 0's to half)
      for (unsigned int j = 0; j < vecsize; j++) {
        tmp = data[j];
        data.push_back(tmp);
        data[j] = tmp | (one << i);
      }
    }
  }
  // append on to end of BDD_DATA
  BDD_DATA->reserve(BDD_DATA->size() + distance(data.begin(),data.end()));
  BDD_DATA->insert(BDD_DATA->end(),data.begin(),data.end());
}

// SIMPLE EXAMPLE PRINT HANDLER
void allsatPrintHandler(char* varset,int size) {
  size = size / 2;
  for(int v=0; v<size; ++v) {
    cout<<(varset[v]<0 ? 'X': (char)('0'+varset[v]));
  }
  cout<<endl;
}
