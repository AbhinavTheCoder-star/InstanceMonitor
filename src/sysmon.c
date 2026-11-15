#include "sysmon.h"
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <inttypes.h>
#include <math.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/statvfs.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <unistd.h>

/* Keep source paths separate from display text. Only fixed suffixes and sysfs
 * names are joined here; fixture roots are never passed to a shell. */
static void path_for(const Sysmon *s, const char *suffix, char *out, size_t capacity)
{
    snprintf(out, capacity, "%s%s", s->root, suffix);
}

static bool read_line(const char *path, char *out, size_t capacity)
{
    out[0] = 0;
    FILE *file = fopen(path, "r");
    if (!file) return false;
    bool ok = fgets(out, (int)capacity, file) != NULL && !ferror(file);
    fclose(file);
    out[strcspn(out, "\r\n")] = 0;
    sanitize(out);
    return ok && out[0];
}

static bool read_number(const char *path, long long *value)
{
    char text[128], *end;
    if (!read_line(path, text, sizeof text)) return false;
    errno = 0;
    *value = strtoll(text, &end, 10);
    if (errno || end == text) return false;
    while (isspace((unsigned char)*end)) ++end;
    return !errno && end != text && !*end;
}

/* Append with a fixed budget, retaining a terminating NUL on every path. */
static void append(char *out, size_t capacity, const char *format, ...)
{
    size_t used = strlen(out);
    if (used >= capacity) return;
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(out + used, capacity - used, format, arguments);
    va_end(arguments);
}

static void operating_system(Sysmon *s)
{
    char path[2048], line[512];
    snprintf(s->os, sizeof s->os, "Linux (distribution unavailable)");
    path_for(s, "/etc/os-release", path, sizeof path);
    FILE *file = fopen(path, "r");
    if (!file) {
        path_for(s, "/usr/lib/os-release", path, sizeof path);
        file = fopen(path, "r");
    }
    if (file) {
        for (unsigned count = 0; count < 128 && fgets(line, sizeof line, file); ++count) {
            if (strncmp(line, "PRETTY_NAME=", 12)) continue;
            char *value = line + 12;
            value[strcspn(value, "\r\n")] = 0;
            size_t length = strlen(value);
            if (length >= 2 && (value[0] == '"' || value[0] == '\'') && value[length - 1] == value[0]) {
                value[length - 1] = 0; ++value;
            }
            /* os-release is data, never sourced as shell code. */
            if (*value) snprintf(s->os, sizeof s->os, "%.255s", value);
            break;
        }
        fclose(file);
    }
    sanitize(s->os);
    struct utsname info;
    snprintf(s->kernel, sizeof s->kernel, "unavailable");
    snprintf(s->hostname, sizeof s->hostname, "unavailable");
    if (!uname(&info)) {
        snprintf(s->kernel, sizeof s->kernel, "%.120s %.120s", info.release, info.machine);
        snprintf(s->hostname, sizeof s->hostname, "%s", info.nodename);
        sanitize(s->kernel); sanitize(s->hostname);
    }
}

static void cpu_information(Sysmon *s)
{
    char path[2048], text[512];
    snprintf(s->cpu, sizeof s->cpu, "unavailable");
    path_for(s, "/proc/cpuinfo", path, sizeof path);
    FILE *file = fopen(path, "r");
    if (file) {
        for (unsigned lines = 0; lines < 16384 && fgets(text, sizeof text, file); ++lines) {
            if (strncmp(text, "model name", 10) && strncmp(text, "Hardware", 8) && strncmp(text, "Processor", 9)) continue;
            char *value = strchr(text, ':');
            if (!value) continue;
            ++value; while (isspace((unsigned char)*value)) ++value;
            value[strcspn(value, "\r\n")] = 0;
            if (*value) { snprintf(s->cpu, sizeof s->cpu, "%.255s", value); break; }
        }
        fclose(file);
    }
    sanitize(s->cpu);
    /* Count unique (socket, core) pairs among online CPUs. core_id alone is
     * insufficient on multi-socket systems. Missing topology is not guessed. */
    struct { long long package, core; } cores[4096];
    size_t count = 0, threads = 0;
    bool complete = true;
    path_for(s, "/sys/devices/system/cpu/cpu[0-9]*", path, sizeof path);
    glob_t matches = {0};
    if (!glob(path, 0, NULL, &matches)) {
        for (size_t i = 0; i < matches.gl_pathc; ++i) {
            if (i >= 4096) { complete = false; break; }
            long long online, package, core;
            snprintf(path, sizeof path, "%s/online", matches.gl_pathv[i]);
            if (read_number(path, &online) && online == 0) continue;
            ++threads;
            snprintf(path, sizeof path, "%s/topology/physical_package_id", matches.gl_pathv[i]);
            if (!read_number(path, &package) || package < 0) { complete = false; continue; }
            snprintf(path, sizeof path, "%s/topology/core_id", matches.gl_pathv[i]);
            if (!read_number(path, &core) || core < 0) { complete = false; continue; }
            size_t j;
            for (j = 0; j < count; ++j) if (cores[j].package == package && cores[j].core == core) break;
            if (j == count) { cores[count].package = package; cores[count++].core = core; }
        }
    }
    globfree(&matches);
    if (threads && complete) snprintf(s->topology, sizeof s->topology, "%zu cores / %zu online threads", count, threads);
    else {
        long online = sysconf(_SC_NPROCESSORS_ONLN);
        if (online > 0) snprintf(s->topology, sizeof s->topology, "cores unavailable / %ld online threads", online);
        else snprintf(s->topology, sizeof s->topology, "unavailable");
    }
}

