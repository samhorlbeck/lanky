#include "lkyobj_builtin.h"
#include "lky_machine.h"
#include "lky_gc.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

lky_object *lobjb_alloc(lky_builtin_type t, lky_builtin_value v)
{
    lky_object_builtin *obj = malloc(sizeof(lky_object_builtin));
    obj->type = t;
    obj->size = sizeof(lky_object_builtin);
    obj->mem_count = 0;
    obj->members = trie_new();
    obj->members.free_func = (trie_pointer_function)(&rc_decr);
    obj->value = v;
    gc_add_object((lky_object *)obj);

    return (lky_object *)obj;
}

lky_object *lobjb_build_int(long value)
{
    lky_builtin_value v;
    v.i = value;
    return lobjb_alloc(LBI_INTEGER, v);
}

lky_object *lobjb_build_float(double value)
{
    lky_builtin_value v;
    v.d = value;
    return lobjb_alloc(LBI_FLOAT, v);
}

lky_object *lobjb_build_func(lky_object_code *code, int argc, arraylist inherited)
{
    lky_object_function *func = malloc(sizeof(lky_object_function));
    func->type = LBI_FUNCTION;
    func->mem_count = 0;
    func->size = sizeof(lky_object_function);
    func->members = trie_new();
    func->members.free_func = (trie_pointer_function)(&rc_decr);
    
    func->code = code;
    func->bucket = NULL;

    func->parent_stack = inherited;

    gc_add_object((lky_object *)func);

    lky_callable c;
    c.function = (lky_function_ptr)&lobjb_default_callable;
    c.argc = argc;
    func->callable = c;

    // Add argc member (this should probably be done somewhere else.
    lobj_set_member((lky_object *)func, "argc", (lky_object *)lobjb_build_int(argc)); 
    
    return (lky_object *)func;
}

lky_object *lobjb_build_class(lky_object_function *builder, char *refname)
{
    lky_object_class *cls = malloc(sizeof(lky_object_class));
    cls->type = LBI_CLASS;
    cls->mem_count = 0;
    cls->size = sizeof(lky_object_class);
    cls->members = trie_new();
    cls->members.free_func = (trie_pointer_function)(&rc_decr);

    cls->builder = builder;
    cls->refname = refname;
    gc_add_object((lky_object *)cls);

    lky_callable c;
    c.function = (lky_function_ptr)&lobjb_default_class_callable;
    c.argc = builder->callable.argc;
    cls->callable = c;

    return (lky_object *)cls;
}

void str_print(lky_builtin_type t, lky_builtin_value v, char *buf)
{
    switch(t)
    {
        case LBI_FLOAT:
            sprintf(buf, "%lf", v.d);
        break;
        case LBI_INTEGER:
            sprintf(buf, "%ld", v.i);
        break;
        default:
        break;
    }
}

lky_object *lobjb_default_callable(lky_object_seq *args, lky_object *self)
{
    lky_object_function *func = (lky_object_function *)self;
    lky_object_code *code = func->code;

    func->bucket = lobj_alloc();

    long i;
    for(i = 0; args; i++, args = args->next)
    {
        char *name = code->names[i];
        lobj_set_member(func->bucket, name, (lky_object *)args->value);
        // rc_decr((lky_object *)args->value);
        // code->locals[i] = args->value;
    }

    return mach_execute(func);
}

lky_object *lobjb_default_class_callable(lky_object_seq *args, lky_object *self)
{
    lky_object_class *cls = (lky_object_class *)self;

    lky_object_function *func = cls->builder;
    func->bucket = lobj_alloc();
    rc_incr(func->bucket);

    lky_object *outobj = lobj_alloc();
    rc_incr(outobj);

    lobj_set_member(func->bucket, cls->refname, outobj);

    lky_object *returned = mach_execute(func);
    //printf("...%d\n", func->bucket->mem_count);

    if(returned)
    {
        // TODO: This should not happen... Runtime error?
        rc_decr(returned);
    }

    lky_object *init = lobj_get_member(outobj, "build_");
    if(init)
    {
        lobjb_default_callable(args, init);
    }

    return outobj;
}

//lky_object *lobjb_default_class_callable(lky_object_seq *args, lky_object *self)
//{
//    lky_object_class *cls = (lky_object_class *)self;
//
//    arraylist list = cls->members;
//
//    lky_object *obj = lobj_alloc();
//
//    int i;
//    for(i = 0; i < list.count; i++)
//    {
//        lky_class_member_wrapper *wrap = arr_get(&list, i);
//        lobj_set_member(obj, wrap->name, wrap->member);
//    }
//
//    return obj;
//}
//    

