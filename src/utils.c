/*

   qc_image_unpacker
   -----------------------------------------

   Anestis Bechtsoudis <anestis@census-labs.com>
   Copyright 2019 - 2020 by CENSUS S.A. All Rights Reserved.

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

     http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.

*/

#include "utils.h"

#include <dirent.h>
#include <sys/mman.h>
#include <sys/stat.h>

static bool utils_readdir(infiles_t *pFiles, const char *basePath) {
  DIR *dir = opendir(basePath);
  if (!dir) {
    LOGMSG_P(l_ERROR, "Couldn't open dir '%s'", basePath);
    return false;
  }

  for (;;) {
    errno = 0;
    struct dirent *entry = readdir(dir);
    if (entry == NULL && errno == EINTR) {
      continue;
    }
    if (entry == NULL && errno != 0) {
      LOGMSG_P(l_ERROR, "readdir('%s')", basePath);
      return false;
    }
    if (entry == NULL) {
      break;
    }

    // Skip special files
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
      continue;
    }

    char path[PATH_MAX + 2];
    snprintf(path, sizeof(path), "%s/%s", basePath, entry->d_name);

    struct stat st;
    if (stat(path, &st) == -1) {
      LOGMSG(l_WARN, "Couldn't stat() the '%s' file", path);
      continue;
    }

    if (S_ISDIR(st.st_mode)) {
      if (!utils_readdir(pFiles, path)) {
        LOGMSG(l_ERROR, "Failed to process '%s' directory", path);
        continue;
      }
    }

    if (!S_ISREG(st.st_mode)) {
      LOGMSG(l_DEBUG, "'%s' is not a regular file, skipping", path);
      continue;
    }

    if (st.st_size == 0) {
      LOGMSG(l_DEBUG, "'%s' is empty", path);
      continue;
    }

    if (!(pFiles->files = realloc(pFiles->files, sizeof(char *) * (pFiles->fileCnt + 1)))) {
      LOGMSG_P(l_ERROR, "Couldn't allocate memory");
      closedir(dir);
      return false;
    }

    pFiles->files[pFiles->fileCnt] = strdup(path);
    if (!pFiles->files[pFiles->fileCnt]) {
      LOGMSG_P(l_ERROR, "Couldn't allocate memory");
      closedir(dir);
      return false;
    }
    pFiles->fileCnt++;

    LOGMSG(l_DEBUG, "Added '%s' to the list of input files", path);
  }

  closedir(dir);
  return true;
}

bool utils_init_files(infiles_t *pFiles) {
  pFiles->files = malloc(sizeof(char *));
  if (!pFiles->files) {
    LOGMSG_P(l_ERROR, "Couldn't allocate memory");
    return false;
  }

  if (!pFiles->inputFile) {
    LOGMSG(l_ERROR, "No input file/dir specified");
    return false;
  }

  struct stat st;
  if (stat(pFiles->inputFile, &st) == -1) {
    LOGMSG_P(l_ERROR, "Couldn't stat the input file/dir '%s'", pFiles->inputFile);
    return false;
  }

  // If a directory, recursively scan
  if (S_ISDIR(st.st_mode)) {
    if (!utils_readdir(pFiles, pFiles->inputFile)) {
      LOGMSG(l_ERROR, "Failed to recursively process '%s' directory", pFiles->inputFile);
      return false;
    }

    if (pFiles->fileCnt == 0) {
      LOGMSG(l_ERROR, "Directory '%s' doesn't contain any regular files", pFiles->inputFile);
      return false;
    }

    LOGMSG(l_INFO, "%u input files have been added to the list", pFiles->fileCnt);
    return true;
  }

  if (!S_ISREG(st.st_mode)) {
    LOGMSG(l_ERROR, "'%s' is not a regular file, nor a directory", pFiles->inputFile);
    return false;
  }

  // Single file case
  pFiles->files[0] = pFiles->inputFile;
  pFiles->fileCnt = 1;

  return true;
}

