/*
 * This file is part of AtomVM.
 *
 * Copyright 2022 Fred Dushin <fred@dushin.net>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * SPDX-License-Identifier: Apache-2.0 OR LGPL-2.1-or-later
 */

#include "stacktrace.h"
#include "defaultatoms.h"
#include "globalcontext.h"
#include "memory.h"

#ifndef AVM_CREATE_STACKTRACES

term stacktrace_create_raw(Context *ctx, Module *mod, int current_offset, term exception_class)
{
    UNUSED(ctx);
    UNUSED(mod);
    UNUSED(current_offset);
    return exception_class;
}

term stacktrace_build(Context *ctx, term *stack_info, uint32_t live)
{
    UNUSED(ctx);
    UNUSED(stack_info);
    UNUSED(live);
    return UNDEFINED_ATOM;
}

term stacktrace_exception_class(term stack_info)
{
    return stack_info;
}

#else

static void cp_to_mod_lbl_off(term cp, Context *ctx, Module **cp_mod, int *label, int *l_off, long *mod_offset)
{
    int module_index = cp >> 24;
    Module *mod = globalcontext_get_module_by_index(ctx->global, module_index);
    *mod_offset = (cp & 0xFFFFFF) >> 2;

    *cp_mod = mod;

    uint8_t *code = &mod->code->code[0];
    int labels_count = ENDIAN_SWAP_32(mod->code->labels);

    int i = 1;
    const uint8_t *l = mod->labels[1];
    while (*mod_offset > l - code) {
        i++;
        if (i >= labels_count) {
            // last label + 1 is reserved for end of module.
            *label = i;
            *l_off = 0;
            return;
        }
        l = mod->labels[i];
    }

    *label = i - 1;
    *l_off = *mod_offset - (mod->labels[*label] - code);
}

static bool location_sets_append(GlobalContext *global, Module *mod, const uint8_t *filename, size_t filename_len, size_t *total_filename_len, const void ***io_locations_set, size_t *io_locations_set_size)
{
    const void **locations_set = *io_locations_set;
    size_t locations_set_size = *io_locations_set_size;
    const void *key = filename;
    if (IS_NULL_PTR(filename)) {
        key = mod;
        AtomString module_name_atom_str = globalcontext_atomstring_from_term(global, module_get_name(mod));
        filename_len = atom_string_len(module_name_atom_str) + 4; // ".erl"
    }
    for (size_t i = 0; i < locations_set_size; i++) {
        if (locations_set[i] == key) {
            return true;
        }
    }
    const void **new_locations_set = realloc(locations_set, (locations_set_size + 1) * sizeof(const uint8_t *));
    if (IS_NULL_PTR(new_locations_set)) {
        // Some versions of gcc don't know that if allocation fails, original pointer should still be freed
#pragma GCC diagnostic push
#if (defined(__GNUC__) && !defined(__clang__) && __GNUC__ >= 12)
#pragma GCC diagnostic ignored "-Wuse-after-free"
#endif
        free(locations_set);
#pragma GCC diagnostic pop
        *io_locations_set = NULL;
        fprintf(stderr, "Unable to allocate space for locations set.  No stacktrace will be created\n");
        return false;
    }
    *io_locations_set = new_locations_set;
    new_locations_set[locations_set_size] = key;
    *io_locations_set_size = locations_set_size + 1;
    *total_filename_len += filename_len;

    return true;
}

