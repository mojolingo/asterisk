/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * ChanSpy Listen in on any channel.
 * 
 * Copyright (C) 2005 Anthony Minessale II (anthmct@yahoo.com)
 *
 * Disclaimed to Digium
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#include <asterisk/file.h>
#include <asterisk/logger.h>
#include <asterisk/channel.h>
#include <asterisk/features.h>
#include <asterisk/options.h>
#include <asterisk/app.h>
#include <asterisk/utils.h>
#include <asterisk/say.h>
#include <asterisk/pbx.h>
#include <asterisk/translate.h>
#include <asterisk/module.h>
#include <asterisk/lock.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>

AST_MUTEX_DEFINE_STATIC(modlock);

#define ast_fit_in_short(in) (in < -32768 ? -32768 : in > 32767 ? 32767 : in)
#define find_smallest_of_three(a, b, c) ((a < b && a < c) ? a : (b < a && b < c) ? b : c)
#define AST_NAME_STRLEN 256
#define ALL_DONE(u, ret) LOCAL_USER_REMOVE(u); return ret;
#define get_volfactor(x) x ? ((x > 0) ? (1 << x) : ((1 << abs(x)) * -1)) : 0
#define minmax(x,y) x ? (x > y) ? y : ((x < (y * -1)) ? (y * -1) : x) : 0



static char *synopsis = "Tap into any type of asterisk channel and listen to audio";
static char *app = "ChanSpy";
static char *desc = "   Chanspy([<scanspec>][|<options>])\n\n"
"Valid Options:\n"
" - q: quiet, don't announce channels beep, etc.\n"
" - b: bridged, only spy on channels involved in a bridged call.\n"
" - v([-4..4]): adjust the initial volume. (negative is quieter)\n"
" - g(grp): enforce group.  Match only calls where their ${SPYGROUP} is 'grp'.\n\n"
"If <scanspec> is specified, only channel names *beginning* with that string will be scanned.\n"
"('all' or an empty string are also both valid <scanspec>)\n\n"
"While Spying:\n\n"
"Dialing # cycles the volume level.\n"
"Dialing * will stop spying and look for another channel to spy on.\n"
"Dialing a series of digits followed by # builds a channel name to append to <scanspec>\n"
"(e.g. run Chanspy(Agent) and dial 1234# while spying to jump to channel Agent/1234)\n\n"
"";

#define OPTION_QUIET	 (1 << 0)	/* Quiet, no announcement */
#define OPTION_BRIDGED   (1 << 1)	/* Only look at bridged calls */
#define OPTION_VOLUME    (1 << 2)	/* Specify initial volume */
#define OPTION_GROUP     (1 << 3)   /* Only look at channels in group */

AST_DECLARE_OPTIONS(chanspy_opts,{
	['q'] = { OPTION_QUIET },
	['b'] = { OPTION_BRIDGED },
	['v'] = { OPTION_VOLUME, 1 },
	['g'] = { OPTION_GROUP, 2 },
});

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

struct chanspy_translation_helper {
	struct ast_trans_pvt *trans0;
	struct ast_trans_pvt *trans1;
	int volfactor;
	struct ast_channel_spy *spy;
};

/* Prototypes */
static struct ast_channel *local_get_channel_by_name(char *name);
static struct ast_channel *local_channel_walk(struct ast_channel *chan);
static void spy_release(struct ast_channel *chan, void *data);
static void *spy_alloc(struct ast_channel *chan, void *params);
static struct ast_frame *spy_queue_shift(struct ast_channel_spy *spy, int qnum);
static void ast_flush_spy_queue(struct ast_channel_spy *spy);
static int spy_generate(struct ast_channel *chan, void *data, int len, int samples);
static void start_spying(struct ast_channel *chan, struct ast_channel *spychan, struct ast_channel_spy *spy);
static void stop_spying(struct ast_channel *chan, struct ast_channel_spy *spy);
static int channel_spy(struct ast_channel *chan, struct ast_channel *spyee, int *volfactor);
static int chanspy_exec(struct ast_channel *chan, void *data);


