// 导入所需的头文件
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"

// 定义了一个结构体，用于存储团队信息
team_t team = {
    /* Team name */
    "SJF",
    /* First member's full name */
    "SJF",
    /* First member's email address */
    "10225501403@stu.ecnu.edu.cn",
    /* Second member's full name (leave blank if none) */
    "",
    /* Second member's email address (leave blank if none) */
    ""};

// 定义了一些宏，用于内存管理
#define ALIGNMENT 8                                                          // 内存对齐
#define ALIGN(size) (((size) + (ALIGNMENT - 1)) & ~0x7)                      // 对齐大小
#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))                                  // size_t的对齐大小
#define WSIZE 4                                                              // 头部/脚部的大小
#define DSIZE 8                                                              // 双字
#define CHUNKSIZE (1 << 8)                                                   // 扩展堆时的默认大小
#define MAX(x, y) ((x) > (y) ? (x) : (y))                                    // 最大值
#define MIN(x, y) ((x) > (y) ? (y) : (x))                                    // 最小值
#define PACK(size, alloc) ((size) | (alloc))                                 // 将size和alloc打包
#define GET(p) (*(unsigned int *)(p))                                        // 从指针p读取
#define PUT(p, val) ((*(unsigned int *)(p)) = (val))                         // 写入val到指针p
#define GET_SIZE(p) (GET(p) & ~0x7)                                          // 从指针p读取有效载荷大小
#define GET_ALLOC(p) (GET(p) & 0x1)                                          // 从指针p读取分配位
#define GET_HEAD(num) ((unsigned int *)(long)(GET(heap_list + WSIZE * num))) // 给定链表序号，返回链表头指针
#define GET_PRE(bp) ((unsigned int *)(long)(GET(bp)))                        // 给定有效载荷，返回前驱的有效载荷指针
#define GET_SUC(bp) ((unsigned int *)(long)(GET((unsigned int *)bp + 1)))    // 给定有效载荷，返回后继的有效载荷指针
#define GET_PTR(p) ((unsigned int *)(long)(GET(p)))                          // 读地址存的指针
#define HDRP(bp) ((char *)(bp)-WSIZE)                                        // 根据有效载荷指针获得该块的头标签
#define FTRP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)                 // 根据有效载荷指针获得该块的尾标签
#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(((char *)(bp)-WSIZE)))        // 根据有效载荷指针获得前一块的有效载荷指针
#define PREV_BLKP(bp) ((char *)(bp)-GET_SIZE(((char *)(bp)-DSIZE)))          // 根据有效载荷指针获得后一块的有效载荷指针
#define CLASS_SIZE 16                                                        // 链表数目

static char *heap_list; // 堆顶，指向序言块的第二块

// 声明一些函数，用于内存管理
static void *extend_heap(size_t words);    // 扩展堆
static void *coalesce(void *bp);           // 合并空闲块
static void *find_fit(size_t asize);       // 找到匹配的块
static void place(void *bp, size_t asize); // 分割空闲块
static void delete(void *bp);              // 从相应链表中删除块
static void insert(void *bp);              // 在对应链表中插入块
static int search(size_t size);            // 根据块大小, 找到头节点位置

// 初始化内存管理器
int mm_init(void)
{
    // 申请20*4个字的空间，第一个字存放起始块，第二到第十七存放20个链表的链表头，第十八和十九个块放序言块，第二十个字放结尾块
    if ((heap_list = mem_sbrk((4 + CLASS_SIZE) * WSIZE)) == (void *)-1)
        return -1;
    // 初始化16个大小类头指针
    for (int i = 0; i < CLASS_SIZE; i++)
    {
        PUT(heap_list + i * WSIZE, NULL);
    }
    // 序言块和结尾块均设置为已分配, 方便考虑边界情况
    PUT(heap_list + ((1 + CLASS_SIZE) * WSIZE), PACK(DSIZE, 1)); /* 填充序言块 */
    PUT(heap_list + ((2 + CLASS_SIZE) * WSIZE), PACK(DSIZE, 1)); /* 填充序言块 */
    PUT(heap_list + ((3 + CLASS_SIZE) * WSIZE), PACK(0, 1));     /* 结尾块 */
    // 扩展空闲空间
    if (extend_heap(CHUNKSIZE / WSIZE) == NULL)
        return -1;
    return 0;
}

