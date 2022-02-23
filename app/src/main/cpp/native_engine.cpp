#include "native_engine.hpp"
#include "game-activity/native_app_glue/android_native_app_glue.h"

NativeEngine::NativeEngine(struct android_app *app) {
    mApp = app;
}

NativeEngine::~NativeEngine() {
}

bool NativeEngine::IsAnimating() {
    return true;
}

void NativeEngine::DoFrame() {

}

void NativeEngine::GameLoop() {
    mApp->userData = this;

    while (1) {
        int events;
        struct android_poll_source* source;

        // If not animating, block until we get an event;
        // If animating, don't block.
        while ((ALooper_pollAll(IsAnimating() ? 0 : -1, NULL, &events,
                                (void **) &source)) >= 0) {
            if (source != NULL) {
                source->process(mApp, source);
            }
            if (mApp->destroyRequested) {
                return;
            }
        }
        if (IsAnimating()) {
            DoFrame();
        }
    }
}