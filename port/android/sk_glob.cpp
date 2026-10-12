/*
 * Minimal glob() replacement for Android API < 28, see sk_glob.h.
 */
#ifdef __ANDROID__

#include "sk_glob.h"

#undef glob_t
#undef glob
#undef globfree

#include <dirent.h>
#include <fnmatch.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <string>
#include <vector>

static bool has_wildcard(const std::string &s)
{
	return s.find_first_of("*?[") != std::string::npos;
}

static bool path_less(const std::string &a, const std::string &b)
{
	return strcmp(a.c_str(), b.c_str()) < 0;
}

int sk_glob(const char *pattern, int flags, int (*errfunc)(const char *, int), sk_glob_t *out)
{
	(void)errfunc;
	out->gl_pathc = 0;
	out->gl_pathv = NULL;
	out->gl_offs = 0;

	if (!pattern || !*pattern)
		return 1;

	std::string spec(pattern);
	std::string dir, base;
	size_t slash = spec.find_last_of('/');
	if (slash == std::string::npos)
	{
		dir = ".";
		base = spec;
	}
	else
	{
		dir = slash == 0 ? "/" : spec.substr(0, slash);
		base = spec.substr(slash + 1);
	}
	if (has_wildcard(dir))
		return 1; // wildcard in a directory component is not supported

	std::vector<std::string> found;
	DIR *d = opendir(dir.c_str());
	if (d)
	{
		struct dirent *e;
		while ((e = readdir(d)) != NULL)
		{
			if (fnmatch(base.c_str(), e->d_name, FNM_PERIOD) != 0)
				continue;
			if (slash == std::string::npos)
				found.push_back(e->d_name);
			else
				found.push_back(spec.substr(0, slash + 1) + e->d_name);
		}
		closedir(d);
	}

	if (!(flags & SK_GLOB_NOSORT))
		std::sort(found.begin(), found.end(), path_less);

	if (found.empty())
		return 0;

	out->gl_pathv = (char **)calloc(found.size() + 1, sizeof(char *));
	if (!out->gl_pathv)
		return 1;
	for (size_t i = 0; i < found.size(); i++)
	{
		out->gl_pathv[i] = strdup(found[i].c_str());
		if (!out->gl_pathv[i])
		{
			out->gl_pathc = i;
			sk_globfree(out);
			return 1;
		}
	}
	out->gl_pathc = found.size();
	return 0;
}

void sk_globfree(sk_glob_t *g)
{
	if (g->gl_pathv)
	{
		for (size_t i = 0; i < g->gl_pathc; i++)
			free(g->gl_pathv[i]);
		free(g->gl_pathv);
	}
	g->gl_pathv = NULL;
	g->gl_pathc = 0;
}

#endif // __ANDROID__