char lobjb_quick_compare(lky_object *a, lky_object *b)
{
    BI_CAST(a, ab);
    BI_CAST(b, bb);

    if(ab->type == LBI_STRING || bb->type == LBI_STRING)
    {
        if(ab->type != LBI_STRING || bb->type != LBI_STRING)
            return 0;

        return !strcmp(ab->value.s, bb->value.s);
    }

    if(a == &lky_nil || b == &lky_nil)
    {
        return a == b;
    }

    if(ab->type == LBI_CODE || bb->type == LBI_CODE)
        return ab == bb;

    return OBJ_NUM_UNWRAP(ab) == OBJ_NUM_UNWRAP(bb);
}

void lobjb_print(lky_object *a)
{
    lky_object_builtin *b = (lky_object_builtin *)a;

    switch(b->type)
    {
        case LBI_FLOAT:
            printf("%lf\n", b->value.d);
        break;
        case LBI_INTEGER:
            printf("%ld\n", b->value.i);
        break;
        case LBI_STRING:
            printf("%s\n", b->value.s);
        break;
        case LBI_NIL:
            printf("(null)\n");
        break;
        default:
            printf("%p\n", b);
            break;

    }
}

lky_object_seq *lobjb_make_seq_node(lky_object *value)
{
    lky_object_seq *seq = malloc(sizeof(lky_object_seq));
    seq->type = LBI_SEQUENCE;
    seq->mem_count = 0;
    seq->members = trie_new();
    seq->size = sizeof(lky_object_seq);
    gc_add_object((lky_object *)seq);

    seq->value = (struct lky_object *)value;
    seq->next = NULL;
    return seq;
}

void lobjb_free_seq(lky_object_seq *seq)
{
    while(seq)
    {
        lky_object *obj = (lky_object *)seq->value;
        rc_decr(obj);
        lky_object_seq *next = seq->next;
        //lobj_dealloc((lky_object *)seq);
        seq = next;
    }
}

void lobjb_serialize_code(lky_object *o, FILE *f)
{
    lky_object_code *code = (lky_object_code *)o;
    
    void **cons = code->constants;
    fwrite(&(code->num_constants), sizeof(long), 1, f);
    fwrite(&(code->num_locals), sizeof(long), 1, f);
    fwrite(&(code->num_names), sizeof(long), 1, f);
    fwrite(&(code->stack_size), sizeof(long), 1, f);

    int i;
    for(i = 0; i < code->num_names; i++)
    {
        char *str = code->names[i];
        long len = strlen(str) + 1;
        fwrite(&len, sizeof(long), 1, f);
        fwrite(str, len, 1, f);
    }

    for(i = 0; i < code->num_constants; i++)
    {
        lky_object *obj = cons[i];
        lobjb_serialize(obj, f);
    }
    
    fwrite(&(code->op_len), sizeof(long), 1, f);
    
    fwrite(code->ops, sizeof(char), code->op_len, f);
}

void lobjb_serialize(lky_object *o, FILE *f)
{
    BI_CAST(o, obj);

    fwrite(&(obj->type), sizeof(int), 1, f);
    switch(obj->type)
    {
        case LBI_FLOAT:
        {
            unsigned long sz = sizeof(double);
            fwrite(&sz, sizeof(unsigned long), 1, f);
            fwrite(&(obj->value.d), sizeof(double), 1, f);
        }
        break;
        case LBI_INTEGER:
        {
            unsigned long sz = sizeof(long);
            fwrite(&sz, sizeof(unsigned long), 1, f);
            fwrite(&(obj->value.d), sizeof(long), 1, f);
        }
        break;
        case LBI_STRING:
        {
            char *str = obj->value.s;
            unsigned long sz = strlen(str) + 1;
            fwrite(&sz, sizeof(unsigned long), 1, f);
            fwrite(str, sizeof(char), sz, f);
        }
        break;
        case LBI_CODE:
        {
            unsigned long sz = 0;
            fwrite(&sz, sizeof(unsigned long), 1, f);
            lobjb_serialize_code(o, f);
            printf("Serializing function...\n");
        }
        break;
        default:
        break;
    }
}

