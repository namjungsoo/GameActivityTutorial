#ifndef __NATIVE_ENGINE_H__
#define __NATIVE_ENGINE_H__

#pragma once

class NativeEngine {
public:
    // create an engine
    NativeEngine(struct android_app *app);

    ~NativeEngine();

    // runs application until it dies
    void GameLoop();

private:
    bool IsAnimating();
    void DoFrame();

    // android_app structure
    struct android_app *mApp;

};

#endif//__NATIVE_ENGINE_H__