#include "builtin.h"
#include "xdiff-interface.h"
#include "strbuf.h"
#include "cdiff-conflict.h"

/* new line in target */
struct nline {
	struct nline *next;
	int len;
	unsigned long targets_map;
	char line[FLEX_ARRAY];
};

/* Origin lines */
struct oline {
	struct nline *new_head, **new_tail;
	char *bol; /* buffer of line */
	int len;
	/* bit 0 up to (N-1) are on if the target has not this line (i.e.
	 * it will be removed).
	 */
	unsigned long targets_map;
	/* unsigned long *p_lno; */ /* ?parent line number */
};

struct combine_diff_state {
	unsigned long lno;
	/* int num_parent; */
	int n;
	struct oline *oline;
};

static void append_new(struct oline *oline, int n, const char *line, int len)
{
	struct nline *nline;
	unsigned long this_mask = (1UL<<n);

	nline = xmalloc(sizeof(*nline) + len + 1);
	nline->len = len;
	nline->next = NULL;
	nline->targets_map = this_mask;
	memcpy(nline->line, line, len);
	nline->line[len] = 0;
	*oline->new_tail = nline;
	oline->new_tail = &nline->next;
}

static void consume_hunk(void *state_, long ob, long on, long nb, long nn,
			const char *funcline, long funclen)
{
	struct combine_diff_state *state = state_;

	if (!on)
		state->lno = ob;
	else
		state->lno = ob - !!ob;
}

static int consume_line(void *state_, char *line, unsigned long len)
{
	struct combine_diff_state *state = state_;

	switch (line[0]) {
	case '-':
		state->oline[++state->lno].targets_map |= (1lu << state->n);
		break;
	case '+':
		append_new(&state->oline[state->lno], state->n, line+1, len-1);
		break;
	}
	return 0;
}

static void build_oline(char *origin, unsigned long origin_size,
			struct oline **oline_p, unsigned long *cnt_p)
{
	struct oline *oline;
	unsigned long cnt, lno;
	char *cp;

	for (cnt = 0, cp = origin; cp < origin + origin_size; cp++) {
		if (*cp == '\n')
			cnt++;
	}
	if (origin_size && origin[origin_size-1] != '\n')
		cnt++; /* incomplete line */

	oline = xcalloc(cnt+1, sizeof(*oline));
	*oline_p = oline;
	*cnt_p = cnt;

	for (lno = 0; lno <= cnt; lno++) {
		oline[lno].new_tail = &oline[lno].new_head;
		oline[lno].targets_map = 0;
	}

	if (!origin_size)
		return;

	oline[1].bol = origin;
	for (lno = 1, cp = origin; cp < origin + origin_size; cp++) {
		if (*cp == '\n') {
			oline[lno].len = cp - oline[lno].bol + 1;
			lno++;
			if (lno <= cnt)
				oline[lno].bol = cp + 1;
		}
	}
	if (origin_size && origin[origin_size-1] != '\n')
		oline[cnt].len = origin_size - (oline[cnt].bol - origin);

}

static void dump_oline(const struct oline *oline, unsigned long cnt,
		int num_targets)
{
	unsigned long lno;
	int i;
	struct nline *nline;

	for (lno = 0; lno <= cnt; lno++) {
		if (1 <= lno) {
			for (i = 0; i < num_targets; i++)
				if (oline[lno].targets_map & (1lu << i))
					putchar('-');
				else
					putchar(' ');
			printf("%.*s", (int)oline[lno].len, oline[lno].bol);
		}

		nline = oline[lno].new_head;
		while (nline) {
			for (i = 0; i < num_targets; i++)
				if (nline->targets_map & (1lu << i))
					putchar('+');
				else
					putchar(' ');
			printf("%s", nline->line);

			nline = nline->next;
		}
	}
}

static void free_oline(struct oline *oline, unsigned long cnt)
{
	unsigned long lno;

	for (lno = 0; lno <= cnt; lno++) {
		while (oline[lno].new_head) {
			struct nline *nline = oline[lno].new_head;
			oline[lno].new_head = nline->next;
			free(nline);
		}
	}
	free(oline);
}

static void do_cdiff_step(struct oline *oline, mmfile_t *origin_file,
		mmfile_t *target_file, int i)
{
	xdemitconf_t xecfg;
	xpparam_t xpp;
	struct combine_diff_state state;

	memset(&state, 0, sizeof(state));
	state.oline = oline;
	state.lno = 1;
	state.n = i;

	memset(&xpp, 0, sizeof(xpp));
	xpp.flags = XDF_NEED_MINIMAL;
	memset(&xecfg, 0, sizeof(xecfg));

	xdi_diff_outf(origin_file, target_file, consume_hunk, consume_line,
		     &state, &xpp, &xecfg);
}

void cdiff_by_path(const char *origin_name, const char **target_names,
		   int num_targets)
{
	mmfile_t origin_file, target_file;
	struct oline *oline = NULL; /* origin lines */
	unsigned long cnt;
	int i;

	if (num_targets > (sizeof(unsigned long) * 8))
		die("Can only handle %zu targets.", (sizeof(unsigned long) * 8));

	if (read_mmfile(&origin_file, origin_name))
		die_errno("can't read `%s'", origin_name);

	/* build origin image */
	build_oline(origin_file.ptr, origin_file.size, &oline, &cnt);

	for (i = 0; i < num_targets; i++) {

		if (read_mmfile(&target_file, target_names[i]))
			die_errno("can't read `%s'", target_names[i]);

		do_cdiff_step(oline, &origin_file, &target_file, i);

		free(target_file.ptr);
	}
	dump_oline(oline, cnt, num_targets);

	free_oline(oline, cnt);

	free(origin_file.ptr);
}

