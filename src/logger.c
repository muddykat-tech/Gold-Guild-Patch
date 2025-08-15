#include "logger.h"
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>

#define MAX_LABELS         16
#define MAX_PATH_LENGTH    256
#define MAX_LABEL_LENGTH   64
#define MAX_LOG_MESSAGE    8192

static FILE* main_log_file = NULL;
static struct {
    char label[64];
    FILE* file;
} label_log_files[MAX_LABELS];

static int label_count = 0;

static void sanitize_label(const char* input, char* output, size_t max_len) {
    size_t j = 0;
    for (size_t i = 0; input[i] != '\0' && j < max_len - 1; ++i) {
        char c = input[i];
        if (isalnum((unsigned char)c) || c == '_' || c == '-') {
            output[j++] = c;
        } else {
            output[j++] = '_';
        }
    }
    output[j] = '\0';
}

static void rotate_log_file(const char* filename) {
    char old_filename[MAX_PATH_LENGTH];
    snprintf(old_filename, sizeof(old_filename), "logs/old_%s", filename);
    DeleteFileA(old_filename);
    MoveFileA(filename, old_filename);
}

void InitializeLogger(void) {
    CreateDirectoryA("logs", NULL);
    rotate_log_file("logs/patch.log");
    WIN32_FIND_DATAA find_data;
    HANDLE hFind = FindFirstFileA("logs/patch.*.log", &find_data);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            char full_path[MAX_PATH_LENGTH];
            snprintf(full_path, sizeof(full_path), "logs/%s", find_data.cFileName);
            rotate_log_file(full_path);
        } while (FindNextFileA(hFind, &find_data));
        FindClose(hFind);
    }
}

static void ensure_main_log_file() {
    if (!main_log_file) {
        main_log_file = fopen("logs/patch.log", "a");
    }
}

static FILE* get_label_log_file(const char* label) {
    if (!label || label[0] == '\0') return NULL;

    for (int i = 0; i < label_count; ++i) {
        if (strcmp(label_log_files[i].label, label) == 0) {
            return label_log_files[i].file;
        }
    }

    if (label_count >= MAX_LABELS) {
        return NULL;
    }

    char safe_label[MAX_LABEL_LENGTH];
    sanitize_label(label, safe_label, sizeof(safe_label));

    char filename[MAX_PATH_LENGTH];
    snprintf(filename, sizeof(filename), "logs/patch.%s.log", safe_label);

    FILE* f = fopen(filename, "a");
    if (!f) return NULL;

    strncpy(label_log_files[label_count].label, label, sizeof(label_log_files[label_count].label) - 1);
    label_log_files[label_count].label[sizeof(label_log_files[label_count].label) - 1] = '\0';
    label_log_files[label_count].file = f;
    ++label_count;

    return f;
}

void WriteLog(const char* label, const char* message) {
    ensure_main_log_file();

    if (main_log_file) {
        fprintf(main_log_file, "%s\n", message);
        fflush(main_log_file);
    }

    if (label && label[0] != '\0') {
        FILE* labeled = get_label_log_file(label);
        if (labeled) {
            fprintf(labeled, "%s\n", message);
            fflush(labeled);
        }
    }

    OutputDebugStringA(message);
    OutputDebugStringA("\n");
}

void WriteLogf(const char* label, const char* format, ...) {
    char buffer[MAX_LOG_MESSAGE];

    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    WriteLog(label, buffer);
}

void WriteLogSimple(const char* message) {
    WriteLog(NULL, message);
}

void WriteLogfSimple(const char* format, ...) {
    char buffer[MAX_LOG_MESSAGE];

    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    WriteLog(NULL, buffer);
}

void CloseLogFiles(void) {
    if (main_log_file) {
        fclose(main_log_file);
        main_log_file = NULL;
    }

    for (int i = 0; i < label_count; ++i) {
        if (label_log_files[i].file) {
            fclose(label_log_files[i].file);
            label_log_files[i].file = NULL;
        }
    }
    label_count = 0;
}
