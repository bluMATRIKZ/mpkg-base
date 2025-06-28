#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <archive.h>
#include <archive_entry.h>
#include <errno.h>
#include <time.h>

#define PKG_DB_PATH "/var/db/mypkg"
#define PKG_CACHE_PATH "/var/cache/mypkg"
#define PKG_REPO_URL "https://loxsete.github.io/mypcg-repo/"

extern int is_installed(const char *package_name);


typedef struct {
    char name[256];
    char version[64]; 
    char arch[16];
    char depends[1024];
    char description[512];
    size_t size;
    time_t install_time;
} Package;

int db_init() {
    if (mkdir(PKG_DB_PATH, 0755) == -1) {
        if (errno != EEXIST) {
            fprintf(stderr, "Fuck, can't create db dir: %s\n", strerror(errno));
            return -1;
        }
    }
    if (mkdir(PKG_CACHE_PATH, 0755) == -1) {
        if (errno != EEXIST) {
            fprintf(stderr, "Shit, cache dir creation failed: %s\n", strerror(errno));
            return -1;
        }
    }
    return 0;
}

int is_installed(const char *package_name) {
    char db_file[512];
    snprintf(db_file, sizeof(db_file), "%s/%s.installed", PKG_DB_PATH, package_name);
    return access(db_file, F_OK) == 0;
}

Package* read_package_info(const char *archive_path) {
    struct archive *a;
    struct archive_entry *entry;
    Package *pkg = NULL;
    int r;
    
    a = archive_read_new();
    archive_read_support_format_all(a);
    archive_read_support_filter_all(a);
    
    if ((r = archive_read_open_filename(a, archive_path, 10240))) {
        fprintf(stderr, "Damn, archive open fucked up: %s\n", archive_error_string(a));
        archive_read_free(a);
        return NULL;
    }
    
    while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
        const char *name = archive_entry_pathname(entry);
        
        if (strcmp(name, "PKGINFO") == 0 || strcmp(name, "./PKGINFO") == 0) {
            pkg = malloc(sizeof(Package));
            memset(pkg, 0, sizeof(Package));
            
            size_t size = archive_entry_size(entry);
            char *content = malloc(size + 1);
            archive_read_data(a, content, size);
            content[size] = '\0';
            
            char *line = strtok(content, "\n");
            while (line != NULL) {
                if (strncmp(line, "name=", 5) == 0) {
                    strncpy(pkg->name, line + 5, sizeof(pkg->name) - 1);
                } else if (strncmp(line, "version=", 8) == 0) {
                    strncpy(pkg->version, line + 8, sizeof(pkg->version) - 1);
                } else if (strncmp(line, "arch=", 5) == 0) {
                    strncpy(pkg->arch, line + 5, sizeof(pkg->arch) - 1);
                } else if (strncmp(line, "description=", 12) == 0) {
                    strncpy(pkg->description, line + 12, sizeof(pkg->description) - 1);
                } else if (strncmp(line, "depends=", 8) == 0) {
                    strncpy(pkg->depends, line + 8, sizeof(pkg->depends) - 1);
                } else if (strncmp(line, "size=", 5) == 0) {
                    pkg->size = atol(line + 5);
                }
                line = strtok(NULL, "\n");
            }
            
            free(content);
            break;
        } else {
            archive_read_data_skip(a);
        }
    }
    
    archive_read_close(a);
    archive_read_free(a);
    
    return pkg;
}

int check_dependencies(const char *depends) {
    if (strlen(depends) == 0) return 0;

    char deps_copy[1024];
    strncpy(deps_copy, depends, sizeof(deps_copy) - 1);
    deps_copy[sizeof(deps_copy) - 1] = '\0';

    char *dep = strtok(deps_copy, ",");
    int missing_deps = 0;

    while (dep != NULL) {
        while (*dep == ' ') dep++;
        char *end = dep + strlen(dep) - 1;
        while (end > dep && *end == ' ') {
            *end = '\0';
            end--;
        }

        if (!is_installed(dep)) {
            printf("Fucking error: dependency '%s' is missing!\n", dep);
            missing_deps++;
        } else {
            printf("Dependency '%s' is installed. Hell yeah.\n", dep);
        }

        dep = strtok(NULL, ",");
    }

    if (missing_deps > 0) {
        fprintf(stderr, "Shit! %d dependencies are missing. Install failed, bro.\n", missing_deps);
        return -1;
    }

    return 0;
}



