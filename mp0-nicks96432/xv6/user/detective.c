#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fs.h"
#include "user/user.h"

#define err_sys(e...)  \
	{                  \
		fprintf(2, e); \
		exit(1);       \
	}

int findFile(const char *filename, const char *prefix)
{
	int fd, result = 0;
	struct dirent de;
	struct stat st;
	char buf[512];
	const char currentDir[] = ".";
	const char prevDir[] = "..";

	if ((fd = open(prefix, 0)) < 0)
		err_sys("open prefix error\n");
	while (read(fd, &de, sizeof(de)) == sizeof(de))
	{
		if (de.inum == 0)
			continue;
		if (strcmp(de.name, currentDir) && strcmp(de.name, prevDir))
		{
			strcpy(buf, prefix);
			char *end = buf + strlen(buf);
			*end++ = '/';
			memmove(end, de.name, DIRSIZ);
			end[DIRSIZ] = 0;
			if (stat(buf, &st) < 0)
			{
				printf("cannot stat %s\n", de.name);
				continue;
			}
			if (st.type == T_FILE && !strcmp(de.name, filename))
			{
				result = 1;
				printf("%d as Watson: %s/%s\n", getpid(), prefix, filename);
			}
			else if (st.type == T_DIR)
			{
				if (!strcmp(de.name, filename))
				{
					result = 1;
					printf("%d as Watson: %s/%s\n", getpid(), prefix, filename);
				}
				if (findFile(filename, buf))
					result = 1;
			}
		}
	}
	close(fd);
	return result;
}

int main(int argc, char const *argv[])
{
	int childPipe[2];
	if (pipe(childPipe) < 0)
		err_sys("pipe error\n");

	if (argc != 2)
	{
		printf("usage: %s [commission]\n", argv[0]);
		exit(1);
	}

	if (fork() == 0) // child process
	{
		if (findFile(argv[1], "."))
		{
			if (write(childPipe[1], "y", 1) < 0) // found
				err_sys("write y error\n");
		}
		else
		{
			if (write(childPipe[1], "n", 1) < 0) // not found
				err_sys("write n error\n");
		}
	}
	else // parent process
	{
		char result;
		if (read(childPipe[0], &result, sizeof(result)) < sizeof(result))
			err_sys("read error\n");
		if (result == 'y')
			printf("%d as Holmes: This is the evidence\n", getpid());
		else if (result == 'n')
			printf("%d as Holmes: This is the alibi\n", getpid());
		else
			err_sys("this shouldn't happen\n");
	}
	exit(0);
}