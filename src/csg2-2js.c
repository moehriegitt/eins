/* -*- Mode: C -*- */
/* Copyright (C) 2018 by Henrik Theiling, License: GPLv3, see LICENSE file */

#include <hob3lbase/arith.h>
#include <hob3lbase/vec.h>
#include <hob3lbase/mat.h>
#include <hob3lbase/panic.h>
#include <hob3lbase/alloc.h>
#include <hob3l/csg2.h>
#include <hob3l/gc.h>
#include "internal.h"

#define SHIFT_I 0

#define O_HORIZ 0
#define O_VERT  1

#define PER_VERTEX 4
#define VERTEX_CNT 65535

typedef struct {
    long x,y,z;
} ivec3_t;

typedef struct {
    ivec3_t p;
    ivec3_t n;
    cp_color_rgba_t c;
} vertex_t;

typedef struct {
    unsigned short i[3];
} u16_3_t;

typedef struct {
    cp_a_u16_t idx;
    vertex_t v[VERTEX_CNT];
    size_t v_cnt;
    u16_3_t tri[VERTEX_CNT];
    size_t tri_cnt;
    size_t scene_cnt;
} ctxt_t;

static inline unsigned short *idx_ptr(
    ctxt_t *c,
    size_t pi,
    unsigned zi,
    unsigned oi)
{
    assert(zi < 2);
    assert(oi < 2);
    size_t i = (pi * PER_VERTEX) + (zi * 2) + oi;
    return &cp_v_nth(&c->idx, i);
}

static void v_csg2_put_js(
    ctxt_t *c,
    cp_stream_t *s,
    cp_csg2_tree_t *t,
    size_t zi,
    cp_v_csg2_p_t *r);

static inline long js_coord(cp_dim_t f)
{
    return lrint(f / cp_pt_epsilon);
}

static unsigned short store_vertex(
    ctxt_t *c,
    unsigned short *ip,
    cp_dim_t xn, cp_dim_t yn, cp_dim_t zn,
    cp_vec2_loc_t const *xy,
    cp_dim_t z)
{
    if (*ip > c->v_cnt) {
        assert(c->v_cnt < cp_countof(c->v));
        *ip = 0xffff & c->v_cnt++;
        vertex_t *v = &c->v[*ip];
        v->p.x = js_coord(xy->coord.x);
        v->p.y = js_coord(xy->coord.y);
        v->p.z = js_coord(z);
        v->n.x = js_coord(xn)*1000;
        v->n.y = js_coord(yn)*1000;
        v->n.z = js_coord(zn)*1000;
        v->c.r = 255;
        v->c.g = 128;
        v->c.b = 128;
        v->c.a = 255;
    }
    return *ip;
}

static int idx_val(
    int *last,
    unsigned short x)
{
    int r = (x - *last) + SHIFT_I;
    *last = x;
    return r;
}

static void scene_flush(
    ctxt_t *c,
    cp_stream_t *s)
{
    if (c->tri_cnt > 0) {
        cp_printf(s, "var scene_%"_Pz"u = {\n", c->scene_cnt);
        cp_printf(s, "   'group':{1:true},\n");
        cp_printf(s, "   'vertex':[");
        for (cp_size_each(i, c->v_cnt)) {
            cp_printf(s, "%s%ld,%ld,%ld",
                i == 0 ? "" : ",",
                c->v[i].p.x, c->v[i].p.y,  c->v[i].p.z);
        }
        cp_printf(s, "],\n");

        cp_printf(s, "   'normal':[");
        for (cp_size_each(i, c->v_cnt)) {
            cp_printf(s, "%s%ld,%ld,%ld",
                i == 0 ? "" : ",",
                c->v[i].n.x, c->v[i].n.y,  c->v[i].n.z);
        }
        cp_printf(s, "],\n");

        cp_printf(s, "   'color':[");
        for (cp_size_each(i, c->v_cnt)) {
            cp_printf(s, "%s%u,%u,%u,%u",
                i == 0 ? "" : ",",
                c->v[i].c.r, c->v[i].c.g,  c->v[i].c.b, c->v[i].c.a);
        }
        cp_printf(s, "],\n");

        cp_printf(s, "   'index':[");
        int last = 0;
        for (cp_size_each(i, c->tri_cnt)) {
            int d0 = idx_val(&last, c->tri[i].i[0]);
            int d1 = idx_val(&last, c->tri[i].i[1]);
            int d2 = idx_val(&last, c->tri[i].i[2]);
            cp_printf(s, "%s%d,%d,%d",
                i == 0 ? "" : ",",
                d0, d1, d2);
        }
        cp_printf(s, "],\n");
        cp_printf(s, "};\n");
        c->scene_cnt++;
    }
    c->v_cnt = 0;
    c->tri_cnt = 0;
}

