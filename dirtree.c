//--------------------------------------------------------------------------------------------------
// System Programming                         I/O Lab                                     Fall 2024
//
/// @file
/// @brief resursively traverse directory tree and list all entries
/// @author <Jeon minseo>
/// @studid <2019-19932>
//--------------------------------------------------------------------------------------------------

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <errno.h>
#include <unistd.h>
#include <stdarg.h>
#include <assert.h>
#include <grp.h>
#include <pwd.h>

#define MAX_DIR 64            ///< maximum number of supported directories

/// @brief output control flags
#define F_TREE      0x1       ///< enable tree view
#define F_SUMMARY   0x2       ///< enable summary
#define F_VERBOSE   0x4       ///< turn on verbose mode

/// @brief struct holding the summary
struct summary {
  unsigned int dirs;          ///< number of directories encountered
  unsigned int files;         ///< number of files
  unsigned int links;         ///< number of links
  unsigned int fifos;         ///< number of pipes
  unsigned int socks;         ///< number of sockets

  unsigned long long size;    ///< total size (in bytes)
  unsigned long long blocks;  ///< total number of blocks (512 byte blocks)
};


/// @brief abort the program with EXIT_FAILURE and an optional error message
///
/// @param msg optional error message or NULL
void panic(const char *msg)
{
  if (msg) fprintf(stderr, "%s\n", msg);
  exit(EXIT_FAILURE);
}


/// @brief read next directory entry from open directory 'dir'. Ignores '.' and '..' entries
///
/// @param dir open DIR* stream
/// @retval entry on success
/// @retval NULL on error or if there are no more entries
struct dirent *getNext(DIR *dir)
{
  struct dirent *next;
  int ignore;

  do {
    errno = 0;
    next = readdir(dir);
    if (errno != 0) perror(NULL);
    ignore = next && ((strcmp(next->d_name, ".") == 0) || (strcmp(next->d_name, "..") == 0));
  } while (next && ignore);

  return next;
}


/// @brief qsort comparator to sort directory entries. Sorted by name, directories first.
///
/// @param a pointer to first entry
/// @param b pointer to second entry
/// @retval -1 if a<b
/// @retval 0  if a==b
/// @retval 1  if a>b
static int dirent_compare(const void *a, const void *b)
{
  struct dirent *e1 = (struct dirent*)a;
  struct dirent *e2 = (struct dirent*)b;

  // if one of the entries is a directory, it comes first
  if (e1->d_type != e2->d_type) {
    if (e1->d_type == DT_DIR) return -1;
    if (e2->d_type == DT_DIR) return 1;
  }

  // otherwise sorty by name
  return strcmp(e1->d_name, e2->d_name);
}

char* gen_tree_shape(bool is_last, unsigned int flags, const char *pstr) {
	int len = strlen(pstr);
	char *result;
	int warn = 0;
	if(flags & F_TREE) {
		result = (char*)malloc(sizeof(char)*(len + 3));
		if(result == NULL) panic("Out of memory.");
		strncpy(result, pstr, len);
		result[len + 2] = '\0';
		if(len > 1) {
			if(result[len - 2] == '`') result[len - 2] = ' ';
			result[len - 1] = ' ';
		}
		result[len] = is_last ? '`' : '|';
		result[len + 1] = '-';
	}
	else {
		warn = asprintf(&result, "%s  ", pstr);
		if(warn == -1) panic("Out of memory.");
	}

	return result;
}

void print_verbose(struct stat *stat){
	struct passwd *upwd= getpwuid(stat->st_uid);
	struct group *ggrp = getgrgid(stat->st_gid);
	char type;
	if (upwd == NULL || ggrp == NULL) panic("\nError on getpwuid /getgrgid.");
	char *user = upwd->pw_name;
	char *group = ggrp->gr_name;
	
	if(S_ISREG(stat->st_mode)) type = ' ';
	else if(S_ISDIR(stat->st_mode)) type = 'd';
	else if(S_ISCHR(stat->st_mode)) type = 'c';
	else if(S_ISLNK(stat->st_mode)) type = 'l';
	else if(S_ISFIFO(stat->st_mode)) type = 'f';
	else if(S_ISBLK(stat->st_mode)) type = 'b';
	else if(S_ISSOCK(stat->st_mode)) type = 's';
	else type = '\0';

	printf("  %8s:%-8s  %10ld  %8ld  %c", user, group, stat->st_size, stat->st_blocks, type);

}

