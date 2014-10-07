#include <string.h>
#include "stl_convert.h"

lky_object *stlcon_to_int(lky_object_seq *args, lky_object *func)
{
    lky_object *from = (lky_object *)args->value;

    if(from->type == LBI_INTEGER)
        return from;

    lky_object_custom *b = (lky_object_custom *)from;
//    if(from->type == LBI_FLOAT)
//        return(lobjb_build_int((long)b->value.d));

//    if(from->type != LBI_STRING)
//        return &lky_nil;

    long val;
    sscanf(b->data, "%ld", &val);

    return lobjb_build_int(val);
}

/*
lky_object *stlcon_to_float(lky_object_seq *args, lky_object *func)
{
    lky_object *from = (lky_object *)args->value;

    if(from->type == LBI_FLOAT)
        return from;

    if(from->type == LBI_INTEGER)
        return lobjb_build_int
*/

lky_object *stlcon_to_string(lky_object_seq *args, lky_object *func)
{
    lky_object *from = (lky_object *)args->value;
    char *str = lobjb_stringify(from);
    lky_object *ret = stlstr_cinit(str);
    free(str);
    return ret;
}

lky_object *stlcon_get_class()
{
    lky_object *obj = lobj_alloc();

    lobj_set_member(obj, "toInt", lobjb_build_func_ex(obj, 1, (lky_function_ptr)stlcon_to_int));
    lobj_set_member(obj, "toString", lobjb_build_func_ex(obj, 1, (lky_function_ptr)stlcon_to_string));

    return obj;
}
