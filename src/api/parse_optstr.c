/*
Copyright (c) 2015,2016 Jeremy Iverson

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/


#ifndef _GNU_SOURCE
# define _GNU_SOURCE 1
#endif


#include <string.h> /* strncmp */
#include "sbma.h"


/****************************************************************************/
/*! Compate two strings and check if the option has been seen before. */
/****************************************************************************/
#define SBMA_OPTCMP(__OPT, __SEEN, __TOK, __STR, __NUM)\
  ((0 == strncmp(__TOK, __STR, __NUM)) &&\
   ('\0' == __TOK[__NUM]) &&\
   ((__OPT) == (((__SEEN)^=(__OPT))&(__OPT))))


/****************************************************************************/
/*! Parse a string into options. */
/****************************************************************************/
SBMA_EXTERN int
sbma_parse_optstr(char const * const __opt_str)
{
  int opts=0, seen=0;
  int all=(VMM_RSDNT|VMM_LZYRD|VMM_AGGCH|VMM_GHOST|VMM_MERGE|VMM_METACH|\
    VMM_MLOCK|VMM_CHECK|VMM_EXTRA|VMM_OSVMM);
  char * tok;
  char str[512];

  if (strlen(__opt_str) > sizeof(str)-1)
    goto CLEANUP;

  strncpy(str, __opt_str, sizeof(str));

  tok = strtok(str, ",");
  while (NULL != tok) {
    if (SBMA_OPTCMP(VMM_RSDNT, seen, tok, "evict", 5)) {
    }
    else if (SBMA_OPTCMP(VMM_RSDNT, seen, tok, "rsdnt", 5)) {
      opts |= VMM_RSDNT;
    }
    else if (SBMA_OPTCMP(VMM_LZYRD, seen, tok, "aggrd", 5)) {
    }
    else if (SBMA_OPTCMP(VMM_LZYRD, seen, tok, "lzyrd", 5)) {
      opts |= VMM_LZYRD;
    }
    else if (SBMA_OPTCMP(VMM_ADMITD, seen, tok, "admitr", 6)) {
    }
    else if (SBMA_OPTCMP(VMM_ADMITD, seen, tok, "admitd", 6)) {
      opts |= VMM_ADMITD;
    }
    else if (SBMA_OPTCMP(VMM_AGGCH, seen, tok, "noaggch", 7)) {
    }
    else if (SBMA_OPTCMP(VMM_AGGCH, seen, tok, "aggch", 5)) {
      opts |= VMM_AGGCH;
    }
    else if (SBMA_OPTCMP(VMM_GHOST, seen, tok, "noghost", 7)) {
    }
    else if (SBMA_OPTCMP(VMM_GHOST, seen, tok, "ghost", 5)) {
      opts |= VMM_GHOST;
    }
    else if (SBMA_OPTCMP(VMM_MERGE, seen, tok, "nomerge", 7)) {
    }
    else if (SBMA_OPTCMP(VMM_MERGE, seen, tok, "merge", 5)) {
      opts |= VMM_MERGE;
    }
    else if (SBMA_OPTCMP(VMM_METACH, seen, tok, "nometach", 8)) {
    }
    else if (SBMA_OPTCMP(VMM_METACH, seen, tok, "metach", 6)) {
      opts |= VMM_METACH;
    }
    else if (SBMA_OPTCMP(VMM_MLOCK, seen, tok, "nomlock", 7)) {
    }
    else if (SBMA_OPTCMP(VMM_MLOCK, seen, tok, "mlock", 5)) {
      opts |= VMM_MLOCK;
    }
    else if (SBMA_OPTCMP((VMM_CHECK|VMM_EXTRA), seen, tok, "nocheck", 7)) {
    }
    else if (SBMA_OPTCMP((VMM_CHECK|VMM_EXTRA), seen, tok, "check", 5)) {
      opts |= VMM_CHECK;
    }
    else if (SBMA_OPTCMP((VMM_CHECK|VMM_EXTRA), seen, tok, "extra", 5)) {
      opts |= (VMM_CHECK|VMM_EXTRA);
    }
    else if (SBMA_OPTCMP(VMM_OSVMM, seen, tok, "noosvmm", 7)) {
    }
    else if (SBMA_OPTCMP(VMM_OSVMM, seen, tok, "osvmm", 5)) {
      opts |= VMM_OSVMM;
    }
    else if (SBMA_OPTCMP(all, seen, tok, "default", 7)) {
      opts |= (VMM_LZYRD|VMM_MERGE);
    }
    else {
      goto CLEANUP;
    }

    tok = strtok(NULL, ",");
  }

  /* VMM_OSVMM is not valid with any other options */
  if (VMM_OSVMM == (opts&VMM_OSVMM) && VMM_OSVMM != opts)
    goto CLEANUP;

  /* VMM_AGGCH is only valid without VMM_LZYRD */
  if (VMM_AGGCH == (opts&(VMM_LZYRD|VMM_AGGCH)))
    goto CLEANUP;

  /* VMM_EXTRA is only valid with VMM_CHECK */
  if (VMM_EXTRA == (opts&(VMM_CHECK|VMM_EXTRA)))
    goto CLEANUP;

  goto RETURN;

  CLEANUP:
  opts = VMM_INVLD;

  RETURN:
  return opts;
}


#ifdef TEST
int
main(int argc, char * argv[])
{
  if (0 == argc || NULL == argv) {}

  return 0;
}
#endif
