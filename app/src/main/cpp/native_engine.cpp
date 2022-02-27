#include "native_engine.hpp"
#include "game-activity/native_app_glue/android_native_app_glue.h"
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <cstdlib>
#include <cstring>
#include "Log.h"
#include "swappy/swappyGL.h"

#define LOG_TAG "GameActivityTutorial"
#define VLOGD ALOGD

struct CookedEvent {
    int type;

    // for pointer events
    int motionPointerId;
    bool motionIsOnScreen;
    float motionX, motionY;
    float motionMinX, motionMaxX;
    float motionMinY, motionMaxY;

    // whether a text input has occurred
    bool textInputState;
};

// event type
#define COOKED_EVENT_TYPE_POINTER_DOWN 0
#define COOKED_EVENT_TYPE_POINTER_UP 1
#define COOKED_EVENT_TYPE_POINTER_MOVE 2
#define COOKED_EVENT_TYPE_TEXT_INPUT 7

typedef bool (*CookedEventCallback)(struct CookedEvent *event);

NativeEngine *NativeEngine::_singleton = NULL;

NativeEngine::NativeEngine(struct android_app *app) {
    mApp = app;

    mEglDisplay = EGL_NO_DISPLAY;
    mEglSurface = EGL_NO_SURFACE;
    mEglContext = EGL_NO_CONTEXT;
    mEglConfig = 0;

    mHasFocus = mIsVisible = mHasWindow = false;
    mHasGLObjects = false;
    mIsFirstFrame = true;

    mSurfWidth = mSurfHeight = 0;
    _singleton = this;
    mIsInputMode = false;

    mTextInputState.text_UTF8 = "";
    mTextInputState.text_length = 0;
    mTextInputState.selection.start = 0;
    mTextInputState.selection.end = 0;
    mTextInputState.composingRegion.start = -1;
    mTextInputState.composingRegion.end = -1;

    ALOGI("Calling SwappyGL_init");
    SwappyGL_init(GetJniEnv(), mApp->activity->javaGameActivity);
    SwappyGL_setSwapIntervalNS(SWAPPY_SWAP_60FPS);
}

NativeEngine::~NativeEngine() {
    SwappyGL_destroy();
    KillContext();
}

JNIEnv *NativeEngine::GetJniEnv() {
    if (!mJniEnv) {
        ALOGI("Attaching current thread to JNI.");
        if (0 != mApp->activity->vm->AttachCurrentThread(&mJniEnv, NULL)) {
            ALOGE("*** FATAL ERROR: Failed to attach thread to JNI.");
        }
        ALOGI("Attached current thread to JNI, %p", mJniEnv);
    }
    return mJniEnv;
}

bool NativeEngine::InitDisplay() {
    if (mEglDisplay != EGL_NO_DISPLAY) {
        return true;
    }

    mEglDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (EGL_FALSE == eglInitialize(mEglDisplay, 0, 0)) {
        ALOGE("NativeEngine: failed to init display, error %d", eglGetError());
        return false;
    }
    return true;
}

bool NativeEngine::InitSurface() {
//    ASSERT(mEglDisplay != EGL_NO_DISPLAY);
    if (mEglSurface != EGL_NO_SURFACE) {
        return true;
    }

    EGLint numConfigs;
    const EGLint attribs[] = {
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, // request OpenGL ES 2.0
            EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
            EGL_BLUE_SIZE, 8,
            EGL_GREEN_SIZE, 8,
            EGL_RED_SIZE, 8,
            EGL_DEPTH_SIZE, 16,
            EGL_NONE
    };

    // Pick the first EGLConfig that matches.
    eglChooseConfig(mEglDisplay, attribs, &mEglConfig, 1, &numConfigs);
    mEglSurface = eglCreateWindowSurface(mEglDisplay, mEglConfig, mApp->window,
                                         NULL);
    if (mEglSurface == EGL_NO_SURFACE) {
        ALOGE("Failed to create EGL surface, EGL error %d", eglGetError());
        return false;
    }
    return true;
}

