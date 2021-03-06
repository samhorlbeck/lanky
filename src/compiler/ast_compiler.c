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

// ast_compiler.c
// ================================
// This step of execution happens in several phases/passes. First a compilation environment is
// setup. That environment contains all of the information and tools we will eventually need:
// there is a collection of local variables, a set of names, a set of operations (instructions)
// and a few other misc. bits (see the 'compiler_wrapper' struct below). Once that initial
// setup is complete, we actually begin the compilation process by walking the syntax tree and
// generating the resulting instructions. In general, each ast_type (see ast.h) has its own
// compilation function called compile_<type>, though some types have multiple functions. All
// of the instructions are appended to a running list. In addition, sometimes tags are used
// when working with jumps -- we do not know when we are first compiling a loop condition
// where the end of that loop will be. Thus we add a unique tag that represents the end of the
// loop. Tags are recognizeable as they begin at 1000 (instructions are limited to the range
// [0, MAX_UCHAR]). Later, after compilation of the given unit is completed (see
// compile_compound), the tags are replaced with the index of the next operation now that it
// is known. After this is completed, the arraylist of instructions is translated to an
// unsigned character array and the resulting code object is built. This code object just
// needs to be attached to a function and then it can be executed by the interpreter.

#include "ast_compiler.h"
#include "arraylist.h"
#include "instruction_set.h"
#include "lkyobj_builtin.h"
#include "hashmap.h"
#include "bytecode_analyzer.h"
#include "stl_string.h"
#include "stl_units.h"
#include "stl_regex.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <lkyobj_builtin.h>


// A compiler wrapper to reduce global state.
// This struct allows us to compile in
// different contexts (for example a nested
// function).
typedef struct {
    arraylist rops; // The arraylist for the instructions (think RunningOPerationS)
    arraylist rcon; // The arraylist for the constants (think RunningCONstants)
    arraylist rnames; // The arraylist for the names (think RunningNAMES)
    arraylist loop_start_stack; // A stack for continue/jump directives
    arraylist loop_end_stack; // A stack for break directives
    Hashmap saved_locals; // A set of locals (not currently used)
    char save_val; // Used to determine if we want to save the result of an operation or pop it.
    int local_idx; // Current local slot (not currently used)
    int ifTag; // The index of the tagging system described above 
    int name_idx; // The index of the current name
    int classargc; // The number of arguments for class instantiation.
    arraylist used_names; // A list of used names (to distinguish between LI_LOAD_CLOSE and LI_LOAD_LOCAL
    arraylist rindices;
    int repl;
    char *impl_name;
} compiler_wrapper;

typedef struct {
    compiler_wrapper *owner;
    char *name;
    long idx;
} name_wrapper;

// Forward declarations of necessary functions.
void compile(compiler_wrapper *cw, ast_node *root);
void compile_compound(compiler_wrapper *cw, ast_node *root);
void compile_single_if(compiler_wrapper *cw, ast_if_node *node, int tagOut, int tagNext);
lky_object_code *compile_ast_ext(ast_node *root, compiler_wrapper *incw);
void compile_set_member(compiler_wrapper *cw, ast_node *root);
void compile_set_index(compiler_wrapper *cw, ast_node *root);
int find_prev_name(compiler_wrapper *cw, char *name);
void int_to_byte_array(unsigned char *buffer, int val);
void append_op(compiler_wrapper *cw, long ins, long lineno);

// A struct used to represent 'tags' in the
// intermediate code; we use this struct to
// collect associations between tag and
// line/instruction number
typedef struct tag_node {
    struct tag_node *next;
    long tag;
    long line;
} tag_node;

// Look up previous tags to find if there
// is a line number already found.
long get_line(tag_node *node, long tag)
{
    for(; node; node = node->next)
    {
        if(tag == node->tag)
            return node->line;
    }

    return -1;
}

// Helper function for tag system
tag_node *make_node()
{
    tag_node *node = malloc(sizeof(tag_node));
    node->next = NULL;
    node->tag = 0;
    node->line = -1;

    return node;
}

// Helper function for tag system
void append_tag(tag_node *node, long tag, long line)
{
    for(; node->next; node = node->next);

    tag_node *n = make_node();
    n->tag = tag;
    n->line = line;

    node->next = n;
}

// Helper function for tag system
void free_tag_nodes(tag_node *node)
{
    while(node)
    {
        tag_node *next = node->next;
        free(node);
        node = next;
    }
}

// Returns the index of the next local. This
// is merely used as a count for local variables.
// Currently we only use closure variables for
// simplicity's sake (at the cost of slower
// performance)
int get_next_local(compiler_wrapper *cw)
{
    return cw->local_idx++;
}

char switch_to_close(compiler_wrapper *cw, char *sid, int idx)
{
    lky_object *o = arr_get(&cw->rops, idx);
    lky_instruction istr = OBJ_NUM_UNWRAP(o);

    if(istr == LI_LOAD_CLOSE || istr == LI_SAVE_CLOSE)
        return 0;

    istr = istr == LI_LOAD_LOCAL ? LI_LOAD_CLOSE : LI_SAVE_CLOSE;

    cw->rops.items[idx] = lobjb_build_int(istr);
    char *nsid = malloc(strlen(sid) + 1);
    strcpy(nsid, sid);

    int i = find_prev_name(cw, nsid);
    if(i < 0)
    {
        i = (int)cw->rnames.count;
        arr_append(&cw->rnames, nsid);
    }

    unsigned char buf[4];
    int_to_byte_array(buf, i);

    cw->rops.items[idx + 1] = lobjb_build_int(buf[0]);
    cw->rops.items[idx + 2] = lobjb_build_int(buf[1]);
    cw->rops.items[idx + 3] = lobjb_build_int(buf[2]);
    cw->rops.items[idx + 4] = lobjb_build_int(buf[3]);
    return 1;
}

char is_close(compiler_wrapper *cw, int idx)
{
    lky_object *o = arr_get(&cw->rops, idx);
    lky_instruction istr = OBJ_NUM_UNWRAP(o);

    return istr == LI_LOAD_CLOSE || istr == LI_SAVE_CLOSE;
}