// 根据传入的字节数拓展堆
void *extend_heap(size_t words)
{
    char *bp;
    size_t size;
    // 将传入的szie双字对齐
    size = (words % 2) ? (words + 1) * WSIZE : words * WSIZE;
    // 分配堆空间
    if ((long)(bp = mem_sbrk(size)) == -1)
        return NULL;
    // 封装头和尾
    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));
    size = GET_SIZE(HDRP(bp));
    // 封装结尾块
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1));
    // 合并前空闲块
    if (!GET_ALLOC(FTRP(PREV_BLKP(bp))))
    {
        delete (PREV_BLKP(bp));
        size += GET_SIZE(HDRP(PREV_BLKP(bp)));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));
        bp = PREV_BLKP(bp);
    }
    insert(bp);
    return bp;
}

// 合并空闲块
void *coalesce(void *bp)
{
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp)));
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    size_t size = GET_SIZE(HDRP(bp)); /* 当前块大小 */
    // 四种情况：前后都不空, 前不空后空, 前空后不空, 前后都空
    // 前不空后空
    if (prev_alloc && !next_alloc)
    {
        // 将后面的块从其链表中删除
        delete (NEXT_BLKP(bp));
        size += GET_SIZE(HDRP(NEXT_BLKP(bp))); // 增加当前块大小
        PUT(HDRP(bp), PACK(size, 0));          // 先修改头
        PUT(FTRP(bp), PACK(size, 0));          // 根据头部中的大小来定位尾部
    }
    // 前空后不空
    else if (!prev_alloc && next_alloc)
    {
        // 将其前面的快从链表中删除
        delete (PREV_BLKP(bp));
        size += GET_SIZE(HDRP(PREV_BLKP(bp))); // 增加当前块大小
        PUT(FTRP(bp), PACK(size, 0));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp); // 注意指针要变
    }
    // 都空
    else if (!prev_alloc && !next_alloc)
    {
        // 将前后两个块都从其链表中删除
        delete (NEXT_BLKP(bp));
        delete (PREV_BLKP(bp));
        size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(FTRP(NEXT_BLKP(bp))); // 增加当前块大小
        PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
    }
    insert(bp);
    return bp;
}

// 插入块, 每个链表按照块大小从小到大排列
void insert(void *bp)
{
    size_t size = GET_SIZE(HDRP(bp));
    int num = search(size);
    unsigned int *current = GET_HEAD(num);
    unsigned int *prev = NULL;
    while (current != NULL && GET_SIZE(HDRP(current)) < size)
    {
        prev = current;
        current = GET_SUC(current);
    }
    if (prev != NULL)
    {
        PUT(prev + 1, bp);
        PUT(bp, prev);
    }
    else
    {
        PUT(heap_list + WSIZE * num, bp);
        PUT(bp, NULL);
    }
    if (current != NULL)
    {
        PUT(current, bp);
        PUT((unsigned int *)bp + 1, current);
    }
    else
    {
        PUT((unsigned int *)bp + 1, NULL);
    }
}

// 删除块,清理指针
void delete(void *bp)
{
    // 块大小
    size_t size = GET_SIZE(HDRP(bp));
    // 根据块大小找到头节点位置
    int num = search(size);
    // 唯一节点,后继为null,前驱为null
    // 头节点设为null
    if (GET_PRE(bp) == NULL && GET_SUC(bp) == NULL)
    {
        PUT(heap_list + WSIZE * num, NULL);
    }
    /*
     * 最后一个节点
     * 前驱的后继设为null
     */
    else if (GET_PRE(bp) != NULL && GET_SUC(bp) == NULL)
    {
        PUT(GET_PRE(bp) + 1, NULL);
    }
    /*
     * 第一个结点
     * 头节点设为bp的后继
     */
    else if (GET_SUC(bp) != NULL && GET_PRE(bp) == NULL)
    {
        PUT(heap_list + WSIZE * num, GET_SUC(bp));
        PUT(GET_SUC(bp), NULL);
    }
    /*
     * 中间结点
     * 前驱的后继设为后继
     * 后继的前驱设为前驱
     */
    else if (GET_SUC(bp) != NULL && GET_PRE(bp) != NULL)
    {
        PUT(GET_PRE(bp) + 1, GET_SUC(bp));
        PUT(GET_SUC(bp), GET_PRE(bp));
    }
}
// 搜索函数，用于确定空闲块的大小类
int search(size_t v)
{
    // 通过位移操作确定v的大小类
    size_t r, shift;
    r = (v > 0xFFFF) << 4;
    v >>= r;
    shift = (v > 0xFF) << 3;
    v >>= shift;
    r |= shift;
    shift = (v > 0xF) << 2;
    v >>= shift;
    r |= shift;
    shift = (v > 0x3) << 1;
    v >>= shift;
    r |= shift;
    r |= (v >> 1);
    // 从 2^4 开始 (空闲块最小 16 bytes)
    int x = (int)r - 4;
    if (x < 0)
        x = 0;
    if (x >= CLASS_SIZE)
        x = CLASS_SIZE - 1;
    return x;
}

