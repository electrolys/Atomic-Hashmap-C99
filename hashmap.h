

#ifndef HSHM_H
#define HSHM_H
#include "hashmapconfig.h"
typedef struct hshm_bucket{
    struct hshm_bucket* next;
    void* val;
    char key[64-sizeof(void*)*2];
} hshm_bucket;

typedef struct hshm_link {
    struct hshm_link* next;
    void* val;
} hshm_link;

typedef struct {
    hshm_bucket** buckets;
    int numbuckets;
    int keysize;

    hshm_link* freebuf;

    HSHM_LOCK_T* locks;
    int numlocks;

    unsigned (*hash)(void*);
} hshm_map;

//Make a new hashmap
hshm_map* hshm_newmap(int numbuckets,int keysize,int threadlocknum, unsigned (*hash)(void*));
//Free the hashmap
void hshm_freemap(hshm_map* map);

//Get a pointer from the map.
void* hshm_get(hshm_map* map,void* key);
//Place or replace a pointer into the map. returns previous pointer if exists, else NULL
void* hshm_put(hshm_map* map,void* key,void* val);
//Remove and return a pointer from the map if it exists, else NULL
void* hshm_del(hshm_map* map,void* key);

//Iterate through all the buckets in the hashmap
#define hshm_iter(map) for (int i = 0 ; i < map->numbuckets ; i++) for (hshm_bucket* n = map->buckets[i] ; n ; n=n->next)

//Place a pointer into the map, freeing the previous pointer if neccessary
void hshm_putf(hshm_map* map,void* key,void* val);
//Remove and free a pointer from the map if exists
void hshm_delf(hshm_map* map,void* key);

//Any time you write/read the hashmap put these function calls around it with a threadindex unique to each thread
//Also make sure a pointer gotten from the hashmap isn't used after endwork
void hshm_startwork(hshm_map* map,int threadindex);
void hshm_endwork(hshm_map* map,int threadindex);

//Free a pointer atomically by adding it to freebuf which will be cleared bu clearfree
void hshm_free(hshm_map* map,void* ptr);

//Call this repeatedly outside of any startwork/endwork blocks to free the freebuf
//This makes sure that everything freed by it isn't used currently
void hshm_clearfree(hshm_map* map);

//Put a pointer in the atomic linked list (if the list is NULL it will be regarded as empty)
void hshm_listput(hshm_link** list, void *val);
//Free the whole list and set it to NULL
void hshm_listfree(hshm_link** list);
//Copy the whole list and set it to NULL
hshm_link* hshm_listpull(hshm_link** list);


#ifdef HSHM_IMPL

#include <stdlib.h>
#include <stddef.h>

void hshm_listfree(hshm_link** list){
    for (hshm_link* l = *list;l;){
        hshm_link* t = l;
        l = t->next;
        if (t->val) free(t->val);
        free(t);
    }
    *list = NULL;
}

hshm_link* new_link(void* val,hshm_link* next){
    hshm_link* l = malloc(sizeof(*l));
    l->val = val;
    l->next = next;
    return l;
}

void hshm_listput(hshm_link** list, void *val){
    hshm_link* l = new_link(val,NULL);
    hshm_link* s;
retry:
    s = *list;
    l->next = s;
    if (HSHM_CASPTR(list,s,l))return;
    goto retry;
}

hshm_link* hshm_listpull(hshm_link** list){
    hshm_link* s;
retry:
    s = *list;
    if (HSHM_CASPTR(list,s,NULL))return s;
    goto retry;
}

void free_keylist(hshm_bucket** start){ // not thread safe
    for (hshm_bucket* p = *start;p;){
        hshm_bucket* t = p;
        p = t->next;
        free(t);
    }
    *start = NULL;
}


hshm_bucket* new_bucket(void* key,void* val,hshm_bucket* next,int keysize){
    hshm_bucket* b = malloc(sizeof(*b));
    b->val = val;
    b->next = next;
    memcpy(b->key,key,keysize);

    return b;
}

hshm_bucket* keylist_get(hshm_bucket* start,void* key,int keysize){
    hshm_bucket* b;
    for (b = start;b;b=b->next)
        if (!memcmp(key,b->key,keysize))
            break;
    return b?b->val:NULL;
}