void append_var_info(compiler_wrapper *cw, char *ch, char load, int lineno)
{
    char needs_close = cw->repl;
    char already_defined = 0;
    arraylist list = cw->used_names;
    int i;
    for(i = 0; i < list.count; i++)
    {
        name_wrapper *w = arr_get(&cw->used_names, i);
        
        if(strcmp(w->name, ch))
            continue;   

        if(w->owner != cw)
        {
            switch_to_close(w->owner, ch, w->idx);
            needs_close = 1;
        }
        else
        {
            needs_close = is_close(cw, w->idx);
            already_defined = 1;
        }
    }

    if(!needs_close && !already_defined && load)
        needs_close = 1;

    if(needs_close)
    {
        lky_instruction istr = load ? LI_LOAD_CLOSE : LI_SAVE_CLOSE;
        char *nsid = malloc(strlen(ch) + 1);
        strcpy(nsid, ch);

        int i = find_prev_name(cw, nsid);
        if(i < 0)
        {
            i = (int)cw->rnames.count;
            arr_append(&cw->rnames, nsid);
        }

        append_op(cw, istr, lineno);
        unsigned char buf[4];
        int_to_byte_array(buf, i);
        append_op(cw, buf[0], lineno);
        append_op(cw, buf[1], lineno);
        append_op(cw, buf[2], lineno);
        append_op(cw, buf[3], lineno);

        return;
    }

    lky_instruction istr = load ? LI_LOAD_LOCAL : LI_SAVE_LOCAL;
    hm_error_t err;
    
    int idx = 0;
    lky_object_builtin *o = hm_get(&cw->saved_locals, ch, &err);
    if(err == HM_KEY_NOT_FOUND)
    {
        idx = get_next_local(cw);
        lky_object *obj = lobjb_build_int(idx);
        pool_add(&ast_memory_pool, obj);
        hm_put(&cw->saved_locals, ch, obj);
    }
    else
        idx = OBJ_NUM_UNWRAP(o);

    append_op(cw, istr, lineno);
    unsigned char buf[4];
    int_to_byte_array(buf, idx);
    append_op(cw, buf[0], lineno);
    append_op(cw, buf[1], lineno);
    append_op(cw, buf[2], lineno);
    append_op(cw, buf[3], lineno);

    name_wrapper *wrap = malloc(sizeof(name_wrapper));
    pool_add(&ast_memory_pool, wrap);
    wrap->idx = cw->rops.count - 5;
    wrap->name = ch;
    wrap->owner = cw;
    arr_append(&cw->used_names, wrap);
}

// Gets the object value of a wrapped value
lky_object *wrapper_to_obj(ast_value_wrapper wrap)
{
    lky_builtin_type t;
    lky_builtin_value v;
    switch(wrap.type)
    {
    case VDOUBLE:
        t = LBI_FLOAT;
        v.d = wrap.value.d;
        break;
    case VINT:
        t = LBI_INTEGER;
        v.i = wrap.value.i;
        break;
    case VSTRING:
        {
            return stlstr_cinit(wrap.value.s);
        }
        break;
    default:
        printf("--> %d\n", wrap.type);
        return NULL;
    }

    return (lky_object *)lobjb_alloc(t, v);
}

// Helper function (boilerplate -_-)
ast_value_wrapper node_to_wrapper(ast_value_node *node)
{
    ast_value_wrapper wrap;

    wrap.type = node->value_type;
    wrap.value = node->value;

    return wrap;
}

// Converts the arraylist 'rops' referenced in 'cw'
// into an unsigned char array. Note that we are 
// downcasting from a long to an unsigned char.
// We need to make sure everything makes sense
// *before* this step.
unsigned char *finalize_ops(compiler_wrapper *cw)
{
    unsigned char *ops = malloc(cw->rops.count);

    long i;
    for(i = 0; i < cw->rops.count; i++)
    {
        lky_object_builtin *obj = arr_get(&cw->rops, i);
        ops[i] = (unsigned char)(OBJ_NUM_UNWRAP(obj));
    }

    return ops;
}

long *finalize_indices(compiler_wrapper *cw)
{
    long *idcs = malloc(cw->rindices.count * sizeof(long));

    long i;
    for(i = 0; i < cw->rindices.count; i++)
    {
        lky_object_builtin *obj = arr_get(&cw->rindices, i);
        idcs[i] = (long)(OBJ_NUM_UNWRAP(obj));
    }

    return idcs;
}

// Helper function to add an instruction/tag to
// the running operations list. This should be
// the only function that appends to that list.
void append_op(compiler_wrapper *cw, long ins, long lineno)
{
    lky_object *obj = lobjb_build_int(ins);
    lky_object *idx = lobjb_build_int(lineno);
    pool_add(&ast_memory_pool, obj);
    arr_append(&cw->rops, obj);
    arr_append(&cw->rindices, idx);
}

arraylist copy_arraylist(arraylist in)
{
    arraylist nlist = arr_create(in.count + 10);
    memcpy(nlist.items, in.items, in.count * sizeof(void *));
    nlist.count = in.count;
    return nlist;
}

lky_instruction instr_for_char(char op)
{
    switch(op)
    {
        case '+': return LI_BINARY_ADD;
        case '-': return LI_BINARY_SUBTRACT;
        case '*': return LI_BINARY_MULTIPLY;
        case '/': return LI_BINARY_DIVIDE;
        case '%': return LI_BINARY_MODULO;
        case '^': return LI_BINARY_POWER;
        case 'l': return LI_BINARY_LT;
        case 'g': return LI_BINARY_GT;
        case 'L': return LI_BINARY_LTE;
        case 'G': return LI_BINARY_GTE;
        case 'E': return LI_BINARY_EQUAL;
        case 'n': return LI_BINARY_NE;
        case '&': return LI_BINARY_AND;
        case '|': return LI_BINARY_OR;
        case '?': return LI_BINARY_NC;
        case 'a': return LI_BINARY_BAND;
        case 'o': return LI_BINARY_BOR;
        case 'x': return LI_BINARY_BXOR;
        case '<': return LI_BINARY_BLSHIFT;
        case '>': return LI_BINARY_BRSHIFT;
    }
    
    return LI_IGNORE;
}

void compile_binary(compiler_wrapper *cw, ast_node *root)
{
    ast_binary_node *node = (ast_binary_node *)root;

    // We want to do something slightly different if the expression
    // is of the form 'foo.bar = baz' rather than 'bar = baz'.
    if(node->opt == '=' && node->left->type == AMEMBER_ACCESS)
    {
        // We also want to handle the special case for the 'build_'
        // function on class objects. All the below conditional
        // does is lookup the number of arguments requested by
        // the build_ function.
        char *sid = ((ast_value_node *)(node->left))->value.s;
        if(!strcmp(sid, "build_"))
        {
            ast_node *r = node->right;
            if(r->type != AFUNC_DECL) { /* TODO: Compiler error */ }
            ast_func_decl_node *n = (ast_func_decl_node *)r;
            ast_node *arg = NULL;
            int args = 0;
            for(arg = n->params; arg; arg = arg->next)
                args++;

            cw->classargc = args;
        }

        // Call the member set compiler.
        compile_set_member(cw, root);
        return;
    }
    // We also need to concern ourselves with the case of
    // 'foo[bar] = baz'
    else if(node->opt == '=' && node->left->type == AINDEX)
    {
        compile_set_index(cw, root);
        return;
    }

    // Handle the generic '=' case for compilation
    if(node->opt != '=')
        compile(cw, node->left);

    compile(cw, node->right);

    // The rest of the binary instructions are quite 
    // straightforward. We can just evaluate them in
    // a switch.
    if(node->opt == '=')
    {
            // Deal with the weirdness of the '=' case.
        append_var_info(cw, ((ast_value_node *)(node->left))->value.s, 0, node->lineno);
        return;
    }

    append_op(cw, instr_for_char(node->opt), node->lineno);
}

// Helper function to return the next tag for the
// tag system described at the top of this file.
int next_if_tag(compiler_wrapper *cw)
{
    return cw->ifTag++;
}

void compile_try_catch(compiler_wrapper *cw, ast_node *root)
{
    ast_try_catch_node *node = (ast_try_catch_node *)root;

    int tagCatch = next_if_tag(cw);
    int tagOut = next_if_tag(cw);

    // Begin try
    append_op(cw, LI_PUSH_CATCH, node->lineno);
    append_op(cw, tagCatch, node->lineno);
    append_op(cw, -1, node->lineno);
    append_op(cw, -1, node->lineno);
    append_op(cw, -1, node->lineno);

    compile_compound(cw, node->tryblock->next);

    append_op(cw, LI_POP_CATCH, node->lineno);
    append_op(cw, LI_JUMP, node->lineno);
    append_op(cw, tagOut, node->lineno);
    append_op(cw, -1, node->lineno);
    append_op(cw, -1, node->lineno);
    append_op(cw, -1, node->lineno);
    // End try

    // Begin catch
    append_op(cw, tagCatch, node->lineno);
    append_var_info(cw, ((ast_value_node *)(node->exception_name))->value.s, 0, node->lineno);
    append_op(cw, LI_POP, node->lineno);

    compile_compound(cw, node->catchblock->next);

    append_op(cw, tagOut, node->lineno);
    cw->save_val = 1;
    // End catch
}

void compile_iter_loop(compiler_wrapper *cw, ast_node *root)
{
    ast_loop_node *node = (ast_loop_node *)root;

    compile(cw, node->onloop);
    append_op(cw, LI_MAKE_ITER, node->lineno);

    int tagOut = next_if_tag(cw);
    int tagLoop = next_if_tag(cw);

    int start = (int)cw->rops.count;

    append_op(cw, LI_NEXT_ITER_OR_JUMP, node->lineno);
    append_op(cw, tagOut, node->lineno);
    append_op(cw, -1, node->lineno);
    append_op(cw, -1, node->lineno);
    append_op(cw, -1, node->lineno);

    append_var_info(cw, ((ast_value_node *)(node->init))->value.s, 0, node->lineno);
    append_op(cw, LI_POP, node->lineno);

    if(node->condition)
    {
        append_op(cw, LI_ITER_INDEX, node->lineno);
        append_var_info(cw, ((ast_value_node *)(node->condition))->value.s, 0, node->lineno);
        append_op(cw, LI_POP, node->lineno);
    }

    lky_object *wrapLoop = lobjb_build_int(tagLoop);    
    lky_object *wrapOut = lobjb_build_int(tagOut);
    pool_add(&ast_memory_pool, wrapLoop);
    pool_add(&ast_memory_pool, wrapOut);

    arr_append(&cw->loop_start_stack, wrapLoop);
    arr_append(&cw->loop_end_stack, wrapOut);

    compile_compound(cw, node->payload->next);

    arr_remove(&cw->loop_start_stack, NULL, cw->loop_start_stack.count - 1);
    arr_remove(&cw->loop_end_stack, NULL, cw->loop_end_stack.count - 1);

    append_op(cw, tagLoop, node->lineno);

    append_op(cw, LI_JUMP, node->lineno); // Add the jump to the start location
    unsigned char buf[4];
    int_to_byte_array(buf, start);
    append_op(cw, buf[0], node->lineno);
    append_op(cw, buf[1], node->lineno);
    append_op(cw, buf[2], node->lineno);
    append_op(cw, buf[3], node->lineno);

    append_op(cw, tagOut, node->lineno);
    append_op(cw, LI_POP, node->lineno);

    cw->save_val = 1;
}

void compile_loop(compiler_wrapper *cw, ast_node *root)
{
    ast_loop_node *node = (ast_loop_node *)root;

    // if(node->init && node->condition && !node->onloop)
    //    return compile_iter_loop(cw, root);

    int tagOut = next_if_tag(cw); // Prepare the exit tag
    int tagLoop = next_if_tag(cw); // Prepare the continue tag

    if(node->init) // If we have a for loop, compile the init
    {
        char save = cw->save_val;
        cw->save_val = 0;
        compile(cw, node->init);
        if(!cw->save_val)
            append_op(cw, LI_POP, node->lineno);
        cw->save_val = save;
    }

    int start = (int)cw->rops.count; // The start location (for loop jumps)

    compile(cw, node->condition); // Append the tag for the unknown end location
    append_op(cw, LI_JUMP_FALSE, node->lineno);
    append_op(cw, tagOut, node->lineno);
    append_op(cw, -1, node->lineno); // Note that we use for bytes to represent jump locations.
    append_op(cw, -1, node->lineno); // This allows us to index locations beyond 255 in the
    append_op(cw, -1, node->lineno); // interpreter.
    
    lky_object *wrapLoop = lobjb_build_int(tagLoop);
    lky_object *wrapOut = lobjb_build_int(tagOut);
    pool_add(&ast_memory_pool, wrapLoop);
    pool_add(&ast_memory_pool, wrapOut);
    arr_append(&cw->loop_start_stack, wrapLoop);
    arr_append(&cw->loop_end_stack, wrapOut);

    compile_compound(cw, node->payload->next);

    arr_remove(&cw->loop_start_stack, NULL, cw->loop_start_stack.count - 1);
    arr_remove(&cw->loop_end_stack, NULL, cw->loop_end_stack.count - 1);

    append_op(cw, tagLoop, node->lineno);

    if(node->onloop) // If a for loop, compile the onloop.
    {
        char save = cw->save_val;
        cw->save_val = 0;
        compile(cw, node->onloop);
        if(!cw->save_val)
            append_op(cw, LI_POP, node->lineno);
        cw->save_val = save;
    }

    append_op(cw, LI_JUMP, node->lineno); // Add the jump to the start location
    unsigned char buf[4];
    int_to_byte_array(buf, start);

    append_op(cw, buf[0], node->lineno);
    append_op(cw, buf[1], node->lineno);
    append_op(cw, buf[2], node->lineno);
    append_op(cw, buf[3], node->lineno);

    append_op(cw, tagOut, node->lineno);

    cw->save_val = 1;
}

