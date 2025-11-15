#ifndef TIGR_HEADLESS

#include "tigr_internal.h"

#ifdef __SWITCH__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <switch.h>
#include <glad/glad.h>

#ifndef NDEBUG
#define LOGD(fmt,...) printf("[DEBUG] %s: " fmt "\n", __PRETTY_FUNCTION__, ## __VA_ARGS__)
static int nxlink_sock = -1;
#else
#define LOGD(fmt,...) ((void)0)
#endif

static AppletHookCookie applet_hook_cookie;

typedef struct {
    HidTouchScreenState touch;
    bool touched;
    PadState pad;
    SwkbdInline kbdInline;
    SwkbdAppearArg kbdAppearArg;
    bool kbdOK;
    bool kbdOpen;
    char kbdBuf[33];
    int kbdBufPos;
} InputState;

/// Global state
static struct {
    NWindow* window;
    InputState inputState;
    EGLDisplay display;
    EGLSurface surface;
    EGLint screenW;
    EGLint screenH;
    bool needsResize;
    EGLConfig config;
    double lastTime;
    bool backgrounded;
    bool closed;
} gState = {
    .window = 0,
    .inputState = {
        .touch = {0},
        .touched = false,
        .pad = {0},
        .kbdInline = {0},
        .kbdAppearArg = {0},
        .kbdOK = false,
        .kbdOpen = false,
        .kbdBuf = {0},
        .kbdBufPos = -1,
    },
    .display = EGL_NO_DISPLAY,
    .surface = EGL_NO_SURFACE,
    .screenW = 0,
    .screenH = 0,
    .needsResize = false,
    .config = 0,
    .lastTime = 0,
    .backgrounded = false,
    .closed = false,
};

static void appletHookCallback(AppletHookType hook, void* param) {
    switch (hook) {
        case AppletHookType_OnExitRequest:
            LOGD("Closing by request from home menu.");
            gState.closed = true;
            break;

        case AppletHookType_OnResume:
            LOGD("Resumed.");
            gState.backgrounded = false;
            tigrTime(); // reset time keeping
            break;

        case AppletHookType_OnFocusState:
            if (appletGetFocusState() == AppletFocusState_InFocus) {
                LOGD("Foregrounded.");
                gState.backgrounded = false;
                tigrTime(); // reset time keeping
                appletSetFocusHandlingMode(AppletFocusHandlingMode_NoSuspend);
            } else {
                LOGD("Backgrounded.");
                gState.backgrounded = true;
                appletSetFocusHandlingMode(AppletFocusHandlingMode_SuspendHomeSleepNotify);
            }
            break;

        case AppletHookType_OnOperationMode:
            if (appletGetOperationMode() == AppletOperationMode_Console) {
                LOGD("Docked mode.");
                gState.screenW = 1920;
                gState.screenH = 1080;
            } else {
                LOGD("Handheld mode.");
                gState.screenW = 1280;
                gState.screenH = 720;
            }
            gState.needsResize = true;
            break;

        default:
            break;
    }
}

static void swkbd_decidedcancel_cb() {
    gState.inputState.kbdOpen = false;
}

static void swkbd_decidedenter_cb(const char* str, SwkbdDecidedEnterArg* arg) {
    size_t bufLen = sizeof(gState.inputState.kbdBuf) - 1;
    assert(arg->stringLen < bufLen);

    strncpy(gState.inputState.kbdBuf, str, bufLen);
    gState.inputState.kbdBuf[bufLen] = '\0';
    gState.inputState.kbdBufPos = 0;
    gState.inputState.kbdOpen = false;
}

static void logEglError() {
    int error = eglGetError();
    switch (error) {
        case EGL_BAD_DISPLAY:
            printf("EGL error: Bad display");
            break;
        case EGL_NOT_INITIALIZED:
            printf("EGL error: Not initialized");
            break;
        case EGL_BAD_NATIVE_WINDOW:
            printf("EGL error: Bad native window");
            break;
        case EGL_BAD_ALLOC:
            printf("EGL error: Bad alloc");
            break;
        case EGL_BAD_MATCH:
            printf("EGL error: Bad match");
            break;
        default:
            printf("EGL error: %d", error);
    }
}

static const EGLint contextAttribs[] = { EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 0, EGL_NONE };