static struct ast_channel *local_get_channel_by_name(char *name) 
{
	struct ast_channel *ret;
	ast_mutex_lock(&modlock);
	if ((ret = ast_get_channel_by_name_locked(name))) {
		ast_mutex_unlock(&ret->lock);
	}
	ast_mutex_unlock(&modlock);

	return ret;
}

static struct ast_channel *local_channel_walk(struct ast_channel *chan) 
{
	struct ast_channel *ret;
	ast_mutex_lock(&modlock);	
	if ((ret = ast_channel_walk_locked(chan))) {
		ast_mutex_unlock(&ret->lock);
	}
	ast_mutex_unlock(&modlock);			
	return ret;
}

static void spy_release(struct ast_channel *chan, void *data) 
{
	struct chanspy_translation_helper *csth = data;
	int same=0;

	same = (csth->trans0 == csth->trans1) ? 1 : 0;

	if (csth->trans0) {
		ast_translator_free_path(csth->trans0);
		csth->trans0 = NULL;
	}
	if (same)
		return;
	if (csth->trans1) {
		ast_translator_free_path(csth->trans1);
		csth->trans1 = NULL;
	}
	return;
}

static void *spy_alloc(struct ast_channel *chan, void *params) 
{
	return params;
}

static struct ast_frame *spy_queue_shift(struct ast_channel_spy *spy, int qnum) 
{
	struct ast_frame *f;
	
	if (qnum < 0 || qnum > 1)
		return NULL;

	f = spy->queue[qnum];
	if (f) {
		spy->queue[qnum] = f->next;
		return f;
	}
	return NULL;
}


static void ast_flush_spy_queue(struct ast_channel_spy *spy) 
{
	struct ast_frame *f=NULL;
	int x = 0;
	ast_mutex_lock(&spy->lock);
	for(x=0;x<2;x++) {
		f = NULL;
		while((f = spy_queue_shift(spy, x))) 
			ast_frfree(f);
	}
	ast_mutex_unlock(&spy->lock);
}