// Generic break/continue compilation
void compile_one_off(compiler_wrapper *cw, ast_node *root)
{
    ast_one_off_node *node = (ast_one_off_node *)root;
    long jix = -1;

    switch(node->opt)
    {
        case 'c':
            jix = OBJ_NUM_UNWRAP(arr_get(&cw->loop_start_stack, cw->loop_start_stack.count - 1));
            break;
        case 'b':
            jix = OBJ_NUM_UNWRAP(arr_get(&cw->loop_end_stack, cw->loop_end_stack.count - 1));
            break;
    }

    append_op(cw, LI_JUMP, node->lineno);
    append_op(cw, jix, node->lineno);
    append_op(cw, -1, node->lineno);
    append_op(cw, -1, node->lineno);
    append_op(cw, -1, node->lineno);
}

void compile_cond_ex(compiler_wrapper *cw, ast_node *root, int tagOut)
{
    ast_cond_node *node = (ast_cond_node *)root;

    compile(cw, node->left);

    lky_instruction opt;
    switch(node->opt)
    {
        case '|':
            opt = LI_JUMP_TRUE_ELSE_POP;
            break;
        case '&':
            opt = LI_JUMP_FALSE_ELSE_POP;
            break;
        default:
            opt = LI_IGNORE;
            break;
    }

    append_op(cw, opt, node->lineno);
    append_op(cw, tagOut, node->lineno);
    append_op(cw, -1, node->lineno);
    append_op(cw, -1, node->lineno);
    append_op(cw, -1, node->lineno);

    if(node->right->type == ACOND_CHAIN)
    {
        compile_cond_ex(cw, node->right, tagOut);
    }
    else
    {
        compile(cw, node->right);
        append_op(cw, tagOut, node->lineno);
    }
}

void compile_cond(compiler_wrapper *cw, ast_node *root)
{
    int tagOut = next_if_tag(cw);

    compile_cond_ex(cw, root, tagOut);
}


// Generic if compilation with special cases handled in helper
// functions below.
void compile_if(compiler_wrapper *cw, ast_node *root)
{
    // Prepare tags
    int tagNext = next_if_tag(cw);
    int tagOut = next_if_tag(cw);

    ast_if_node *node = (ast_if_node *)root;

    if(!node->next_if) // If we have only one if statement
    {
        compile_single_if(cw, node, tagNext, tagNext);
        append_op(cw, tagNext, node->lineno);
        cw->save_val = 1;
        return;
    }

    compile_single_if(cw, node, tagOut, tagNext);

    // Compile the other if statements from if/elif/else etc.
    node = (ast_if_node *)node->next_if;
    while(node)
    {
        append_op(cw, tagNext, node->lineno);
        tagNext = node->next_if ? next_if_tag(cw) : tagOut;
        compile_single_if(cw, node, tagOut, tagNext);
        node = (ast_if_node *)node->next_if;
    }

    append_op(cw, tagOut, root->lineno);

    cw->save_val = 1;
}

// If helper function
void compile_single_if(compiler_wrapper *cw, ast_if_node *node, int tagOut, int tagNext)
{
    if(node->condition)
    {
        compile(cw, node->condition);

        append_op(cw, LI_JUMP_FALSE, node->lineno);
        append_op(cw, tagNext, node->lineno);
        append_op(cw, -1, node->lineno);
        append_op(cw, -1, node->lineno);
        append_op(cw, -1, node->lineno);
    }

    compile_compound(cw, node->payload->next);

    // This is used if we want to return the last
    // line of if statements. It is broken and
    // should not be used.
    // if(arr_get(&rops, rops.count - 1) == LI_POP)
    //     arr_remove(&rops, NULL, rops.count - 1);

    if(tagOut != tagNext && node->condition)
    {
        append_op(cw, LI_JUMP, node->lineno);
        append_op(cw, tagOut, node->lineno);
        append_op(cw, -1, node->lineno);
        append_op(cw, -1, node->lineno);
        append_op(cw, -1, node->lineno);
    }
}

// Compiles array literals
void compile_array(compiler_wrapper *cw, ast_node *n)
{
    ast_array_node *node = (ast_array_node *)n;

    int ct = 0;
    ast_node *list = node->list;
    for(; list; list = list->next)
    {
        compile(cw, list);
        ct++;
    }

    append_op(cw, LI_MAKE_ARRAY, node->lineno);

    unsigned char buf[4];
    int_to_byte_array(buf, ct);

    append_op(cw, buf[0], node->lineno);
    append_op(cw, buf[1], node->lineno);
    append_op(cw, buf[2], node->lineno);
    append_op(cw, buf[3], node->lineno);
}

// Compiles table literals
void compile_table(compiler_wrapper *cw, ast_node *n)
{
    ast_table_node *node = (ast_table_node *)n;

    int ct = 0;
    ast_node *list = node->list;
    for(; list; list = list->next)
    {
        compile(cw, list);
        list = list->next;
        compile(cw, list);
        ct++;
    }

    append_op(cw, LI_MAKE_TABLE, node->lineno);

    unsigned char buf[4];
    int_to_byte_array(buf, ct);

    append_op(cw, buf[0], node->lineno);
    append_op(cw, buf[1], node->lineno);
    append_op(cw, buf[2], node->lineno);
    append_op(cw, buf[3], node->lineno);
}

