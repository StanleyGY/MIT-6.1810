// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

#ifdef LAB_LOCK

// Hashtable of size of a prime is less prone to having conflicts
#define NBUCKET 13

struct bucket {
  struct spinlock lock;
  struct buf *head;
  int avail;
};

// Use similar idea from question 1. All buckets will initially have 2-3 bufs,
// and if not enough, steal more bufs from other buckets. Hash-table with linear
// probing is not available.
struct {
  struct spinlock lock;
  struct bucket buckets[NBUCKET];
  struct buf buf[NBUF];
} bcache;

static void
link(struct buf *s, struct buf *t)
{
  if (s)
    s->next = t;
  if (t)
    t->prev = s;
}

#else

struct {
  struct spinlock lock;
  struct buf buf[NBUF];

  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
  struct buf head;
} bcache;

#endif

void
binit(void)
{
  #ifdef LAB_LOCK

  initlock(&bcache.lock, "bcache");

  for (int i = 0; i < NBUF; i++) {
    initsleeplock(&bcache.buf[i].lock, "buffer");
    if (i < NBUF - 1)
      link(&bcache.buf[i], &bcache.buf[i + 1]);
    else
      link(&bcache.buf[i], 0);
  }

  for (int i = 0; i < NBUCKET; i++) {
    initlock(&bcache.buckets[i].lock, "bcache");

    bcache.buckets[i].head = &bcache.buf[i * 2];
    link(0, &bcache.buf[i * 2]);

    if (i < NBUCKET - 1) {
      link(&bcache.buf[i * 2 + 1], 0);
      bcache.buckets[i].avail = 2;
    } else {
      bcache.buckets[i].avail = 6;
    }
  }

  #else
  struct buf *b;

  initlock(&bcache.lock, "bcache");

  // Create linked list of buffers
  bcache.head.prev = &bcache.head;
  bcache.head.next = &bcache.head;
  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    b->next = bcache.head.next;
    b->prev = &bcache.head;
    initsleeplock(&b->lock, "buffer");
    bcache.head.next->prev = b;
    bcache.head.next = b;
  }

  #endif
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;

  #ifdef LAB_LOCK

  const int bucketid = (dev + blockno) % NBUCKET;
  struct bucket *bkt = &bcache.buckets[bucketid];

  // Check if it's cached
  acquire(&bkt->lock);

  b = bkt->head;
  while (b) {
    if (b->dev == dev && b->blockno == blockno) {
      b->refcnt++;
      release(&bkt->lock);
      acquiresleep(&b->lock);
      return b;
    }
    b = b->next;
  }

  // Not cached. Need to find an empty buffer
  struct bucket *nbkt;
  for (int i = 0; i < NBUCKET; i++) {
    nbkt = &bcache.buckets[(bucketid + i) % NBUCKET];
    if (nbkt != bkt)
      acquire(&nbkt->lock);

    if (nbkt->avail > 0) {
      b = nbkt->head;
      while (b) {
        if (b->refcnt == 0) {
          // Remove from a different bucket
          nbkt->avail--;
          if (b == nbkt->head)
            nbkt->head = b->next;
          link(b->prev, b->next);

          // Put it into the current bucket
          bkt->avail++;
          link(b, bkt->head);
          link(0, b);
          bkt->head = b;

          if (nbkt != bkt)
            release(&nbkt->lock);

          break;
        }
        b = b->next;
      }

      if(b) {
        bkt->avail--;
        b->dev = dev;
        b->blockno = blockno;
        b->valid = 0;
        b->refcnt = 1;
        release(&bkt->lock);
        acquiresleep(&b->lock);
        return b;
      }
    }

    if (nbkt != bkt)
      release(&nbkt->lock);
  }
  release(&bkt->lock);

  #else

  acquire(&bcache.lock);

  // Is the block already cached?
  for(b = bcache.head.next; b != &bcache.head; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // Not cached.
  // Recycle the least recently used (LRU) unused buffer.
  for(b = bcache.head.prev; b != &bcache.head; b = b->prev){
    if(b->refcnt == 0) {
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;
      release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;
    }
  }

  #endif

  panic("bget: no buffers");
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  #ifdef LAB_LOCK

  const int hashid = (b->dev + b->blockno) % NBUCKET;
  struct bucket *bkt = &bcache.buckets[hashid];

  acquire(&bkt->lock);
  b->refcnt --;
  if (b->refcnt == 0)
    bkt->avail ++;
  release(&bkt->lock);

  #else

  acquire(&bcache.lock);
  b->refcnt--;
  if (b->refcnt == 0) {
    // no one is waiting for it.
    b->next->prev = b->prev;
    b->prev->next = b->next;
    b->next = bcache.head.next;
    b->prev = &bcache.head;
    bcache.head.next->prev = b;
    bcache.head.next = b;
  }

  release(&bcache.lock);

  #endif
}

void
bpin(struct buf *b) {
  #ifdef LAB_LOCK
  const int hashid = (b->dev + b->blockno) % NBUCKET;
  struct bucket *bkt = &bcache.buckets[hashid];

  acquire(&bkt->lock);
  b->refcnt++;
  release(&bkt->lock);

  #else
  acquire(&bcache.lock);
  b->refcnt++;
  release(&bcache.lock);
  #endif
}

void
bunpin(struct buf *b) {
  #ifdef LAB_LOCK
  const int hashid = (b->dev + b->blockno) % NBUCKET;
  struct bucket *bkt = &bcache.buckets[hashid];

  acquire(&bkt->lock);
  b->refcnt--;
  release(&bkt->lock);
  #else
  acquire(&bcache.lock);
  b->refcnt--;
  release(&bcache.lock);
  #endif
}


