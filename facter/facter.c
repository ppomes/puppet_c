/**
 * @file facter.c
 * @brief Native fact collection implementation for Linux
 */

#include <stdio.h>
#include <stdlib.h>
#include "puppet_memory.h"
#include "puppet_json_common.h"
#include "file_utils.h"
#include "string_utils.h"
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <time.h>
#include <sys/utsname.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <dirent.h>
#include <ctype.h>
#include <errno.h>

/* Platform-specific includes */
#ifdef __linux__
#include <sys/sysinfo.h>
#endif

#ifdef __APPLE__
#include <sys/sysctl.h>
#include <mach/mach.h>
#include <mach/mach_host.h>
#include <sys/param.h>
#include <sys/mount.h>
#include <net/if_dl.h>
#include <IOKit/IOKitLib.h>
#include <CoreFoundation/CoreFoundation.h>
#endif

#include "facter.h"

#define FACTER_VERSION "0.1.0"
#define MAX_FACTS 256
#define MAX_FACT_NAME 128
#define MAX_INTERFACES 32

/* ============================================================================
 * Internal Structures
 * ============================================================================ */

typedef struct {
    char name[MAX_FACT_NAME];
    facter_value_t *value;
} fact_entry_t;

struct facter_ctx {
    fact_entry_t facts[MAX_FACTS];
    size_t fact_count;
    const char *fact_names[MAX_FACTS];  /* For facter_list */
    bool collected;
};

/* ============================================================================
 * Value Creation/Destruction
 * ============================================================================ */

facter_value_t *facter_value_string(const char *str) {
    facter_value_t *v = puppet_calloc(1, sizeof(facter_value_t));
    if (!v) return NULL;
    v->type = FACTER_VALUE_STRING;
    v->data.string_val = safe_strdup(str);
    return v;
}

facter_value_t *facter_value_integer(long val) {
    facter_value_t *v = puppet_calloc(1, sizeof(facter_value_t));
    if (!v) return NULL;
    v->type = FACTER_VALUE_INTEGER;
    v->data.integer_val = val;
    return v;
}

facter_value_t *facter_value_boolean(bool val) {
    facter_value_t *v = puppet_calloc(1, sizeof(facter_value_t));
    if (!v) return NULL;
    v->type = FACTER_VALUE_BOOLEAN;
    v->data.boolean_val = val;
    return v;
}

static facter_value_t *facter_value_float(double val) __attribute__((unused));
static facter_value_t *facter_value_float(double val) {
    facter_value_t *v = puppet_calloc(1, sizeof(facter_value_t));
    if (!v) return NULL;
    v->type = FACTER_VALUE_FLOAT;
    v->data.float_val = val;
    return v;
}

void facter_value_free(facter_value_t *value) {
    if (!value) return;

    switch (value->type) {
        case FACTER_VALUE_STRING:
            puppet_free(value->data.string_val);
            break;
        case FACTER_VALUE_ARRAY:
            for (size_t i = 0; i < value->data.array_val.count; i++) {
                facter_value_free(value->data.array_val.items[i]);
            }
            puppet_free(value->data.array_val.items);
            break;
        case FACTER_VALUE_HASH:
            for (size_t i = 0; i < value->data.hash_val.count; i++) {
                puppet_free(value->data.hash_val.keys[i]);
                facter_value_free(value->data.hash_val.values[i]);
            }
            puppet_free(value->data.hash_val.keys);
            puppet_free(value->data.hash_val.values);
            break;
        default:
            break;
    }
    puppet_free(value);
}

/* ============================================================================
 * Context Management
 * ============================================================================ */

facter_ctx_t *facter_create(void) {
    facter_ctx_t *ctx = puppet_calloc(1, sizeof(facter_ctx_t));
    return ctx;
}

void facter_destroy(facter_ctx_t *ctx) {
    if (!ctx) return;

    for (size_t i = 0; i < ctx->fact_count; i++) {
        facter_value_free(ctx->facts[i].value);
    }
    puppet_free(ctx);
}

static int facter_set(facter_ctx_t *ctx, const char *name, facter_value_t *value) {
    if (!ctx || !name || !value) return -1;
    if (ctx->fact_count >= MAX_FACTS) return -1;

    /* Check if fact already exists */
    for (size_t i = 0; i < ctx->fact_count; i++) {
        if (strcmp(ctx->facts[i].name, name) == 0) {
            facter_value_free(ctx->facts[i].value);
            ctx->facts[i].value = value;
            return 0;
        }
    }

    /* Add new fact */
    strncpy(ctx->facts[ctx->fact_count].name, name, MAX_FACT_NAME - 1);
    ctx->facts[ctx->fact_count].value = value;
    ctx->fact_names[ctx->fact_count] = ctx->facts[ctx->fact_count].name;
    ctx->fact_count++;

    return 0;
}

int facter_set_string(facter_ctx_t *ctx, const char *name, const char *value) {
    if (!value) return -1;
    return facter_set(ctx, name, facter_value_string(value));
}

static int facter_set_integer(facter_ctx_t *ctx, const char *name, long value) {
    return facter_set(ctx, name, facter_value_integer(value));
}

static int facter_set_boolean(facter_ctx_t *ctx, const char *name, bool value) {
    return facter_set(ctx, name, facter_value_boolean(value));
}

/* ============================================================================
 * Hash Value Helpers (for nested fact structures)
 * ============================================================================ */

static facter_value_t *facter_value_hash_create(void) {
    facter_value_t *v = puppet_calloc(1, sizeof(facter_value_t));
    if (!v) return NULL;
    v->type = FACTER_VALUE_HASH;
    v->data.hash_val.keys = NULL;
    v->data.hash_val.values = NULL;
    v->data.hash_val.count = 0;
    return v;
}

static int facter_hash_set_string(facter_value_t *hash, const char *key, const char *value) {
    if (!hash || hash->type != FACTER_VALUE_HASH || !key) return -1;

    /* Check if key already exists */
    for (size_t i = 0; i < hash->data.hash_val.count; i++) {
        if (strcmp(hash->data.hash_val.keys[i], key) == 0) {
            facter_value_free(hash->data.hash_val.values[i]);
            hash->data.hash_val.values[i] = facter_value_string(value);
            return 0;
        }
    }

    /* Add new key */
    size_t new_count = hash->data.hash_val.count + 1;
    hash->data.hash_val.keys = puppet_realloc(hash->data.hash_val.keys, new_count * sizeof(char*));
    hash->data.hash_val.values = puppet_realloc(hash->data.hash_val.values, new_count * sizeof(facter_value_t*));

    hash->data.hash_val.keys[hash->data.hash_val.count] = puppet_strdup(key);
    hash->data.hash_val.values[hash->data.hash_val.count] = facter_value_string(value);
    hash->data.hash_val.count = new_count;

    return 0;
}