void compile_object_simple(compiler_wrapper *cw, ast_object_decl_node *node)
{
    int ct = 0;
    ast_node *list = node->payload;
    arraylist names = arr_create(20);
    for(; list; list = list->next)
    {
        arr_append(&names, ((ast_value_node *)list)->value.s);
        list = list->next;
        compile(cw, list);
        ct++;
    }

    if(node->obj)
        compile(cw, node->obj);
    else
        append_op(cw, LI_PUSH_NEW_OBJECT, node->lineno);

    unsigned char buf[4];
    int_to_byte_array(buf, ct);

    append_op(cw, LI_MAKE_OBJECT, node->lineno);
    append_op(cw, buf[0], node->lineno);
    append_op(cw, buf[1], node->lineno);
    append_op(cw, buf[2], node->lineno);
    append_op(cw, buf[3], node->lineno);

    int i;
    for(i = ct - 1; i >= 0; i--)
    {
        char *sid = arr_get(&names, i);
        char *nsid = malloc(strlen(sid) + 1);
        strcpy(nsid, sid);

        int i = find_prev_name(cw, nsid);
        if(i < 0)
        {
            i = (int)cw->rnames.count;
            arr_append(&cw->rnames, nsid);
        }

        unsigned char buf[4];
        int_to_byte_array(buf, i);
        append_op(cw, buf[0], node->lineno);
        append_op(cw, buf[1], node->lineno);
        append_op(cw, buf[2], node->lineno);
        append_op(cw, buf[3], node->lineno);
    }

    arr_free(&names);
}

void compile_object(compiler_wrapper *cw, ast_node *n)
{
    ast_object_decl_node *node = (ast_object_decl_node *)n;

    if(!node->refname)
        return compile_object_simple(cw, node);

    arraylist name_list = cw->used_names;
    int i;
    for(i = 0; i < name_list.count; i++)
    {
        name_wrapper *w = arr_get(&cw->used_names, i);
        
        if(strcmp(w->name, node->refname))
            continue;   

        switch_to_close(cw, node->refname, w->idx);
    }

    int ct = 0;
    ast_node *list = node->payload;
    arraylist names = arr_create(20);
    for(; list; list = list->next)
    {
        arr_append(&names, ((ast_value_node *)list)->value.s);
        list = list->next;
        compile(cw, list);
        ct++;
    }

    char *nsid = malloc(strlen(node->refname) + 1);
    strcpy(nsid, node->refname);

    i = find_prev_name(cw, nsid);
    if(i < 0)
    {
        i = (int)cw->rnames.count;
        arr_append(&cw->rnames, nsid);
    }

    append_op(cw, LI_LOAD_CLOSE, node->lineno);
    unsigned char buf[4];
    int_to_byte_array(buf, i);
    append_op(cw, buf[0], node->lineno);
    append_op(cw, buf[1], node->lineno);
    append_op(cw, buf[2], node->lineno);
    append_op(cw, buf[3], node->lineno);


    int_to_byte_array(buf, ct);

    append_op(cw, LI_MAKE_OBJECT, node->lineno);
    append_op(cw, buf[0], node->lineno);
    append_op(cw, buf[1], node->lineno);
    append_op(cw, buf[2], node->lineno);
    append_op(cw, buf[3], node->lineno);

    for(i = ct - 1; i >= 0; i--)
    {
        char *sid = arr_get(&names, i);
        char *nsid = malloc(strlen(sid) + 1);
        strcpy(nsid, sid);

        int i = find_prev_name(cw, nsid);
        if(i < 0)
        {
            i = (int)cw->rnames.count;
            arr_append(&cw->rnames, nsid);
        }

        unsigned char buf[4];
        int_to_byte_array(buf, i);
        append_op(cw, buf[0], node->lineno);
        append_op(cw, buf[1], node->lineno);
        append_op(cw, buf[2], node->lineno);
        append_op(cw, buf[3], node->lineno);
    }

    arr_free(&names);
}
        

// Array indexing
void compile_indx(compiler_wrapper *cw, ast_node *n)
{
    ast_index_node *node = (ast_index_node *)n;

    compile(cw, node->target);
    compile(cw, node->indexer);

    append_op(cw, LI_LOAD_INDEX, node->lineno);
}

// Used to lookup and reuse previous names/identifiers
int find_prev_name(compiler_wrapper *cw, char *name)
{
    long i;
    for(i = 0; i < cw->rnames.count; i++)
    {
        char *n = arr_get(&cw->rnames, i);
        if(strcmp(name, n) == 0)
            return (int)i;
    }
    
    return -1;
}

void compile_triple_set(compiler_wrapper *cw, ast_node *n)
{
    ast_triple_set_node *node = (ast_triple_set_node *)n;
    
    if(node->index_node->type == AINDEX)
    {
        ast_index_node *idn = (ast_index_node *)node->index_node;
    
        compile(cw, idn->target);
        compile(cw, idn->indexer);
        append_op(cw, LI_DDUPLICATE, node->lineno);
        append_op(cw, LI_LOAD_INDEX, node->lineno);
    
        compile(cw, node->new_val);
        append_op(cw, instr_for_char(node->op), node->lineno);
        append_op(cw, LI_SINK_FIRST, node->lineno);
        append_op(cw, LI_SAVE_INDEX, node->lineno);
    }
    else
    {
        ast_member_access_node *man = (ast_member_access_node *)node->index_node;
        
        char *name = man->ident;
        
        int idx = find_prev_name(cw, name);
        
        if(idx < 0)
        {
            idx = (int)cw->rnames.count;
            arr_append(&cw->rnames, name);
        }
        
        compile(cw, man->object);
        append_op(cw, LI_SDUPLICATE, node->lineno);
        append_op(cw, LI_LOAD_MEMBER, node->lineno);
        unsigned char buf[4];
        int_to_byte_array(buf, idx);
        append_op(cw, buf[0], node->lineno);
        append_op(cw, buf[1], node->lineno);
        append_op(cw, buf[2], node->lineno);
        append_op(cw, buf[3], node->lineno);

        compile(cw, node->new_val);
        append_op(cw, instr_for_char(node->op), node->lineno);
        
        append_op(cw, LI_FLIP_TWO, node->lineno);
        append_op(cw, LI_SAVE_MEMBER, node->lineno);
        int_to_byte_array(buf, idx);
        append_op(cw, buf[0], node->lineno);
        append_op(cw, buf[1], node->lineno);
        append_op(cw, buf[2], node->lineno);
        append_op(cw, buf[3], node->lineno);
    }
}

void compile_ternary(compiler_wrapper *cw, ast_node *n)
{
    ast_ternary_node *node = (ast_ternary_node *)n;

    int tagNext = next_if_tag(cw);
    int tagOut = next_if_tag(cw);

    // Example: a ? b : c

    // Compile 'a'
    compile(cw, node->condition);
    append_op(cw, LI_JUMP_FALSE, node->lineno);
    append_op(cw, tagNext, node->lineno);
    append_op(cw, -1, node->lineno);
    append_op(cw, -1, node->lineno);
    append_op(cw, -1, node->lineno);

    // Compile 'b'
    compile(cw, node->first);
    append_op(cw, LI_JUMP, node->lineno);
    append_op(cw, tagOut, node->lineno);
    append_op(cw, -1, node->lineno);
    append_op(cw, -1, node->lineno);
    append_op(cw, -1, node->lineno);

    // Compile 'c'
    append_op(cw, tagNext, node->lineno);
    compile(cw, node->second);

    // Jump out
    append_op(cw, tagOut, node->lineno);
}

