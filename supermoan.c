// To compile: gcc -Wall -g -lm -pthread -o supermoan supermoan.c
// Run with options:
//   --list-devices (-l): List available input devices
//   --input (-i) <device>: Specify input device path
//   --debug (-d): Enable debug output
//   --no-sound (-n): Don't play sound files (for testing)
//   --version (-v): Display version information
//   --sound-dir (-s): Specify custom folder containing .wav files

#define SUPERMOAN_VERSION "1.0.0"
#define SUPERMOAN_COPYRIGHT "Copyright (C) 2025"

#define _DEFAULT_SOURCE
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <linux/input.h>
#include <dirent.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <signal.h>
#include <stdbool.h>
#include <pthread.h>
#include <getopt.h>
#include <errno.h>

#define NUM_INTENSITY_LEVELS 10
#define DEV_INPUT_PATH "/dev/input"
#define EVENT_PREFIX "event"
#define DEVICE_PATH_MAX 300
#define DEFAULT_SOUND_DIR "moans"

#define DEFAULT_MIN_THRESHOLD 1.0
#define DEFAULT_MAX_THRESHOLD 100.0
#define DEFAULT_LOG_BASE 2.0

static const char *sound_directory = DEFAULT_SOUND_DIR;
static char sound_path_buffer[PATH_MAX];
static double min_movement_threshold = DEFAULT_MIN_THRESHOLD;
static double max_movement_threshold = DEFAULT_MAX_THRESHOLD;
static double log_base = DEFAULT_LOG_BASE;
static volatile bool running = true;
static bool no_sound = false;

struct debug_stats {
    long intensity_counts[NUM_INTENSITY_LEVELS + 1];
    long total_movements;
    double last_raw_movement;
    double last_scaled_value;
    bool enabled;
};

static volatile int current_intensity = 0;
static volatile bool is_playing = false;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
static struct debug_stats debug = {0};

void list_input_devices(void);
void *sound_player_thread(void *unused);
void monitor_device(const char *device_path);
static inline int calculate_intensity(int dx, int dy);
void play_sound_file(int intensity);
void print_usage(const char *program_name);
void print_debug_stats(void);
void handle_signal(int sig);
bool validate_sound_directory(const char *dir_path);
void print_version(void);

void print_version(void) {
    printf("supermoan version %s\n", SUPERMOAN_VERSION);
    printf("%s\n", SUPERMOAN_COPYRIGHT);
    printf("A Linux mouse movement to sound converter\n");
}

void print_usage(const char *program_name) {
    printf("Usage: %s -i <device> [OPTIONS]\n", program_name);
    printf("Options:\n");
    printf("  -l, --list-devices      List all available input devices\n");
    printf("  -i, --input <device>    Specify input device path (required)\n");
    printf("  -d, --debug             Enable debug output\n");
    printf("  -m, --min-threshold N   Set minimum movement threshold (default: %.1f)\n", DEFAULT_MIN_THRESHOLD);
    printf("  -M, --max-threshold N   Set maximum movement threshold (default: %.1f)\n", DEFAULT_MAX_THRESHOLD);
    printf("  -b, --log-base N        Set logarithm base for scaling (default: %.1f)\n", DEFAULT_LOG_BASE);
    printf("  -n, --no-sound          Don't play sound files (for testing)\n");
    printf("  -s, --sound-dir <path>  Specify custom folder containing wav files (default: %s)\n", DEFAULT_SOUND_DIR);
    printf("  -v, --version           Display version information\n");
    printf("  -h, --help              Display this help message\n");
    printf("\nUse -l to list available devices\n");
}

void list_input_devices(void) {
    DIR *dir = opendir(DEV_INPUT_PATH);
    if (!dir) {
        perror("Failed to open /dev/input");
        return;
    }

    printf("Available input devices:\n");
    printf("------------------------\n");

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strncmp(entry->d_name, EVENT_PREFIX, strlen(EVENT_PREFIX)) == 0) {
            char device_path[DEVICE_PATH_MAX];
            snprintf(device_path, sizeof(device_path), "%s/%s", 
                    DEV_INPUT_PATH, entry->d_name);

            int fd = open(device_path, O_RDONLY);
            if (fd < 0) {
                continue;
            }

            char device_name[256] = "Unknown Device";
            if (ioctl(fd, EVIOCGNAME(sizeof(device_name)), device_name) >= 0) {
                printf("Device: %-30s | Path: %s\n", device_name, device_path);
            }
            close(fd);
        }
    }
    closedir(dir);
}