static int facter_hash_set_integer(facter_value_t *hash, const char *key, long value) {
    if (!hash || hash->type != FACTER_VALUE_HASH || !key) return -1;

    /* Check if key already exists */
    for (size_t i = 0; i < hash->data.hash_val.count; i++) {
        if (strcmp(hash->data.hash_val.keys[i], key) == 0) {
            facter_value_free(hash->data.hash_val.values[i]);
            hash->data.hash_val.values[i] = facter_value_integer(value);
            return 0;
        }
    }

    /* Add new key */
    size_t new_count = hash->data.hash_val.count + 1;
    hash->data.hash_val.keys = puppet_realloc(hash->data.hash_val.keys, new_count * sizeof(char*));
    hash->data.hash_val.values = puppet_realloc(hash->data.hash_val.values, new_count * sizeof(facter_value_t*));

    hash->data.hash_val.keys[hash->data.hash_val.count] = puppet_strdup(key);
    hash->data.hash_val.values[hash->data.hash_val.count] = facter_value_integer(value);
    hash->data.hash_val.count = new_count;

    return 0;
}

static int __attribute__((unused)) facter_hash_set_boolean(facter_value_t *hash, const char *key, bool value) {
    if (!hash || hash->type != FACTER_VALUE_HASH || !key) return -1;

    /* Check if key already exists */
    for (size_t i = 0; i < hash->data.hash_val.count; i++) {
        if (strcmp(hash->data.hash_val.keys[i], key) == 0) {
            facter_value_free(hash->data.hash_val.values[i]);
            hash->data.hash_val.values[i] = facter_value_boolean(value);
            return 0;
        }
    }

    /* Add new key */
    size_t new_count = hash->data.hash_val.count + 1;
    hash->data.hash_val.keys = puppet_realloc(hash->data.hash_val.keys, new_count * sizeof(char*));
    hash->data.hash_val.values = puppet_realloc(hash->data.hash_val.values, new_count * sizeof(facter_value_t*));

    hash->data.hash_val.keys[hash->data.hash_val.count] = puppet_strdup(key);
    hash->data.hash_val.values[hash->data.hash_val.count] = facter_value_boolean(value);
    hash->data.hash_val.count = new_count;

    return 0;
}

static int facter_hash_set_hash(facter_value_t *hash, const char *key, facter_value_t *value) {
    if (!hash || hash->type != FACTER_VALUE_HASH || !key || !value) return -1;

    /* Check if key already exists */
    for (size_t i = 0; i < hash->data.hash_val.count; i++) {
        if (strcmp(hash->data.hash_val.keys[i], key) == 0) {
            facter_value_free(hash->data.hash_val.values[i]);
            hash->data.hash_val.values[i] = value;
            return 0;
        }
    }

    /* Add new key */
    size_t new_count = hash->data.hash_val.count + 1;
    hash->data.hash_val.keys = puppet_realloc(hash->data.hash_val.keys, new_count * sizeof(char*));
    hash->data.hash_val.values = puppet_realloc(hash->data.hash_val.values, new_count * sizeof(facter_value_t*));

    hash->data.hash_val.keys[hash->data.hash_val.count] = puppet_strdup(key);
    hash->data.hash_val.values[hash->data.hash_val.count] = value;
    hash->data.hash_val.count = new_count;

    return 0;
}

static facter_value_t __attribute__((unused)) *facter_hash_get(facter_value_t *hash, const char *key) {
    if (!hash || hash->type != FACTER_VALUE_HASH || !key) return NULL;

    for (size_t i = 0; i < hash->data.hash_val.count; i++) {
        if (strcmp(hash->data.hash_val.keys[i], key) == 0) {
            return hash->data.hash_val.values[i];
        }
    }
    return NULL;
}

/* ============================================================================
 * Direct Fact Collectors
 * ============================================================================ */

char *facter_hostname(void) {
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) == 0) {
        /* Remove domain part if present */
        char *dot = strchr(hostname, '.');
        if (dot) *dot = '\0';
        return puppet_strdup(hostname);
    }
    return NULL;
}

char *facter_fqdn(void) {
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) != 0) {
        return NULL;
    }

    struct addrinfo hints, *info;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_CANONNAME;

    if (getaddrinfo(hostname, NULL, &hints, &info) == 0 && info) {
        char *fqdn = puppet_strdup(info->ai_canonname ? info->ai_canonname : hostname);
        freeaddrinfo(info);
        return fqdn;
    }

    return puppet_strdup(hostname);
}

char *facter_domain(void) {
    char *fqdn = facter_fqdn();
    if (!fqdn) return NULL;

    char *dot = strchr(fqdn, '.');
    if (dot && *(dot + 1)) {
        char *domain = puppet_strdup(dot + 1);
        puppet_free(fqdn);
        return domain;
    }

    puppet_free(fqdn);
    return NULL;
}

char *facter_kernel(void) {
    struct utsname buf;
    if (uname(&buf) == 0) {
        return puppet_strdup(buf.sysname);
    }
    return NULL;
}

char *facter_kernelversion(void) {
    struct utsname buf;
    if (uname(&buf) == 0) {
        /* Extract major.minor from release */
        char *version = puppet_strdup(buf.release);
        if (version) {
            char *p = version;
            int dots = 0;
            while (*p) {
                if (*p == '.') {
                    dots++;
                    if (dots == 2) {
                        *p = '\0';
                        break;
                    }
                }
                p++;
            }
        }
        return version;
    }
    return NULL;
}

char *facter_kernelrelease(void) {
    struct utsname buf;
    if (uname(&buf) == 0) {
        return puppet_strdup(buf.release);
    }
    return NULL;
}

char *facter_architecture(void) {
    struct utsname buf;
    if (uname(&buf) == 0) {
        return puppet_strdup(buf.machine);
    }
    return NULL;
}

