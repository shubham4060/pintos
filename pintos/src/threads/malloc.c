#include "threads/malloc.h"
#include <debug.h>
#include <list.h>
#include <round.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/vaddr.h"

/* A simple implementation of malloc().

   The size of each request, in bytes, is rounded up to a power
   of 2 and assigned to the "descriptor" that manages blocks of
   that size.  The descriptor keeps a list of free blocks.  If
   the free list is nonempty, one of its blocks is used to
   satisfy the request.

   Otherwise, a new page of memory, called an "arena", is
   obtained from the page allocator (if none is available,
   malloc() returns a null pointer).  The new arena is divided
   into blocks, all of which are added to the descriptor's free
   list.  Then we return one of the new blocks.

   When we free a block, we add it to its descriptor's free list.
   But if the arena that the block was in now has no in-use
   blocks, we remove all of the arena's blocks from the free list
   and give the arena back to the page allocator.

   We can't handle blocks bigger than 2 kB using this scheme,
   because they're too big to fit in a single page with a
   descriptor.  We handle those by allocating contiguous pages
   with the page allocator and sticking the allocation size at
   the beginning of the allocated block's arena header. */

/* Descriptor. */
struct desc
  {
    size_t block_size;          /* Size of each element in bytes. */
    size_t blocks_per_arena;    /* Number of blocks in an arena. */
    struct list free_list;      /* List of free blocks. */
    struct lock lock;           /* Lock. */
  };

/* Magic number for detecting arena corruption. */
#define ARENA_MAGIC 0x9a548eed

/* Arena. */
struct arena 
  {
    struct list_elem free_elem;
    unsigned magic;             /* Always set to ARENA_MAGIC. */
    struct desc *desc;          /* Owning descriptor, null for big block. */
    size_t free_cnt;            /* Free blocks; pages in big block. */
  };

/* Free block. */
struct block 
  {
    struct list_elem free_elem; /* Free list element. */
    size_t size;
  };

/* Our set of descriptors. */
static struct desc descs[10];   /* Descriptors. */
static size_t desc_cnt;         /* Number of descriptors. */
static struct list arena_list;

static struct arena *block_to_arena (struct block *);
static struct block *arena_to_block (struct arena *, size_t idx);
static void malloc_breakdown (struct desc *,struct desc *);
static int free_buildup (struct block *, struct desc *);
static void printMemory(void);
/* Initializes the malloc() descriptors. */
void
malloc_init (void) 
{
  size_t block_size;
 
  for (block_size = 16; block_size < PGSIZE / 2; block_size *= 2)
    {
      struct desc *d = &descs[desc_cnt++];
      ASSERT (desc_cnt <= sizeof descs / sizeof *descs);
      d->block_size = block_size;
      d->blocks_per_arena = (PGSIZE - sizeof (struct arena)) / block_size;
      list_init (&d->free_list);
      lock_init (&d->lock);
    }

  list_init (&arena_list);
}

void printMemory(void){
  size_t num=0;
  struct list_elem *e,*f;
  struct arena *a;
  size_t block_size;
  struct desc *d;

  num=list_size(&arena_list);
  printf("--------------------------------------------------------------------\n");
  printf("No. of pages allocated : %d\n\n", num);

  int i=1;
  for (e = list_begin (&arena_list); e != list_end (&arena_list); e = list_next (e)) {
    printf("\n***Page %d : \n\n",i);
    i++;
    a=(struct arena *)e;
    int j=0;
    for (block_size = 16; block_size < PGSIZE / 2; block_size *= 2)
    {
      printf("Size %d : \t",block_size);
      d=&descs[j];
      for (f = list_begin (&d->free_list); f != list_end (&d->free_list); f = list_next (f)) {
        if (block_to_arena((struct block *)f) == a) printf("%d (%d) , ",(uint8_t *)f, ((struct block *)f)->size);
      }
      j++;
      printf("\n");
    }
  }
  printf("--------------------------------------------------------------------\n");
  return;
}


