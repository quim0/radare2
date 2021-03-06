/* radare - LGPL - Copyright 2019 - pancake */

#include <r_util/r_table.h>
#include "r_cons.h"

// cant do that without globals because RList doesnt have void *user :(
static bool Ginc = false;
static int Gnth = 0;
static RListComparator Gcmp = NULL;

static int sortString(const void *a, const void *b) {
	return strcmp (a, b);
}

static int sortNumber(const void *a, const void *b) {
	return r_num_get (NULL, a) - r_num_get (NULL, b);
}

// maybe just index by name instead of exposing those symbols as global
static RTableColumnType r_table_type_string = { "string", sortString };
static RTableColumnType r_table_type_number = { "number", sortNumber };

R_API RTableColumnType *r_table_type (const char *name) {
	if (!strcmp (name, "string")) {
		return &r_table_type_string;
	}
	if (!strcmp (name, "number")) {
		return &r_table_type_number;
	}
	return NULL;
}


// TODO: unused for now, maybe good to call after filter :?
static void __table_adjust(RTable *t) {
	RListIter *iter, *iter2;
	RTableColumn *col;
	RTableRow  *row;
	r_list_foreach (t->cols, iter, col) {
		int itemLength = r_str_len_utf8 (col->name) + 1;
		col->width = itemLength;
	}
	r_list_foreach (t->rows, iter, row) {
		const char *item;
		int ncol = 0;
		r_list_foreach (row->items, iter2, item) {
			int itemLength = r_str_len_utf8 (item) + 1;
			RTableColumn *c = r_list_get_n (t->cols, ncol);
			if (c) {
				c->width = R_MAX (c->width, itemLength);
			}
			ncol ++;
		}
	}
}

R_API void r_table_row_free(void *_row) {
	RTableRow *row = _row;
	free (row);
}

R_API void r_table_column_free(void *_col) {
	RTableColumn *col = _col;
	free (col->name);
	free (col);
}

R_API RTable *r_table_new() {
	RTable *t = R_NEW0 (RTable);
	if (t) {
		t->showHeader = true;
		t->cols = r_list_newf (r_table_column_free);
		t->rows = r_list_newf (r_table_row_free);
		t->showSum = false;
	}
	return t;
}

R_API void r_table_free(RTable *t) {
	r_list_free (t->cols);
	r_list_free (t->rows);
	free (t);
}

R_API void r_table_add_column(RTable *t, RTableColumnType *type, const char *name, int maxWidth) {
	RTableColumn *c = R_NEW0 (RTableColumn);
	if (c) {
		c->name = strdup (name);
		c->maxWidth = maxWidth;
		c->type = type;
		int itemLength = r_str_len_utf8 (name) + 1;
		c->width = itemLength;
		r_list_append (t->cols, c);
		c->total = -1;
	}
}

R_API RTableRow *r_table_row_new(RList *items) {
	RTableRow *row = R_NEW (RTableRow);
	row->items = items;
	return row;
}

static bool addRow (RTable *t, RList *items, const char *arg, int col) {
	int itemLength = r_str_len_utf8 (arg) + 1;
	RTableColumn *c = r_list_get_n (t->cols, col);
	if (c) {
		c->width = R_MAX (c->width, itemLength);
		r_list_append (items, strdup (arg));
		return true;
	}
	return false;
}

R_API void r_table_add_row_list(RTable *t, RList *items) {
	RTableRow *row = r_table_row_new (items);
	r_list_append (t->rows, row);
	// throw warning if not enough columns defined in header
	t->totalCols = R_MAX (t->totalCols, r_list_length (items));
}