int download_package(const char *package_name) {
    char url[512];
    char cache_file[512];
    char cmd[1024];

    snprintf(url, sizeof(url), "%s/%s.tar.xz", PKG_REPO_URL, package_name);
    snprintf(cache_file, sizeof(cache_file), "%s/%s.tar.xz", PKG_CACHE_PATH, package_name);
    snprintf(cmd, sizeof(cmd), "curl -L -f -o %s %s", cache_file, url);

    printf("Grabbing %s, hold tight...\n", package_name);
    int result = system(cmd);
    if (result != 0) {
        fprintf(stderr, "Fuck, download for %s went to shit\n", package_name);
        return -1;
    }
    
    return 0;
}

static int copy_data(struct archive *ar, struct archive *aw) {
    int r;
    const void *buff;
    size_t size;
    la_int64_t offset;

    for (;;) {
        r = archive_read_data_block(ar, &buff, &size, &offset);
        if (r == ARCHIVE_EOF)
            return ARCHIVE_OK;
        if (r < ARCHIVE_OK)
            return r;
        r = archive_write_data_block(aw, buff, size, offset);
        if (r < ARCHIVE_OK) {
            fprintf(stderr, "Write error, what the hell: %s\n", archive_error_string(aw));
            return r;
        }
    }
}

int extract_package(const char *package_name) {
    char archive_path[512];
    struct archive *a;
    struct archive *ext;
    struct archive_entry *entry;
    int r;
    char *old_cwd = NULL;

    snprintf(archive_path, sizeof(archive_path), "%s/%s.tar.xz", PKG_CACHE_PATH, package_name);

    a = archive_read_new();
    archive_read_support_format_all(a);
    archive_read_support_filter_all(a);

    ext = archive_write_disk_new();
    archive_write_disk_set_options(ext, ARCHIVE_EXTRACT_TIME | ARCHIVE_EXTRACT_PERM | ARCHIVE_EXTRACT_OWNER);

    if ((r = archive_read_open_filename(a, archive_path, 10240))) {
        fprintf(stderr, "Archive open fucked up: %s\n", archive_error_string(a));
        return -1;
    }

    printf("Unpacking %s, let's roll...\n", package_name);
    char files_log_path[512];
    FILE *files_log;
    
    snprintf(files_log_path, sizeof(files_log_path), "%s/%s.files", PKG_DB_PATH, package_name);
    files_log = fopen(files_log_path, "w");
    if (!files_log) {
        fprintf(stderr, "Can't open files log, damn it: %s\n", strerror(errno));
        return -1;
    }

    old_cwd = getcwd(NULL, 0);
    if (chdir("/") != 0) {
        fprintf(stderr, "Chdir to root failed, shit: %s\n", strerror(errno));
        if (old_cwd) free(old_cwd);
        fclose(files_log);
        return -1;
    }

    while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
        const char *name = archive_entry_pathname(entry);
        
        if (strncmp(name, "PKGINFO", 7) == 0 || strncmp(name, "FILES", 5) == 0 ||
            strncmp(name, "./PKGINFO", 9) == 0 || strncmp(name, "./FILES", 7) == 0) {
            archive_read_data_skip(a);
            continue;
        }

        printf("  %s\n", name);

        if (archive_entry_filetype(entry) == AE_IFREG) {
            const char *clean_path = name;
            if (strncmp(name, "./", 2) == 0) {
                clean_path = name + 1;
            }
            
            if (clean_path[0] != '/') {
                fprintf(files_log, "/%s\n", clean_path);
            } else {
                fprintf(files_log, "%s\n", clean_path);
            }
        }

        if ((r = archive_write_header(ext, entry)) != ARCHIVE_OK) {
            fprintf(stderr, "Header write fucked up: %s\n", archive_error_string(ext));
        } else {
            copy_data(a, ext);
        }
    }

    if (old_cwd) {
        chdir(old_cwd);
        free(old_cwd);
    }

    fclose(files_log);
    archive_read_close(a);
    archive_read_free(a);
    archive_write_close(ext);
    archive_write_free(ext);

    return 0;
}

