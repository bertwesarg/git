/*
 * cdiff conflict filter interface
 */

#ifndef CDIFF_CONFLICT_H
#define CDIFF_CONFLICT_H

void cdiff_conflict_filter(mmbuffer_t *merge_result, int marker_size);
void cdiff_by_path(const char *origin_name, const char **target_names,
		   int num_targets);

#endif
