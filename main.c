#define PCRE2_CODE_UNIT_WIDTH 8

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "pcre2/pcre2.h"

#define PATTERN "\\d+(sens|respons)e"
#define JIT_STACK_MIN	16*1024
#define JIT_STACK_MAX	128*1024

static char subject[] = "h123response";
static void* my_malloc(size_t sz, void *heap)
{
	void *ptr = malloc(sz);
	printf("malloc %ld, ret: %p\n", sz, ptr);
	return ptr;
}

static void my_free(void *ptr, void *heap)
{
	printf("free %p\n", ptr);
	free(ptr);
}

static void do_jit_match(pcre2_code *re, pcre2_match_data *mdata, pcre2_match_context *mcontext)
{
	PCRE2_UCHAR buffer[256];
	PCRE2_SIZE *ovector = NULL;
	int rc;
	printf("=========do jit match=========\n");
	rc = pcre2_jit_match(re, subject, strlen(subject), 0, 0, mdata, mcontext);
	if(rc<0)
	{
		switch(rc)
		{
			case PCRE2_ERROR_NOMATCH:
				printf("No matching\n");
				break;
			default:
				pcre2_get_error_message(rc, buffer, sizeof(buffer));
				break;
		}
		return;
	}

	ovector = pcre2_get_ovector_pointer(mdata);
	printf("Matching from %lu to %lu\n", ovector[0], ovector[1]);
}

static void do_dfa_match(pcre2_code *re, pcre2_match_data *mdata, pcre2_match_context *mcontext)
{
	#define WORKSPACE_SIZE 40
	PCRE2_UCHAR buffer[256];
	PCRE2_SIZE *ovector = NULL;
	int workspace[WORKSPACE_SIZE] = {0};
	int rc=0;
	int index=0;
	uint32_t option = PCRE2_PARTIAL_SOFT;
	printf("=========do dfa match=========\n");

	for(index=0; subject[index] != '\0'; index++)
	{
		if(index)
			option |= PCRE2_DFA_RESTART;
		rc = pcre2_dfa_match(re, subject+index, 1, 0, option, mdata, mcontext, workspace, WORKSPACE_SIZE);
		if(rc<0)
		{
			switch(rc)
			{
				case PCRE2_ERROR_NOMATCH:
					printf("No matching\n");
					continue;
				case PCRE2_ERROR_PARTIAL:
					printf("partial matching index: %d\n", index);
					continue;
				default:
					pcre2_get_error_message(rc, buffer, sizeof(buffer));
					printf("Error: %s\n", buffer);
					return;
			}
		}

		ovector = pcre2_get_ovector_pointer(mdata);
		printf("Matching from %lu to %lu\n", ovector[0], ovector[1]);
	}
}

int main(int argc, char *argv[])
{
	pcre2_general_context *gcontext = NULL;
	pcre2_compile_context *ccontext = NULL;
	pcre2_match_context *mcontext = NULL;
	pcre2_jit_stack *jit_stack = NULL;
	pcre2_code *re = NULL;
	pcre2_match_data *match_data = NULL;
	PCRE2_SIZE erroffset = 0;
	PCRE2_UCHAR buffer[256];
	int errorcode = 0;
	int rc = 0;
	
	gcontext = pcre2_general_context_create(my_malloc, my_free, NULL);
	if(!gcontext)
	{
		printf("failed to create general context\n");
		goto EXIT;
	}

	ccontext = pcre2_compile_context_create(gcontext);
	if(!ccontext)
	{
		printf("failed to create compile context\n");
		goto EXIT;
	}
	pcre2_set_character_tables(ccontext, NULL);
	pcre2_set_bsr(ccontext, PCRE2_BSR_ANYCRLF);
	pcre2_set_max_pattern_length(ccontext, PCRE2_UNSET);
	pcre2_set_newline(ccontext, PCRE2_NEWLINE_LF);
	pcre2_set_parens_nest_limit(ccontext, 250);

	mcontext = pcre2_match_context_create(gcontext);
	if(!mcontext)
	{
		printf("failed to create match context\n");
		goto EXIT;
	}
	
	jit_stack = pcre2_jit_stack_create(JIT_STACK_MIN, JIT_STACK_MAX, gcontext);
	if(!jit_stack)
	{
		printf("failed to create jit stack context\n");
		goto EXIT;
	}
	pcre2_jit_stack_assign(mcontext, NULL, jit_stack);

	re = pcre2_compile(
			PATTERN,                /* the pattern */
			PCRE2_ZERO_TERMINATED,  /* the pattern is zero-terminated */
			0,                      /* default options */
			&errorcode,             /* for error code */
			&erroffset,             /* for error offset */
			ccontext);              /* no compile context */

	if (re == NULL)
	{   
		pcre2_get_error_message(errorcode, buffer, sizeof(buffer));
		printf("PCRE2 compilation failed at offset %d: %s\n", (int)erroffset, buffer);
		goto EXIT;
	}
	
	errorcode = pcre2_jit_compile(re, PCRE2_JIT_COMPLETE);
	if(errorcode < 0)
	{
		pcre2_get_error_message(errorcode, buffer, sizeof(buffer));
		printf("PCRE2 jit compilation failed: %s\n", buffer);
		goto EXIT;
	}

	match_data = pcre2_match_data_create_from_pattern(re, gcontext);
	if(!match_data)
	{
		printf("failed to create match data\n");
		goto EXIT;
	}
	
	do_dfa_match(re, match_data, mcontext);
	do_jit_match(re, match_data, mcontext);
EXIT:
	printf("start cleanup\n");
	if(match_data)
		pcre2_match_data_free(match_data);
	if(re)
		pcre2_code_free(re);
	if(jit_stack)
		pcre2_jit_stack_free(jit_stack);
	if(mcontext)
		pcre2_match_context_free(mcontext);
	if(ccontext)
		pcre2_compile_context_free(ccontext);
	if(gcontext)
		pcre2_general_context_free(gcontext);
	return 0;
}