int mark_installed(const char *package_name, Package *pkg) {
    char db_file[512];
    FILE *f;

    snprintf(db_file, sizeof(db_file), "%s/%s.installed", PKG_DB_PATH, package_name);

    f = fopen(db_file, "w");
    if (!f) {
        fprintf(stderr, "Can't open db file, fuck: %s\n", strerror(errno));
        return -1;
    }

    time_t now = time(NULL);
    
    fprintf(f, "name=%s\n", pkg ? pkg->name : package_name);
    if (pkg) {
        fprintf(f, "version=%s\n", pkg->version);
        fprintf(f, "arch=%s\n", pkg->arch);
        fprintf(f, "description=%s\n", pkg->description);
        fprintf(f, "depends=%s\n", pkg->depends);
        fprintf(f, "size=%zu\n", pkg->size);
    }
    fprintf(f, "install_time=%ld\n", now);
    fprintf(f, "installed=1\n");
    
    fclose(f);
    return 0;
}

Package* read_installed_package(const char *package_name) {
    char db_file[512];
    FILE *f;
    Package *pkg;
    char line[1024];

    snprintf(db_file, sizeof(db_file), "%s/%s.installed", PKG_DB_PATH, package_name);
    
    f = fopen(db_file, "r");
    if (!f) return NULL;
    
    pkg = malloc(sizeof(Package));
    memset(pkg, 0, sizeof(Package));
    
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\n")] = 0;
        
        if (strncmp(line, "name=", 5) == 0) {
            strncpy(pkg->name, line + 5, sizeof(pkg->name) - 1);
        } else if (strncmp(line, "version=", 8) == 0) {
            strncpy(pkg->version, line + 8, sizeof(pkg->version) - 1);
        } else if (strncmp(line, "arch=", 5) == 0) {
            strncpy(pkg->arch, line + 5, sizeof(pkg->arch) - 1);
        } else if (strncmp(line, "description=", 12) == 0) {
            strncpy(pkg->description, line + 12, sizeof(pkg->description) - 1);
        } else if (strncmp(line, "depends=", 8) == 0) {
            strncpy(pkg->depends, line + 8, sizeof(pkg->depends) - 1);
        } else if (strncmp(line, "size=", 5) == 0) {
            pkg->size = atol(line + 5);
        } else if (strncmp(line, "install_time=", 13) == 0) {
            pkg->install_time = atol(line + 13);
        }
    }
    
    fclose(f);
    return pkg;
}

int install_package(const char *package_name) {
    if (is_installed(package_name)) {
        printf("%s is already fucking installed, chill\n", package_name);
        return 0;
    }

    printf("Installing %s, let's do this shit\n", package_name);

    if (download_package(package_name) != 0) {
        fprintf(stderr, "Download for %s fucked up\n", package_name);
        return -1;
    }

    char cache_file[512];
    snprintf(cache_file, sizeof(cache_file), "%s/%s.tar.xz", PKG_CACHE_PATH, package_name);
    
    Package *pkg = read_package_info(cache_file);
    if (pkg) {
        printf("Package details, check this shit out:\n");
        printf("  name: %s\n", pkg->name);
        printf("  version: %s\n", pkg->version);
        printf("  arch: %s\n", pkg->arch);
        printf("  description: %s\n", pkg->description);
        if (strlen(pkg->depends) > 0) {
            printf("  depends: %s\n", pkg->depends);
            check_dependencies(pkg->depends);
        }
        if (pkg->size > 0) {
            printf("  size: %zu bytes\n", pkg->size);
        }
    }

    if (extract_package(package_name) != 0) {
        fprintf(stderr, "Extracting %s went to hell\n", package_name);
        if (pkg) free(pkg);
        return -1;
    }

    mark_installed(package_name, pkg);
    
    printf("%s installed like a badass\n", package_name);
    
    if (pkg) free(pkg);
    return 0;
}

int remove_package(const char *package_name) {
    char db_file[512];
    char files_file[512];

    if (!is_installed(package_name)) {
        printf("%s ain't installed, what the fuck?\n", package_name);
        return 0;
    }

    printf("Nuking %s, hold on...\n", package_name);

    snprintf(files_file, sizeof(files_file), "%s/%s.files", PKG_DB_PATH, package_name);
    FILE *f = fopen(files_file, "r");
    if (f) {
        char path[1024];
        int files_removed = 0;
        int files_failed = 0;
        
        while (fgets(path, sizeof(path), f)) {
            path[strcspn(path, "\n")] = 0;
            
            if (strlen(path) == 0) continue;
            
            printf("  Deleting: %s\n", path);
            
            if (unlink(path) == 0) {
                files_removed++;
            } else {
                printf("  Couldn't delete %s, damn it: %s\n", path, strerror(errno));
                files_failed++;
            }
        }
        fclose(f);
        
        printf("Cleanup: %d files trashed, %d fucked up\n", files_removed, files_failed);
        
        if (unlink(files_file) != 0) {
            printf("Warning: Couldn't delete files list, shit: %s\n", strerror(errno));
        }
    } else {
        printf("Warning: No files list for %s, what the hell?\n", package_name);
    }

    snprintf(db_file, sizeof(db_file), "%s/%s.installed", PKG_DB_PATH, package_name);
    if (unlink(db_file) != 0) {
        printf("Warning: Couldn't remove install record, fuck: %s\n", strerror(errno));
    }

    printf("%s is gone, baby, gone\n", package_name);
    return 0;
}