static void triangle_put_js(
    ctxt_t *c,
    cp_stream_t *s,
    cp_v_vec2_loc_t const *point,
    cp_dim_t const z[],
    unsigned oi,
    cp_dim_t xn, cp_dim_t yn, cp_dim_t zn,
    size_t k1, unsigned i1,
    size_t k2, unsigned i2,
    size_t k3, unsigned i3)
{
    if (((c->v_cnt   + 3) > cp_countof(c->v)) ||
        ((c->tri_cnt + 1) > cp_countof(c->tri)))
    {
        scene_flush(c, s);
    }

    cp_vec2_loc_t const *xy1 = &cp_v_nth(point, k1);
    cp_vec2_loc_t const *xy2 = &cp_v_nth(point, k2);
    cp_vec2_loc_t const *xy3 = &cp_v_nth(point, k3);

    assert(c->tri_cnt < cp_countof(c->tri));
    u16_3_t *t = &c->tri[c->tri_cnt++];
    t->i[0] = store_vertex(c, idx_ptr(c, k1, i1, oi), xn, yn, zn, xy1, z[i1]);
    t->i[1] = store_vertex(c, idx_ptr(c, k2, i2, oi), xn, yn, zn, xy2, z[i2]);
    t->i[2] = store_vertex(c, idx_ptr(c, k3, i3, oi), xn, yn, zn, xy3, z[i3]);
}

static void poly_put_js(
    ctxt_t *c,
    cp_stream_t *s,
    cp_csg2_tree_t *t,
    size_t zi,
    cp_csg2_poly_t *r)
{
    size_t idx_cnt = r->point.size * PER_VERTEX;
    assert(idx_cnt <= c->idx.size);
    memset(c->idx.data, 255, idx_cnt * sizeof(c->idx.data[0]));

    cp_dim_t z[2];
    z[0] = cp_v_nth(&t->z, zi);
    z[1] = z[0] + cp_csg2_layer_thickness(t, zi) - t->opt.layer_gap;

    cp_v_vec2_loc_t const *point = &r->point;

    /* top */
    for (cp_v_each(i, &r->triangle)) {
        size_t const *p = cp_v_nth(&r->triangle, i).p;
        triangle_put_js(c, s, point, z, O_HORIZ,
            0., 0., 1.,
            p[1], 1,
            p[0], 1,
            p[2], 1);
    }

    /* bottom */
    for (cp_v_each(i, &r->triangle)) {
        size_t const *p = cp_v_nth(&r->triangle, i).p;
        triangle_put_js(c, s, point, z, O_HORIZ,
            0., 0., -1.,
            p[0], 0,
            p[1], 0,
            p[2], 0);
    }

    /* sides */
    for (cp_v_each(i, &r->path)) {
        cp_csg2_path_t const *p = &cp_v_nth(&r->path, i);
        for (cp_v_each(j, &p->point_idx)) {
            size_t k = cp_wrap_add1(j, p->point_idx.size);
            size_t ij = cp_v_nth(&p->point_idx, j);
            size_t ik = cp_v_nth(&p->point_idx, k);
            cp_vec2_loc_t const *pj = &cp_v_nth(point, ij);
            cp_vec2_loc_t const *pk = &cp_v_nth(point, ik);

            /**
             * All paths are viewed from above, and pj, pk are in CW order =>
             * Side view from outside:
             *
             *     (pk,z[1])-------(pj,z[1])
             *     |                   |
             *     (pk,z[0])-------(pj,z[0])
             *
             * Triangles in STL are CCW, so:
             *     (pk,z[0])--(pj,z[1])--(pk,z[1])
             * and (pk,z[0])..(pj,z[0])--(pj,z[1])
             */
            cp_vec3_t n;
            cp_vec3_left_normal3(&n,
                &(cp_vec3_t){{ pk->coord.x, pk->coord.y, z[0] }},
                &(cp_vec3_t){{ pj->coord.x, pj->coord.y, z[1] }},
                &(cp_vec3_t){{ pk->coord.x, pk->coord.y, z[1] }});

            triangle_put_js(c, s, point, z, O_VERT,
                n.x, n.y, n.z,
                ik, 0,
                ij, 1,
                ik, 1);
            triangle_put_js(c, s, point, z, O_VERT,
                n.x, n.y, n.z,
                ik, 0,
                ij, 0,
                ij, 1);
        }
    }
}