char *facter_os_name(void) {
#ifdef __APPLE__
    return puppet_strdup("macOS");
#elif defined(__linux__)
    /* Try /etc/os-release first (modern distros) */
    /* Use ID= (e.g., "debian") rather than NAME= ("Debian GNU/Linux")
     * to match Puppet's facter behavior */
    char *content = read_file_contents("/etc/os-release", 0);
    if (content) {
        char *line = strtok(content, "\n");
        while (line) {
            if (strncmp(line, "ID=", 3) == 0) {
                char *id = line + 3;
                /* Remove quotes if present */
                if (*id == '"') id++;
                char *end = id + strlen(id) - 1;
                if (*end == '"') *end = '\0';
                /* Capitalize first letter (debian -> Debian) */
                char *result = puppet_strdup(id);
                if (result && result[0] >= 'a' && result[0] <= 'z') {
                    result[0] = result[0] - 'a' + 'A';
                }
                puppet_free(content);
                return result;
            }
            line = strtok(NULL, "\n");
        }
        puppet_free(content);
    }

    /* Fallback to uname */
    struct utsname buf;
    if (uname(&buf) == 0) {
        return puppet_strdup(buf.sysname);
    }

    return puppet_strdup("Linux");
#else
    struct utsname buf;
    if (uname(&buf) == 0) {
        return puppet_strdup(buf.sysname);
    }
    return puppet_strdup("Unknown");
#endif
}

char *facter_os_family(void) {
#ifdef __APPLE__
    return puppet_strdup("Darwin");
#elif defined(__linux__)
    char *content = read_file_contents("/etc/os-release", 0);
    if (content) {
        char *id_like = NULL;
        char *id = NULL;

        char *line = strtok(content, "\n");
        while (line) {
            if (strncmp(line, "ID_LIKE=", 8) == 0) {
                id_like = line + 8;
                if (*id_like == '"') id_like++;
            } else if (strncmp(line, "ID=", 3) == 0) {
                id = line + 3;
                if (*id == '"') id++;
            }
            line = strtok(NULL, "\n");
        }

        const char *family = id_like ? id_like : id;
        if (family) {
            /* Map to Puppet OS families */
            if (strstr(family, "debian") || strstr(family, "ubuntu")) {
                puppet_free(content);
                return puppet_strdup("Debian");
            } else if (strstr(family, "rhel") || strstr(family, "fedora") ||
                       strstr(family, "centos") || strstr(family, "redhat")) {
                puppet_free(content);
                return puppet_strdup("RedHat");
            } else if (strstr(family, "suse") || strstr(family, "opensuse")) {
                puppet_free(content);
                return puppet_strdup("Suse");
            } else if (strstr(family, "arch")) {
                puppet_free(content);
                return puppet_strdup("Archlinux");
            } else if (strstr(family, "gentoo")) {
                puppet_free(content);
                return puppet_strdup("Gentoo");
            }
        }
        puppet_free(content);
    }

    /* Check for specific distro files */
    if (access("/etc/debian_version", F_OK) == 0) {
        return puppet_strdup("Debian");
    } else if (access("/etc/redhat-release", F_OK) == 0) {
        return puppet_strdup("RedHat");
    } else if (access("/etc/SuSE-release", F_OK) == 0) {
        return puppet_strdup("Suse");
    }

    return puppet_strdup("Linux");
#else
    return puppet_strdup("Unknown");
#endif
}

char *facter_os_release(void) {
#ifdef __APPLE__
    char osversion[256];
    size_t len = sizeof(osversion);
    if (sysctlbyname("kern.osproductversion", osversion, &len, NULL, 0) == 0) {
        return puppet_strdup(osversion);
    }
    /* Fallback to kern.osrelease */
    if (sysctlbyname("kern.osrelease", osversion, &len, NULL, 0) == 0) {
        return puppet_strdup(osversion);
    }
    return NULL;
#elif defined(__linux__)
    char *content = read_file_contents("/etc/os-release", 0);
    if (content) {
        char *line = strtok(content, "\n");
        while (line) {
            if (strncmp(line, "VERSION_ID=", 11) == 0) {
                char *version = line + 11;
                if (*version == '"') version++;
                char *end = version + strlen(version) - 1;
                if (*end == '"') *end = '\0';
                char *result = puppet_strdup(version);
                puppet_free(content);
                return result;
            }
            line = strtok(NULL, "\n");
        }
        puppet_free(content);
    }

    return NULL;
#else
    return NULL;
#endif
}

/* LSB (Linux Standard Base) fact helpers */
static char *facter_lsb_field(const char *field) {
#ifdef __linux__
    /* Try /etc/lsb-release first */
    char *content = read_file_contents("/etc/lsb-release", 0);
    if (content) {
        char *content_copy = puppet_strdup(content);
        char *line = strtok(content_copy, "\n");
        while (line) {
            size_t field_len = strlen(field);
            if (strncmp(line, field, field_len) == 0 && line[field_len] == '=') {
                char *value = line + field_len + 1;
                /* Remove quotes */
                if (*value == '"') value++;
                char *end = value + strlen(value) - 1;
                if (end >= value && *end == '"') *end = '\0';
                char *result = puppet_strdup(value);
                puppet_free(content_copy);
                puppet_free(content);
                return result;
            }
            line = strtok(NULL, "\n");
        }
        puppet_free(content_copy);
        puppet_free(content);
    }

    /* Fallback: try /etc/os-release for some fields */
    content = read_file_contents("/etc/os-release", 0);
    if (content) {
        char *os_field = NULL;
        if (strcmp(field, "DISTRIB_ID") == 0) {
            os_field = "ID=";
        } else if (strcmp(field, "DISTRIB_RELEASE") == 0) {
            os_field = "VERSION_ID=";
        } else if (strcmp(field, "DISTRIB_CODENAME") == 0) {
            os_field = "VERSION_CODENAME=";
        } else if (strcmp(field, "DISTRIB_DESCRIPTION") == 0) {
            os_field = "PRETTY_NAME=";
        }

        if (os_field) {
            char *content_copy = puppet_strdup(content);
            char *line = strtok(content_copy, "\n");
            size_t os_field_len = strlen(os_field);
            while (line) {
                if (strncmp(line, os_field, os_field_len) == 0) {
                    char *value = line + os_field_len;
                    if (*value == '"') value++;
                    char *end = value + strlen(value) - 1;
                    if (end >= value && *end == '"') *end = '\0';
                    /* For DISTRIB_ID, capitalize (Ubuntu not ubuntu) */
                    char *result = puppet_strdup(value);
                    if (strcmp(field, "DISTRIB_ID") == 0 && result && *result) {
                        result[0] = toupper((unsigned char)result[0]);
                    }
                    puppet_free(content_copy);
                    puppet_free(content);
                    return result;
                }
                line = strtok(NULL, "\n");
            }
            puppet_free(content_copy);
        }
        puppet_free(content);
    }
#else
    (void)field;
#endif
    return NULL;
}

