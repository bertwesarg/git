#include "builtin.h"
#include "xdiff-interface.h"
#include "strbuf.h"
#include "cdiff-conflict.h"
#include "parse-options.h"

static const char *const cdiff_usage[] = {
	"git cdiff [files...]",
	NULL
};

int cmd_cdiff(int argc, const char **argv, const char *prefix)
{
	struct option options[] = {
		OPT_END(),
	};

	argc = parse_options(argc, argv, prefix, options, cdiff_usage, 0);
	if (argc < 2)
		die("insufficient number of argument");

	cdiff_by_path(argv[0], argv + 1, argc - 1);

	return 0;
}

static void mmfile_from_stdin(mmfile_t *mmfile)
{
	struct strbuf line = STRBUF_INIT;
	struct strbuf collector = STRBUF_INIT;
	size_t sz;

	while (strbuf_getwholeline(&line, stdin, '\n') != EOF) {
		strbuf_addbuf(&collector, &line);
		strbuf_reset(&line);
	}

	/* force allocated buffer */
	strbuf_grow(&collector, 0);
	mmfile->ptr  = strbuf_detach(&collector, &sz);
	mmfile->size = sz;
}

static const char *const cdiff_conflict_filter_usage[] = {
	"git cdiff-conflict-filter [options] [files...]",
	NULL
};

int cmd_cdiff_conflict_filter(int argc, const char **argv, const char *prefix)
{
	int to_stdout = 0;
	int marker_size = DEFAULT_CONFLICT_MARKER_SIZE;
	int i;

	struct option options[] = {
		OPT_BOOL('p', "stdout", &to_stdout, "send results to standard output"),
		OPT_INTEGER('m', "marker-size", &marker_size, "specify the marker size"),
		/* OPT__QUIET(&quiet), */
		OPT_END(),
	};

	argc = parse_options(argc, argv, prefix, options,
			     cdiff_conflict_filter_usage, 0);

	if (argc > 1 && to_stdout)
		die("--stdout works only for one file argument");

	/* inject stdin, if no files were given on the command line */
	if (argc == 0) {
		argv[0] = "-";
		to_stdout = 1;
		argc++;
	}

	for (i = 0; i < argc; i++) {
		mmfile_t source;
		mmbuffer_t source_buffer;
		FILE *f;

		if (0 == strcmp(argv[i], "-"))
			mmfile_from_stdin(&source);
		else if (read_mmfile(&source, argv[i]))
			die_errno("can't read `%s'", argv[i]);

		if (buffer_is_binary(source.ptr, source.size)) {
			fprintf(stderr, "skipping binary file: %s\n", argv[i]);
			free(source.ptr);
			continue;
		}

		source_buffer.ptr = source.ptr;
		source_buffer.size = source.size;
		cdiff_conflict_filter(&source_buffer, marker_size);

		f = to_stdout ? stdout : fopen(argv[i], "wb");
		if (!f)
			die("Could not open %s for writing", argv[i]);
		else if (source_buffer.size &&
			 fwrite(source_buffer.ptr,
				source_buffer.size, 1, f) != 1)
			die("Could not write to %s", argv[i]);
		else if (fclose(f))
			die("Could not close %s", argv[i]);

		free(source_buffer.ptr);
	}

	return 0;
}