//helper function for malloc
void
malloc_breakdown(struct desc *t, struct desc *d){
  if ( t->block_size == d->block_size ) return;
  
  struct block *b,*b1,*b2;
  struct arena *a=NULL;
  struct desc *next;
  struct list_elem *e;

  lock_acquire (&t->lock);
  b = list_entry (list_pop_front (&t->free_list), struct block, free_elem);
  a = block_to_arena (b);
  a->free_cnt--;
  lock_release (&t->lock);

  t--;
  b1=(struct block *)b;
  b1->size=t->block_size;
//  printf("%d  %d\n",(uint8_t *)b1,(uint8_t *)&b1->free_elem );

  b2=(struct block *) ((uint8_t *) b1 + t->block_size);
  b2->size=t->block_size;
//  printf("%d  %d\n",(uint8_t *)b2,(uint8_t *)&b2->free_elem );

//  if ((uint8_t *)&b2->free_elem  == (uint8_t *)&b1->free_elem  + b1->size ) printf("joda jaa sakta hai!\n");

  next=t;

  lock_acquire (&next->lock);
  a = block_to_arena (b1);
  for (e = list_begin (&next->free_list); e != list_end (&next->free_list); e = list_next (e))
    if ((uint8_t *)&b1->free_elem < (uint8_t *)e)
      break;
  list_insert (e, &b1->free_elem);
  a->free_cnt++;
  a = block_to_arena (b2);
  for (e = list_begin (&next->free_list); e != list_end (&next->free_list); e = list_next (e))
    if ((uint8_t *)&b2->free_elem < (uint8_t *)e)
      break;
  list_insert (e, &b2->free_elem);
  a->free_cnt++;
  lock_release (&next->lock);

  malloc_breakdown(t,d);
}

/* Obtains and returns a new block of at least SIZE bytes.
   Returns a null pointer if memory is not available. */
void *
malloc (size_t size) 
{
  struct desc *d,*t;
  struct block *b;
  struct arena *a;

  /* A null pointer satisfies a request for 0 bytes. */
  if (size == 0)
    return NULL;

  /* Find the smallest descriptor that satisfies a SIZE-byte
     request. */
  size+=sizeof *b;
  for (d = descs; d < descs + desc_cnt; d++)
    if (d->block_size >= size)
      break;

  if (d == descs + desc_cnt) 
    {
      /* SIZE is too big for any descriptor.
         Allocate enough pages to hold SIZE plus an arena. */
      size_t page_cnt = DIV_ROUND_UP (size + sizeof *a, PGSIZE);
      a = palloc_get_multiple (0, page_cnt);
      if (a == NULL)
        return NULL;

      /* Initialize the arena to indicate a big block of PAGE_CNT
         pages, and return it. */
      a->magic = ARENA_MAGIC;
      a->desc = NULL;
      a->free_cnt = page_cnt;
      return a + 1;
    }

  lock_acquire (&d->lock);

  /* If the free list is empty, create a new arena. */
  if (list_empty (&d->free_list))
    {
      int result=0;
      t=d;
      for (t = d; t < descs + desc_cnt; t++) {
        if (!list_empty(&t->free_list)) {
          result=1;
          break;
        }
      }

      lock_release (&d->lock);
  
      //The next larger block to be broken is t, we break it recursively till we aquire the block of requested size
      //modifying the descs array in the process

      //if the result is 0, means there is no block free of any size greater than the requested size, so we 
      //get a page and make it a block of the largest possible size
      if(result==0) {

        /* Allocate a page. */
        a = palloc_get_page (0);
        list_push_back(&arena_list , &a->free_elem);
        if (a == NULL) 
          {
            return NULL; 
          }

        /* Initialize arena and add its block to the free list. */
        
        t=descs+desc_cnt-1;
        lock_acquire (&t->lock);
        a->magic = ARENA_MAGIC;
        a->desc = descs;
        a->free_cnt = t->blocks_per_arena;
        struct block *b = arena_to_block (a, 0);
        b->size=t->block_size;
        list_push_back (&t->free_list, &b->free_elem);
        lock_release (&t->lock);
      }

      //Now, we are gauranteed to have found the bigger block to be broken recursively to 
      //get the block of requested size
      malloc_breakdown(t,d);
    }

    else lock_release(&d->lock);

  lock_acquire (&d->lock);
  /* Get a block from free list and return it. */
  b = list_entry (list_pop_front (&d->free_list), struct block, free_elem);
  a = block_to_arena (b);
  a->free_cnt--;
  lock_release (&d->lock);
  printMemory();
  return b+1;
}


