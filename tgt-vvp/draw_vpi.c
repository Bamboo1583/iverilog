/*
 * Copyright (c) 2003-2008 Stephen Williams (steve@icarus.com)
 *
 *    This source code is free software; you can redistribute it
 *    and/or modify it in source code form under the terms of the GNU
 *    General Public License as published by the Free Software
 *    Foundation; either version 2 of the License, or (at your option)
 *    any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software
 *    Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA
 */

# include  "vvp_priv.h"
# include  <string.h>
#ifdef HAVE_MALLOC_H
# include  <malloc.h>
#endif
# include  <stdlib.h>
# include  <assert.h>

#ifdef __MINGW32__  /* MinGW has inconsistent %p output. */
#define snprintf _snprintf
#endif

/*
 * Check to see if the expression (number) can be correctly represented
 * with a long variable.
 */
static int is_constant_number(ivl_expr_t ex)
{
	/* Make sure this matches the return type of constant_number(). */
      unsigned lim_wid = 8*sizeof(long);
      const char*bits;
      char pad_bit = '0';
      unsigned idx;
      unsigned nbits = ivl_expr_width(ex);

      if (ivl_expr_type(ex) != IVL_EX_NUMBER
          && ivl_expr_type(ex) != IVL_EX_ULONG)
            return 0;

      bits = ivl_expr_bits(ex);

	/* For unsigned values the effective MSB and on must be '0'. */
      if (!ivl_expr_signed(ex)) lim_wid -= 1;

	/* For negative values the pad bit is '1'. */
      if (ivl_expr_signed(ex) && bits[nbits-1]=='1') {
            pad_bit = '1';
      }

	/* For the number to fit in the variable all the upper bits must
	 * match the pad bits. */
      for (idx = lim_wid ;  idx < nbits ;  idx += 1) {
            if (bits[idx] != pad_bit) return 0;
      }

      return 1;
}

/*
 * Convert the expression (number) to a long value.
 */
static long get_constant_number(ivl_expr_t ex)
{
      long rtn = 0;

      switch (ivl_expr_type(ex)) {
	  case IVL_EX_ULONG:
	    rtn = (signed)ivl_expr_value(ex);
	    break;
	  case IVL_EX_NUMBER: {
	    unsigned idx;
	    const char*bits = ivl_expr_bits(ex);
	    unsigned nbits = ivl_expr_width(ex);
	    char pad_bit = bits[nbits-1];
	      /* Define all the bits in the long (negative numbers). */
	    for (idx = 0 ;  idx < 8*sizeof(long) ;  idx += 1) {
		  char bit;
		  if (idx < nbits) bit = bits[idx];
		  else bit = pad_bit;
		  switch (bit) {
		      case '0':
			break;
		      case '1':
			rtn |= 1 << idx;
			break;
		      default:
			assert(0);
		  }
	    }
	    break;
	  }
	  default:
	    assert(0);
      }

      return rtn;
}

static const char* magic_sfuncs[] = {
      "$time",
      "$stime",
      "$realtime",
      "$simtime",
      0
};

static int is_magic_sfunc(const char*name)
{
      int idx;
      for (idx = 0 ;  magic_sfuncs[idx] ;  idx += 1)
	    if (strcmp(magic_sfuncs[idx],name) == 0)
		  return 1;

      return 0;
}

static int is_fixed_memory_word(ivl_expr_t net)
{
      ivl_signal_t sig;

      if (ivl_expr_type(net) != IVL_EX_SIGNAL)
	    return 0;

      sig = ivl_expr_signal(net);

      if (ivl_signal_dimensions(sig) == 0)
	    return 1;

      if (ivl_signal_type(sig) == IVL_SIT_REG)
	    return 0;

      if (number_is_immediate(ivl_expr_oper1(net), 8*sizeof(unsigned)))
	    return 1;

      return 0;
}

