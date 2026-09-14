/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Hugh Frater
 *
 * This file is part of pipegen. pipegen is free software: you can
 * redistribute it and/or modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version. It is distributed in
 * the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the
 * implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License in LICENSE for details.
 */
/*
 * pg_route.h — the chamber as a chain of pieces in space
 *
 * The designed chamber is straight. To build it, each section is cut into
 * pieces; to fit it to a frame, the pieces are turned against each other at
 * their joints. Every joint is a mitre: both pieces are cut on the plane that
 * bisects the angle between their axes, so the centreline — and with it every
 * length the wave timing depends on — is the same bent as straight.
 *
 * Each piece carries a frame: its axis, and a reference direction round it
 * that starts pointing straight down at the port and is carried from piece
 * to piece without twisting. Azimuth round a piece is measured from that
 * reference; bends, seams and alignment marks are all placed by azimuth.
 */
#ifndef PG_ROUTE_H
#define PG_ROUTE_H

#include "pg_project.h"
#include "pg_design.h"

#define PG_MAX_PIECES 96

typedef enum {
    PG_PIECE_DUCT = 0,     /* inside the cylinder casting: not made     */
    PG_PIECE_SHEET,        /* rolled or hydroformed from sheet          */
    PG_PIECE_TUBE          /* cut from tube                             */
} PgPieceKind;

typedef struct {
    PgPieceKind kind;
    int    section;          /* design section it belongs to             */
    PgSectionKind sec_kind;  /* ... and what that section is             */
    int    stage;            /* diffuser stage, 1-based; 0 otherwise     */
    int    seg, nseg;        /* 1-based piece within the section         */
    char   name[48];
    double x0, len;          /* centreline position and length, mm       */
    double d0, d1;           /* internal diameter at each end            */

    double p0[3], p1[3];     /* axis end points, engine coordinates      */
    double axis[3];          /* unit, from p0 to p1                      */
    double ref[3];           /* azimuth 0                                */
    double side[3];          /* azimuth 90 = axis x ref                  */

    /* End planes, as unit normals in the piece's own frame (x = ref,
     * y = side, z = axis). Square ends are (0, 0, 1). The start plane
     * passes through the local origin, the end plane through (0, 0, len). */
    double n0[3], n1[3];
    double cut0_deg, cut1_deg;   /* tilt of each end from square         */
    double bend0_roll, bend1_roll; /* azimuth of the inside of the bend  */

    int    part;             /* flat part built from it, or -1           */
    bool   bad_mitre;        /* too short for the bends at its ends      */
    bool   clash;            /* leaves the box, hits the engine or itself */
} PgPiece;

typedef struct {
    int    before, after;    /* piece indices either side                */
    double x;                /* centreline position                      */
    double pos[3];
    double bend_deg, roll_deg;
    int    bend_index;       /* into project route, or -1                */
    double limit_deg;        /* the most this joint should turn          */
    double radius_d;         /* tightest run of bends through it, in     */
                             /* diameters; HUGE_VAL if not in a run      */
    bool   sharp;            /* over its limit, or too tight a run       */
} PgJoint;

/* The engine drawn around the port, from bore and stroke. Rough on purpose:
 * it is there for scale and clearance, not as a model of any engine. */
typedef struct {
    double barrel_r;                   /* over the fins                   */
    double barrel_y0, barrel_y1;
    double head_r, head_y1;
    double case_min[3], case_max[3];   /* crankcase box                   */
    double port[3];                    /* port face centre                */
} PgStub;

typedef struct {
    int     n_pieces;
    PgPiece piece[PG_MAX_PIECES];
    int     n_joints;
    PgJoint joint[PG_MAX_PIECES];

    PgStub  stub;
    double  bmin[3], bmax[3];          /* bounding box of the chamber     */
    double  end[3];                    /* stinger exit centre             */
    double  thickness;
    int     n_clash_box, n_clash_engine, n_clash_self, n_bad_mitre;
    int     n_sharp;                   /* joints breaking the bend rules  */

    int     n_warn;
    char    warn[8][PG_WARN_LEN];
} PgChain;

/* PgChain is large; allocate it on the heap. */
void pg_chain_build(const PgProject *p, const PgDesign *d, PgChain *c);

/* Pieces, bends, positions and the bend rules, without the clash checks —
 * the part auto-fold evaluates thousands of times. */
void pg_chain_layout(const PgProject *p, const PgDesign *d, PgChain *c);

/*
 * Bend rules. Rules of thumb for a pipe that still behaves like the straight
 * design, not physics: a sharp mitre reflects part of the pulse early and the
 * flow separates at the corner, and both get worse as the pipe gets fatter.
 *
 *   - no joint turns more than its section's limit (the stricter of the two
 *     pieces it joins): header 30, diffuser 25, belly and baffle 20, into
 *     the stinger 15 degrees;
 *   - a run of bends — a piece bent at both ends — is no tighter than
 *     PG_MIN_BEND_RADIUS_D times the pipe's outside diameter.
 */
#define PG_MIN_BEND_RADIUS_D 2.0

double pg_section_joint_limit(PgSectionKind kind);
double pg_joint_limit(const PgChain *c, int joint);

/* Bend radius, in outside diameters, of the run through piece i when it is
 * bent at both ends: R = len / (tan(a/2) + tan(b/2)). HUGE_VAL otherwise. */
double pg_piece_bend_radius_d(const PgChain *c, int piece);

/* Closest distance between segments p0-p1 and q0-q1, with the parameters
 * (0..1) of the closest points. */
double pg_segment_distance(const double *p0, const double *p1,
                           const double *q0, const double *q1,
                           double *sp, double *sq);

void pg_engine_stub(const PgEngine *e, PgStub *s);

/* Local z at which the surface generator at azimuth `psi` (radians) meets
 * end `end` (0 start, 1 finish), for a surface whose radius runs from rA at
 * z = 0 to rB at z = len. */
double pg_piece_end_z(const PgPiece *pc, double rA, double rB, double psi, int end);

/* Engine coordinates of the surface point at azimuth `psi` and local `z`. */
void pg_piece_point(const PgPiece *pc, double rA, double rB, double psi,
                    double z, double out[3]);

/* The joint nearest a centreline position, or -1 if there are none. */
int pg_chain_joint_near(const PgChain *c, double x);

/* Words for a bend direction at the port ("down", "up" ...). */
const char *pg_roll_name(double roll_deg);

#endif /* PG_ROUTE_H */
