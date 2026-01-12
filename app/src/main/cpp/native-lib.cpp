#include <jni.h>
#include <android/log.h>

extern "C" {
#include "egl_renderer.h"
#include "x11_server.h"
}

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "X11", __VA_ARGS__)

extern "C"
JNIEXPORT void JNICALL
Java_com_example_x11server_MainActivity_nativeInit(JNIEnv*, jobject) {
    LOGI("nativeInit");
    egl_init();
    x11_server_start();
}

extern "C"
JNIEXPORT void JNICALL
Java_com_example_x11server_MainActivity_nativePause(JNIEnv*, jobject) {
    x11_server_pause();
}

extern "C"
JNIEXPORT void JNICALL
Java_com_example_x11server_MainActivity_nativeResume(JNIEnv*, jobject) {
    x11_server_resume();
}