static int spy_generate(struct ast_channel *chan, void *data, int len, int samples) 
{
	struct ast_frame *f0, *f1, write_frame, *f;
	int x=0, framelen_a = 0, framelen_b = 0, size = 0;
	short buf[320], buf0[320], buf1[320];
	struct chanspy_translation_helper *csth = data;
	int nc = 0, vf;
	
	ast_mutex_lock(&csth->spy->lock);
	f0 = spy_queue_shift(csth->spy, 0);
	f1 = spy_queue_shift(csth->spy, 1);
	ast_mutex_unlock(&csth->spy->lock);

	if (f0 && f1) {
		if (!csth->trans0) {
			if (f0->subclass != AST_FORMAT_SLINEAR && (csth->trans0 = ast_translator_build_path(AST_FORMAT_SLINEAR, f0->subclass)) == NULL) {
				ast_log(LOG_WARNING, "Cannot build a path from %s to slin\n", ast_getformatname(f0->subclass));
				return -1;
			}
			if (!csth->trans1) {
				if (f1->subclass == f0->subclass) {
					csth->trans1 = csth->trans0;
				} else if (f1->subclass != AST_FORMAT_SLINEAR && (csth->trans1 = ast_translator_build_path(AST_FORMAT_SLINEAR, f1->subclass)) == NULL) {
					ast_log(LOG_WARNING, "Cannot build a path from %s to slin\n", ast_getformatname(f1->subclass));
					return -1;
				}
				
			}
		}

		memset(buf, 0, sizeof(buf));
		memset(buf0, 0, sizeof(buf0));
		memset(buf1, 0, sizeof(buf1));
		if (csth->trans0) {
			if ((f = ast_translate(csth->trans0, f0, 0))) {
				framelen_a = f->datalen * sizeof(short);
				memcpy(buf0, f->data, framelen_a);
				ast_frfree(f);
			} else 
				return 0;
		} else {
			framelen_a = f0->datalen * sizeof(short);
			memcpy(buf0, f0->data, framelen_a);
		}
		if (csth->trans1) {
			if ((f = ast_translate(csth->trans1, f1, 0))) {
				framelen_b = f->datalen * sizeof(short);
				memcpy(buf1, f->data, framelen_b);
				ast_frfree(f);
			} else 
				return 0;
		} else {
			framelen_b = f1->datalen * sizeof(short);
			memcpy(buf1, f1->data, framelen_b);
		}
		size = find_smallest_of_three(len, framelen_a, framelen_b);

		vf = get_volfactor(csth->volfactor);
		vf = minmax(vf, 16);
		for(x=0; x < size; x++) {
			if (vf < 0) {
				buf0[x] /= abs(vf);
				buf1[x] /= abs(vf);
			} else if (vf > 0) {
				buf0[x] *= vf;
				buf1[x] *= vf;
			}
			buf[x] = ast_fit_in_short(buf0[x] + buf1[x]);
		}
		memset(&write_frame, 0, sizeof(write_frame));
		write_frame.frametype = AST_FRAME_VOICE;
		write_frame.subclass = AST_FORMAT_SLINEAR;
		write_frame.datalen = size;
		write_frame.samples = size;
		write_frame.data = buf;
		write_frame.offset = 0;
		ast_write(chan, &write_frame);
	} else {
		nc++;
		if(nc > 1) {
			return -1;
		}
	}
	
	if (f0)
		ast_frfree(f0);
	if (f1) 
		ast_frfree(f1);

	return 0;

}

static struct ast_generator spygen = {
    alloc: spy_alloc, 
    release: spy_release, 
    generate: spy_generate, 
};

static void start_spying(struct ast_channel *chan, struct ast_channel *spychan, struct ast_channel_spy *spy) 
{

	struct ast_channel_spy *cptr=NULL;
	struct ast_channel *peer;


	ast_log(LOG_WARNING, "Attaching %s to %s\n", spychan->name, chan->name);


	ast_mutex_lock(&chan->lock);
	if (chan->spiers) {
		for(cptr=chan->spiers;cptr && cptr->next;cptr=cptr->next);
		cptr->next = spy;
	} else {
		chan->spiers = spy;
	}
	ast_mutex_unlock(&chan->lock);
	if ( ast_test_flag(chan, AST_FLAG_NBRIDGE) && (peer = ast_bridged_channel(chan))) {
		ast_softhangup(peer, AST_SOFTHANGUP_UNBRIDGE);	
	}

}

static void stop_spying(struct ast_channel *chan, struct ast_channel_spy *spy) 
{
	struct ast_channel_spy *cptr=NULL, *prev=NULL;
	int count = 0;

	while(ast_mutex_trylock(&chan->lock)) {
		/* if its locked already it's almost surely hanging up and we are too late 
		   we can safely remove the head pointer if it points at us without needing a lock.
		   since everybody spying will be in the same boat whomever is pointing at the head
		   will surely erase it which is all we really need since it's a linked list of
		   staticly declared structs that belong to each spy.
		*/
		if (chan->spiers == spy) {
			chan->spiers = NULL;
			return;
		}
		count++;
		if(count > 10) {
			return;
		}
		sched_yield();
	}

	for(cptr=chan->spiers; cptr; cptr=cptr->next) {
		if (cptr == spy) {
			if (prev) {
				prev->next = cptr->next;
				cptr->next = NULL;
			} else
				chan->spiers = NULL;
		}
		prev = cptr;
	}
	ast_mutex_unlock(&chan->lock);

}

