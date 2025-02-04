/* $OpenBSD: cmd-command-prompt.c,v 1.56 2021/08/13 06:50:42 nicm Exp $ */

/*
 * Copyright (c) 2008 Nicholas Marriott <nicholas.marriott@gmail.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF MIND, USE, DATA OR PROFITS, WHETHER
 * IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING
 * OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "tmux.h"

/*
 * Prompt for command in client.
 */

static enum cmd_retval	cmd_command_prompt_exec(struct cmd *,
			    struct cmdq_item *);

static int	cmd_command_prompt_callback(struct client *, void *,
		    const char *, int);
static void	cmd_command_prompt_free(void *);

const struct cmd_entry cmd_command_prompt_entry = {
	.name = "command-prompt",
	.alias = NULL,

	.args = { "1bFkiI:Np:t:T:", 0, 1 },
	.usage = "[-1bFkiN] [-I inputs] [-p prompts] " CMD_TARGET_CLIENT_USAGE
		 " [-T type] [template]",

	.flags = CMD_CLIENT_TFLAG,
	.exec = cmd_command_prompt_exec
};

struct cmd_command_prompt_cdata {
	struct cmdq_item	*item;
	struct cmd_parse_input	 pi;

	int			 flags;
	enum prompt_type	 prompt_type;

	char			*inputs;
	char			*next_input;

	char			*prompts;
	char			*next_prompt;

	char			*template;
	int	 		 idx;
};

static enum cmd_retval
cmd_command_prompt_exec(struct cmd *self, struct cmdq_item *item)
{
	struct args			*args = cmd_get_args(self);
	struct client			*tc = cmdq_get_target_client(item);
	struct cmd_find_state		*target = cmdq_get_target(item);
	const char			*inputs, *prompts, *type;
	struct cmd_command_prompt_cdata	*cdata;
	char				*prompt, *ptr, *input = NULL;
	size_t				 n;
	int				 wait = !args_has(args, 'b');

	if (tc->prompt_string != NULL)
		return (CMD_RETURN_NORMAL);

	cdata = xcalloc(1, sizeof *cdata);
	cdata->idx = 1;

	cmd_get_source(self, &cdata->pi.file, &cdata->pi.line);
	if (wait)
		cdata->pi.item = item;
	cdata->pi.c = tc;
	cmd_find_copy_state(&cdata->pi.fs, target);

	if (wait)
		cdata->item = item;

	if (args->argc != 0 && args_has(args, 'F'))
	    cdata->template = format_single_from_target(item, args->argv[0]);
	else if (args->argc != 0)
		cdata->template = xstrdup(args->argv[0]);
	else
		cdata->template = xstrdup("%1");

	if ((prompts = args_get(args, 'p')) != NULL)
		cdata->prompts = xstrdup(prompts);
	else if (args->argc != 0) {
		n = strcspn(cdata->template, " ,");
		xasprintf(&cdata->prompts, "(%.*s) ", (int) n, cdata->template);
	} else
		cdata->prompts = xstrdup(":");

	/* Get first prompt. */
	cdata->next_prompt = cdata->prompts;
	ptr = strsep(&cdata->next_prompt, ",");
	if (prompts == NULL)
		prompt = xstrdup(ptr);
	else
		xasprintf(&prompt, "%s ", ptr);

	/* Get initial prompt input. */
	if ((inputs = args_get(args, 'I')) != NULL) {
		cdata->inputs = xstrdup(inputs);
		cdata->next_input = cdata->inputs;
		input = strsep(&cdata->next_input, ",");
	}

	/* Get prompt type. */
	if ((type = args_get(args, 'T')) != NULL) {
		cdata->prompt_type = status_prompt_type(type);
		if (cdata->prompt_type == PROMPT_TYPE_INVALID) {
			cmdq_error(item, "unknown type: %s", type);
			return (CMD_RETURN_ERROR);
		}
	} else
		cdata->prompt_type = PROMPT_TYPE_COMMAND;

	if (args_has(args, '1'))
		cdata->flags |= PROMPT_SINGLE;
	else if (args_has(args, 'N'))
		cdata->flags |= PROMPT_NUMERIC;
	else if (args_has(args, 'i'))
		cdata->flags |= PROMPT_INCREMENTAL;
	else if (args_has(args, 'k'))
		cdata->flags |= PROMPT_KEY;
	status_prompt_set(tc, target, prompt, input,
	    cmd_command_prompt_callback, cmd_command_prompt_free, cdata,
	    cdata->flags, cdata->prompt_type);
	free(prompt);

	if (!wait)
		return (CMD_RETURN_NORMAL);
	return (CMD_RETURN_WAIT);
}

static int
cmd_command_prompt_callback(struct client *c, void *data, const char *s,
    int done)
{
	struct cmd_command_prompt_cdata	*cdata = data;
	char				*new_template, *prompt, *ptr, *error;
	char				*input = NULL;
	struct cmdq_item		*item = cdata->item;
	enum cmd_parse_status		 status;

	if (s == NULL)
		goto out;
	if (done && (cdata->flags & PROMPT_INCREMENTAL))
		goto out;

	new_template = cmd_template_replace(cdata->template, s, cdata->idx);
	if (done) {
		free(cdata->template);
		cdata->template = new_template;
	}

	/*
	 * Check if there are more prompts; if so, get its respective input
	 * and update the prompt data.
	 */
	if (done && (ptr = strsep(&cdata->next_prompt, ",")) != NULL) {
		xasprintf(&prompt, "%s ", ptr);
		input = strsep(&cdata->next_input, ",");
		status_prompt_update(c, prompt, input);

		free(prompt);
		cdata->idx++;
		return (1);
	}

	if (item != NULL) {
		status = cmd_parse_and_insert(new_template, &cdata->pi, item,
		    cmdq_get_state(item), &error);
	} else {
		status = cmd_parse_and_append(new_template, &cdata->pi, c, NULL,
		    &error);
	}
	if (status == CMD_PARSE_ERROR) {
		cmdq_append(c, cmdq_get_error(error));
		free(error);
	}

	if (!done)
		free(new_template);
	if (c->prompt_inputcb != cmd_command_prompt_callback)
		return (1);

out:
        if (item != NULL)
                cmdq_continue(item);
	return (0);
}

static void
cmd_command_prompt_free(void *data)
{
	struct cmd_command_prompt_cdata	*cdata = data;

	free(cdata->inputs);
	free(cdata->prompts);
	free(cdata->template);
	free(cdata);
}
