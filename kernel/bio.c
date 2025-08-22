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

#define NBUCKET 13
#define HASH(dev, blockno) (((dev) << 27 | (blockno)) % NBUCKET)

struct {
  struct buf buf[NBUF];

  struct buf bufmap[NBUCKET]; // 哈希桶
  struct spinlock bucketlocks[NBUCKET]; // 每个桶一把锁
  struct spinlock evictionlocks[NBUCKET]; // 每个桶一把驱逐锁
} bcache;

void
binit(void)
{
  for (int i = 0; i < NBUCKET; i++) {
    initlock(&bcache.bucketlocks[i], "bcache_bucket_lock");
    initlock(&bcache.evictionlocks[i], "bcache_eviction_lock");
    bcache.bufmap[i].next = 0;
  }

  for (int i = 0; i < NBUF; i++) {
    struct buf *b = &bcache.buf[i];
    initsleeplock(&b->lock, "buffer");
    b->lastuse = 0;
    b->refcnt = 0;
    // b接到0号bucket上
    b->next = bcache.bufmap[0].next;
    // 挂上新的b
    bcache.bufmap[0].next = b;
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{

  struct buf *b;

  uint key = HASH(dev, blockno);

  // 拿到key对应的桶锁
  acquire(&bcache.bucketlocks[key]);

  // 缓存命中
  for (b = bcache.bufmap[key].next; b; b = b->next) {
    if (b->dev == dev && b->blockno == blockno) {
      b->refcnt++;
      // 释放key桶锁
      release(&bcache.bucketlocks[key]);
      acquiresleep(&b->lock);
      return b;
    }
  }
  // 缓存未命中

  // 先释放key桶锁 避免死锁
  release(&bcache.bucketlocks[key]);

  // 拿到桶的驱逐锁串行化 避免对同一个块进行多次缓存驱逐及重分配
  acquire(&bcache.evictionlocks[key]);
  // 再次检查 是避免第二个进程拿到锁后进行缓存驱逐及重分配
  for (b = bcache.bufmap[key].next; b; b = b->next) {
    if (b->dev == dev && b->blockno == blockno) {
      acquire(&bcache.bucketlocks[key]);
      b->refcnt++;
      release(&bcache.bucketlocks[key]);
      release(&bcache.evictionlocks[key]);
      acquiresleep(&b->lock);
      return b;
    }
  }
  // 没有缓存命中执行的操作
  struct buf *beforeleast = 0;
  uint holdingbucket = -1;
  for (int i = 0; i < NBUCKET; i++) {
    // 拿到当前桶锁
    acquire(&bcache.bucketlocks[i]);
    int newfound = 0;
    for (b = &bcache.bufmap[i]; b->next; b = b->next) {
      // 遍历当前桶的buffer
      // 没有文件引用的空闲块 找lastuse最小的
      if (b->next->refcnt == 0 && (!beforeleast || b->next->lastuse < beforeleast->lastuse)) {
        beforeleast = b;
        newfound = 1;
      }
    }
    if (!newfound) {
      // 没找到 释放当前桶锁
      release(&bcache.bucketlocks[i]);
    }else {
      // 新桶找到了就更新 释放旧桶的锁
      if (holdingbucket != -1) release(&bcache.bucketlocks[holdingbucket]);
      holdingbucket = i;
    }
  }
  // 遍历所有桶都没有找到
  if (!beforeleast) {
    panic("bget: no buffers");
  }
  // 找的对应buffer
  b = beforeleast->next;
  // 驱逐的buffer和新加的buffer不是一个桶
  if (holdingbucket != key) {
    // 移除驱逐桶的buffer
    beforeleast->next = b->next;
    release(&bcache.bucketlocks[holdingbucket]);
    // 重新拿到key桶的锁
    acquire(&bcache.bucketlocks[key]);
    // 挂到key桶上
    b->next = bcache.bufmap[key].next;
    bcache.bufmap[key].next = b;
  }
  // 是一个桶就不需要链表操作
  b->dev = dev;
  b->blockno = blockno;
  b->refcnt = 1;
  b->valid = 0;
  release(&bcache.bucketlocks[key]);
  release(&bcache.evictionlocks[key]);
  acquiresleep(&b->lock);
  return b;

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

  uint key = HASH(b->dev, b->blockno);
  acquire(&bcache.bucketlocks[key]);
  b->refcnt--;
  if (b->refcnt == 0) {
    // 更新时间戳
    b->lastuse = ticks;
  }
  release(&bcache.bucketlocks[key]);
}

void
bpin(struct buf *b) {
  uint key = HASH(b->dev, b->blockno);
  acquire(&bcache.bucketlocks[key]);
  b->refcnt++;
  release(&bcache.bucketlocks[key]);
}

void
bunpin(struct buf *b) {
  uint key = HASH(b->dev, b->blockno);
  acquire(&bcache.bucketlocks[key]);
  b->refcnt--;
  release(&bcache.bucketlocks[key]);
}