static EGLConfig getGLConfig(EGLDisplay display) {
    EGLConfig config = 0;

    const EGLint attribs[] = { EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_BLUE_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_RED_SIZE, 8,
                               EGL_NONE };
    EGLint numConfigs;

    eglChooseConfig(display, attribs, NULL, 0, &numConfigs);
    EGLConfig* supportedConfigs = (EGLConfig*)malloc(sizeof(EGLConfig) * numConfigs);
    eglChooseConfig(display, attribs, supportedConfigs, numConfigs, &numConfigs);

    int i = 0;
    for (; i < numConfigs; i++) {
        EGLConfig* cfg = supportedConfigs[i];
        EGLint r, g, b, d;
        if (eglGetConfigAttrib(display, cfg, EGL_RED_SIZE, &r) &&
            eglGetConfigAttrib(display, cfg, EGL_GREEN_SIZE, &g) &&
            eglGetConfigAttrib(display, cfg, EGL_BLUE_SIZE, &b) &&
            eglGetConfigAttrib(display, cfg, EGL_DEPTH_SIZE, &d) && r == 8 && g == 8 && b == 8 && d == 0) {
            config = supportedConfigs[i];
            break;
        }
    }

    if (i == numConfigs) {
        config = supportedConfigs[0];
    }

    if (config == NULL) {
        tigrError(NULL, "Unable to initialize EGLConfig");
    }

    free(supportedConfigs);

    return config;
}

static void tigr_switch_destroy() {
    LOGD("tearDownOpenGL");
    if (gState.display != EGL_NO_DISPLAY) {
        eglMakeCurrent(gState.display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);

        if (gState.surface != EGL_NO_SURFACE) {
            LOGD("eglDestroySurface");
            if (!eglDestroySurface(gState.display, gState.surface)) {
                logEglError();
            }
            gState.surface = EGL_NO_SURFACE;
        }
    }
    gState.window = 0;

    eglTerminate(gState.display);
    gState.display = EGL_NO_DISPLAY;

    appletUnhook(&applet_hook_cookie);
    appletSetFocusHandlingMode(AppletFocusHandlingMode_SuspendHomeSleep);

    swkbdInlineClose(&gState.inputState.kbdInline);

    appletUnlockExit();
}

static void tigr_switch_create() {
    gState.closed = false;
    gState.window = nwindowGetDefault();
    gState.surface = EGL_NO_SURFACE;
    gState.lastTime = 0;

    nwindowSetDimensions(gState.window, 1920, 1080);

    if (gState.display == EGL_NO_DISPLAY) {
        gState.display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        EGLBoolean status = eglInitialize(gState.display, NULL, NULL);
        if (!status) {
            tigrError(NULL, "Failed to init EGL");
        }
        gState.config = getGLConfig(gState.display);
    }

    LOGD("setupOpenGL");
    assert(gState.surface == EGL_NO_SURFACE);
    assert(gState.window != 0);

    gState.surface = eglCreateWindowSurface(gState.display, gState.config, gState.window, NULL);
    if (gState.surface == EGL_NO_SURFACE) {
        logEglError();
    }
    assert(gState.surface != EGL_NO_SURFACE);

    appletHookCallback(AppletHookType_OnOperationMode, NULL);
    appletSetFocusHandlingMode(AppletFocusHandlingMode_NoSuspend);
    appletHook(&applet_hook_cookie, appletHookCallback, NULL);

    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&gState.inputState.pad);
    hidInitializeTouchScreen();

    Result rc = swkbdInlineCreate(&gState.inputState.kbdInline);
    if (R_SUCCEEDED(rc)) {
        rc = swkbdInlineLaunchForLibraryApplet(&gState.inputState.kbdInline, SwkbdInlineMode_AppletDisplay, 0);
        swkbdInlineSetDecidedEnterCallback(&gState.inputState.kbdInline, swkbd_decidedenter_cb);
        swkbdInlineSetDecidedCancelCallback(&gState.inputState.kbdInline, swkbd_decidedcancel_cb);

        swkbdInlineMakeAppearArg(&gState.inputState.kbdAppearArg, SwkbdType_Normal);
        swkbdInlineAppearArgSetOkButtonText(&gState.inputState.kbdAppearArg, "Submit");
        swkbdInlineAppearArgSetStringLenMax(&gState.inputState.kbdAppearArg, 32);
        gState.inputState.kbdAppearArg.dicFlag = 1;
        gState.inputState.kbdAppearArg.returnButtonFlag = 1;
        if (R_SUCCEEDED(rc))
            gState.inputState.kbdOK = true;
    }

    atexit(tigr_switch_destroy);
    appletLockExit();
}

