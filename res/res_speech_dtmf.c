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

ASTERISK_FILE_VERSION(__FILE__, "$Revision: $")

#include <asterisk/module.h>
#include <asterisk/frame.h>
#include <asterisk/format.h>
#include <asterisk/speech.h>

#define UNI_ENGINE_NAME "res_speech_dtmf"

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
  return -1;
}

/** \brief Destroy any data set on the speech structure by the engine */
static int res_speech_dtmf_destroy(struct ast_speech *speech)
{
  return -1;
}

/*! \brief Load a local grammar on the speech structure */
static int res_speech_dtmf_load_grammar(struct ast_speech *speech, const char *grammar_name, const char *grammar_path)
{
  return -1;
}

/** \brief Unload a local grammar */
static int res_speech_dtmf_unload_grammar(struct ast_speech *speech, const char *grammar_name)
{
  return -1;
}

/** \brief Activate a loaded grammar */
static int res_speech_dtmf_activate_grammar(struct ast_speech *speech, const char *grammar_name)
{
  return -1;
}

/** \brief Deactivate a loaded grammar */
static int res_speech_dtmf_deactivate_grammar(struct ast_speech *speech, const char *grammar_name)
{
  return -1;
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