char *facter_lsbdistid(void) {
    return facter_lsb_field("DISTRIB_ID");
}

char *facter_lsbdistrelease(void) {
    return facter_lsb_field("DISTRIB_RELEASE");
}

char *facter_lsbdistcodename(void) {
    return facter_lsb_field("DISTRIB_CODENAME");
}

char *facter_lsbdistdescription(void) {
    return facter_lsb_field("DISTRIB_DESCRIPTION");
}

unsigned long facter_memory_total(void) {
#ifdef __linux__
    struct sysinfo info;
    if (sysinfo(&info) == 0) {
        return info.totalram * info.mem_unit;
    }
#elif defined(__APPLE__)
    int mib[2] = {CTL_HW, HW_MEMSIZE};
    uint64_t memsize;
    size_t len = sizeof(memsize);
    if (sysctl(mib, 2, &memsize, &len, NULL, 0) == 0) {
        return (unsigned long)memsize;
    }
#endif
    return 0;
}

unsigned long facter_memory_free(void) {
#ifdef __linux__
    struct sysinfo info;
    if (sysinfo(&info) == 0) {
        return info.freeram * info.mem_unit;
    }
#elif defined(__APPLE__)
    mach_port_t host_port = mach_host_self();
    vm_size_t page_size;
    vm_statistics64_data_t vm_stat;
    mach_msg_type_number_t host_count = HOST_VM_INFO64_COUNT;

    if (host_page_size(host_port, &page_size) == KERN_SUCCESS &&
        host_statistics64(host_port, HOST_VM_INFO64, (host_info64_t)&vm_stat, &host_count) == KERN_SUCCESS) {
        unsigned long free_mem = (unsigned long)(vm_stat.free_count * page_size);
        unsigned long inactive_mem = (unsigned long)(vm_stat.inactive_count * page_size);
        return free_mem + inactive_mem;
    }
#endif
    return 0;
}

int facter_processor_count(void) {
    long nprocs = sysconf(_SC_NPROCESSORS_ONLN);
    return nprocs > 0 ? (int)nprocs : 1;
}

long facter_uptime_seconds(void) {
#ifdef __linux__
    struct sysinfo info;
    if (sysinfo(&info) == 0) {
        return info.uptime;
    }
#elif defined(__APPLE__)
    struct timeval boottime;
    size_t len = sizeof(boottime);
    int mib[2] = {CTL_KERN, KERN_BOOTTIME};

    if (sysctl(mib, 2, &boottime, &len, NULL, 0) == 0) {
        time_t now = time(NULL);
        return (long)(now - boottime.tv_sec);
    }
#endif
    return 0;
}

bool facter_is_virtual(void) {
    /* Check various indicators of virtualization */

    /* Check /sys/class/dmi/id/product_name */
    char *product = read_file_line("/sys/class/dmi/id/product_name");
    if (product) {
        bool is_virtual = (strstr(product, "VirtualBox") != NULL ||
                          strstr(product, "VMware") != NULL ||
                          strstr(product, "QEMU") != NULL ||
                          strstr(product, "KVM") != NULL ||
                          strstr(product, "Bochs") != NULL ||
                          strstr(product, "HVM") != NULL);
        puppet_free(product);
        if (is_virtual) return true;
    }

    /* Check /sys/hypervisor/type */
    char *hypervisor = read_file_line("/sys/hypervisor/type");
    if (hypervisor) {
        puppet_free(hypervisor);
        return true;
    }

    /* Check for Docker */
    if (access("/.dockerenv", F_OK) == 0) {
        return true;
    }

    /* Check cgroup for container indicators */
    char *cgroup = read_file_contents("/proc/1/cgroup", 0);
    if (cgroup) {
        bool is_container = (strstr(cgroup, "docker") != NULL ||
                            strstr(cgroup, "lxc") != NULL ||
                            strstr(cgroup, "kubepods") != NULL);
        puppet_free(cgroup);
        if (is_container) return true;
    }

    return false;
}

char *facter_virtual(void) {
    /* Check /sys/class/dmi/id/product_name */
    char *product = read_file_line("/sys/class/dmi/id/product_name");
    if (product) {
        if (strstr(product, "VirtualBox")) {
            puppet_free(product);
            return puppet_strdup("virtualbox");
        } else if (strstr(product, "VMware")) {
            puppet_free(product);
            return puppet_strdup("vmware");
        } else if (strstr(product, "QEMU") || strstr(product, "KVM")) {
            puppet_free(product);
            return puppet_strdup("kvm");
        } else if (strstr(product, "Bochs")) {
            puppet_free(product);
            return puppet_strdup("bochs");
        } else if (strstr(product, "Xen") || strstr(product, "HVM")) {
            puppet_free(product);
            return puppet_strdup("xen");
        }
        puppet_free(product);
    }

    /* Check /sys/hypervisor/type */
    char *hypervisor = read_file_line("/sys/hypervisor/type");
    if (hypervisor) {
        char *result = puppet_strdup(trim_string(hypervisor));
        puppet_free(hypervisor);
        return result;
    }

    /* Check for Docker */
    if (access("/.dockerenv", F_OK) == 0) {
        return puppet_strdup("docker");
    }

    /* Check cgroup for container */
    char *cgroup = read_file_contents("/proc/1/cgroup", 0);
    if (cgroup) {
        if (strstr(cgroup, "docker")) {
            puppet_free(cgroup);
            return puppet_strdup("docker");
        } else if (strstr(cgroup, "lxc")) {
            puppet_free(cgroup);
            return puppet_strdup("lxc");
        } else if (strstr(cgroup, "kubepods")) {
            puppet_free(cgroup);
            return puppet_strdup("docker");
        }
        puppet_free(cgroup);
    }

    return puppet_strdup("physical");
}