R_API void r_table_set_columnsf(RTable *t, const char *fmt, ...) {
	va_list ap;
	va_start (ap, fmt);
	RTableColumnType *typeString = r_table_type ("string");
	RTableColumnType *typeNumber = r_table_type ("number");
	const char *name;
	const char *f = fmt;
	for (;*f;f++) {
		name = va_arg (ap, const char *);
		if (!name) {
			break;
		}
		switch (*f) {
		case 's':
		case 'z':
			r_table_add_column (t, typeString, name, 0);
			break;
		case 'i':
		case 'd':
			r_table_add_column (t, typeNumber, name, 0);
			break;
		case 'n':
			r_table_add_column (t, typeNumber, name, 0);
			break;
		case 'x':
			r_table_add_column (t, typeNumber, name, 0);
			break;
		default:
			eprintf ("Invalid format string char '%c', use 's' or 'n'\n", *f);
			break;
		}
	}
}

R_API void r_table_add_rowf(RTable *t, const char *fmt, ...) {
	va_list ap;
	va_start (ap, fmt);
	RList *list = r_list_newf (free);
	const char *f = fmt;
	const char *arg = NULL;
	for (; *f; f++) {
		switch (*f) {
		case 's':
		case 'z':
			arg = va_arg (ap, const char *);
			r_list_append (list, strdup (arg?arg:""));
			break;
		case 'i':
		case 'd':
			r_list_append (list, r_str_newf ("%d", va_arg (ap, int)));
			break;
		case 'n':
			r_list_append (list, r_str_newf ("%"PFMT64d, va_arg (ap, ut64)));
			break;
		case 'u':
			r_list_append (list, r_num_units (NULL, 32, va_arg (ap, ut64)));
			break;
		case 'x':
			{
				ut64 n = va_arg (ap, ut64);
				if (n == UT64_MAX) {
					r_list_append (list, "-1");
				} else {
					r_list_append (list, r_str_newf ("0x%"PFMT64x, n));
				}
			}
			break;
		default:
			eprintf ("Invalid format string char '%c', use 's' or 'n'\n", *f);
			break;
		}
	}
	r_table_add_row_list (t, list);
}

R_API void r_table_add_row(RTable *t, const char *name, ...) {
	va_list ap;
	va_start (ap, name);
	int col = 0;
	RList *items = r_list_newf (free);
	addRow (t, items, name, col++);
	for (;;) {
		const char *arg = va_arg (ap, const char *);
		if (!arg) {
			break;
		}
		addRow (t, items, arg, col);
		// TODO: assert if number of columns doesnt match t->cols
		col++;
	}
	va_end (ap);
	RTableRow *row = r_table_row_new (items);
	r_list_append (t->rows, row);
	// throw warning if not enough columns defined in header
	t->totalCols = R_MAX (t->totalCols, r_list_length (items));
}

// import / export

static void __strbuf_append_col_aligned_fancy(RTable *t, RStrBuf *sb, RTableColumn *col, char *str) {
	RCons *cons = (RCons *) t->cons;
	const char *v_line = (cons && (cons->use_utf8 ||  cons->use_utf8_curvy)) ? RUNE_LINE_VERT : "|";
	switch (col->align) {
	case R_TABLE_ALIGN_LEFT:
		r_strbuf_appendf (sb, "%s %-*s ", v_line, col->width, str);
		break;
	case R_TABLE_ALIGN_RIGHT:
		r_strbuf_appendf (sb, "%s %*s ", v_line, col->width, str);
		break;
	case R_TABLE_ALIGN_CENTER:
		{
			int len = r_str_len_utf8 (str);
			int pad = (col->width - len) / 2;
			int left = col->width - (pad * 2 + len);
			r_strbuf_appendf (sb, "%s %-*s ", v_line, pad, " ");
			r_strbuf_appendf (sb, "%-*s ", pad + left, str);
			break;
		}
	}
}

static void __computeTotal(RTable *t) {
	RTableRow *row;
	RListIter *iter, *iter2;
	r_list_foreach (t->rows, iter, row) {
		char *item;
		int c = 0;
		r_list_foreach (row->items, iter2, item) {
			RTableColumn *col = r_list_get_n (t->cols, c);
			if (!r_str_cmp (col->type->name, "number", r_str_ansi_len ("number")) && r_str_isnumber (item)) {
				if (col->total < 0) {
					col->total = 0;
				}
				col->total += sdb_atoi(item);
			}
			c++;
		}
	}
}

