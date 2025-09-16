#include "log.h"
#include <syslog.h>
#include <stdarg.h>

void log_init(const char *ident){openlog(ident, LOG_PID|LOG_CONS, LOG_DAEMON);}
void log_close(void){ closelog();}

static void vlog_init(int prio, const char *fmt, va_list ap){
	vsyslog(prio, fmt, ap);
}

void log_info(const char *fmt, ...){
	va_list ap;
	va_start(ap, fmt);
	vlog_init(LOG_INFO, fmt, ap); 
	va_end(ap);
}

void log_warn(const char *fmt, ...){
	va_list ap;
	va_start(ap, fmt);
	vlog_init(LOG_WARNING, fmt, ap);
	va_end(ap);
}

void  log_err(const char* fmt, ...){
	va_list ap;
	va_start(ap,fmt);
	vlog_init(LOG_ERR, fmt, ap);
	va_end(ap);
}

