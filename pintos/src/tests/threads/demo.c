#include <stdio.h>
#include "tests/threads/tests.h"
#include "threads/init.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "devices/timer.h"



void test_demo(void) {

	 char *pc, k;
     int chars=20;
    /* now allocate memory */
    pc = (char *)malloc(chars * sizeof(char));
    /* let us see the contents of allocated block */
    k = 0;
    printf("Allocated block, an array of %d chars, contains values:",
            chars);
    while (k < chars) {
            printf("%2c", *(pc + k));
            k++;
    }
 
    /* Now we free allocated block back to pool of available memory */
    free(pc);

    char *mem_allocation;
     /* memory is allocated dynamically */
     mem_allocation = malloc( 20 * sizeof(char) );
     if( mem_allocation== NULL )
     {
        printf("Couldn't able to allocate requested memory\n");
     }
     free(mem_allocation);



int **Y=(int **)malloc(50*sizeof(int *));
int i,j;
for(i=0;i<50;++i)
{
    printf("+++%d\n",i );
   
    Y[i]=(int *)malloc(sizeof(int)*2);
}

for(i=0;i<50;i++)
{
for(j=0;j<2;++j)
{   
   
    Y[i][j]=6;
printf("i= %d,\tj= %d\n",i,j);
}
printf("\n");
}

for (i=0;i<50;i++) free(Y[i]);
free(Y);
printf("Here I am\n");
/*
int *X=(int *)malloc(sizeof(int)*2);
X[0]=0;
X[1]=1;
printf("%d  %d\n",X[0],X[1] );
*/

}
void test_demo2(void) {
	int i=10;
	while(i)
		printf("Yes\n");
}