static int processEvents(TigrInternal* win) {
    if (gState.closed) {
        return 0;
    }

    // swkbd
    if(gState.inputState.kbdOK) {
        Result rc = swkbdInlineUpdate(&gState.inputState.kbdInline, NULL);
        if(R_FAILED(rc)) {
            gState.inputState.kbdOK = false;
            gState.inputState.kbdOpen = false;
            swkbdInlineDisappear(&gState.inputState.kbdInline);
        }
    }
    if(gState.inputState.kbdBufPos > -1) {
        if(gState.inputState.kbdBuf[gState.inputState.kbdBufPos] == '\0') {
            gState.inputState.kbdBufPos = -1;
        } else {
            win->keys[gState.inputState.kbdBuf[gState.inputState.kbdBufPos]] = 1;
            win->lastChar = gState.inputState.kbdBuf[gState.inputState.kbdBufPos];
            gState.inputState.kbdBufPos++;
        }
    }

    // buttons
    padUpdate(&gState.inputState.pad);
    u64 kDown = padGetButtonsDown(&gState.inputState.pad);
    u64 kNow = padGetButtons(&gState.inputState.pad);
#ifndef NDEBUG
    // easy way to stop
    if (kDown & HidNpadButton_Minus) {
        gState.closed = false;
        return 0;
    }
#endif
    if ((kDown & HidNpadButton_Plus) && !gState.inputState.kbdOpen) {
        tigrShowKeyboard(1);
    }

    if (gState.inputState.kbdOpen) {
        // release all fingers and skip keys
        gState.inputState.touched = false;
        return 1;
    }

    #define MAPPING_SIZE 24
    static const struct {
        int key;
        HidNpadButton button;
    } mapping[MAPPING_SIZE] = {
        {'A', HidNpadButton_A},
        {'B', HidNpadButton_B},
        {'X', HidNpadButton_X},
        {'Y', HidNpadButton_Y},
        {'L', HidNpadButton_L},
        {'R', HidNpadButton_R},
        {'Z', HidNpadButton_ZL},
        {'Q', HidNpadButton_ZR},
        {'C', HidNpadButton_StickL},
        {'T', HidNpadButton_StickR},
        {TK_ESCAPE, HidNpadButton_Minus},
        {TK_LEFT, HidNpadButton_Left},
        {TK_RIGHT, HidNpadButton_Right},
        {TK_UP, HidNpadButton_Up},
        {TK_DOWN, HidNpadButton_Down},
        {'J', HidNpadButton_StickLLeft},
        {'L', HidNpadButton_StickLRight},
        {'I', HidNpadButton_StickLUp},
        {'K', HidNpadButton_StickLDown},
        {TK_PAD4, HidNpadButton_StickRLeft},
        {TK_PAD6, HidNpadButton_StickRRight},
        {TK_PAD8, HidNpadButton_StickRUp},
        {TK_PAD2, HidNpadButton_StickRDown},
    };
    for (int i = 0; i < MAPPING_SIZE; i++) {
        win->keys[mapping[i].key] = (kNow & mapping[i].button);
    }
    #undef MAPPING_SIZE

    // touch
    gState.inputState.touched = hidGetTouchScreenStates(&gState.inputState.touch, 1);

    return 1;
}

static Tigr* refreshWindow(Tigr* bmp) {
    if (gState.window == 0) {
        return 0;
    }

    if(gState.needsResize) {
        LOGD("Screen is %d x %d", gState.screenW, gState.screenH);
        nwindowSetCrop(gState.window, 0, 0, gState.screenW, gState.screenH);
        gState.needsResize = false;
    }

    TigrInternal* win = tigrInternal(bmp);

    int scale = 1;
    if (win->flags & TIGR_AUTO) {
        // Always use a 1:1 pixel size.
        scale = 1;
    } else {
        // See how big we can make it and still fit on-screen.
        scale = tigrCalcScale(bmp->w, bmp->h, gState.screenW, gState.screenH);
    }

    win->scale = tigrEnforceScale(scale, win->flags);

    return bmp;
}

