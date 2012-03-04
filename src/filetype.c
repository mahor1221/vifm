/* vifm
 * Copyright (C) 2001 Ken Steen.
 * Copyright (C) 2011 xaizek.
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include <curses.h>
#include <regex.h>

#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "status.h"

#include "filetype.h"

static assoc_t *all_filetypes;
static int nfiletypes;

static void set_ext_programs(const char *extension, const char *programs,
		const char *description, int for_x);
static int add_assoc(assoc_t **arr, int count, const char *extension,
		const char *programs, const char *description);
static void free_assoc(assoc_t *assoc);
static void safe_free(char **adr);

static char *
to_regex(const char *global)
{
	char *result = strdup("^$");
	int result_len = 1;
	while(*global != '\0')
	{
		if(strchr("^.$()|+{", *global) != NULL)
		{
		  if(*global != '^' || result[result_len - 1] != '[')
			{
				result = realloc(result, result_len + 2 + 1 + 1);
				result[result_len++] = '\\';
			}
		}
		else if(*global == '!' && result[result_len - 1] == '[')
		{
			result = realloc(result, result_len + 2 + 1 + 1);
			result[result_len++] = '^';
			continue;
		}
		else if(*global == '\\')
		{
			result = realloc(result, result_len + 2 + 1 + 1);
			result[result_len++] = *global++;
		}
		else if(*global == '?')
		{
			result = realloc(result, result_len + 1 + 1 + 1);
			result[result_len++] = '.';
			global++;
			continue;
		}
		else if(*global == '*')
		{
			if(result_len == 1)
			{
				result = realloc(result, result_len + 9 + 1 + 1);
				result[result_len++] = '[';
				result[result_len++] = '^';
				result[result_len++] = '.';
				result[result_len++] = ']';
				result[result_len++] = '.';
				result[result_len++] = '*';
			}
			else
			{
				result = realloc(result, result_len + 2 + 1 + 1);
				result[result_len++] = '.';
				result[result_len++] = '*';
			}
			global++;
			continue;
		}
		else
		{
			result = realloc(result, result_len + 1 + 1 + 1);
		}
		result[result_len++] = *global++;
	}
	result[result_len++] = '$';
	result[result_len] = '\0';
	return result;
}

static int
global_matches(const char *global, const char *file)
{
	char *regex;
	regex_t re;

	regex = to_regex(global);

	if(regcomp(&re, regex, REG_EXTENDED | REG_ICASE) == 0)
	{
		if(regexec(&re, file, 0, NULL, 0) == 0)
		{
			regfree(&re);
			free(regex);
			return 1;
		}
	}
	regfree(&re);
	free(regex);
	return 0;
}

static int
matches_assoc(const char *file, const assoc_t *assoc)
{
	char *exptr;

	/* Only one extension */
	if((exptr = strchr(assoc->ext, ',')) == NULL)
	{
		if(global_matches(assoc->ext, file))
			return 1;
	}
	else
	{
		char *ex_copy = strdup(assoc->ext);
		char *free_this = ex_copy;
		while((exptr = strchr(ex_copy, ',')) != NULL)
		{
			*exptr++ = '\0';

			if(global_matches(ex_copy, file))
			{
				free(free_this);
				return 1;
			}

			ex_copy = exptr;
		}
		if(global_matches(ex_copy, file))
		{
			free(free_this);
			return 1;
		}
		free(free_this);
	}
	return 0;
}

static int
get_filetype_number(const char *file, int count, assoc_t *array)
{
	int x;

	for(x = 0; x < count; x++)
	{
		if(matches_assoc(file, array + x))
			return x;
	}
	return -1;
}

void
replace_double_comma(char *cmd, int put_null)
{
	char *p = cmd;
	while(*cmd != '\0')
	{
		if(cmd[0] == ',')
		{
			if(cmd[1] == ',')
			{
				*p++ = *cmd++;
				cmd++;
				continue;
			}
			else if(put_null)
			{
				break;
			}
		}
		*p++ = *cmd++;
	}
	*p = '\0';
}

int
get_default_program_for_file(const char *file, assoc_prog_t *result)
{
	int x;

	x = get_filetype_number(file, nfiletypes, all_filetypes);
	if(x < 0)
		return 0;

	result->com = strdup(all_filetypes[x].program.com);
	result->description = strdup(all_filetypes[x].program.description);

	replace_double_comma(result->com, 1);
	return 1;
}

char *
get_viewer_for_file(char *file)
{
	int x = get_filetype_number(file, cfg.fileviewers_num, fileviewers);

	if(x < 0)
		return NULL;

	return fileviewers[x].program.com;
}