// 内存分配函数，根据请求的大小分配内存
void *mm_malloc(size_t size)
{
    size_t asize;
    size_t extendsize;
    char *bp;
    if (size == 0)
        return NULL;
    if (size <= DSIZE)
        asize = 2 * DSIZE;
    else
        asize = DSIZE * ((size + (DSIZE) + (DSIZE - 1)) / DSIZE);
    // 寻找合适的空闲块
    if ((bp = find_fit(asize)) != NULL)
    {
        place(bp, asize);
        if (!GET_ALLOC(HDRP(bp)))
            return NEXT_BLKP(bp);
        return bp;
    }
    // 找不到则扩展堆
    extendsize = asize;
    if ((bp = extend_heap(extendsize / WSIZE)) == NULL)
        return NULL;
    place(bp, asize);
    if (!GET_ALLOC(HDRP(bp)))
        return NEXT_BLKP(bp);
    // if(checkBlock(bp))
    return bp;
    // return NULL;
}

void *find_fit(size_t asize)
{
    int num = search(asize);
    unsigned int *bp;
    // 如果找不到合适的块，那么就搜索下一个更大的大小类
    while (num < CLASS_SIZE)
    {
        bp = GET_HEAD(num);
        // 不为空则寻找
        while (bp)
        {
            size_t size = GET_SIZE(HDRP(bp));
            if (size >= asize)
            {
                return bp;
            }
            // 用后继找下一块
            bp = GET_SUC(bp);
        }
        // 找不到则进入下一个大小类
        num++;
    }
    return NULL;
}

// 分离空闲块
void place(void *bp, size_t asize)
{
    size_t csize = GET_SIZE(HDRP(bp));
    size_t rm_size = csize - asize;
    // 块已分配，从空闲链表中删除
    delete (bp);
    if ((rm_size) >= 32 * DSIZE)
    {
        PUT(HDRP(bp), PACK(asize, 1));
        PUT(FTRP(bp), PACK(asize, 1));
        // bp指向空闲块
        bp = NEXT_BLKP(bp);
        PUT(HDRP(bp), PACK(rm_size, 0));
        PUT(FTRP(bp), PACK(rm_size, 0));
        // 加入分离出来的空闲块
        insert(bp);
    }
    // 设置为填充
    else
    {
        PUT(HDRP(bp), PACK(csize, 1));
        PUT(FTRP(bp), PACK(csize, 1));
    }
}

// 释放内存块
void mm_free(void *ptr)
{
    if (ptr == 0)
        return;
    size_t size = GET_SIZE(HDRP(ptr));

    PUT(HDRP(ptr), PACK(size, 0));
    PUT(FTRP(ptr), PACK(size, 0));
    coalesce(ptr);
}

