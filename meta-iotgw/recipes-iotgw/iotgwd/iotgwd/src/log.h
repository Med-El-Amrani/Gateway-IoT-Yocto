#pragma once
void log_init(const char *ident);
void log_info(const char *fmt, ...);
void log_warn(const char *fmt, ...);
void log_err(const char *fmt, ...);
void log_close(void);