R_API char *r_table_tofancystring(RTable *t) {
	RStrBuf *sb = r_strbuf_new ("");
	RTableRow *row;
	RTableColumn *col;
	RCons *cons = (RCons *)t->cons;
	RListIter *iter, *iter2;
	bool useUtf8 = (cons && cons->use_utf8);
	bool useUtf8Curvy = (cons && cons->use_utf8_curvy);
	const char *v_line = useUtf8 ||  useUtf8Curvy ? RUNE_LINE_VERT : "|";
	const char *h_line = useUtf8 || useUtf8Curvy ? RUNE_LINE_HORIZ : "-";
	const char *l_intersect = useUtf8 ||  useUtf8Curvy ? RUNE_LINE_VERT : ")";
	const char *r_intersect = useUtf8 ||  useUtf8Curvy ? RUNE_LINE_VERT : "(";
	const char *tl_corner = useUtf8 ? (useUtf8Curvy ? RUNE_CURVE_CORNER_TL : RUNE_CORNER_TL) : ".";
	const char *tr_corner = useUtf8 ? (useUtf8Curvy ? RUNE_CURVE_CORNER_TR : RUNE_CORNER_TR) : ".";
	const char *bl_corner = useUtf8 ? (useUtf8Curvy ? RUNE_CURVE_CORNER_BL : RUNE_CORNER_BL) : "`";
	const char *br_corner = useUtf8 ? (useUtf8Curvy ? RUNE_CURVE_CORNER_BR : RUNE_CORNER_BR) : "'";
	__table_adjust (t);

	r_list_foreach (t->cols, iter, col) {
		__strbuf_append_col_aligned_fancy (t, sb, col, col->name);
	}
	int len = r_str_len_utf8_ansi (r_strbuf_get (sb)) - 1;
	{
		char *s = r_str_newf ("%s%s%s\n", tl_corner, r_str_repeat (h_line, len), tr_corner);
		r_strbuf_prepend (sb, s);
		free (s);
	}

	r_strbuf_appendf (sb, "%s\n%s%s%s\n", v_line, l_intersect, r_str_repeat (h_line, len), r_intersect);
	r_list_foreach (t->rows, iter, row) {
		char *item;
		int c = 0;
		r_list_foreach (row->items, iter2, item) {
			RTableColumn *col = r_list_get_n (t->cols, c);
			if (col) {
				__strbuf_append_col_aligned_fancy (t, sb, col, item);
			}
			c++;
		}
		r_strbuf_appendf (sb, "%s\n", v_line);
	}

	if (t->showSum) {
		char tmp[64];
		__computeTotal (t);
		r_strbuf_appendf (sb, "%s%s%s\n", l_intersect, r_str_repeat (h_line, len), r_intersect);
		r_list_foreach (t->cols, iter, col) {
			char *num = col->total == -1 ? "" : sdb_itoa (col->total, tmp, 10);
			__strbuf_append_col_aligned_fancy (t, sb, col, num);
		}
		r_strbuf_appendf (sb, "%s\n", v_line);
	}
	r_strbuf_appendf (sb, "%s%s%s\n", bl_corner, r_str_repeat (h_line, len), br_corner);
	return r_strbuf_drain (sb);
}

static void __strbuf_append_col_aligned(RStrBuf *sb, RTableColumn *col, const char *str, bool nopad) {
	if (nopad) {
		r_strbuf_appendf (sb, "%s", str);
		return;
	}
	switch (col->align) {
	case R_TABLE_ALIGN_LEFT:
		r_strbuf_appendf (sb, "%-*s", col->width, str);
		break;
	case R_TABLE_ALIGN_RIGHT:
		r_strbuf_appendf (sb, "%*s", col->width, str);
		break;
	case R_TABLE_ALIGN_CENTER:
		{
			int len = r_str_len_utf8 (str);
			int pad = (col->width - len) / 2;
			int left = col->width - (pad * 2 + len);
			r_strbuf_appendf (sb, "%-*s", pad, " ");
			r_strbuf_appendf (sb, "%-*s", pad + left, str);
			break;
		}
	}
}

