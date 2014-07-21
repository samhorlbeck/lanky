#include "lky_object.h"
#include "lkyobj_builtin.h"
#include <stdio.h>
#include <stdlib.h>

lky_object *lobj_alloc()
{
    lky_object *obj = malloc(sizeof(lky_object));
    obj->type = LBI_CUSTOM;
    obj->mem_count = 0;
    obj->members = arr_create(10);

    // obj->value = value;

    return obj;
}

void rc_decr(lky_object *obj)
{
    obj->mem_count--;
    if(!obj->mem_count)
    {
        // printf("Freeing : ");
        // lobjb_print(obj);
        if(obj->type != LBI_CUSTOM)
            lobjb_clean(obj);
        arr_free(&(obj->members));
        free(obj);
    }
}

void rc_incr(lky_object *obj)
{
    obj->mem_count++;
}