/*
 * Asterisk -- An open source telephony toolkit.
 *
 * See http://www.asterisk.org for more information about
 * the Asterisk project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 *
 * Please follow coding guidelines
 * http://svn.digium.com/view/asterisk/trunk/doc/CODING-GUIDELINES
 */

/*! \file
 *
 * \brief Implementation of a DTMF detector for res_speech
 *
 * \author Ben Langfeld ben@langfeld.me
 */

#include <asterisk.h>

#include <stdbool.h>

ASTERISK_FILE_VERSION(__FILE__, "$Revision: $")

#include <asterisk/module.h>
#include <asterisk/frame.h>
#include <asterisk/format.h>
#include <asterisk/speech.h>

#define UNI_ENGINE_NAME "res_speech_dtmf"

/** \brief Forward declaration of speech */
typedef struct dtmf_recog dtmf_recog;

/** \brief Declaration of DTMF recognizer structure */
struct dtmf_recog {
  /* Name of the speech object to be used for logging */
  const char            *name;
  /* Asterisk speech base */
  struct ast_speech     *speech_base;
  /* Loaded grammar */
  const char            *grammar;
  /* Wether or not grammar is active */
  bool                  active;
};

static int res_speech_dtmf_create(struct ast_speech *speech, struct ast_format *format);
static int res_speech_dtmf_load_grammar(struct ast_speech *speech, const char *grammar_name, const char *grammar_path);
static int res_speech_dtmf_unload_grammar(struct ast_speech *speech, const char *grammar_name);
static int res_speech_dtmf_activate_grammar(struct ast_speech *speech, const char *grammar_name);
static int res_speech_dtmf_deactivate_grammar(struct ast_speech *speech, const char *grammar_name);
static int res_speech_dtmf_write(struct ast_speech *speech, void *data, int len);
static int res_speech_dtmf_dtmf(struct ast_speech *speech, const char *dtmf);
static int res_speech_dtmf_start(struct ast_speech *speech);
static int res_speech_dtmf_change(struct ast_speech *speech, const char *name, const char *value);
static int res_speech_dtmf_change_results_type(struct ast_speech *speech,enum ast_speech_results_type results_type);
struct ast_speech_result* res_speech_dtmf_get(struct ast_speech *speech);

/** \brief Set up the speech structure within the engine */
static int res_speech_dtmf_create(struct ast_speech *speech, struct ast_format *format)
{
  dtmf_recog *recog = malloc(sizeof(dtmf_recog));
  recog->speech_base = speech;
  recog->grammar = NULL;
  recog->active = false;
  speech->data = recog;
  return 0;
}

/** \brief Destroy any data set on the speech structure by the engine */
static int res_speech_dtmf_destroy(struct ast_speech *speech)
{
  speech->data = NULL;
  return 0;
}

/*! \brief Load a local grammar on the speech structure */
static int res_speech_dtmf_load_grammar(struct ast_speech *speech, const char *grammar_name, const char *grammar_path)
{
  dtmf_recog *recog = speech->data;
  const char *content_type = NULL;
  const char *body = NULL;
  bool inline_content = false;
  char *tmp;

  if(recog->grammar) {
    ast_log(LOG_ERROR, "(%s) Unable to load grammar name: %s type: %s path: %s because there is already a grammar\n",
      recog->name,
      grammar_name,
      content_type,
      grammar_path);
    return -1;
  }

  /*
   * Grammar name and path are mandatory attributes,
   * grammar type can be optionally specified with path.
   *
   * SpeechLoadGrammar(name|path)
   * SpeechLoadGrammar(name|type:path)
   * SpeechLoadGrammar(name|uri:path)
   * SpeechLoadGrammar(name|builtin:grammar/digits)
  */

  tmp = strchr(grammar_path,':');
  if(tmp) {
    const char builtin_token[] = "builtin";
    const char uri_token[] = "uri";
    if(strncmp(grammar_path,builtin_token,sizeof(builtin_token)-1) == 0) {
      content_type = "text/uri-list";
      inline_content = true;
      body = grammar_path;
    }
    else if(strncmp(grammar_path,uri_token,sizeof(uri_token)-1) == 0) {
      content_type = "text/uri-list";
      inline_content = true;
      grammar_path = tmp+1;
    }
    else {
      *tmp = '\0';
      content_type = grammar_path;
      grammar_path = tmp+1;
    }
  }

  if(inline_content != true) {
    ast_log(LOG_WARNING, "(%s) Fetching of grammar %s from file not supported\n",recog->name,grammar_path);
    return -1;
  }

  if(!body) {
    ast_log(LOG_WARNING, "(%s) No grammar content available %s\n",recog->name,grammar_path);
    return -1;
  }

  /* Try to implicitly detect content type, if it's not specified */
  if(!content_type) {
    if(strstr(body,"#JSGF")) {
      content_type = "application/x-jsgf";
    }
    else if(strstr(body,"#ABNF")) {
      content_type = "application/srgs";
    }
    else {
      content_type = "application/srgs+xml";
    }
  }

  ast_log(LOG_NOTICE, "(%s) Load grammar name: %s type: %s path: %s\n",
        recog->name,
        grammar_name,
        content_type,
        grammar_path);

  recog->grammar = grammar_path;

  return 0;
}

