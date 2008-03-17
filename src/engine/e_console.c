#include "e_system.h"
#include "e_console.h"
#include "e_config.h"
#include "e_linereader.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>


#define CONSOLE_MAX_STR_LENGTH 255
/* the maximum number of tokens occurs in a string of length CONSOLE_MAX_STR_LENGTH with tokens size 1 separated by single spaces */
#define MAX_PARTS (CONSOLE_MAX_STR_LENGTH+1)/2

typedef struct
{ 
	char string_storage[CONSOLE_MAX_STR_LENGTH+1];
	char *args_start;
	
	const char *command;
	const char *args[MAX_PARTS]; 
	unsigned int num_args; 
} PARSE_RESULT;

static char *str_skipblanks(char *str)
{
	while(*str && (*str == ' ' || *str == '\t' || *str == '\n'))
		str++;
	return str;
}

static char *str_skiptoblank(char *str)
{
	while(*str && (*str != ' ' && *str != '\t' && *str != '\n'))
		str++;
	return str;
}

/* static int digit(char c) { return '0' <= c && c <= '9'; } */

static int console_parse_start(PARSE_RESULT *result, const char *string)
{
	char *str;
	str_copy(result->string_storage, string, sizeof(result->string_storage));
	str = result->string_storage;
	
	/* get command */
	str = str_skipblanks(str);
	result->command = str;
	str = str_skiptoblank(str);
	
	if(*str)
	{
		str[0] = 0;
		str++;
	}
	
	result->args_start = str;
	result->num_args = 0;
	return 0;
}

static int console_parse_args(PARSE_RESULT *result, const char *format)
{
	char command;
	char *str;
	int optional = 0;
	int error = 0;
	
	str = result->args_start;

	while(1)	
	{
		/* fetch command */
		command = *format;
		format++;
		
		if(!command)
			break;
		
		if(command == '?')
			optional = 1;
		else
		{
			str = str_skipblanks(str);
		
			if(!(*str)) /* error, non optional command needs value */
			{
				if(!optional)
					error = 1;
				break;
			}
			
			/* add token */
			result->args[result->num_args++] = str;
		
			if(command == 'r') /* rest of the string */
				break;
			else if(command == 'i') /* validate int */
				str = str_skiptoblank(str);
			else if(command == 'f') /* validate float */
				str = str_skiptoblank(str);
			else if(command == 's') /* validate string */
				str = str_skiptoblank(str);

			if(str[0] != 0) /* check for end of string */
			{
				str[0] = 0;
				str++;
			}
		}
	}

	return error;
}

const char *console_arg_string(void *res, int index)
{
	PARSE_RESULT *result = (PARSE_RESULT *)res;
	if (index < 0 || index >= result->num_args)
		return "";
	return result->args[index];
}

int console_arg_int(void *res, int index)
{
	PARSE_RESULT *result = (PARSE_RESULT *)res;
	if (index < 0 || index >= result->num_args)
		return 0;
	return atoi(result->args[index]);
}

float console_arg_float(void *res, int index)
{
	PARSE_RESULT *result = (PARSE_RESULT *)res;
	if (index < 0 || index >= result->num_args)
		return 0.0f;
	return atof(result->args[index]);
}

int console_arg_num(void *result)
{
	return ((PARSE_RESULT *)result)->num_args;
}

static COMMAND *first_command = 0x0;

COMMAND *console_find_command(const char *name)
{
	COMMAND *cmd;
	for (cmd = first_command; cmd; cmd = cmd->next)
	{
		if (strcmp(cmd->name, name) == 0)
			return cmd;
	}

	return 0x0;
}

void console_register(COMMAND *cmd)
{
	cmd->next = first_command;
	first_command = cmd;
}

static void (*print_callback)(const char *) = 0x0;

void console_register_print_callback(void (*callback)(const char *))
{
	print_callback = callback;
}

void console_print(const char *str)
{
	if (print_callback)
		print_callback(str);
}

void console_execute_line_stroked(int stroke, const char *str)
{
	PARSE_RESULT result;
	COMMAND *command;
	
	char strokestr[2] = {'0', 0};
	if(stroke)
		strokestr[0] = '1';

	if(console_parse_start(&result, str) != 0)
		return;

	command = console_find_command(result.command);

	if(command)
	{
		int is_stroke_command = 0;
		if(result.command[0] == '+')
		{
			/* insert the stroke direction token */
			result.args[result.num_args] = strokestr;
			result.num_args++;
			is_stroke_command = 1;
		}
		
		if(stroke || is_stroke_command)
		{
			if(console_parse_args(&result, command->params))
			{
				char buf[256];
				str_format(buf, sizeof(buf), "Invalid arguments... Usage: %s %s", command->name, command->params);
				console_print(buf);
			}
			else
				command->callback(&result, command->user_data);
		}
	}
	else
	{
		char buf[256];
		str_format(buf, sizeof(buf), "No such command: %s.", result.command);
		console_print(buf);
	}
}

void console_execute_line(const char *str)
{
	console_execute_line_stroked(1, str);
}

void console_execute_file(const char *filename)
{
	IOHANDLE file;
	file = io_open(filename, IOFLAG_READ);
	
	if(file)
	{
		char *line;
		LINEREADER lr;
		
		dbg_msg("console", "executing '%s'", filename);
		linereader_init(&lr, file);

		while((line = linereader_get(&lr)))
			console_execute_line(line);

		io_close(file);
	}
	else
		dbg_msg("console", "failed to open '%s'", filename);
}

static void echo_command(void *result, void *user_data)
{
	console_print(console_arg_string(result, 0));
}


typedef struct 
{
	CONFIG_INT_GETTER getter;
	CONFIG_INT_SETTER setter;
} INT_VARIABLE_DATA;

typedef struct
{
	CONFIG_STR_GETTER getter;
	CONFIG_STR_SETTER setter;
} STR_VARIABLE_DATA;

static void int_variable_command(void *result, void *user_data)
{
	INT_VARIABLE_DATA *data = (INT_VARIABLE_DATA *)user_data;

	if(console_arg_num(result))
		data->setter(&config, console_arg_int(result, 0));
	else
	{
		char buf[256];
		str_format(buf, sizeof(buf), "Value: %d", data->getter(&config));
		console_print(buf);
	}
}

static void str_variable_command(void *result, void *user_data)
{
	STR_VARIABLE_DATA *data = (STR_VARIABLE_DATA *)user_data;

	if(console_arg_num(result))
		data->setter(&config, console_arg_string(result, 0));
	else
	{
		char buf[256];
		str_format(buf, sizeof(buf), "Value: %s", data->getter(&config));
		console_print(buf);
	}
}

void console_init()
{
	MACRO_REGISTER_COMMAND("echo", "r", echo_command, 0x0);

	#define MACRO_CONFIG_INT(name,def,min,max) { static INT_VARIABLE_DATA data = { &config_get_ ## name, &config_set_ ## name }; MACRO_REGISTER_COMMAND(#name, "?i", int_variable_command, &data) }
	#define MACRO_CONFIG_STR(name,len,def) { static STR_VARIABLE_DATA data = { &config_get_ ## name, &config_set_ ## name }; MACRO_REGISTER_COMMAND(#name, "?r", str_variable_command, &data) }

	#include "e_config_variables.h" 

	#undef MACRO_CONFIG_INT 
	#undef MACRO_CONFIG_STR 
}