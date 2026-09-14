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
 * pg_help.h — the manual, as data
 *
 * One copy of the text, in a table, rendered two ways: the Help page draws it
 * in the program, and `pipegen --help-doc` writes it out as Markdown for
 * docs/HELP.md. A help file edited separately from the help page goes stale
 * the first time a control is renamed, so there is only the one.
 *
 * No SDL and no Nuklear here: this is content.
 */
#ifndef PG_HELP_H
#define PG_HELP_H

#include <stdio.h>

typedef enum {
    PG_HELP_TEXT = 0,   /* a paragraph                                    */
    PG_HELP_SUB,        /* a sub-heading within the section               */
    PG_HELP_BULLET,     /* one bullet                                     */
    PG_HELP_ROW,        /* control or key on the left, what it does right */
    PG_HELP_NOTE        /* something that will cost material or an engine */
} PgHelpKind;

typedef struct {
    PgHelpKind  kind;
    const char *a;      /* paragraph, bullet, heading, or the left column */
    const char *b;      /* PG_HELP_ROW only: the right column             */
} PgHelpItem;

typedef struct {
    const char       *title;
    const char       *intro;   /* may be NULL */
    const PgHelpItem *items;
    int               n_items;
} PgHelpSection;

const PgHelpSection *pg_help_sections(int *n_sections);

void pg_help_write_markdown(FILE *f, const char *version);

#endif /* PG_HELP_H */