// 重新分配内存块
void *mm_realloc(void *ptr, size_t size)
{
    printBlock(ptr);
    if (ptr == NULL)
        return mm_malloc(size);
    else if (size == 0)
    {
        mm_free(ptr);
        return NULL;
    }
    else if (size == GET_SIZE(HDRP(ptr)))
        return ptr;
    int prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(ptr)));
    int next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(ptr)));
    size_t oldsize = GET_SIZE(HDRP(ptr));
    size_t newsize = size;
    if (newsize <= DSIZE)
    {
        newsize = 2 * DSIZE;
    }
    else
    {
        newsize = DSIZE * ((size + (DSIZE) + (DSIZE - 1)) / DSIZE);
    }
    size_t nextsize = GET_SIZE(HDRP(NEXT_BLKP(ptr)));
    size_t prevsize = GET_SIZE(HDRP(PREV_BLKP(ptr)));
    if (!next_alloc & (oldsize + nextsize) >= newsize)
    {
        delete (NEXT_BLKP(ptr));
        PUT(HDRP(ptr), PACK(oldsize + nextsize, 1));
        PUT(FTRP(ptr), PACK(oldsize + nextsize, 1));
        place(ptr, newsize);
        return ptr;
    }
    else if (!nextsize && newsize >= oldsize)
    {
        size_t extend_size = newsize - oldsize;
        if ((long)(mem_sbrk(extend_size)) == -1)
            return NULL;
        PUT(HDRP(ptr), PACK(newsize, 1));
        PUT(FTRP(ptr), PACK(newsize, 1));
        PUT(HDRP(NEXT_BLKP(ptr)), PACK(0, 1));
        return ptr;
    }
    else if (GET_SIZE(NEXT_BLKP(NEXT_BLKP(ptr))) == 0 && !next_alloc)
    {
        int extend_size = newsize - nextsize - oldsize;
        delete (NEXT_BLKP(ptr));
        if ((long)(mem_sbrk(extend_size)) == -1)
            return NULL;
        PUT(HDRP(ptr), PACK(newsize, 1));
        PUT(FTRP(ptr), PACK(newsize, 1));
        PUT(HDRP(NEXT_BLKP(ptr)), PACK(0, 1));
        return ptr;
    }
    else
    { // 直接分配
        void *newptr = mm_malloc(newsize);
        if (newptr == NULL)
            return 0;
        memcpy(newptr, ptr, MIN(oldsize, newsize));
        mm_free(ptr);
        return newptr;
    }
}

int checkBlock(void *bp)
{
    if ((size_t)bp % 8)
    {
        //printf("Error: %p is not doubleword aligned\n", bp);
        return 0;
    }
    if (GET(HDRP(bp)) != GET(FTRP(bp)))
    {
        //printf("In block %p   Error: header does not match footer\n", bp);
        return 0;
    }
    else
    {
        //printf("In block %p   Header matches footer\n", bp);
        return 1;
    }
}

void checkFreeList()
{
    int i;
    void *bp;
    void *head;
    void *next;
    for (i = 0; i < CLASS_SIZE; i++)
    {
        head = GET_HEAD(i);
        if (head == NULL)
            continue;
        printf("Free list %d\n", i);
        for (bp = head; bp != NULL; bp = next)
        {
            if(checkBlock(bp))
                next = GET_SUC(bp);
            else
                return 0;
        }
        return 1;
    }
}

void checkHeap()
{
    void *bp;
    for (bp = heap_list; GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp))
    {
        if(checkBlock(bp))
            continue;
        else
            return 0;
    }
    return 1;
}

void printBlock(void *bp)
{
    size_t hsize, halloc, fsize, falloc;
    //checkBlock(bp);
    hsize = GET_SIZE(HDRP(bp));
    halloc = GET_ALLOC(HDRP(bp));
    fsize = GET_SIZE(FTRP(bp));
    falloc = GET_ALLOC(FTRP(bp));
    if (hsize == 0)
    {
        printf("%p: EOL\n", bp);
        return;
    }
    printf("%p: header: [%zu:%c] footer: [%zu:%c]\n", bp,
           hsize, (halloc ? 'a' : 'f'),
           fsize, (falloc ? 'a' : 'f'));
}

void printFreeList()
{
    int i;
    void *bp;
    void *head;
    void *next;
    for (i = 0; i < CLASS_SIZE; i++)
    {
        head = GET_HEAD(i);
        if (head == NULL)
            continue;
        printf("Free list %d\n", i);
        for (bp = head; bp != NULL; bp = next)
        {
            printBlock(bp);
            next = GET_SUC(bp);
        }
    }
}