/* Allocates and return A times B bytes initialized to zeroes.
   Returns a null pointer if memory is not available. */
void *
calloc (size_t a, size_t b) 
{
  void *p;
  size_t size;

  /* Calculate block size and make sure it fits in size_t. */
  size = a * b;
  if (size < a || size < b)
    return NULL;

  /* Allocate and zero memory. */
  p = malloc (size);
  if (p != NULL)
    memset (p, 0, size);

  return p;
}

/* Returns the number of bytes allocated for BLOCK. */
static size_t
block_size (void *block) 
{
  struct block *b = block;
  //struct arena *a = block_to_arena (b);
  //struct desc *d = a->desc;
  return b->size;
  //return d != NULL ? d->block_size : PGSIZE * a->free_cnt - pg_ofs (block);
}

/* Attempts to resize OLD_BLOCK to NEW_SIZE bytes, possibly
   moving it in the process.
   If successful, returns the new block; on failure, returns a
   null pointer.
   A call with null OLD_BLOCK is equivalent to malloc(NEW_SIZE).
   A call with zero NEW_SIZE is equivalent to free(OLD_BLOCK). */
void *
realloc (void *old_block, size_t new_size) 
{
  if (new_size == 0) 
    {
      free (old_block);
      return NULL;
    }
  else 
    {
      void *new_block = malloc (new_size);
      if (old_block != NULL && new_block != NULL)
        {
          size_t old_size = block_size (old_block);
          size_t min_size = new_size < old_size ? new_size : old_size;
          memcpy (new_block, old_block, min_size);
          free (old_block);
        }
      return new_block;
    }
}

/*free helper function to merge the child blocks into bigger blocks*/
int free_buildup(struct block *b, struct desc *d){
  if(d->block_size == (&descs[desc_cnt-1])->block_size ) return 1;

  struct block *b1,*b2;
  struct arena *a=NULL;
  struct list_elem *e,*f;
  int index=0;

  lock_acquire (&d->lock);

  for (e = list_begin (&d->free_list); e != list_end (&d->free_list); e = list_next (e)) {
    if ( (uint8_t *)e == (uint8_t *)&b->free_elem ) {
      break;
    }
  }

  a = block_to_arena (b);
  index = ( (uint8_t *)b - ((uint8_t *) a + sizeof *a) )/d->block_size;

  if ((uint8_t *)e != (uint8_t *)list_begin (&d->free_list) && index%2==1 ) {
    f=list_prev(e);
    if (f==list_head(&d->free_list)) {
      lock_release(&d->lock);
      return;
    }
    if ((uint8_t *)e  == (uint8_t *)f  + d->block_size ) {
      b1 = list_entry(f,struct block, free_elem);
      b2 = list_entry(e,struct block, free_elem);
      a = block_to_arena (b1);
      list_remove (&b1->free_elem);
      a->free_cnt--;

      a = block_to_arena (b2);
      list_remove (&b2->free_elem);
      a->free_cnt--;
      lock_release (&d->lock);


#ifndef NDEBUG
      /* Clear the block to help detect use-after-free bugs. */
      memset (b, 0xcc, d->block_size);
#endif

      b=b1;
      b->size = d->block_size*2;
      d++;

      lock_acquire (&d->lock);

      /* Add block to free list in ascending sorted order */
      for (e = list_begin (&d->free_list); e != list_end (&d->free_list); e = list_next (e))
      if ((uint8_t *)&b->free_elem < (uint8_t *)e)
        break;
      list_insert (e, &b->free_elem);
      a=block_to_arena(b);
      a->free_cnt++;
      lock_release (&d->lock);
      
      //Now we call the free helper function which merges or coalasces buddies together into the parent block
      free_buildup(b,d);
      return;
    }
  }

if ((uint8_t *)e != (uint8_t *)list_end (&d->free_list) && index%2==0 ) {
    f=list_next(e);
    if (f==list_tail(&d->free_list)) {
      lock_release(&d->lock);
      return;
    }
    if ((uint8_t *)f  == (uint8_t *)e  + d->block_size ) {
      b1 = list_entry(e,struct block, free_elem);
      b2 = list_entry(f,struct block, free_elem);
      a = block_to_arena (b1);
      list_remove (&b1->free_elem);
      a->free_cnt--;

      a = block_to_arena (b2);
      list_remove (&b2->free_elem);
      a->free_cnt--;
      lock_release (&d->lock);

#ifndef NDEBUG
      /* Clear the block to help detect use-after-free bugs. */
      memset (b, 0xcc, d->block_size);
#endif

      b=b1;
      b->size = d->block_size*2;
      d++;

      lock_acquire (&d->lock);

      /* Add block to free list in ascending sorted order */
      for (e = list_begin (&d->free_list); e != list_end (&d->free_list); e = list_next (e))
      if ((uint8_t *)&b->free_elem < (uint8_t *)e)
        break;
      list_insert (e, &b->free_elem);
      a=block_to_arena(b);
      a->free_cnt++;
      lock_release (&d->lock);
      
      //Now we call the free helper function which merges or coalasces buddies together into the parent block
      free_buildup(b,d);
      return;
    }
  }

lock_release (&d->lock);
return;

}


