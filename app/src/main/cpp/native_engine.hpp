#ifndef __NATIVE_ENGINE_H__
#define __NATIVE_ENGINE_H__

#pragma once

#include <EGL/egl.h>
#include <game-text-input/gametextinput.h>

class NativeEngine {
public:
    // create an engine
    NativeEngine(struct android_app *app);
    ~NativeEngine();

    // runs application until it dies
    void GameLoop();
    void HandleCommand(int32_t cmd);
    static NativeEngine *GetInstance() {
        return _singleton;
    }

    void UpdateInputMode();

    bool mIsInputMode;
    GameTextInputState mTextInputState;

private:
    bool IsAnimating();
    void DoFrame();
    void HandleGameActivityInput();

    bool InitDisplay();
    bool InitSurface();
    bool InitContext();
    void ConfigureOpenGL();
    bool PrepareToRender();

    // kill context
    void KillContext();
    void KillSurface();
    void KillDisplay(); // also causes context and surface to get killed
    bool HandleEglError(EGLint error);
    void OnTextInput();

    // android_app structure
    struct android_app *mApp;
    static NativeEngine *_singleton;

    EGLDisplay mEglDisplay;
    EGLSurface mEglSurface;
    EGLContext mEglContext;
    EGLConfig mEglConfig;

    bool mHasFocus, mIsVisible, mHasWindow;
    bool mHasGLObjects;
    bool mIsFirstFrame;

    int mSurfWidth, mSurfHeight;
};

#endif//__NATIVE_ENGINE_H__