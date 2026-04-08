#if !defined( PAGE_H )
#define PAGE_H 1

typedef void (*PageFunc)(void);

typedef struct Page
{
    const char *name;
    PageFunc init;
    PageFunc deinit;
    PageFunc update;

    struct Page *next;
} Page;

void page_register(Page *page);
Page *page_find(const char *name);
Page *page_get_active();
void page_set_active(Page *page);
void page_set_next_active();
void page_update();

#define PAGE_REGISTER(_name, _init, _update, _deinit) \
    static Page __page_ ## _name = { #_name, _init, _deinit, _update, NULL }; \
    __attribute__((constructor)) static void register_page_ ## _name() \
    { \
        page_register(&__page_ ## _name); \
    }

#endif // PAGE_H
