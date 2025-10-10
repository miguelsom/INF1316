#include <stdio.h>
extern short s;
void f1(void) {
  s--;
  printf("s = %hd\n", s);
}
void f2(void) { f1(); }

//gcc -Wall -o prog prog1.c prog2.c
