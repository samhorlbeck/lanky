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

#ifndef LKY_GC_H
#define LKY_GC_H

#include "lkyobj_builtin.h"

void gc_init();
void gc_pause();
void gc_pause_collection();
void gc_resume();
void gc_resume_collection();
void gc_add_func_stack(stackframe *frame);
void gc_add_root_object(lky_object *obj);
void gc_remove_root_object(lky_object *obj);
void gc_add_object(lky_object *obj);
void gc_gc();
void gc_mark();
void gc_collect();
void gc_mark_object(lky_object *o);
size_t gc_alloced();

#endif
