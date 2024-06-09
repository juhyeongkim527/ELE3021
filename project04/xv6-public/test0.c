#include "types.h"
#include "stat.h"
#include "user.h"

int 
main(int argc, char* argv[])
{
  printf(1, "[Test 0] default\n");

  int numfp = countfp();
  int numvp = countvp();
  int numpp = countpp();
  int numptp = countptp();

  // printf(1, "before fp : %d\n\n", numfp);
  // printf(1, "before vp : %d\n\n", numvp);
  // printf(1, "before pp : %d\n\n", numpp);
  // printf(1, "before ptp : %d\n\n", numptp);

  sbrk(4096);

  int numfpa = countfp();
  int numvpa = countvp();
  int numppa = countpp();
  int numptpa = countptp();

  // printf(1, "after fp : %d\n\n", numfpa);
  // printf(1, "after vp : %d\n\n", numvpa);
  // printf(1, "after pp : %d\n\n", numppa);
  // printf(1, "after ptp : %d\n\n", numptpa);
  
  printf(1, "ptp: %d %d\n", numptp, numptpa);

  if((numvp == numpp) && (numvpa == numppa) && (numfp - numfpa == 1))
    printf(1, "[Test 0] pass\n\n");
  else
    printf(1, "[Test 0] fail\n\n");

  exit();
}

