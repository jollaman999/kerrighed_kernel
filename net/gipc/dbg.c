/*
 * net/gipc/dbg.c: GIPC print buffer routines for debugging
 *
 * Copyright (c) 1996-2006, Ericsson AB
 * Copyright (c) 2005-2007, Wind River Systems
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the names of the copyright holders nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "core.h"
#include "config.h"
#include "dbg.h"

/*
 * GIPC pre-defines the following print buffers:
 *
 * GIPC_NULL : null buffer (i.e. print nowhere)
 * GIPC_CONS : system console
 * GIPC_LOG  : GIPC log buffer
 *
 * Additional user-defined print buffers are also permitted.
 */

static struct print_buf null_buf = { NULL, 0, NULL, 0 };
struct print_buf *const GIPC_NULL = &null_buf;

static struct print_buf cons_buf = { NULL, 0, NULL, 1 };
struct print_buf *const GIPC_CONS = &cons_buf;

static struct print_buf log_buf = { NULL, 0, NULL, 1 };
struct print_buf *const GIPC_LOG = &log_buf;

/*
 * Locking policy when using print buffers.
 *
 * 1) gipc_printf() uses 'print_lock' to protect against concurrent access to
 * 'print_string' when writing to a print buffer. This also protects against
 * concurrent writes to the print buffer being written to.
 *
 * 2) gipc_dump() and gipc_log_XXX() leverage the aforementioned
 * use of 'print_lock' to protect against all types of concurrent operations
 * on their associated print buffer (not just write operations).
 *
 * Note: All routines of the form gipc_printbuf_XXX() are lock-free, and rely
 * on the caller to prevent simultaneous use of the print buffer(s) being
 * manipulated.
 */

static char print_string[GIPC_PB_MAX_STR];
static DEFINE_SPINLOCK(print_lock);


#define FORMAT(PTR,LEN,FMT) \
{\
       va_list args;\
       va_start(args, FMT);\
       LEN = vsprintf(PTR, FMT, args);\
       va_end(args);\
       *(PTR + LEN) = '\0';\
}

/**
 * gipc_printbuf_init - initialize print buffer to empty
 * @pb: pointer to print buffer structure
 * @raw: pointer to character array used by print buffer
 * @size: size of character array
 *
 * Note: If the character array is too small (or absent), the print buffer
 * becomes a null device that discards anything written to it.
 */

void gipc_printbuf_init(struct print_buf *pb, char *raw, u32 size)
{
	pb->buf = raw;
	pb->crs = raw;
	pb->size = size;
	pb->echo = 0;

	if (size < GIPC_PB_MIN_SIZE) {
		pb->buf = NULL;
	} else if (raw) {
		pb->buf[0] = 0;
		pb->buf[size - 1] = ~0;
	}
}

/**
 * gipc_printbuf_reset - reinitialize print buffer to empty state
 * @pb: pointer to print buffer structure
 */

void gipc_printbuf_reset(struct print_buf *pb)
{
	if (pb->buf) {
		pb->crs = pb->buf;
		pb->buf[0] = 0;
		pb->buf[pb->size - 1] = ~0;
	}
}

/**
 * gipc_printbuf_empty - test if print buffer is in empty state
 * @pb: pointer to print buffer structure
 *
 * Returns non-zero if print buffer is empty.
 */

int gipc_printbuf_empty(struct print_buf *pb)
{
	return (!pb->buf || (pb->crs == pb->buf));
}

/**
 * gipc_printbuf_validate - check for print buffer overflow
 * @pb: pointer to print buffer structure
 *
 * Verifies that a print buffer has captured all data written to it.
 * If data has been lost, linearize buffer and prepend an error message
 *
 * Returns length of print buffer data string (including trailing NUL)
 */

int gipc_printbuf_validate(struct print_buf *pb)
{
	char *err = "\n\n*** PRINT BUFFER OVERFLOW ***\n\n";
	char *cp_buf;
	struct print_buf cb;

	if (!pb->buf)
		return 0;

	if (pb->buf[pb->size - 1] == 0) {
		cp_buf = kmalloc(pb->size, GFP_ATOMIC);
		if (cp_buf) {
			gipc_printbuf_init(&cb, cp_buf, pb->size);
			gipc_printbuf_move(&cb, pb);
			gipc_printbuf_move(pb, &cb);
			kfree(cp_buf);
			memcpy(pb->buf, err, strlen(err));
		} else {
			gipc_printbuf_reset(pb);
			gipc_printf(pb, err);
		}
	}
	return (pb->crs - pb->buf + 1);
}

