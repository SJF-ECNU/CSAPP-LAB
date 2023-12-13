#include "cachelab.h"
#include<stdlib.h>
#include<stdbool.h>
#include <stdio.h>
#include <assert.h>
#include <getopt.h>
#include <stdint.h>
#include <string.h>

int hit_count,evict_count,miss_count,time;

//define a line,contain is valid ,tag and timestamp
typedef struct line
{
    int isValid;
    int tag;
    int timestamp;
}line;
typedef line* set;
// define an operation
// contain type(what is opr it is)
// size, and target address
typedef struct operation
{
    char type;
    int size;
    unsigned long address;
}operation;

//define cache
//blockSize means the num of the bits of block
typedef struct cache
{
    int lineNum;
    int setNum;
    int blockSize;
    int setSize;
    set *head;
} cache;

//get set from address
static inline unsigned getSet(unsigned long address,int S,int B)
{
   return (address >> B) % (1<<S); 
}

//get tag from address
static inline unsigned getTag(unsigned long address, int S,int B)
{
    return address >> (S+B);
}

//allocate for cache
cache allocateSet(int S, int B, int E)
{
    int nsets=1<<S;
    set*newSet=calloc(nsets,sizeof(cache*));
    for (int i=0;i<nsets;i++)
    {
        newSet[i]=calloc(E,sizeof(line));
    }
    cache newCache={E,nsets,B,S,newSet};
    return newCache;
}

//free cache
void freeSet(cache*c)
{
    for(int i=0;i<c->setNum;i++)
    {
        free(c->head[i]);
        c->head[i] = NULL;
    }
    free(c->head);
    c->head = NULL;
}

//get a operation from trace file
operation readOneOperation(FILE* pf)
{
    char temp[128];
    operation newOper={0,0,0};
    if(!fgets(temp,128,pf))
        return newOper;
    sscanf(temp, "\n%c %lx,%d", &newOper.type, &newOper.address, &newOper.size);
    return newOper;
}

//get arguments from terminal
void getArguments(int argc,char* argv[],int* pS,int* pE,int* pB,char**path,bool* pIsDisplay)
{
    int op;
    while((op=getopt(argc,argv,"hvs:E:b:t:"))!=-1)
    {
    switch(op){
        case 'h':
            printf("h-help\nv-ifShow\neg.:./csim -v -s 1 -E 1 -b 1 -t traces/dave.trace");
            exit(0);
        case 'v':
            *(pIsDisplay) = true;
            break;
        case 's':
            *(pS) = atoi(optarg);
            break;
        case 'E':
            *(pE)=atoi(optarg);
            break;
        case 'b':
            *(pB)=atoi(optarg);
            break;
        case 't':
            strcpy(*path, optarg);
            break;
        default:
            break;
            }
    }
}

//get the line with right tag
line* findMatchLine(set s,int E,int tag)
{
    for (int i = 0; i < E;i++)
    {
        if(s[i].tag==tag&&s[i].isValid)
            return s + i;
    }
    return NULL;
}

//get an available line
line* findNewLine(set s,int E)
{
    for (int i = 0; i < E;i++)
    {
        if(!(s[i].isValid))
            return s + i;
    }
    return NULL;
}

//evict the oldest line
line* findEvictLine(set s,int E)
{
    int index = 0;
    for (int i = 1; i < E;i++)
    {
        if(s[i].timestamp<s[index].timestamp)
            index = i;
    }
    return s + index;
}

//try to hit and print hit or miss
void loadOrStore(cache* c,operation*opr,bool isDisplay,bool isCalledByMain)
{
    int s = getSet(opr->address, c->setSize, c->blockSize);
    int t = getTag(opr->address, c->setSize, c->blockSize);
    if (isDisplay)
        printf("%c %lx,%d ", opr->type, opr->address, opr->size);
    line *appropriate_line = findMatchLine(c->head[s], c->lineNum, t);
    if(appropriate_line)
    {
        hit_count++;
        if(isDisplay&&isCalledByMain)
            printf("hit ");
    }
    else{
        miss_count++;
        if(isDisplay&&isCalledByMain)
            printf("miss ");
        appropriate_line = findNewLine(c->head[s], c->lineNum);
        if(appropriate_line)
            appropriate_line->isValid = true;
        else{
            evict_count++;
            appropriate_line = findEvictLine(c->head[s], c->lineNum);
            if (isDisplay&&isCalledByMain)
                printf("eviction ");
        }
        appropriate_line->tag = t;
    }
    appropriate_line->timestamp = time;
    time++;
    if(isDisplay&&isCalledByMain)
        printf("\n");
}

//modify
void modify(cache* c,operation* opr,bool isDisplay)
{
    if (isDisplay)
        printf("%c %lx,%d ", opr->type, opr->address, opr->size);
    opr->type = 'L';
    loadOrStore(c, opr, isDisplay,false);
    opr->type = 'S';
    loadOrStore(c, opr, isDisplay,false);
    if (isDisplay)
        printf("\n");
}


int main(int argc,char* argv[])
{
    hit_count = 0, miss_count = 0, evict_count = 0, time = 0;
    char *path = malloc(sizeof(char) * 128);
    bool isDisplay = false;
    int S=0, E=0, B=0;
    getArguments(argc, argv, &S, &E, &B, &path, &isDisplay);
    cache c = allocateSet(S, B, E);
    FILE *pfile = fopen(path, "r");
    if(pfile)
        printf("success");
    operation opr;
    while((opr=readOneOperation(pfile)).type)
    {
        if(opr.type=='M')
            modify(&c, &opr, isDisplay);
        else if(opr.type=='L'||opr.type=='S')
            loadOrStore(&c, &opr, isDisplay, true);
        else if(opr.type=='I')
            continue;
    }
    fclose(pfile);
    freeSet(&c);
    free(path);
    printSummary(hit_count, miss_count, evict_count);
    return 0;
}