char *facter_ipaddress(void) {
    struct ifaddrs *ifaddr, *ifa;
    char *result = NULL;

    if (getifaddrs(&ifaddr) == -1) {
        return NULL;
    }

    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL) continue;
        if (ifa->ifa_addr->sa_family != AF_INET) continue;

        /* Skip loopback */
        if (strcmp(ifa->ifa_name, "lo") == 0) continue;

        /* Skip down interfaces */
        if (!(ifa->ifa_flags & IFF_UP)) continue;

        struct sockaddr_in *addr = (struct sockaddr_in *)ifa->ifa_addr;
        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &addr->sin_addr, ip, sizeof(ip));

        /* Skip link-local addresses */
        if (strncmp(ip, "169.254.", 8) == 0) continue;

        result = puppet_strdup(ip);
        break;
    }

    freeifaddrs(ifaddr);
    return result;
}

char *facter_macaddress(void) {
    struct ifaddrs *ifaddr, *ifa;
    char *result = NULL;

    if (getifaddrs(&ifaddr) == -1) {
        return NULL;
    }

    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL) continue;

        /* Skip loopback */
        if (strcmp(ifa->ifa_name, "lo") == 0) continue;

        /* Skip down interfaces */
        if (!(ifa->ifa_flags & IFF_UP)) continue;

#ifdef __linux__
        /* Read MAC from /sys */
        char path[256];
        snprintf(path, sizeof(path), "/sys/class/net/%s/address", ifa->ifa_name);
        char *mac = read_file_line(path);
        if (mac && strcmp(mac, "00:00:00:00:00:00") != 0) {
            result = mac;
            break;
        }
        puppet_free(mac);
#elif defined(__APPLE__)
        /* Get MAC address from link-layer address */
        if (ifa->ifa_addr->sa_family == AF_LINK) {
            struct sockaddr_dl *sdl = (struct sockaddr_dl *)ifa->ifa_addr;
            if (sdl->sdl_alen == 6) {
                unsigned char *mac = (unsigned char *)LLADDR(sdl);
                char mac_str[18];
                snprintf(mac_str, sizeof(mac_str), "%02x:%02x:%02x:%02x:%02x:%02x",
                        mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
                if (strcmp(mac_str, "00:00:00:00:00:00") != 0) {
                    result = puppet_strdup(mac_str);
                    break;
                }
            }
        }
#endif
    }

    freeifaddrs(ifaddr);
    return result;
}

facter_interface_t *facter_interfaces(size_t *count) {
    struct ifaddrs *ifaddr, *ifa;
    facter_interface_t *interfaces = NULL;
    size_t iface_count = 0;

    *count = 0;

    if (getifaddrs(&ifaddr) == -1) {
        return NULL;
    }

    /* Count unique interfaces */
    char seen[MAX_INTERFACES][IFNAMSIZ];
    size_t seen_count = 0;

    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (!ifa->ifa_name) continue;

        bool found = false;
        for (size_t i = 0; i < seen_count; i++) {
            if (strcmp(seen[i], ifa->ifa_name) == 0) {
                found = true;
                break;
            }
        }
        if (!found && seen_count < MAX_INTERFACES) {
            strncpy(seen[seen_count], ifa->ifa_name, IFNAMSIZ - 1);
            seen_count++;
        }
    }

    interfaces = puppet_calloc(seen_count, sizeof(facter_interface_t));
    if (!interfaces) {
        freeifaddrs(ifaddr);
        return NULL;
    }

    /* Fill in interface details */
    for (size_t i = 0; i < seen_count; i++) {
        interfaces[iface_count].name = puppet_strdup(seen[i]);

#ifdef __linux__
        /* Get MAC address from /sys */
        char path[256];
        snprintf(path, sizeof(path), "/sys/class/net/%s/address", seen[i]);
        interfaces[iface_count].mac = read_file_line(path);

        /* Get MTU */
        snprintf(path, sizeof(path), "/sys/class/net/%s/mtu", seen[i]);
        char *mtu_str = read_file_line(path);
        if (mtu_str) {
            interfaces[iface_count].mtu = atoi(mtu_str);
            puppet_free(mtu_str);
        }
#endif

        /* Get UP status and addresses from ifaddrs */
        for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
            if (strcmp(ifa->ifa_name, seen[i]) != 0) continue;

            interfaces[iface_count].up = (ifa->ifa_flags & IFF_UP) != 0;

#ifdef __APPLE__
            /* Get MAC address from link-layer address */
            if (ifa->ifa_addr && ifa->ifa_addr->sa_family == AF_LINK) {
                struct sockaddr_dl *sdl = (struct sockaddr_dl *)ifa->ifa_addr;
                if (sdl->sdl_alen == 6) {
                    unsigned char *mac = (unsigned char *)LLADDR(sdl);
                    char mac_str[18];
                    snprintf(mac_str, sizeof(mac_str), "%02x:%02x:%02x:%02x:%02x:%02x",
                            mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
                    interfaces[iface_count].mac = puppet_strdup(mac_str);
                }
                /* Get MTU on macOS */
                interfaces[iface_count].mtu = sdl->sdl_data[sdl->sdl_nlen];
            }
#endif

            if (ifa->ifa_addr && ifa->ifa_addr->sa_family == AF_INET) {
                struct sockaddr_in *addr = (struct sockaddr_in *)ifa->ifa_addr;
                char ip[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &addr->sin_addr, ip, sizeof(ip));
                interfaces[iface_count].ip = puppet_strdup(ip);

                if (ifa->ifa_netmask) {
                    struct sockaddr_in *mask = (struct sockaddr_in *)ifa->ifa_netmask;
                    char netmask[INET_ADDRSTRLEN];
                    inet_ntop(AF_INET, &mask->sin_addr, netmask, sizeof(netmask));
                    interfaces[iface_count].netmask = puppet_strdup(netmask);
                }
            } else if (ifa->ifa_addr && ifa->ifa_addr->sa_family == AF_INET6) {
                struct sockaddr_in6 *addr6 = (struct sockaddr_in6 *)ifa->ifa_addr;
                char ip6[INET6_ADDRSTRLEN];
                inet_ntop(AF_INET6, &addr6->sin6_addr, ip6, sizeof(ip6));
                if (!interfaces[iface_count].ip6) {
                    interfaces[iface_count].ip6 = puppet_strdup(ip6);
                }
            }
        }

        iface_count++;
    }

    freeifaddrs(ifaddr);
    *count = iface_count;
    return interfaces;
}

