/* Lanky -- Scripting Language and Virtual Machine
 * Copyright (C) 2014  Sam Olsen
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "hashmap.h"
#include "tools.h"

typedef struct _node {
    char *value;
    void *dv;
    struct _node *next;
} node;

unsigned long djb2(char *str)
{
    unsigned long hash = 5381;
    char c;

    while((c = *str++))
        hash = ((hash << 5) + hash) + c;

    return hash;
}

Hashmap hm_create(int hash_size, char copies_str)
{
    Hashmap set;
    set.hash_size = hash_size;
    set.copies_str = copies_str;
    set.buckets = malloc(sizeof(node *) * hash_size);

    int i;
    for(i = 0; i < hash_size; i++)
        set.buckets[i] = NULL;

    return set;
}

void hm_put(Hashmap *set, char *str, void *val)
{
    unsigned long hash = djb2(str);
    hash %= set->hash_size;

    node *n;
    if(!set->buckets[hash])
    {
        n = malloc(sizeof(node));
        set->buckets[hash] = n;
    }
    else
    {
        node *last = NULL;
        for(n = set->buckets[hash]; n; n = n->next)
        {
            if(strcmp(n->value, str) == 0)
            {
                n->dv = val;
                return;
            }

            last = n;
        }

        n = last;
        n->next = malloc(sizeof(node));
        n = n->next;
    }

    char *value;
    if(set->copies_str)
    {
        value = malloc(strlen(str) + 1);
        strcpy(value, str);
    }
    else
        value = str;

    n->value = value;
    n->dv = val;
    n->next = NULL;
}

int hm_contains(Hashmap *set, char *str)
{
    unsigned long hash = djb2(str) % set->hash_size;

    if(!set->buckets[hash])
        return 0;

    node *n = set->buckets[hash];
    for(; n; n = n->next)
        if(strcmp(n->value, str) == 0)
            return 1;

    return 0;
}

int hm_count(Hashmap *set)
{
    int count = 0;

    int i;
    for(i = 0; i < set->hash_size; i++)
    {
        node *n = set->buckets[i];
        if(!n)
            continue;

        for(; n; n = n->next)
            count++;
    }

    return count;
}

void *hm_get(Hashmap *set, char *key, hm_error_t *error)
{
    hm_error_t throwaway;

    if(!error)
        error = &throwaway;



    unsigned long hash = djb2(key) % set->hash_size;

    if(!set->buckets[hash])
    {
        *error = HM_KEY_NOT_FOUND;
        return NULL;
    }

    node *n = set->buckets[hash];
    for(; n; n = n->next)
    {
        if(strcmp(n->value, key) == 0)
        {
            *error = HM_NO_ERROR;
            return n->dv;
        }
    }

    *error = HM_KEY_NOT_FOUND;
    return NULL;
}

void hm_remove(Hashmap *set, char *str)
{
    unsigned long hash = djb2(str) % set->hash_size;

    if(!set->buckets[hash])
        return;

    node *n = set->buckets[hash];
    node *prev = NULL;
    for(; n; n = n->next)
    {
        if(strcmp(n->value, str))
        {
            prev = n;
            continue;
        }

        node *next = n->next;

        if(prev)
            prev->next = next;
        else
            set->buckets[hash] = next;

        if(set->copies_str)
            free(n->value);
        free(n);

        break;
    }
}

char **hm_list_keys(Hashmap *set)
{
    int count = hm_count(set);

    char **keys = malloc(sizeof(char *) * count);
    int appendIdx = 0;

    int i;
    for(i = 0; i < set->hash_size; i++)
    {
        node *n = set->buckets[i];
        if(!n)
            continue;

        for(; n; n = n->next)
            keys[appendIdx++] = n->value;
    }

    return keys;
}

void hm_free(Hashmap *set)
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
            if(set->copies_str)
                free(n->value);
            // if(n->dv.type == VSTRING && n->dv.value.s)
            // {
            //     FREE(n->dv.value.s);
            // }
            free(n);
            n = next; 
        }
    }

    free(set->buckets);
}