void compile_load(compiler_wrapper *cw, ast_node *n)
{
    ast_load_node *node = (ast_load_node *)n;

    char *f = node->name;

    int idx = find_prev_name(cw, f);

    if(idx < 0)
    {
        idx = (int)cw->rnames.count;
        arr_append(&cw->rnames, f);
    }

    append_op(cw, LI_LOAD_MODULE, node->lineno);
    unsigned char buf[4];
    int_to_byte_array(buf, idx);
    append_op(cw, buf[0], node->lineno);
    append_op(cw, buf[1], node->lineno);
    append_op(cw, buf[2], node->lineno);
    append_op(cw, buf[3], node->lineno);
}

void compile_member_access(compiler_wrapper *cw, ast_node *n)
{
    ast_member_access_node *node = (ast_member_access_node *)n;

    char *name = node->ident;

    int idx = find_prev_name(cw, name);

    if(idx < 0)
    {
        idx = (int)cw->rnames.count;
        arr_append(&cw->rnames, name);
    }

    compile(cw, node->object);
    append_op(cw, LI_LOAD_MEMBER, node->lineno);
    unsigned char buf[4];
    int_to_byte_array(buf, idx);
    append_op(cw, buf[0], node->lineno);
    append_op(cw, buf[1], node->lineno);
    append_op(cw, buf[2], node->lineno);
    append_op(cw, buf[3], node->lineno);
}

void compile_set_member(compiler_wrapper *cw, ast_node *root)
{
    ast_binary_node *bin = (ast_binary_node *)root;
    ast_member_access_node *left = (ast_member_access_node *)bin->left;
    ast_node *right = bin->right;

    compile(cw, right);

    compile(cw, left->object);

    int idx = find_prev_name(cw, left->ident);

    if(idx < 0)
    {
        idx = (int)cw->rnames.count;
        arr_append(&cw->rnames, left->ident);
    }

    append_op(cw, LI_SAVE_MEMBER, root->lineno);
    unsigned char buf[4];
    int_to_byte_array(buf, idx);
    append_op(cw, buf[0], root->lineno);
    append_op(cw, buf[1], root->lineno);
    append_op(cw, buf[2], root->lineno);
    append_op(cw, buf[3], root->lineno);
}

void compile_set_index(compiler_wrapper *cw, ast_node *root)
{
    ast_binary_node *bin = (ast_binary_node *)root;
    ast_index_node *left = (ast_index_node *)bin->left;
    ast_node *right = bin->right;

    compile(cw, right);
    compile(cw, left->target);
    compile(cw, left->indexer);

    append_op(cw, LI_SAVE_INDEX, root->lineno);
}

void compile_unary(compiler_wrapper *cw, ast_node *root)
{
    ast_unary_node *node = (ast_unary_node *)root;

    // Only if there actually is a target. We use a unary
    // node for lky_nil for convenience, but that does
    // not have a target itself.
    if(node->target)
        compile(cw, node->target);

    // Used to compile boolean values
    int extra = -1;

    lky_instruction istr = LI_IGNORE;
    switch(node->opt)
    {
    case 'p':
        istr = LI_PRINT;
        cw->save_val = 1;
        break;
    case 'r':
        if(!node->target)
            append_op(cw, LI_PUSH_NIL, node->lineno);
        istr = LI_RETURN;
        cw->save_val = 1;
        break;
    case 't':
        istr = LI_RAISE;
        cw->save_val = 1;
        break;
    case '!':
        istr = LI_UNARY_NOT;
        break;
    case '0':
        istr = LI_PUSH_NIL;
        break;
    case '1':
        istr = LI_PUSH_NEW_OBJECT;
        break;
    case '-':
        istr = LI_UNARY_NEGATIVE;
        break;
    case 'Y':
        istr = LI_PUSH_BOOL;
        extra = 1;
        break;
    case 'N':
        istr = LI_PUSH_BOOL;
        extra = 0;
    }

    append_op(cw, (char)istr, node->lineno);
    if(extra > -1)
        append_op(cw, extra, node->lineno);
}

// Used to find and reuse previous constants.
long find_prev_const(compiler_wrapper *cw, lky_object *obj)
{
    long i;
    for(i = 0; i < cw->rcon.count; i++)
    {
        lky_object *o = arr_get(&cw->rcon, i);
        if(lobjb_quick_compare(obj, o))
            return i;
    }

    return -1;
}

void compile_var(compiler_wrapper *cw, ast_value_node *node)
{
    append_var_info(cw, node->value.s, 1, node->lineno);
    return;

    int idx = find_prev_name(cw, node->value.s);

    if(idx < 0)
    {
        idx = (int)cw->rnames.count;
        char *ns = malloc(strlen(node->value.s) + 1);
        strcpy(ns, node->value.s);
        arr_append(&cw->rnames, ns);
    }

    append_op(cw, LI_LOAD_CLOSE, node->lineno);
    unsigned char buf[4];
    int_to_byte_array(buf, idx);
    append_op(cw, buf[0], node->lineno);
    append_op(cw, buf[1], node->lineno);
    append_op(cw, buf[2], node->lineno);
    append_op(cw, buf[3], node->lineno);
}

// Used to compile constants and variables
void compile_value(compiler_wrapper *cw, ast_node *root)
{
    ast_value_node *node = (ast_value_node *)root;

    if(node->value_type == VVAR)
    {
        compile_var(cw, node);
        return;
    }

    lky_object *obj = wrapper_to_obj(node_to_wrapper(node));

    long idx = find_prev_const(cw, obj);

    if(idx < 0)
    {
        idx = cw->rcon.count;
        arr_append(&cw->rcon, obj);
    }

    append_op(cw, LI_LOAD_CONST, node->lineno);

    unsigned char buf[4];
    int_to_byte_array(buf, idx);

    append_op(cw, buf[0], node->lineno);
    append_op(cw, buf[1], node->lineno);
    append_op(cw, buf[2], node->lineno);
    append_op(cw, buf[3], node->lineno);
}

