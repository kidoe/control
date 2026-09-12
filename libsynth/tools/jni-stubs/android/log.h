/* Not Android's log.h; see aaudio/AAudio.h in this directory for why it exists. */
#ifndef SYNTH_STUB_ANDROID_LOG_H
#define SYNTH_STUB_ANDROID_LOG_H

enum { ANDROID_LOG_ERROR = 6 };

int __android_log_print(int priority, const char *tag, const char *fmt, ...);

#endif
