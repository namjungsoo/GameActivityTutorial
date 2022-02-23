#include "native_engine.hpp"

extern "C" {
    void android_main(struct android_app *app);
};

/*
    android_main (not main) is our game entry function, it is called from
    the native app glue utility code as part of the onCreate handler.
*/

void android_main(struct android_app *app) {
    NativeEngine *engine = new NativeEngine(app);
    engine->GameLoop();
    delete engine;
}