void list_installed() {
    DIR *dir;
    struct dirent *entry;
    char *dot;

    dir = opendir(PKG_DB_PATH);
    if (!dir) {
        fprintf(stderr, "Can't open dir, shit: %s\n", strerror(errno));
        return;
    }

    printf("Installed packages, check this shit:\n");
    while ((entry = readdir(dir)) != NULL) {
        if (strstr(entry->d_name, ".installed")) {
            char pkg_name[256];
            strncpy(pkg_name, entry->d_name, sizeof(pkg_name) - 1);
            pkg_name[sizeof(pkg_name) - 1] = '\0';
            
            dot = strstr(pkg_name, ".installed");
            if (dot) *dot = '\0';
            
            Package *pkg = read_installed_package(pkg_name);
            if (pkg) {
                printf("  %s-%s (%s)\n", pkg->name, pkg->version, pkg->description);
                free(pkg);
            } else {
                printf("  %s\n", pkg_name);
            }
        }
    }

    closedir(dir);
}

void show_package_info(const char *package_name) {
    if (!is_installed(package_name)) {
        printf("%s ain't installed, dumbass\n", package_name);
        return;
    }
    
    Package *pkg = read_installed_package(package_name);
    if (!pkg) {
        printf("Can't read info for %s, shit's broken\n", package_name);
        return;
    }
    
    printf("Package info, here’s the deal:\n");
    printf("  name: %s\n", pkg->name);
    printf("  version: %s\n", pkg->version);
    printf("  architecture: %s\n", pkg->arch);
    printf("  description: %s\n", pkg->description);
    if (strlen(pkg->depends) > 0) {
        printf("  dependencies: %s\n", pkg->depends);
    }
    if (pkg->size > 0) {
        printf("  installed size: %zu bytes\n", pkg->size);
    }
    if (pkg->install_time > 0) {
        printf("  install date: %s", ctime(&pkg->install_time));
    }
    
    char files_file[512];
    snprintf(files_file, sizeof(files_file), "%s/%s.files", PKG_DB_PATH, package_name);
    FILE *f = fopen(files_file, "r");
    if (f) {
        printf("  files:\n");
        char path[1024];
        int count = 0;
        while (fgets(path, sizeof(path), f) && count < 10) {
            path[strcspn(path, "\n")] = 0;
            if (strlen(path) > 0) {
                printf("    %s\n", path);
                count++;
            }
        }
        if (!feof(f)) {
            printf("    ... and more shit\n");
        }
        fclose(f);
    }
    
    free(pkg);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: %s <command> [package], don't fuck it up\n", argv[0]);
        printf("Commands:\n");
        printf("  install <package>  - install some shit\n");
        printf("  remove <package>   - nuke that package\n");
        printf("  list               - show installed crap\n");
        printf("  info <package>     - get package details\n");
        return 1;
    }

    if (db_init() != 0) {
        fprintf(stderr, "Database init fucked up\n");
        return 1;
    }

    if (strcmp(argv[1], "install") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Gimme a package name to install, asshole\n");
            return 1;
        }
        return install_package(argv[2]);
    }

    if (strcmp(argv[1], "remove") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Tell me what package to nuke, dipshit\n");
            return 1;
        }
        return remove_package(argv[2]);
    }

    if (strcmp(argv[1], "list") == 0) {
        list_installed();
        return 0;
    }

    if (strcmp(argv[1], "info") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Need a package name for info, genius\n");
            return 1;
        }
        show_package_info(argv[2]);
        return 0;
    }

    fprintf(stderr, "Unknown command: %s, what the fuck?\n", argv[1]);
    return 1;
}