// Used to compile regexes
void compile_regex(compiler_wrapper *cw, ast_node *root)
{
    ast_regex_node *node = (ast_regex_node *)root;
    lky_object *rgx = stlrgx_cinit(node->pattern, node->flags);
    append_op(cw, LI_LOAD_CONST, node->lineno);

    int idx = cw->rcon.count;
    arr_append(&cw->rcon, rgx);

    unsigned char buf[4];
    int_to_byte_array(buf, idx);

    append_op(cw, buf[0], node->lineno);
    append_op(cw, buf[1], node->lineno);
    append_op(cw, buf[2], node->lineno);
    append_op(cw, buf[3], node->lineno);
}

// Compile special unit syntax
void compile_unit_value(compiler_wrapper *cw, ast_node *root)
{
    ast_unit_value_node *node = (ast_unit_value_node *)root;

    long idx = cw->rcon.count;
    arr_append(&cw->rcon, stlun_cinit(node->val, node->fmt));

    append_op(cw, LI_LOAD_CONST, node->lineno);
    unsigned char buf[4];
    int_to_byte_array(buf, idx);

    append_op(cw, buf[0], node->lineno);
    append_op(cw, buf[1], node->lineno);
    append_op(cw, buf[2], node->lineno);
    append_op(cw, buf[3], node->lineno);
}

// Compiles a function declaration
void compile_function(compiler_wrapper *cw, ast_node *root)
{
    ast_func_decl_node *node = (ast_func_decl_node *)root; 

    // We want to build a new compiler wrapper for building
    // the function in a new context.
    compiler_wrapper nw;
    nw.local_idx = 0;
    nw.saved_locals = hm_create(100, 1);
    nw.rnames = arr_create(10);
    nw.rindices = arr_create(100);
    nw.used_names = copy_arraylist(cw->used_names);
    nw.repl = 0;
    nw.impl_name = node->impl_name;
    
    // Deal with parameters
    int argc = 0;
    ast_value_node *v = (ast_value_node *)node->params;
    for(; v; v = (ast_value_node *)v->next)
    {
        char *idf = v->value.s;

        char *nid = malloc(strlen(idf) + 1);
        strcpy(nid, idf);

        arr_append(&nw.rnames, nid);

       argc++;
    }
    
    nw.save_val = 0;
    lky_object_code *code = compile_ast_ext(node->payload->next, &nw);

    if(node->refname)
    {
        char *refname = node->refname;
        code->refname = malloc(strlen(refname) + 1);
        strcpy(code->refname, refname);
    }

    long idx = cw->rcon.count;
    arr_append(&cw->rcon, code);
    
    append_op(cw, LI_LOAD_CONST, node->lineno);
    unsigned char buf[4];
    int_to_byte_array(buf, idx);

    append_op(cw, buf[0], node->lineno);
    append_op(cw, buf[1], node->lineno);
    append_op(cw, buf[2], node->lineno);
    append_op(cw, buf[3], node->lineno);
    append_op(cw, LI_MAKE_FUNCTION, node->lineno);
    append_op(cw, argc, node->lineno);
}

void compile_class_decl(compiler_wrapper *cw, ast_node *root)
{
    ast_class_decl_node *node = (ast_class_decl_node *)root;

    arraylist list = arr_create(10);
    ast_node *member = node->members->next;
    for(; member; member = member->next)
    {
        ast_class_member_node *m = (ast_class_member_node *)member;
        //append_op(cw, m->prefix);
        compile(cw, m->payload);
        arr_append(&list, member);
    }

    int init_flag = 0;

    if(node->super)
    {
        compile(cw, node->super);
        init_flag |= 2;
    }
    if(node->init)
    {
        compile(cw, ((ast_class_member_node *)node->init)->payload);
        init_flag |= 1;
    }

    append_op(cw, LI_MAKE_CLASS, node->lineno);
    append_op(cw, list.count, node->lineno);
    append_op(cw, init_flag, node->lineno);
    int i;
    for(i = list.count - 1; i >= 0; i--)
    {
        ast_class_member_node *m = (ast_class_member_node *)arr_get(&list, i);
        append_op(cw, m->prefix, node->lineno);
        
        long idx = find_prev_name(cw, m->name);

        if(idx < 0)
        {
            idx = cw->rnames.count;
            char *nid = malloc(strlen(m->name) + 1);
            strcpy(nid, m->name);
            arr_append(&cw->rnames, nid);
        }

        unsigned char buf[4];
        int_to_byte_array(buf, idx);

        append_op(cw, buf[0], node->lineno);
        append_op(cw, buf[1], node->lineno);
        append_op(cw, buf[2], node->lineno);
        append_op(cw, buf[3], node->lineno);
    }
}

void compile_function_call(compiler_wrapper *cw, ast_node *root)
{
    ast_func_call_node *node = (ast_func_call_node *)root;

    ast_node *arg = node->arguments;

    int ct = 0;

    for(; arg; arg = arg->next)
    {
        compile(cw, arg);
        ct++;
    }

    compile(cw, node->ident);
    append_op(cw, LI_CALL_FUNC, node->lineno);
    append_op(cw, ct, node->lineno);
}

// Main compiler dispatch system
void compile(compiler_wrapper *cw, ast_node *root)
{
    switch(root->type)
    {
        case ABINARY_EXPRESSION:
            compile_binary(cw, root);
        break;
        case AUNARY_EXPRESSION:
            compile_unary(cw, root);
        break;
        case AVALUE:
            compile_value(cw, root);
        break;
        case AUNIT:
            compile_unit_value(cw, root);
        break;
        case ACOND_CHAIN:
            compile_cond(cw, root);
        break;
        case AIF:
            compile_if(cw, root);
        break;
        case ALOOP:
            compile_loop(cw, root);
        break;
        case AITERLOOP:
            compile_iter_loop(cw, root);
        break;
        case AFUNC_DECL:
            compile_function(cw, root);
        break;
        case AFUNC_CALL:
            compile_function_call(cw, root);
        break;
        case ACLASS_DECL:
            compile_class_decl(cw, root);
        break;
        case ATERNARY:
            compile_ternary(cw, root);
        break;
        case AMEMBER_ACCESS:
            compile_member_access(cw, root);
        break;
        case AARRAY:
            compile_array(cw, root);
        break;
        case ATABLE:
            compile_table(cw, root);
        break;
        case AOBJDECL:
            compile_object(cw, root);
        break;
        case AINDEX:
            compile_indx(cw, root);
        break;
        case ATRIPLESET:
            compile_triple_set(cw, root);
        break;
        case ATRYCATCH:
            compile_try_catch(cw, root);
        break;
        case AONEOFF:
            compile_one_off(cw, root);
        break;
        case ALOAD:
            compile_load(cw, root);
        break;
        case AREGEX:
            compile_regex(cw, root);
        break;
        default:
        break;
    }
}