bool validate_sound_directory(const char *dir_path) {
    struct stat st;
    
    if (stat(dir_path, &st) != 0) {
        fprintf(stderr, "Error: Cannot access sound directory '%s': %s\n", 
                dir_path, strerror(errno));
        return false;
    }
    
    if (!S_ISDIR(st.st_mode)) {
        fprintf(stderr, "Error: '%s' is not a directory\n", dir_path);
        return false;
    }

    bool missing_files = false;
    for (int i = 1; i <= NUM_INTENSITY_LEVELS; i++) {
        snprintf(sound_path_buffer, sizeof(sound_path_buffer), "%s/%d.wav", dir_path, i);
        if (access(sound_path_buffer, R_OK) != 0) {
            fprintf(stderr, "Error: Missing or unreadable sound file: %s\n", sound_path_buffer);
            missing_files = true;
        }
    }

    if (missing_files) {
        fprintf(stderr, "Error: Sound directory must contain wav files named 1.wav through %d.wav\n", 
                NUM_INTENSITY_LEVELS);
        return false;
    }

    return true;
}

static inline int calculate_intensity(int dx, int dy) {
    double movement = sqrt((double)(dx * dx + dy * dy));
    debug.last_raw_movement = movement;
    
    if (movement < min_movement_threshold) {
        if (debug.enabled) {
            printf("DEBUG: Movement %.2f below threshold, returning 1\n", movement);
        }
        return 1;
    }
    
    if (movement > max_movement_threshold) {
        if (debug.enabled) {
            printf("DEBUG: Movement %.2f above max threshold, returning %d\n", 
                   movement, NUM_INTENSITY_LEVELS);
        }
        return NUM_INTENSITY_LEVELS;
    }
    
    double scaled = log(movement) / log(log_base);
    debug.last_scaled_value = scaled;
    
    double max_scaled = log(max_movement_threshold) / log(log_base);
    double intensity_scaled = 1.0 + (scaled / max_scaled) * (NUM_INTENSITY_LEVELS - 1);
    
    int intensity = (int)(intensity_scaled + 0.5);
    intensity = intensity < 1 ? 1 : (intensity > NUM_INTENSITY_LEVELS ? NUM_INTENSITY_LEVELS : intensity);
    
    if (debug.enabled) {
        printf("DEBUG: Movement: %.2f, Scaled: %.2f, Intensity: %d\n", 
               movement, scaled, intensity);
    }
    
    debug.intensity_counts[intensity]++;
    debug.total_movements++;
    
    return intensity;
}

void play_sound_file(int intensity) {
    if (debug.enabled) {
        printf("DEBUG: Playing sound from directory: %s, intensity: %d\n", sound_directory, intensity);
    }

    if (!no_sound) {
        snprintf(sound_path_buffer, sizeof(sound_path_buffer), 
                "aplay %s/%d.wav 2>/dev/null", sound_directory, intensity);
        
        if (debug.enabled) {
            printf("DEBUG: Executing command: %s\n", sound_path_buffer);
        }
        
        system(sound_path_buffer);
    } else if (debug.enabled) {
        printf("DEBUG: Sound playback disabled, would have played: %s/%d.wav\n", 
               sound_directory, intensity);
    }
}

void print_debug_stats(void) {
    if (!debug.enabled) return;
    
    printf("\nIntensity Distribution Statistics:\n");
    printf("----------------------------------\n");
    printf("Total movements: %ld\n\n", debug.total_movements);
    
    long max_count = 0;
    for (int i = 1; i <= NUM_INTENSITY_LEVELS; i++) {
        if (debug.intensity_counts[i] > max_count) {
            max_count = debug.intensity_counts[i];
        }
    }
    
    for (int i = 1; i <= NUM_INTENSITY_LEVELS; i++) {
        double percentage = (debug.total_movements > 0) ? 
            (double)debug.intensity_counts[i] / debug.total_movements * 100.0 : 0.0;
        
        printf("Intensity %2d: %6ld (%5.1f%%) ", i, debug.intensity_counts[i], percentage);
        
        int histogram_width = (int)((double)debug.intensity_counts[i] / max_count * 50.0);
        for (int j = 0; j < histogram_width; j++) {
            printf("#");
        }
        printf("\n");
    }
    printf("\n");
}

void handle_signal(int sig) {
    if (sig == SIGINT) {
        printf("\nReceived SIGINT, shutting down...\n");
        running = false;
        pthread_cond_broadcast(&cond);
        print_debug_stats();
        exit(0);
    }
}