static int channel_spy(struct ast_channel *chan, struct ast_channel *spyee, int *volfactor) 
{
	struct chanspy_translation_helper csth;
	int running = 1, res = 0, x = 0;
	char inp[24];
	char *name=NULL;
	struct ast_channel_spy spy;

	if (chan && !ast_check_hangup(chan) && spyee && !ast_check_hangup(spyee)) {
		memset(inp, 0, sizeof(inp));
		name = ast_strdupa(spyee->name);
		if (option_verbose >= 2)
			ast_verbose(VERBOSE_PREFIX_2 "Spying on channel %s\n", name);
		
		memset(&spy, 0, sizeof(struct ast_channel_spy));
		spy.status = CHANSPY_RUNNING;
		ast_mutex_init(&spy.lock);
		start_spying(spyee, chan, &spy);
		
		memset(&csth, 0, sizeof(csth));
		csth.volfactor = *volfactor;
		csth.spy = &spy;
		ast_activate_generator(chan, &spygen, &csth);

		while(spy.status == CHANSPY_RUNNING && chan && !ast_check_hangup(chan) && spyee && !ast_check_hangup(spyee) && running == 1) {
			res = ast_waitfordigit(chan, 100);

			if (x == sizeof(inp)) {
				x = 0;
			}
			if (res < 0) {
				running = -1;
			}
			if (res == 0) {
				continue;
			} else if (res == '*') {
				running = 0; 
			} else if (res == '#') {
				if (!ast_strlen_zero(inp)) {
					running = x ? atoi(inp) : -1;
					break;
				} else {
					csth.volfactor++;
					if (csth.volfactor > 4) {
						csth.volfactor = -4;
					}
					if (option_verbose > 2) {
						ast_verbose(VERBOSE_PREFIX_3"Setting spy volume on %s to %d\n", chan->name, csth.volfactor);
					}
					*volfactor = csth.volfactor;
				}
			} else if (res >= 48 && res <= 57) {
				inp[x++] = res;
			}
		}
		ast_deactivate_generator(chan);
		stop_spying(spyee, &spy);

		if (option_verbose >= 2)
			ast_verbose(VERBOSE_PREFIX_2 "Done Spying on channel %s\n", name);

		ast_flush_spy_queue(&spy);
	} else {
		running = 0;
	}
	ast_mutex_destroy(&spy.lock);
	return running;
}