char *
get_all_programs_for_file(const char *file)
{
	int x;
	char *result = NULL;
	size_t len = 0;

	for(x = 0; x < nfiletypes; x++)
	{
		size_t new_size;
		if(!matches_assoc(file, all_filetypes + x))
			continue;
		new_size = len + 1 + strlen(all_filetypes[x].program.com) + 1;
		result = realloc(result, new_size);
		if(len > 0)
			result[len++] = ',';
		strcpy(result + len, all_filetypes[x].program.com);
		len += strlen(result + len);
	}

	return result;
}

void
set_programs(const char *extensions, const char *programs,
		const char *description, int x)
{
	char *exptr;
	if((exptr = strchr(extensions, ',')) == NULL)
	{
		set_ext_programs(extensions, programs, description, x);
	}
	else
	{
		char *ex_copy = strdup(extensions);
		char *free_this = ex_copy;
		while((exptr = strchr(ex_copy, ',')) != NULL)
		{
			*exptr++ = '\0';

			set_ext_programs(ex_copy, programs, description, x);

			ex_copy = exptr;
		}
		set_ext_programs(ex_copy, programs, description, x);
		free(free_this);
	}
}

static void
set_ext_programs(const char *extension, const char *programs,
		const char *description, int for_x)
{
	if(extension[0] == '\0')
		return;

	if(for_x)
		cfg.xfiletypes_num = add_assoc(&xfiletypes, cfg.xfiletypes_num, extension,
				programs, description);
	else
		cfg.filetypes_num = add_assoc(&filetypes, cfg.filetypes_num, extension,
				programs, description);
	if(!for_x || !curr_stats.is_console)
		nfiletypes = add_assoc(&all_filetypes, nfiletypes, extension, programs,
				description);
}

static void
set_ext_viewer(const char *extension, const char *viewer)
{
	int x;

	if(extension[0] == '\0')
		return;

	for(x = 0; x < cfg.fileviewers_num; x++)
		if(strcasecmp(fileviewers[x].ext, extension) == 0)
			break;
	if(x == cfg.fileviewers_num)
	{
		cfg.fileviewers_num = add_assoc(&fileviewers, cfg.fileviewers_num,
				extension, viewer, "");
	}
	else
	{
		free(fileviewers[x].program.com);
		fileviewers[x].program.com = strdup(viewer);
	}
}

static int
add_assoc(assoc_t **arr, int count, const char *extension, const char *programs,
		const char *description)
{
	*arr = realloc(*arr, (count + 1)*sizeof(assoc_t));

	(*arr)[count].ext = strdup(extension);
	(*arr)[count].program.com = strdup(programs);
	(*arr)[count].program.description = strdup(description);
	return count + 1;
}

void
set_fileviewer(const char *extensions, const char *viewer)
{
	char *exptr;
	if((exptr = strchr(extensions, ',')) == NULL)
	{
		set_ext_viewer(extensions, viewer);
	}
	else
	{
		char *ex_copy = strdup(extensions);
		char *free_this = ex_copy;
		char *exptr2 = NULL;
		while((exptr = exptr2 = strchr(ex_copy, ',')) != NULL)
		{
			*exptr = '\0';
			exptr2++;

			set_ext_viewer(ex_copy, viewer);

			ex_copy = exptr2;
		}
		set_ext_viewer(ex_copy, viewer);
		free(free_this);
	}
}

static void
reset_list(assoc_t **arr, int *size)
{
	int i;

	for(i = 0; i < *size; i++)
	{
		free_assoc(&(*arr)[i]);
	}

	free(*arr);
	*arr = NULL;
	*size = 0;
}

void
reset_filetypes(void)
{
	reset_list(&filetypes, &cfg.filetypes_num);
	reset_list(&all_filetypes, &nfiletypes);
}

void
reset_xfiletypes(void)
{
	reset_list(&xfiletypes, &cfg.xfiletypes_num);
}

void
reset_fileviewers(void)
{
	reset_list(&fileviewers, &cfg.fileviewers_num);
}

static void
free_assoc(assoc_t *assoc)
{
	safe_free(&assoc->ext);
	free_assoc_prog(&assoc->program);
}

void
free_assoc_prog(assoc_prog_t *assoc_prog)
{
	safe_free(&assoc_prog->com);
	safe_free(&assoc_prog->description);
}

static void
safe_free(char **adr)
{
	free(*adr);
	*adr = NULL;
}

int
assoc_prog_is_empty(const assoc_prog_t *assoc_prog)
{
	return assoc_prog->com == NULL && assoc_prog->description == NULL;
}

/* vim: set tabstop=2 softtabstop=2 shiftwidth=2 noexpandtab cinoptions-=(0 : */
/* vim: set cinoptions+=t0 : */