// Compiles multi-statements/blocks of code
void compile_compound(compiler_wrapper *cw, ast_node *root)
{
    while(root)
    {
        cw->save_val = 0;
        compile(cw, root);
        if(!cw->save_val)
            append_op(cw, LI_POP, root->lineno);

        root = root->next;
    }
}

// TODO: We should probably worry about endienness
void int_to_byte_array(unsigned char *buffer, int val)
{
    buffer[3] = (val >> 24) & 0xFF;
    buffer[2] = (val >> 16) & 0xFF;
    buffer[1] = (val >> 8)  & 0xFF;
    buffer[0] = val         & 0xFF;
}

// As described in the explanation at the top of the file,
// we need to replace tags with and index to other code. The way this
// works is the array of operations is walked backwards. The first
// time a tag is seen, it is recorded as the final destination for
// that particular tag. Every subsquent lookup, the tag is replaced
// with the location.
void replace_tags(compiler_wrapper *cw)
{
    tag_node *tags = make_node();

    long i;
    for(i = cw->rops.count - 1; i >= 0; i--)
    {
        long op = OBJ_NUM_UNWRAP(arr_get(&cw->rops, i));

        if(op < 0) // This should *never* happen (except it does...).
            continue;

        if(op > 999) // If we are dealing with a tag...
        {
            long line = get_line(tags, op); // Get the line associated with the tag
            if(line < 0) // A negative value indicates the tag is as-yet unseen.
            {
                // Make a new tag location with the current index (i)
                append_tag(tags, op, i);

                // Replace the tag with the 'ignore' instruction (think NOP)
                lky_object *obj = lobjb_build_int(LI_IGNORE);
                pool_add(&ast_memory_pool, obj);
                cw->rops.items[i] = obj;
                continue;
            }

            // Otherwise we have a valid tag location and we can fix our jumps.
            unsigned char buf[4];
            int_to_byte_array(buf, (int)line);

            int j;
            for(j = 0; j < 4; j++)
            {   // Deal with our multiple bytes for addressing
                lky_object *obj = lobjb_build_int(buf[j]);
                pool_add(&ast_memory_pool, obj);
                cw->rops.items[i + j] = obj;
            }
        }
    }

    free_tag_nodes(tags);
}

// Finalize the constants into an array
void **make_cons_array(compiler_wrapper *cw)
{
    void **data = malloc(sizeof(void *) * cw->rcon.count);

    long i;
    for(i = 0; i < cw->rcon.count; i++)
    {
        data[i] = arr_get(&cw->rcon, i);
    }

    return data;
}

// Finalize the names into an array
char **make_names_array(compiler_wrapper *cw)
{
    char **names = malloc(sizeof(char *) * cw->rnames.count);

    long i;
    for(i = 0; i < cw->rnames.count; i++)
    {
        char *txt = arr_get(&cw->rnames, i);
        char *nw = malloc(strlen(txt) + 1);
        strcpy(nw, txt);
        names[i] = nw;
    }

    return names;
}

lky_object_code *compile_ast_repl(ast_node *root)
{
    compiler_wrapper cw;
    cw.saved_locals = hm_create(10, 1);
    cw.local_idx = 0;
    cw.rnames = arr_create(10);
    cw.used_names = arr_create(10);
    cw.repl = 1;
    cw.impl_name = "main";

    return compile_ast_ext(root, &cw);
}

// The brain of the operation; compiles an AST from its root. Sometimes we want
// to set some initial settings, so we pass in a compiler wrapper. That argument
// is optional.
lky_object_code *compile_ast_ext(ast_node *root, compiler_wrapper *incw)
{
    compiler_wrapper cw;
    cw.rops = arr_create(50);
    cw.rcon = arr_create(10);
    cw.loop_start_stack = arr_create(10);
    cw.loop_end_stack = arr_create(10);
    cw.rindices = arr_create(100);
    cw.ifTag = 1000;
    cw.save_val = 0;
    cw.name_idx = 0;
    cw.classargc = 0;
    
    if(incw)
    {
        cw.saved_locals = incw->saved_locals;
        cw.local_idx = incw->local_idx;
        cw.rnames = incw->rnames;
        cw.used_names = incw->used_names;
        cw.repl = incw->repl;
        cw.impl_name = incw->impl_name;
    }
    else
    {
        cw.saved_locals = hm_create(100, 1);
        cw.local_idx = 0;
        cw.rnames = arr_create(50);
        cw.used_names = arr_create(20);
        cw.repl = 0;
        cw.impl_name = NULL;
    }

    compile_compound(&cw, root);
    replace_tags(&cw);

    // We want to propogate the classargc
    // (number of args passed to build_)
    // back up to the caller.
    if(incw)
        incw->classargc = cw.classargc;
    
    if(cw.rops.count == 0 || OBJ_NUM_UNWRAP(arr_get(&cw.rops, cw.rops.count - 1)) != LI_RETURN)
    {
        append_op(&cw, LI_PUSH_NIL, root->lineno);
        append_op(&cw, LI_RETURN, root->lineno);
    }

    // Build the resultant code object.
    lky_object_code *code = malloc(sizeof(lky_object_code));
    code->constants = make_cons_array(&cw);
    code->num_constants = cw.rcon.count;
    code->num_locals = cw.local_idx;
    code->mem_count = 0;
    code->type = LBI_CODE;
    code->num_names = cw.rnames.count;
    code->indices = finalize_indices(&cw);
    code->ops = finalize_ops(&cw);
    code->op_len = cw.rops.count;
    code->locals = malloc(sizeof(void *) * cw.local_idx);
    code->names = make_names_array(&cw);
    code->refname = NULL;
    code->stack_size = calculate_max_stack_depth(code->ops, (int)code->op_len);
    code->catch_size = calculate_max_catch_depth(code->ops, (int)code->op_len);
    
    cw.impl_name = cw.impl_name ? cw.impl_name : "Anonymous Function";
    code->impl_name = malloc(strlen(cw.impl_name) + 1);
    strcpy(code->impl_name, cw.impl_name);

    int i;
    for(i = 0; i < cw.local_idx; i++)
        code->locals[i] = NULL;

    return code;
}

// Externally visible compilation function
lky_object_code *compile_ast(ast_node *root)
{
    int backup = lobjb_uses_pointer_tags_;
    lobjb_uses_pointer_tags_ = 0;
    lky_object_code *ret = compile_ast_ext(root, NULL);
    lobjb_uses_pointer_tags_ = backup;
    return ret;
}