R_API char *r_table_tostring(RTable *t) {
	RStrBuf *sb = r_strbuf_new ("");
	RTableRow *row;
	RTableColumn *col;
	RListIter *iter, *iter2;
	RCons *cons = (RCons *) t->cons;
	const char *h_line = (cons && (cons->use_utf8 || cons->use_utf8_curvy)) ? RUNE_LONG_LINE_HORIZ : "-";
	__table_adjust (t);
	if (t->showHeader) {
		r_list_foreach (t->cols, iter, col) {
			bool nopad = !iter->n;
			__strbuf_append_col_aligned (sb, col, col->name, nopad);
		}
		int len = r_str_len_utf8_ansi (r_strbuf_get (sb)) - 1;
		r_strbuf_appendf (sb, "\n%s\n", r_str_repeat (h_line, len));
	}
	r_list_foreach (t->rows, iter, row) {
		char *item;
		int c = 0;
		r_list_foreach (row->items, iter2, item) {
			bool nopad = !iter2->n;
			RTableColumn *col = r_list_get_n (t->cols, c);
			if (col) {
				__strbuf_append_col_aligned (sb, col, item, nopad);
			}
			c++;
		}
		r_strbuf_append (sb, "\n");
	}
	if (t->showSum) {
		char tmp[64];
		__computeTotal (t);
		int len = r_str_len_utf8_ansi (r_strbuf_get (sb)) - 1;
		r_strbuf_appendf (sb, "\n%s\n", r_str_repeat (h_line, len));
		r_list_foreach (t->cols, iter, col) {
			bool nopad = !iter->n;
			__strbuf_append_col_aligned (sb, col, sdb_itoa (col->total, tmp, 10), nopad);
		}
	}
	return r_strbuf_drain (sb);
}

R_API char *r_table_tocsv(RTable *t) {
	RStrBuf *sb = r_strbuf_new ("");
	RTableRow *row;
	RTableColumn *col;
	RListIter *iter, *iter2;
	if (t->showHeader) {
		const char *comma = "";
		r_list_foreach (t->cols, iter, col) {
			if (strchr (col->name, ',')) {
				// TODO. escaped string?
				r_strbuf_appendf (sb, "%s\"%s\"", comma, col->name);
			} else {
				r_strbuf_appendf (sb, "%s%s", comma, col->name);
			}
			comma = ",";
		}
		r_strbuf_append (sb, "\n");
	}
	r_list_foreach (t->rows, iter, row) {
		char *item;
		int c = 0;
		const char *comma = "";
		r_list_foreach (row->items, iter2, item) {
			RTableColumn *col = r_list_get_n (t->cols, c);
			if (col) {
				if (strchr (col->name, ',')) {
					r_strbuf_appendf (sb, "%s\"%s\"", comma, col->name);
				} else {
					r_strbuf_appendf (sb, "%s%s", comma, item);
				}
				comma = ",";
			}
			c++;
		}
		r_strbuf_append (sb, "\n");
	}
	return r_strbuf_drain (sb);
}

R_API char *r_table_tojson(RTable *t) {
	PJ *pj = pj_new ();
	RTableRow *row;
	RListIter *iter, *iter2;
	pj_a (pj);
	r_list_foreach (t->rows, iter, row) {
		char *item;
		int c = 0;
		pj_o (pj);
		r_list_foreach (row->items, iter2, item) {
			RTableColumn *col = r_list_get_n (t->cols, c);
			if (col) {
				if (col->type == &r_table_type_number) {
					ut64 n = r_num_get (NULL, item);
					if (n) {
						pj_kn (pj, col->name, n);
					} else if (*item && *item != '0') {
						pj_ks (pj, col->name, item);
					}
				} else {
					pj_ks (pj, col->name, item);
				}
			}
			c++;
		}
		pj_end (pj);
	}
	pj_end (pj);
	return pj_drain (pj);
}