static void draw_vpi_taskfunc_args(const char*call_string,
				   ivl_statement_t tnet,
				   ivl_expr_t fnet)
{
      unsigned idx;
      unsigned parm_count = tnet
	    ? ivl_stmt_parm_count(tnet)
	    : ivl_expr_parms(fnet);

      struct args_info {
	    char*text;
	    int vec_flag; /* True if the vec must be released. */
	    struct vector_info vec;
      } *args = calloc(parm_count, sizeof(struct args_info));

      char buffer[4096];

      ivl_parameter_t par;

	/* Figure out how many expressions are going to be evaluated
	   for this task call. I won't need to evaluate expressions
	   for items that are VPI objects directly. */
      for (idx = 0 ;  idx < parm_count ;  idx += 1) {
	    ivl_expr_t expr = tnet
		  ? ivl_stmt_parm(tnet, idx)
		  : ivl_expr_parm(fnet, idx);

	    switch (ivl_expr_type(expr)) {

		    /* These expression types can be handled directly,
		       with VPI handles of their own. Therefore, skip
		       them in the process of evaluating expressions. */
		case IVL_EX_NONE:
		  args[idx].text = strdup("\" \"");
		  continue;

		case IVL_EX_ARRAY:
		  snprintf(buffer, sizeof buffer,
			   "v%p", ivl_expr_signal(expr));
		  args[idx].text = strdup(buffer);
		  continue;

		case IVL_EX_NUMBER: {
		      unsigned bit, wid = ivl_expr_width(expr);
		      const char*bits = ivl_expr_bits(expr);
		      char*dp;

		      snprintf(buffer, sizeof buffer,
			       "%u'%sb", wid, ivl_expr_signed(expr)? "s" : "");
		      dp = buffer + strlen(buffer);
		      for (bit = wid ;  bit > 0 ;  bit -= 1)
			    *dp++ = bits[bit-1];
		      *dp++ = 0;
		      assert(dp - buffer <= sizeof buffer);
		      args[idx].text = strdup(buffer);
		      continue;
		}

		case IVL_EX_STRING:
		  if (( par = ivl_expr_parameter(expr) )) {
			snprintf(buffer, sizeof buffer, "P_%p", par);

		  } else {
			snprintf(buffer, sizeof buffer, "\"%s\"", ivl_expr_string(expr));
		  }
		  args[idx].text = strdup(buffer);
		  continue;

		case IVL_EX_EVENT:
		  snprintf(buffer, sizeof buffer, "E_%p", ivl_expr_event(expr));
		  args[idx].text = strdup(buffer);
		  continue;
		case IVL_EX_SCOPE:
		  snprintf(buffer, sizeof buffer, "S_%p", ivl_expr_scope(expr));
		  args[idx].text = strdup(buffer);
		  continue;

		case IVL_EX_SFUNC:
		  if (is_magic_sfunc(ivl_expr_name(expr))) {
			snprintf(buffer, sizeof buffer, "%s", ivl_expr_name(expr));
			args[idx].text = strdup(buffer);
			continue;
		  }
		  break;

		case IVL_EX_SIGNAL:
		    /* If the signal node is narrower then the signal
		       itself, then this is a part select so I'm going
		       to need to evaluate the expression.

		       Also, if the signedness of the expression is
		       different from the signedness of the
		       signal. This could be caused by a $signed or
		       $unsigned system function.

		       If I don't need to do any evaluating, then skip
		       it as I'll be passing the handle to the signal
		       itself. */
		  if (ivl_expr_width(expr) !=
		      ivl_signal_width(ivl_expr_signal(expr))) {
			break;

		  } else if (ivl_expr_signed(expr) !=
			     ivl_signal_signed(ivl_expr_signal(expr))) {
			break;
		  } else if (is_fixed_memory_word(expr)) {
			  /* This is a word of a non-array, or a word
			     of a net array, so we can address the
			     word directly. */
			ivl_signal_t sig = ivl_expr_signal(expr);
			unsigned use_word = 0;
			ivl_expr_t word_ex = ivl_expr_oper1(expr);
			if (word_ex) {
			        /* Some array select have been evaluated. */
			      if (number_is_immediate(word_ex, 8*sizeof(unsigned))) {
				    use_word = get_number_immediate(word_ex);
				    word_ex = 0;
			      }
			}
			if (word_ex)
			      break;

			assert(word_ex == 0);
			snprintf(buffer, sizeof buffer, "v%p_%u", sig, use_word);
			args[idx].text = strdup(buffer);
			continue;

		  } else {
			  /* What's left, this is the work of a var
			     array. Create the right code to handle
			     it. */
			ivl_signal_t sig = ivl_expr_signal(expr);
			unsigned use_word = 0;
			ivl_expr_t word_ex = ivl_expr_oper1(expr);
			if (word_ex) {
			        /* Some array select have been evaluated. */
			      if (number_is_immediate(word_ex, 8*sizeof(unsigned))) {
				    use_word = get_number_immediate(word_ex);
				    word_ex = 0;
			      }
			}
			if (word_ex && ivl_expr_type(word_ex)==IVL_EX_SIGNAL) {
				/* Special case: the index is a signal. */
			      snprintf(buffer, sizeof buffer,
				       "&A<v%p, v%p_0 >", sig, ivl_expr_signal(word_ex));
			} else if (word_ex) {
				/* fallback case: evaluate expression. */
			      struct vector_info av;
			      av = draw_eval_expr(word_ex, STUFF_OK_XZ);
			      snprintf(buffer, sizeof buffer,
				       "&A<v%p, %u %u>", sig, av.base, av.wid);
			      args[idx].vec = av;
			      args[idx].vec_flag = 1;
			} else {
			      snprintf(buffer, sizeof buffer,
				       "&A<v%p, %u>", sig, use_word);
			}
			args[idx].text = strdup(buffer);
			continue;
		  }

		case IVL_EX_SELECT: {
		  ivl_expr_t vexpr = ivl_expr_oper1(expr);
                  assert(vexpr);

		    /* This code is only for signals. */
		  if (ivl_expr_type(vexpr) != IVL_EX_SIGNAL) break;

		    /* The signal is part of an array. */
		    /* Add &APV<> code here when it is finished. */
		  if (ivl_expr_oper1(vexpr)) break;

                  ivl_expr_t bexpr = ivl_expr_oper2(expr);
                  assert(bexpr);

		    /* This is a constant bit/part select. */
                  if (is_constant_number(bexpr)) {
			snprintf(buffer, sizeof buffer, "&PV<v%p_0, %ld, %u>",
			         ivl_expr_signal(vexpr),
			         get_constant_number(bexpr),
			         ivl_expr_width(expr));
		    /* This is an indexed bit/part select. */
                  } else {
			struct vector_info rv;
			rv = draw_eval_expr(bexpr, STUFF_OK_XZ);
			snprintf(buffer, sizeof buffer, "&PV<v%p_0, %u %u, %u>",
			         ivl_expr_signal(vexpr),
			         rv.base, rv.wid,
			         ivl_expr_width(expr));
                  }
		  args[idx].text = strdup(buffer);
		  continue;
		}

		    /* Everything else will need to be evaluated and
		       passed as a constant to the vpi task. */
		default:
		  break;
	    }

	    switch (ivl_expr_value(expr)) {
		case IVL_VT_LOGIC:
		case IVL_VT_BOOL:
		  args[idx].vec_flag = 1;
		  args[idx].vec = draw_eval_expr(expr, 0);
		  snprintf(buffer, sizeof buffer,
			   "T<%u,%u,%s>", args[idx].vec.base, args[idx].vec.wid,
			   ivl_expr_signed(expr)? "s" : "u");
		  break;
		case IVL_VT_REAL:
		  args[idx].vec_flag = 1;
		  args[idx].vec.base = draw_eval_real(expr);
		  args[idx].vec.wid  = 0;
		  snprintf(buffer, sizeof buffer,
			   "W<%u,r>", args[idx].vec.base);
		  break;
		default:
		  assert(0);
	    }
	    args[idx].text = strdup(buffer);
      }

      fprintf(vvp_out, "%s", call_string);

      for (idx = 0 ;  idx < parm_count ;  idx += 1) {

	    fprintf(vvp_out, ", %s", args[idx].text);
	    free(args[idx].text);
	    if (args[idx].vec_flag) {
		  if (args[idx].vec.wid > 0)
			clr_vector(args[idx].vec);
		  else
			clr_word(args[idx].vec.base);
	    }
      }

      free(args);

      fprintf(vvp_out, ";\n");
}