/**
 * gipc_printbuf_move - move print buffer contents to another print buffer
 * @pb_to: pointer to destination print buffer structure
 * @pb_from: pointer to source print buffer structure
 *
 * Current contents of destination print buffer (if any) are discarded.
 * Source print buffer becomes empty if a successful move occurs.
 */

void gipc_printbuf_move(struct print_buf *pb_to, struct print_buf *pb_from)
{
	int len;

	/* Handle the cases where contents can't be moved */

	if (!pb_to->buf)
		return;

	if (!pb_from->buf) {
		gipc_printbuf_reset(pb_to);
		return;
	}

	if (pb_to->size < pb_from->size) {
		strcpy(pb_to->buf, "*** PRINT BUFFER MOVE ERROR ***");
		pb_to->buf[pb_to->size - 1] = ~0;
		pb_to->crs = strchr(pb_to->buf, 0);
		return;
	}

	/* Copy data from char after cursor to end (if used) */

	len = pb_from->buf + pb_from->size - pb_from->crs - 2;
	if ((pb_from->buf[pb_from->size - 1] == 0) && (len > 0)) {
		strcpy(pb_to->buf, pb_from->crs + 1);
		pb_to->crs = pb_to->buf + len;
	} else
		pb_to->crs = pb_to->buf;

	/* Copy data from start to cursor (always) */

	len = pb_from->crs - pb_from->buf;
	strcpy(pb_to->crs, pb_from->buf);
	pb_to->crs += len;

	gipc_printbuf_reset(pb_from);
}

/**
 * gipc_printf - append formatted output to print buffer
 * @pb: pointer to print buffer
 * @fmt: formatted info to be printed
 */

void gipc_printf(struct print_buf *pb, const char *fmt, ...)
{
	int chars_to_add;
	int chars_left;
	char save_char;

	spin_lock_bh(&print_lock);

	FORMAT(print_string, chars_to_add, fmt);
	if (chars_to_add >= GIPC_PB_MAX_STR)
		strcpy(print_string, "*** PRINT BUFFER STRING TOO LONG ***");

	if (pb->buf) {
		chars_left = pb->buf + pb->size - pb->crs - 1;
		if (chars_to_add <= chars_left) {
			strcpy(pb->crs, print_string);
			pb->crs += chars_to_add;
		} else if (chars_to_add >= (pb->size - 1)) {
			strcpy(pb->buf, print_string + chars_to_add + 1
			       - pb->size);
			pb->crs = pb->buf + pb->size - 1;
		} else {
			strcpy(pb->buf, print_string + chars_left);
			save_char = print_string[chars_left];
			print_string[chars_left] = 0;
			strcpy(pb->crs, print_string);
			print_string[chars_left] = save_char;
			pb->crs = pb->buf + chars_to_add - chars_left;
		}
	}

	if (pb->echo)
		printk("%s", print_string);

	spin_unlock_bh(&print_lock);
}

#ifdef CONFIG_GIPC_DEBUG

/**
 * print_to_console - write string of bytes to console in multiple chunks
 */

static void print_to_console(char *crs, int len)
{
	int rest = len;

	while (rest > 0) {
		int sz = rest < GIPC_PB_MAX_STR ? rest : GIPC_PB_MAX_STR;
		char c = crs[sz];

		crs[sz] = 0;
		printk((const char *)crs);
		crs[sz] = c;
		rest -= sz;
		crs += sz;
	}
}

/**
 * printbuf_dump - write print buffer contents to console
 */