R_API void r_table_filter(RTable *t, int nth, int op, const char *un) {
	RTableRow *row;
	RListIter *iter, *iter2;
	ut64 uv = r_num_math (NULL, un);
	ut64 sum = 0;
	r_list_foreach_safe (t->rows, iter, iter2, row) {
		const char *nn = r_list_get_n (row->items, nth);
		ut64 nv = r_num_math (NULL, nn);
		bool match = true;
		switch (op) {
		case '+':
			// "sum"
			sum += nv;
			match = false;
			break;
		case '>':
			match = (nv > uv);
			break;
		case '<':
			match = (nv < uv);
			break;
		case '=':
			match = (nv == uv);
			break;
		case '!':
			match = (nv == uv);
			break;
		case '~':
			match = strstr (nn, un) != NULL;
		case '\0':
			break;
		}
		if (!match) {
			r_list_delete (t->rows, iter);
		}
	}
	if (op == '+') {
		r_table_add_rowf (t, "u", sum);
	}
}

static int cmp(const void *_a, const void *_b) {
	RTableRow *a = (RTableRow*)_a;
	RTableRow *b = (RTableRow*)_b;
	const char *wa = r_list_get_n (a->items, Gnth);
	const char *wb = r_list_get_n (b->items, Gnth);
	int res = Gcmp (wa, wb);
	if (Ginc) {
		res = -res;
	}
	return res;
}

R_API void r_table_sort(RTable *t, int nth, bool inc) {
	RTableColumn *col = r_list_get_n (t->cols, nth);
	if (col) {
		Ginc = inc;
		Gnth = nth;
		Gcmp = col->type->cmp;
		r_list_sort (t->rows, cmp);
		Gnth = Ginc = 0;
		Gcmp = NULL;
	}
}

R_API int r_table_column_nth(RTable *t, const char *name) {
	RListIter *iter;
	RTableColumn *col;
	int n = 0;
	r_list_foreach (t->cols, iter, col) {
		if (!strcmp (name, col->name)) {
			return n;
		}
		n++;
	}
	return -1;
}

static int __resolveOperation(const char *op) {
	if (!strcmp (op, "gt")) {
		return '>';
	}
	if (!strcmp (op, "lt")) {
		return '<';
	}
	if (!strcmp (op, "eq")) {
		return '=';
	}
	if (!strcmp (op, "ne")) {
		return '!';
	}
	return -1;
}

static void __table_column_free(void *_col) {
	RTableColumn *col = (RTableColumn*)_col;
	free (col);
}

R_API void r_table_columns(RTable *t, RList *colNames) {
	RListIter  *iter, *iterCol;
	char * colName;
	RTableRow *row;
	r_list_foreach (t->rows, iter, row) {
		RList *items = r_list_newf (r_table_row_free);
		r_list_foreach (colNames, iterCol, colName) {
			int fc = r_table_column_nth (t, colName);
			RTableRow *item = r_list_get_n (row->items, fc);
			if (item) {
				r_list_append (items, item);
			}
		}
		row->items = items;
	}
	RList *cols = r_list_newf (r_table_column_free);
	r_list_foreach (colNames, iterCol, colName) {
		int fc = r_table_column_nth (t, colName);
		if (fc >= 0) {
			RTableColumn *c = r_list_get_n (t->cols, fc);
			if (c) {
				r_list_append (cols, c);
			}
		}
	}
	t->cols = cols;
}

R_API void r_table_filter_columns(RTable *t, RList *list) {
	const char *col;
	RListIter *iter;
	RList *cols = t->cols;
	t->cols = r_list_newf (__table_column_free);
	r_list_foreach (list, iter, col) {
		int ncol = r_table_column_nth (t, col);
		if (ncol != -1) {
			RTableColumn *c = r_list_get_n (cols, ncol);
			if (c) {
				//c->width = R_MAX (c->width, itemLength);
				r_table_add_column (t, c->type, col, 0);
			}
		}
	}
}

