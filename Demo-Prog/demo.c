#include <stdio.h>
#include "tests/threads/tests.h"
#include "threads/init.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "devices/timer.h"

void test_demo(void) {
  printf("Here I am\n");
}

void test_demo2(void) {
	int i=10;
	while(i)
		printf("Yes\n");
}