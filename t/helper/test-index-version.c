#include "test-tool.h"
#include "cache.h"

int cmd__index_version(int argc, const char **argv)
{
	struct cache_header hdr = { 0 };
	int version;

	if (read(0, &hdr, sizeof(hdr)) != sizeof(hdr))
		return 0;
	version = ntohl(hdr.hdr_version);
	printf("%d\n", version);
	return 0;
}