void draw_vpi_task_call(ivl_statement_t tnet)
{
      char call_string[1024];
      sprintf(call_string, "    %%vpi_call %u %u \"%s\"",
              ivl_file_table_index(ivl_stmt_file(tnet)),
              ivl_stmt_lineno(tnet), ivl_stmt_name(tnet));
      draw_vpi_taskfunc_args(call_string, tnet, 0);
}

struct vector_info draw_vpi_func_call(ivl_expr_t fnet, unsigned wid)
{
      char call_string[1024];
      struct vector_info res;

      res.base = allocate_vector(wid);
      res.wid  = wid;
      if (res.base == 0) {
	    fprintf(stderr, "%s:%u: vvp.tgt error: "
		    "Unable to allocate %u thread bits for system function result.\n",
		    ivl_expr_file(fnet), ivl_expr_lineno(fnet), wid);
	    vvp_errors += 1;
      }

      sprintf(call_string, "    %%vpi_func %u %u \"%s\", %u, %u",
              ivl_file_table_index(ivl_expr_file(fnet)),
	      ivl_expr_lineno(fnet), ivl_expr_name(fnet), res.base, res.wid);

      draw_vpi_taskfunc_args(call_string, 0, fnet);

      return res;
}

int draw_vpi_rfunc_call(ivl_expr_t fnet)
{
      char call_string[1024];
      int res = allocate_word();

      sprintf(call_string, "    %%vpi_func/r %u %u \"%s\", %d",
              ivl_file_table_index(ivl_expr_file(fnet)),
	      ivl_expr_lineno(fnet), ivl_expr_name(fnet), res);

      draw_vpi_taskfunc_args(call_string, 0, fnet);

      return res;
}

