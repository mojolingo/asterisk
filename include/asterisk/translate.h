/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Translate via the use of pseudo channels
 * 
 * Copyright (C) 1999, Mark Spencer
 *
 * Mark Spencer <markster@linux-support.net>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#ifndef _ASTERISK_TRANSLATE_H
#define _ASTERISK_TRANSLATE_H

#define MAX_FORMAT 32

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

#include <asterisk/frame.h>

/* Declared by individual translators */
struct ast_translator_pvt;

struct ast_translator {
	char name[80];
	int srcfmt;
	int dstfmt;
	struct ast_translator_pvt *(*new)(void);
	int (*framein)(struct ast_translator_pvt *pvt, struct ast_frame *in);
	struct ast_frame * (*frameout)(struct ast_translator_pvt *pvt);
	void (*destroy)(struct ast_translator_pvt *pvt);
	/* For performance measurements */
	/* Generate an example frame */
	struct ast_frame * (*sample)(void);
	/* Cost in milliseconds for encoding/decoding 1 second of sound */
	int cost;
	/* For linking, not to be modified by the translator */
	struct ast_translator *next;
};

struct ast_trans_pvt;

/* Register a Codec translator */
extern int ast_register_translator(struct ast_translator *t);
/* Unregister same */
extern int ast_unregister_translator(struct ast_translator *t);
/* Given a list of sources, and a designed destination format, which should
   I choose? Returns 0 on success, -1 if no path could be found.  Modifies
   dests and srcs in place */
extern int ast_translator_best_choice(int *dsts, int *srcs);

/* Build a path (possibly NULL) from source to dest */
extern struct ast_trans_pvt *ast_translator_build_path(int dest, int source);
extern void ast_translator_free_path(struct ast_trans_pvt *tr);

/* Apply an input frame into the translator and receive zero or one output frames.  Consume
   determines whether the original frame should be freed */
extern struct ast_frame *ast_translate(struct ast_trans_pvt *tr, struct ast_frame *f, int consume);


#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif
