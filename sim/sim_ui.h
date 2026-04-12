
#ifndef SIM_UI_H
#define SIM_UI_H

class CommandQueue;

void UiInit(const char *title);
void UiSetCommandQueue(CommandQueue *queue);

void UiGameChanged();

bool UiBeginFrame();
void UiEndFrame();
void UiDraw();

#endif // SIM_UI_H
