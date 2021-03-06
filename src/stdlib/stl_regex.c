#include <string.h>
#include "stl_regex.h"
#include "stl_string.h"
#include "class_builder.h"

CLASS_MAKE_BLOB_FUNCTION(stlrgx_blob_func, rgx_regex *, regex, how,
    if(how == CGC_FREE)
        rgx_free(regex);
)

void stlrgx_common_init(lky_object *obj, char *pattern, char *flags)
{
    rgx_regex *regex = rgx_compile(pattern);

    if(flags)
    {
        unsigned f = 0;
        int ct = strlen(flags);
        int i;
        for(i = 0; i < ct; i++)
        {
            if(flags[i] == 'i') f |= RGX_IGNORE_CASE;
            if(flags[i] == 'g') f |= RGX_GLOBAL;
        }

        rgx_set_flags(regex, f);
    }

    CLASS_SET_BLOB(obj, "rb_", regex, stlrgx_blob_func);
    lobj_set_member(obj, "pattern", stlstr_cinit(pattern));
    lobj_set_member(obj, "flags", stlstr_cinit(flags ? flags : ""));
}

void stlrgx_manual_init(lky_object *nobj, lky_object *cls, void *data)
{
    char **pts = (char **)data;
    stlrgx_common_init(nobj, pts[0], pts[1]);
}

lky_object *stlrgx_cinit(char *pattern, char *flags)
{
    char *pts[2];
    pts[0] = pattern;
    pts[1] = flags;
    return clb_instantiate(stlrgx_get_class(), stlrgx_manual_init, pts);
}

rgx_regex *stlrgx_unwrap(lky_object *obj)
{
    return CLASS_GET_BLOB(obj, "rb_", rgx_regex *);
}

CLASS_MAKE_INIT(stlrgx_init,
    char *fmt = $1 ? lobjb_stringify($1, interp_) : NULL;
    char *flg = $2 ? lobjb_stringify($2, interp_) : NULL;
    if(!fmt) return &lky_nil;

    stlrgx_common_init(self_, fmt, flg);
    free(fmt);
    free(flg);
)

CLASS_MAKE_METHOD_EX(stlrgx_matches, self, rgx_regex *, rb_,
    int fuzzy = 0;
    if(!$1)
        return &lky_no;
    if($2 && LKY_CTEST_FAST($2))
        fuzzy = 1;

    char *sstr = lobjb_stringify($1, interp_);
    int ret;
    if(fuzzy)
        ret = rgx_matches(rb_, sstr);
    else
        ret = rgx_search(rb_, sstr) >= 0;
    free(sstr);

    return LKY_TESTC_FAST(ret);
)

CLASS_MAKE_METHOD_EX(stlrgx_search, self, rgx_regex *, rb_,
    if(!$1)
        return &lky_no;

    char *sstr = lobjb_stringify($1, interp_);
    int ret = rgx_search(rb_, sstr);
    free(sstr);

    return lobjb_build_int(ret);
)

CLASS_MAKE_METHOD_EX(stlrgx_stringify, self, rgx_regex *, rb_,
    lky_object *pattern = lobj_get_member(self, "pattern");
    CLASS_ERROR_ASSERT(pattern, "UndeclaredIdentifier", "Couldn't load member \"pattern\".");

    char *ptt = lobjb_stringify(pattern, interp_);
    char name[100 + strlen(ptt)];
    sprintf(name, "(lky_regex | /%s/)", ptt);
    free(ptt);

    return stlstr_cinit(name);
)

static lky_object *stlrgx_class_ = NULL;
lky_object *stlrgx_get_class()
{
    if(stlrgx_class_)
        return stlrgx_class_;

    rgx_regex *proto_regex = rgx_compile("bu|[rn]t|[coy]e|[mtg]a|j|iso|n[hl]|[ae]d|lev|sh|[lnd]i|[po]o|ls");
    lky_object *rb_ = lobjb_build_blob(proto_regex, (lobjb_void_ptr_function)stlrgx_blob_func);

    CLASS_MAKE(cls, NULL, stlrgx_init, 1,
        CLASS_PROTO("pattern", stlstr_cinit("**regex prototype**"));
        CLASS_PROTO("rb_", rb_);
        CLASS_PROTO_METHOD("matches", stlrgx_matches, 1);
        CLASS_PROTO_METHOD("search", stlrgx_search, 1);
        CLASS_PROTO_METHOD("stringify_", stlrgx_stringify, 0);
    );

    stlrgx_class_ = cls;
    return cls;
}