static int chanspy_exec(struct ast_channel *chan, void *data)
{
	int res=-1;
	struct localuser *u;
	struct ast_channel *peer=NULL, *prev=NULL;
	char *ptr=NULL;
	char name[AST_NAME_STRLEN], peer_name[AST_NAME_STRLEN];
	int count=0, waitms=100, num=0;
	char *args;
	struct ast_flags flags;
	char *options=NULL;
	char *spec = NULL;
	int volfactor=0;
	int silent=0;
	int argc = 0;
	char *argv[5];
	char *mygroup = NULL;
	int bronly = 0;


	if (!(args = ast_strdupa((char *)data))) {
		ast_log(LOG_ERROR, "Out of memory!\n");
		return -1;
	}

	if (ast_set_read_format(chan, AST_FORMAT_SLINEAR) < 0) {
		ast_log(LOG_ERROR, "Could Not Set Read Format.\n");
		return -1;
	}
	if (ast_set_write_format(chan, AST_FORMAT_SLINEAR) < 0) {
		ast_log(LOG_ERROR, "Could Not Set Write Format.\n");
		return -1;
	}

	LOCAL_USER_ADD(u);
	ast_answer(chan);

	ast_set_flag(chan, AST_FLAG_SPYING); /* so nobody can spy on us while we are spying */


	if ((argc = ast_separate_app_args(args, '|', argv, sizeof(argv) / sizeof(argv[0])))) {
		spec = argv[0];
		if ( argc > 1) {
			options = argv[1];
		}
		if (ast_strlen_zero(spec) || !strcmp(spec, "all")) {
			spec = NULL;
		}
	}
	
	if (options) {
		char *opts[2];
		ast_parseoptions(chanspy_opts, &flags, opts, options);
		if (ast_test_flag(&flags, OPTION_GROUP))
			mygroup = opts[0];
		silent = ast_test_flag(&flags, OPTION_QUIET);
		bronly = ast_test_flag(&flags, OPTION_BRIDGED);
		if (ast_test_flag(&flags, OPTION_VOLUME) && opts[1]) {
			if (sscanf(opts[1], "%d", &volfactor) != 1)
				ast_log(LOG_NOTICE, "volfactor must be a number between -16 and 16\n");
			else if (volfactor > 16)
				volfactor = 16;
			else if (volfactor < -16)
				volfactor = -16;
		}
	}


	for(;;) {
		res = ast_streamfile(chan, "beep", chan->language);
		if (!res)
			res = ast_waitstream(chan, "");
		if (res < 0) {
			ast_clear_flag(chan, AST_FLAG_SPYING);
			ALL_DONE(u, -1);
		}			

		count = 0;
		res = ast_waitfordigit(chan, waitms);
		if (res < 0) {
			ast_clear_flag(chan, AST_FLAG_SPYING);
			ALL_DONE(u, -1);
		}
				
		peer = local_channel_walk(NULL);
		prev=NULL;
		while(peer) {
			if (peer != chan) {
				char *group = NULL;

				if (peer == prev) {
					break;
				}

				group = pbx_builtin_getvar_helper(chan, "SPYGROUP");

				if (mygroup && group && strcmp(group, mygroup)) { 
					continue;
				}
				if (!spec || ((strlen(spec) < strlen(peer->name) && 
							   !strncasecmp(peer->name, spec, strlen(spec))))) {
						
					if (peer && (!bronly || ast_bridged_channel(peer)) &&
						!ast_check_hangup(peer) && !ast_test_flag(peer, AST_FLAG_SPYING)) {
						int x = 0;

						strncpy(peer_name, peer->name, AST_NAME_STRLEN);
						ptr = strchr(peer_name, '/');
						*ptr = '\0';
						ptr++;
						for (x = 0 ; x < strlen(peer_name) ; x++) {
							if(peer_name[x] == '/') {
								break;
							}
							peer_name[x] = tolower(peer_name[x]);
						}

						if (!silent) {
							if (ast_fileexists(peer_name, NULL, NULL) != -1) {
								res = ast_streamfile(chan, peer_name, chan->language);
								if (!res)
									res = ast_waitstream(chan, "");
								if (res)
									break;
							} else
								res = ast_say_character_str(chan, peer_name, "", chan->language);
							if ((num=atoi(ptr))) 
								ast_say_digits(chan, atoi(ptr), "", chan->language);
						}
						count++;
						prev = peer;
						res = channel_spy(chan, peer, &volfactor);
						if (res == -1) {
							ast_clear_flag(chan, AST_FLAG_SPYING);
							ALL_DONE(u, -1);
						} else if (res > 1 && spec) {
							snprintf(name, AST_NAME_STRLEN, "%s/%d", spec, res);
							if (!silent)
								ast_say_digits(chan, res, "", chan->language);
							peer=local_get_channel_by_name(name);
							continue;
						}
					}
				}
			}

			if ((peer = local_channel_walk(peer)) == NULL)
				break;
		}
		waitms = count ? 100 : 5000;
	}
	

	ast_clear_flag(chan, AST_FLAG_SPYING);
	ALL_DONE(u, res);
}

int unload_module(void)
{
	STANDARD_HANGUP_LOCALUSERS;
	return ast_unregister_application(app);
}

int load_module(void)
{
	return ast_register_application(app, chanspy_exec, synopsis, desc);
}

char *description(void)
{
	return synopsis;
}

int usecount(void)
{
	int res;
	STANDARD_USECOUNT(res);
	return res;
}

char *key()
{
	return ASTERISK_GPL_KEY;
}
