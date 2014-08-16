#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "gc_hashset.h"

typedef gc_hashlist node;

gc_hashset gchs_create(int hash_size)
{
    gc_hashset set;
    set.hash_size = hash_size;
    set.buckets = malloc(sizeof(node *) * hash_size);
    set.count = 0;

    int i;
    for(i = 0; i < hash_size; i++)
        set.buckets[i] = NULL;

    return set;
}

void gchs_add(gc_hashset *set, void *obj)
{
    // Handle resizing
    if(set->count > set->hash_size * (2. / 3))
    {
        void **data = gchs_to_list(set);
        gc_hashset next = gchs_create(set->hash_size * 3);
        int i;
        for(i = 0; i < set->count; i++)
        {
            gchs_add(&next, data[i]);
        }

        gchs_add(&next, obj);
        free(data);
        gchs_free(set);
        *set = next;
        return;
    }

    unsigned long hash = (unsigned long)obj;
    hash %= set->hash_size;

    node *n;
    if(!set->buckets[hash])
    {
        n = malloc(sizeof(node));
        set->buckets[hash] = n;
        n->next = NULL;
    }
    else
    {
        node *last = NULL;
        for(n = set->buckets[hash]; n; n = n->next)
        {
            if(n->value == obj)
                return;

            last = n;
        }

        n = last;
        n->next = malloc(sizeof(node));
        n = n->next;
        n->next = NULL;
    }

    set->count++;
    n->value = obj;
}

int gchs_contains(gc_hashset *set, void *obj)
{
    unsigned long hash = (unsigned long)obj % set->hash_size;

    if(!set->buckets[hash])
        return 0;

    node *n = set->buckets[hash];
    for(; n; n = n->next)
        if(n->value == obj)
            return 1;

    return 0;
}

void gchs_remove(gc_hashset *set, void *obj)
{
    unsigned long hash = (unsigned long)obj % set->hash_size;

    if(!set->buckets[hash])
        return;

    node *n = set->buckets[hash];
    node *prev = NULL;
    for(; n; n = n->next)
    {
        if(n->value != obj)
        {
            prev = n;
            continue;
        }

        node *next = n->next;

        if(prev)
            prev->next = next;
        else
            set->buckets[hash] = next;

        free(n);

        break;
    }

    set->count--;
}

void gchs_for_each(gc_hashset *set, gchs_pointer_function callback)
{
    int i;
    for(i = 0; i < set->hash_size; i++)
    {
        node *n = set->buckets[i];

        if(!n)
            continue;

        for(; n; n = n->next)
            callback(n->value);
    }
}

void gchs_free(gc_hashset *set)
{
    int i;
    for(i = 0; i < set->hash_size; i++)
    {
        node *n = set->buckets[i];
        if(!n)
            continue;

        while(n)
        {
            node *next = n->next;
            free(n);
            n = next; 
        }
    }

    free(set->buckets);
}

void **gchs_to_list(gc_hashset *set)
{
    void **retval = malloc(sizeof(void *) * set->count);

    int i, ct;
    for(i = ct = 0; i < set->hash_size; i++)
    {
        node *n = set->buckets[i];

        if(!n)
            continue;

        for(; n; n = n->next)
            retval[ct++] = n->value;
    }

    return retval;
}