lky_object *lobjb_deserialize_code(FILE *f)
{
    long len;
    fread(&len, sizeof(long), 1, f);
    
    long locals;
    fread(&locals, sizeof(long), 1, f);

    long num_names;
    fread(&num_names, sizeof(long), 1, f);
    char **names = malloc(sizeof(char *) * num_names);

    long stack_size;
    fread(&stack_size, sizeof(long), 1, f);

    int i;
    for(i = 0; i < num_names; i++)
    {
        long sz;
        fread(&sz, sizeof(long), 1, f);
        names[i] = malloc(sz);
        fread(names[i], sizeof(char), sz, f);
    }
   
    void **con = NULL; 
    if(len > 0)
    {
        con = malloc(sizeof(void *) * len);
    
        for(i = 0; i < len; i++)
        {
            lky_object *obj = lobjb_deserialize(f);
            rc_incr(obj);
            con[i] = obj;
        }
    }
    
    long oplen;
    fread(&oplen, sizeof(long), 1, f);
    unsigned char *ops = malloc(oplen);
    fread(ops, sizeof(unsigned char), oplen, f);
    
    lky_object_code *obj = malloc(sizeof(lky_object_code));
    obj->constants = con;
    obj->type = LBI_CODE;
    obj->members = trie_new();
    obj->type = LBI_CODE;
    obj->size = sizeof(lky_object_code);
    obj->mem_count = 0;
    obj->num_constants = len;
    obj->num_locals = locals;
    obj->num_names = num_names;
    obj->locals = malloc(sizeof(void *) * locals);
    obj->ops = ops;
    obj->op_len = oplen;
    obj->names = names;
    obj->stack_size = stack_size;

    gc_add_object((lky_object *)obj);

    for(i = 0; i < locals; i++)
        obj->locals[i] = NULL;
    
    return (lky_object *)obj;
}

lky_object *lobjb_deserialize(FILE *f)
{
    lky_builtin_type type;
    fread(&type, sizeof(int), 1, f);

    unsigned long sz;
    fread(&sz, sizeof(unsigned long), 1, f);

    lky_builtin_value value;

    switch(type)
    {
        case LBI_FLOAT:
            fread(&value.d, sz, 1, f);
        break;
        case LBI_INTEGER:
            fread(&value.i, sz, 1, f);
        break;
        case LBI_STRING:
        {
            char *str = malloc(sz);
            fread(str, sz, 1, f);
            value.s = str;
        }
        break;
        case LBI_CODE:
        {
            return lobjb_deserialize_code(f);
        }
        break;
        default:
        break;
    }

    return lobjb_alloc(type, value);
}

lky_object_code *lobjb_load_file(char *name)
{
    FILE *f = fopen(name, "r");
    long len;
    fread(&len, sizeof(long), 1, f);

    long locals;
    fread(&locals, sizeof(long), 1, f);

    long num_names;
    fread(&num_names, sizeof(long), 1, f);

    long stack_size;
    fread(&stack_size, sizeof(long), 1, f);

    char **names = malloc(sizeof(char *) * num_names);
    int i;
    for(i = 0; i < num_names; i++)
    {
        long sz;
        fread(&sz, sizeof(long), 1, f);
        char *str = malloc(sizeof(char) * sz);
        fread(str, sizeof(char), sz, f);
        names[i] = str;
    }

    void **con = NULL;

    if(len > 0)
    {
        con = malloc(sizeof(void *) * len);
        for(i = 0; i < len; i++)
        {
            lky_object *obj = lobjb_deserialize(f);
            rc_incr(obj);
            con[i] = obj;
        }
    }

    long oplen;
    fread(&oplen, sizeof(long), 1, f);
    unsigned char *ops = malloc(oplen);
    fread(ops, sizeof(unsigned char), oplen, f);

    lky_object_code *obj = malloc(sizeof(lky_object_code));
    obj->constants = con;
    obj->type = LBI_CODE;
    obj->size = sizeof(lky_object_code);
    obj->num_constants = len;
    obj->num_locals = locals;
    obj->members = trie_new();
    obj->mem_count = 0;
    obj->num_names = num_names;
    obj->locals = malloc(sizeof(void *) * locals);
    obj->ops = ops;
    obj->op_len = oplen;
    obj->names = names;
    obj->stack_size = stack_size;

    gc_add_object((lky_object *)obj);

    for(i = 0; i < locals; i++)
        obj->locals[i] = NULL;

    return obj;
}

void lobjb_clean(lky_object *a)
{
    lky_object_builtin *obj = (lky_object_builtin *)a;

    switch(obj->type)
    {
        case LBI_STRING:
            free(obj->value.s);
        break;
        case LBI_FUNCTION:
            arr_free(&((lky_object_function *)a)->parent_stack);
        default:
        break;
    }
}