/* Frees block P, which must have been previously allocated with
   malloc(), calloc(), or realloc(). */
void
free (void *p) 
{
  if (p != NULL)
    {
      struct block *b = (struct block *)p;
      b-=1;
      struct arena *a = block_to_arena (b);
      struct desc *d = a->desc;
      size_t size, block_size;
      size=b->size;
      struct list_elem *e;
      int index=0;
      if((int)size < 0) {
        printf("corrupted Memory\n");
        size=(size_t) ((uint8_t)p);
      }
      /*
      printMemory();
      if ((int)size < 0){
        printf("gadbad\n");
        int sz;
        sz=(uint8_t)p;
        printf("%d\n",sz );
      }
      */
      for (block_size = 16; block_size < PGSIZE / 2; block_size *= 2) {
        if (block_size >= size) {
          break;
        }
        index++;
      }
      if (index==7) printf("corrupted memory\n");;
      if (d != NULL) 
        {
          /* It's a normal block.  We handle it here. */
          d = &descs[index];

// #ifndef NDEBUG
//        //Clear the block to help detect use-after-free bugs. 
//        memset (b, 0xcc, d->block_size);
// #endif
          lock_acquire (&d->lock);
          /* Add block to free list in ascending sorted order */
          for (e = list_begin (&d->free_list); e != list_end (&d->free_list); e = list_next (e))
          if ((uint8_t *)&b->free_elem < (uint8_t *)e)
            break;
          list_insert (e, &b->free_elem);
          a->free_cnt++;
          lock_release (&d->lock);

          //Now we call the free helper function which merges or coalasces buddies together into the parent block
          int res;
          res=free_buildup(b,d);

          d=descs+desc_cnt-1;
          lock_acquire (&d->lock);
          /* If the arena is now entirely unused, free it. */
          if (res==1 && a->free_cnt == d->blocks_per_arena) 
            {
              size_t i=0;

              ASSERT (a->free_cnt == d->blocks_per_arena);
            //  for (i = 0; i < d->blocks_per_arena; i++) 
            //   {
                  struct block *b = arena_to_block (a, i);
                  list_remove (&b->free_elem);
            //    }
              list_remove(&a->free_elem);  
              palloc_free_page (a);
            }

          lock_release (&d->lock);
        }
      else
        {
          /* It's a big block.  Free its pages. */
          palloc_free_multiple (a, a->free_cnt);
          return;
        }
    }
  printMemory();
}

/* Returns the arena that block B is inside. */
static struct arena *
block_to_arena (struct block *b)
{
  struct arena *a = pg_round_down (b);

  /* Check that the arena is valid. */
  ASSERT (a != NULL);
  ASSERT (a->magic == ARENA_MAGIC);

  /* Check that the block is properly aligned for the arena. */
  ASSERT (a->desc == NULL
          || (pg_ofs (b) - sizeof *a) % a->desc->block_size == 0);
  ASSERT (a->desc != NULL || pg_ofs (b) == sizeof *a);

  return a;
}

/* Returns the (IDX - 1)'th block within arena A. */
static struct block *
arena_to_block (struct arena *a, size_t idx) 
{
  ASSERT (a != NULL);
  ASSERT (a->magic == ARENA_MAGIC);
  ASSERT (idx < a->desc->blocks_per_arena);
  return (struct block *) ((uint8_t *) a
                           + sizeof *a
                           + idx * a->desc->block_size);
}