void facter_interfaces_free(facter_interface_t *interfaces, size_t count) {
    if (!interfaces) return;

    for (size_t i = 0; i < count; i++) {
        puppet_free(interfaces[i].name);
        puppet_free(interfaces[i].ip);
        puppet_free(interfaces[i].ip6);
        puppet_free(interfaces[i].mac);
        puppet_free(interfaces[i].netmask);
    }
    puppet_free(interfaces);
}

/* ============================================================================
 * Full Collection
 * ============================================================================ */

static void collect_system_facts(facter_ctx_t *ctx) {
    char *hostname = facter_hostname();
    if (hostname) {
        facter_set_string(ctx, "hostname", hostname);
        puppet_free(hostname);
    }

    char *fqdn = facter_fqdn();
    if (fqdn) {
        facter_set_string(ctx, "fqdn", fqdn);
        puppet_free(fqdn);
    }

    char *domain = facter_domain();
    if (domain) {
        facter_set_string(ctx, "domain", domain);
        puppet_free(domain);
    }

    facter_set_integer(ctx, "uptime_seconds", facter_uptime_seconds());

    long uptime = facter_uptime_seconds();
    facter_set_integer(ctx, "uptime_days", uptime / 86400);
    facter_set_integer(ctx, "uptime_hours", uptime / 3600);
}

static void collect_kernel_facts(facter_ctx_t *ctx) {
    char *kernel = facter_kernel();
    if (kernel) {
        facter_set_string(ctx, "kernel", kernel);
        puppet_free(kernel);
    }

    char *kernelrelease = facter_kernelrelease();
    if (kernelrelease) {
        facter_set_string(ctx, "kernelrelease", kernelrelease);
        puppet_free(kernelrelease);
    }

    char *kernelversion = facter_kernelversion();
    if (kernelversion) {
        facter_set_string(ctx, "kernelversion", kernelversion);
        puppet_free(kernelversion);
    }
}

static void collect_os_facts(facter_ctx_t *ctx) {
    /* Create nested 'os' hash for $facts['os']['family'] style access */
    facter_value_t *os_hash = facter_value_hash_create();
    facter_value_t *os_release_hash = facter_value_hash_create();

    char *os_name = facter_os_name();
    if (os_name) {
        /* Legacy flat key */
        facter_set_string(ctx, "operatingsystem", os_name);
        /* Nested hash */
        facter_hash_set_string(os_hash, "name", os_name);
        puppet_free(os_name);
    }

    char *os_family = facter_os_family();
    if (os_family) {
        /* Legacy flat key */
        facter_set_string(ctx, "osfamily", os_family);
        /* Nested hash */
        facter_hash_set_string(os_hash, "family", os_family);
        puppet_free(os_family);
    }

    char *os_release = facter_os_release();
    if (os_release) {
        /* Legacy flat key */
        facter_set_string(ctx, "operatingsystemrelease", os_release);
        /* Nested release hash */
        facter_hash_set_string(os_release_hash, "full", os_release);

        /* Extract major version */
        char *major = puppet_strdup(os_release);
        char *dot = strchr(major, '.');
        if (dot) *dot = '\0';
        facter_hash_set_string(os_release_hash, "major", major);
        puppet_free(major);

        puppet_free(os_release);
    }

    char *arch = facter_architecture();
    if (arch) {
        /* Legacy flat keys */
        facter_set_string(ctx, "architecture", arch);
        facter_set_string(ctx, "hardwaremodel", arch);
        /* Nested hash */
        facter_hash_set_string(os_hash, "architecture", arch);
        facter_hash_set_string(os_hash, "hardware", arch);
        puppet_free(arch);
    }

    /* Add release sub-hash to os hash */
    facter_hash_set_hash(os_hash, "release", os_release_hash);

    /* Set the nested 'os' fact */
    facter_set(ctx, "os", os_hash);
}

static void collect_lsb_facts(facter_ctx_t *ctx) {
    /* LSB facts for legacy Puppet manifest compatibility */
    char *lsbdistid = facter_lsbdistid();
    if (lsbdistid) {
        facter_set_string(ctx, "lsbdistid", lsbdistid);
        puppet_free(lsbdistid);
    }

    char *lsbdistrelease = facter_lsbdistrelease();
    if (lsbdistrelease) {
        facter_set_string(ctx, "lsbdistrelease", lsbdistrelease);
        puppet_free(lsbdistrelease);
    }

    char *lsbdistcodename = facter_lsbdistcodename();
    if (lsbdistcodename) {
        facter_set_string(ctx, "lsbdistcodename", lsbdistcodename);
        puppet_free(lsbdistcodename);
    }

    char *lsbdistdescription = facter_lsbdistdescription();
    if (lsbdistdescription) {
        facter_set_string(ctx, "lsbdistdescription", lsbdistdescription);
        puppet_free(lsbdistdescription);
    }
}

static void collect_memory_facts(facter_ctx_t *ctx) {
    unsigned long total = facter_memory_total();
    unsigned long free_mem = facter_memory_free();

    /* Legacy flat keys */
    facter_set_integer(ctx, "memorysize_mb", total / (1024 * 1024));
    facter_set_integer(ctx, "memoryfree_mb", free_mem / (1024 * 1024));

    /* Create nested 'memory' hash for $facts['memory']['system']['total_bytes'] */
    facter_value_t *memory_hash = facter_value_hash_create();
    facter_value_t *system_hash = facter_value_hash_create();

    facter_hash_set_integer(system_hash, "total_bytes", total);
    facter_hash_set_integer(system_hash, "available_bytes", free_mem);

    /* Human-readable format */
    char buf[32];
    if (total >= 1024UL * 1024 * 1024) {
        snprintf(buf, sizeof(buf), "%.2f GiB", (double)total / (1024.0 * 1024 * 1024));
    } else {
        snprintf(buf, sizeof(buf), "%.2f MiB", (double)total / (1024.0 * 1024));
    }
    facter_hash_set_string(system_hash, "total", buf);

    facter_hash_set_hash(memory_hash, "system", system_hash);
    facter_set(ctx, "memory", memory_hash);
}