void *sound_player_thread(void *unused) {
    (void)unused;

    while (running) {
        pthread_mutex_lock(&mutex);

        while (current_intensity == 0 && running) {
            pthread_cond_wait(&cond, &mutex);
        }

        if (!running) {
            pthread_mutex_unlock(&mutex);
            break;
        }

        int intensity_to_play = current_intensity;
        current_intensity = 0;
        is_playing = true;

        pthread_mutex_unlock(&mutex);

        play_sound_file(intensity_to_play);

        pthread_mutex_lock(&mutex);
        is_playing = false;
        pthread_mutex_unlock(&mutex);
    }
    return NULL;
}

void monitor_device(const char *device_path) {
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGINT);
    pthread_sigmask(SIG_BLOCK, &set, NULL);

    pthread_t player_thread;
    if (pthread_create(&player_thread, NULL, sound_player_thread, NULL) != 0) {
        perror("Failed to create sound player thread");
        return;
    }

    pthread_sigmask(SIG_UNBLOCK, &set, NULL);

    int fd = open(device_path, O_RDONLY);
    if (fd < 0) {
        perror("Failed to open device");
        pthread_cancel(player_thread);
        pthread_join(player_thread, NULL);
        return;
    }

    struct input_event ev;
    while (running) {
        ssize_t n = read(fd, &ev, sizeof(struct input_event));
        if (n < (ssize_t)sizeof(struct input_event)) {
            if (running) {
                perror("Error reading input event");
            }
            break;
        }

        if (ev.type == EV_REL) {
            if (ev.code == REL_X || ev.code == REL_Y) {
                int dx = (ev.code == REL_X) ? ev.value : 0;
                int dy = (ev.code == REL_Y) ? ev.value : 0;

                int new_intensity = calculate_intensity(dx, dy);

                pthread_mutex_lock(&mutex);
                if (!is_playing || new_intensity != current_intensity) {
                    current_intensity = new_intensity;
                    pthread_cond_signal(&cond);
                }
                pthread_mutex_unlock(&mutex);
            }
        }
    }

    close(fd);
    pthread_cancel(player_thread);
    pthread_join(player_thread, NULL);
}

int main(int argc, char *argv[]) {
    static struct option long_options[] = {
        {"list-devices", no_argument, 0, 'l'},
        {"input", required_argument, 0, 'i'},
        {"debug", no_argument, 0, 'd'},
        {"help", no_argument, 0, 'h'},
        {"version", no_argument, 0, 'v'},
        {"min-threshold", required_argument, 0, 'm'},
        {"max-threshold", required_argument, 0, 'M'},
        {"log-base", required_argument, 0, 'b'},
        {"no-sound", no_argument, 0, 'n'},
        {"sound-dir", required_argument, 0, 's'},
        {0, 0, 0, 0}
    };

    const char *device_path = NULL;
    int opt;
    bool list_requested = false;

    while ((opt = getopt_long(argc, argv, "li:dhvm:M:b:ns:", long_options, NULL)) != -1) {
        switch (opt) {
            case 'l':
                list_requested = true;
                break;
            case 'i':
                device_path = optarg;
                break;
            case 'd':
                debug.enabled = true;
                printf("Debug mode enabled\n");
                break;
            case 'h':
                print_usage(argv[0]);
                return 0;
            case 'v':
                print_version();
                return 0;
            case 'm':
                min_movement_threshold = atof(optarg);
                break;
            case 'M':
                max_movement_threshold = atof(optarg);
                break;
            case 'b':
                log_base = atof(optarg);
                break;
            case 'n':
                no_sound = true;
                printf("Sound disabled (test mode)\n");
                break;
            case 's':
                sound_directory = optarg;
                break;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }

    if (list_requested) {
        list_input_devices();
        return 0;
    }

    if (device_path == NULL) {
        fprintf(stderr, "Error: Input device is required\n");
        print_usage(argv[0]);
        return 1;
    }

    if (!no_sound && !validate_sound_directory(sound_directory)) {
        return 1;
    }

    if (min_movement_threshold <= 0) {
        fprintf(stderr, "Error: Minimum threshold must be greater than 0\n");
        return 1;
    }
    if (max_movement_threshold <= min_movement_threshold) {
        fprintf(stderr, "Error: Maximum threshold must be greater than minimum threshold\n");
        return 1;
    }
    if (log_base <= 1) {
        fprintf(stderr, "Error: Log base must be greater than 1\n");
        return 1;
    }

    signal(SIGINT, handle_signal);

    printf("Using input device: %s\n", device_path);
    printf("Configuration:\n");
    printf("  Sound directory: %s\n", sound_directory);
    printf("  Minimum threshold: %.2f\n", min_movement_threshold);
    printf("  Maximum threshold: %.2f\n", max_movement_threshold);
    printf("  Log base: %.2f\n", log_base);
    if (no_sound) {
        printf("  Sound: Disabled\n");
    }
    
    monitor_device(device_path);
    return 0;
}