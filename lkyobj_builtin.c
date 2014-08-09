#include "lkyobj_builtin.h"
#include "lky_machine.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define OBJ_NUM_PROMO(a, b) (a->type == LBI_FLOAT || b->type == LBI_FLOAT ? LBI_FLOAT : LBI_INTEGER) 
#define BI_CAST(o, n) lky_object_builtin * n = (lky_object_builtin *) o

lky_object *lobjb_alloc(lky_builtin_type t, lky_builtin_value v)
{
    lky_object_builtin *obj = malloc(sizeof(lky_object_builtin));
    obj->type = t;
    obj->mem_count = 0;
    obj->members = trie_new();
    obj->value = v;

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
    func->members = trie_new();
    
    func->code = code;
    func->bucket = NULL;

    func->parent_stack = inherited;

    lky_callable c;
    c.function = (lky_function_ptr)&lobjb_default_callable;
    c.argc = argc;
    func->callable = c;
    
    return (lky_object *)func;
}

lky_object *lobjb_build_class(lky_object_function *builder, char *refname)
{
    lky_object_class *cls = malloc(sizeof(lky_object_class));
    cls->type = LBI_CLASS;
    cls->mem_count = 0;
    cls->members = trie_new();

    cls->builder = builder;
    cls->refname = refname;

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

lky_object *lobjb_binary_add(lky_object *a, lky_object *b)
{
    BI_CAST(a, ab);
    BI_CAST(b, bb);

    if(a == &lky_nil || b == &lky_nil)
        return &lky_nil;

    if(ab->type == LBI_STRING || bb->type == LBI_STRING)
    {
        char *r, *l;
        char nr, nl;
        nr = nl = 0;
        if(ab->type != LBI_STRING)
        {
            l = malloc(100);
            str_print(ab->type, ab->value, l);
            r = bb->value.s;
            nl = 1;
        }
        else if(bb->type != LBI_STRING)
        {
            r = malloc(100);
            str_print(bb->type, bb->value, r);
            l = ab->value.s;
            nr = 1;
        }
        else
        {
            l = ab->value.s;
            r = bb->value.s;
        }

        char *n = malloc(strlen(r) + strlen(l) + 1);
        sprintf(n, "%s%s", l, r);
        lky_builtin_value v;
        v.s = n;

        if(nr)
            free(r);
        if(nl)
            free(l);

        return lobjb_alloc(LBI_STRING, v);
    }

    lky_builtin_value v;
    lky_builtin_type t = OBJ_NUM_PROMO(ab, bb);
    switch(t)
    {
        case LBI_FLOAT:
            v.d = OBJ_NUM_UNWRAP(ab) + OBJ_NUM_UNWRAP(bb);
            break;
        case LBI_INTEGER:
            v.i = OBJ_NUM_UNWRAP(ab) + OBJ_NUM_UNWRAP(bb);
            break;
        default:
            break;
    }

    return lobjb_alloc(t, v);
}

lky_object *lobjb_binary_subtract(lky_object *a, lky_object *b)
{
    BI_CAST(a, ab);
    BI_CAST(b, bb);

    if(a == &lky_nil || b == &lky_nil)
        return &lky_nil;

    lky_builtin_value v;
    lky_builtin_type t = OBJ_NUM_PROMO(ab, bb);
    switch(t)
    {
        case LBI_FLOAT:
            v.d = OBJ_NUM_UNWRAP(ab) - OBJ_NUM_UNWRAP(bb);
            break;
        case LBI_INTEGER:
            v.i = OBJ_NUM_UNWRAP(ab) - OBJ_NUM_UNWRAP(bb);
            break;
        default:
            break;
    }

    return lobjb_alloc(t, v);
}

lky_object *lobjb_binary_multiply(lky_object *a, lky_object *b)
{
    BI_CAST(a, ab);
    BI_CAST(b, bb);

    lky_builtin_value v;
    lky_builtin_type t;

    if(a == &lky_nil || b == &lky_nil)
        return &lky_nil;

    if(ab->type == LBI_STRING || bb->type == LBI_STRING)
    {
        if(ab->type == LBI_STRING && bb->type == LBI_STRING)
            return &lky_nil;

        lky_object_builtin *strobj = ab->type == LBI_STRING ? ab : bb;
        lky_object_builtin *oobj = strobj == ab ? bb : ab;

        if(oobj->type != LBI_INTEGER)
            return &lky_nil;

        char *str = strobj->value.s;
        long ct = oobj->value.i;

        int len = strlen(str);
        int targ = len * ct;

        char *nstr = malloc(len * ct + 1);
        nstr[targ] = 0;
        strcpy(nstr, str);

        long done = len;
        while(done < targ)
        {
            long n = (done <= targ - done ? done : targ - done);
            memcpy(nstr + done, nstr, n);
            done += n;
        }

        v.s = nstr;
        t = LBI_STRING;

        return lobjb_alloc(t, v);
    }

    t = OBJ_NUM_PROMO(ab, bb);
    switch(t)
    {
        case LBI_FLOAT:
            v.d = OBJ_NUM_UNWRAP(ab) * OBJ_NUM_UNWRAP(bb);
            break;
        case LBI_INTEGER:
            v.i = OBJ_NUM_UNWRAP(ab) * OBJ_NUM_UNWRAP(bb);
            break;
        default:
            break;
    }

    return lobjb_alloc(t, v);
}

lky_object *lobjb_binary_divide(lky_object *a, lky_object *b)
{
    BI_CAST(a, ab);
    BI_CAST(b, bb);

    if(a == &lky_nil || b == &lky_nil)
        return &lky_nil;

    lky_builtin_value v;
    lky_builtin_type t = OBJ_NUM_PROMO(ab, bb);
    switch(t)
    {
        case LBI_FLOAT:
            v.d = OBJ_NUM_UNWRAP(ab) / OBJ_NUM_UNWRAP(bb);
            break;
        case LBI_INTEGER:
            v.i = OBJ_NUM_UNWRAP(ab) / OBJ_NUM_UNWRAP(bb);
            break;
        default:
            break;
    }

    return lobjb_alloc(t, v);
}

lky_object *lobjb_binary_modulo(lky_object *a, lky_object *b)
{
    BI_CAST(a, ab);
    BI_CAST(b, bb);

    if(a == &lky_nil || b == &lky_nil)
        return &lky_nil;

    lky_builtin_value v;
    lky_builtin_type t = OBJ_NUM_PROMO(ab, bb);
    switch(t)
    {
        case LBI_FLOAT:
            v.d = remainder(OBJ_NUM_UNWRAP(ab), OBJ_NUM_UNWRAP(bb));
            break;
        case LBI_INTEGER:
            v.i = ab->value.i % bb->value.i;
            break;
        default:
            break;
    }

    return lobjb_alloc(t, v);
}

lky_object *lobjb_binary_lessthan(lky_object *a, lky_object *b)
{
    BI_CAST(a, ab);
    BI_CAST(b, bb);

    if(a == &lky_nil || b == &lky_nil)
        return &lky_nil;

    lky_builtin_value v;
    lky_builtin_type t = LBI_INTEGER;

    if(ab->type == LBI_STRING || bb->type == LBI_STRING)
    {
        if(ab->type != LBI_STRING || bb->type != LBI_STRING)
            v.i = 0;
        else
            v.i = strcmp(ab->value.s, bb->value.s) < 0;
    }
    else
        v.i = (OBJ_NUM_UNWRAP(ab) < OBJ_NUM_UNWRAP(bb));

    // printf("==> %d\n", v.i);

    return lobjb_alloc(t, v);
}

lky_object *lobjb_binary_greaterthan(lky_object *a, lky_object *b)
{
    BI_CAST(a, ab);
    BI_CAST(b, bb);

    if(a == &lky_nil || b == &lky_nil)
        return &lky_nil;

    lky_builtin_value v;
    lky_builtin_type t = LBI_INTEGER;

    if(ab->type == LBI_STRING || bb->type == LBI_STRING)
    {
        if(ab->type != LBI_STRING || bb->type != LBI_STRING)
            v.i = 0;
        else
            v.i = strcmp(ab->value.s, bb->value.s) > 0;
    }
    else
        v.i = (OBJ_NUM_UNWRAP(ab) > OBJ_NUM_UNWRAP(bb));

    return lobjb_alloc(t, v);
}

lky_object *lobjb_binary_equals(lky_object *a, lky_object *b)
{
    BI_CAST(a, ab);
    BI_CAST(b, bb);

    if(a == &lky_nil || b == &lky_nil)
        return &lky_nil;

    lky_builtin_value v;
    lky_builtin_type t = LBI_INTEGER;

    if(ab->type == LBI_STRING || bb->type == LBI_STRING)
    {
        if(ab->type != LBI_STRING || bb->type != LBI_STRING)
            v.i = 0;
        else
            v.i = strcmp(ab->value.s, bb->value.s) == 0;
    }
    else
        v.i = (OBJ_NUM_UNWRAP(ab) == OBJ_NUM_UNWRAP(bb));

    return lobjb_alloc(t, v);
}

lky_object *lobjb_binary_lessequal(lky_object *a, lky_object *b)
{
    BI_CAST(a, ab);
    BI_CAST(b, bb);

    if(a == &lky_nil || b == &lky_nil)
        return &lky_nil;

    lky_builtin_value v;
    lky_builtin_type t = LBI_INTEGER;

    if(ab->type == LBI_STRING || bb->type == LBI_STRING)
    {
        if(ab->type != LBI_STRING || bb->type != LBI_STRING)
            v.i = 0;
        else
            v.i = strcmp(ab->value.s, bb->value.s) <= 0;
    }
    else
        v.i = (OBJ_NUM_UNWRAP(ab) <= OBJ_NUM_UNWRAP(bb));

    return lobjb_alloc(t, v);
}

lky_object *lobjb_binary_greatequal(lky_object *a, lky_object *b)
{
    BI_CAST(a, ab);
    BI_CAST(b, bb);

    if(a == &lky_nil || b == &lky_nil)
        return &lky_nil;

    lky_builtin_value v;
    lky_builtin_type t = LBI_INTEGER;

    if(ab->type == LBI_STRING || bb->type == LBI_STRING)
    {
        if(ab->type != LBI_STRING || bb->type != LBI_STRING)
            v.i = 0;
        else
            v.i = strcmp(ab->value.s, bb->value.s) >= 0;
    }
    else
        v.i = (OBJ_NUM_UNWRAP(ab) >= OBJ_NUM_UNWRAP(bb));

    return lobjb_alloc(t, v);
}

lky_object *lobjb_binary_notequal(lky_object *a, lky_object *b)
{
    BI_CAST(a, ab);
    BI_CAST(b, bb);

    if(a == &lky_nil || b == &lky_nil)
        return &lky_nil;

    lky_builtin_value v;
    lky_builtin_type t = LBI_INTEGER;

    if(ab->type == LBI_STRING || bb->type == LBI_STRING)
    {
        if(ab->type != LBI_STRING || bb->type != LBI_STRING)
            v.i = 0;
        else
            v.i = strcmp(ab->value.s, bb->value.s) != 0;
    }
    else
        v.i = (OBJ_NUM_UNWRAP(ab) != OBJ_NUM_UNWRAP(bb));

    return lobjb_alloc(t, v);
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
        rc_decr((lky_object *)args->value);
        // code->locals[i] = args->value;
    }

    return mach_execute(func);
}

