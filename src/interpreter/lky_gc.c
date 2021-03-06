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

#include "lky_gc.h"
#include "lky_machine.h"
#include "arraylist.h"
#include "gc_hashset.h"
#include "module.h"

void gc_mark();
void gc_collect();

typedef struct gc_root_list {
    struct gc_root_list *next;
    void *value;
} gc_root_list;

typedef struct {
    gc_hashset pool;
    gc_root_list *roots;
    stackframe *function_stacks;
    size_t max_size;
    size_t cur_size;
    
    size_t growth_size;
    size_t marked_size;
} gc_bundle;

typedef struct {
    void **stack;
    int size;
} gc_stack;

gc_bundle bundle;
char gc_started = 0;
char gc_paused = 0;

void gc_pause()
{
    gc_started = 0;
}

void gc_pause_collection()
{
    gc_paused = 1;
}

void gc_resume()
{
    gc_started = 1;
}

void gc_resume_collection()
{
    gc_paused = 0;
}

void gc_add_func_stack(stackframe *frame)
{
    bundle.function_stacks = frame;
}

void gc_init()
{
    bundle.pool = gchs_create(8);
    bundle.roots = NULL;
    //    bundle.max_size = 16000000;
    bundle.growth_size = 1600000;
    bundle.marked_size = 0;
    bundle.max_size = 1600000;
    bundle.cur_size = 0;
    bundle.function_stacks = NULL;
    gc_started = 1;
}

void gc_add_root_object(lky_object *obj)
{
    if(!gc_started)
        return;
    
    gc_root_list *list = malloc(sizeof(gc_root_list));
    list->next = NULL;
    list->value = obj;
    
    if(bundle.roots)
        list->next = bundle.roots;
    bundle.roots = list;
}

void gc_remove_root_object(lky_object *obj)
{
    gc_root_list *list = bundle.roots;
    gc_root_list *prev = NULL;
    for(; list; list = list->next)
    {
        if(list->value == obj)
        {
            if(prev)
                prev->next = list->next;
            
            if(bundle.roots == list)
                bundle.roots = list->next;
            
            free(list);
            break;
        }
        
        prev = list;
    }
    
}

#define TYPE_SIZE_CASE(type, obj) case LBI_ ## type : return sizeof(obj)

size_t gc_determine_size_of(lky_object *obj)
{
    switch(obj->type)
    {
        TYPE_SIZE_CASE(INTEGER, lky_object_builtin);
        TYPE_SIZE_CASE(FLOAT, lky_object_builtin);
        TYPE_SIZE_CASE(BLOB, lky_object_builtin);
        TYPE_SIZE_CASE(CUSTOM, lky_object);
        TYPE_SIZE_CASE(CUSTOM_EX, lky_object_custom);
        TYPE_SIZE_CASE(CLASS, lky_object_class);
        TYPE_SIZE_CASE(FUNCTION, lky_object_function);
        TYPE_SIZE_CASE(CODE, lky_object_code);
        TYPE_SIZE_CASE(ITERABLE, lky_object_iterable);
        TYPE_SIZE_CASE(ERROR, lky_object_error);
        TYPE_SIZE_CASE(SEQUENCE, lky_object_seq);
        default: return 0;
    }
}

void gc_add_object(lky_object *obj)
{
    if(!gc_started || !obj)
        return;
    
    gchs_add(&bundle.pool, obj);
    bundle.cur_size += gc_determine_size_of(obj);
}

size_t gc_alloced()
{
    return bundle.cur_size;
}

void gc_gc()
{
    if(bundle.cur_size < bundle.max_size)
        return;

    if(gc_paused)
        return;
    
    gc_mark();
    gc_collect();
    bundle.max_size = bundle.marked_size + bundle.growth_size;
}

void gc_collect()
{
    bundle.marked_size = 0;
    
    gc_hashset pool = bundle.pool;
    void **objs = gchs_to_list(&bundle.pool);
    
    int i;
    for(i = pool.count - 1; i >= 0; i--)
    {
        lky_object *o = objs[i];
        if(!o->mem_count)
        {
            bundle.cur_size -= gc_determine_size_of(o);
            // TODO: This will need to change.

            
            if(o->type == LBI_CUSTOM)
            {
                lky_object *func = lobj_get_member(o, "on_destroy_");
                if(func)
                    lobjb_call(func, NULL, NULL);
            }

            lobj_dealloc(o);
            gchs_remove(&bundle.pool, o);
        }
        else
        {
            bundle.marked_size += gc_determine_size_of(o);
            o->mem_count = 0;
        }
    }
    
    free(objs);
}