static void gpu_information(Sysmon *s)
{
    char path[2048], vendor[64], device[64];
    path_for(s, "/sys/class/drm/card[0-9]*", path, sizeof path);
    glob_t matches = {0};
    if (!glob(path, 0, NULL, &matches)) {
        for (size_t i = 0; i < matches.gl_pathc && i < 32; ++i) {
            const char *card = strrchr(matches.gl_pathv[i], '/') + 1;
            const char *number = card + 4;
            if (strspn(number, "0123456789") != strlen(number)) continue; /* Skip connectors. */
            snprintf(path, sizeof path, "%s/device/vendor", matches.gl_pathv[i]);
            bool have_vendor = read_line(path, vendor, sizeof vendor);
            snprintf(path, sizeof path, "%s/device/device", matches.gl_pathv[i]);
            bool have_device = read_line(path, device, sizeof device);
            if (!have_vendor || !have_device) continue;
            const char *name = !strcmp(vendor, "0x8086") ? "Intel" : !strcmp(vendor, "0x10de") ? "NVIDIA" :
                               !strcmp(vendor, "0x1002") ? "AMD" : "GPU";
            append(s->gpu, sizeof s->gpu, "%s%s: %s (%s:%s)", s->gpu[0] ? "; " : "", card, name, vendor, device);
        }
    }
    globfree(&matches);
    if (!s->gpu[0]) snprintf(s->gpu, sizeof s->gpu, "unavailable (no readable DRM device IDs)");
}

static void storage_information(Sysmon *s)
{
    char path[2048];
    path_for(s, "/sys/block/*", path, sizeof path);
    glob_t matches = {0};
    if (!glob(path, 0, NULL, &matches)) {
        unsigned displayed = 0;
        for (size_t i = 0; i < matches.gl_pathc && i < 256; ++i) {
            snprintf(path, sizeof path, "%s/device", matches.gl_pathv[i]);
            if (access(path, F_OK)) continue; /* Exclude loop, RAM and virtual mapper devices. */
            snprintf(path, sizeof path, "%s/size", matches.gl_pathv[i]);
            long long sectors;
            if (!read_number(path, &sectors) || sectors <= 0) continue;
            if (displayed++ == 8) { append(s->storage, sizeof s->storage, "; ..."); break; }
            const char *name = strrchr(matches.gl_pathv[i], '/') + 1;
            /* Linux block size counters always use 512-byte sectors. */
            append(s->storage, sizeof s->storage, "%s%s %.1f GiB", s->storage[0] ? "; " : "", name, (double)sectors / 2097152.0);
        }
    }
    globfree(&matches);
    if (!s->storage[0]) snprintf(s->storage, sizeof s->storage, "physical devices unavailable (root filesystem below)");
}

static bool cpu_sensor(const char *name)
{
    return !strcmp(name, "coretemp") || !strcmp(name, "k10temp") || !strcmp(name, "zenpower") ||
           !strcmp(name, "cpu_thermal") || !strcmp(name, "soc_thermal") || !strcmp(name, "x86_pkg_temp") ||
           !strncmp(name, "cpu-thermal", 11);
}

static void temperature(Sysmon *s)
{
    char pattern[2048], path[2048], name[128];
    bool found = false;
    long long maximum = -20001;
    /* Accept only known CPU sensors, not arbitrary motherboard/GPU readings.
     * Report the maximum across readable package/core inputs, in millidegrees. */
    for (unsigned source = 0; source < 2; ++source) {
        path_for(s, source ? "/sys/class/thermal/thermal_zone*" : "/sys/class/hwmon/hwmon*", pattern, sizeof pattern);
        glob_t devices = {0};
        if (!glob(pattern, 0, NULL, &devices)) {
            for (size_t i = 0; i < devices.gl_pathc && i < 128; ++i) {
                snprintf(path, sizeof path, "%s/%s", devices.gl_pathv[i], source ? "type" : "name");
                if (!read_line(path, name, sizeof name) || !cpu_sensor(name)) continue;
                snprintf(pattern, sizeof pattern, "%s/%s", devices.gl_pathv[i], source ? "temp" : "temp[0-9]*_input");
                glob_t sensors = {0};
                if (!glob(pattern, 0, NULL, &sensors)) {
                    for (size_t j = 0; j < sensors.gl_pathc && j < 256; ++j) {
                        long long value;
                        if (read_number(sensors.gl_pathv[j], &value) && value >= -20000 && value <= 150000 && value > maximum) {
                            maximum = value; found = true;
                        }
                    }
                }
                globfree(&sensors);
            }
        }
        globfree(&devices);
    }
    if (found) snprintf(s->temperature, sizeof s->temperature, "%.1f C (maximum readable CPU sensor)", (double)maximum / 1000);
    else snprintf(s->temperature, sizeof s->temperature, "unavailable (no readable CPU sensor)");
}

void sysmon_init(Sysmon *s, const char *root)
{
    *s = (Sysmon){.pipe_fd = -1};
    snprintf(s->root, sizeof s->root, "%s", root);
    operating_system(s); cpu_information(s); gpu_information(s); storage_information(s);
    snprintf(s->failed, sizeof s->failed, "not checked yet");
}

/* systemd has no stable proc/sys interface for failed units. Query its optional
 * CLI directly, without a shell or D-Bus dependency. Its output is bounded and
 * nonblocking; a slow/unreachable manager cannot freeze terminal input. */
static void start_services(Sysmon *s, double now)
{
    s->services_at = now;
    char path[2048];
    path_for(s, "/run/systemd/system", path, sizeof path);
    if (access(path, F_OK)) {
        snprintf(s->failed, sizeof s->failed, "unavailable (systemd is not running)"); return;
    }
    int descriptors[2];
    if (pipe2(descriptors, O_CLOEXEC)) {
        snprintf(s->failed, sizeof s->failed, "unavailable (cannot create query pipe)"); return;
    }
    s->child = fork();
    if (s->child == 0) {
        int null = open("/dev/null", O_RDWR);
        if (null < 0 || dup2(null, STDIN_FILENO) < 0 || dup2(null, STDERR_FILENO) < 0 ||
            dup2(descriptors[1], STDOUT_FILENO) < 0) _exit(127);
        if (null > STDERR_FILENO) close(null);
        close(descriptors[0]); close(descriptors[1]);
        setenv("LC_ALL", "C", 1);
        execlp("systemctl", "systemctl", "--system", "--failed", "--type=service", "--no-legend", "--plain", "--no-pager", (char *)NULL);
        _exit(127);
    }
    close(descriptors[1]);
    if (s->child < 0) {
        close(descriptors[0]); s->child = 0;
        snprintf(s->failed, sizeof s->failed, "unavailable (cannot start systemctl)"); return;
    }
    if (fcntl(descriptors[0], F_SETFL, O_NONBLOCK) < 0) {
        close(descriptors[0]); kill(s->child, SIGKILL);
        while (waitpid(s->child, NULL, 0) < 0 && errno == EINTR) {}
        s->child = 0;
        snprintf(s->failed, sizeof s->failed, "unavailable (cannot poll systemctl)"); return;
    }
    s->pipe_fd = descriptors[0]; s->child_started = now;
    s->output_length = 0; s->service_output[0] = 0;
    s->output_truncated = false; s->timed_out = false;
    snprintf(s->failed, sizeof s->failed, "checking system services...");
}

static void drain_services(Sysmon *s)
{
    /* Drain at most 8 KiB per frame, even if a broken command floods stdout. */
    char buffer[1024];
    for (unsigned i = 0; i < 8; ++i) {
        ssize_t count = read(s->pipe_fd, buffer, sizeof buffer);
        if (count <= 0) break;
        for (ssize_t j = 0; j < count; ++j) {
            unsigned char ch = (unsigned char)buffer[j];
            if (s->output_length + 1 >= sizeof s->service_output) { s->output_truncated = true; continue; }
            s->service_output[s->output_length++] = ch == '\n' || (ch >= 32 && ch < 127) ? (char)ch : '?';
        }
        s->service_output[s->output_length] = 0;
    }
}

void sysmon_poll(Sysmon *s, double now)
{
    if (!s->child) return;
    drain_services(s);
    if (now - s->child_started >= 2 && !s->timed_out) { kill(s->child, SIGKILL); s->timed_out = true; }
    int status;
    pid_t done = waitpid(s->child, &status, WNOHANG);
    if (done == 0 || (done < 0 && errno == EINTR)) return;
    /* The child may write its last bytes between the first drain and waitpid. */
    drain_services(s);
    if (s->timed_out) snprintf(s->failed, sizeof s->failed, "unavailable (systemctl timed out)");
    else if (done < 0 || !WIFEXITED(status) || WEXITSTATUS(status) != 0)
        snprintf(s->failed, sizeof s->failed, "unavailable (systemctl missing, restricted or manager unreachable)");
    else if (s->output_truncated) snprintf(s->failed, sizeof s->failed, "%.4000s\n[truncated service list]", s->service_output);
    else if (s->output_length) snprintf(s->failed, sizeof s->failed, "%s", s->service_output);
    else snprintf(s->failed, sizeof s->failed, "none");
    close(s->pipe_fd); s->pipe_fd = -1; s->child = 0;
}

void sysmon_refresh(Sysmon *s, double now)
{
    if (!s->initialized || now - s->refreshed_at >= 5) {
        temperature(s);
        char path[2048], text[256];
        path_for(s, "/proc/loadavg", path, sizeof path);
        double one, five, fifteen;
        if (read_line(path, text, sizeof text) && sscanf(text, "%lf %lf %lf", &one, &five, &fifteen) == 3 &&
            isfinite(one) && isfinite(five) && isfinite(fifteen) && one >= 0 && five >= 0 && fifteen >= 0)
            snprintf(s->load, sizeof s->load, "%.2f / %.2f / %.2f (1 / 5 / 15 min)", one, five, fifteen);
        else snprintf(s->load, sizeof s->load, "unavailable");
        struct statvfs disk;
        path_for(s, "/", path, sizeof path);
        if (!statvfs(path, &disk) && disk.f_blocks && disk.f_frsize && disk.f_bfree <= disk.f_blocks && disk.f_bavail <= disk.f_blocks) {
            double total = (double)disk.f_blocks * (double)disk.f_frsize;
            double used = (double)(disk.f_blocks - disk.f_bfree) * (double)disk.f_frsize;
            double available = (double)disk.f_bavail * (double)disk.f_frsize;
            snprintf(s->disk, sizeof s->disk, "%.1f / %.1f GiB used (%.1f%%); %.1f GiB available", used / 1073741824,
                     total / 1073741824, used / total * 100, available / 1073741824);
        } else snprintf(s->disk, sizeof s->disk, "unavailable");
        s->refreshed_at = now; s->initialized = true;
    }
    if (!s->child && (s->services_at == 0 || now - s->services_at >= 30)) start_services(s, now);
}

void sysmon_format(const Sysmon *s, const Snapshot *snapshot, char *out, size_t capacity)
{
    uint64_t available = snapshot->memory_available <= snapshot->memory_total ? snapshot->memory_available : snapshot->memory_total;
    uint64_t used = snapshot->memory_total - available;
    double percent = snapshot->memory_total ? (double)used / (double)snapshot->memory_total * 100 : 0;
    unsigned long long minutes = (unsigned long long)(snapshot->uptime / 60);
    snprintf(out, capacity,
             "System Information\nOS: %s\nKernel: %s\nHostname: %s\nUptime: %llu days %llu hours %llu minutes\n"
             "CPU: %s\nTopology: %s\nGPU: %s\nTotal RAM: %.2f GiB\nStorage: %s\n\n"
             "Diagnostics\nCPU temperature: %s\nRAM: %.2f / %.2f GiB used (%.1f%%)\nDisk (/): %s\nLoad: %s\n"
             "Failed systemd services (system scope, cached up to 30s):\n%s\n",
             s->os, s->kernel, s->hostname, minutes / 1440, (minutes / 60) % 24, minutes % 60,
             s->cpu, s->topology, s->gpu, (double)snapshot->memory_total / 1073741824, s->storage,
             s->temperature, (double)used / 1073741824, (double)snapshot->memory_total / 1073741824, percent,
             s->disk, s->load, s->failed);
}

void sysmon_destroy(Sysmon *s)
{
    if (s->pipe_fd >= 0) close(s->pipe_fd);
    if (s->child > 0) {
        kill(s->child, SIGKILL);
        while (waitpid(s->child, NULL, 0) < 0 && errno == EINTR) {}
    }
    s->pipe_fd = -1; s->child = 0;
}