lky_object *lobjb_default_class_callable(lky_object_seq *args, lky_object *self)
{
    lky_object_class *cls = (lky_object_class *)self;

    lky_object *obj = lobj_alloc();

    lky_object_function *func = cls->builder;
    func->bucket = lobj_alloc();

    lky_object *outobj = lobj_alloc();

    lobj_set_member(func->bucket, cls->refname, outobj);

    lky_object *returned = mach_execute(func);

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
        lobj_dealloc((lky_object *)seq);
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
    char **names = malloc(sizeof(void *) * num_names);

    int i;
    for(i = 0; i < num_names; i++)
    {
        long sz;
        fread(&sz, sizeof(long), 1, f);
        names[i] = malloc(sz);
        fread(names[i], sizeof(char), sz, f);
    }
    
    void **con = malloc(sizeof(void *) * len);
    
    for(i = 0; i < len; i++)
    {
        lky_object *obj = lobjb_deserialize(f);
        rc_incr(obj);
        con[i] = obj;
    }
    
    fread(&len, sizeof(long), 1, f);
    char *ops = malloc(len);
    fread(ops, sizeof(char), len, f);
    
    lky_object_code *obj = malloc(sizeof(lky_object_code));
    obj->constants = con;
    obj->type = LBI_CODE;
    obj->num_constants = len;
    obj->num_locals = locals;
    obj->num_names = num_names;
    obj->locals = malloc(sizeof(void *) * locals);
    obj->ops = ops;
    obj->op_len = len;
    obj->names = names;
    
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

    void **con = malloc(sizeof(void *) * len);
    for(i = 0; i < len; i++)
    {
        lky_object *obj = lobjb_deserialize(f);
        rc_incr(obj);
        con[i] = obj;
    }

    fread(&len, sizeof(long), 1, f);
    char *ops = malloc(len);
    fread(ops, sizeof(char), len, f);

    lky_object_code *obj = malloc(sizeof(lky_object_code));
    obj->constants = con;
    obj->num_constants = len;
    obj->num_locals = locals;
    obj->num_names = num_names;
    obj->locals = malloc(sizeof(void *) * locals);
    obj->ops = ops;
    obj->op_len = len;
    obj->names = names;

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
        default:
        break;
    }
}
