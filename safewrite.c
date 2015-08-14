#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include "safewrite.h"

int safewrite(char *filename, char *buf)
{
        char *tmpfile = malloc(strlen(filename) + 3);
        mode_t mode = S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH|S_IWOTH;
        int fd;

        if (tmpfile == NULL)
                return (-1);

        sprintf(tmpfile, "%s~", filename);

        if (unlink(tmpfile) == -1) {
                if (errno != ENOENT) {
                        fprintf(stderr, "Failed to remove %s (errno=%d)\n", tmpfile, errno);
                        free(tmpfile);
                        return (-1);
                }
        }

        if ((fd = open(tmpfile, O_RDWR|O_CREAT|O_TRUNC, mode)) == -1) {
                fprintf(stderr, "Failed to create %s (errno=%d)\n", tmpfile, errno);
                free(tmpfile);
                return (-1);
        }

        if (write(fd, buf, strlen(buf)) != strlen(buf)) {
                fprintf(stderr, "Failed to write to %s (errno=%d)\n", tmpfile, errno);
                free(tmpfile);
                close(fd);
                return (-1);
        }
	/* Ensure NL-terminated */
	if (buf[strlen(buf) - 1] != '\n') {
		write(fd, "\n", 1);
	}

        close(fd);

        if ((rename(tmpfile, filename)) == -1) {
                fprintf(stderr, "Failed to rename %s to %s (errno=%d)\n", tmpfile, filename, errno);
                free(tmpfile);
        }

        free(tmpfile);
        return (0);
}