void print_errno(const char *pstr, unsigned int flags){
	char *error_pstr = gen_tree_shape(true, flags, pstr);
	switch(errno) {
		case EACCES:
			printf("%sERROR: Permission denied\n", error_pstr);
			break;
		case ENOENT:
			printf("%sERROR: No such file or directory\n", error_pstr);
			break;
		case ENOTDIR:
			printf("%sERROR: Not a directory\n", error_pstr);
			break;
		case ENOMEM:
			panic("Out of memory.");
			break;
		default:
			// default error handling
			printf("ERROR: error code %d\n", errno);
			panic("quit process");
	}
	free(error_pstr);
	return;
}

void update_stats(struct summary *stats, struct stat *i_stat){
	
	stats->files += S_ISREG(i_stat->st_mode); 
	stats->dirs += S_ISDIR(i_stat->st_mode);
	stats->links += S_ISLNK(i_stat->st_mode);
	stats->fifos += S_ISFIFO(i_stat->st_mode);
	stats->socks += S_ISSOCK(i_stat->st_mode);
	stats->size += i_stat->st_size;
	stats->blocks += i_stat->st_blocks;

	return;
}

/// @brief recursively process directory @a dn and print its tree
///char *next_pstr = genPstr(i == childs - 1, flags, pstr);
/// @param dn absolute or relative path string
/// @param pstr prefix string printed in front of each entry
/// @param stats pointer to statistics
/// @param flags output control flags (F_*)
void processDir(const char *dn, const char *pstr, struct summary *stats, unsigned int flags)
{
	int warn=0;
	
	if (dn[strlen(dn)-1] != '/'){
		warn = asprintf(&dn, "%s/", dn);
		if(warn == -1) panic("Out of memory.");
	}
	DIR *dir = opendir(dn);
	
	if(errno) print_errno(pstr, flags);
	struct dirent *dirents = (struct dirent*)malloc(sizeof(struct dirent));
	if (dirents == NULL) panic("Out of memory.");
	struct dirent *getnext_result;
	int num = 0;

	getnext_result = getNext(dir);
	
	while(getnext_result != NULL) {
		dirents = (struct dirent*)realloc(dirents, (num + 1) * sizeof(struct dirent));
		if(dirents == NULL) panic("Out of memory.");
		dirents[num++] = *getnext_result;
		getnext_result = getNext(dir);
	}

	qsort(dirents, num, sizeof(struct dirent), dirent_compare);
	
	for(int i=0;i< num; i++){
		char *path;
		struct stat i_stat;
		warn = asprintf(&path, "%s%s", dn, dirents[i].d_name);
		if (warn == -1) panic("Out of memory.");
		lstat(path, &i_stat);
		char *next_pstr = gen_tree_shape(i == num - 1, flags, pstr);
		//printDir
		char *final_pstr;
		warn = asprintf(&final_pstr, "%s%s", next_pstr, dirents[i].d_name);
		if (warn == -1) panic("Out of memory.");
		if((flags & F_VERBOSE) && strlen(final_pstr) > 54) printf("%-51.51s...", final_pstr);
		else printf("%-54s",final_pstr);
		free(final_pstr);
		if(flags & F_VERBOSE) print_verbose(&i_stat);
		printf("\n");

		update_stats(stats, &i_stat);
		
		if (S_ISDIR(i_stat.st_mode)) {
			warn = asprintf(&path, "%s/", path);
			if (warn == -1) panic("Out of memory.");
			processDir(path, next_pstr, stats, flags);
		}
		free(path);
		free(next_pstr);
	}
	closedir(dir);
	free(dirents);
	return;
}


/// @brief print program syntax and an optional error message. Aborts the program with EXIT_FAILURE
///
/// @param argv0 command line argument 0 (executable)
/// @param error optional error (format) string (printf format) or NULL
/// @param ... parameter to the error format string
void syntax(const char *argv0, const char *error, ...)
{
  if (error) {
    va_list ap;

    va_start(ap, error);
    vfprintf(stderr, error, ap);
    va_end(ap);

    printf("\n\n");
  }

  assert(argv0 != NULL);

  fprintf(stderr, "Usage %s [-t] [-s] [-v] [-h] [path...]\n"
                  "Gather information about directory trees. If no path is given, the current directory\n"
                  "is analyzed.\n"
                  "\n"
                  "Options:\n"
                  " -t        print the directory tree (default if no other option specified)\n"
                  " -s        print summary of directories (total number of files, total file size, etc)\n"
                  " -v        print detailed information for each file. Turns on tree view.\n"
                  " -h        print this help\n"
                  " path...   list of space-separated paths (max %d). Default is the current directory.\n",
                  basename(argv0), MAX_DIR);

  exit(EXIT_FAILURE);
}