static unsigned long put_line(char *dest, char *line, unsigned long len,
		unsigned long map, int marker, int num_targets)
{
	int i;

	for (i = 0; i < num_targets; i++)
		if (map & (1lu << i))
			*dest++ = marker;
		else
			*dest++ = ' ';
	memcpy(dest, line, len);

	return len + num_targets;
}

static unsigned long oline_to_buf(const struct oline *oline, unsigned long cnt,
				  int num_targets, char *dest)
{
	unsigned long lno;
	struct nline *nline;
	unsigned long size = 0;

	for (lno = 0; lno <= cnt; lno++) {
		if (1 <= lno) {
			if (!dest) {
				size += num_targets + oline[lno].len + 1;
			} else {
				size += put_line(dest + size, oline[lno].bol,
						oline[lno].len,
						oline[lno].targets_map, '-',
						num_targets);
			}
		}

		nline = oline[lno].new_head;
		while (nline) {
			if (!dest) {
				size += num_targets + nline->len + 1;
			} else {
				size += put_line(dest + size, nline->line,
						nline->len, nline->targets_map,
						'+', num_targets);
			}

			nline = nline->next;
		}
	}

	return size;
}

static unsigned long do_buf_cdiff(mmfile_t *origin_file,
				  mmfile_t *target_files,
				  int num_targets, char *dest)
{
	struct oline *oline = NULL; /* origin lines */
	unsigned long cnt, size;
	int i;

	if (num_targets > (sizeof(unsigned long) * 8))
		die("Can only handle %zu targets.", (sizeof(unsigned long) * 8));

	/* build origin image */
	build_oline(origin_file->ptr, origin_file->size, &oline, &cnt);

	for (i = 0; i < num_targets; i++)
		do_cdiff_step(oline, origin_file, &target_files[i], i);

	size = oline_to_buf(oline, cnt, num_targets, dest);

	free_oline(oline, cnt);

	return size;
}

static int is_marker_perfix(const char *buffer, int len,
			    int marker, int marker_size)
{
	/* == happens if it is at the end of the file */
	if (len <= marker_size)
		return 0;

	while (marker_size--)
		if (marker != *buffer++)
			return 0;

	return isspace(*buffer);
}

static int next_line_from_buffer(mmbuffer_t *buf, char **p_bol,
				 unsigned long *p_len)
{
	char *ep, *be = buf->ptr + buf->size;

	if (*p_bol == NULL) {
		/* first line */
		*p_bol = buf->ptr;
		*p_len = 0;
	}

	if (*p_bol + *p_len >= be) {
		/* p_len points to the end of the buf */
		return -1;
	}

	/* set begin of line to the next */
	*p_bol += *p_len;

	/* find end of line */
	ep = memchr(*p_bol, '\n', be - *p_bol);
	if (!ep)
		ep = be;
	else if (*ep == '\n')
		ep++;
	*p_len = ep - *p_bol;
	return 0;
}

void cdiff_conflict_filter(mmbuffer_t *merge_result, int marker_size)
{
	char *bol;
	unsigned long len;
	enum filter_state {
		IN_BASE,
		IN_OURS,
		IN_THEIRS,
		IN_CONTEXT
	} state;
	mmfile_t files[IN_CONTEXT];
	mmfile_t result = { NULL, 0 };

	if (marker_size <= 0)
		marker_size = DEFAULT_CONFLICT_MARKER_SIZE;

pass_2:
	memset(files, 0, sizeof(files));
	bol = NULL;
	len = 0;
	state = IN_CONTEXT;
	while (!next_line_from_buffer(merge_result, &bol, &len)) {
		if (is_marker_perfix(bol, len, '<', marker_size)) {
			if (state != IN_CONTEXT)
				goto out;
			state = IN_OURS;
			goto do_accumulate;
		} else if (is_marker_perfix(bol, len, '|', marker_size)) {
			if (state != IN_OURS)
				goto out;
			state = IN_BASE;
			continue;
		} else if (is_marker_perfix(bol, len, '=', marker_size)) {
			if (state != IN_BASE)
				goto out;
			state = IN_THEIRS;
			continue;
		} else if (is_marker_perfix(bol, len, '>', marker_size)) {
			if (state != IN_THEIRS)
				goto out;
			state = IN_CONTEXT;

			result.size += do_buf_cdiff(&files[IN_BASE],
						    files + 1, 2,
						    result.ptr ?
						    result.ptr + result.size :
						    NULL);
			memset(files, 0, sizeof(files));

			goto do_accumulate;
		}

		if  (state < IN_CONTEXT) {
			if (!files[state].ptr)
				files[state].ptr = bol;
			files[state].size += len;
			continue;
		}

	do_accumulate:
		if (result.ptr)
			memcpy(result.ptr + result.size, bol, len);
		result.size += len;
	}

	if (state != IN_CONTEXT)
		goto out;

	if (!result.ptr) {
		result.ptr = xmalloc(result.size);
		result.size = 0;
		goto pass_2;
	}

	free(merge_result->ptr);
	merge_result->ptr  = result.ptr;
	merge_result->size = result.size;
	result.ptr = NULL;

out:
	free(result.ptr);
}
