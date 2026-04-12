#if !defined( PAGE_H )
#define PAGE_H 1

#include <stdbool.h>

typedef void (*PageFunc)(void);

typedef struct Page
{
    const char *name;
    PageFunc init;
    PageFunc deinit;
    PageFunc update;

    PageFunc irq4;
    PageFunc irq6;

    struct Page *next;
} Page;

void page_register(Page *page);
Page *page_find(const char *name);
Page *page_get_active();
void page_set_active(Page *page);
void page_set_next_active();
void page_update();

bool page_irq4();
bool page_irq6();

#define PAGE_REGISTER_IRQ(_name, _init, _update, _deinit, _irq4, _irq6) \
    static Page __page_ ## _name = { #_name, _init, _deinit, _update, _irq4, _irq6, NULL }; \
    __attribute__((constructor)) static void register_page_ ## _name() \
    { \
        page_register(&__page_ ## _name); \
    }

#define PAGE_REGISTER(_name, _init, _update, _deinit) PAGE_REGISTER_IRQ(_name, _init, _update, _deinit, NULL, NULL)

#endif // PAGE_H