void tigrShowKeyboard(int show) {
    if (!gState.inputState.kbdOK)
        return;

    if(show) {
        swkbdInlineSetInputText(&gState.inputState.kbdInline, "");
        swkbdInlineSetCursorPos(&gState.inputState.kbdInline, 0);
        swkbdInlineUpdate(&gState.inputState.kbdInline, NULL);
        swkbdInlineAppear(&gState.inputState.kbdInline, &gState.inputState.kbdAppearArg);
        gState.inputState.kbdOpen = true;
    } else {
        swkbdInlineDisappear(&gState.inputState.kbdInline);
        gState.inputState.kbdOpen = false;
    }
}

Tigr* tigrWindow(int w, int h, const char* title, int flags) {
    tigr_switch_create();
    EGLContext context = eglCreateContext(gState.display, gState.config, NULL, contextAttribs);

    int scale = 1;
    if (flags & TIGR_AUTO) {
        // Always use a 1:1 pixel size.
        scale = 1;
    } else {
        // See how big we can make it and still fit on-screen.
        scale = tigrCalcScale(w, h, gState.screenW, gState.screenH);
    }

    scale = tigrEnforceScale(scale, flags);

    Tigr* bmp = tigrBitmap2(w, h, sizeof(TigrInternal));
    bmp->handle = (void*)gState.window;

    TigrInternal* win = tigrInternal(bmp);
    win->context = context;

    win->shown = 0;
    win->closed = false;
    win->scale = scale;

    win->lastChar = 0;
    win->flags = flags;
    win->p1 = win->p2 = win->p3 = 0;
    win->p4 = 1;
    win->widgetsWanted = 0;
    win->widgetAlpha = 0;
    win->widgetsScale = 0;
    win->widgets = 0;
    win->gl.gl_legacy = 0;

    memset(win->keys, 0, 256);
    memset(win->prev, 0, 256);

    tigrPosition(bmp, win->scale, bmp->w, bmp->h, win->pos);

    if (eglMakeCurrent(gState.display, gState.surface, gState.surface, context) == EGL_FALSE) {
        printf("Unable to eglMakeCurrent");
        return 0;
    }

    tigrGAPICreate(bmp);

    return bmp;
}

int tigrClosed(Tigr* bmp) {
    TigrInternal* win = tigrInternal(bmp);
    return win->closed;
}

int tigrGAPIBegin(Tigr* bmp) {
    assert(gState.display != EGL_NO_DISPLAY);
    assert(gState.surface != EGL_NO_SURFACE);

    TigrInternal* win = tigrInternal(bmp);
    if (eglMakeCurrent(gState.display, gState.surface, gState.surface, win->context) == EGL_FALSE) {
        return -1;
    }
    return 0;
}

