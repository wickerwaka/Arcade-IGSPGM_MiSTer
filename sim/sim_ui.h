
#ifndef SIM_UI_H
#define SIM_UI_H

class CommandQueue;

void ui_init(const char *title);
void ui_set_command_queue(CommandQueue *queue);

void ui_game_changed();

bool ui_begin_frame();
void ui_end_frame();
void ui_draw();

#endif // SIM_UI_H
