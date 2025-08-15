#ifndef LOGGER_H
#define LOGGER_H

#ifdef __cplusplus
extern "C" {
#endif

void WriteLogSimple(const char* message);

void InitializeLogger(void);

void WriteLogfSimple(const char* format, ...);

void WriteLog(const char* message, const char* label);

void WriteLogf(const char* label, const char* format, ...);

void CloseLogFiles(void);

#ifdef __cplusplus
}
#endif

#endif 