R_API bool r_table_query(RTable *t, const char *q) {
	r_return_val_if_fail (t, false);
	q = r_str_trim_ro (q);
	if (*q == '?') {
		eprintf ("RTableQuery> comma separated \n");
		eprintf (" colname/sort/inc     sort rows by given colname\n");
		eprintf (" col0/cols/col1/col2  select cols\n");
		eprintf (" col0/gt/0x800        grep rows matching col0 > 0x800\n");
		eprintf (" col0/lt/0x800        grep rows matching col0 < 0x800\n");
		eprintf (" col0/eq/0x800        grep rows matching col0 == 0x800\n");
		eprintf (" size/sum             sum all the values of given column\n");
		return false;
	}
	// TODO support parenthesis and (or)||
	// split by "&&" (or comma) -> run .filter on each
	// addr/gt/200,addr/lt/400,addr/sort/dec,offset/sort/inc
	if (!q || !*q) {
		__table_adjust (t);
		return true;
	}
	RListIter *iter;
	char *qq = strdup (q);
	RList *queries = r_str_split_list (qq, ",",  0);
	char *query;
	r_list_foreach (queries, iter, query) {
		RList *q = r_str_split_list (query, "/", 2);
		const char *columnName = r_list_get_n (q, 0);
		const char *operation = r_list_get_n (q, 1);
		const char *operand = r_list_get_n (q, 2);
		int col = r_table_column_nth (t, columnName);
		if (col == -1) {
			if (*columnName == '[') {
				col = atoi (columnName + 1);
			} else {
				eprintf ("Invalid column name (%s) for (%s)\n", columnName, query);
			}
		}
		if (!operation) {
			break;
		}
		if (!strcmp (operation, "sort")) {
			r_table_sort (t, col, operand && !strcmp (operand, "dec"));
		} else if (!strcmp (operation, "join")) {
			// TODO: implement join operation with other command's tables
		} else if (!strcmp (operation, "sum")) {
			char *op = strdup (operand?operand: "");
			RList *list = r_str_split_list (op, "/", 0);
			r_list_prepend (list, strdup (columnName));
			r_table_columns (t, list); // select/reorder columns
			r_list_free (list);
			r_table_filter (t, 0, '+', op);
			free (op);
		} else if (!strcmp (operation, "cols")) {
			char *op = strdup (operand);
			RList *list = r_str_split_list (op, "/", 0);
			r_list_prepend (list, strdup (columnName));
			r_table_columns (t, list); // select/reorder columns
			r_list_free (list);
			free (op);
		// TODO	r_table_filter_columns (t, q);
		} else if (!strcmp (operation, "quiet")) {
			t->showHeader = false;
		} else if (!strcmp (operation, "graph")) {
		// TODO	r_table_rendergraph(t, q);
		} else {
			int op = __resolveOperation (operation);
			if (op == -1) {
				eprintf ("Invalid operation (%s)\n", operation);
			} else {
				r_table_filter (t, col, op, operand);
			}
		}
		r_list_free (q);
	}
	r_list_free (queries);
	free (qq);
	__table_adjust (t);
	return true;
}