int tigrGAPIEnd(Tigr* bmp) {
    (void)bmp;
    eglMakeCurrent(gState.display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    return 0;
}

int tigrKeyDown(Tigr* bmp, int key) {
    TigrInternal* win;
    assert(key < 256);
    win = tigrInternal(bmp);
    return win->keys[key] && !win->prev[key];
}

int tigrKeyHeld(Tigr* bmp, int key) {
    TigrInternal* win;
    assert(key < 256);
    win = tigrInternal(bmp);
    return win->keys[key];
}

int tigrReadChar(Tigr* bmp) {
    TigrInternal* win = tigrInternal(bmp);
    int c = win->lastChar;
    win->lastChar = 0;
    return c;
}

static void tigrUpdateModifiers(TigrInternal* win) {
    win->keys[TK_SHIFT] = win->keys[TK_LSHIFT] || win->keys[TK_RSHIFT];
    win->keys[TK_CONTROL] = win->keys[TK_LCONTROL] || win->keys[TK_RCONTROL];
    win->keys[TK_ALT] = win->keys[TK_LALT] || win->keys[TK_RALT];
}

static int toWindowX(TigrInternal* win, int x) {
    return (x - win->pos[0]) / win->scale;
}

static int toWindowY(TigrInternal* win, int y) {
    return (y - win->pos[1]) / win->scale;
}

void tigrUpdate(Tigr* bmp) {
    TigrInternal* win = tigrInternal(bmp);

    if(!appletMainLoop()) {
        gState.closed = true;
        return;
    }

    if(gState.backgrounded) {
        return;
    }

    memcpy(win->prev, win->keys, 256);

    if (!processEvents(win)) {
        win->closed = true;
        return;
    }

    tigrUpdateModifiers(win);

    if (gState.window == 0) {
        return;
    }

    bmp = refreshWindow(bmp);
    if (bmp == 0) {
        return;
    }

    if (gState.inputState.touched) {
        win->numTouchPoints = gState.inputState.touch.count;
        if (win->numTouchPoints > MAX_TOUCH_POINTS)
            win->numTouchPoints = MAX_TOUCH_POINTS;
        for (int i = 0; i < win->numTouchPoints; i++) {
            win->touchPoints[i].x = toWindowX(win, gState.inputState.touch.touches[i].x * gState.screenW / 1280.0f);
            win->touchPoints[i].y = toWindowY(win, gState.inputState.touch.touches[i].y * gState.screenH / 720.0f);
        }
    } else {
        win->numTouchPoints = 0;
    }

    win->mouseButtons = win->numTouchPoints;
    if (win->mouseButtons > 0) {
        win->mouseX = win->touchPoints[0].x;
        win->mouseY = win->touchPoints[0].y;
    }

    if (win->flags & TIGR_AUTO) {
        tigrResize(bmp, gState.screenW / win->scale, gState.screenH / win->scale);
    } else {
        win->scale = tigrEnforceScale(tigrCalcScale(bmp->w, bmp->h, gState.screenW, gState.screenH), win->flags);
    }

    tigrPosition(bmp, win->scale, gState.screenW, gState.screenH, win->pos);
    tigrGAPIBegin(bmp);
    tigrGAPIPresent(bmp, gState.screenW, gState.screenH);
    eglSwapBuffers(gState.display, gState.surface);
    tigrGAPIEnd(bmp);
}

void tigrFree(Tigr* bmp) {
    if (bmp->handle) {
        TigrInternal* win = tigrInternal(bmp);

        eglMakeCurrent(gState.display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (win->context != EGL_NO_CONTEXT) {
            // Win closed means app windows has closed, and the call would fail.
            if (!win->closed) {
                tigrGAPIDestroy(bmp);
            }
            eglDestroyContext(gState.display, win->context);
        }

        win->context = EGL_NO_CONTEXT;
    }
    free(bmp->pix);
    free(bmp);
}

void tigrError(Tigr* bmp, const char* message, ...) {
    char tmp[1024];

    va_list args;
    va_start(args, message);
    vsnprintf(tmp, sizeof(tmp), message, args);
    tmp[sizeof(tmp) - 1] = 0;
    va_end(args);

    tigr_switch_destroy();
    PadState pad;
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&pad);
    consoleInit(NULL);

    printf("tigr fatal error: %s\n", tmp);
    printf("\nPress A to exit.\n");
    consoleUpdate(NULL);

    while (appletMainLoop()) {
        padUpdate(&pad);
        const u64 keys = padGetButtonsDown(&pad);
        if (keys & HidNpadButton_A)
            break;
    }

    consoleExit(NULL);
    exit(1);
}

float tigrTime() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);

    double now = (double)ts.tv_sec + (ts.tv_nsec / 1000000000.0);
    double elapsed = gState.lastTime == 0 ? 0 : now - gState.lastTime;
    gState.lastTime = now;

    return (float)elapsed;
}

void tigrMouse(Tigr* bmp, int* x, int* y, int* buttons) {
    TigrInternal* win = tigrInternal(bmp);
    if (x) {
        *x = win->mouseX;
    }
    if (y) {
        *y = win->mouseY;
    }
    if (buttons) {
        *buttons = win->mouseButtons;
    }
}

int tigrTouch(Tigr* bmp, TigrTouchPoint* points, int maxPoints) {
    TigrInternal* win = tigrInternal(bmp);
    for (int i = 0; i < maxPoints && i < win->numTouchPoints; i++) {
        points[i] = win->touchPoints[i];
    }
    return maxPoints < win->numTouchPoints ? maxPoints : win->numTouchPoints;
}

void tigrMouseWheel(Tigr* bmp, float* x, float* y) {
    if (x) {
        *x = 0;
    }
    if (y) {
        *y = 0;
    }
}

void userAppInit(void) {
#ifndef NDEBUG
    socketInitializeDefault();
    nxlink_sock = nxlinkStdio();
    printf("NXLINK\n");
#endif
    romfsInit();
}

void userAppExit(void) {
    romfsExit();
#ifndef NDEBUG
    if(nxlink_sock >= 0)
        close(nxlink_sock);
    socketExit();
#endif
}

#endif  // __SWITCH__
#endif // #ifndef TIGR_HEADLESS