static void printbuf_dump(struct print_buf *pb)
{
	int len;

	if (!pb->buf) {
		printk("*** PRINT BUFFER NOT ALLOCATED ***");
		return;
	}

	/* Dump print buffer from char after cursor to end (if used) */

	len = pb->buf + pb->size - pb->crs - 2;
	if ((pb->buf[pb->size - 1] == 0) && (len > 0))
		print_to_console(pb->crs + 1, len);

	/* Dump print buffer from start to cursor (always) */

	len = pb->crs - pb->buf;
	print_to_console(pb->buf, len);
}

/**
 * gipc_dump_dbg - dump (non-console) print buffer to console
 * @pb: pointer to print buffer
 */

void gipc_dump_dbg(struct print_buf *pb, const char *fmt, ...)
{
	int len;

	if (pb == GIPC_CONS)
		return;

	spin_lock_bh(&print_lock);

	FORMAT(print_string, len, fmt);
	printk(print_string);

	printk("\n---- Start of %s log dump ----\n\n",
	       (pb == GIPC_LOG) ? "global" : "local");
	printbuf_dump(pb);
	gipc_printbuf_reset(pb);
	printk("\n---- End of dump ----\n");

	spin_unlock_bh(&print_lock);
}

#endif

/**
 * gipc_log_resize - change the size of the GIPC log buffer
 * @log_size: print buffer size to use
 */

int gipc_log_resize(int log_size)
{
	int res = 0;

	spin_lock_bh(&print_lock);
	if (GIPC_LOG->buf) {
		kfree(GIPC_LOG->buf);
		GIPC_LOG->buf = NULL;
	}
	if (log_size) {
		if (log_size < GIPC_PB_MIN_SIZE)
			log_size = GIPC_PB_MIN_SIZE;
		res = GIPC_LOG->echo;
		gipc_printbuf_init(GIPC_LOG, kmalloc(log_size, GFP_ATOMIC),
				   log_size);
		GIPC_LOG->echo = res;
		res = !GIPC_LOG->buf;
	}
	spin_unlock_bh(&print_lock);

	return res;
}

/**
 * gipc_log_resize_cmd - reconfigure size of GIPC log buffer
 */

struct sk_buff *gipc_log_resize_cmd(const void *req_tlv_area, int req_tlv_space)
{
	u32 value;

	if (!TLV_CHECK(req_tlv_area, req_tlv_space, GIPC_TLV_UNSIGNED))
		return gipc_cfg_reply_error_string(GIPC_CFG_TLV_ERROR);

	value = ntohl(*(__be32 *)TLV_DATA(req_tlv_area));
	if (value != delimit(value, 0, 32768))
		return gipc_cfg_reply_error_string(GIPC_CFG_INVALID_VALUE
						   " (log size must be 0-32768)");
	if (gipc_log_resize(value))
		return gipc_cfg_reply_error_string(
			"unable to create specified log (log size is now 0)");
	return gipc_cfg_reply_none();
}

/**
 * gipc_log_dump - capture GIPC log buffer contents in configuration message
 */

struct sk_buff *gipc_log_dump(void)
{
	struct sk_buff *reply;

	spin_lock_bh(&print_lock);
	if (!GIPC_LOG->buf) {
		spin_unlock_bh(&print_lock);
		reply = gipc_cfg_reply_ultra_string("log not activated\n");
	} else if (gipc_printbuf_empty(GIPC_LOG)) {
		spin_unlock_bh(&print_lock);
		reply = gipc_cfg_reply_ultra_string("log is empty\n");
	}
	else {
		struct tlv_desc *rep_tlv;
		struct print_buf pb;
		int str_len;

		str_len = min(GIPC_LOG->size, 32768u);
		spin_unlock_bh(&print_lock);
		reply = gipc_cfg_reply_alloc(TLV_SPACE(str_len));
		if (reply) {
			rep_tlv = (struct tlv_desc *)reply->data;
			gipc_printbuf_init(&pb, TLV_DATA(rep_tlv), str_len);
			spin_lock_bh(&print_lock);
			gipc_printbuf_move(&pb, GIPC_LOG);
			spin_unlock_bh(&print_lock);
			str_len = strlen(TLV_DATA(rep_tlv)) + 1;
			skb_put(reply, TLV_SPACE(str_len));
			TLV_SET(rep_tlv, GIPC_TLV_ULTRA_STRING, NULL, str_len);
		}
	}
	return reply;
}