R_API void r_table_visual_list(RTable *table, RList *list, ut64 seek, ut64 len, int width, bool va) {
	ut64 mul, min = -1, max = -1;
	RListIter *iter;
	RListInfo *info;
	RCons *cons = (RCons *) table->cons;
	table->showHeader = false;
	const char *h_line = cons->use_utf8 ? RUNE_LONG_LINE_HORIZ : "-";
	const char *block = cons->use_utf8 ? UTF_BLOCK : "#";
	int j, i;
	width -= 80;
	if (width < 1) {
		width = 30;
	}

	r_table_set_columnsf (table, "sssssss", "No.", "offset", "blocks", "offset", "perms", "extra", "name");
	r_list_foreach (list, iter, info) {
		if (min == -1 || info->pitv.addr < min) {
			min = info->pitv.addr;
		}
		if (max == -1 || info->pitv.addr + info->pitv.size > max) {
			max = info->pitv.addr + info->pitv.size;
		}
	}
	mul = (max - min) / width;
	if (min != -1 && mul > 0) {
		const char * color = "", *color_end = "";
		i = 0;
		r_list_foreach (list, iter, info) {
			RStrBuf *buf = r_strbuf_new ("");
			if (cons->context->color_mode && info->perm != -1) {
				color_end = Color_RESET;
				if ((info->perm & R_PERM_X) && (info->perm & R_PERM_W)) { // exec & write bits
					color = cons->context->pal.graph_trufae;
				} else if ((info->perm & R_PERM_X)) { // exec bit
					color = cons->context->pal.graph_true;
				} else if ((info->perm & R_PERM_W)) { // write bit
					color = cons->context->pal.graph_false;
				} else {
					color = "";
					color_end = "";
				}
			} else {
				color = "";
				color_end = "";
			}
			for (j = 0; j < width; j++) {
				ut64 pos = min + j * mul;
				ut64 npos = min + (j + 1) * mul;
				if (info->pitv.addr < npos && (info->pitv.addr + info->pitv.size) > pos) {
					r_strbuf_append (buf, block);
				} else {
					r_strbuf_append (buf, h_line);
				}
			}
			if (va) {
				r_table_add_rowf (table, "sssssss", sdb_fmt ("%d%c", i, r_itv_contain (info->vitv, seek) ? '*' : ' '),
				    sdb_fmt ("%s0x%"PFMT64x"%s", "", info->vitv.addr, ""), r_strbuf_drain (buf),
				    sdb_fmt ("%s0x%"PFMT64x"%s", "", r_itv_end (info->vitv), ""),
				    (info->perm != -1)? r_str_rwx_i (info->perm) : "",(info->extra)?info->extra : "", (info->name)?info->name :"");
			} else {
				r_table_add_rowf (table, "sssssss", sdb_fmt ("%d%c", i, r_itv_contain (info->pitv, seek) ? '*' : ' '),
				    sdb_fmt ("%s0x%"PFMT64x"%s", "", info->pitv.addr, ""), r_strbuf_drain (buf),
				    sdb_fmt ("%s0x%"PFMT64x"%s", "", r_itv_end (info->pitv), ""),
				    (info->perm != -1)? r_str_rwx_i (info->perm) : "",(info->extra)?info->extra : "", (info->name)?info->name :"");
			}
			i++;
		}
		RStrBuf *buf = r_strbuf_new ("");
		/* current seek */
		if (i > 0 && len != 0) {
			if (seek == UT64_MAX) {
				seek = 0;
			}
			for (j = 0; j < width; j++) {
				r_strbuf_append (buf,((j * mul) + min >= seek &&
						     (j * mul) + min <= seek + len) ? "^" : h_line);
			}
			r_table_add_rowf (table, "sssssss", "=>", sdb_fmt ("0x%08"PFMT64x"", seek),
			    r_strbuf_drain (buf),  sdb_fmt ("0x%08"PFMT64x"", seek + len), "", "", "");
		}
	}
}

#if 0
// TODO: to be implemented
R_API RTable *r_table_clone(RTable *t) {
	// TODO: implement
	return NULL;
}

R_API RTable *r_table_push(RTable *t) {
	// TODO: implement
	return NULL;
}

R_API RTable *r_table_pop(RTable *t) {
	// TODO: implement
	return NULL;
}

R_API void r_table_fromjson(RTable *t, const char *csv) {
	//  TODO
}

R_API void r_table_fromcsv(RTable *t, const char *csv) {
	//  TODO
}

R_API char *r_table_tohtml(RTable *t) {
	// TODO
	return NULL;
}

R_API void r_table_transpose(RTable *t) {
	// When the music stops rows will be cols and cols... rows!
}

R_API void r_table_format(RTable *t, int nth, RTableColumnType *type) {
	// change the format of a specific column
	// change imm base, decimal precission, ...
}

// to compute sum result of all the elements in a column
R_API ut64 r_table_reduce(RTable *t, int nth) {
	// When the music stops rows will be cols and cols... rows!
	return 0;
}
#endif