term stacktrace_create_raw(Context *ctx, Module *mod, int current_offset, term exception_class)
{
    unsigned int num_frames = 0;
    unsigned int num_aux_terms = 0;
    size_t filename_lens = 0;
    Module *prev_mod = NULL;
    long prev_mod_offset = -1;
    term *ct = ctx->e;
    term *stack_base = context_stack_base(ctx);

    const void **locations = NULL;
    size_t num_locations = 0;

    while (ct != stack_base) {
        if (term_is_cp(*ct)) {

            Module *cp_mod;
            int label;
            int offset;
            long mod_offset;

            cp_to_mod_lbl_off(*ct, ctx, &cp_mod, &label, &offset, &mod_offset);
            if (mod_offset != cp_mod->end_instruction_ii && !(prev_mod == cp_mod && mod_offset == prev_mod_offset)) {
                ++num_frames;
                prev_mod = cp_mod;
                prev_mod_offset = mod_offset;
                if (module_has_line_chunk(cp_mod)) {
                    uint32_t line;
                    const uint8_t *filename;
                    size_t filename_len;
                    if (LIKELY(module_find_line(cp_mod, (unsigned int) mod_offset, &line, &filename_len, &filename))) {
                        if (!location_sets_append(ctx->global, cp_mod, filename, filename_len, &filename_lens, &locations, &num_locations)) {
                            return UNDEFINED_ATOM;
                        }
                        num_aux_terms++;
                    }
                }
            }
        } else if (term_is_catch_label(*ct)) {
            int module_index;
            int label = term_to_catch_label_and_module(*ct, &module_index);

            Module *cl_mod = globalcontext_get_module_by_index(ctx->global, module_index);
            uint8_t *code = &cl_mod->code->code[0];
            int mod_offset = cl_mod->labels[label] - code;

            if (!(prev_mod == cl_mod && mod_offset == prev_mod_offset)) {
                ++num_frames;
                prev_mod = cl_mod;
                prev_mod_offset = mod_offset;
                if (module_has_line_chunk(cl_mod)) {
                    uint32_t line;
                    const uint8_t *filename;
                    size_t filename_len;
                    if (LIKELY(module_find_line(cl_mod, (unsigned int) mod_offset, &line, &filename_len, &filename))) {
                        if (!location_sets_append(ctx->global, cl_mod, filename, filename_len, &filename_lens, &locations, &num_locations)) {
                            return UNDEFINED_ATOM;
                        }
                        num_aux_terms++;
                    }
                }
            }
        }
        ct++;
    }

    num_frames++;
    if (module_has_line_chunk(mod)) {
        uint32_t line;
        const uint8_t *filename;
        size_t filename_len;
        if (LIKELY(module_find_line(mod, (unsigned int) current_offset, &line, &filename_len, &filename))) {
            if (!location_sets_append(ctx->global, mod, filename, filename_len, &filename_lens, &locations, &num_locations)) {
                return UNDEFINED_ATOM;
            }
            num_aux_terms++;
        }
    }

    free(locations);

    // {num_frames, num_aux_terms, filename_lens, num_mods, [{module, offset}, ...]}
    size_t requested_size = TUPLE_SIZE(6) + num_frames * (2 + TUPLE_SIZE(2));
    // We need to preserve x0 and x1 that contain information on the current exception
    if (UNLIKELY(memory_ensure_free_with_roots(ctx, requested_size, 2, ctx->x, MEMORY_CAN_SHRINK) != MEMORY_GC_OK)) {
        fprintf(stderr, "WARNING: Unable to allocate heap space for raw stacktrace\n");
        return OUT_OF_MEMORY_ATOM;
    }

    term raw_stacktrace = term_nil();

    term frame_info = term_alloc_tuple(2, &ctx->heap);
    term_put_tuple_element(frame_info, 0, term_from_int(mod->module_index));
    term_put_tuple_element(frame_info, 1, term_from_int(current_offset));
    raw_stacktrace = term_list_prepend(frame_info, raw_stacktrace, &ctx->heap);

    prev_mod = NULL;
    prev_mod_offset = -1;
    // GC may have moved stack
    ct = ctx->e;
    stack_base = context_stack_base(ctx);
    while (ct != stack_base) {
        if (term_is_cp(*ct)) {
            Module *cp_mod;
            int label;
            int offset;
            long mod_offset;

            cp_to_mod_lbl_off(*ct, ctx, &cp_mod, &label, &offset, &mod_offset);
            if (mod_offset != cp_mod->end_instruction_ii && !(prev_mod == cp_mod && mod_offset == prev_mod_offset)) {

                prev_mod = cp_mod;
                prev_mod_offset = mod_offset;

                term frame_info = term_alloc_tuple(2, &ctx->heap);
                term_put_tuple_element(frame_info, 0, term_from_int(cp_mod->module_index));
                term_put_tuple_element(frame_info, 1, term_from_int(mod_offset));

                raw_stacktrace = term_list_prepend(frame_info, raw_stacktrace, &ctx->heap);
            }
        } else if (term_is_catch_label(*ct)) {

            int module_index;
            int label = term_to_catch_label_and_module(*ct, &module_index);
            Module *cl_mod = globalcontext_get_module_by_index(ctx->global, module_index);
            uint8_t *code = &cl_mod->code->code[0];
            int mod_offset = cl_mod->labels[label] - code;

            if (!(prev_mod == cl_mod && mod_offset == prev_mod_offset)) {

                prev_mod = cl_mod;
                prev_mod_offset = mod_offset;

                term frame_info = term_alloc_tuple(2, &ctx->heap);
                term_put_tuple_element(frame_info, 0, term_from_int(module_index));
                term_put_tuple_element(frame_info, 1, term_from_int(mod_offset));

                raw_stacktrace = term_list_prepend(frame_info, raw_stacktrace, &ctx->heap);
            }
        }
        ct++;
    }

    term stack_info = term_alloc_tuple(6, &ctx->heap);
    term_put_tuple_element(stack_info, 0, term_from_int(num_frames));
    term_put_tuple_element(stack_info, 1, term_from_int(num_aux_terms));
    term_put_tuple_element(stack_info, 2, term_from_int(filename_lens));
    term_put_tuple_element(stack_info, 3, term_from_int(num_locations));
    term_put_tuple_element(stack_info, 4, raw_stacktrace);
    term_put_tuple_element(stack_info, 5, exception_class);

    return stack_info;
}