void* keylist_put(hshm_bucket** start,void* key,void* val,int keysize){//v must only be held by this thread before this point
    hshm_bucket* t = new_bucket(key,val,NULL,keysize);
    hshm_bucket* prev;
retry:
    prev = *start;
    if (!prev){
        if (HSHM_CASPTR(start,NULL,t))return NULL;
        goto retry;
    }
    if (!memcmp(key,prev->key,keysize)){
        free(t);
        return HSHM_SWAPPTR(&prev->val,val);
    }
    for (hshm_bucket* node = prev->next;node;node=node->next){
        if (!memcmp(key,node->key,keysize)){
            free(t);
            return HSHM_SWAPPTR(&node->val,val);
        }
        prev = node;
    }

    if (HSHM_CASPTR(&prev->next,NULL,t))return NULL;
    goto retry;
}

hshm_bucket* keylist_del(hshm_bucket** start,void* key,int keysize){
    hshm_bucket* prev;
    hshm_bucket* b;
retry:
    prev=NULL;
    for (b = *start;b;b=b->next){
        if (!memcmp(key,b->key,keysize))
            break;
        prev = b;
    }
    if (HSHM_CASPTR((prev?(&prev->next):start),b,(b?b->next:NULL))) return b;
    goto retry;
    return b;
}

hshm_map* hshm_newmap(int numbuckets, int keysize, int threadlocknum, unsigned (*hash)(void*)){
    hshm_map* map = malloc(sizeof(*map));
    map->numbuckets = numbuckets;
    map->keysize = keysize;
    map->numlocks = threadlocknum;
    map->locks = malloc(sizeof(HSHM_LOCK_T)*threadlocknum);
    for (int i = 0 ; i < threadlocknum; i++) HSHM_LNULL(map->locks[i]);
    map->buckets = malloc(sizeof(hshm_bucket*)*numbuckets);
    for (int i = 0 ; i < numbuckets; i++) map->buckets[i]=NULL;
    map->freebuf=NULL;
    map->hash = hash;

    return map;
}

void hshm_clearfree(hshm_map* map){
    hshm_link* l = hshm_listpull(&map->freebuf);
    for (int i = 0; i < map->numlocks ; i++){
        HSHM_LOCK(&map->locks[i]);
        HSHM_UNLOCK(&map->locks[i]);
    }
    hshm_listfree(&l);
}


void hshm_freemap(hshm_map* map) {//not thread safe of course

    for (int i = 0 ; i < map->numbuckets ; i++)
        free_keylist(&map->buckets[i]);
    hshm_clearfree(map);
    free(map->buckets);
    free(map->locks);
    free(map);

}

void hshm_free(hshm_map* map,void* ptr){
    if (ptr) hshm_listput(&map->freebuf, ptr);
}

void hshm_startwork(hshm_map* map,int threadindex){
    HSHM_LOCK(&map->locks[threadindex]);
}//put these around any accesses to the hashmap
void hshm_endwork(hshm_map* map,int threadindex){
    HSHM_UNLOCK(&map->locks[threadindex]);
}

void* hshm_put(hshm_map* map,void* key,void* val){
    return keylist_put(&map->buckets[map->hash(key)%map->numbuckets],key,val,map->keysize);
}
void* hshm_get(hshm_map* map,void* key){
    return keylist_get(map->buckets[map->hash(key)%map->numbuckets],key,map->keysize);
}
void* hshm_del(hshm_map* map,void* key){
    hshm_bucket* b = keylist_del(&map->buckets[map->hash(key)%map->numbuckets],key,map->keysize);
    hshm_free(map,b);
    return b?b->val:NULL;
}

void hshm_putf(hshm_map* map,void* key,void* val){
    hshm_free(map,keylist_put(&map->buckets[map->hash(key)%map->numbuckets],key,val,map->keysize));
}
void hshm_delf(hshm_map* map,void* key){
    hshm_bucket* b = keylist_del(&map->buckets[map->hash(key)%map->numbuckets],key,map->keysize);
    hshm_free(map,b);
    if (b) hshm_free(map,b->val);
}

#endif
#endif