void gc_mark_for_each(void *key, void *val, void *data)
{
    gc_mark_object((lky_object *)val);
}

void gc_mark_object(lky_object *o)
{
    if(((uintptr_t)(o) & 1) || o->mem_count)
        return;
    
    o->mem_count = 1;

    if(o->type != LBI_INTEGER && o->type != LBI_FLOAT && o->type != LBI_BLOB &&
            o->type != LBI_SEQUENCE && o->type != LBI_CODE && o->type != LBI_ITERABLE)
        hst_for_each(&o->members, gc_mark_for_each, NULL);
    //gc_mark_object(&o->parent);
    
    switch(o->type)
    {
        case LBI_FUNCTION:
        {
            lky_object_function *func = (lky_object_function *)o;
            
            if(func->bucket)
                gc_mark_object(func->bucket);
            if(func->code)
                gc_mark_object((lky_object *)func->code);
            if(func->owner)
                gc_mark_object((lky_object *)func->owner);
            if(func->bound)
                gc_mark_object((lky_object *)func->bound);
            
            int i;
            for(i = 0; i < func->parent_stack.count; i++)
            {
                gc_mark_object(arr_get(&func->parent_stack, i));
            }
        }
            break;
        case LBI_SEQUENCE:
        {
            lky_object_seq *seq = (lky_object_seq *)o;
            if(seq->next)
                gc_mark_object((lky_object *)seq->next);
            if(seq->value)
                gc_mark_object((lky_object *)seq->value);
        }
            break;
        case LBI_CODE:
        {
            lky_object_code *code = (lky_object_code *)o;
            int i;
            for(i = 0; i < code->num_constants; i++)
                gc_mark_object(code->constants[i]);
        }
            break;
        case LBI_CLASS:
        {
            lky_object_class *cls = (lky_object_class *)o;
            gc_mark_object((lky_object *)cls->builder);
        }
            break;
        case LBI_CUSTOM_EX:
        {
            lky_object_custom *cu = (lky_object_custom *)o;
            if(!cu->savefunc)
                break;
            cu->savefunc(o);
        }
            break;
        case LBI_ITERABLE:
        {
            lky_object_iterable *it = (lky_object_iterable *)o;
            gc_mark_object(it->owner);
        }
            break;
        case LBI_BLOB:
        {
            lky_object_builtin *bl = (lky_object_builtin *)o;
            if(bl->on_gc)
                bl->on_gc(bl->value.b, CGC_MARK);
        }
            break;
        default:
            break;
    }
}

void gc_mark_stack(void **stack, int size)
{
    int i;
    for(i = 0; i < size; i++)
    {
        if(!stack[i])
            break;
        
        gc_mark_object(stack[i]);
    }
}

void gc_mark_function_stack(stackframe *frame)
{
    for(; frame; frame = frame->next)
    {
        gc_mark_object(frame->bucket);
        gc_mark_stack(frame->data_stack, (int)frame->stack_size);
        gc_mark_stack(frame->locals, (int)frame->locals_count);
        
        int i;
        for(i = 0; i < frame->parent_stack.count; i++)
        {
            gc_mark_object(arr_get(&frame->parent_stack, i));
        }
    }
}

void gc_mark()
{
    gc_root_list *list = bundle.roots;
    
    for(; list; list = list->next)
    {
        gc_mark_object(list->value);
        
    }
    
    //    arr_for_each(&bundle.root_stacks, (arr_pointer_function)&gc_mark_stack);
    gc_mark_function_stack(bundle.function_stacks);

    md_gc_cycle();
    
    // arr_for_each(&bundle.pool, (arr_pointer_function)&gc_reset_mark);
    // arr_for_each(&bundle.roots, (arr_pointer_function)&gc_mark_object_with_return);
    // arr_for_each(&bundle.root_stacks, (arr_pointer_function)&gc_mark_stack_with_return);
}