/** \brief Unload a local grammar */
static int res_speech_dtmf_unload_grammar(struct ast_speech *speech, const char *grammar_name)
{
  dtmf_recog *recog = speech->data;
  recog->grammar = NULL;
  recog->active = false;
  return 0;
}

/** \brief Activate a loaded grammar */
static int res_speech_dtmf_activate_grammar(struct ast_speech *speech, const char *grammar_name)
{
  dtmf_recog *recog = speech->data;
  recog->active = true;
  return 0;
}

/** \brief Deactivate a loaded grammar */
static int res_speech_dtmf_deactivate_grammar(struct ast_speech *speech, const char *grammar_name)
{
  dtmf_recog *recog = speech->data;
  recog->active = false;
  return 0;
}

/** \brief Write audio to the speech engine */
static int res_speech_dtmf_write(struct ast_speech *speech, void *data, int len)
{
  return -1;
}

/** \brief Signal DTMF was received */
static int res_speech_dtmf_dtmf(struct ast_speech *speech, const char *dtmf)
{
  return -1;
}

/** brief Prepare engine to accept audio */
static int res_speech_dtmf_start(struct ast_speech *speech)
{
  return -1;
}

/** \brief Change an engine specific setting */
static int res_speech_dtmf_change(struct ast_speech *speech, const char *name, const char *value)
{
  return -1;
}

/** \brief Change the type of results we want back */
static int res_speech_dtmf_change_results_type(struct ast_speech *speech, enum ast_speech_results_type results_type)
{
  return -1;
}

/** \brief Try to get result */
struct ast_speech_result* res_speech_dtmf_get(struct ast_speech *speech)
{
  struct ast_speech_result *speech_result;
  speech_result = ast_calloc(sizeof(struct ast_speech_result), 1);
  return speech_result;
}

/** \brief Speech engine declaration */
static struct ast_speech_engine ast_engine = {
  UNI_ENGINE_NAME,
  res_speech_dtmf_create,
  res_speech_dtmf_destroy,
  res_speech_dtmf_load_grammar,
  res_speech_dtmf_unload_grammar,
  res_speech_dtmf_activate_grammar,
  res_speech_dtmf_deactivate_grammar,
  res_speech_dtmf_write,
  res_speech_dtmf_dtmf,
  res_speech_dtmf_start,
  res_speech_dtmf_change,
  res_speech_dtmf_change_results_type,
  res_speech_dtmf_get
};

/** \brief Load module */
static int load_module(void)
{
  ast_log(LOG_NOTICE, "Load Res-Speech-DTMF module\n");

  if(ast_speech_register(&ast_engine)) {
    ast_log(LOG_ERROR, "Failed to register module\n");
    return AST_MODULE_LOAD_FAILURE;
  }

  return AST_MODULE_LOAD_SUCCESS;
}

/** \brief Unload module */
static int unload_module(void)
{
  ast_log(LOG_NOTICE, "Unload Res-Speech-DTMF module\n");
  if(ast_speech_unregister(UNI_ENGINE_NAME)) {
    ast_log(LOG_ERROR, "Failed to unregister module\n");
  }

  return 0;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "res_speech DTMF Recognizer");