/// @brief program entry point
int main(int argc, char *argv[])
{
  //
  // default directory is the current directory (".")
  //
  const char CURDIR[] = ".";
  const char *directories[MAX_DIR];
  int   ndir = 0;

  struct summary tstat;
  unsigned int flags = 0;

  //
  // parse arguments
  //
  for (int i = 1; i < argc; i++) {
    if (argv[i][0] == '-') {
      // format: "-<flag>"
      if      (!strcmp(argv[i], "-t")) flags |= F_TREE;
      else if (!strcmp(argv[i], "-s")) flags |= F_SUMMARY;
      else if (!strcmp(argv[i], "-v")) flags |= F_VERBOSE;
      else if (!strcmp(argv[i], "-h")) syntax(argv[0], NULL);
      else syntax(argv[0], "Unrecognized option '%s'.", argv[i]);
    } else {
      // anything else is recognized as a directory
      if (ndir < MAX_DIR) {
        directories[ndir++] = argv[i];
      } else {
        printf("Warning: maximum number of directories exceeded, ignoring '%s'.\n", argv[i]);
      }
    }
  }

  // if no directory was specified, use the current directory
  if (ndir == 0) directories[ndir++] = CURDIR;


  //
  // process each directory
  //
  // TODO
  //
  // Pseudo-code
  // - reset statistics (tstat)
  // - loop over all entries in 'directories' (number of entires stored in 'ndir')
  //   - reset statistics (dstat)
  //   - if F_SUMMARY flag set: print header
  //   - print directory name
  //   - call processDir() for the directory
  //   - if F_SUMMARY flag set: print summary & update statistics
  memset(&tstat, 0, sizeof(tstat));
  //...

  for(int i=0;i<ndir;i++){
	  struct summary dstat = {0};// each directory summary
	  if(flags & F_SUMMARY) {
	  	if(flags & F_VERBOSE) printf("Name                                                        User:Group           Size    Blocks Type \n");
	  	else printf("Name                                                                                                \n");
		printf("----------------------------------------------------------------------------------------------------\n");
	  }
	  printf("%s\n",directories[i]);
	  //recursively find
	  processDir(directories[i], "",&dstat, flags);
	  if(flags & F_SUMMARY){
		  //print
		  char *summary;
		  printf("----------------------------------------------------------------------------------------------------\n");
		  int warn = asprintf(&summary,"%u %s, %u %s, %u %s, %u %s, and %u %s",
				  dstat.files, (dstat.files==1) ? "file":"files",
				  dstat.dirs, (dstat.dirs==1) ? "directory":"directories",
				  dstat.links, (dstat.links==1) ? "link":"links",
				  dstat.fifos, (dstat.fifos==1) ? "pipe":"pipes",
				  dstat.socks, (dstat.socks==1) ? "socket":"sockets");
		  if(warn==-1) panic("Out of memory.");
		  if(flags & F_VERBOSE) printf("%-68.68s   %14lld %9lld\n\n", summary, dstat.size, dstat.blocks);
		  else printf("%s\n\n", summary);
		  free(summary);
		  //
		  tstat.blocks += dstat.blocks;
		  tstat.size += dstat.size;
		  tstat.files += dstat.files;
		  tstat.dirs += dstat.dirs;
		  tstat.links += dstat.links;
		  tstat.fifos += dstat.fifos;
		  tstat.socks += dstat.socks;
	  }
  }
  //
  // print grand total
  //
  if ((flags & F_SUMMARY) && (ndir > 1)) {
    printf("Analyzed %d directories:\n"
           "  total # of files:        %16d\n"
           "  total # of directories:  %16d\n"
           "  total # of links:        %16d\n"
           "  total # of pipes:        %16d\n"
           "  total # of sockets:      %16d\n",
           ndir, tstat.files, tstat.dirs, tstat.links, tstat.fifos, tstat.socks);

    if (flags & F_VERBOSE) {
      printf("  total file size:         %16llu\n"
             "  total # of blocks:       %16llu\n",
             tstat.size, tstat.blocks);
    }

  }

  //
  // that's all, folks!
  //
  return EXIT_SUCCESS;
}

