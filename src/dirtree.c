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
//--------------------------------------------------------------------------------------------------
// Function: gen_tree_shape
// Generates the tree-like structure for directory printing 
// based on whether the current entry is the last in its directory. 
// Adds tree branches ("|", "`") if tree view flag is enabled.
//--------------------------------------------------------------------------------------------------
char* gen_tree_shape(bool is_last, unsigned int flags, const char *pstr) {
	int len = strlen(pstr);// Length of the current prefix string
	char *result;// Stores the generated tree structure
	int warn = 0;// Error checking for memory allocation
	// If F_TREE flag is set(-t), format the output with tree symbols
	if(flags & F_TREE) {
		result = (char*)malloc(sizeof(char)*(len + 3));
		// Allocate memory for the new tree string
		if(result == NULL) panic("Out of memory.");// Handle memory allocation failure
		strncpy(result, pstr, len);// Copy the existing prefix
		result[len + 2] = '\0';// Null-terminate the string
		if(len > 1) {
			if(result[len - 2] == '`') result[len - 2] = ' ';// Adjust the tree symbols
			result[len - 1] = ' ';
		}
		result[len] = is_last ? '`' : '|';// Set tree symbol depending on last entry
		result[len + 1] = '-';// Add horizontal branch
	}
	else {// If tree view is not enabled, just add spaces
		warn = asprintf(&result, "%s  ", pstr);
		if(warn == -1) panic("Out of memory.");
	}

	return result;
}
//--------------------------------------------------------------------------------------------------
// Function: print_verbose
// Prints detailed information about the file or directory 
// (such as user, group, size, and type) if the verbose flag is enabled.
//--------------------------------------------------------------------------------------------------
void print_verbose(struct stat *stat){
	struct passwd *pw= getpwuid(stat->st_uid);// Get user information
	struct group *grp = getgrgid(stat->st_gid);// Get group information
	char type;// File type character
	// If user or group information is unavailable, panic()
	if (pw == NULL || grp == NULL) panic("\nError on getpwuid /getgrgid.");
	// Get user and group names
	char *user = pw->pw_name;
	char *group = grp->gr_name;
	// Determine file type
	if(S_ISREG(stat->st_mode)) type = ' ';
	else if(S_ISDIR(stat->st_mode)) type = 'd';
	else if(S_ISCHR(stat->st_mode)) type = 'c';
	else if(S_ISLNK(stat->st_mode)) type = 'l';
	else if(S_ISFIFO(stat->st_mode)) type = 'f';
	else if(S_ISBLK(stat->st_mode)) type = 'b';
	else if(S_ISSOCK(stat->st_mode)) type = 's';
	else type = '\0';
	// Print
	printf("  %8s:%-8s  %10ld  %8ld  %c", user, group, stat->st_size, stat->st_blocks, type);

}
//--------------------------------------------------------------------------------------------------
// Function: print_errno
// Handles printing error messages based on the errno value,
// and appends tree structure if needed.
//--------------------------------------------------------------------------------------------------
void print_errno(const char *pstr, unsigned int flags){
	// Generate tree structure with prefix
	char *error_pstr = gen_tree_shape(true, flags, pstr);
	switch(errno) {// Switch case based on the errno value
		case ENOMEM:
			panic("Out of memory.");
			break;
                case EACCES:
                        printf("%sERROR: Permission denied\n", error_pstr);
                        break;
                case ENOENT:
                        printf("%sERROR: No such file or directory\n", error_pstr);
                        break;
                case ENOTDIR:
                        printf("%sERROR: Not a directory\n", error_pstr);
                        break;
		default:
			// default error handling
			printf("ERROR: error code %d\n", errno);
			panic("quit process");
	}
	free(error_pstr);
	return;
}
//--------------------------------------------------------------------------------------------------
// Function: update_stats
// Updates the summary statistics (total files, directories, links, etc.) 
// based on the file type and size.
//--------------------------------------------------------------------------------------------------
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
	int warn=0;// Variable to track errors
	char *new_dn = NULL;// Stores the directory path
	int num =0;// childs

	// Ensure directory path ends with '/'
	if (dn[strlen(dn)-1] != '/'){
		// Add '/'
		warn = asprintf(&new_dn, "%s/", dn);
		if(warn == -1) panic("Out of memory.");
	}
	else {// Duplicate the directory name if already properly formatted
		new_dn = strdup(dn);
		if (new_dn == NULL) panic("Out of memory.");
	}
	// Open the directory stream
	DIR *dir = opendir(new_dn);
	if (!dir) {
		print_errno(pstr, flags);// Print error if unable to open the directory
		free(new_dn);
		return;
	}
	if(errno) print_errno(pstr, flags);// Check for additional errors after opening
	
	// Allocate memory for directory entries and retrieve the next entry
	struct dirent *dirents = (struct dirent*)malloc(sizeof(struct dirent));
	if (dirents == NULL) panic("Out of memory.");
	struct dirent *getnext_result;
	
	// Read all directory entries, ignoring "." and ".."
	getnext_result = getNext(dir);
	
	while(getnext_result != NULL) {// Resize array
		dirents = (struct dirent*)realloc(dirents, (num + 1) * sizeof(struct dirent));
		if(dirents == NULL) panic("Out of memory.");
		dirents[num++] = *getnext_result;// Store the retrieved entry
		getnext_result = getNext(dir);// Get the next entry
	}
	// Sort directory entries
	qsort(dirents, num, sizeof(struct dirent), dirent_compare);
	
	// Iterate through each directory entry and process
	for(int i=0;i< num; i++){
		char *path;// Store the full path
		struct stat i_stat;// Stat structure to hold file metadata

		// Create the full path of the current entry
		warn = asprintf(&path, "%s%s", new_dn, dirents[i].d_name);
		if (warn == -1) panic("Out of memory.");

		// Get metadata for the current file/directory
		lstat(path, &i_stat);

		// Generate the next level tree structure
		char *next_pstr = gen_tree_shape(i == num - 1, flags, pstr);
		
		// Print the directory/file name with tree structure
		char *final_pstr;
		warn = asprintf(&final_pstr, "%s%s", next_pstr, dirents[i].d_name);
		if (warn == -1) panic("Out of memory.");

		// Print file information and verbose details
		if((flags & F_VERBOSE) && strlen(final_pstr) > 54) printf("%-51.51s...", final_pstr);
		else printf("%-54s",final_pstr);

		free(final_pstr);
		
		// If verbose mode is enabled, print additional details
		if(flags & F_VERBOSE) print_verbose(&i_stat);
		printf("\n");
		
		// Update the statistics
		update_stats(stats, &i_stat);
		
		// If the current entry is a directory, recursively process it
		if (S_ISDIR(i_stat.st_mode)) {
			warn = asprintf(&path, "%s/", path);
			if (warn == -1) panic("Out of memory.");
			processDir(path, next_pstr, stats, flags);
		}
		free(path);
		free(next_pstr);
	}
	free(dirents);
	free(new_dn);
	closedir(dir);

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
		  
		  tstat.blocks += dstat.blocks;
		  tstat.size += dstat.size;
		  tstat.files += dstat.files;
		  tstat.dirs += dstat.dirs;
		  tstat.links += dstat.links;
		  tstat.fifos += dstat.fifos;
		  tstat.socks += dstat.socks;
		  
		  free(summary);
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