static void union_put_js(
    ctxt_t *c,
    cp_stream_t *s,
    cp_csg2_tree_t *t,
    size_t zi,
    cp_v_csg2_p_t *r)
{
    v_csg2_put_js(c, s, t, zi, r);
}

static void add_put_js(
    ctxt_t *c,
    cp_stream_t *s,
    cp_csg2_tree_t *t,
    size_t zi,
    cp_csg2_add_t *r)
{
    union_put_js (c, s, t, zi, &r->add);
}

static void sub_put_js(
    ctxt_t *c,
    cp_stream_t *s,
    cp_csg2_tree_t *t,
    size_t zi,
    cp_csg2_sub_t *r)
{
    /* This output format cannot do SUB, only UNION, so we ignore
     * the 'sub' part.  It is wrong, but you asked for it. */
    union_put_js (c, s, t, zi, &r->add.add);
}

static void cut_put_js(
    ctxt_t *c,
    cp_stream_t *s,
    cp_csg2_tree_t *t,
    size_t zi,
    cp_csg2_cut_t *r)
{
    /* This output format cannot do CUT, only UNION, so just print
     * the first part.  It is wrong, but you asked for it. */
    if (r->cut.size > 0) {
        union_put_js(c, s, t, zi, &cp_v_nth(&r->cut, 0)->add);
    }
}

static void layer_put_js(
    ctxt_t *c,
    cp_stream_t *s,
    cp_csg2_tree_t *t,
    size_t zi __unused,
    cp_csg2_layer_t *r)
{
    if (r->root.add.size == 0) {
        return;
    }
    assert(zi == r->zi);
    v_csg2_put_js(c, s, t, r->zi, &r->root.add);
}

static void stack_put_js(
    ctxt_t *c,
    cp_stream_t *s,
    cp_csg2_tree_t *t,
    cp_csg2_stack_t *r)
{
    for (cp_v_each(i, &r->layer)) {
        layer_put_js(c, s, t, r->idx0 + i, &cp_v_nth(&r->layer, i));
    }
}

static void csg2_put_js(
    ctxt_t *c,
    cp_stream_t *s,
    cp_csg2_tree_t *t,
    size_t zi,
    cp_csg2_t *r)
{
    switch (r->type) {
    case CP_CSG2_ADD:
        add_put_js(c, s, t, zi, &r->add);
        return;

    case CP_CSG2_SUB:
        sub_put_js(c, s, t, zi, &r->sub);
        return;

    case CP_CSG2_CUT:
        cut_put_js(c, s, t, zi, &r->cut);
        return;

    case CP_CSG2_POLY:
        poly_put_js(c, s, t, zi, &r->poly);
        return;

    case CP_CSG2_STACK:
        stack_put_js(c, s, t, &r->stack);
        return;

    case CP_CSG2_CIRCLE:
        CP_NYI("circle in stl");
        return;
    }

    CP_DIE();
}

static void v_csg2_put_js(
    ctxt_t *c,
    cp_stream_t *s,
    cp_csg2_tree_t *t,
    size_t zi,
    cp_v_csg2_p_t *r)
{
    for (cp_v_each(i, r)) {
        csg2_put_js(c, s, t, zi, cp_v_nth(r, i));
    }
}

static size_t v_csg2_max_point_cnt(
    cp_v_csg2_p_t *r);

static size_t poly_max_point_cnt(
    cp_csg2_poly_t *r)
{
    return r->point.size;
}