bool NativeEngine::InitContext() {
//    ASSERT(mEglDisplay != EGL_NO_DISPLAY);
    if (mEglContext != EGL_NO_CONTEXT) {
        return true;
    }

    // OpenGL ES 2.0
    EGLint attribList[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    mEglContext = eglCreateContext(mEglDisplay, mEglConfig, NULL, attribList);
    if (mEglContext == EGL_NO_CONTEXT) {
        ALOGE("Failed to create EGL context, EGL error %d", eglGetError());
        return false;
    }
    return true;
}

void NativeEngine::ConfigureOpenGL() {
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glEnable(GL_DEPTH_TEST);
    glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
}

static void _handle_cmd_proxy(struct android_app *app, int32_t cmd) {
    NativeEngine *engine = (NativeEngine *) app->userData;
    engine->HandleCommand(cmd);
}

static bool _cook_game_activity_motion_event(GameActivityMotionEvent *motionEvent,
                                             int screenWidth, int screenHeight,
                                             CookedEventCallback callback) {
    if (motionEvent->pointerCount > 0) {
        int action = motionEvent->action;
        int actionMasked = action & AMOTION_EVENT_ACTION_MASK;
        int ptrIndex = (action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >>
                                                                          AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;

        if (ptrIndex < motionEvent->pointerCount) {
            struct CookedEvent ev;
            memset(&ev, 0, sizeof(ev));

            if (actionMasked == AMOTION_EVENT_ACTION_DOWN || actionMasked ==
                                                             AMOTION_EVENT_ACTION_POINTER_DOWN) {
                ev.type = COOKED_EVENT_TYPE_POINTER_DOWN;
            } else if (actionMasked == AMOTION_EVENT_ACTION_UP || actionMasked ==
                                                                  AMOTION_EVENT_ACTION_POINTER_UP) {
                ev.type = COOKED_EVENT_TYPE_POINTER_UP;
            } else {
                ev.type = COOKED_EVENT_TYPE_POINTER_MOVE;
            }

            ev.motionPointerId = motionEvent->pointers[ptrIndex].id;
            ev.motionIsOnScreen = motionEvent->source == AINPUT_SOURCE_TOUCHSCREEN;
            ev.motionX = GameActivityPointerAxes_getX(&motionEvent->pointers[ptrIndex]);
            ev.motionY = GameActivityPointerAxes_getY(&motionEvent->pointers[ptrIndex]);

            if (ev.motionIsOnScreen) {
                // use screen size as the motion range
                ev.motionMinX = 0.0f;
                ev.motionMaxX = screenWidth;
                ev.motionMinY = 0.0f;
                ev.motionMaxY = screenHeight;
            }

            return callback(&ev);
        }
    }
    return false;
}

static bool _cooked_event_callback(struct CookedEvent *event) {
    switch (event->type) {
        case COOKED_EVENT_TYPE_POINTER_DOWN:
            ALOGD("COOKED_EVENT_TYPE_POINTER_DOWN: %f %f %f %f %f %f %d",
                  event->motionX, event->motionY, event->motionMinX, event->motionMinY, event->motionMaxX, event->motionMaxY, event->motionIsOnScreen);
            NativeEngine::GetInstance()->mIsInputMode = !NativeEngine::GetInstance()->mIsInputMode;
            NativeEngine::GetInstance()->UpdateInputMode();
            return true;
        case COOKED_EVENT_TYPE_POINTER_UP:
            ALOGD("COOKED_EVENT_TYPE_POINTER_UP: %f %f %f %f %f %f %d",
                  event->motionX, event->motionY, event->motionMinX, event->motionMinY, event->motionMaxX, event->motionMaxY, event->motionIsOnScreen);
            return true;
        case COOKED_EVENT_TYPE_POINTER_MOVE:
            ALOGD("COOKED_EVENT_TYPE_POINTER_MOVE: %f %f %f %f %f %f %d",
                  event->motionX, event->motionY, event->motionMinX, event->motionMinY, event->motionMaxX, event->motionMaxY, event->motionIsOnScreen);
            return true;
        default:
            return false;
    }
}

void NativeEngine::UpdateInputMode() {
    if(mIsInputMode) {
        GameActivity_setTextInputState(mApp->activity, &mTextInputState);
        GameActivity_showSoftInput(mApp->activity, 0);
    } else {
        GameActivity_hideSoftInput(mApp->activity, 0);
    }
}

void NativeEngine::HandleCommand(int32_t cmd) {
    VLOGD("NativeEngine: handling command %d.", cmd);
    switch (cmd) {
        case APP_CMD_SAVE_STATE:
            // The system has asked us to save our current state.
            VLOGD("NativeEngine: APP_CMD_SAVE_STATE");
            break;
        case APP_CMD_INIT_WINDOW:
            // We have a window!
            VLOGD("NativeEngine: APP_CMD_INIT_WINDOW");
            if (mApp->window != NULL) {
                mHasWindow = true;
                SwappyGL_setWindow(mApp->window);
            }
            VLOGD("HandleCommand(%d): hasWindow = %d, hasFocus = %d", cmd,
                  mHasWindow ? 1 : 0, mHasFocus ? 1 : 0);

            mSinePlayer.startAudio();
            break;
        case APP_CMD_TERM_WINDOW:
            // The window is going away -- kill the surface
            VLOGD("NativeEngine: APP_CMD_TERM_WINDOW");
            KillSurface();
            mHasWindow = false;
            break;
        case APP_CMD_GAINED_FOCUS:
            VLOGD("NativeEngine: APP_CMD_GAINED_FOCUS");
            mHasFocus = true;
            break;
        case APP_CMD_LOST_FOCUS:
            VLOGD("NativeEngine: APP_CMD_LOST_FOCUS");
            mHasFocus = false;
            break;
        case APP_CMD_PAUSE:
            VLOGD("NativeEngine: APP_CMD_PAUSE");
            break;
        case APP_CMD_RESUME:
            VLOGD("NativeEngine: APP_CMD_RESUME");
            break;
        case APP_CMD_STOP:
            VLOGD("NativeEngine: APP_CMD_STOP");
            mIsVisible = false;
            break;
        case APP_CMD_START:
            VLOGD("NativeEngine: APP_CMD_START");
            mIsVisible = true;
            break;
        case APP_CMD_WINDOW_RESIZED:
        case APP_CMD_CONFIG_CHANGED:
            VLOGD("NativeEngine: %s", cmd == APP_CMD_WINDOW_RESIZED ?
                                      "APP_CMD_WINDOW_RESIZED" : "APP_CMD_CONFIG_CHANGED");
            break;
        case APP_CMD_LOW_MEMORY:
            VLOGD("NativeEngine: APP_CMD_LOW_MEMORY");
            if (!mHasWindow) {
                VLOGD("NativeEngine: trimming memory footprint (deleting GL objects).");
//                KillGLObjects();
            }
            break;
        case APP_CMD_CONTENT_RECT_CHANGED:
            VLOGD("NativeEngine: APP_CMD_CONTENT_RECT_CHANGED");
            break;
        case APP_CMD_WINDOW_REDRAW_NEEDED:
            VLOGD("NativeEngine: APP_CMD_WINDOW_REDRAW_NEEDED");
            break;
        case APP_CMD_WINDOW_INSETS_CHANGED:
            VLOGD("NativeEngine: APP_CMD_WINDOW_INSETS_CHANGED");
            ARect insets;
            // Log all the insets types
            for (int type = 0; type < GAMECOMMON_INSETS_TYPE_COUNT; ++type) {
                GameActivity_getWindowInsets(mApp->activity, (GameCommonInsetsType)type, &insets);
//                VLOGD("%s insets: left=%d right=%d top=%d bottom=%d",
//                      sInsetsTypeName[type], insets.left, insets.right, insets.top, insets.bottom);
            }
            break;
        default:
            VLOGD("NativeEngine: (unknown command).");
            break;
    }

    VLOGD("NativeEngine: STATUS: F%d, V%d, W%d, EGL: D %p, S %p, CTX %p, CFG %p",
          mHasFocus, mIsVisible, mHasWindow, mEglDisplay, mEglSurface, mEglContext,
          mEglConfig);
}

bool NativeEngine::IsAnimating() {
//    VLOGD("NativeEngine: IsAnimating %d %d %d", mHasFocus, mIsVisible, mHasWindow);
    return mHasFocus && mIsVisible && mHasWindow;
}

// kill context
void NativeEngine::KillContext() {
    eglMakeCurrent(mEglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);

    if (mEglContext != EGL_NO_CONTEXT) {
        eglDestroyContext(mEglDisplay, mEglContext);
        mEglContext = EGL_NO_CONTEXT;
    }
}

void NativeEngine::KillSurface() {
    eglMakeCurrent(mEglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (mEglSurface != EGL_NO_SURFACE) {
        eglDestroySurface(mEglDisplay, mEglSurface);
        mEglSurface = EGL_NO_SURFACE;
    }
}

// also causes context and surface to get killed
void NativeEngine::KillDisplay() {
    KillContext();
    KillSurface();

    if (mEglDisplay != EGL_NO_DISPLAY) {
        ALOGI("NativeEngine: terminating display now.");
        eglTerminate(mEglDisplay);
        mEglDisplay = EGL_NO_DISPLAY;
    }
}

bool NativeEngine::HandleEglError(EGLint error) {
    switch (error) {
        case EGL_SUCCESS:
            // nothing to do
            return true;
        case EGL_CONTEXT_LOST:
            ALOGW("NativeEngine: egl error: EGL_CONTEXT_LOST. Recreating context.");
            KillContext();
            exit(-1);
            return true;
        case EGL_BAD_CONTEXT:
            ALOGW("NativeEngine: egl error: EGL_BAD_CONTEXT. Recreating context.");
            KillContext();
            exit(-1);
            return true;
        case EGL_BAD_DISPLAY:
            ALOGW("NativeEngine: egl error: EGL_BAD_DISPLAY. Recreating display.");
            KillDisplay();
            exit(-1);
            return true;
        case EGL_BAD_SURFACE:
            ALOGW("NativeEngine: egl error: EGL_BAD_SURFACE. Recreating display.");
            KillSurface();
            exit(-1);
            return true;
        default:
            ALOGW("NativeEngine: unknown egl error: %d", error);
            exit(-1);
            return false;
    }
}

bool NativeEngine::PrepareToRender() {
    if (mEglDisplay == EGL_NO_DISPLAY || mEglSurface == EGL_NO_SURFACE ||
        mEglContext == EGL_NO_CONTEXT) {

        // create display if needed
        if (!InitDisplay()) {
            ALOGE("NativeEngine: failed to create display.");
            return false;
        }

        // create surface if needed
        if (!InitSurface()) {
            ALOGE("NativeEngine: failed to create surface.");
            return false;
        }

        // create context if needed
        if (!InitContext()) {
            ALOGE("NativeEngine: failed to create context.");
            return false;
        }

        ALOGI("NativeEngine: binding surface and context (display %p, surface %p, context %p)",
              mEglDisplay, mEglSurface, mEglContext);

        // bind them
        if (EGL_FALSE == eglMakeCurrent(mEglDisplay, mEglSurface, mEglSurface, mEglContext)) {
            ALOGE("NativeEngine: eglMakeCurrent failed, EGL error %d", eglGetError());
            HandleEglError(eglGetError());
        }

        // configure our global OpenGL settings
        ConfigureOpenGL();
    }
    return true;
}

void NativeEngine::DoFrame() {
    // prepare to render (create context, surfaces, etc, if needed)
    if (!PrepareToRender()) {
        // not ready
        ALOGE("NativeEngine: preparation to render failed.");
        return;
    }

    // how big is the surface? We query every frame because it's cheap, and some
    // strange devices out there change the surface size without calling any callbacks...
    int width, height;
    eglQuerySurface(mEglDisplay, mEglSurface, EGL_WIDTH, &width);
    eglQuerySurface(mEglDisplay, mEglSurface, EGL_HEIGHT, &height);

    if (width != mSurfWidth || height != mSurfHeight) {
        // notify scene manager that the surface has changed size
        ALOGI("NativeEngine: surface changed size %dx%d --> %dx%d", mSurfWidth, mSurfHeight,
              width, height);
        mSurfWidth = width;
        mSurfHeight = height;
        glViewport(0, 0, mSurfWidth, mSurfHeight);
    }

    // Swap buffers.
    if (!SwappyGL_swap(mEglDisplay, mEglSurface)) {        // failed to swap buffers...
        ALOGW("NativeEngine: SwappyGL_swap failed, EGL error %d", eglGetError());
        HandleEglError(eglGetError());
    }
}

void NativeEngine::GameLoop() {
    mApp->userData = this;
    mApp->onAppCmd = _handle_cmd_proxy;

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

        HandleGameActivityInput();

        if (mApp->textInputState) {
            struct CookedEvent ev;
            ev.type = COOKED_EVENT_TYPE_TEXT_INPUT;
            ev.textInputState = true;
            //_cooked_event_callback(&ev);
            OnTextInput();
            mApp->textInputState = 0;
        }

        if (IsAnimating()) {
            DoFrame();
        }
    }
}

void NativeEngine::HandleGameActivityInput() {
    // Swap input buffers so we don't miss any events while processing inputBuffer.
    android_input_buffer* inputBuffer = android_app_swap_input_buffers(mApp);
    // Early exit if no events.
    if (inputBuffer == nullptr) return;

    if (inputBuffer->motionEventsCount != 0) {
        for (uint64_t i = 0; i < inputBuffer->motionEventsCount; ++i) {
            GameActivityMotionEvent* motionEvent = &inputBuffer->motionEvents[i];
            // Didn't belong to a game controller, process it ourselves if it is a touch event
            _cook_game_activity_motion_event(motionEvent, mSurfWidth, mSurfHeight, _cooked_event_callback);
        }
        android_app_clear_motion_events(inputBuffer);
    }
}


void NativeEngine::OnTextInput() {
    auto activity = mApp->activity;
    GameActivity_getTextInputState(activity, [](void *context, const GameTextInputState *state) {
        VLOGD("InputString: %s", state->text_UTF8);
        NativeEngine::GetInstance()->mTextInputState = *state;
    }, this);
    ARect insets;
    GameTextInput_getImeInsets(GameActivity_getTextInput(activity), &insets);
//    VLOGD("NativeEngine", "IME insets: left=%d right=%d top=%d bottom=%d",
//                        insets.left, insets.right, insets.top, insets.bottom);
}