term stacktrace_exception_class(term stack_info)
{
    return term_get_tuple_element(stack_info, 5);
}

struct ModulePathPair
{
    const void *key;
    term path;
};

static term find_path_created(const void *key, struct ModulePathPair *module_paths, int len)
{
    for (int i = 0; i < len; ++i) {
        if (module_paths[i].key == key) {
            return module_paths[i].path;
        }
    }
    return term_invalid_term();
}

term stacktrace_build(Context *ctx, term *stack_info, uint32_t live)
{
    GlobalContext *glb = ctx->global;

    if (*stack_info == OUT_OF_MEMORY_ATOM) {
        return *stack_info;
    }
    if (!term_is_tuple(*stack_info)) {
        return UNDEFINED_ATOM;
    }

    int num_frames = term_to_int(term_get_tuple_element(*stack_info, 0));
    int num_aux_terms = term_to_int(term_get_tuple_element(*stack_info, 1));
    int filename_lens = term_to_int(term_get_tuple_element(*stack_info, 2));
    int num_mods = term_to_int(term_get_tuple_element(*stack_info, 3));

    struct ModulePathPair *module_paths = malloc(num_mods * sizeof(struct ModulePathPair));
    if (IS_NULL_PTR(module_paths)) {
        fprintf(stderr, "Unable to allocate space for module paths.  Returning raw stacktrace.\n");
        return *stack_info;
    }

    //
    // [{module, function, arity, [{file, string()}, {line, int}]}, ...]
    //
    size_t requested_size = (TUPLE_SIZE(4) + 2) * num_frames + num_aux_terms * (4 + 2 * TUPLE_SIZE(2)) + 2 * filename_lens;
    if (UNLIKELY(memory_ensure_free_with_roots(ctx, requested_size, live, ctx->x, MEMORY_CAN_SHRINK) != MEMORY_GC_OK)) {
        free(module_paths);
        return OUT_OF_MEMORY_ATOM;
    }

    term raw_stacktrace = term_get_tuple_element(*stack_info, 4);

    term stacktrace = term_nil();
    term el = raw_stacktrace;
    int module_path_idx = 0;
    while (!term_is_nil(el)) {
        term mod_index_tuple = term_get_list_head(el);
        term cp = module_address(
            term_to_int(term_get_tuple_element(mod_index_tuple, 0)),
            term_to_int(term_get_tuple_element(mod_index_tuple, 1)));

        Module *cp_mod;
        int label;
        int offset;
        long mod_offset;
        cp_to_mod_lbl_off(cp, ctx, &cp_mod, &label, &offset, &mod_offset);

        term module_name = module_get_name(cp_mod);

        term frame_i = term_alloc_tuple(4, &ctx->heap);
        term_put_tuple_element(frame_i, 0, module_name);

        term aux_data = term_nil();
        if (module_has_line_chunk(cp_mod)) {
            uint32_t line;
            const uint8_t *filename;
            size_t filename_len;
            if (LIKELY(module_find_line(cp_mod, (unsigned int) mod_offset, &line, &filename_len, &filename))) {
                term line_tuple = term_alloc_tuple(2, &ctx->heap);
                term_put_tuple_element(line_tuple, 0, globalcontext_make_atom(glb, ATOM_STR("\x4", "line")));
                term_put_tuple_element(line_tuple, 1, term_from_int(line));
                aux_data = term_list_prepend(line_tuple, aux_data, &ctx->heap);

                term file_tuple = term_alloc_tuple(2, &ctx->heap);
                term_put_tuple_element(file_tuple, 0, globalcontext_make_atom(glb, ATOM_STR("\x4", "file")));

                const void *key = IS_NULL_PTR(filename) ? (const void *) cp_mod : (const void *) filename;
                term path = find_path_created(key, module_paths, module_path_idx);
                if (term_is_invalid_term(path)) {
                    if (IS_NULL_PTR(filename)) {
                        AtomString module_name_atom_str = globalcontext_atomstring_from_term(ctx->global, module_get_name(cp_mod));
                        uint8_t *default_filename = malloc(atom_string_len(module_name_atom_str) + 4);
                        if (IS_NULL_PTR(default_filename)) {
                            free(module_paths);
                            return OUT_OF_MEMORY_ATOM;
                        }
                        memcpy(default_filename, atom_string_data(module_name_atom_str), atom_string_len(module_name_atom_str));
                        memcpy(default_filename + atom_string_len(module_name_atom_str), ".erl", 4);
                        path = term_from_string(default_filename, atom_string_len(module_name_atom_str) + 4, &ctx->heap);
                        free(default_filename);
                    } else {
                        path = term_from_string(filename, filename_len, &ctx->heap);
                    }
                    module_paths[module_path_idx].key = key;
                    module_paths[module_path_idx].path = path;
                    module_path_idx++;
                }
                term_put_tuple_element(file_tuple, 1, path);
                aux_data = term_list_prepend(file_tuple, aux_data, &ctx->heap);
            }
        }
        term_put_tuple_element(frame_i, 3, aux_data);

        AtomString function_name = NULL;
        int arity = 0;
        bool result = module_get_function_from_label(cp_mod, label, &function_name, &arity, glb);

        if (LIKELY(result)) {
            term_put_tuple_element(frame_i, 1, globalcontext_make_atom(glb, function_name));
            term_put_tuple_element(frame_i, 2, term_from_int(arity));
        } else {
            term_put_tuple_element(frame_i, 1, UNDEFINED_ATOM);
            term_put_tuple_element(frame_i, 2, term_from_int(0));
        }
        stacktrace = term_list_prepend(frame_i, stacktrace, &ctx->heap);

        el = term_get_list_tail(el);
    }
    free(module_paths);

    return stacktrace;
}

#endif