static size_t union_max_point_cnt(
    cp_v_csg2_p_t *r)
{
    return v_csg2_max_point_cnt(r);
}

static size_t add_max_point_cnt(
    cp_csg2_add_t *r)
{
    return union_max_point_cnt (&r->add);
}

static size_t sub_max_point_cnt(
    cp_csg2_sub_t *r)
{
    /* add only, sub is ignored */
    return union_max_point_cnt (&r->add.add);
}

static size_t cut_max_point_cnt(
    cp_csg2_cut_t *r)
{
    /* first element only */
    if (r->cut.size == 0) {
        return 0;
    }
    return union_max_point_cnt(&cp_v_nth(&r->cut, 0)->add);
}

static size_t layer_max_point_cnt(
    cp_csg2_layer_t *r)
{
    if (r->root.add.size == 0) {
        return 0;
    }
    return v_csg2_max_point_cnt(&r->root.add);
}

static size_t stack_max_point_cnt(
    cp_csg2_stack_t *r)
{
    size_t cnt = 0;
    for (cp_v_each(i, &r->layer)) {
        size_t k = layer_max_point_cnt(&cp_v_nth(&r->layer, i));
        if (k > cnt) {
            cnt = k;
        }
    }
    return cnt;
}

static size_t csg2_max_point_cnt(
    cp_csg2_t *r)
{
    switch (r->type) {
    case CP_CSG2_ADD:
        return add_max_point_cnt(&r->add);

    case CP_CSG2_SUB:
        return sub_max_point_cnt(&r->sub);

    case CP_CSG2_CUT:
        return cut_max_point_cnt(&r->cut);

    case CP_CSG2_POLY:
        return poly_max_point_cnt(&r->poly);

    case CP_CSG2_STACK:
        return stack_max_point_cnt(&r->stack);

    case CP_CSG2_CIRCLE:
        CP_NYI("circle in stl");
    }

    CP_DIE();
}

static size_t v_csg2_max_point_cnt(
    cp_v_csg2_p_t *r)
{
    size_t cnt = 0;
    for (cp_v_each(i, r)) {
        size_t k = csg2_max_point_cnt(cp_v_nth(r, i));
        if (k > cnt) {
            cnt = k;
        }
    }
    return cnt;
}

/* ********************************************************************** */

/**
 * Print as JavaScript file containing a WebGL scene configuration.
 *
 * This uses both the triangle and the polygon data for printing.  The
 * triangles are used for the xy plane (top and bottom) and the path
 * for connecting top and bottom at the edges of the slice.
 */
extern void cp_csg2_tree_put_js(
    cp_stream_t *s,
    cp_csg2_tree_t *t)
{
    ctxt_t *c;
    CP_CALLOC(c);

    c->idx.size = csg2_max_point_cnt(t->root) * PER_VERTEX;
    CP_CALLOC_ARR(c->idx.data, c->idx.size);

    cp_printf(s, "var group_1 = {\n");
    cp_printf(s, " 'tag':'1',\n");
    cp_printf(s, " 'name':'1',\n");
    cp_printf(s, " 'optional':false,\n");
    cp_printf(s, " 'default_off':false,\n");
    cp_printf(s, " 'fixed':false,\n");
    cp_printf(s, " 'prio':1,\n");
    cp_printf(s, "};\n");
    cp_printf(s, "var group = {\n");
    cp_printf(s, "  1:group_1\n");
    cp_printf(s, "};\n");
    cp_printf(s, "var sceneScaleV = %g;\n", 1000/cp_pt_epsilon);
    cp_printf(s, "var sceneScaleC = 255;\n");
    cp_printf(s, "var sceneShiftI = %u;\n", SHIFT_I);

    scene_flush(c, s);
    if (t->root != NULL) {
        csg2_put_js(c, s, t, 0, t->root);
    }
    scene_flush(c, s);

    cp_printf(s, "var scene = [\n");
    for (cp_size_each(i, c->scene_cnt)) {
        cp_printf(s, "    scene_%"_Pz"u,\n", i);
    }
    cp_printf(s, "];\n");

    CP_FREE(c->idx.data);
    CP_FREE(c);
}
