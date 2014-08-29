#include "stl_requisitions.h"
#include <dlfcn.h>
#include <stdlib.h>
#include <string.h>

lky_mempool dlmempool = {NULL, &dlclose};

lky_object *stlreq_import(lky_object_seq *args, lky_object_function *func)
{
    lky_object_custom *nobj = (lky_object_custom *)args->value;
    char *pname = nobj->data;
    char *name = malloc(strlen(pname) + 6);
    char *shortname = malloc(strlen(pname) + 1);
    char *shrtloc = NULL;

    int i;
    for(i = strlen(pname); i >= 0; i--)
    {
        if(pname[i] != '/')
        {
            shortname[i] = pname[i];
            continue;
        }

        shrtloc = shortname + i + 1;
    }
    if(!shrtloc)
        shrtloc = shortname;

    char *initname = malloc(strlen(shrtloc) + 6);
    sprintf(initname, "%s_init", shrtloc);

    free(shortname);

    sprintf(name, "./%s.so", pname);

    void *lib_handle;
    lky_object *(*init_func)();

    lib_handle = dlopen(name, RTLD_LAZY);
    if(!lib_handle)
    {
        printf("Couldn't load library.\n");
        // TODO: Error.
    }

    init_func = dlsym(lib_handle, initname);
    free(name);
    free(initname);

    char *error;
    if((error = dlerror()))
    {
        printf("Couldn't load init function.\n");
        // TODO: Error.
    }

    lky_object *obj = (*init_func)();
    pool_add(&dlmempool, lib_handle);

    return obj;
}

lky_object *stlreq_compile(lky_object_seq *args, lky_object *func)
{
    lky_object_custom *fileobj = (lky_object_custom *)args->value;
    char *filename = fileobj->data;

    unsigned long lastnum = strlen(filename) - 1;
    char *objname = calloc(lastnum + 2, sizeof(char));
    char *soname = calloc(lastnum + 3, sizeof(char));

    strcpy(objname, filename);
    strcpy(soname, filename);

    objname[lastnum] = 'o';
    soname[lastnum] = 's';
    soname[lastnum + 1] = 'o';

    char *compcmd = calloc(1000, sizeof(char));
    sprintf(compcmd, "gcc -fPIC -c %s -o %s", filename, objname);
    system(compcmd);
    sprintf(compcmd, "gcc -shared -o %s %s", soname, objname);
    system(compcmd);

    free(soname);
    free(objname);
    
    return &lky_nil;
}

lky_object *stlreq_get_class()
{
    lky_object *obj = lobj_alloc();
    
    lobj_set_member(obj, "import", lobjb_build_func_ex(obj, 1, (lky_function_ptr)&stlreq_import));
    lobj_set_member(obj, "compile", lobjb_build_func_ex(obj, 1, (lky_function_ptr)&stlreq_compile));
    
    return obj;
}