static void collect_processor_facts(facter_ctx_t *ctx) {
    int count = facter_processor_count();
    facter_set_integer(ctx, "processorcount", count);
    facter_set_integer(ctx, "processors.count", count);

#ifdef __linux__
    /* Get processor model from /proc/cpuinfo */
    char *cpuinfo = read_file_contents("/proc/cpuinfo", 0);
    if (cpuinfo) {
        char *line = strtok(cpuinfo, "\n");
        while (line) {
            if (strncmp(line, "model name", 10) == 0) {
                char *colon = strchr(line, ':');
                if (colon) {
                    char *model = trim_string(colon + 1);
                    facter_set_string(ctx, "processor0", model);
                    facter_set_string(ctx, "processors.models.0", model);
                    break;
                }
            }
            line = strtok(NULL, "\n");
        }
        puppet_free(cpuinfo);
    }
#elif defined(__APPLE__)
    /* Get processor model from sysctl */
    char cpu_brand[256];
    size_t len = sizeof(cpu_brand);
    if (sysctlbyname("machdep.cpu.brand_string", &cpu_brand, &len, NULL, 0) == 0) {
        facter_set_string(ctx, "processor0", cpu_brand);
        facter_set_string(ctx, "processors.models.0", cpu_brand);
    }
#endif
}

static void collect_network_facts(facter_ctx_t *ctx) {
    /* Create nested 'networking' hash for $facts['networking']['ip'] */
    facter_value_t *networking_hash = facter_value_hash_create();
    facter_value_t *interfaces_hash = facter_value_hash_create();

    char *ip = facter_ipaddress();
    if (ip) {
        /* Legacy flat key */
        facter_set_string(ctx, "ipaddress", ip);
        /* Nested hash */
        facter_hash_set_string(networking_hash, "ip", ip);
        puppet_free(ip);
    }

    char *mac = facter_macaddress();
    if (mac) {
        /* Legacy flat key */
        facter_set_string(ctx, "macaddress", mac);
        /* Nested hash */
        facter_hash_set_string(networking_hash, "mac", mac);
        puppet_free(mac);
    }

    /* Collect interface details */
    size_t iface_count;
    facter_interface_t *interfaces = facter_interfaces(&iface_count);
    if (interfaces) {
        for (size_t i = 0; i < iface_count; i++) {
            facter_value_t *iface_hash = facter_value_hash_create();

            if (interfaces[i].ip) {
                facter_hash_set_string(iface_hash, "ip", interfaces[i].ip);
                /* Also add legacy flat keys for primary interface */
                char key[128];
                snprintf(key, sizeof(key), "ipaddress_%s", interfaces[i].name);
                facter_set_string(ctx, key, interfaces[i].ip);
            }
            if (interfaces[i].mac) {
                facter_hash_set_string(iface_hash, "mac", interfaces[i].mac);
            }
            if (interfaces[i].netmask) {
                facter_hash_set_string(iface_hash, "netmask", interfaces[i].netmask);
            }
            facter_hash_set_integer(iface_hash, "mtu", interfaces[i].mtu);

            facter_hash_set_hash(interfaces_hash, interfaces[i].name, iface_hash);
        }
        facter_interfaces_free(interfaces, iface_count);
    }

    facter_hash_set_hash(networking_hash, "interfaces", interfaces_hash);
    facter_set(ctx, "networking", networking_hash);
}

static void collect_virtual_facts(facter_ctx_t *ctx) {
    bool is_virtual = facter_is_virtual();
    facter_set_boolean(ctx, "is_virtual", is_virtual);

    char *virtual = facter_virtual();
    if (virtual) {
        facter_set_string(ctx, "virtual", virtual);
        puppet_free(virtual);
    }
}

int facter_collect(facter_ctx_t *ctx) {
    if (!ctx) return -1;

    collect_system_facts(ctx);
    collect_kernel_facts(ctx);
    collect_os_facts(ctx);
    collect_lsb_facts(ctx);
    collect_memory_facts(ctx);
    collect_processor_facts(ctx);
    collect_network_facts(ctx);
    collect_virtual_facts(ctx);

    ctx->collected = true;
    return 0;
}

int facter_collect_category(facter_ctx_t *ctx, const char *category) {
    if (!ctx || !category) return -1;

    if (strcmp(category, "system") == 0) {
        collect_system_facts(ctx);
    } else if (strcmp(category, "kernel") == 0) {
        collect_kernel_facts(ctx);
    } else if (strcmp(category, "os") == 0) {
        collect_os_facts(ctx);
    } else if (strcmp(category, "lsb") == 0) {
        collect_lsb_facts(ctx);
    } else if (strcmp(category, "memory") == 0) {
        collect_memory_facts(ctx);
    } else if (strcmp(category, "processor") == 0 || strcmp(category, "processors") == 0) {
        collect_processor_facts(ctx);
    } else if (strcmp(category, "networking") == 0 || strcmp(category, "network") == 0) {
        collect_network_facts(ctx);
    } else if (strcmp(category, "virtual") == 0) {
        collect_virtual_facts(ctx);
    } else {
        return -1;
    }

    return 0;
}

/* ============================================================================
 * Fact Access
 * ============================================================================ */

const facter_value_t *facter_get(facter_ctx_t *ctx, const char *name) {
    if (!ctx || !name) return NULL;

    for (size_t i = 0; i < ctx->fact_count; i++) {
        if (strcmp(ctx->facts[i].name, name) == 0) {
            return ctx->facts[i].value;
        }
    }
    return NULL;
}

const char *facter_get_string(facter_ctx_t *ctx, const char *name) {
    const facter_value_t *v = facter_get(ctx, name);
    if (v && v->type == FACTER_VALUE_STRING) {
        return v->data.string_val;
    }
    return NULL;
}

int facter_get_integer(facter_ctx_t *ctx, const char *name, long *value) {
    const facter_value_t *v = facter_get(ctx, name);
    if (v && v->type == FACTER_VALUE_INTEGER) {
        *value = v->data.integer_val;
        return 0;
    }
    return -1;
}

int facter_get_boolean(facter_ctx_t *ctx, const char *name, bool *value) {
    const facter_value_t *v = facter_get(ctx, name);
    if (v && v->type == FACTER_VALUE_BOOLEAN) {
        *value = v->data.boolean_val;
        return 0;
    }
    return -1;
}

