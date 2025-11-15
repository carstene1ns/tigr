#include "tigr.h"
#include <stdio.h>
#include <errno.h>

int main(int argc, char**argv) {
    Tigr* screen = tigrWindow(640, 360, "Hello", TIGR_AUTO | TIGR_3X);

    int x;
    int y;
    int buttons;
    TigrTouchPoint touchPoints[3];

    const char* message = "Hello, world.";
    int textHeight = tigrTextHeight(tfont, message);
    int textWidth = tigrTextWidth(tfont, message);

    float liveTime = 0;

    Tigr* logo = tigrLoadImage("romfs:/tigr.png");
    if (!logo) {
        tigrError(0, "Failed to load image: %d", errno);
        return 1;
    }

    char typed[5] = "";

    while (!tigrClosed(screen)) {
        liveTime += tigrTime();

        tigrMouse(screen, &x, &y, &buttons);
        int numTouches = tigrTouch(screen, touchPoints, 3);

        if (numTouches && y > screen->h - screen->h / 10) {
            tigrShowKeyboard(1);
        }

        if (tigrKeyDown(screen, TK_RETURN)) {
            tigrShowKeyboard(0);
        }

        if (tigrKeyDown(screen, TK_ESCAPE)) {
            break;
        }

        char ch = tigrReadChar(screen);
        if (ch != 0) {
            char *end = tigrEncodeUTF8(typed, ch);
            *end = 0;
        }
        
        tigrClear(screen, tigrRGB(0x80, 0x90, 0xa0));

        int logoX = (screen->w - logo->w) / 2;
        tigrBlitAlpha(screen, logo, logoX, 10, 0, 0, logo->w, logo->h, numTouches > 0 ? 0.5 : 1);

        TPixel mouseLineColor = tigrRGB(0, 0, 0);
        if (buttons != 0) {
            tigrLine(screen, 0, 0, x, y, mouseLineColor);
            tigrLine(screen, 0, screen->h - 1, x, y, mouseLineColor);
            tigrLine(screen, screen->w - 1, 0, x, y, mouseLineColor);
            tigrLine(screen, screen->w - 1, screen->h - 1, x, y, mouseLineColor);
        }

        TPixel touchLineColor = tigrRGB(0, 0x80, 0x80);
        for (int i = 0; i < numTouches; i++) {
            TigrTouchPoint* point = touchPoints + i;
            tigrLine(screen, 0, point->y, screen->w - 1, point->y, touchLineColor);
            tigrLine(screen, point->x, 0, point->x, screen->h - 1, touchLineColor);
        }

        int textX = (screen->w - textWidth) / 2;
        int textY = screen->h - (screen->h - textHeight) / 3;

        tigrPrint(screen, tfont, textX, textY, tigrRGB(0xff, 0xff, 0xff), message);

        tigrPrint(screen, tfont, textX, textY + 20, tigrRGB(0xff, 0xff, 0xff), "%.2f", liveTime);
        
        tigrPrint(screen, tfont, textX, textY + 40, tigrRGB(0x00, 0xff, 0xff), typed);

        if (tigrKeyDown(screen, 'A')) {
            tigrFillRect(screen, 40, 20, 20, 20, tigrRGB(0x00, 0xff, 0xff));
        } else {
            tigrRect(screen, 40, 20, 20, 20, tigrRGB(0x00, 0xff, 0xff));
        }
        tigrPrint(screen, tfont, 50, 30, tigrRGB(0x00, 0x00, 0x00), "A");
        if (tigrKeyDown(screen, 'B')) {
            tigrFillRect(screen, 20, 40, 20, 20, tigrRGB(0x00, 0xff, 0xff));
        } else {
            tigrRect(screen, 20, 40, 20, 20, tigrRGB(0x00, 0xff, 0xff));
        }
        tigrPrint(screen, tfont, 30, 50, tigrRGB(0x00, 0x00, 0x00), "B");
        if (tigrKeyDown(screen, 'X')) {
            tigrFillRect(screen, 20, 0, 20, 20, tigrRGB(0x00, 0xff, 0xff));
        } else {
            tigrRect(screen, 20, 0, 20, 20, tigrRGB(0x00, 0xff, 0xff));
        }
        tigrPrint(screen, tfont, 30, 10, tigrRGB(0x00, 0x00, 0x00), "X");
        if (tigrKeyDown(screen, 'Y')) {
            tigrFillRect(screen, 0, 20, 20, 20, tigrRGB(0x00, 0xff, 0xff));
        } else {
            tigrRect(screen, 0, 20, 20, 20, tigrRGB(0x00, 0xff, 0xff));
        }
        tigrPrint(screen, tfont, 10, 30, tigrRGB(0x00, 0x00, 0x00), "Y");


        tigrUpdate(screen);
    }
    tigrFree(screen);

    return 0;
}