bool utils_writeToFd(int fd, const u1 *buf, off_t fileSz) {
  off_t written = 0;
  while (written < fileSz) {
    ssize_t sz = write(fd, &buf[written], fileSz - written);
    if (sz < 0 && errno == EINTR) continue;

    if (sz < 0) return false;

    written += sz;
  }

  return true;
}

u1 *utils_mapFileToRead(const char *fileName, off_t *fileSz, int *fd) {
  if ((*fd = open(fileName, O_RDONLY)) == -1) {
    LOGMSG_P(l_WARN, "Couldn't open() '%s' file in R/O mode", fileName);
    return NULL;
  }

  struct stat st;
  if (fstat(*fd, &st) == -1) {
    LOGMSG_P(l_WARN, "Couldn't stat() the '%s' file", fileName);
    close(*fd);
    return NULL;
  }

  u1 *buf;
  if ((buf = mmap(NULL, st.st_size, PROT_READ | PROT_WRITE, MAP_PRIVATE, *fd, 0)) == MAP_FAILED) {
    LOGMSG_P(l_WARN, "Couldn't mmap() the '%s' file", fileName);
    close(*fd);
    return NULL;
  }

  *fileSz = st.st_size;
  return buf;
}

void *utils_malloc(size_t sz) {
  void *p = malloc(sz);
  if (p == NULL) {
    // This is expected to abort
    LOGMSG(l_FATAL, "malloc(size='%zu')", sz);
  }
  return p;
}

void *utils_calloc(size_t sz) {
  void *p = utils_malloc(sz);
  memset(p, '\0', sz);
  return p;
}

void *utils_realloc(void *ptr, size_t sz) {
  void *ret = realloc(ptr, sz);
  if (ret == NULL) {
    // This is expected to abort
    LOGMSG_P(l_FATAL, "realloc(%p, %zu)", ptr, sz);
    free(ptr);
  }
  return ret;
}

void *utils_crealloc(void *ptr, size_t old_sz, size_t new_sz) {
  // utils_realloc is expected to abort in case of error
  void *ret = utils_realloc(ptr, new_sz);
  memset(ret + old_sz, 0, new_sz - old_sz);
  return ret;
}

char *utils_fileBasename(char const *path) {
  char *s = strrchr(path, '/');
  if (!s) {
    return strdup(path);
  } else {
    return strdup(s + 1);
  }
}

bool utils_isValidDir(const char *path) {
  struct stat buf;
  if (stat(path, &buf) != 0) {
    LOGMSG(l_ERROR, "stat() failed: %s", strerror(errno));
    return false;
  }
  return S_ISDIR(buf.st_mode);
}

void utils_hexDump(char *desc, const u1 *addr, int len) {
  int i;
  unsigned char buff[17];
  unsigned char *pc = (unsigned char *)addr;

  // Output description if given.
  if (desc != NULL) LOGMSG_RAW(l_DEBUG, "%s:\n", desc);

  if (len == 0) {
    LOGMSG_RAW(l_DEBUG, "  ZERO LENGTH\n");
    return;
  }
  if (len < 0) {
    LOGMSG_RAW(l_DEBUG, "  NEGATIVE LENGTH: %i\n", len);
    return;
  }

  // Process every byte in the data.
  for (i = 0; i < len; i++) {
    // Multiple of 16 means new line (with line offset).

    if ((i % 16) == 0) {
      // Just don't print ASCII for the zeroth line.
      if (i != 0) LOGMSG_RAW(l_DEBUG, "  %s\n", buff);

      // Output the offset.
      LOGMSG_RAW(l_DEBUG, "  %04x ", i);
    }

    // Now the hex code for the specific character.
    LOGMSG_RAW(l_DEBUG, " %02x", pc[i]);

    // And store a printable ASCII character for later.
    if ((pc[i] < 0x20) || (pc[i] > 0x7e))
      buff[i % 16] = '.';
    else
      buff[i % 16] = pc[i];
    buff[(i % 16) + 1] = '\0';
  }

  // Pad out last line if not exactly 16 characters.
  while ((i % 16) != 0) {
    LOGMSG_RAW(l_DEBUG, "   ");
    i++;
  }

  // And print the final ASCII bit.
  LOGMSG_RAW(l_DEBUG, "  %s\n", buff);
}