bool facter_has(facter_ctx_t *ctx, const char *name) {
    return facter_get(ctx, name) != NULL;
}

const char **facter_list(facter_ctx_t *ctx, size_t *count) {
    if (!ctx || !count) return NULL;
    *count = ctx->fact_count;
    return ctx->fact_names;
}

/* ============================================================================
 * Output Formats
 * ============================================================================ */

/* Recursive helper to output a facter_value_t as JSON */
static void facter_value_to_json(json_buffer_t *buf, facter_value_t *value, int indent) {
    if (!value) {
        json_buffer_append(buf, "null");
        return;
    }

    switch (value->type) {
        case FACTER_VALUE_STRING:
            json_buffer_append_string(buf, value->data.string_val);
            break;
        case FACTER_VALUE_INTEGER:
            json_buffer_append_int(buf, value->data.integer_val);
            break;
        case FACTER_VALUE_FLOAT:
            json_buffer_append_double(buf, value->data.float_val);
            break;
        case FACTER_VALUE_BOOLEAN:
            json_buffer_append_bool(buf, value->data.boolean_val);
            break;
        case FACTER_VALUE_HASH:
            json_buffer_append(buf, "{\n");
            for (size_t i = 0; i < value->data.hash_val.count; i++) {
                /* Indent */
                for (int j = 0; j < indent + 1; j++) {
                    json_buffer_append(buf, "  ");
                }
                json_buffer_append_string(buf, value->data.hash_val.keys[i]);
                json_buffer_append(buf, ": ");
                facter_value_to_json(buf, value->data.hash_val.values[i], indent + 1);
                if (i < value->data.hash_val.count - 1) {
                    json_buffer_append(buf, ",");
                }
                json_buffer_append(buf, "\n");
            }
            /* Closing brace indent */
            for (int j = 0; j < indent; j++) {
                json_buffer_append(buf, "  ");
            }
            json_buffer_append(buf, "}");
            break;
        case FACTER_VALUE_ARRAY:
            json_buffer_append(buf, "[\n");
            for (size_t i = 0; i < value->data.array_val.count; i++) {
                /* Indent */
                for (int j = 0; j < indent + 1; j++) {
                    json_buffer_append(buf, "  ");
                }
                facter_value_to_json(buf, value->data.array_val.items[i], indent + 1);
                if (i < value->data.array_val.count - 1) {
                    json_buffer_append(buf, ",");
                }
                json_buffer_append(buf, "\n");
            }
            /* Closing bracket indent */
            for (int j = 0; j < indent; j++) {
                json_buffer_append(buf, "  ");
            }
            json_buffer_append(buf, "]");
            break;
        default:
            json_buffer_append_null(buf);
            break;
    }
}

char *facter_to_json(facter_ctx_t *ctx) {
    if (!ctx) return NULL;

    json_buffer_t *buf = json_buffer_create();
    if (!buf) return NULL;

    json_buffer_append(buf, "{\n");

    for (size_t i = 0; i < ctx->fact_count; i++) {
        fact_entry_t *f = &ctx->facts[i];

        json_buffer_append(buf, "  ");
        json_buffer_append_string(buf, f->name);
        json_buffer_append(buf, ": ");

        facter_value_to_json(buf, f->value, 1);

        if (i < ctx->fact_count - 1) {
            json_buffer_append(buf, ",");
        }
        json_buffer_append(buf, "\n");
    }

    json_buffer_append(buf, "}\n");

    char *result = puppet_strdup(buf->data);
    json_buffer_destroy(buf);

    return result;
}

char *facter_to_yaml(facter_ctx_t *ctx) {
    if (!ctx) return NULL;

    size_t buf_size = 16384;
    char *buf = puppet_malloc(buf_size);
    if (!buf) return NULL;

    size_t pos = 0;
    pos += snprintf(buf + pos, buf_size - pos, "---\n");

    for (size_t i = 0; i < ctx->fact_count; i++) {
        fact_entry_t *f = &ctx->facts[i];

        switch (f->value->type) {
            case FACTER_VALUE_STRING:
                pos += snprintf(buf + pos, buf_size - pos, "%s: \"%s\"\n",
                               f->name, f->value->data.string_val);
                break;
            case FACTER_VALUE_INTEGER:
                pos += snprintf(buf + pos, buf_size - pos, "%s: %ld\n",
                               f->name, f->value->data.integer_val);
                break;
            case FACTER_VALUE_FLOAT:
                pos += snprintf(buf + pos, buf_size - pos, "%s: %g\n",
                               f->name, f->value->data.float_val);
                break;
            case FACTER_VALUE_BOOLEAN:
                pos += snprintf(buf + pos, buf_size - pos, "%s: %s\n",
                               f->name, f->value->data.boolean_val ? "true" : "false");
                break;
            default:
                pos += snprintf(buf + pos, buf_size - pos, "%s: null\n", f->name);
                break;
        }
    }

    return buf;
}

char *facter_to_text(facter_ctx_t *ctx) {
    if (!ctx) return NULL;

    size_t buf_size = 16384;
    char *buf = puppet_malloc(buf_size);
    if (!buf) return NULL;

    size_t pos = 0;

    for (size_t i = 0; i < ctx->fact_count; i++) {
        fact_entry_t *f = &ctx->facts[i];

        switch (f->value->type) {
            case FACTER_VALUE_STRING:
                pos += snprintf(buf + pos, buf_size - pos, "%s => %s\n",
                               f->name, f->value->data.string_val);
                break;
            case FACTER_VALUE_INTEGER:
                pos += snprintf(buf + pos, buf_size - pos, "%s => %ld\n",
                               f->name, f->value->data.integer_val);
                break;
            case FACTER_VALUE_FLOAT:
                pos += snprintf(buf + pos, buf_size - pos, "%s => %g\n",
                               f->name, f->value->data.float_val);
                break;
            case FACTER_VALUE_BOOLEAN:
                pos += snprintf(buf + pos, buf_size - pos, "%s => %s\n",
                               f->name, f->value->data.boolean_val ? "true" : "false");
                break;
            default:
                pos += snprintf(buf + pos, buf_size - pos, "%s => \n", f->name);
                break;
        }
    }

    return buf;
}

/* ============================================================================
 * Library Info
 * ============================================================================ */

const char *facter_version(void) {
    return FACTER_VERSION;
}
