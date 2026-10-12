/*
 * Minimal glob() replacement for Android API < 28 (bionic only gained glob.h in API 28; this
 * port targets minSdk 24). Supports what the game needs: a wildcard in the LAST path component
 * (for example SCENARIO/ followed by a star and ".SCN"), or a literal path.
 */
#ifndef SK_GLOB_H
#define SK_GLOB_H

#ifdef __ANDROID__

#include <stddef.h>

typedef struct
{
	size_t gl_pathc;
	char **gl_pathv;
	size_t gl_offs;
} sk_glob_t;

#define SK_GLOB_NOSORT 0x4

// Returns 0 on success (also when nothing matched, gl_pathc is then 0), non-zero on error.
int sk_glob(const char *pattern, int flags, int (*errfunc)(const char *, int), sk_glob_t *out);
void sk_globfree(sk_glob_t *g);

#define glob_t sk_glob_t
#define glob sk_glob
#define globfree sk_globfree
#define GLOB_NOSORT SK_GLOB_NOSORT

#endif // __ANDROID__

#endif // SK_GLOB_H
